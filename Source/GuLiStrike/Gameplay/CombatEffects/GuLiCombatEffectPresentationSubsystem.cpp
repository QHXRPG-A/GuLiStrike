#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/CombatEffects/GuLiMissileClusterPresentation.h"
#include "Gameplay/CombatEffects/GuLiImpactBatchPresentation.h"
#include "Gameplay/CombatEffects/GuLiMuzzleBatchPresentation.h"
#include "Gameplay/CombatEffects/GuLiWingmanProjectilePresentation.h"
#include "Gameplay/CombatEffects/GuLiProjectileFlightPresentationProfile.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraDataChannel.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "NiagaraDataChannelAccessContext.h"
#include "NiagaraDataChannelAccessor.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraDataChannelFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCombatEffectVisuals, Log, All);
static TAutoConsoleVariable<int32> CVarGuLiCombatEffectVisuals(TEXT("gs.CombatEffects.Visuals"), 1,
	TEXT("Local combat VFX only: 0 disables rendering without changing server combat. Used for scoped A/B acceptance."));
static TAutoConsoleVariable<int32> CVarGuLiMissileClusterEnabled(TEXT("gs.MissileCluster.Enabled"), 1,
	TEXT("Production WM01 GPU rendering, enabled by default. 0 forces the fallback visual for diagnostics; gameplay is unchanged."));
static TAutoConsoleVariable<int32> CVarGuLiWingmanWarnings(TEXT("gs.WingmanFlight.Warnings"),1,
	TEXT("Local wingman ground warnings; 0 isolates flight presentation cost without changing authority."));

bool UGuLiCombatEffectPresentationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

void UGuLiCombatEffectPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	Collection.InitializeDependency<UGuLiCommanderLODSubsystem>();
	CommanderData = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	MissileClusters = NewObject<UGuLiMissileClusterPresentation>(this);
	ImpactBatches = NewObject<UGuLiImpactBatchPresentation>(this);
	MuzzleBatches = NewObject<UGuLiMuzzleBatchPresentation>(this);
	WingmanFlights = NewObject<UGuLiWingmanProjectilePresentation>(this);
	if (GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		Collection.InitializeDependency<UGuLiGroundWarningSubsystem>();
		GroundWarnings = GetWorld()->GetSubsystem<UGuLiGroundWarningSubsystem>();
	}
}

bool UGuLiCombatEffectPresentationSubsystem::IsTickable() const { return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer; }
TStatId UGuLiCombatEffectPresentationSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiCombatEffectPresentationSubsystem, STATGROUP_Tickables); }

void UGuLiCombatEffectPresentationSubsystem::Deinitialize()
{
	ResetVisuals(); PoseProviders.Reset(); MuzzleProviders.Reset();
	for (AGuLiFlightVisualActor* Actor : FlightActors) if (IsValid(Actor)) Actor->Destroy();
	FlightActors.Reset(); FreeFlightActors.Reset();
	Catalog = nullptr; CommanderData = nullptr; Super::Deinitialize();
}

void UGuLiCombatEffectPresentationSubsystem::ResetVisuals()
{
	TArray<FGuid> Ids; Visuals.GenerateKeyArray(Ids);
	for (const FGuid& Id : Ids) RemoveVisual(Id, true);
	GuidanceMembers.Reset();
	ShipPoseCache.Reset();
	BatchPoses.Reset(); BatchMuzzles.Reset();
	ResetLaserPool(); LaserVisualIds.Reset();
	ResetRogueUpgradePool();
	ResetMechanicalMuzzles();
	if (MissileClusters) MissileClusters->Reset();
	if (ImpactBatches) ImpactBatches->Reset();
	if (MuzzleBatches) MuzzleBatches->Reset();
#if WITH_EDITOR
	ReviewMuzzlePoses.Reset();
#endif
	if (WingmanFlights) WingmanFlights->Reset();
	if (Gunfire) { Gunfire->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Gunfire); Gunfire->ReleaseToPool(); Gunfire = nullptr; }
	for (const auto& Item : Retiring) if (IsValid(Item.Component)) { Item.Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Item.Component); Item.Component->ReleaseToPool(); }
	Retiring.Reset(); SpawnedEffectIds.Reset(); PendingShots.Reset(); ActiveMuzzles.Reset(); SeenShots.Reset(); ShotOrder.Reset(); Tombstones.Reset(); TombstoneOrder.Reset();
	GunfireBounds = FBox(ForceInit);
	PreviousGunfireBounds = FBox(ForceInit);
	GunfireBoundsResetTime = 0.0f;
	NextMuzzleRefreshTime = 0.0f;
}

void UGuLiCombatEffectPresentationSubsystem::BeginEpoch(uint32 NewEpoch)
{
	// The battle derives opaque epochs from the low cycle-counter bits, which can
	// wrap. Remember retired identities instead of comparing their numeric order.
	if (NewEpoch == 0 || NewEpoch == Epoch || RetiredEpochs.Contains(NewEpoch)) return;
	if (Epoch) RetiredEpochs.Add(Epoch);
	ResetVisuals(); Epoch = NewEpoch; ClientFlights.BeginEpoch(Epoch); Counters = {};
}

float UGuLiCombatEffectPresentationSubsystem::ServerTime() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

UGuLiCombatEffectCatalog* UGuLiCombatEffectPresentationSubsystem::GetCatalog()
{
	if (!Catalog)
	{
		Catalog = GetDefault<UGuLiCombatEffectSettings>()->Catalog.LoadSynchronous();
	}
	return Catalog;
}

#if WITH_EDITOR
bool UGuLiCombatEffectPresentationSubsystem::SetReviewImpactChannel(UNiagaraDataChannelAsset* Channel)
{
	if (!GetWorld() || GetWorld()->WorldType!=EWorldType::PIE || !Channel || !GetCatalog()) return false;
	if (Catalog->GetOuter()!=this) Catalog=DuplicateObject<UGuLiCombatEffectCatalog>(Catalog,this);
	Catalog->ImpactChannel=Channel; ImpactBatches->ResetPreparation(); ImpactBatches->Prepare(Catalog); return true;
}
bool UGuLiCombatEffectPresentationSubsystem::SetReviewMuzzleChannel(UNiagaraDataChannelAsset* Channel)
{
	if (!GetWorld() || GetWorld()->WorldType!=EWorldType::PIE || !Channel || !GetCatalog() || !MuzzleBatches) return false;
	if (Catalog->GetOuter()!=this) Catalog=DuplicateObject<UGuLiCombatEffectCatalog>(Catalog,this);
	Catalog->MuzzleChannel=Channel; MuzzleBatches->ResetPreparation(); MuzzleBatches->Prepare(Catalog); return true;
}
bool UGuLiCombatEffectPresentationSubsystem::EmitReviewMuzzleInput(int32 Identity,FVector Location,FRotator Rotation,bool Heavy,int32 Mode,FVector FollowVelocity)
{
	if (!GetWorld() || !GetWorld()->IsPlayInEditor() || !MuzzleBatches || Identity<=0 || Location.ContainsNaN() || FollowVelocity.ContainsNaN()) return false;
	FGuLiCombatShotCue Cue; Cue.ShotId=FGuid(0x47534d5a,Identity,0,1); Cue.MatchEpoch=Epoch;
	Cue.Source.Kind=EGuLiTargetKind::CommanderSoldier; Cue.UnitTypeId=Heavy ? 2 : 1;
	Cue.SlotId=TEXT("ReviewMuzzle"); Cue.MuzzleIndex=Identity%2; Cue.bMechanicalShot=true;
	Cue.ServerTime=ServerTime(); Cue.MechanicalPoseTimeSeconds=Cue.ServerTime;
	MuzzleBatches->Prepare(GetCatalog());
	if (!MuzzleBatches->Enqueue(Cue,Mode)) return false;
	ReviewMuzzlePoses.Add(Cue.ShotId,{FTransform(Rotation,Location),FollowVelocity,GetWorld()->GetTimeSeconds()});
	return true;
}
bool UGuLiCombatEffectPresentationSubsystem::ResetReviewMuzzles()
{
	if (!GetWorld() || !GetWorld()->IsPlayInEditor() || !MuzzleBatches) return false;
	MuzzleBatches->Reset(); ReviewMuzzlePoses.Reset(); return true;
}
bool UGuLiCombatEffectPresentationSubsystem::RemoveReviewMuzzleSource(int32 Identity)
{
	return GetWorld() && GetWorld()->IsPlayInEditor()
		&& ReviewMuzzlePoses.Remove(FGuid(0x47534d5a,Identity,0,1))>0;
}
bool UGuLiCombatEffectPresentationSubsystem::AdvanceReviewEffectEpoch()
{
	if (!GetWorld() || !GetWorld()->IsPlayInEditor()) return false;
	uint32 Next=Epoch+1;
	while (Next==0 || RetiredEpochs.Contains(Next)) ++Next;
	BeginEpoch(Next); return Epoch==Next;
}
FString UGuLiCombatEffectPresentationSubsystem::GetMuzzleProtocolSnapshot() const
{ return MuzzleBatches ? MuzzleBatches->GetProtocolSnapshot() : TEXT("{}"); }
bool UGuLiCombatEffectPresentationSubsystem::SetReviewFlightProfile(UGuLiProjectileEffectDefinition* Definition,UGuLiProjectileFlightPresentationProfile* Profile)
{
	if (!GetWorld() || GetWorld()->WorldType!=EWorldType::PIE || !Definition || !Profile || !Profile->IsValidProfile()) return false;
	ReviewFlightProfiles.Add(Definition,Profile); WingmanFlights->Prepare(Profile);
	for (auto& Pair:Visuals) if (Pair.Value.LoadedDefinition==Definition && Pair.Value.State.Source.Kind==EGuLiTargetKind::Wingman) Pair.Value.LoadedFlightProfile=Profile;
	return true;
}
#endif

void UGuLiCombatEffectPresentationSubsystem::RegisterPoseResolver(EGuLiTargetKind Kind, UObject* Owner, FPoseResolver Resolver)
{
	BatchPoses.Reset(); BatchMuzzles.Reset();
	if (Owner && Resolver) PoseProviders.Add(Kind, {Owner, MoveTemp(Resolver)});
}

void UGuLiCombatEffectPresentationSubsystem::UnregisterPoseResolver(EGuLiTargetKind Kind, const UObject* Owner)
{
	BatchPoses.Reset(); BatchMuzzles.Reset();
	if (const auto* Provider = PoseProviders.Find(Kind); Provider && Provider->Owner.Get() == Owner) PoseProviders.Remove(Kind);
}

void UGuLiCombatEffectPresentationSubsystem::RegisterMuzzleResolver(EGuLiTargetKind Kind, UObject* Owner,
	FMuzzleResolver Resolver, FShotObserver Observer, FLaunchOffsetResolver LaunchOffset)
{
	BatchPoses.Reset(); BatchMuzzles.Reset();
	if (Owner && Resolver) MuzzleProviders.Add(Kind, {Owner, MoveTemp(Resolver), MoveTemp(Observer), MoveTemp(LaunchOffset)});
}

void UGuLiCombatEffectPresentationSubsystem::UnregisterMuzzleResolver(EGuLiTargetKind Kind, const UObject* Owner)
{
	BatchPoses.Reset(); BatchMuzzles.Reset();
	if (const auto* Provider = MuzzleProviders.Find(Kind); Provider && Provider->Owner.Get() == Owner) MuzzleProviders.Remove(Kind);
}

bool UGuLiCombatEffectPresentationSubsystem::TryGetWeaponAim(const FGuLiTargetHandle& Source, FName SlotId, FVector& Target) const
{
	const auto* Muzzle = ActiveMuzzles.Find({Source, SlotId, 0});
	if (!Muzzle || ServerTime() > Muzzle->ExpireServerTime) return false;
	Target = Muzzle->Cue.End;
	ResolveTargetPosition(Muzzle->Cue, Target);
	return !Target.ContainsNaN();
}

bool UGuLiCombatEffectPresentationSubsystem::ResolvePose(const FGuLiTargetHandle& Target, FTransform& Transform, int32& UnitTypeId) const
{
	if (bResolvingPresentationBatch)
		if (const auto* Cached = BatchPoses.Find(Target))
		{ ++Counters.PoseCacheHits; Transform = Cached->Transform; UnitTypeId = Cached->UnitTypeId; return Cached->bSuccess; }
	++Counters.PoseCacheMisses;
	FResolvedPose Resolved;
	if (Target.Kind == EGuLiTargetKind::Ship || Target.Kind == EGuLiTargetKind::GroundActor)
	{
		AActor* Actor = ShipPoseCache.FindRef(Target).Get();
		if (!Actor)
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				if (const auto* Health = It->FindComponentByClass<UGuLiCombatHealthComponent>(); Health && Health->GetTargetHandle() == Target)
				{ Actor = *It; ShipPoseCache.Add(Target, Actor); break; }
		if (Actor)
			if (const auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>(); Health && Health->IsAlive() && Health->GetTargetHandle() == Target)
			{ Resolved.Transform = Actor->GetActorTransform(); Resolved.bSuccess = !Resolved.Transform.ContainsNaN(); }
	}
	else if (const auto* Provider = PoseProviders.Find(Target.Kind); Provider && Provider->Owner.IsValid() && Provider->Resolve)
		Resolved.bSuccess = Provider->Resolve(Target, Resolved.Transform, Resolved.UnitTypeId) && !Resolved.Transform.ContainsNaN();
	if (bResolvingPresentationBatch) BatchPoses.Add(Target, Resolved);
	Transform = Resolved.Transform; UnitTypeId = Resolved.UnitTypeId;
	return Resolved.bSuccess;
}

bool UGuLiCombatEffectPresentationSubsystem::ResolveMuzzlePosition(const FGuLiCombatShotCue& Cue, FVector& Position) const
{
	FTransform Transform;
	float RenderTime = 0.0f;
	if (!ResolveMuzzleTransform(Cue, Transform, RenderTime)) return false;
	Position = Transform.GetLocation();
	return true;
}

bool UGuLiCombatEffectPresentationSubsystem::ResolveMuzzleTransform(const FGuLiCombatShotCue& Cue, FTransform& Transform, float& RenderTime) const
{
	const FMuzzleCacheKey Key{Cue};
	if (bResolvingPresentationBatch)
		if (const auto* Cached = BatchMuzzles.Find(Key))
		{ ++Counters.PoseCacheHits; Transform = Cached->Transform; RenderTime = Cached->RenderTime; return Cached->bSuccess; }
	++Counters.PoseCacheMisses;
	FResolvedMuzzle Resolved;
	if (const auto* Provider = MuzzleProviders.Find(Cue.Source.Kind); Provider && Provider->Owner.IsValid() && Provider->Resolve)
		Resolved.bSuccess = Provider->Resolve(Cue, Resolved.Transform, Resolved.RenderTime) && !Resolved.Transform.ContainsNaN();
	if (!Resolved.bSuccess && !Cue.bMechanicalShot)
	{
		FTransform Pose; int32 Type = Cue.UnitTypeId;
		if (ResolvePose(Cue.Source, Pose, Type))
		{
			Resolved.Transform = FTransform((FVector(Cue.End)-FVector(Cue.Start)).Rotation(), Pose.TransformPosition(Cue.MuzzleOffset));
			Resolved.RenderTime = ServerTime(); Resolved.bSuccess = !Resolved.Transform.ContainsNaN();
		}
	}
	if (bResolvingPresentationBatch) BatchMuzzles.Add(Key, Resolved);
	Transform = Resolved.Transform; RenderTime = Resolved.RenderTime;
	return Resolved.bSuccess;
}

bool UGuLiCombatEffectPresentationSubsystem::ResolveTargetPosition(const FGuLiCombatShotCue& Cue, FVector& Position) const
{
	FTransform TargetPose;
	int32 TargetUnitType = 0;
	if (!ResolvePose(Cue.Target, TargetPose, TargetUnitType)) return false;
	const FVector* AimOffset = CommanderData && CommanderData->IsWeaponMountCatalogValid()
		&& TargetUnitType > 0 && TargetUnitType <= MAX_uint16
		? CommanderData->FindAimOffset(static_cast<uint16>(TargetUnitType)) : nullptr;
	Position = TargetPose.TransformPosition(AimOffset ? *AimOffset : FVector::ZeroVector);
	return !Position.ContainsNaN();
}

void UGuLiCombatEffectPresentationSubsystem::ResolveShotEndpoints(const FGuLiCombatShotCue& Cue, FVector& Start, FVector& End) const
{
	FVector PresentedStart;
	FVector PresentedEnd;
	// Both ends must come from the same presentation tick. Never mix one freshly
	// resolved endpoint with the other endpoint from the accepted server cue.
	if (ResolveMuzzlePosition(Cue, PresentedStart) && ResolveTargetPosition(Cue, PresentedEnd))
	{
		Start = PresentedStart;
		End = PresentedEnd;
		return;
	}
	Start = Cue.Start;
	End = Cue.End;
}

double UGuLiCombatEffectPresentationSubsystem::ClosestLocalCameraDistanceSquared(FVector Location) const
{
	double Closest = TNumericLimits<double>::Max();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController()) continue;
		FVector Camera; FRotator Rotation; PC->GetPlayerViewPoint(Camera, Rotation);
		Closest = FMath::Min(Closest, FVector::DistSquared(Camera, Location));
	}
	return Closest;
}

bool UGuLiCombatEffectPresentationSubsystem::IsVisibleLocation(FVector Location) const
{
	if (!Catalog || Location.ContainsNaN()) return false;
	if (auto* LOD = GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
		return LOD->ShouldRenderWorldEffect(Location, Catalog->MaximumVisualDistance);
	return ClosestLocalCameraDistanceSquared(Location)
		<= FMath::Square(static_cast<double>(Catalog->MaximumVisualDistance));
}

bool UGuLiCombatEffectPresentationSubsystem::IsVisibleBounds(const FBox& Bounds) const
{
	if (!Catalog || !Bounds.IsValid) return false;
	if (auto* LOD = GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
		return LOD->ShouldRenderWorldEffectBounds(Bounds, Catalog->MaximumVisualDistance);
	return IsVisibleLocation(Bounds.GetCenter());
}

bool UGuLiCombatEffectPresentationSubsystem::GetSystemWorldBounds(UNiagaraSystem* System, const FTransform& Transform, FBox& OutBounds) const
{
	if (!System || Transform.ContainsNaN()) return false;
	FBox Bounds=System->GetFixedBounds();
	if (!System->bFixedBounds || !Bounds.IsValid)
	{
		const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.GuLiPreSpawnBoundsExtent"));
		const auto& Store=System->GetExposedParameters();
		if (Store.IndexOf(Parameter)==INDEX_NONE) return false;
		const FVector Extent(Store.GetParameterValue<FVector3f>(Parameter));
		if (Extent.ContainsNaN() || Extent.GetMin()<=0) return false;
		Bounds=FBox(-Extent,Extent);
	}
	OutBounds=Bounds.TransformBy(Transform); return OutBounds.IsValid && !OutBounds.Min.ContainsNaN() && !OutBounds.Max.ContainsNaN();
}

bool UGuLiCombatEffectPresentationSubsystem::IsVisibleSystemBounds(UNiagaraSystem* System, const FTransform& Transform) const
{
	if (!System || Transform.ContainsNaN()) return false;
	FBox Bounds;
	// Unknown dynamic templates retain conservative admission; owned candidates
	// carry a certified envelope independent of Niagara's simulation bounds.
	return !GetSystemWorldBounds(System,Transform,Bounds) || IsVisibleBounds(Bounds);
}

UNiagaraComponent* UGuLiCombatEffectPresentationSubsystem::SpawnPooled(
	int32 VfxId, FVector Location, float DynamicScale, float Radius, FRotator Rotation, FName ScaleParameterName)
{
	if (VfxId == 0 || CVarGuLiCombatEffectVisuals.GetValueOnGameThread() == 0) return nullptr;
	UNiagaraSystem* System = GuLiVfx::Load<UNiagaraSystem>(this, VfxId);
	const FVector Scale = GuLiVfx::Scale(this, VfxId, FVector(DynamicScale));
	if (!System || !UGuLiVfxRegistrySubsystem::IsValidScale(Scale)) return nullptr;
	if ((VfxId==GuLiVfxIds::GroundMachineGunMuzzle || (Catalog && VfxId==Catalog->MachineGunImpact.VfxId))
		&& !IsVisibleSystemBounds(System,FTransform(Rotation,Location,Scale))) return nullptr;
	if (GuLiClientPresentation::ThreeTierEffectsEnabled() && (VfxId==GuLiVfxIds::GroundMachineGunMuzzle || (Catalog && VfxId==Catalog->MachineGunImpact.VfxId)))
	{
		FBox Bounds;
		if (GetSystemWorldBounds(System,FTransform(Rotation,Location,Scale),Bounds))
			if (auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>())
			{
				FGuLiCommanderLODQuery Query; Query.Bounds=Bounds;
				const auto Decision=LOD->EvaluateWorldEffectBounds(Query,Catalog ? Catalog->MaximumVisualDistance : 20000);
				if (auto* Registry=GetWorld()->GetSubsystem<UGuLiVfxRegistrySubsystem>())
					if (auto* Selected=Registry->LoadNiagaraForLOD(VfxId,Decision.TargetLevel)) System=Selected;
			}
	}
	if (!ScaleParameterName.IsNone() && (!FMath::IsNearlyEqual(Scale.X, Scale.Y) || !FMath::IsNearlyEqual(Scale.X, Scale.Z)))
	{
		UE_LOG(LogGuLiCombatEffectVisuals, Error, TEXT("VfxId %d requires uniform scale for its Niagara float parameter."), VfxId);
		return nullptr;
	}
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Location,
		Rotation, ScaleParameterName.IsNone() ? Scale : FVector::OneVector, false, false, ENCPoolMethod::ManualRelease, false);
	if (Component)
	{
		SpawnedEffectIds.Add(Component,VfxId);
		if (auto* Overview = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Component);
		Component->SetCastShadow(false);
		if (!ScaleParameterName.IsNone()) Component->SetVariableFloat(ScaleParameterName, Scale.X);
		Component->SetVariableFloat(TEXT("User.Radius"), Radius);
		Component->SetVariableLinearColor(TEXT("User.Tint"), Catalog ? Catalog->GunfireTint : FLinearColor::White);
		Component->Activate(true);
	}
	return Component;
}

void UGuLiCombatEffectPresentationSubsystem::Retire(UNiagaraComponent* Component, float Seconds, bool bDeactivate)
{
	if (!IsValid(Component)) return;
	for (const auto& Item : Retiring) if (Item.Component==Component) return;
	if (bDeactivate) Component->Deactivate();
	auto& Item=Retiring.AddDefaulted_GetRef(); Item.Component=Component;
	Item.ReleaseTime=GetWorld()->GetTimeSeconds()+FMath::Max(0.f,Seconds);
	Item.VfxId=SpawnedEffectIds.FindRef(Component); SpawnedEffectIds.Remove(Component);
	GetSystemWorldBounds(Component->GetAsset(),Component->GetComponentTransform(),Item.Bounds);
}

void UGuLiCombatEffectPresentationSubsystem::RemoveVisual(const FGuid& Id, bool bImmediate)
{
	FGuLiLocalCombatEffect Visual;
	if (!Visuals.RemoveAndCopyValue(Id, Visual)) return;
	RemoveGuidanceMember(Visual.State);
	if (MissileClusters) MissileClusters->Finish(Id, Visual.RenderLocation, bImmediate);
	if (WingmanFlights) WingmanFlights->Finish(Id, Visual.RenderLocation, bImmediate);
	if (GroundWarnings) GroundWarnings->RemoveWarning(Id);
	if (Visual.LaserSlot != INDEX_NONE) FreeLaserSlot(Visual.LaserSlot);
	LaserVisualIds.Remove(Id);
	ReleaseFlightActor(Visual.FlightActor);
	ClientFlights.Release(Visual.FlightHandle);
	float Tail = 0.5f;
	if (UGuLiProjectileEffectDefinition* Definition = Visual.State.ProjectileDefinition.Get()) Tail = Definition->TrailFadeSeconds;
	Retire(Visual.Flight, bImmediate ? 0 : Tail);
	Retire(Visual.Waiting, bImmediate ? 0 : 0.2f);
	Retire(Visual.ActiveLoop, bImmediate ? 0 : 0.5f);
}

void UGuLiCombatEffectPresentationSubsystem::ApplyState(const FGuLiCombatEffectState& State, bool bFromSnapshot)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	++Counters.ReceivedStates;
	if (!State.IsWellFormed()) { ++Counters.RejectedStates; return; }
	if (State.MatchEpoch != Epoch) BeginEpoch(State.MatchEpoch);
	if (State.MatchEpoch != Epoch) { ++Counters.RejectedStates; return; }
	if (Tombstones.Contains(State.EffectId)) { ++Counters.RejectedStates; return; }
	FGuLiLocalCombatEffect* Existing = Visuals.Find(State.EffectId);
	// A snapshot batch may arrive before the live multicast in the same network frame.
	// Only the multicast authorizes a fresh one-shot; late-join snapshots never replay it.
	if (Existing && !bFromSnapshot && State.Kind == EGuLiCombatEffectKind::SpellField
		&& State.Phase != EGuLiCombatEffectPhase::Finished && Existing->bSuppressOldBurst
		&& ServerTime() - State.ActivationTime < 0.5f)
	{
		Existing->bSuppressOldBurst = false;
		Existing->bActivationPlayed = false;
	}
	if (Existing && !GuLiCombatEffects::IsNewerState(State, Existing->State)) { ++Counters.RejectedStates; return; }
	if (State.Phase == EGuLiCombatEffectPhase::Finished)
	{
		if (State.Kind == EGuLiCombatEffectKind::Projectile && MissileClusters)
			MissileClusters->Finish(State.EffectId, State.Location, State.EndReason == EGuLiCombatEffectEndReason::EpochEnded);
		if (State.Kind == EGuLiCombatEffectKind::Projectile && WingmanFlights)
			WingmanFlights->Finish(State.EffectId, State.Location, State.EndReason == EGuLiCombatEffectEndReason::EpochEnded);
		if (State.Kind == EGuLiCombatEffectKind::LinearProjectile && Existing)
		{
			Existing->State.Location = State.Location; Existing->State.Phase = State.Phase;
			Existing->State.EndReason = State.EndReason; Existing->State.SampleTime = State.SampleTime;
			Existing->State.Sequence = State.Sequence;
			Existing->bActivationPlayed = bFromSnapshot;
			Existing->LaserFadeUntil = GetWorld()->GetTimeSeconds() + 0.04f;
		}
		else RemoveVisual(State.EffectId, State.EndReason == EGuLiCombatEffectEndReason::EpochEnded);
		Tombstones.Add(State.EffectId, State.Sequence); TombstoneOrder.Add(State.EffectId);
		if (TombstoneOrder.Num() > 8192)
		{
			for (int32 Index = 0; Index < 1024; ++Index) Tombstones.Remove(TombstoneOrder[Index]);
			TombstoneOrder.RemoveAt(0, 1024, EAllowShrinking::No);
		}
		// Terminal wire records deliberately omit the source and launch payload. A fresh
		// hit must work even if its flight was culled, expired locally, or never received.
		if (!bFromSnapshot) PlayMachineGunImpact(State);
		return;
	}
	// Never resurrect an expired projectile/burst or replay one after a long network stall.
	if ((State.Kind == EGuLiCombatEffectKind::Projectile
		|| State.Kind == EGuLiCombatEffectKind::LinearProjectile
		|| State.Kind == EGuLiCombatEffectKind::SustainedHitscan)
		&& ServerTime() >= State.EndTime) return;
	GetCatalog();
	if (!Existing)
	{
		FGuLiLocalCombatEffect Visual; Visual.State = State; Visual.RenderLocation = State.Location;
		Visual.LoadedDefinition = State.ProjectileDefinition.LoadSynchronous();
		Visual.bUsesMissileCluster = Visual.LoadedDefinition && Visual.LoadedDefinition->UsesMissileClusterRendering();
		if (Visual.LoadedDefinition && State.Source.Kind==EGuLiTargetKind::Wingman)
		{
			Visual.LoadedFlightProfile=Visual.LoadedDefinition->FlightPresentationProfile.LoadSynchronous();
			if (const auto* ReviewProfile=ReviewFlightProfiles.Find(Visual.LoadedDefinition)) Visual.LoadedFlightProfile=*ReviewProfile;
			if (WingmanFlights) WingmanFlights->Prepare(Visual.LoadedFlightProfile);
		}
		Visual.LoadedFlightSystem = GuLiVfx::Load<UNiagaraSystem>(this, Visual.LoadedDefinition ? Visual.LoadedDefinition->FlightVfxId : State.PlayerBulletVfxId);
		// Snapshot flights are already in progress; never pull them back toward a muzzle.
		Visual.bLaunchVisualOffsetResolved = bFromSnapshot;
		if (State.Kind == EGuLiCombatEffectKind::LinearProjectile)
		{
			if (State.Source.Kind != EGuLiTargetKind::GroundActor && State.Source.Kind != EGuLiTargetKind::Ship && Catalog)
				Visual.LaserSlot = AllocateLaserSlot(State.Source.Kind == EGuLiTargetKind::CommanderSoldier
					? Catalog->GroundMachineGunVfxId : Catalog->WingmanLaserVfxId);
			if (State.Source.Kind == EGuLiTargetKind::Wingman && !bFromSnapshot && ServerTime() - State.StartTime < 0.35f)
				Visual.LaserMuzzleUntil = GetWorld()->GetTimeSeconds() + (Catalog ? Catalog->LaserMuzzleSeconds : 0.05f);
		}
		if (bFromSnapshot && State.Kind == EGuLiCombatEffectKind::SustainedHitscan)
		{
			const double Elapsed = FMath::Clamp(
				static_cast<double>(ServerTime() - State.StartTime), 0.0,
				static_cast<double>(State.EndTime - State.StartTime));
			Visual.NextGunShotOrdinal = FMath::Max(0,
				FMath::FloorToInt(Elapsed * State.FireRateHz + 1.e-6) + 1);
		}
		Visual.bSuppressOldBurst = bFromSnapshot && State.Kind == EGuLiCombatEffectKind::SpellField
			&& State.Phase != EGuLiCombatEffectPhase::Waiting;
		Visuals.Add(State.EffectId, MoveTemp(Visual));
	}
	else
	{
		if (Existing->State.GuidanceBatchId != State.GuidanceBatchId) RemoveGuidanceMember(Existing->State);
		Existing->State = State;
	}
	if (const auto* Visual = Visuals.Find(State.EffectId); Visual && Visual->LaserSlot != INDEX_NONE) LaserVisualIds.Add(State.EffectId);
	UpdateGroundWarning(State, CVarGuLiCombatEffectVisuals.GetValueOnGameThread() != 0);
	if (State.Kind == EGuLiCombatEffectKind::Projectile && State.GuidanceBatchId.IsValid())
	{
		GuidanceMembers.FindOrAdd(State.GuidanceBatchId).Add(State.EffectId);
		RefreshGuidanceWarning(State.GuidanceBatchId, CVarGuLiCombatEffectVisuals.GetValueOnGameThread() != 0);
	}
}

void UGuLiCombatEffectPresentationSubsystem::RemoveGuidanceMember(const FGuLiCombatEffectState& State)
{
	if (auto* Members = GuidanceMembers.Find(State.GuidanceBatchId))
	{
		Members->Remove(State.EffectId);
		if (Members->IsEmpty())
		{
			if (GroundWarnings) GroundWarnings->RemoveWarning(State.GuidanceBatchId);
			GuidanceMembers.Remove(State.GuidanceBatchId);
		}
		else RefreshGuidanceWarning(State.GuidanceBatchId, CVarGuLiCombatEffectVisuals.GetValueOnGameThread() != 0);
	}
}

void UGuLiCombatEffectPresentationSubsystem::RefreshGuidanceWarning(const FGuid& BatchId, bool bEnabled)
{
	if (!GroundWarnings) return;
	const auto* Members = GuidanceMembers.Find(BatchId);
	FGuLiGroundWarningParams Params;
	bool bHasLiveMissile = false;
	const double Now = ServerTime();
	if (bEnabled && Members) for (const FGuid& Id : *Members)
	{
		const auto* Visual = Visuals.Find(Id);
		if (!Visual || Visual->State.Phase == EGuLiCombatEffectPhase::Finished || Now >= Visual->State.EndTime) continue;
		const auto& State = Visual->State;
		if (!bHasLiveMissile)
		{
			Params.Location = State.GuidanceCenter; Params.Radius = State.GuidanceRadius;
			Params.Style = State.GroundWarningStyle.LoadSynchronous(); Params.PoolGroupId = BatchId;
			Params.StartServerSeconds = State.StartTime; Params.ExpireServerSeconds = State.EndTime;
			bHasLiveMissile = true;
		}
		else
		{
			Params.StartServerSeconds = FMath::Min(Params.StartServerSeconds, double(State.StartTime));
			Params.ExpireServerSeconds = FMath::Max(Params.ExpireServerSeconds, double(State.EndTime));
		}
	}
	if (bHasLiveMissile) GroundWarnings->UpsertWarning(BatchId, Params);
	else GroundWarnings->RemoveWarning(BatchId);
}

void UGuLiCombatEffectPresentationSubsystem::PlayMachineGunImpact(const FGuLiCombatEffectState& State)
{
	if (State.Kind != EGuLiCombatEffectKind::LinearProjectile
		|| (State.EndReason != EGuLiCombatEffectEndReason::Impact && State.EndReason != EGuLiCombatEffectEndReason::Blocked)
		|| ServerTime() - State.SampleTime > 0.5f || !GetCatalog()) return;
	const FGuLiEffectVisualVariant& Impact = Catalog->MachineGunImpact;
	if (Impact.VfxId > 0 && FMath::IsFinite(Impact.MaximumLifetime) && Impact.MaximumLifetime > 0.0f)
	{
		if (ImpactBatches)
		{
			ImpactBatches->Prepare(Catalog);
			FGuLiImpactEvent Event;
			Event.Identity={State.EffectId,State.MatchEpoch,State.Sequence,0};
			Event.Position=State.Location; Event.Scale=GuLiVfx::Scale(this,Impact.VfxId);
			Event.VfxId=Impact.VfxId; Event.Seed=State.RandomSeed;
			Event.Tint=Catalog->GunfireTint; Event.Lifetime=FMath::Min(Impact.MaximumLifetime,3.f);
			if (ImpactBatches->Enqueue(Event)) ++Counters.MachineGunImpactsPlayed;
		}
	}
}

void UGuLiCombatEffectPresentationSubsystem::UpdateGroundWarning(const FGuLiCombatEffectState& State, bool bEnabled)
{
	if (!GroundWarnings) return;
	if (!bEnabled || State.Kind != EGuLiCombatEffectKind::Projectile || State.GroundWarningStyle.IsNull()
		|| (State.Source.Kind==EGuLiTargetKind::Wingman && CVarGuLiWingmanWarnings.GetValueOnGameThread()==0)
		|| State.Phase == EGuLiCombatEffectPhase::Finished || ServerTime() >= State.EndTime)
	{ GroundWarnings->RemoveWarning(State.EffectId); return; }
	FGuLiGroundWarningParams Params;
	Params.Location = State.LastTargetLocation; Params.Radius = State.Radius;
	Params.Style = State.GroundWarningStyle.LoadSynchronous();
	Params.StartServerSeconds = State.StartTime; Params.ExpireServerSeconds = State.EndTime;
	GroundWarnings->UpsertWarning(State.EffectId, Params);
}

void UGuLiCombatEffectPresentationSubsystem::QueueSustainedGunfire(const float Now, const bool bEnabled)
{
	for (auto& Pair : Visuals)
	{
		FGuLiLocalCombatEffect& Visual = Pair.Value;
		const FGuLiCombatEffectState& State = Visual.State;
		if (State.Kind != EGuLiCombatEffectKind::SustainedHitscan
			|| Now < State.StartTime || Now >= State.EndTime || State.FireRateHz <= 0.0f)
		{
			continue;
		}
		const int32 LatestOrdinal = FMath::FloorToInt(
			static_cast<double>(Now - State.StartTime) * State.FireRateHz + 1.e-6);
		if (LatestOrdinal < Visual.NextGunShotOrdinal) continue;
		// A stalled or late-joining client jumps directly to the latest scheduled shot.
		// Historical tracers are deliberately never replayed.
		Visual.NextGunShotOrdinal = LatestOrdinal + 1;
		// Wingman bolts now arrive as actual pooled launch records; never also draw a full hitscan segment.
		if (State.Source.Kind == EGuLiTargetKind::Wingman) continue;
		if (!bEnabled || PendingShots.Num() >= 2048) continue;
		FGuLiCombatShotCue Cue;
		Cue.MatchEpoch = State.MatchEpoch;
		Cue.ShotId = GuLiCombatEffects::DamageId(State.EffectId, LatestOrdinal, State.Target);
		Cue.Source = State.Source;
		Cue.Target = State.Target;
		Cue.SlotId = State.SlotId;
		Cue.MuzzleOffset = State.MuzzleOffset;
		Cue.KeepAliveSeconds = FMath::Clamp(0.75f / State.FireRateHz, 0.01f, 0.15f);
		Cue.Start = State.Location;
		Cue.End = State.LastTargetLocation;
		Cue.ServerTime = Now;
		if (SeenShots.Contains(Cue.ShotId)) continue;
		SeenShots.Add(Cue.ShotId);
		ShotOrder.Add(Cue.ShotId);
		PendingShots.Add(Cue);
		const FActiveMuzzleKey Key{Cue.Source, Cue.SlotId, 0};
		FActiveMuzzleVisual& Muzzle = ActiveMuzzles.FindOrAdd(Key);
		Muzzle.Cue = Cue;
		const FVector Direction = (FVector(Cue.End) - FVector(Cue.Start)).GetSafeNormal();
		if (!Direction.IsNearlyZero()) Muzzle.LastDirection = Direction;
		Muzzle.ExpireServerTime = FMath::Max(Muzzle.ExpireServerTime, Now + Cue.KeepAliveSeconds);
		++Counters.SynthesizedGunShots;
	}
	if (ShotOrder.Num() > 8192)
	{
		const int32 RemoveCount = ShotOrder.Num() - 4096;
		for (int32 Index = 0; Index < RemoveCount; ++Index) SeenShots.Remove(ShotOrder[Index]);
		ShotOrder.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

void UGuLiCombatEffectPresentationSubsystem::ApplyCorrection(const FGuLiCombatEffectCorrection& Correction)
{
	// Loss or reordering may deliver a correction before the reliable create. Only
	// a create/snapshot may establish an effect; a correction can never resurrect it.
	auto* Visual = Visuals.Find(Correction.EffectId);
	if (Correction.MatchEpoch != Epoch || !Visual || Visual->State.Kind != EGuLiCombatEffectKind::Projectile
		|| Correction.Sequence <= Visual->State.Sequence || !FMath::IsFinite(Correction.SampleTime)
		|| Correction.SampleTime < Visual->State.SampleTime || Correction.Location.ContainsNaN()
		|| Correction.Velocity.ContainsNaN() || Correction.LastTargetLocation.ContainsNaN()) return;
	Visual->State.Sequence=Correction.Sequence; Visual->State.SampleTime=Correction.SampleTime;
	Visual->State.Location=Correction.Location; Visual->State.Velocity=Correction.Velocity;
	Visual->State.LastTargetLocation=Correction.LastTargetLocation;
}

void UGuLiCombatEffectPresentationSubsystem::ApplyShots(const TArray<FGuLiCombatShotCue>& Cues)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	const float Now = ServerTime();
	for (const auto& Cue : Cues)
	{
		if (Cue.MatchEpoch != Epoch) BeginEpoch(Cue.MatchEpoch);
		++Counters.ReceivedShots;
		if (Cue.MatchEpoch != Epoch || !Cue.ShotId.IsValid() || Cue.Start.ContainsNaN() || Cue.End.ContainsNaN()
			|| !FMath::IsFinite(Cue.ServerTime) || Now - Cue.ServerTime > 0.35f || Cue.ServerTime - Now > 0.5f
			|| SeenShots.Contains(Cue.ShotId))
		{ ++Counters.DroppedShots; continue; }
		SeenShots.Add(Cue.ShotId); ShotOrder.Add(Cue.ShotId);
		if (const auto* Provider = MuzzleProviders.Find(Cue.Source.Kind);
			Provider && Provider->Owner.IsValid() && Provider->Observe) Provider->Observe(Cue);
		if (Cue.bMechanicalShot)
		{
			if (MuzzleBatches) { MuzzleBatches->Prepare(GetCatalog()); if (!MuzzleBatches->Enqueue(Cue)) ++Counters.DroppedShots; }
			continue;
		}
		if (PendingShots.Num() >= 2048) { ++Counters.DroppedShots; continue; }
		if (!Cue.bMuzzleOnly) PendingShots.Add(Cue);
		const FActiveMuzzleKey Key{Cue.Source, Cue.SlotId, Cue.MuzzleIndex};
		FActiveMuzzleVisual& Muzzle = ActiveMuzzles.FindOrAdd(Key);
		if (!Muzzle.Cue.ShotId.IsValid() || Cue.ServerTime >= Muzzle.Cue.ServerTime)
		{
			Muzzle.Cue = Cue;
			const FVector Direction = (FVector(Cue.End) - FVector(Cue.Start)).GetSafeNormal();
			if (!Direction.IsNearlyZero()) Muzzle.LastDirection = Direction;
		}
		const float HoldSeconds = Cue.Source.Kind == EGuLiTargetKind::Wingman
            ? FMath::Clamp(Cue.KeepAliveSeconds, 0.01f, 0.15f) : (Catalog ? Catalog->MuzzleActivityHoldSeconds : 2.0f);
		Muzzle.ExpireServerTime = FMath::Max(Muzzle.ExpireServerTime, Now + HoldSeconds);
	}
	if (ShotOrder.Num() > 8192)
	{
		const int32 RemoveCount = ShotOrder.Num() - 4096;
		for (int32 Index = 0; Index < RemoveCount; ++Index) SeenShots.Remove(ShotOrder[Index]);
		ShotOrder.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

void UGuLiCombatEffectPresentationSubsystem::FlushGunfire()
{
	const float ServerNow = ServerTime();
	for (auto It = ActiveMuzzles.CreateIterator(); It; ++It)
	{
		if (ServerNow > It.Value().ExpireServerTime) It.RemoveCurrent();
	}
	if (!GetCatalog()) { PendingShots.Reset(); return; }
	const float LocalNow = GetWorld()->GetTimeSeconds();
	const float MuzzleInterval = 1.0f / FMath::Max(Catalog->MuzzleRefreshRate, 1.0f);
	const bool bRefreshMuzzles = !ActiveMuzzles.IsEmpty() && LocalNow >= NextMuzzleRefreshTime;
	if (PendingShots.IsEmpty() && !bRefreshMuzzles) return;
	if (CVarGuLiCombatEffectVisuals.GetValueOnGameThread() == 0) { PendingShots.Reset(); return; }
	UNiagaraDataChannelAsset* Channel = Catalog->GunfireChannel.LoadSynchronous();
	UNiagaraSystem* System = GuLiVfx::Load<UNiagaraSystem>(this, Catalog->GunfireVfxId);
	const FVector GunfireBaseScale = GuLiVfx::Scale(this, Catalog->GunfireVfxId);
	if (!Channel || !Channel->Get() || !System)
	{
		if (!bChannelWarning) { UE_LOG(LogGuLiCombatEffectVisuals, Warning, TEXT("Gunfire System/Data Channel is missing; no placeholder renderer will be spawned.")); bChannelWarning = true; }
		Counters.DroppedShots += PendingShots.Num(); PendingShots.Reset(); return;
	}
	if (LocalNow >= GunfireBoundsResetTime)
	{
		PreviousGunfireBounds = GunfireBounds;
		GunfireBounds = FBox(ForceInit);
		GunfireBoundsResetTime = LocalNow + 0.2f;
	}
	struct FGunfireRow
	{
		FVector Position = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float Length = 1.0f;
		float Width = 1.0f;
		float Lifetime = 0.075f;
		float VisualIntensity = 1.0f;
		float LightBrightness = 0.0f;
		float LightRadius = 0.0f;
		double CameraDistanceSquared = TNumericLimits<double>::Max();
		bool bStrobeOn = false;
		int32 Mode = 0; // 0: exact hitscan tracer, 1: persistent muzzle flame
	};
	TArray<FGunfireRow> Rows;
	Rows.Reserve(PendingShots.Num() + (bRefreshMuzzles ? ActiveMuzzles.Num() : 0));
	int32 TracerRows = 0;
	for (const FGuLiCombatShotCue& Cue : PendingShots)
	{
		FVector Start;
		FVector End;
		ResolveShotEndpoints(Cue, Start, End);
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (!FMath::IsFinite(Length) || Length <= 0.2f)
		{
			++Counters.DroppedShots;
			continue;
		}
		const double CameraDistanceSquared = FMath::Min(
			ClosestLocalCameraDistanceSquared(Start), ClosestLocalCameraDistanceSquared(End));
		if (!IsVisibleBounds(FBox(Start.ComponentMin(End), Start.ComponentMax(End)).ExpandBy(Catalog->TracerWidth*GunfireBaseScale.Y)))
		{ ++Counters.DroppedShots; continue; }
		FGunfireRow& Row = Rows.AddDefaulted_GetRef();
		Row.Direction = Delta / Length;
		Row.Length = Length * GunfireBaseScale.X;
		Row.Position = Start + Row.Direction * (Row.Length * 0.5f);
		Row.Width = Catalog->TracerWidth * GunfireBaseScale.Y;
		Row.Lifetime = Catalog->TracerLifetime;
		Row.VisualIntensity = 1.25f;
		Row.CameraDistanceSquared = CameraDistanceSquared;
		Row.Mode = 0;
		++TracerRows;
		GunfireBounds += Start;
		GunfireBounds += Start + Row.Direction * Row.Length;
	}
	PendingShots.Reset();
	if (bRefreshMuzzles)
	{
		NextMuzzleRefreshTime = LocalNow + MuzzleInterval;
		for (auto& Pair : ActiveMuzzles)
		{
			FActiveMuzzleVisual& Muzzle = Pair.Value;
			FVector Start = Muzzle.Cue.Start;
			ResolveMuzzlePosition(Muzzle.Cue, Start);
			FVector Target;
			if (ResolveTargetPosition(Muzzle.Cue, Target))
			{
				const FVector CurrentDirection = (Target - Start).GetSafeNormal();
				if (!CurrentDirection.IsNearlyZero()) Muzzle.LastDirection = CurrentDirection;
			}
			const double CameraDistanceSquared = ClosestLocalCameraDistanceSquared(Start);
			if (Muzzle.LastDirection.IsNearlyZero() || !IsVisibleBounds(FBox(Start.ComponentMin(Start+Muzzle.LastDirection*Catalog->MuzzleLength*GunfireBaseScale.X),
				Start.ComponentMax(Start+Muzzle.LastDirection*Catalog->MuzzleLength*GunfireBaseScale.X)).ExpandBy(Catalog->MuzzleWidth*GunfireBaseScale.Y))) continue;
			FGunfireRow& Row = Rows.AddDefaulted_GetRef();
			Row.Direction = Muzzle.LastDirection;
			Row.Length = Catalog->MuzzleLength * GunfireBaseScale.X;
			Row.Width = Catalog->MuzzleWidth * GunfireBaseScale.Y;
			Row.Lifetime = FMath::Max(Catalog->MuzzleParticleLifetime, MuzzleInterval * 1.5f);
			const float HashPhase = static_cast<float>(GetTypeHash(Pair.Key) & 0xffffu) / 65535.0f;
			const float StrobePhase = FMath::Frac(ServerNow * Catalog->MuzzleStrobeRate + HashPhase);
			Row.bStrobeOn = StrobePhase < Catalog->MuzzleStrobeDutyCycle;
			Row.VisualIntensity = Row.bStrobeOn ? 2.2f : 0.65f;
			Row.CameraDistanceSquared = CameraDistanceSquared;
			Row.Mode = 1;
			Row.Position = Start + Row.Direction * (Row.Length * 0.5f);
			GunfireBounds += Start;
			GunfireBounds += Start + Row.Direction * Row.Length;
		}
	}
	if (Rows.IsEmpty()) return;
	TArray<int32> TracerLightCandidates;
	TArray<int32> MuzzleLightCandidates;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].Mode == 0) TracerLightCandidates.Add(Index);
		else if (Rows[Index].bStrobeOn) MuzzleLightCandidates.Add(Index);
	}
	auto SortNearest = [&Rows](TArray<int32>& Indices)
	{
		Indices.Sort([&Rows](const int32 Lhs, const int32 Rhs)
		{ return Rows[Lhs].CameraDistanceSquared < Rows[Rhs].CameraDistanceSquared; });
	};
	SortNearest(TracerLightCandidates);
	SortNearest(MuzzleLightCandidates);
	for (int32 Index = 0; Index < FMath::Min(Catalog->MaximumTracerLightsPerFrame, TracerLightCandidates.Num()); ++Index)
	{
		FGunfireRow& Row = Rows[TracerLightCandidates[Index]];
		Row.LightBrightness = Catalog->TracerLightBrightness;
		Row.LightRadius = Catalog->TracerLightRadius * GunfireBaseScale.Z;
	}
	for (int32 Index = 0; Index < FMath::Min(Catalog->MaximumMuzzleLightsPerFrame, MuzzleLightCandidates.Num()); ++Index)
	{
		FGunfireRow& Row = Rows[MuzzleLightCandidates[Index]];
		Row.LightBrightness = Catalog->MuzzleLightBrightness;
		Row.LightRadius = Catalog->MuzzleLightRadius * GunfireBaseScale.Z;
	}
	if (!Gunfire) Gunfire = SpawnPooled(Catalog->GunfireVfxId, FVector::ZeroVector, 1.0f);
	if (!Gunfire) { Counters.DroppedShots += TracerRows; return; }
	// NDC supplies world-space positions and dimensions; its particle sizes above own the base scale.
	Gunfire->SetWorldScale3D(FVector::OneVector);
	FNDCAccessContextInst Access(Channel->Get()->GetAccessContextType());
	UNiagaraDataChannelWriter* Writer = UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel_WithContext(
		GetWorld(), Channel, Access, Rows.Num(), false, true, true, TEXT("GuLiCommanderGunfire"));
	if (!Writer) { Counters.DroppedShots += TracerRows; return; }
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FGunfireRow& Row = Rows[Index];
		Writer->WritePosition(TEXT("Position"), Index, Row.Position);
		Writer->WriteVector(TEXT("Direction"), Index, Row.Direction);
		Writer->WriteFloat(TEXT("Length"), Index, Row.Length);
		Writer->WriteInt(TEXT("Mode"), Index, Row.Mode);
		Writer->WriteLinearColor(TEXT("Tint"), Index, Catalog->GunfireTint);
		Writer->WriteFloat(TEXT("Lifetime"), Index, Row.Lifetime);
		Writer->WriteFloat(TEXT("Width"), Index, Row.Width);
		Writer->WriteFloat(TEXT("VisualIntensity"), Index, Row.VisualIntensity);
		Writer->WriteFloat(TEXT("LightBrightness"), Index, Row.LightBrightness);
		Writer->WriteFloat(TEXT("LightRadius"), Index, Row.LightRadius);
	}
	Gunfire->SetSystemFixedBounds((GunfireBounds + PreviousGunfireBounds).ExpandBy(20.0));
	Counters.WrittenShots += TracerRows;
}

void UGuLiCombatEffectPresentationSubsystem::UpdateField(FGuLiLocalCombatEffect& Visual, float Now)
{
	UGuLiSpellFieldDefinition* Definition = Visual.State.FieldDefinition.LoadSynchronous();
	if (!Definition) return;
	const FVector Position = Visual.State.Location;
	// Radius is frozen from SpellFields on the server. Scale every phase from the
	// authored reference radius so a table edit changes gameplay and visuals together.
	const float FieldScale = FMath::Clamp(Visual.State.Radius / FMath::Max(Definition->VisualReferenceRadius, 0.001f), 0.001f, 20.0f);
	const bool bVisible = IsVisibleLocation(Position);
	if (Now < Visual.State.ActivationTime)
	{
		if (!Visual.Waiting && bVisible) Visual.Waiting = SpawnPooled(Definition->WaitingVfxId, Position, FieldScale, Visual.State.Radius);
		return;
	}
	if (Visual.Waiting) { Retire(Visual.Waiting, 0.2f); Visual.Waiting = nullptr; }
	if (!Visual.bActivationPlayed)
	{
		Visual.bActivationPlayed = true;
		auto* LOD = GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>();
		const bool bBurstVisible = Catalog && LOD
			? LOD->ShouldRenderWorldEffect(Position, Catalog->MaximumVisualDistance) : bVisible;
		if (!Visual.bSuppressOldBurst && bBurstVisible && Definition->ActivationVariants.IsValidIndex(Visual.State.VariantIndex))
		{
			const auto& Variant = Definition->ActivationVariants[Visual.State.VariantIndex];
			if (Now - Visual.State.ActivationTime < 0.5f)
			{
				FRotator Rotation = FRotator::ZeroRotator;
				if (Variant.bRandomYaw)
				{
					FRandomStream Random(Visual.State.RandomSeed ^ 0x57494e47);
					Rotation.Yaw = Random.FRandRange(-180.0f, 180.0f);
				}
				if (UNiagaraComponent* Burst = SpawnPooled(Variant.VfxId, Position,
					FieldScale, Visual.State.Radius, Rotation, Variant.ScaleParameterName))
				{ Retire(Burst, Variant.MaximumLifetime, false); ++Counters.BurstsPlayed; }
				for (const FGuLiEffectVisualLayer& Layer : Variant.AdditionalLayers)
				{
					if (Layer.VfxId <= 0) continue;
					if (UNiagaraComponent* Burst = SpawnPooled(Layer.VfxId, Position,
						FieldScale, Visual.State.Radius, Rotation))
					{ Retire(Burst, Variant.MaximumLifetime, false); ++Counters.BurstsPlayed; }
				}
			}
		}
	}
	if (Now < Visual.State.EndTime && bVisible)
	{
		if (!Visual.ActiveLoop) Visual.ActiveLoop = SpawnPooled(Definition->ActiveLoopVfxId, Position, FieldScale, Visual.State.Radius);
	}
	else if (Visual.ActiveLoop) { Retire(Visual.ActiveLoop, Definition->DissipationSeconds); Visual.ActiveLoop = nullptr; }
}

void UGuLiCombatEffectPresentationSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCombatEffects_Presentation);
	FGuLiPerformanceScope Timing(GetWorld(), TEXT("Flight.PresentationMs"));
	const double Started = FPlatformTime::Seconds();
	const float Now = ServerTime();
	BatchPoses.Reset(); BatchMuzzles.Reset();
	TGuardValue<bool> ResolvingBatch(bResolvingPresentationBatch, true);
	const bool bEnabled = CVarGuLiCombatEffectVisuals.GetValueOnGameThread() != 0;
	const bool bClusterEnabled = bEnabled && CVarGuLiMissileClusterEnabled.GetValueOnGameThread() != 0;
	int32 PredictionCount = 0;
	int32 PredictionCountBySource[4] = {};
	auto* Capture = GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>();
	if (Capture && Capture->IsCapturing())
	{
		Capture->Record(TEXT("Flight.Records"), Visuals.Num());
		Capture->Record(TEXT("Flight.ActorActive"), FlightActors.Num()-FreeFlightActors.Num());
		Capture->Record(TEXT("Flight.DataActive"), ClientFlights.GetActiveCount());
	}
	if (MissileClusters) MissileClusters->BeginFrame(GetWorld()->GetTimeSeconds(), bClusterEnabled);
	if (WingmanFlights) WingmanFlights->BeginFrame(GetWorld()->GetTimeSeconds(),Now,bEnabled);
	GetCatalog(); QueueSustainedGunfire(Now, bEnabled); FlushGunfire();
	if (!bEnabled && Gunfire) { Retire(Gunfire, 0); Gunfire = nullptr; }
	TArray<FGuid> Expired;
	for (auto& Pair : Visuals)
	{
		auto& Visual = Pair.Value;
        if (Visual.bFlightConfigurationPending && Now >= Visual.NextFlightConfigurationRetry)
            ResolveFlightConfiguration(Visual);
        if (Visual.FlightRecipe.State.EffectId.IsValid() && Visual.State.Phase != EGuLiCombatEffectPhase::Finished)
        {
			++PredictionCount;
			if (Capture && Capture->IsCapturing())
			{
				switch (Visual.State.Source.Kind)
				{
				case EGuLiTargetKind::CommanderSoldier: ++PredictionCountBySource[0]; break;
				case EGuLiTargetKind::GroundActor: ++PredictionCountBySource[1]; break;
				case EGuLiTargetKind::Wingman: ++PredictionCountBySource[2]; break;
				case EGuLiTargetKind::Ship: ++PredictionCountBySource[3]; break;
				default: break;
				}
			}
            FVector TargetLocation = Visual.State.LastTargetLocation;
            FTransform TargetPose; int32 TargetType = 0;
            const bool HasTarget = !Visual.State.bFixedPoint && ResolvePose(Visual.State.Target,TargetPose,TargetType);
            if (HasTarget) TargetLocation = TargetPose.GetLocation();
            if (ClientFlights.Find(Visual.FlightHandle))
            {
                ClientFlights.Advance(Visual.FlightHandle, Visual.FlightRecipe, GetWorld(), Visual.FlightActor,
                    Now, HasTarget ? &TargetLocation : nullptr);
                const auto* Slot = ClientFlights.Find(Visual.FlightHandle);
                Visual.Prediction = Slot->Prediction; Visual.RenderLocation = Slot->DisplayLocation;
                if (Visual.FlightActor) Visual.FlightActor->ApplyPrediction(Visual.Prediction, Visual.RenderLocation, false);
            }
            else if (Visual.FlightActor)
            {
                Visual.FlightActor->AdvanceFlight(Now, HasTarget ? &TargetLocation : nullptr, false);
                Visual.Prediction = Visual.FlightActor->GetPrediction();
                Visual.RenderLocation = Visual.FlightActor->GetDisplayLocation(Now);
            }
            Visual.bHasPrediction = true;
            if (Visual.FlightActor)
            {
                const FBox Bounds=Visual.FlightActor->GetDisplayBounds(Visual.RenderLocation,Visual.Prediction.Velocity);
                const bool Visible=bEnabled && (!Bounds.IsValid || IsVisibleBounds(Bounds));
                if (Visible || !GuLiClientPresentation::OffscreenFlightsEnabled())
                    Visual.FlightActor->ApplyPrediction(Visual.Prediction,Visual.RenderLocation,true);
                Visual.FlightActor->ShowFlight(Visible);
            }
        }

		if (Visual.State.Kind == EGuLiCombatEffectKind::Projectile)
		{
			UpdateGroundWarning(Visual.State, bEnabled);
			// Expiry also retires guidance membership while visual rendering is disabled.
			if (Now >= Visual.State.EndTime) { Expired.Add(Pair.Key); continue; }
		}
		if (Visual.State.Kind == EGuLiCombatEffectKind::LinearProjectile)
		{
			const auto& State = Visual.State;
			if (State.Source.Kind == EGuLiTargetKind::GroundActor)
			{
				const bool bFinished = State.Phase == EGuLiCombatEffectPhase::Finished;
				const float Age = FMath::Clamp(Now - State.StartTime, 0.f, State.EndTime - State.StartTime);
				if (bFinished) Visual.RenderLocation = State.Location;
				else if (!Visual.bHasPrediction) Visual.RenderLocation = FVector(State.LaunchLocation) + FVector(State.Velocity) * Age;
				if (bEnabled && !bFinished && (Visual.Flight ? IsVisibleBounds(Visual.Flight->Bounds.GetBox())
					: IsVisibleSystemBounds(Visual.LoadedFlightSystem, FTransform(FVector(State.LaunchDirection).Rotation(),Visual.RenderLocation))))
				{
					if (!Visual.Flight) Visual.Flight = SpawnPooled(State.PlayerBulletVfxId, Visual.RenderLocation,
						1.f, 0, FVector(State.LaunchDirection).Rotation());
					if (Visual.Flight) Visual.Flight->SetWorldLocationAndRotation(Visual.RenderLocation, FVector(State.LaunchDirection).Rotation());
				}
				else if (Visual.Flight) { Retire(Visual.Flight, 0); Visual.Flight = nullptr; }
			}
			if ((Visual.State.Phase == EGuLiCombatEffectPhase::Finished && GetWorld()->GetTimeSeconds() >= Visual.LaserFadeUntil)
				|| (Visual.State.Phase != EGuLiCombatEffectPhase::Finished && Now >= Visual.State.EndTime)) Expired.Add(Pair.Key);
			continue;
		}
		if (!bEnabled)
		{
			Retire(Visual.Flight, 0); Visual.Flight = nullptr;
			Retire(Visual.Waiting, 0); Visual.Waiting = nullptr;
			Retire(Visual.ActiveLoop, 0); Visual.ActiveLoop = nullptr;
			Visual.bActivationPlayed = Now >= Visual.State.ActivationTime;
			continue;
		}
		if (Visual.State.Kind == EGuLiCombatEffectKind::SpellField)
		{
			UpdateField(Visual, Now);
			if (Now > Visual.State.EndTime + 30.0f) Expired.Add(Pair.Key);
			continue;
		}
		if (Visual.State.Kind == EGuLiCombatEffectKind::SustainedHitscan)
		{
			if (Now > Visual.State.EndTime + 0.25f) Expired.Add(Pair.Key);
			continue;
		}
		if (Now > Visual.State.EndTime + 0.25f) { Expired.Add(Pair.Key); continue; }
		UGuLiProjectileEffectDefinition* Definition = Visual.LoadedDefinition;
		if (!Definition)
        {
            if (Visual.LaserSlot==INDEX_NONE && Catalog) { Visual.LaserSlot=AllocateLaserSlot(Catalog->WingmanLaserVfxId); if (Visual.LaserSlot!=INDEX_NONE) LaserVisualIds.Add(Pair.Key); }
            continue;
        }
        const FGuLiCombatEffectState& Prediction = Visual.bHasPrediction ? Visual.Prediction : Visual.State;
        if (!Visual.bHasPrediction) Visual.RenderLocation = Prediction.Location;
		const bool bUseCluster = Visual.bUsesMissileCluster;
		const FVector DisplayLocation = Visual.RenderLocation + (bUseCluster
			? EvaluateLaunchVisualOffset(Visual, TEXT("MissileLauncher"), Now) : FVector::ZeroVector);
		if (Visual.LoadedFlightProfile && WingmanFlights
			&& WingmanFlights->Submit(Pair.Key,DisplayLocation,Visual.State.StartTime,Visual.LoadedFlightProfile))
		{
			if (Visual.Flight) { Retire(Visual.Flight,0); Visual.Flight=nullptr; }
			continue;
		}
		if (bUseCluster && MissileClusters && bClusterEnabled && MissileClusters->IsReady()
			&& MissileClusters->Submit(Pair.Key, DisplayLocation, Prediction.Velocity, Definition))
		{
			if (Visual.Flight) { Retire(Visual.Flight, 0); Visual.Flight = nullptr; }
			continue;
		}
		FBox FlightBounds;
		const FVector FlightScale=GuLiVfx::Scale(this,Definition->FlightVfxId);
		const bool bKnownFlightBounds=GetSystemWorldBounds(Visual.LoadedFlightSystem,
			FTransform(FVector(Prediction.Velocity).Rotation(),DisplayLocation,FlightScale),FlightBounds);
		if (Visual.Flight && Visual.Flight->Bounds.GetBox().IsValid) FlightBounds+=Visual.Flight->Bounds.GetBox();
		if (!GuLiClientPresentation::OffscreenFlightsEnabled() || !bKnownFlightBounds || IsVisibleBounds(FlightBounds))
		{
			if (!Visual.Flight) Visual.Flight = SpawnPooled(Definition->FlightVfxId,
				DisplayLocation, 1.0f, 0.0f, FRotator::ZeroRotator, TEXT("User.VisualScale"));
			if (Visual.Flight)
			{
				Visual.Flight->SetWorldLocationAndRotation(DisplayLocation, FVector(Prediction.Velocity).Rotation());
				Visual.Flight->SetVariableVec3(TEXT("User.Velocity"), Prediction.Velocity);
			}
		}
		else if (Visual.Flight) { Retire(Visual.Flight, Definition->TrailFadeSeconds); Visual.Flight = nullptr; }
	}
	for (const FGuid& Id : Expired) RemoveVisual(Id, false);
	// One aggregation per batch per frame, rather than a scan for every missile.
	for (const auto& Pair : GuidanceMembers) RefreshGuidanceWarning(Pair.Key, bEnabled);
	UpdateLaserPool(Now, bEnabled);
	UpdateRogueUpgradePool(Now, bEnabled);
	if (MuzzleBatches)
	{
		MuzzleBatches->Prepare(GetCatalog());
		MuzzleBatches->BeginFrame(GFrameCounter,GetWorld()->GetTimeSeconds(),Now,bEnabled,
			[this](const FGuLiCombatShotCue& Cue,FTransform& Pose,float& RenderTime)
			{
#if WITH_EDITOR
				if (const auto* Review=ReviewMuzzlePoses.Find(Cue.ShotId))
				{
					const double Age=GetWorld()->GetTimeSeconds()-Review->Start;
					Pose=Review->Pose; Pose.AddToTranslation(Review->Velocity*Age); RenderTime=Cue.MechanicalPoseTimeSeconds+float(Age); return true;
				}
#endif
				return ResolveMuzzleTransform(Cue,Pose,RenderTime);
			});
		MuzzleBatches->PublishFrame(GFrameCounter);
#if WITH_EDITOR
		for (auto It=ReviewMuzzlePoses.CreateIterator();It;++It) if (GetWorld()->GetTimeSeconds()-It.Value().Start>2) It.RemoveCurrent();
#endif
	}
	if (ImpactBatches)
	{
		ImpactBatches->BeginFrame(GFrameCounter,GetWorld()->GetTimeSeconds(),bEnabled);
		ImpactBatches->PublishFrame(GFrameCounter);
		Counters.ImpactActive=ImpactBatches->GetActiveCount(); Counters.ImpactComponents=ImpactBatches->GetComponentCount();
		Counters.ImpactBatchPublished=ImpactBatches->Published; Counters.ImpactBatchFallbacks=ImpactBatches->Fallbacks;
	}
	if (MissileClusters) MissileClusters->EndFrame();
	if (WingmanFlights) WingmanFlights->EndFrame();
	const float LocalNow = GetWorld()->GetTimeSeconds();
	for (int32 Index = Retiring.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* Component = Retiring[Index].Component;
		auto& Item=Retiring[Index];
		bool OffscreenExpired=false;
		if (GuLiClientPresentation::OffscreenLifecycleEnabled() && IsValid(Component) && Item.Bounds.IsValid
			&& (Item.VfxId==GuLiVfxIds::GroundMachineGunMuzzle || (Catalog && Item.VfxId==Catalog->MachineGunImpact.VfxId)))
		{
			FBox Bounds=Item.Bounds;
			if (Component->Bounds.GetBox().IsValid) Bounds+=Component->Bounds.GetBox();
			if (IsVisibleBounds(Bounds)) Item.OffscreenSince=-1;
			else if (Item.OffscreenSince<0) Item.OffscreenSince=LocalNow;
			else OffscreenExpired=LocalNow-Item.OffscreenSince>=GuLiClientPresentation::OffscreenGraceSeconds;
		}
		else Item.OffscreenSince=-1;
		if (!IsValid(Component) || Component->IsComplete() || LocalNow >= Item.ReleaseTime || OffscreenExpired)
		{
			if (IsValid(Component)) { Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Component); Component->ReleaseToPool(); }
			Retiring.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
	Counters.LastUpdateMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
	if (Capture && Capture->IsCapturing())
	{
		Capture->Record(TEXT("Flight.PredictorCalls"), PredictionCount);
		Capture->Record(TEXT("Flight.Predictions.Commander"), PredictionCountBySource[0]);
		Capture->Record(TEXT("Flight.Predictions.Ground"), PredictionCountBySource[1]);
		Capture->Record(TEXT("Flight.Predictions.Wingman"), PredictionCountBySource[2]);
		Capture->Record(TEXT("Flight.Predictions.Ship"), PredictionCountBySource[3]);
	}
}

#if WITH_EDITOR
int32 UGuLiCombatEffectPresentationSubsystem::EmitReviewImpacts(FVector Location,int32 Count,int32 Seed)
{
	if (!GetWorld() || GetWorld()->WorldType!=EWorldType::PIE || GetWorld()->GetNetMode()==NM_DedicatedServer
		|| Location.ContainsNaN() || Count<1 || Count>4096) return 0;
	const int64 Before=Counters.MachineGunImpactsPlayed;
	for (int32 I=0;I<Count;++I)
	{
		FGuLiCombatEffectState State; State.Kind=EGuLiCombatEffectKind::LinearProjectile;
		State.MatchEpoch=Epoch; State.EffectId=FGuid::NewGuid(); State.Sequence=1;
		State.Phase=EGuLiCombatEffectPhase::Finished; State.EndReason=EGuLiCombatEffectEndReason::Impact;
		State.Location=Location+FVector((I%8)*120,(I/8%8)*120,0);
		State.SampleTime=ServerTime(); State.RandomSeed=Seed+I;
		PlayMachineGunImpact(State);
	}
	return int32(Counters.MachineGunImpactsPlayed-Before);
}

bool UGuLiCombatEffectPresentationSubsystem::EmitReviewImpactInput(int32 Identity,FVector Location,
	FRotator Rotation,FVector Scale,FLinearColor Tint,int32 Seed,float Lifetime,int32 ReviewEpoch,int32 TerminalSequence)
{
	if (!GetWorld() || GetWorld()->WorldType!=EWorldType::PIE || GetWorld()->GetNetMode()==NM_DedicatedServer
		|| !ImpactBatches || !GetCatalog() || Identity<=0 || ReviewEpoch<0 || TerminalSequence<0
		|| Location.ContainsNaN() || Rotation.ContainsNaN() || !UGuLiVfxRegistrySubsystem::IsValidScale(Scale)
		|| !FMath::IsFinite(Lifetime) || Lifetime<=0 || Lifetime>3.f) return false;
	ImpactBatches->Prepare(Catalog);
	FGuLiImpactEvent Event;
	Event.Identity={FGuid(0x47554C49,0x494D5041,0,uint32(Identity)),uint32(ReviewEpoch),uint32(TerminalSequence),0};
	Event.Position=Location; Event.Rotation=Rotation.Quaternion(); Event.Scale=Scale; Event.Tint=Tint;
	Event.VfxId=Catalog->MachineGunImpact.VfxId; Event.Seed=Seed; Event.Lifetime=Lifetime;
	return ImpactBatches->Enqueue(Event);
}

bool UGuLiCombatEffectPresentationSubsystem::ResetReviewImpacts()
{
	if (!GetWorld() || GetWorld()->WorldType!=EWorldType::PIE || GetWorld()->GetNetMode()==NM_DedicatedServer
		|| !ImpactBatches) return false;
	ImpactBatches->Reset();
	return true;
}
#endif

FGuLiCombatEffectVisualCounters UGuLiCombatEffectPresentationSubsystem::GetCounters() const
{
	FGuLiCombatEffectVisualCounters Result = Counters;
	Result.ClientFlightActorCapacity = FlightActors.Num();
	Result.ClientFlightActorActive = FlightActors.Num()-FreeFlightActors.Num();
	Result.ClientFlightDataCapacity = ClientFlights.GetCapacity();
	Result.ClientFlightDataActive = ClientFlights.GetActiveCount();
	Result.ClientFlightDataReuses = ClientFlights.GetReusedAcquisitions();
	Result.ClientFlightDataReleases = ClientFlights.GetReleases();
	Result.ClientFlightDataEpoch = ClientFlights.GetEpoch();
	Result.ComponentCount = IsValid(Gunfire) ? 1 : 0;
	if (ImpactBatches) Result.ComponentCount+=ImpactBatches->GetComponentCount();
	if (MuzzleBatches)
	{
		Result.MuzzleActive=MuzzleBatches->GetActiveCount(); Result.MuzzleComponents=MuzzleBatches->GetComponentCount();
		Result.ComponentCount+=Result.MuzzleComponents; Result.MuzzleAccepted=MuzzleBatches->Accepted; Result.MuzzleBorn=MuzzleBatches->Born;
		Result.MuzzleDuplicates=MuzzleBatches->Duplicates; Result.MuzzleExpired=MuzzleBatches->Expired;
		Result.MuzzleOffscreenRecycled=MuzzleBatches->OffscreenRecycled; Result.MuzzleBatchPublished=MuzzleBatches->Published;
		Result.MuzzleBatchFallbacks=MuzzleBatches->Fallbacks; Result.MuzzlePoseQueries=MuzzleBatches->PoseQueries;
		Result.MuzzleLifeUploads=MuzzleBatches->LifeUploads; Result.MuzzlePoseUploads=MuzzleBatches->PoseUploads;
		Result.BurstsPlayed+=MuzzleBatches->Born;
	}
	if (WingmanFlights)
	{
		Result.WingmanFlightActive=WingmanFlights->GetActiveCount(); Result.WingmanFlightComponents=WingmanFlights->GetComponentCount();
		Result.WingmanFlightUploads=WingmanFlights->Uploads; Result.ComponentCount+=Result.WingmanFlightComponents;
	}
	if (MissileClusters)
	{
		Result.MissileClusterComponents = MissileClusters->GetSystemCount();
		Result.MissileParticleCapacity = MissileClusters->GetParticleCapacity();
		Result.MissileFullTrails = MissileClusters->GetFullTrails();
		Result.MissileClusterUpdateMilliseconds = MissileClusters->GetUpdateMilliseconds();
		Result.ComponentCount += Result.MissileClusterComponents;
	}
	for (const auto& Item : MechanicalMuzzles) Result.ComponentCount += IsValid(Item.Component) ? 1 : 0;
	for (const auto& Block : LaserBlocks) Result.ComponentCount += IsValid(Block.Component) ? 1 : 0;
	for (const auto& Block : UpgradeBlocks) Result.ComponentCount += IsValid(Block.Component) ? 1 : 0;
	for (const auto& Pair : Visuals) Result.ComponentCount += (IsValid(Pair.Value.Flight) ? 1 : 0) + (IsValid(Pair.Value.Waiting) ? 1 : 0) + (IsValid(Pair.Value.ActiveLoop) ? 1 : 0);
	for (const auto& Item : Retiring) Result.ComponentCount += IsValid(Item.Component) ? 1 : 0;
	return Result;
}

TArray<FGuLiCombatEffectState> UGuLiCombatEffectPresentationSubsystem::GetEffectStates() const
{
	TArray<FGuLiCombatEffectState> Result;
	for (const auto& Pair : Visuals) Result.Add(Pair.Value.State);
	return Result;
}
