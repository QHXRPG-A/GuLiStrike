#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Gameplay/GroundMech/GuLiGroundMassCollisionTypes.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float ObstacleUpdateIntervalSeconds = 0.1f;
	constexpr float SupportRetentionToleranceCentimeters = 5.0f;
	constexpr int32 MassSupportSyntheticHitItem = MIN_int32 + 137;
}

UGuLiGroundMechMovementComponent::UGuLiGroundMechMovementComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UGuLiGroundMechMovementComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	ActiveMovementDeltaSeconds = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateGroundMechObstacle(DeltaTime);
	ActiveMovementDeltaSeconds = 0.0f;
}

void UGuLiGroundMechMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterGroundMechObstacle();
	Super::EndPlay(EndPlayReason);
}

void UGuLiGroundMechMovementComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(
		UGuLiGroundMechMovementComponent,
		MassSupportSoldierId,
		COND_OwnerOnly);
}

bool UGuLiGroundMechMovementComponent::IsMassSupportMode() const
{
	return MovementMode == MOVE_Custom
		&& CustomMovementMode == GuLiGroundMechMovement::MassSupportCustomMode;
}

UGuLiGroundMassContactSubsystem*
UGuLiGroundMechMovementComponent::GetMassContactSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UGuLiGroundMassContactSubsystem>() : nullptr;
}

bool UGuLiGroundMechMovementComponent::MoveUpdatedComponentImpl(
	const FVector& Delta,
	const FQuat& NewRotation,
	const bool bSweep,
	FHitResult* OutHit,
	const ETeleportType Teleport)
{
	if (Teleport != ETeleportType::None && MassSupportSoldierId.IsValid())
	{
		ClearMassSupport(true);
	}
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		|| !bSweep || Teleport != ETeleportType::None || Delta.ContainsNaN()
		|| !UpdatedComponent)
	{
		return Super::MoveUpdatedComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);
	}
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	UGuLiGroundMassContactSubsystem* Contacts = GetMassContactSubsystem();
	if (!Capsule || !Contacts)
	{
		return Super::MoveUpdatedComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);
	}

	float CapsuleRadius = 0.0f;
	float CapsuleHalfHeight = 0.0f;
	Capsule->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	const float MovementDeltaSeconds = ActiveMovementDeltaSeconds > 0.0f
		? ActiveMovementDeltaSeconds
		: FMath::Clamp(GetWorld()->GetDeltaSeconds(), 0.0f, 0.1f);
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start + Delta;
	const FVector2D Minimum(
		FMath::Min(Start.X, End.X) - CapsuleRadius,
		FMath::Min(Start.Y, End.Y) - CapsuleRadius);
	const FVector2D Maximum(
		FMath::Max(Start.X, End.X) + CapsuleRadius,
		FMath::Max(Start.Y, End.Y) + CapsuleRadius);
	Contacts->QueryMassBodies(
		FBox2D(Minimum, Maximum),
		MovementDeltaSeconds,
		CandidateBodies);

	FVector RequestedDelta = Delta;
	FGuLiGroundMassLandingResult Landing;
	if (MovementMode == MOVE_Falling && Delta.Z < 0.0f)
	{
		Landing = GuLiGroundMassCollision::FindLandingSupport(
			Start,
			Delta,
			CapsuleHalfHeight,
			MovementDeltaSeconds,
			CandidateBodies);
		if (Landing.IsValid()) RequestedDelta *= Landing.Time;
	}

	const FGuLiSoldierId IgnoredBody = Landing.IsValid()
		? Landing.SoldierId
		: MassSupportSoldierId;
	const FGuLiGroundMassMoveResult MassMove =
		GuLiGroundMassCollision::ResolvePlanarMove(
			Start,
			RequestedDelta,
			CapsuleRadius,
			CapsuleHalfHeight,
			MovementDeltaSeconds,
			CandidateBodies,
			IgnoredBody);
	FHitResult LocalHit(1.0f);
	FHitResult* EffectiveHit = OutHit ? OutHit : &LocalHit;
	const bool bMoved = Super::MoveUpdatedComponentImpl(
		MassMove.Delta,
		NewRotation,
		bSweep,
		EffectiveHit,
		Teleport);
	Contacts->RecordSideHits(MassMove.SideHitCount);

	const bool bWorldBlocked = EffectiveHit->bBlockingHit && EffectiveHit->Time < 1.0f;
	if (Landing.IsValid() && !bWorldBlocked)
	{
		SetMassSupport(Landing);
		Contacts->RecordSupportContact();
		// PhysFalling only notices a movement-mode change after a blocking result.
		// Mark this data-only landing as an internal sentinel so the base loop stops
		// before applying another falling substep, without exposing a fake component.
		*EffectiveHit = FHitResult(1.0f);
		EffectiveHit->bBlockingHit = true;
		EffectiveHit->Time = 1.0f;
		EffectiveHit->Location = UpdatedComponent->GetComponentLocation();
		EffectiveHit->ImpactPoint = EffectiveHit->Location
			- FVector(0.0f, 0.0f, CapsuleHalfHeight);
		EffectiveHit->Normal = FVector::ZeroVector;
		EffectiveHit->ImpactNormal = FVector::ZeroVector;
		EffectiveHit->Item = MassSupportSyntheticHitItem;
	}
	return bMoved;
}

void UGuLiGroundMechMovementComponent::PhysCustom(
	const float DeltaTime,
	const int32 Iterations)
{
	if (!IsMassSupportMode())
	{
		Super::PhysCustom(DeltaTime, Iterations);
		return;
	}
	if (DeltaTime < MIN_TICK_TIME || !CharacterOwner || !UpdatedComponent
		|| CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) return;

	UGuLiGroundMassContactSubsystem* Contacts = GetMassContactSubsystem();
	FGuLiGroundMassBody Support;
	if (!Contacts || !Contacts->FindMassBody(MassSupportSoldierId, Support))
	{
		ClearMassSupport(true);
		return;
	}
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		ClearMassSupport(true);
		return;
	}
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Start = UpdatedComponent->GetComponentLocation();
	if (FVector::DistSquared2D(Start, Support.Location)
		> FMath::Square(Support.RadiusCentimeters
			+ GuLiGroundMassCollision::ContactToleranceCentimeters))
	{
		ClearMassSupport(true);
		return;
	}

	Acceleration.Z = 0.0f;
	CalcVelocity(DeltaTime, GroundFriction, false, GetMaxBrakingDeceleration());
	Velocity.Z = 0.0f;
	const FVector SupportTranslation = Support.Location - LastSupportBodyLocation;
	FVector MoveDelta = Velocity * DeltaTime;
	MoveDelta.X += SupportTranslation.X;
	MoveDelta.Y += SupportTranslation.Y;
	MoveDelta.Z = Support.TopZ + CapsuleHalfHeight - Start.Z;

	ActiveMovementDeltaSeconds = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	FHitResult Hit(1.0f);
	SafeMoveUpdatedComponent(MoveDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		HandleImpact(Hit, DeltaTime, MoveDelta);
		SlideAlongSurface(MoveDelta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
	}

	const FVector FinalLocation = UpdatedComponent->GetComponentLocation();
	const bool bStillAboveSupport = FVector::DistSquared2D(FinalLocation, Support.Location)
		<= FMath::Square(Support.RadiusCentimeters
			+ GuLiGroundMassCollision::ContactToleranceCentimeters);
	const bool bHeightValid = FMath::Abs(
		FinalLocation.Z - CapsuleHalfHeight - Support.TopZ)
		<= SupportRetentionToleranceCentimeters;
	if (!bStillAboveSupport || !bHeightValid)
	{
		ClearMassSupport(true);
		return;
	}
	LastSupportBodyLocation = Support.Location;
}

void UGuLiGroundMechMovementComponent::SetMassSupport(
	const FGuLiGroundMassLandingResult& Landing)
{
	MassSupportSoldierId = Landing.SoldierId;
	LastSupportBodyLocation = Landing.BodyLocation;
	SetMovementMode(MOVE_Custom, GuLiGroundMechMovement::MassSupportCustomMode);
}

void UGuLiGroundMechMovementComponent::ResetMassSupportState()
{
	MassSupportSoldierId.Reset();
	LastSupportBodyLocation = FVector::ZeroVector;
}

void UGuLiGroundMechMovementComponent::ClearMassSupport(const bool bEnterFalling)
{
	const bool bWasMassSupport = IsMassSupportMode();
	ResetMassSupportState();
	if (!bEnterFalling || !bWasMassSupport) return;
	FFindFloorResult Floor;
	if (UpdatedComponent)
	{
		FindFloor(UpdatedComponent->GetComponentLocation(), Floor, false);
	}
	SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
}

void UGuLiGroundMechMovementComponent::OnMovementModeChanged(
	const EMovementMode PreviousMovementMode,
	const uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	const bool bWasMassSupport = PreviousMovementMode == MOVE_Custom
		&& PreviousCustomMode == GuLiGroundMechMovement::MassSupportCustomMode;
	if ((bWasMassSupport && !IsMassSupportMode()) || MovementMode == MOVE_Flying)
		ResetMassSupportState();
}

void UGuLiGroundMechMovementComponent::HandleImpact(
	const FHitResult& Hit,
	const float TimeSlice,
	const FVector& MoveDelta)
{
	if (Hit.bBlockingHit
		&& Hit.Item == MassSupportSyntheticHitItem
		&& !Hit.GetComponent()
		&& IsMassSupportMode())
	{
		return;
	}
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
}

void UGuLiGroundMechMovementComponent::ApplyExternalDisplacement(
	const FTransform& Transform)
{
	ClearMassSupport(true);
	ObstacleUpdateAccumulator = ObstacleUpdateIntervalSeconds;
	Super::ApplyExternalDisplacement(Transform);
}

void UGuLiGroundMechMovementComponent::OnTeleported()
{
	ClearMassSupport(true);
	ObstacleUpdateAccumulator = ObstacleUpdateIntervalSeconds;
	Super::OnTeleported();
}

void UGuLiGroundMechMovementComponent::OnRep_MassSupportSoldierId()
{
	if (!MassSupportSoldierId.IsValid() && IsMassSupportMode())
	{
		ClearMassSupport(true);
		return;
	}
	if (MassSupportSoldierId.IsValid())
	{
		FGuLiGroundMassBody Support;
		if (UGuLiGroundMassContactSubsystem* Contacts = GetMassContactSubsystem();
			Contacts && Contacts->FindMassBody(MassSupportSoldierId, Support))
		{
			LastSupportBodyLocation = Support.Location;
		}
	}
}

void UGuLiGroundMechMovementComponent::UpdateGroundMechObstacle(const float DeltaTime)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || !GetWorld()) return;
	const AGuLiBattlePlayerState* State = CharacterOwner->GetPlayerState<AGuLiBattlePlayerState>();
	const EGuLiTeam Team = State ? State->GetTeam() : EGuLiTeam::Unassigned;
	const bool bShouldRegister = IsMovingOnGround()
		&& (Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue);
	if (!bShouldRegister)
	{
		UnregisterGroundMechObstacle();
		return;
	}
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	UGuLiDynamicObstacleRegistrySubsystem* Registry =
		GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!Capsule || !Registry) return;
	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
	FGuLiDynamicObstacle Obstacle;
	Obstacle.Location = CharacterOwner->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
	Obstacle.RadiusCentimeters = Radius;
	Obstacle.Kind = EGuLiDynamicObstacleKind::GroundMech;
	Obstacle.Team = Team;
	if (!GroundMechObstacleHandle.IsValid())
	{
		GroundMechObstacleHandle = Registry->RegisterObstacle(Obstacle);
		RegisteredObstacleTeam = Team;
		ObstacleUpdateAccumulator = 0.0f;
		return;
	}
	ObstacleUpdateAccumulator += FMath::Max(0.0f, DeltaTime);
	if (RegisteredObstacleTeam != Team
		|| ObstacleUpdateAccumulator >= ObstacleUpdateIntervalSeconds)
	{
		if (!Registry->UpdateObstacle(GroundMechObstacleHandle, Obstacle))
			GroundMechObstacleHandle = Registry->RegisterObstacle(Obstacle);
		RegisteredObstacleTeam = Team;
		ObstacleUpdateAccumulator = 0.0f;
	}
}

void UGuLiGroundMechMovementComponent::UnregisterGroundMechObstacle()
{
	if (!GroundMechObstacleHandle.IsValid()) return;
	if (UWorld* World = GetWorld())
	{
		if (UGuLiDynamicObstacleRegistrySubsystem* Registry =
			World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
			Registry->UnregisterObstacle(GroundMechObstacleHandle);
	}
	GroundMechObstacleHandle.Reset();
	RegisteredObstacleTeam = EGuLiTeam::Unassigned;
	ObstacleUpdateAccumulator = 0.0f;
}
