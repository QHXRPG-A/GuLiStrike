#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponAnimInstance.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
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
		|| !FMath::IsFinite(Skill->RecoilTargetLocalZCentimeters) || !FMath::IsFinite(Skill->RecoilDuration)
		|| Skill->RecoilDuration <= 0 || Gun()->GetBoneIndex(FName(Skill->RecoilBone)) == INDEX_NONE
		|| !Gun()->DoesSocketExist(FName(Skill->MuzzleSocket))) return false;
	auto* Curve = Cast<UCurveFloat>(Skill->RecoilCurve.LoadSynchronous());
	auto* Animation = Skill->WeaponAnimation.LoadSynchronous();
	auto* Bullet = Cast<UNiagaraSystem>(Skill->BulletSystem.LoadSynchronous());
	auto* Muzzle = Cast<UNiagaraSystem>(Skill->MuzzleSystem.LoadSynchronous());
	if (!Curve || !Animation || !Animation->IsChildOf(UGuLiGroundMechWeaponAnimInstance::StaticClass()) || !Bullet || !Muzzle) return false;
	ActiveSkill = *Skill; ActiveUpgrade = *Upgrade; BulletSystem = Bullet; MuzzleSystem = Muzzle;
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

void UGuLiGroundMechWeaponComponent::SetFireHeld(bool bHeld)
{
	if (!bHeld) bFireHeld = false;
	const auto* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled()) return;
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
	Launch.ServerTime = Now; Launch.PlayerBulletSystem = BulletSystem;
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
		Effect->SetUsingAbsoluteScale(true); Effect->SetWorldScale3D(FVector::OneVector);
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
	if (!bConfigured) return;
	RegisterSource();
	const auto* Pawn = CastChecked<APawn>(GetOwner());
	if (Pawn->IsLocallyControlled())
	{
		if (!CanControl(true)) { if (bFireHeld) SetFireHeld(false); bHasAim = false; }
		else
		{
			auto* PC = CastChecked<APlayerController>(Pawn->GetController());
			FVector Origin, Direction; FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechFireAim), true, GetOwner());
			bHasAim = PC->DeprojectMousePositionToWorld(Origin, Direction)
				&& GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin+Direction*600000., ECC_Visibility, Params)
				&& AcceptAim(Hit.ImpactPoint);
			if (!bHasAim && bFireHeld) SetFireHeld(false);
			if (bHasAim && GetWorld()->GetTimeSeconds() >= NextAimSendTime)
			{ ServerUpdateAim(AimPoint); NextAimSendTime = GetWorld()->GetTimeSeconds() + .05; }
		}
	}
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
