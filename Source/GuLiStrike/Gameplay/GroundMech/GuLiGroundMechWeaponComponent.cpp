#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponAnimInstance.h"
#include "Gameplay/GroundMech/GuLiGroundMechAnimInstance.h"
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
	const FGuLiStrikeMechSkillsRow* Skill = nullptr;
	if (SkillTable->GetRowStruct() != FGuLiStrikeMechSkillsRow::StaticStruct()) return false;
	for (const auto& Pair : SkillTable->GetRowMap())
	{
		const auto* Row = reinterpret_cast<const FGuLiStrikeMechSkillsRow*>(Pair.Value);
		if (Row->Id == Upgrade->SkillId) { Skill = Row; break; }
	}
	if (!Skill || !FMath::IsFinite(Skill->ProjectileSpeed) || Skill->ProjectileSpeed <= 0
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
	if (!Curve) return false;
	if (Integrated)
	{
		auto* BodyAnim = Cast<UGuLiGroundMechAnimInstance>(Gun()->GetAnimInstance());
		if (!BodyAnim || !Gun()->DoesSocketExist(TEXT("Muzzle_L"))) return false;
		BodyAnim->ConfigureIntegratedRecoil(Curve,Skill->RecoilDuration);
		ActiveSkill = *Skill; ActiveUpgrade = *Upgrade; MuzzleSystem = Muzzle;
		ConfiguredAnim.Reset(); bConfigured = true;
		return true;
	}
	if (!Animation || !Animation->IsChildOf(UGuLiGroundMechWeaponAnimInstance::StaticClass())) return false;
	ActiveSkill = *Skill; ActiveUpgrade = *Upgrade; MuzzleSystem = Muzzle;
	if (Gun()->GetAnimClass() != Animation) Gun()->SetAnimInstanceClass(Animation);
	auto* Anim = Cast<UGuLiGroundMechWeaponAnimInstance>(Gun()->GetAnimInstance());
	if (!Anim) return false;
	Anim->Configure(Curve, FName(Skill->RecoilBone), Skill->RecoilTargetLocalZCentimeters, Skill->RecoilDuration);
	ConfiguredAnim = Anim; bConfigured = true;
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

bool UGuLiGroundMechWeaponComponent::ResolveLocalWeaponAim(FVector& OutPoint)
{
	const auto* Mech = Cast<AGuLiGroundMechCharacter>(GetOwner());
	if (!CanControl(true) || !Mech || !Mech->GetCursorAimPoint(OutPoint))
	{
		ResetAimAssistSources();
		return false;
	}
	// Excel's weapon skill row is the only tuning source. The raw sky/raycast point is the fallback.
	if (!ActiveSkill.AimAssistEnabled || ActiveSkill.AimAssistRadiusCentimeters <= 0)
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
	if (!InView(Mouse)) { ResetAimAssistSources(); return true; }
	RefreshAimAssistSources(Battle->GetMatchEpoch());
	const FMatrix ViewProjection = Projection.ComputeViewProjectionMatrix();
	// Projection view axes are X=right, Y=up, Z=forward; do not use the mech's ControlRotation.
	const FMatrix ViewToWorld = Projection.ViewRotationMatrix.InverseFast();
	const FVector CameraRight = FVector(ViewToWorld.TransformVector(FVector::ForwardVector)).GetSafeNormal();
	const FVector CameraForward = FVector(ViewToWorld.TransformVector(FVector::UpVector)).GetSafeNormal();
	const FVector CameraOrigin = Projection.ViewOrigin;
	const FVector Muzzle = Gun()->GetSocketLocation(MuzzleSocket(ShotsFired & 1));
	const double Range = FMath::Min(static_cast<double>(ActiveSkill.ProjectileSpeed) * ActiveSkill.ProjectileLifetime, 599999.0);
	const EGuLiTeam OwnTeam = Mech->GetPlayerState<AGuLiBattlePlayerState>()->GetTeam();
	TSet<FGuLiTargetHandle> SeenTargets;
	FGuLiTargetHandle BestTarget;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	double BestDepth = TNumericLimits<double>::Max();
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
		if (!FSceneView::ProjectWorldToScreen(Point, ViewRect, ViewProjection, Screen) || !InView(Screen)
			|| !FSceneView::ProjectWorldToScreen(Point + CameraRight * ActiveSkill.AimAssistRadiusCentimeters,
				ViewRect, ViewProjection, Edge) || Edge.ContainsNaN()) return;
		const double RadiusSquared = FVector2D::DistSquared(Edge, Screen);
		const double DistanceSquared = FVector2D::DistSquared(Mouse, Screen);
		if (!FMath::IsFinite(RadiusSquared) || RadiusSquared <= 0 || DistanceSquared > RadiusSquared) return;
		if (BestTarget.IsValid() && (DistanceSquared > BestDistanceSquared
			|| (DistanceSquared == BestDistanceSquared && (Depth > BestDepth
				|| (Depth == BestDepth && !FGuLiWingmanTargetAcquisition::IsStableHandleLess(Target, BestTarget)))))) return;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundMechAimAssist), true, GetOwner());
		if (CollisionActor) Query.AddIgnoredActor(CollisionActor);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, CameraOrigin, Point, ECC_Visibility, Query)
			|| GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, Point, ECC_Visibility, Query)) return;
		BestTarget = Target; BestDistanceSquared = DistanceSquared; BestDepth = Depth; OutPoint = Point;
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
	return true;
}

void UGuLiGroundMechWeaponComponent::SetFireHeld(bool bHeld)
{
	if (auto* Mech=Cast<AGuLiGroundMechCharacter>(GetOwner()))
	{
		FVector Point;
		if (bHeld && ResolveLocalWeaponAim(Point)) { Mech->HybridInput.Aim=Point; Mech->HybridInput.bHasAim=true; }
		Mech->HybridInput.bFire=bHeld && CanControl(true) && Mech->HybridInput.bHasAim;
	}
}

bool UGuLiGroundMechWeaponComponent::GetActualShotRay(FVector& Origin,FVector& Direction) const
{
	if (!bConfigured || !Gun()) return false;
	const FTransform Socket=Gun()->GetSocketTransform(MuzzleSocket(ShotsFired&1));
	Origin=Socket.GetLocation(); Direction=Socket.GetUnitAxis(EAxis::X); return true;
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

void UGuLiGroundMechWeaponComponent::TryFire()
{
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !bFireHeld || !bHasAim || !CanControl(false)) return;
	const double Now = UGuLiHybridActorComponent::BattleSeconds(GetOwner());
	if (Now + 1.e-6 < NextShotTime) return;
	auto* Pool = GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();
	if (!Pool) return;
	FGuLiPooledProjectileLaunch Launch;
	Launch.Context.MatchEpoch = SourceEpoch; Launch.Context.Source = SourceHandle;
	Launch.Context.ShotId = FGuid::NewGuid(); Launch.Context.RootEventId = Launch.Context.ShotId;
	if (const auto* Identity=UGuLiHybridActorComponent::Find(GetOwner()))
	{
		const FString StableShot=Identity->GetObjectKey().StableId.ToString(EGuidFormats::Digits)
			+FString::Printf(TEXT("/life/%u/shot/%d"),Identity->GetObjectKey().LifeGeneration,ShotsFired+1);
		Launch.Context.ShotId=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/GroundMech"),EGuLiHybridObjectKind::Flight,StableShot).StableId;
		Launch.Context.RootEventId=Launch.Context.ShotId;
	}
	Launch.Context.Damage = ActiveUpgrade.Damage;
	Launch.Context.SkillId = FName(*CurrentUpgradeId);
	Launch.Context.WeaponBinding.SlotId=TEXT("BasicAttack");
	const uint8 MuzzleIndex = CastChecked<AGuLiGroundMechCharacter>(GetOwner())->UsesIntegratedBodyMesh() ? (ShotsFired & 1) : 0;
	Launch.Position = Gun()->GetSocketLocation(MuzzleSocket(MuzzleIndex));
	const auto* Mech=Cast<AGuLiGroundMechCharacter>(GetOwner());
	Launch.Direction = Mech && Mech->UsesIntegratedBodyMesh()
		? Gun()->GetSocketTransform(MuzzleSocket(MuzzleIndex)).GetUnitAxis(EAxis::X)
		: (FVector(AimPoint) - Launch.Position).GetSafeNormal();
	Launch.MuzzleOffset = GetOwner()->GetActorTransform().InverseTransformPosition(Launch.Position);
	Launch.Speed = ActiveSkill.ProjectileSpeed; Launch.Lifetime = ActiveSkill.ProjectileLifetime;
	Launch.MaximumDistance = Launch.Speed * Launch.Lifetime; Launch.SweepRadius = ActiveSkill.SweepRadius;
	Launch.ServerTime = Now; Launch.PlayerBulletVfxId = ActiveSkill.BulletVfxId;
	if (!Pool->Launch(Launch).IsValid()) return;
	++ShotsFired;
	const double Interval = 1. / ActiveUpgrade.FireRate;
	NextShotTime = NextShotTime > 0 && Now - NextShotTime <= 1./30. + 1.e-6 ? NextShotTime + Interval : Now + Interval;
	FGuLiCombatShotCue Cue;
	Cue.MatchEpoch = SourceEpoch; Cue.ShotId = Launch.Context.ShotId; Cue.Source = SourceHandle;
	Cue.Start = Launch.Position; Cue.End = Launch.Position+Launch.Direction*FVector::Distance(Launch.Position,FVector(AimPoint)); Cue.ServerTime = Now;
	Cue.MuzzleDirection = Launch.Direction; Cue.bMuzzleOnly = true;
	Cue.MuzzleIndex = MuzzleIndex;
	Cue.SlotId = TEXT("BasicAttack");
	UGuLiCombatEffectReplicationComponent::AttachFlightMuzzle(GetWorld(),Cue);
	PlayFlightShot(Cue.ShotId,Cue.ServerTime,Cue.MuzzleIndex);
}

void UGuLiGroundMechWeaponComponent::PlayFlightShot(FGuid ShotId, float ShotTime, uint8 MuzzleIndex)
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
	const FName Socket = MuzzleSocket(MuzzleIndex);
	if (auto* Effect = UNiagaraFunctionLibrary::SpawnSystemAttached(MuzzleSystem, Gun(), Socket,
		FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false, false, ENCPoolMethod::ManualRelease, false))
	{
		Effect->SetUsingAbsoluteScale(true); Effect->SetWorldScale3D(GuLiVfx::Scale(this, ActiveSkill.MuzzleVfxId));
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
	RegisterSource(); bHasAim=bAim && AcceptAim(Aim); bFireHeld=bFire && bHasAim && CanControl(false);
	AlignGun(); TryFire();
}

void UGuLiGroundMechWeaponComponent::EndPlay(EEndPlayReason::Type Reason)
{
	bFireHeld = false;
	ResetAimAssistSources();
	if (auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()) Ledger->UnregisterSource(SourceHandle, this);
	for (UNiagaraComponent* Effect : MuzzleEffects) if (IsValid(Effect)) { Effect->DeactivateImmediate(); Effect->ReleaseToPool(); }
	MuzzleEffects.Reset();
	Super::EndPlay(Reason);
}
