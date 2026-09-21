#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponAnimInstance.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanTargetAcquisition.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Curves/CurveFloat.h"

double GuLiMechFire::RescaleCooldown(double Now, double NextShot, float OldRate, float NewRate)
{
	return Now + FMath::Clamp((NextShot - Now) * OldRate, 0., 1.) / NewRate;
}

UGuLiGroundMechWeaponComponent::UGuLiGroundMechWeaponComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

USkeletalMeshComponent* UGuLiGroundMechWeaponComponent::Gun() const
{
	const auto* Pawn = Cast<AGuLiGroundMechCharacter>(GetOwner());
	return Pawn ? Pawn->GetMachinegun() : nullptr;
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
	if (Gun()) { Gun()->AddTickPrerequisiteComponent(this); Gun()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; }
}

bool UGuLiGroundMechWeaponComponent::LoadConfiguration(const FString& Id)
{
	if (!bWeaponEnabled || !UpgradeTable || !SkillTable) return false;
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
		|| Skill->RecoilDuration <= 0 || Gun()->GetBoneIndex(FName(Skill->RecoilBone)) == INDEX_NONE
		|| !Gun()->DoesSocketExist(FName(Skill->MuzzleSocket))) return false;
	auto* Curve = Cast<UCurveFloat>(Skill->RecoilCurve.LoadSynchronous());
	auto* Animation = Skill->WeaponAnimation.LoadSynchronous();

	auto* Muzzle = GuLiVfx::Load<UNiagaraSystem>(this, Skill->MuzzleVfxId);
	if (!Curve || !Animation || !Animation->IsChildOf(UGuLiGroundMechWeaponAnimInstance::StaticClass())) return false;
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
	if (!GetOwner()->HasAuthority()) return false;
	const float OldRate = ActiveUpgrade.FireRate;
	if (!LoadConfiguration(Id)) return false;
	NextShotTime = GuLiMechFire::RescaleCooldown(GetWorld()->GetTimeSeconds(), NextShotTime, OldRate, ActiveUpgrade.FireRate);
	CurrentUpgradeId = Id; GetOwner()->ForceNetUpdate();
	return true;
}

void UGuLiGroundMechWeaponComponent::OnRep_Upgrade() { LoadConfiguration(CurrentUpgradeId); }

bool UGuLiGroundMechWeaponComponent::CanControl(bool bLocal) const
{
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* State = Pawn ? Pawn->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const auto* PC = Pawn ? Cast<AGuLiCommanderPlayerController>(Pawn->GetController()) : nullptr;
	return bConfigured && Pawn && PC && State && State->IsBattleReady() && State->GetBattleRole() == EGuLiCommanderRole::Ground
		&& (State->GetTeam() == EGuLiTeam::Red || State->GetTeam() == EGuLiTeam::Blue)
		&& !PC->IsMoveInputIgnored() && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn)
		&& (!bLocal || (Pawn->IsLocallyControlled() && PC->CanUseGroundMechFireInput()));
}

void UGuLiGroundMechWeaponComponent::RegisterSource()
{
	if (!GetOwner()->HasAuthority() || !bConfigured) return;
	auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!Ledger || !Ledger->GetMatchEpoch()) return;
	if (SourceEpoch == Ledger->GetMatchEpoch() && SourceHandle.IsValid()) return;
	if (SourceHandle.IsValid()) Ledger->UnregisterSource(SourceHandle, this);
	bFireHeld = false;
	SourceEpoch = Ledger->GetMatchEpoch();
	SourceHandle = {}; SourceHandle.Kind = EGuLiTargetKind::GroundActor;
	SourceHandle.AuthorityId = FGuid::NewGuid(); SourceHandle.Generation = 1; SourceHandle.LocalId = 1;
	FGuLiCombatSourceAdapter Adapter; Adapter.LifetimeOwner = this;
	TWeakObjectPtr<UGuLiGroundMechWeaponComponent> Weak(this);
	Adapter.ReadSnapshot = [Weak](FGuLiCombatTargetSnapshot& Out)
	{
		const auto* Self = Weak.Get();
		if (!Self || !IsValid(Self->GetOwner())) return false;
		const auto* Pawn = CastChecked<APawn>(Self->GetOwner());
		if (!Pawn->GetPlayerState<AGuLiBattlePlayerState>()) return false;
		Out.Handle = Self->SourceHandle; Out.Team = Pawn->GetPlayerState<AGuLiBattlePlayerState>()->GetTeam();
		Out.Location = Pawn->GetActorLocation(); Out.bAlive = Self->CanControl(false); Out.CollisionActor = Self->GetOwner();
		return true;
	};
	Ledger->RegisterSource(SourceHandle, MoveTemp(Adapter)); GetOwner()->ForceNetUpdate();
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
		|| !PC->GetMousePosition(MouseX, MouseY) || !LocalPlayer->GetProjectionData(Viewport, Projection)
		|| !Projection.IsValidViewRectangle())
	{
		ResetAimAssistSources();
		return true;
	}
	const FIntRect ViewRect = Projection.GetConstrainedViewRect();
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
	const FVector Muzzle = Gun()->GetSocketLocation(FName(ActiveSkill.MuzzleSocket));
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
	if (!bHeld) bFireHeld = false;
	const auto* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled()) { ResetAimAssistSources(); return; }
	if (bHeld)
	{
		FVector Point;
		bHasAim = ResolveLocalWeaponAim(Point) && AcceptAim(Point);
	}
	bHeld = bHeld && CanControl(true) && bHasAim;
	bFireHeld = bHeld;
	ServerSetFireHeld(bHeld, AimPoint);
}

void UGuLiGroundMechWeaponComponent::ServerSetFireHeld_Implementation(bool bHeld, FVector_NetQuantize Point)
{
	RegisterSource();
	bFireHeld = bHeld && CanControl(false) && AcceptAim(Point);
	if (bFireHeld) { AlignGun(); TryFire(); }
}

void UGuLiGroundMechWeaponComponent::ServerUpdateAim_Implementation(FVector_NetQuantize Point)
{
	if (CanControl(false)) AcceptAim(Point);
}

void UGuLiGroundMechWeaponComponent::AlignGun()
{
	if (!bConfigured || FVector(AimPoint).IsNearlyZero()) return;
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
	if (!GetOwner()->HasAuthority() || !bFireHeld || !bHasAim || !CanControl(false)) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now + 1.e-6 < NextShotTime) return;
	auto* Pool = GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();
	if (!Pool) return;
	FGuLiPooledProjectileLaunch Launch;
	Launch.Context.MatchEpoch = SourceEpoch; Launch.Context.Source = SourceHandle;
	Launch.Context.ShotId = FGuid::NewGuid(); Launch.Context.RootEventId = Launch.Context.ShotId;
	Launch.Context.Damage = ActiveUpgrade.Damage;
	Launch.Context.SkillId = FName(*FString::FromInt(ActiveUpgrade.SkillId));
	Launch.Position = Gun()->GetSocketLocation(FName(ActiveSkill.MuzzleSocket));
	Launch.Direction = (FVector(AimPoint) - Launch.Position).GetSafeNormal();
	Launch.MuzzleOffset = GetOwner()->GetActorTransform().InverseTransformPosition(Launch.Position);
	Launch.Speed = ActiveSkill.ProjectileSpeed; Launch.Lifetime = ActiveSkill.ProjectileLifetime;
	Launch.MaximumDistance = Launch.Speed * Launch.Lifetime; Launch.SweepRadius = ActiveSkill.SweepRadius;
	Launch.ServerTime = Now; Launch.PlayerBulletVfxId = ActiveSkill.BulletVfxId;
	if (!Pool->Launch(Launch).IsValid()) return;
	++ShotsFired;
	const double Interval = 1. / ActiveUpgrade.FireRate;
	NextShotTime = NextShotTime > 0 && Now - NextShotTime <= 1./30. + 1.e-6 ? NextShotTime + Interval : Now + Interval;
	MulticastShot(Launch.Context.ShotId, Now);
}

void UGuLiGroundMechWeaponComponent::MulticastShot_Implementation(FGuid ShotId, float ShotTime)
{
	if (!bConfigured || SeenShots.Contains(ShotId)) return;
	if (SeenShots.Num() >= 256) SeenShots.Reset();
	SeenShots.Add(ShotId);
	const auto* GameState = GetWorld()->GetGameState();
	const float Age = FMath::Max(0.f, (GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds()) - ShotTime);
	if (Age >= ActiveSkill.RecoilDuration) return;
	if (ConfiguredAnim.IsValid()) ConfiguredAnim->TriggerRecoil(Age);
	if (GetNetMode() == NM_DedicatedServer) return;
	++CosmeticShots;
	if (auto* Effect = UNiagaraFunctionLibrary::SpawnSystemAttached(MuzzleSystem, Gun(), FName(ActiveSkill.MuzzleSocket),
		FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false, false, ENCPoolMethod::ManualRelease, false))
	{
		Effect->SetUsingAbsoluteScale(true); Effect->SetWorldScale3D(GuLiVfx::Scale(this, ActiveSkill.MuzzleVfxId));
		// The weapon mount mirrors X. Removing inherited scale also removes that
		// reflection, so orient the unscaled effect along the actual socket axis.
		Effect->SetUsingAbsoluteRotation(true);
		Effect->SetWorldRotation(Gun()->GetSocketTransform(FName(ActiveSkill.MuzzleSocket)).TransformVector(FVector::ForwardVector).Rotation());
		Effect->SetVariableFloat(TEXT("User.GlobleDuration"), .08f);
		Effect->SetCastShadow(false); Effect->Activate(true); MuzzleEffects.Add(Effect);
	}
}

void UGuLiGroundMechWeaponComponent::TickComponent(float DeltaSeconds, ELevelTick Type, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(DeltaSeconds, Type, Tick);
	if (!bConfigured) { ResetAimAssistSources(); return; }
	RegisterSource();
	const auto* Pawn = CastChecked<APawn>(GetOwner());
	if (Pawn->IsLocallyControlled())
	{
		if (!CanControl(true)) { if (bFireHeld) SetFireHeld(false); bHasAim = false; ResetAimAssistSources(); }
		else
		{
			FVector Point;
			bHasAim = ResolveLocalWeaponAim(Point) && AcceptAim(Point);
			if (!bHasAim && bFireHeld) SetFireHeld(false);
			if (bHasAim && GetWorld()->GetTimeSeconds() >= NextAimSendTime)
			{ ServerUpdateAim(AimPoint); NextAimSendTime = GetWorld()->GetTimeSeconds() + .05; }
		}
	}
	else ResetAimAssistSources();
	if (GetOwner()->HasAuthority() && !CanControl(false)) bFireHeld = false;
	AlignGun(); TryFire();
	for (int32 Index = MuzzleEffects.Num()-1; Index >= 0; --Index)
	{
		UNiagaraComponent* Effect = MuzzleEffects[Index];
		if (IsValid(Effect) && !Effect->IsComplete())
			Effect->SetWorldRotation(Gun()->GetSocketTransform(FName(ActiveSkill.MuzzleSocket)).TransformVector(FVector::ForwardVector).Rotation());
		if (!IsValid(Effect) || Effect->IsComplete())
		{ if (IsValid(Effect)) Effect->ReleaseToPool(); MuzzleEffects.RemoveAtSwap(Index); }
	}
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

void UGuLiGroundMechWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, CurrentUpgradeId); DOREPLIFETIME(ThisClass, SourceHandle); DOREPLIFETIME(ThisClass, ShotsFired);
	DOREPLIFETIME_CONDITION(ThisClass, AimPoint, COND_SkipOwner);
}
