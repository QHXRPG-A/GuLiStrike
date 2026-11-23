#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponAnimInstance.h"
#include "Gameplay/GroundMech/Animation/GuLiGroundMechAnimInstance.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanTargetAcquisition.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Curves/CurveFloat.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridManifest.h"

double GuLiMechFire::RescaleCooldown(double Now, double NextShot, float OldRate, float NewRate)
{
	return Now + FMath::Clamp((NextShot - Now) * OldRate, 0., 1.) / NewRate;
}

FGuLiAILockHexSide::FGuLiAILockHexSide(int32 Index, double Progress, double LockedRadius, double Release)
{
	static constexpr int32 CloseOrder[] = {0, 2, 4, 1, 3, 5};
	Angle = (Index - 1) * UE_DOUBLE_PI / 3.;
	Closure = FMath::SmoothStep(CloseOrder[Index] * .026, 1., Progress);
	Radius = LockedRadius + 21. * (1. - Closure) + 6. * Release;
	Outset = 22. * (1. - Closure) + 27. * Release;
}

bool FGuLiAILockHexSide::TouchesAperture(const FVector2D& ApertureFromTarget, double ApertureRadius) const
{
	// Closest point on the finite side, including both vertices and tangency.
	const FVector2D Normal(FMath::Cos(Angle), FMath::Sin(Angle));
	const FVector2D Tangent(-Normal.Y, Normal.X);
	const double ToLine = FVector2D::DotProduct(ApertureFromTarget, Normal) - (Outset + Radius * UE_DOUBLE_HALF_SQRT_3);
	const double PastEnd = FMath::Max(0., FMath::Abs(FVector2D::DotProduct(ApertureFromTarget, Tangent)) - Radius * .5);
	return FMath::Square(ToLine) + FMath::Square(PastEnd) <= FMath::Square(ApertureRadius) + UE_DOUBLE_SMALL_NUMBER;
}

bool FGuLiAILockSight::IncludesTarget(const FGuLiTargetHandle& Candidate, const FVector2D& PlayerPixel) const
{
	if (!bProjected || Radius <= 0.f) return false;
	const FVector2D ToCenter = Center - PlayerPixel;
	if (ToCenter.SizeSquared() <= FMath::Square(Radius)) return true;
	if (Candidate != Target || !Target.IsValid()) return false;
	const FVector2D DesignCenter = ToCenter * (double(DesignRadius) / Radius);
	for (int32 Side = 0; Side < 6; ++Side)
		if (FGuLiAILockHexSide(Side, Progress, HexRadius).TouchesAperture(DesignCenter, DesignRadius)) return true;
	return false;
}

void FGuLiAILockSight::UpdateAperture(const FSceneViewProjectionData& Projection)
{
    const auto& rect=Projection.GetConstrainedViewRect();
    Center=FVector2D(rect.Size())*.5;
    Radius=DesignRadius*FMath::Min(rect.Width()/1920.f,rect.Height()/1080.f);
    bProjected=Radius>0;
}

UGuLiGroundMechWeaponComponent::UGuLiGroundMechWeaponComponent()
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

USkeletalMeshComponent* UGuLiGroundMechWeaponComponent::Gun() const
{
	const auto* Pawn = Cast<AGuLiGroundMechCharacter>(GetOwner());
	return Pawn ? Pawn->GetMachinegun() : nullptr;
}

FName UGuLiGroundMechWeaponComponent::MuzzleSocket(uint8 Index) const
{
	const auto* Pawn = Cast<AGuLiGroundMechCharacter>(GetOwner());
	return Pawn && Pawn->UsesIntegratedBodyMesh() && Index == 1 ? FName(TEXT("Muzzle_L")) : FName(*ActiveSkill.MuzzleSocket);
}

void UGuLiGroundMechWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
    if(auto* Cards=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>())Cards->OnGroundBuildChanged.AddDynamic(this,&UGuLiGroundMechWeaponComponent::OnRogueBuildChanged);
	AddTickPrerequisiteActor(GetOwner());
	if (auto* Mech = Cast<AGuLiGroundMechCharacter>(GetOwner()))
	{
		// The upper body and muzzle are attached to the animated leg skeleton.
		// Server launches must sample the same refreshed mounting pose as clients.
		Mech->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		AddTickPrerequisiteComponent(Mech->GetMesh());
	}
	LoadConfiguration(CurrentUpgradeId);
	if (Gun())
	{
		// Integrated body is already a prerequisite of Weapon; avoid a cycle.
		if (!CastChecked<AGuLiGroundMechCharacter>(GetOwner())->UsesIntegratedBodyMesh()) Gun()->AddTickPrerequisiteComponent(this);
		Gun()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

bool UGuLiGroundMechWeaponComponent::LoadConfiguration(const FString& Id)
{
	if (!bWeaponEnabled || !UpgradeTable || !SkillTable) return false;
	const bool Integrated = CastChecked<AGuLiGroundMechCharacter>(GetOwner())->UsesIntegratedBodyMesh();
	const FGuLiStrikeMechUpgradesRow* Upgrade = nullptr;
	for (const auto& Pair : UpgradeTable->GetRowMap())
	{
		if (UpgradeTable->GetRowStruct() != FGuLiStrikeMechUpgradesRow::StaticStruct()) return false;
		const auto* Row = reinterpret_cast<const FGuLiStrikeMechUpgradesRow*>(Pair.Value);
		if (Row->Id == Id) { Upgrade = Row; break; }
	}
	if (!Upgrade || !FMath::IsFinite(Upgrade->FireRate) || Upgrade->FireRate <= 0 || Upgrade->FireRate > 30
		|| !FMath::IsFinite(Upgrade->Damage) || Upgrade->Damage <= 0 || !Gun()) return false;
    GuLiNet::Effects::Set Effects;
    if(const auto* Identity=UGuLiHybridActorComponent::Find(GetOwner()))
    {TMap<FString,int32> Counts;uint64 Revision;if(!GuLiGroundRogue::ReadBuild(*GetWorld(),Identity->GetState().Controller,Counts,Revision,&Effects))return false;}
    if(double(Upgrade->FireRate)*GuLiNet::Effects::FireRateMultiplier(Effects)>GuLiNet::Effects::MaximumGroundFireRate)return false;
	const FGuLiStrikeMechSkillsRow* Skill = nullptr;
	if (SkillTable->GetRowStruct() != FGuLiStrikeMechSkillsRow::StaticStruct()) return false;
	for (const auto& Pair : SkillTable->GetRowMap())
	{
		const auto* Row = reinterpret_cast<const FGuLiStrikeMechSkillsRow*>(Pair.Value);
		if (Row->Id == Upgrade->SkillId) { Skill = Row; break; }
	}
	if (!Skill || !FMath::IsFinite(Skill->ProjectileSpeed) || Skill->ProjectileSpeed <= 0
		|| Skill->InitialBulletVfxId<=0 || !FMath::IsFinite(Skill->InitialBulletDamageRatio) || Skill->InitialBulletDamageRatio<=0 || Skill->InitialBulletDamageRatio>1
		|| !FMath::IsFinite(Skill->ProjectileLifetime) || Skill->ProjectileLifetime <= 0
		|| !FMath::IsFinite(Skill->SweepRadius) || Skill->SweepRadius <= 0
		|| !FMath::IsFinite(Skill->AimAssistRadiusCentimeters) || Skill->AimAssistRadiusCentimeters < 0
		|| (Skill->AimAssistEnabled && Skill->AimAssistRadiusCentimeters <= 0)
		|| !FMath::IsFinite(Skill->RecoilTargetLocalZCentimeters) || !FMath::IsFinite(Skill->RecoilDuration)
		|| Skill->RecoilDuration <= 0 || (!Integrated && Gun()->GetBoneIndex(FName(Skill->RecoilBone)) == INDEX_NONE)
		|| !Gun()->DoesSocketExist(FName(Skill->MuzzleSocket))) return false;
	auto* Curve = Cast<UCurveFloat>(Skill->RecoilCurve.LoadSynchronous());
	auto* Animation = Skill->WeaponAnimation.LoadSynchronous();

	auto* Muzzle = UGuLiNetworkSettings::CanPresent(GetWorld()) ? GuLiVfx::Load<UNiagaraSystem>(this, Skill->MuzzleVfxId) : nullptr;
	if (UGuLiNetworkSettings::CanPresent(GetWorld()))
	{
		// Retain the configured shot resources in the world's VFX registry before
		// the weapon becomes usable. A first impact must not synchronously load
		// its Niagara package inside the authoritative projectile step.
		GuLiVfx::Load<UNiagaraSystem>(this, Skill->BulletVfxId);
        GuLiVfx::Load<UNiagaraSystem>(this, Skill->InitialBulletVfxId);
		GuLiVfx::Load<UNiagaraSystem>(this, Skill->ImpactVfxId);
	}
	if (!Curve) return false;
	if (Integrated)
	{
		auto* BodyAnim = Cast<UGuLiGroundMechAnimInstance>(Gun()->GetAnimInstance());
		if (!BodyAnim || !Gun()->DoesSocketExist(TEXT("Muzzle_L"))) return false;
		BodyAnim->ConfigureIntegratedRecoil(Curve,Skill->RecoilDuration);
		ActiveSkill = *Skill; ActiveUpgrade = *Upgrade; MuzzleSystem = Muzzle;
		ConfiguredAnim.Reset(); bConfigured = true;
        RefreshRogueBuild();
		return true;
	}
	if (!Animation || !Animation->IsChildOf(UGuLiGroundMechWeaponAnimInstance::StaticClass())) return false;
	ActiveSkill = *Skill; ActiveUpgrade = *Upgrade; MuzzleSystem = Muzzle;
	if (Gun()->GetAnimClass() != Animation) Gun()->SetAnimInstanceClass(Animation);
	auto* Anim = Cast<UGuLiGroundMechWeaponAnimInstance>(Gun()->GetAnimInstance());
	if (!Anim) return false;
	Anim->Configure(Curve, FName(Skill->RecoilBone), Skill->RecoilTargetLocalZCentimeters, Skill->RecoilDuration);
	ConfiguredAnim = Anim; bConfigured = true;
    RefreshRogueBuild();
	return true;
}

bool UGuLiGroundMechWeaponComponent::ApplyUpgradeById(const FString& Id)
{
	return RequestUpgradeById(Id).IsValid();
}

void UGuLiGroundMechWeaponComponent::OnRep_Upgrade() { LoadConfiguration(CurrentUpgradeId); }

bool UGuLiGroundMechWeaponComponent::CanControl(bool bLocal) const
{
	const auto* Pawn = Cast<APawn>(GetOwner());
    const auto* Health=GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>();
    if (!Health || !Health->IsAlive()) return false;
	const auto* Identity=UGuLiHybridActorComponent::Find(GetOwner());
	return bConfigured && Pawn && Identity && (bLocal?Identity->CanControl():Identity->CanSimulate())
		&& (!bLocal || Pawn->IsLocallyControlled()) && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn);
}

void UGuLiGroundMechWeaponComponent::RegisterSource()
{
	if (!bConfigured) return;
	auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!Ledger || !Ledger->GetMatchEpoch()) return;
	const auto* Identity=UGuLiHybridActorComponent::Find(GetOwner());
	if (!Identity || !Identity->GetObjectKey().IsValid()) return;
	if (SourceEpoch == Ledger->GetMatchEpoch() && SourceHandle.IsValid()) return;
	if (SourceHandle.IsValid()) Ledger->UnregisterSource(SourceHandle, this);
	bFireHeld = false;
	SourceEpoch = Ledger->GetMatchEpoch();
	SourceHandle = {}; SourceHandle.Kind = EGuLiTargetKind::GroundActor;
	SourceHandle.AuthorityId=Identity->GetObjectKey().StableId;
	SourceHandle.Generation=Identity->GetObjectKey().LifeGeneration; SourceHandle.LocalId=Identity->GetState().Ordinal;
	if (!GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->BindTarget(SourceHandle,Identity->GetObjectKey())) return;
	FGuLiCombatSourceAdapter Adapter; Adapter.LifetimeOwner = this;
	TWeakObjectPtr<UGuLiGroundMechWeaponComponent> Weak(this);
	Adapter.ReadSnapshot = [Weak](FGuLiCombatTargetSnapshot& Out)
	{
		const auto* Self = Weak.Get();
		if (!Self || !IsValid(Self->GetOwner())) return false;
		const auto* Pawn = CastChecked<APawn>(Self->GetOwner());
		const auto* Binding=UGuLiHybridActorComponent::Find(Pawn);
        const auto* Health=Pawn->FindComponentByClass<UGuLiCombatHealthComponent>();
        if (!Health) return false;
		Out.Handle = Self->SourceHandle; Out.Team = Binding?Binding->GetState().Team:Health->GetCombatTeam();
		Out.Location = Pawn->GetActorLocation(); Out.bAlive = Health->IsAlive(); Out.CollisionActor = Self->GetOwner();
		return true;
	};
	Ledger->RegisterSource(SourceHandle, MoveTemp(Adapter));
}

bool UGuLiGroundMechWeaponComponent::AcceptAim(const FVector& Point)
{
	if (Point.ContainsNaN() || !Gun()) return false;
	const double Distance = FVector::Dist(Point, Gun()->GetComponentLocation());
	if (!FMath::IsFinite(Distance) || Distance < 1 || Distance > 600000) return false;
	AimPoint = Point; bHasAim = true; return true;
}

void UGuLiGroundMechWeaponComponent::QueueAimAssistActor(AActor* Actor)
{
	// Spawn notification may precede Blueprint component construction. Read next aim update.
	if (IsValid(Actor)) PendingAimActors.Add(Actor);
}

void UGuLiGroundMechWeaponComponent::CacheAimAssistActor(AActor* Actor)
{
	if (!IsValid(Actor) || Actor->GetWorld() != GetWorld() || Actor == GetOwner()) return;
	if (auto* States = Cast<AGuLiSoldierStateReplicator>(Actor)) AimSoldierStates = States;
	if (auto* Presentation = Cast<AGuLiCommanderPresentationActor>(Actor)) AimCommanderPresentation = Presentation;
	if (auto* Presentation = Cast<AGuLiWingmanPresentationActor>(Actor)) AimWingmanPresentation = Presentation;
	if (auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>()) AimHealthComponents.AddUnique(Health);
}

void UGuLiGroundMechWeaponComponent::RefreshAimAssistSources(uint32 MatchEpoch)
{
	if (AimMatchEpoch != MatchEpoch) ResetAimAssistSources();
	if (!bAimSourcesInitialized)
	{
		AimMatchEpoch = MatchEpoch;
		AimActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::QueueAimAssistActor));
		for (TActorIterator<AActor> It(GetWorld()); It; ++It) CacheAimAssistActor(*It);
		bAimSourcesInitialized = true;
	}
	for (const auto& Actor : PendingAimActors) CacheAimAssistActor(Actor.Get());
	PendingAimActors.Reset();
	AimHealthComponents.RemoveAllSwap([](const auto& Health) { return !Health.IsValid(); }, EAllowShrinking::No);
}

void UGuLiGroundMechWeaponComponent::ResetAimAssistSources()
{
	if (GetWorld() && AimActorSpawnedHandle.IsValid()) GetWorld()->RemoveOnActorSpawnedHandler(AimActorSpawnedHandle);
	AimActorSpawnedHandle.Reset();
	AimSoldierStates.Reset(); AimCommanderPresentation.Reset(); AimWingmanPresentation.Reset();
	AimHealthComponents.Reset(); PendingAimActors.Reset();
	AimMatchEpoch = 0; bAimSourcesInitialized = false;
}

bool UGuLiGroundMechWeaponComponent::InManualSplitSight(const FGuLiGroundLockInput& view,const FVector& point) const
{
    if(view.Focal.X<=0 || view.Focal.Y>=0)return false;
    const FVector up=FVector::CrossProduct(view.Forward,view.Right);
    const FVector axis=(view.Forward-view.Right*(view.Offset.X/view.Focal.X)-up*(view.Offset.Y/view.Focal.Y)).GetSafeNormal();
    const FVector relative=point-view.Origin;
    const double depth=FVector::DotProduct(relative,axis);
    const double radius=FMath::Max(ActiveSkill.SweepRadius,ActiveSkill.AimAssistEnabled?ActiveSkill.AimAssistRadiusCentimeters:0.f);
    return depth>0 && (relative-axis*depth).SizeSquared()<=FMath::Square(radius);
}

bool UGuLiGroundMechWeaponComponent::ResolveLocalWeaponAim(FVector& OutPoint)
{
	const auto* Mech = Cast<AGuLiGroundMechCharacter>(GetOwner());
	if (!CanControl(true) || !Mech || !Mech->GetCursorAimPoint(OutPoint))
	{
		ClearAILock();
		ResetAimAssistSources();
		return false;
	}
	// Excel's weapon skill row is the only tuning source. The raw sky/raycast point is the fallback.
	const bool Assist=ActiveSkill.AimAssistEnabled && ActiveSkill.AimAssistRadiusCentimeters>0;
	if (!UsesTargetLocks() && !Assist)
	{
		ResetAimAssistSources();
		return true;
	}
	const auto* PC = Cast<APlayerController>(Mech->GetController());
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	const UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	FViewport* Viewport = ViewportClient ? ViewportClient->Viewport : nullptr;
	const auto* Battle = GetWorld()->GetGameState<AGuLiBattleGameState>();
	FSceneViewProjectionData Projection;
	float MouseX = 0, MouseY = 0;
	if (!Viewport || !Viewport->HasFocus() || !Battle || Battle->GetMatchEpoch() == 0
		|| !LocalPlayer->GetProjectionData(Viewport, Projection)
		|| !Projection.IsValidViewRectangle())
	{
		ClearAILock();
		ResetAimAssistSources();
		return true;
	}
	const FIntRect ViewRect = Projection.GetConstrainedViewRect();
	MouseX = (ViewRect.Min.X+ViewRect.Max.X)*.5f;
	MouseY = (ViewRect.Min.Y+ViewRect.Max.Y)*.5f;
	const auto InView = [&ViewRect](const FVector2D& Point)
	{
		return !Point.ContainsNaN() && Point.X >= ViewRect.Min.X && Point.X < ViewRect.Max.X
			&& Point.Y >= ViewRect.Min.Y && Point.Y < ViewRect.Max.Y;
	};
	const FVector2D Mouse(MouseX, MouseY);
	if (!InView(Mouse)) { ClearAILock(); ResetAimAssistSources(); return true; }
	if(AimMatchEpoch!=Battle->GetMatchEpoch())ClearAILock();
	RefreshAimAssistSources(Battle->GetMatchEpoch());
	const FMatrix ViewProjection = Projection.ComputeViewProjectionMatrix();
	const FVector2D ViewMin(ViewRect.Min.X,ViewRect.Min.Y);
	const double Now = UGuLiHybridActorComponent::BattleSeconds(GetOwner());
	if (HasAIEngine())
	{
        AILockSight.DesignRadius=AILockRank.AperturePixels;
        AILockSight.HexRadius=AILockRank.HexPixels;
        AILockSight.UpdateAperture(Projection);
        for(auto& sight:LocalLocks)
        {
            sight.Center=AILockSight.Center; sight.Radius=AILockSight.Radius;
            sight.DesignRadius=AILockSight.DesignRadius; sight.HexRadius=AILockSight.HexRadius;
            sight.Progress=FMath::Clamp(float((Now-sight.Started)/AILockRank.Seconds),0.f,1.f);
        }
	}
	// Projection view axes are X=right, Y=up, Z=forward; do not use the mech's ControlRotation.
	const FMatrix ViewToWorld = Projection.ViewRotationMatrix.InverseFast();
	const FVector CameraRight = FVector(ViewToWorld.TransformVector(FVector::ForwardVector)).GetSafeNormal();
	const FVector CameraForward = FVector(ViewToWorld.TransformVector(FVector::UpVector)).GetSafeNormal();
	const FVector CameraOrigin = Projection.ViewOrigin;
	const FVector Muzzle = Gun()->GetSocketLocation(MuzzleSocket(NextFireSide));
	const double Range = FMath::Min(static_cast<double>(ActiveSkill.ProjectileSpeed) * ActiveSkill.ProjectileLifetime, 599999.0);
    FGuLiGroundLockInput view;
    view.Origin=CameraOrigin; view.Forward=CameraForward; view.Right=CameraRight;
    const double designScale=HasAIEngine() && AILockSight.Radius>0 ? AILockSight.DesignRadius/AILockSight.Radius : 1.;
    const FVector2D half=FVector2D(ViewRect.Size())*(.5*designScale);
    const auto& lens=Projection.ProjectionMatrix;
    view.Focal={half.X*lens.M[0][0],-half.Y*lens.M[1][1]};
    view.Offset={half.X*lens.M[2][0],-half.Y*lens.M[2][1]};
	const EGuLiTeam OwnTeam = Mech->GetPlayerState<AGuLiBattlePlayerState>()->GetTeam();
	TSet<FGuLiTargetHandle> SeenTargets;
	FGuLiTargetHandle BestTarget;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	double BestDepth = TNumericLimits<double>::Max();
    struct FCandidate { FGuLiTargetHandle Target; FVector Point; FVector2D Screen; double Distance; bool Retained; };
    TArray<FCandidate> candidates;
	auto Consider = [&](const FGuLiTargetHandle& Target, EGuLiTeam Team, const FVector& Point, AActor* CollisionActor)
	{
		if (!Target.IsValid() || SeenTargets.Contains(Target) || Team == OwnTeam
			|| Team == EGuLiTeam::Unassigned || Point.ContainsNaN()) return;
		SeenTargets.Add(Target);
		const double Depth = FVector::DotProduct(Point - CameraOrigin, CameraForward);
		const double GunDistanceSquared = FVector::DistSquared(Point, Gun()->GetComponentLocation());
		if (!FMath::IsFinite(Depth) || Depth <= 0 || GunDistanceSquared < 1 || GunDistanceSquared > FMath::Square(599999.0)
			|| FVector::DistSquared(Point, Muzzle) > FMath::Square(Range)) return;
		FVector2D Screen, Edge;
		if (!FSceneView::ProjectWorldToScreen(Point, ViewRect, ViewProjection, Screen) || Screen.ContainsNaN()) return;
		const FVector2D PlayerPixel = Screen - ViewMin;
		// The release animation starts at the departing target's current position, even outside the aperture.

		if (!InView(Screen)) return;
		const double DistanceSquared = FVector2D::DistSquared(Mouse, Screen);
		const bool BetterAssist=Assist && FSceneView::ProjectWorldToScreen(Point+CameraRight*ActiveSkill.AimAssistRadiusCentimeters,ViewRect,ViewProjection,Edge)
			&& !Edge.ContainsNaN() && DistanceSquared<=FVector2D::DistSquared(Edge,Screen)
			&& (!BestTarget.IsValid() || DistanceSquared<BestDistanceSquared || (DistanceSquared==BestDistanceSquared
				&& (Depth<BestDepth || (Depth==BestDepth && FGuLiWingmanTargetAcquisition::IsStableHandleLess(Target,BestTarget)))));
        const auto* prior=LocalLocks.FindByPredicate([&](const auto& slot){return slot.Target==Target;});
        const bool canLock=HasAIEngine()
            ? (prior?*prior:AILockSight).IncludesTarget(prior?Target:FGuLiTargetHandle{},PlayerPixel)
                && FVector::DistSquared(Mech->GetActorLocation(),Point)<=FMath::Square(double(AILockRank.RangeCm))
            : MaxLockTargets>1 && InManualSplitSight(view,Point);
        if(!BetterAssist && !canLock)return;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundMechAimAssist), true, GetOwner());
		if (CollisionActor) Query.AddIgnoredActor(CollisionActor);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, CameraOrigin, Point, ECC_Visibility, Query)
			|| GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, Point, ECC_Visibility, Query)) return;
		if(BetterAssist){BestTarget=Target; BestDistanceSquared=DistanceSquared; BestDepth=Depth; OutPoint=Point;}
		if(canLock)candidates.Add({Target,Point,PlayerPixel,DistanceSquared,prior!=nullptr});
	};

	// Reliable health/identity and accepted poses remain the target truth on both listen and remote clients.
	const auto* States = AimSoldierStates.Get();
	const auto* Presentation = AimCommanderPresentation.Get();
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	if (States && Presentation && States->GetSnapshotMatchEpoch() == AimMatchEpoch)
	{
		for (const auto& Soldier : States->GetItems())
		{
			if (!Soldier.IsAlive() || Soldier.bPhased || Soldier.Team == OwnTeam) continue;
			FTransform Pose;
			if (!Presentation->TryGetAuthoritativeSoldierTransform(Soldier.SoldierId, Pose)) continue;
			const FVector* Offset = Data && Data->IsWeaponMountCatalogValid() ? Data->FindAimOffset(Soldier.UnitTypeId) : nullptr;
			FVector LocalPoint;
			if (Offset) LocalPoint = *Offset;
			else
			{
				const FBox Bounds = Presentation->GetUnitModelBoundsCentimeters(Soldier.UnitTypeId);
				if (!Bounds.IsValid) continue;
				LocalPoint = Bounds.GetCenter();
			}
			Consider(GuLiCombatTargets::MakeCommanderSoldierTargetHandle(AimMatchEpoch, Soldier.SoldierId.Value),
				Soldier.Team, Pose.TransformPosition(LocalPoint), nullptr);
		}
	}
	for (const auto& WeakHealth : AimHealthComponents)
	{
		const auto* Health = WeakHealth.Get();
		AActor* Actor = Health ? Health->GetOwner() : nullptr;
		if (!IsValid(Actor) || !Health->IsAlive() || !Actor->CanBeDamaged() || Actor->IsHidden()
			|| UGuLiExternalUnitControlComponent::IsActorPhased(Actor)) continue;
		const FBox Bounds = Actor->GetComponentsBoundingBox(false);
		if (Bounds.IsValid) Consider(Health->GetTargetHandle(), Health->GetCombatTeam(), Bounds.GetCenter(), Actor);
	}
	if (auto* Wingmen = AimWingmanPresentation.Get())
	{
		TArray<FGuLiWingmanAcceptedTargetPose> Poses;
		Wingmen->GetFreshAcceptedTargetPoses(Battle->GetServerWorldTimeSeconds(),
			FGuLiWingmanTargetAcquisition::MaximumAcceptedPoseAgeSeconds, Poses);
		const TArray<FGuLiCommanderRoleSlotState> Roles = Battle->GetRoleSlots();
		for (const auto& Pose : Poses)
		{
			if (!Wingmen->IsWingmanInteractable(Pose.Wingman)) continue;
			const auto* Role = Roles.FindByPredicate([&Pose](const auto& Slot)
				{ return Slot.bOccupied && Slot.PlayerGuid == Pose.LeaseOwnerPlayerGuid; });
			if (Role) Consider(GuLiCombatTargets::MakeWingmanTargetHandle(Pose.Wingman), Role->Team,
				Pose.Transform.GetLocation(), Wingmen->FindPresentedPawn(Pose.Wingman));
		}
	}
    if(!UsesTargetLocks())return true;
    candidates.Sort([](const FCandidate& a,const FCandidate& b)
    {
        if(a.Retained!=b.Retained)return a.Retained;
        if(a.Distance!=b.Distance)return a.Distance<b.Distance;
        return FGuLiWingmanTargetAcquisition::IsStableHandleLess(a.Target,b.Target);
    });
    auto old=MoveTemp(LocalLocks); LocalLocks.Reset(); LocalLockInput=MoveTemp(view);
    AILockSight.Target={}; AILockSight.Progress=0;
    auto* hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    for(const auto& candidate:candidates)
    {
        if(LocalLocks.Num()>=MaxLockTargets)break;
        FGuLiHybridObjectKey key;
        if(!hybrid || !hybrid->ResolveTarget(candidate.Target,key))continue;
        const auto* prior=old.FindByPredicate([&](const auto& s){return s.Target==candidate.Target;});
        auto& slot=LocalLocks.Add_GetRef(prior?*prior:AILockSight);
        if(!prior){slot.Started=Now;slot.Progress=0;}
        if(!HasAIEngine())slot.Progress=1;
        slot.Target=candidate.Target; slot.TargetPoint=candidate.Screen; slot.WorldPoint=candidate.Point;
        slot.DistanceMeters=FVector::Distance(Mech->GetActorLocation(),candidate.Point)*.01;
        LocalLockInput.Candidates.Add(key);
    }
    if(!LocalLocks.IsEmpty())
    {
        const auto* ready=LocalLocks.FindByPredicate([](const auto& s){return s.IsLocked();});
        AILockSight=ready?*ready:LocalLocks[0];
        if(HasAIEngine() && ready)OutPoint=ready->WorldPoint;
    }
	return true;
}

void UGuLiGroundMechWeaponComponent::ClearAILock()
{
	AILockSight={};
    LocalLocks.Reset(); LocalLockInput={};
}

void UGuLiGroundMechWeaponComponent::SetFireHeld(bool bHeld)
{
	if (auto* Mech=Cast<AGuLiGroundMechCharacter>(GetOwner()))
	{
		FVector Point;
		const bool bForward=Mech->HybridInput.WeaponAimMode==EGuLiGroundWeaponAimMode::Forward;
		if (bHeld && !bForward && ResolveLocalWeaponAim(Point)) { Mech->HybridInput.Aim=Point; Mech->HybridInput.bHasAim=true; }
		if (bForward) ResetAimAssistSources();
		Mech->HybridInput.bFire=!HasAIEngine() && bHeld && CanControl(true) && (bForward || Mech->HybridInput.bHasAim);
	}
}

bool UGuLiGroundMechWeaponComponent::GetActualShotRay(FVector& Origin,FVector& Direction) const
{
	if (!bConfigured || !Gun()) return false;
	const FTransform Socket=Gun()->GetSocketTransform(MuzzleSocket(NextFireSide));
	Origin=Socket.GetLocation(); Direction=Socket.GetUnitAxis(EAxis::X); return true;
}

bool UGuLiGroundMechWeaponComponent::GetActualSightPoint(FVector& Point,FVector& Direction) const
{
    FVector Origin;
    if(!GetActualShotRay(Origin,Direction))return false;
    const FVector End=Origin+Direction*600000;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundHUDActualRay),false,GetOwner());
    Point=GetWorld()->LineTraceSingleByChannel(Hit,Origin,End,ECC_Visibility,Query)?Hit.ImpactPoint:End;
    return true;
}

void UGuLiGroundMechWeaponComponent::AlignGun()
{
	if (!bConfigured || FVector(AimPoint).IsNearlyZero()) return;
	if (CastChecked<AGuLiGroundMechCharacter>(GetOwner())->UsesIntegratedBodyMesh()) return;
	auto* Mesh = Gun();
	// Source art faces +Y. Two passes account for the muzzle offset from the mount pivot.
	FVector Direction = (FVector(AimPoint) - Mesh->GetComponentLocation()).GetSafeNormal();
	for (int32 Pass = 0; Pass < 2 && !Direction.IsNearlyZero(); ++Pass)
	{
		Mesh->SetWorldRotation(FRotationMatrix::MakeFromYZ(Direction, FVector::UpVector).Rotator());
		Direction = (FVector(AimPoint) - Mesh->GetSocketLocation(FName(ActiveSkill.MuzzleSocket))).GetSafeNormal();
	}
}

FGuLiGroundAttackSnapshot UGuLiGroundMechWeaponComponent::BuildAttackSnapshot() const
{
    return CachedAttack;
}
void UGuLiGroundMechWeaponComponent::OnRogueBuildChanged(FGuid Player,int64 Revision){RefreshRogueBuild(Player,Revision);}
void UGuLiGroundMechWeaponComponent::RefreshRogueBuild(FGuid Player,int64)
{
    if(!bConfigured)return;
    FGuLiGroundAttackSnapshot S;
    if(const auto* Identity=UGuLiHybridActorComponent::Find(GetOwner()))
    {S.Player=Identity->GetState().Controller;if(Player.IsValid()&&Player!=S.Player)return;TMap<FString,int32> Counts;if(!GuLiGroundRogue::ReadBuild(*GetWorld(),S.Player,Counts,S.Revision,&S.Effects))return;}
    const GuLiNet::Effects::GroundWeaponBase base{ActiveUpgrade.Damage,ActiveUpgrade.FireRate,ActiveSkill.InitialBulletDamageRatio,
        uint32(ActiveSkill.BulletVfxId),uint32(ActiveSkill.InitialBulletVfxId),uint32(ActiveSkill.ImpactVfxId),uint32(ActiveSkill.MuzzleVfxId)};
    GuLiNet::Effects::GroundShotStats shot;
    if(!GuLiNet::Effects::ResolveGroundShot(base,S.Effects,shot) || !GuLiNet::Effects::ResolveHitProperties(S.Effects,S.Hit))return;
    const float Rate=shot.FireRate;
    S.Damage=shot.Damage; S.BulletVfxId=int32(shot.BulletVfx);
    S.ImpactVfxId=int32(shot.ImpactVfx); S.MuzzleVfxId=int32(shot.MuzzleVfx);
    if(EffectiveFireRate>0&&EffectiveFireRate!=Rate)NextShotTime=GuLiMechFire::RescaleCooldown(UGuLiHybridActorComponent::BattleSeconds(GetOwner()),NextShotTime,EffectiveFireRate,Rate);
    S.FireRate=Rate;EffectiveFireRate=Rate;CachedAttack=MoveTemp(S);
    const bool wasAutomatic=HasAIEngine();
    AILockRank=GuLiNet::Effects::GetAILockRank(CachedAttack.Effects);
    MaxLockTargets=int32(GuLiNet::Effects::LockCapacity(CachedAttack.Effects));
    if(wasAutomatic!=HasAIEngine() || !UsesTargetLocks()){ClearAILock();AuthorityLockStarts.Reset();}
    if(auto* Anim=Cast<UGuLiGroundMechAnimInstance>(Gun()->GetAnimInstance()))Anim->SetFireAnimationRate(Rate);
    if(UGuLiNetworkSettings::CanPresent(GetWorld()))
    {const auto Extra=GuLiNet::Effects::GetExplosive(CachedAttack.Effects);if(Extra.Interval()){GuLiVfx::Load<UNiagaraSystem>(this,int32(Extra.BulletVfx));GuLiVfx::Load<UNiagaraSystem>(this,int32(Extra.ImpactVfx));}}
}
bool UGuLiGroundMechWeaponComponent::CheckAuthorityTarget(const FGuLiHybridObjectKey& key,uint8 side,
    double progress,bool retain,FVector& point) const
{
    const auto* mech=CastChecked<AGuLiGroundMechCharacter>(GetOwner());
    const auto& view=mech->GetHybridSimulationInput().Lock;
    const auto* identity=UGuLiHybridActorComponent::Find(mech);
    auto* hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    auto* ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
    FGuLiTargetHandle target;
    FGuLiCombatTargetSnapshot body;
    if(!identity || !hybrid || !ledger || !hybrid->ResolveObjectTarget(key,target) ||
       !ledger->TryGetTargetSnapshot(target,body) || !body.bAlive || body.Team==EGuLiTeam::Unassigned ||
       body.Team==identity->GetState().Team)return false;
    point=body.GetHitCenter();
    const FVector relative=point-view.Origin;
    const double depth=FVector::DotProduct(relative,view.Forward);
    if(depth<=0)return false;
    if(HasAIEngine())
    {
        if(FVector::DistSquared(mech->GetActorLocation(),point)>FMath::Square(double(AILockRank.RangeCm)))return false;
        const FVector up=FVector::CrossProduct(view.Forward,view.Right);
        const FVector2D pixel=view.Offset+FVector2D(FVector::DotProduct(relative,view.Right)*view.Focal.X/depth,
                                                    FVector::DotProduct(relative,up)*view.Focal.Y/depth);
        FGuLiAILockSight sight;
        sight.bProjected=true; sight.DesignRadius=sight.Radius=AILockRank.AperturePixels;
        sight.HexRadius=AILockRank.HexPixels; sight.Progress=progress;
        if(retain)sight.Target=target;
        if(!sight.IncludesTarget(target,pixel))return false;
    }
    else if(!InManualSplitSight(view,point) || FVector::DistSquared(Gun()->GetSocketLocation(MuzzleSocket(side)),point)
            >FMath::Square(double(ActiveSkill.ProjectileSpeed)*ActiveSkill.ProjectileLifetime))return false;
    FCollisionQueryParams query(SCENE_QUERY_STAT(GroundAILock),true,mech);
    if(body.CollisionActor.IsValid())query.AddIgnoredActor(body.CollisionActor.Get());
    return !GetWorld()->LineTraceTestByChannel(view.Origin,point,ECC_Visibility,query) &&
           !GetWorld()->LineTraceTestByChannel(Gun()->GetSocketLocation(MuzzleSocket(side)),point,ECC_Visibility,query);
}
void UGuLiGroundMechWeaponComponent::UpdateAuthorityLocks(bool mayAim)
{
    if(!mayAim || !UsesTargetLocks() || !CanControl(false)){AuthorityLockStarts.Reset();return;}
    const auto& candidates=CastChecked<AGuLiGroundMechCharacter>(GetOwner())->GetHybridSimulationInput().Lock.Candidates;
    const double now=UGuLiHybridActorComponent::BattleSeconds(GetOwner());
    TMap<FGuLiHybridObjectKey,double> next;
    for(const auto& key:candidates)
    {
        if(next.Num()>=MaxLockTargets)break;
        const double* old=AuthorityLockStarts.Find(key);
        const double start=old && *old<=now?*old:now;
        FVector point;
        const double progress=HasAIEngine()?FMath::Clamp((now-start)/AILockRank.Seconds,0.,1.):1.;
        if(CheckAuthorityTarget(key,NextFireSide,progress,old!=nullptr,point))
            next.Add(key,start);
    }
    AuthorityLockStarts=MoveTemp(next);
}

void UGuLiGroundMechWeaponComponent::QueueFireAnimationNotify(uint8 Side)
{
    // Only an authoritative, positive-delta animation step may enqueue a shot.
    if(!bAcceptingFireNotifies || !bFireHeld || Side!=NextFireSide)return;
    const double Now=UGuLiHybridActorComponent::BattleSeconds(GetOwner());
    PendingFireNotifications.Add({Side,++FireSerial,Now});NextFireSide^=1;
    NextShotTime=Now+1./EffectiveFireRate;
}
void UGuLiGroundMechWeaponComponent::TryFire()
{
    bAcceptingFireNotifies=false;
    if(auto* Anim=Cast<UGuLiGroundMechAnimInstance>(Gun()->GetAnimInstance()))SavedFirePhase=Anim->GetFireLoopPhase();
    auto Pending=MoveTemp(PendingFireNotifications);PendingFireNotifications.Reset();
    for(const auto& Event:Pending)FireFromAnimation(Event.Side,Event.Serial,Event.Time);
}
void UGuLiGroundMechWeaponComponent::FireFromAnimation(uint8 MuzzleIndex,uint64 Serial,double FireTime)
{
    if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !bFireHeld || !bHasAim || !CanControl(false)) return;
    auto* Pool=GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();if(!Pool)return;
    const auto* Identity=UGuLiHybridActorComponent::Find(GetOwner());if(!Identity)return;
    FGuLiPooledProjectileLaunch Launch;
    Launch.Context.MatchEpoch=SourceEpoch;Launch.Context.Source=SourceHandle;
    const FString StableShot=Identity->GetObjectKey().StableId.ToString(EGuidFormats::Digits)
        +FString::Printf(TEXT("/life/%u/shot/%llu"),Identity->GetObjectKey().LifeGeneration,Serial);
    Launch.Context.ShotId=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/GroundMech"),EGuLiHybridObjectKind::Flight,StableShot).StableId;
    Launch.Context.RootEventId=Launch.Context.ShotId;Launch.Context.Damage=GetShotDamage();
    Launch.Context.SkillId=FName(*CurrentUpgradeId);Launch.Context.WeaponBinding.SlotId=TEXT("BasicAttack");
    Launch.Position=Gun()->GetSocketLocation(MuzzleSocket(MuzzleIndex));
    Launch.Direction=Gun()->GetSocketTransform(MuzzleSocket(MuzzleIndex)).GetUnitAxis(EAxis::X);
    const auto* Mech=CastChecked<AGuLiGroundMechCharacter>(GetOwner());
    const auto Transform=GetOwner()->GetActorTransform();
    Launch.MuzzleOffset=Transform.InverseTransformPosition(Launch.Position);
    Launch.GroundOutward=FRotationMatrix(FRotator(0,Mech->GetUpperAimState(false).WorldYaw,0)).GetUnitAxis(EAxis::Y)*(MuzzleIndex==0?1.:-1.);
    Launch.GroundOutwardLocal=Transform.InverseTransformVectorNoScale(Launch.GroundOutward);
    Launch.Speed=ActiveSkill.ProjectileSpeed;Launch.Lifetime=ActiveSkill.ProjectileLifetime;
    Launch.MaximumDistance=Launch.Speed*Launch.Lifetime;Launch.SweepRadius=ActiveSkill.SweepRadius;
    Launch.ServerTime=FireTime;Launch.FireNotifyTime=FireTime;Launch.FireOrdinal=Serial;Launch.GroundMuzzleIndex=MuzzleIndex;
    Launch.PlayerBulletVfxId=CachedAttack.BulletVfxId;Launch.GroundAttack=BuildAttackSnapshot();
    const FGuid volley=Launch.Context.ShotId;
    int32 rounds=0;
    const auto& input=Mech->GetHybridSimulationInput().Lock;
    for(const auto& key:input.Candidates)
    {
        if(rounds>=MaxLockTargets)break;
        const double* start=AuthorityLockStarts.Find(key);
        FVector point;
        if(!start || FireTime-*start+UE_SMALL_NUMBER<AILockRank.Seconds ||
           !CheckAuthorityTarget(key,MuzzleIndex,1,true,point))continue;
        auto child=Launch;
        child.Direction=(point-Launch.Position).GetSafeNormal();
        if(child.Direction.IsNearlyZero())continue;
        auto* hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        if(!hybrid || !hybrid->ResolveObjectTarget(key,child.Context.Target))continue;
        if(rounds>0)
        {
            const FString suffix=FString::Printf(TEXT("/target/%u/%s/%u"),uint32(key.Kind),*key.StableId.ToString(EGuidFormats::Digits),key.LifeGeneration);
            child.Context.ShotId=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/GroundMech"),EGuLiHybridObjectKind::Flight,StableShot+suffix).StableId;
            child.Context.RootEventId=child.Context.ShotId;
            child.bSecondaryBarrelRound=true;
        }
        if(child.GroundAttack.Hit.Instant)child.MaximumDistance=FVector::Dist(Launch.Position,point);
        if(Pool->Launch(child).IsValid())++rounds;
    }
    if(!rounds && !HasAIEngine() && Pool->Launch(Launch).IsValid())++rounds;
    if(rounds)
    {
        ++ShotsFired;
        PresentAnimationFire(volley,float(FireTime),MuzzleIndex,Launch.GroundAttack.MuzzleVfxId);
    }
}

void UGuLiGroundMechWeaponComponent::PresentAnimationFire(FGuid ShotId,float ShotTime,uint8 MuzzleIndex,int32 FrozenMuzzleVfxId)
{
	if (!bConfigured || MuzzleIndex > 1 || SeenShots.Contains(ShotId)) return;
	if (SeenShots.Num() >= 256) SeenShots.Reset();
	SeenShots.Add(ShotId);
	const double Clock=UGuLiHybridActorComponent::BattleSeconds(GetOwner());
    const float Age = FMath::Max(0.f,float(Clock-ShotTime));

	// The source barrel clip is longer than the recoil flash. Late accepted cues still seek
	// the independent barrel player, but never replay an expired muzzle flash.
	if (auto* BodyAnim = Cast<UGuLiGroundMechAnimInstance>(Gun()->GetAnimInstance())) BodyAnim->TriggerIntegratedRecoil(Age,MuzzleIndex);
	if (Age >= ActiveSkill.RecoilDuration) return;
	if (ConfiguredAnim.IsValid()) ConfiguredAnim->TriggerRecoil(Age);
	if (!UGuLiNetworkSettings::CanPresent(GetWorld())) return;
	++CosmeticShots;
    // Only accepted, deduplicated, unexpired shots reach the local HUD.
	CastChecked<AGuLiGroundMechCharacter>(GetOwner())->NotifyLocalPresentation({EGuLiGroundPresentationAction::Fire,EGuLiGroundPresentationPhase::Started,Age});
	const FName Socket = MuzzleSocket(MuzzleIndex);
    const int32 Vfx=FrozenMuzzleVfxId==INDEX_NONE?ActiveSkill.MuzzleVfxId:FrozenMuzzleVfxId;
    auto* System=Vfx==ActiveSkill.MuzzleVfxId?MuzzleSystem.Get():GuLiVfx::Load<UNiagaraSystem>(this,Vfx);
	if (auto* Effect = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Gun(), Socket,
		FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false, false, ENCPoolMethod::ManualRelease, false))
	{
		Effect->SetUsingAbsoluteScale(true);
		// This emitter's User.Size drives both beam length and width. Component
		// scaling alone does not scale Niagara ribbon/sprite widths consistently.
		Effect->SetWorldScale3D(FVector::OneVector);
		Effect->SetVariableFloat(TEXT("User.Size"), GuLiVfx::Scale(this, Vfx).X);
		// The weapon mount mirrors X. Removing inherited scale also removes that
		// reflection, so orient the unscaled effect along the actual socket axis.
		Effect->SetUsingAbsoluteRotation(true);
		Effect->SetWorldRotation(Gun()->GetSocketTransform(Socket).TransformVector(FVector::ForwardVector).Rotation());
		Effect->SetVariableFloat(TEXT("User.GlobleDuration"), .08f);
		Effect->SetCastShadow(false); Effect->Activate(true); MuzzleEffects.Add(Effect);
	}
}

void UGuLiGroundMechWeaponComponent::TickComponent(float DeltaSeconds, ELevelTick Type, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(DeltaSeconds, Type, Tick);
	for (int32 Index = MuzzleEffects.Num()-1; Index >= 0; --Index)
	{
		UNiagaraComponent* Effect = MuzzleEffects[Index];
		if (IsValid(Effect) && !Effect->IsComplete())
			Effect->SetWorldRotation(Gun()->GetSocketTransform(Effect->GetAttachSocketName()).TransformVector(FVector::ForwardVector).Rotation());
		if (!IsValid(Effect) || Effect->IsComplete())
		{ if (IsValid(Effect)) Effect->ReleaseToPool(); MuzzleEffects.RemoveAtSwap(Index); }
	}
}
void UGuLiGroundMechWeaponComponent::AdvanceHybrid(float DeltaSeconds,bool bFire,const FVector& Aim,bool bAim)
{
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !bConfigured) return;
	RegisterSource(); bHasAim=bAim && AcceptAim(Aim);
    bool wantsFire=bFire;
    if(HasAIEngine())
    {
        wantsFire=false;
        const double now=UGuLiHybridActorComponent::BattleSeconds(GetOwner());
        for(const auto& lock:AuthorityLockStarts)
            if(now-lock.Value+UE_SMALL_NUMBER>=AILockRank.Seconds){wantsFire=true;break;}
    }
    bFireHeld=wantsFire && bHasAim && CanControl(false);
    AlignGun();
    auto* Anim=Cast<UGuLiGroundMechAnimInstance>(Gun()->GetAnimInstance());if(!Anim)return;
    bAcceptingFireNotifies=bFireHeld && EffectiveFireRate>0 && CachedAttack.Damage>0;
    if(!bAcceptingFireNotifies){if(Anim->IsFireLoopPlaying())Anim->StopFireLoop();return;}
    if(!Anim->IsFireLoopPlaying())
    {
        const double Remaining=FMath::Max(0.,NextShotTime-UGuLiHybridActorComponent::BattleSeconds(GetOwner()));
        const float Phase=bRestoreFirePhase?SavedFirePhase:float(FMath::Fmod(NextFireSide*.5-Remaining*EffectiveFireRate*.5+1.,1.));
        bRestoreFirePhase=false;Anim->StartFireLoop(Phase,EffectiveFireRate,true);
    }
}

void UGuLiGroundMechWeaponComponent::EndPlay(EEndPlayReason::Type Reason)
{
    if(auto* Cards=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>())Cards->OnGroundBuildChanged.RemoveDynamic(this,&UGuLiGroundMechWeaponComponent::OnRogueBuildChanged);
	bFireHeld = false;
	ResetAimAssistSources();
	if (auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()) Ledger->UnregisterSource(SourceHandle, this);
	for (UNiagaraComponent* Effect : MuzzleEffects) if (IsValid(Effect)) { Effect->DeactivateImmediate(); Effect->ReleaseToPool(); }
	MuzzleEffects.Reset();
	Super::EndPlay(Reason);
}


void UGuLiGroundMechWeaponComponent::PlayFlightShot(FGuid ShotId,float ShotTime,uint8 MuzzleIndex,int32 FrozenMuzzleVfxId,float FrozenFireRate)
{
    if (!bConfigured || SeenShots.Contains(ShotId) || UGuLiHybridActorComponent::HasSimulation(GetOwner())) return;
    const float Rate=FrozenFireRate>0?FrozenFireRate:EffectiveFireRate;
    const float Age=FMath::Max(0.f,float(UGuLiHybridActorComponent::BattleSeconds(GetOwner())-ShotTime));
    if (Rate<=0 || Age>=2.f/Rate) return;
    if (auto* Anim=Cast<UGuLiGroundMechAnimInstance>(Gun()->GetAnimInstance()))
        Anim->PresentFireLoop(MuzzleIndex,Rate,Age);
    PresentAnimationFire(ShotId,ShotTime,MuzzleIndex,FrozenMuzzleVfxId);
}
