#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/CombatEffects/GuLiUnitWreck.h"

#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

bool UGuLiUnitFeedbackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

void UGuLiUnitFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	Collection.InitializeDependency<UGuLiVfxRegistrySubsystem>();
	if (const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>())
	{
		for (const FGuLiSoldierDefinition& Definition : Data->GetSoldierDefinitions())
		{
			if (!Definition.Model) continue;
			const float Size = Definition.Model->GetBoundingBox().GetExtent().GetMax();
			if (FMath::IsFinite(Size) && Size > UE_SMALL_NUMBER && (ReferenceUnitSize <= 0.0f || Size < ReferenceUnitSize))
				ReferenceUnitSize = Size;
		}
	}
	CosmeticRandom.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	const auto* Settings = GetDefault<UGuLiUnitFeedbackSettings>();
	HitMaterial = GuLiVfx::Load<UMaterialInterface>(this, Settings->HitVfxId);
	InstancedHitMaterial = GuLiVfx::Load<UMaterialInterface>(this, Settings->InstancedHitVfxId);
	WreckMaterial = GuLiVfx::Load<UMaterialInterface>(this, Settings->WreckVfxId);
	TArray<FSoftObjectPath> Paths;
	for (const auto& Asset : Settings->ExplosionVfxIds) { const auto Path = GuLiVfx::Path(this, Asset); if (!Path.IsNull()) Paths.AddUnique(Path); }
	for (const auto& Asset : Settings->WingmanExplosionVfxIds) { const auto Path = GuLiVfx::Path(this, Asset); if (!Path.IsNull()) Paths.AddUnique(Path); }
	if (!Paths.IsEmpty())
	{
		LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
			FStreamableDelegate::CreateUObject(this, &ThisClass::FinishLoading));
	}
}

void UGuLiUnitFeedbackSubsystem::FinishLoading()
{
	const auto* Settings = GetDefault<UGuLiUnitFeedbackSettings>();
	for (const auto& Asset : Settings->ExplosionVfxIds)
		if (GuLiVfx::Load<UNiagaraSystem>(this, Asset, false)) LoadedExplosions.AddUnique(Asset);
	for (const auto& Asset : Settings->WingmanExplosionVfxIds)
		if (GuLiVfx::Load<UNiagaraSystem>(this, Asset, false)) LoadedWingmanExplosions.AddUnique(Asset);
	const float Now = GetWorld()->GetTimeSeconds();
	for (const auto& Pending : PendingExplosions)
		if (Pending.ExpireTime >= Now) SpawnExplosion(Pending.Location, Pending.UnitSize, Pending.bWingman);
	PendingExplosions.Reset();
}

bool UGuLiUnitFeedbackSubsystem::IsWithinCullDistance(const FVector& Location) const
{
	if (!GetWorld() || Location.ContainsNaN()) return false;
	const float Distance = GetDefault<UGuLiUnitFeedbackSettings>()->CullDistance;
	if (Distance <= 0) return true;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (!Controller || !Controller->IsLocalController()) continue;
		FVector Camera; FRotator Rotation; Controller->GetPlayerViewPoint(Camera, Rotation);
		if (FVector::DistSquared(Camera, Location) <= FMath::Square(Distance)) return true;
	}
	return false;
}

float UGuLiUnitFeedbackSubsystem::HealthBarOpacity(const float Age)
{
	if (!FMath::IsFinite(Age) || Age < 0.0f) return 0.0f;
	const auto* Settings = GetDefault<UGuLiUnitFeedbackSettings>();
	return FMath::Clamp(1.0f - FMath::Max(0.0f, Age - Settings->HealthBarHoldSeconds)
		/ FMath::Max(Settings->HealthBarFadeSeconds, UE_SMALL_NUMBER), 0.0f, 1.0f);
}

void UGuLiUnitFeedbackSubsystem::EnsureHealthBarRenderers()
{
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (APlayerController* Controller = It->Get(); Controller && Controller->IsLocalController())
			AGuLiCommanderHealthBarRenderer::FindOrSpawn(GetWorld(), Controller);
}

bool UGuLiUnitFeedbackSubsystem::GetActorVisualBounds(const AActor* Actor, FBox& OutWorldBounds, float& OutSize)
{
	OutWorldBounds = FBox(ForceInit);
	OutSize = 0.0f;
	if (!IsValid(Actor)) return false;
	FBox ModelBounds(ForceInit);
	// Remove world yaw/roll before measuring, but retain all model/component/Actor scales.
	const FTransform UnitAxes(Actor->GetActorQuat(), Actor->GetActorLocation(), FVector::OneVector);
	TInlineComponentArray<UMeshComponent*> Meshes;
	Actor->GetComponents(Meshes, true);
	for (const UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->IsVisible()) continue;
		FBox LocalBounds(ForceInit);
		if (const auto* StaticMesh = Cast<UStaticMeshComponent>(Mesh); StaticMesh && StaticMesh->GetStaticMesh())
			LocalBounds = StaticMesh->GetStaticMesh()->GetBoundingBox();
		else if (const auto* SkeletalMesh = Cast<USkeletalMeshComponent>(Mesh); SkeletalMesh && SkeletalMesh->GetSkeletalMeshAsset())
			LocalBounds = SkeletalMesh->CalcBounds(FTransform::Identity).GetBox();
		if (!LocalBounds.IsValid || LocalBounds.Min.ContainsNaN() || LocalBounds.Max.ContainsNaN()) continue;
		OutWorldBounds += LocalBounds.TransformBy(Mesh->GetComponentTransform());
		ModelBounds += LocalBounds.TransformBy(Mesh->GetComponentTransform().GetRelativeTransform(UnitAxes));
	}
	if (!ModelBounds.IsValid || !OutWorldBounds.IsValid) return false;
	OutSize = ModelBounds.GetExtent().GetMax();
	return FMath::IsFinite(OutSize) && OutSize > UE_SMALL_NUMBER;
}

float UGuLiUnitFeedbackSubsystem::CalculateDestructionScale(const float UnitSize) const
{
	constexpr float BaseScale = 1.0f; // Only the model-size ratio; each VfxId owns its authored scale.
	if (!FMath::IsFinite(UnitSize) || UnitSize <= UE_SMALL_NUMBER || ReferenceUnitSize <= UE_SMALL_NUMBER)
		return BaseScale; // Missing visual data uses the approved reference look, never a guessed unit radius.
	const float Scale = BaseScale * UnitSize / ReferenceUnitSize;
	return FMath::IsFinite(Scale) && Scale > 0.0f ? Scale : BaseScale;
}

void UGuLiUnitFeedbackSubsystem::FlashActor(AActor* Actor, float HealthFraction)
{
	if (!IsValid(Actor) || Actor->IsHidden()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (const auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>())
	{
		const auto& State = Health->GetHealthState();
		HealthFraction = State.IsWellFormed() && State.MaxHealth > 0 ? State.Health / State.MaxHealth : -1.0f;
	}
	ActorHealthBars.Add(Actor, {Now, HealthFraction});
	EnsureHealthBarRenderers();
	OnActorHealthBarChanged.Broadcast(Actor);
	if (!HitMaterial || !IsWithinCullDistance(Actor->GetActorLocation())) return;
	TInlineComponentArray<UMeshComponent*> Meshes;
	Actor->GetComponents(Meshes, true);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->IsVisible()
			|| (!Cast<UStaticMeshComponent>(Mesh) && !Cast<USkeletalMeshComponent>(Mesh))) continue;
		FGuLiActiveHitOverlay* Hit = ActiveHits.FindByPredicate([Mesh](const auto& Entry) { return Entry.Mesh == Mesh; });
		if (!Hit)
		{
			Hit = &ActiveHits.AddDefaulted_GetRef();
			Hit->Mesh = Mesh; Hit->PreviousMaterial = Mesh->GetOverlayMaterial();
		}
		Hit->ExpireTime = Now + HitDuration;
		Mesh->SetCustomPrimitiveDataFloat(HitTimeCustomDataIndex, Now);
		Mesh->SetOverlayMaterial(HitMaterial);
	}
}

void UGuLiUnitFeedbackSubsystem::ClearActorFlash(AActor* Actor)
{
	ActorHealthBars.Remove(Actor);
	OnActorHealthBarChanged.Broadcast(Actor);
	for (int32 Index = ActiveHits.Num() - 1; Index >= 0; --Index)
	{
		auto& Hit = ActiveHits[Index];
		UMeshComponent* Mesh = Hit.Mesh.Get();
		if (!Mesh || Mesh->GetOwner() == Actor || (Mesh->GetOwner() && Mesh->GetOwner()->IsChildActor() && Mesh->GetOwner()->GetParentActor() == Actor))
		{
			if (Mesh && Mesh->GetOverlayMaterial() == HitMaterial) Mesh->SetOverlayMaterial(Hit.PreviousMaterial);
			ActiveHits.RemoveAtSwap(Index);
		}
	}
}

void UGuLiUnitFeedbackSubsystem::PlayDestruction(FVector Location, float UnitSize)
{
	QueueDestruction(Location, UnitSize, false);
}

AGuLiUnitWreck* UGuLiUnitFeedbackSubsystem::AllocateWreck(const FVector& Location)
{
	if (!WreckMaterial || !GetWorld() || GetWorld()->bIsTearingDown || !IsWithinCullDistance(Location)) return nullptr;
	ActiveWrecks.RemoveAllSwap([](const auto& Wreck) { return !Wreck.IsValid(); });
	if (ActiveWrecks.Num() >= FMath::Max(1, GetDefault<UGuLiUnitFeedbackSettings>()->MaximumConcurrentWrecks)) return nullptr;
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Wreck = GetWorld()->SpawnActor<AGuLiUnitWreck>(AGuLiUnitWreck::StaticClass(), FTransform::Identity, Parameters);
	if (Wreck) ActiveWrecks.Add(Wreck);
	return Wreck;
}

AGuLiUnitWreck* UGuLiUnitFeedbackSubsystem::SpawnActorWreck(AActor* Source, FVector InitialVelocity, bool bFalling)
{
	if (!Source || InitialVelocity.ContainsNaN()) return nullptr;
	AGuLiUnitWreck* Wreck = AllocateWreck(Source->GetActorLocation());
	if (!Wreck) return nullptr;
	if (!Wreck->InitializeFromActor(Source, WreckMaterial)) { Wreck->Destroy(); return nullptr; }
	const auto* Settings = GetDefault<UGuLiUnitFeedbackSettings>();
	if (bFalling) Wreck->StartFalling(InitialVelocity, Settings->FallingWreckMaximumLifetime);
	else Wreck->SetLifeSpan(FMath::Max(0.1f, Settings->GroundWreckLifetime));
	return Wreck;
}

void UGuLiUnitFeedbackSubsystem::QueueDestruction(const FVector& Location, float UnitSize, bool bWingman)
{
	if (!IsWithinCullDistance(Location) || !FMath::IsFinite(UnitSize)) return;
	const auto& Variants = bWingman ? LoadedWingmanExplosions : LoadedExplosions;
	if (Variants.IsEmpty())
	{
		if (LoadHandle && !LoadHandle->HasLoadCompleted() && PendingExplosions.Num() < 64)
			PendingExplosions.Add({Location, UnitSize, static_cast<float>(GetWorld()->GetTimeSeconds()) + 1.0f, bWingman});
		return;
	}
	SpawnExplosion(Location, UnitSize, bWingman);
}

void UGuLiUnitFeedbackSubsystem::ApplyWingmanFeedback(const FGuLiWingmanHandle& Wingman, const FVector& Location, bool bDestroyed, float HealthFraction)
{
	if (!Wingman.IsValid() || DestroyedWingmen.Contains(Wingman)) return;
	auto* Presentation = AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld());
	AGuLiWingmanPawn* Pawn = Presentation ? Presentation->FindPresentedPawn(Wingman) : nullptr;
	if (bDestroyed)
	{
		DestroyedWingmen.Add(Wingman); DestroyedWingmanOrder.Add(Wingman);
		if (DestroyedWingmanOrder.Num() > 4096)
		{
			DestroyedWingmen.Remove(DestroyedWingmanOrder[0]);
			DestroyedWingmanOrder.RemoveAt(0, 1, EAllowShrinking::No);
		}
		FTransform Pose;
		FVector Velocity = FVector::ZeroVector;
		const bool bHasPose = Presentation && Presentation->TryGetDestructionMotion(Wingman, Pose, Velocity);
		if (Pawn)
		{
			SpawnActorWreck(Pawn, Velocity, true);
		}
		else if (bHasPose)
		{
			// A roster refresh can release a Pawn before the reliable fallback reaches this client.
			if (AGuLiUnitWreck* Wreck = AllocateWreck(Pose.GetLocation()))
			{
				const FTransform MeshPose = FTransform(FRotator(0, 180, 0), FVector::ZeroVector, FVector(0.2f)) * Pose;
				if (Wreck->InitializeFromStaticMesh(Presentation->GetFeedbackMesh(), MeshPose, WreckMaterial))
					Wreck->StartFalling(Velocity, GetDefault<UGuLiUnitFeedbackSettings>()->FallingWreckMaximumLifetime);
				else Wreck->Destroy();
			}
		}
		FBox Bounds(ForceInit); float Size = 0.0f;
		if (GetActorVisualBounds(Pawn, Bounds, Size))
			QueueDestruction(Bounds.GetCenter(), Size, true);
		else if (const UStaticMesh* Mesh = Presentation ? Presentation->GetFeedbackMesh() : nullptr)
			QueueDestruction(Location, Mesh->GetBoundingBox().GetExtent().GetMax() * 0.2f, true);
		else
			QueueDestruction(Location, 0.0f, true);
		if (Pawn) ClearActorFlash(Pawn);
		return;
	}
	if (Pawn) FlashActor(Pawn, HealthFraction);
}

void UGuLiUnitFeedbackSubsystem::SpawnExplosion(const FVector& Location, float UnitSize, bool bWingman)
{
	const auto& Variants = bWingman ? LoadedWingmanExplosions : LoadedExplosions;
	if (Variants.IsEmpty() || !IsWithinCullDistance(Location)
		|| ActiveExplosions.Num() >= FMath::Max(1, GetDefault<UGuLiUnitFeedbackSettings>()->MaximumConcurrentExplosions)) return;
	const int32 VfxId = Variants[CosmeticRandom.RandRange(0, Variants.Num() - 1)];
	UNiagaraSystem* System = GuLiVfx::Load<UNiagaraSystem>(this, VfxId, false);
	const FVector Scale = GuLiVfx::Scale(this, VfxId, FVector(CalculateDestructionScale(UnitSize)));
	if (!System || !UGuLiVfxRegistrySubsystem::IsValidScale(Scale)) return;
	const auto* Settings = GetDefault<UGuLiUnitFeedbackSettings>();
	const FName ScaleParameter = bWingman ? Settings->WingmanExplosionScaleParameter : Settings->GroundExplosionScaleParameter;
	if (!ScaleParameter.IsNone() && (!FMath::IsNearlyEqual(Scale.X, Scale.Y) || !FMath::IsNearlyEqual(Scale.X, Scale.Z)))
	{ UE_LOG(LogTemp, Error, TEXT("VfxId %d requires uniform Niagara parameter scale."), VfxId); return; }
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Location,
		FRotator(0, CosmeticRandom.FRandRange(0, 360), 0), ScaleParameter.IsNone() ? Scale : FVector::OneVector,
		false, false, ENCPoolMethod::ManualRelease, true);
	if (!Component) return;
	if (!ScaleParameter.IsNone()) Component->SetVariableFloat(ScaleParameter, Scale.X);
	Component->SetCastShadow(false);
	auto& Active = ActiveExplosions.AddDefaulted_GetRef();
	Active.Component = Component;
	// The pack's one-shots finish naturally. The deadline also bounds a future accidentally looping variant.
	Active.Deadline = GetWorld()->GetTimeSeconds() + 12.0f;
	Component->OnSystemFinished.AddDynamic(this, &ThisClass::HandleExplosionFinished);
	Component->Activate(true);
}

void UGuLiUnitFeedbackSubsystem::HandleExplosionFinished(UNiagaraComponent* Component)
{
	if (auto* Active = ActiveExplosions.FindByPredicate([Component](const auto& Entry) { return Entry.Component == Component; }))
		Active->bFinished = true;
}

bool UGuLiUnitFeedbackSubsystem::IsTickable() const
{
	return !IsTemplate() && GetWorld() && (!ActiveHits.IsEmpty() || !ActiveExplosions.IsEmpty() || !ActorHealthBars.IsEmpty());
}

TStatId UGuLiUnitFeedbackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiUnitFeedbackSubsystem, STATGROUP_Tickables);
}

void UGuLiUnitFeedbackSubsystem::Tick(float DeltaTime)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextHealthBarCleanupTime)
	{
		NextHealthBarCleanupTime = Now + 0.1f;
		TArray<TWeakObjectPtr<AActor>> RemovedBars;
		for (auto It = ActorHealthBars.CreateIterator(); It; ++It)
			if (!It.Key().IsValid() || It.Key()->IsHidden() || HealthBarOpacity(Now - It.Value().StartTime) <= 0.0f)
			{ RemovedBars.Add(It.Key()); It.RemoveCurrent(); }
		for (auto Actor : RemovedBars) OnActorHealthBarChanged.Broadcast(Actor);
	}
	for (int32 Index = ActiveHits.Num() - 1; Index >= 0; --Index)
	{
		const auto& Hit = ActiveHits[Index];
		UMeshComponent* Mesh = Hit.Mesh.Get();
		if (!Mesh || Now >= Hit.ExpireTime || (Mesh->GetOwner() && Mesh->GetOwner()->IsHidden()))
		{
			if (Mesh && Mesh->GetOverlayMaterial() == HitMaterial) Mesh->SetOverlayMaterial(Hit.PreviousMaterial);
			ActiveHits.RemoveAtSwap(Index);
		}
	}
	for (int32 Index = ActiveExplosions.Num() - 1; Index >= 0; --Index)
	{
		const auto& Active = ActiveExplosions[Index];
		// IsComplete() also returns true before Niagara has its first ready system instance.
		// Use the actual completion event so an asynchronous activation is not returned to the pool early.
		if (!IsValid(Active.Component) || Active.bFinished || Now >= Active.Deadline)
		{
			if (IsValid(Active.Component))
			{
				Active.Component->OnSystemFinished.RemoveDynamic(this, &ThisClass::HandleExplosionFinished);
				Active.Component->DeactivateImmediate(); Active.Component->ReleaseToPool();
			}
			ActiveExplosions.RemoveAtSwap(Index);
		}
	}
}

void UGuLiUnitFeedbackSubsystem::Deinitialize()
{
	for (const auto& Wreck : ActiveWrecks) if (Wreck.IsValid()) Wreck->Destroy();
	ActiveWrecks.Reset();
	WreckMaterial = nullptr;
	if (LoadHandle) { LoadHandle->CancelHandle(); LoadHandle.Reset(); }
	for (const auto& Hit : ActiveHits)
		if (UMeshComponent* Mesh = Hit.Mesh.Get(); Mesh && Mesh->GetOverlayMaterial() == HitMaterial) Mesh->SetOverlayMaterial(Hit.PreviousMaterial);
	for (const auto& Active : ActiveExplosions)
		if (IsValid(Active.Component))
		{
			Active.Component->OnSystemFinished.RemoveDynamic(this, &ThisClass::HandleExplosionFinished);
			Active.Component->DeactivateImmediate(); Active.Component->ReleaseToPool();
		}
	ActiveHits.Reset(); ActiveExplosions.Reset(); LoadedExplosions.Reset(); PendingExplosions.Reset();
	LoadedWingmanExplosions.Reset();
	ActorHealthBars.Reset();
	DestroyedWingmen.Reset(); DestroyedWingmanOrder.Reset();
	HitMaterial = nullptr; InstancedHitMaterial = nullptr;
	Super::Deinitialize();
}
