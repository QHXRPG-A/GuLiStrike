// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSwarmFlow.h"
#include "GuLiFlightNavigationSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanFlightMovementComponent)

namespace
{
	constexpr float EmergencyEntrySeconds = 0.5f;
	constexpr float LocalDeadlockRecoverySeconds = 0.5f;
	constexpr float RecoveryClearSeconds = 0.5f;
	constexpr float FlightModeEvaluationPeriodSeconds = 0.1f;
	constexpr float MinimumProgressPerStepCentimeters = 3.0f;
	constexpr float EmergencyHoverTurnRateDegreesPerSecond = 90.0f;

	FVector BuildForwardFromTransform(const FTransform& Transform)
	{
		const FVector Forward = Transform.GetUnitAxis(EAxis::X).GetSafeNormal();
		return Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
	}

	EGuLiWingmanFlightMode EvaluateNominalFlightMode(
		const FVector& Position,
		const FGuLiWingmanRuntimeState& State)
	{
		const double CarrierDistance = FVector::Distance(
			Position, State.Carrier.Transform.GetLocation());
		if (CarrierDistance > State.Tuning.Formation.RecoveryDistanceCentimeters)
		{
			return EGuLiWingmanFlightMode::Recover;
		}
		if (CarrierDistance > State.Tuning.Formation.CatchUpDistanceCentimeters)
		{
			return EGuLiWingmanFlightMode::CatchUp;
		}
		return State.Carrier.Velocity.SizeSquared() > FMath::Square(100.0)
			? EGuLiWingmanFlightMode::Follow
			: EGuLiWingmanFlightMode::Orbit;
	}
}

UGuLiWingmanFlightMovementComponent::UGuLiWingmanFlightMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void UGuLiWingmanFlightMovementComponent::InitializeForOwner(AGuLiWingmanPawn& InPawn)
{
	WingmanPawn = &InPawn;
	SetUpdatedComponent(InPawn.GetRootComponent());
	SetCarrierActor(CarrierActor.Get());
	FGuLiWingmanRuntimeState& State = InPawn.GetMutableRuntimeState();
	State.Dynamics.FixedStepAccumulator = 0.0f;
	State.Dynamics.CaptureSimulationTick = FMath::Max(1u, State.Dynamics.CaptureSimulationTick);
	State.SwarmAgent.FlowSimulationTick = State.Dynamics.CaptureSimulationTick;
	State.Avoidance.LastVerifiedSafePoint = InPawn.GetActorLocation();
	State.Avoidance.bHasVerifiedSafePoint = true;
	State.Avoidance.bRecoveryPointCurrentlyValid = true;
}

void UGuLiWingmanFlightMovementComponent::SetCarrierActor(AActor* InCarrierActor)
{
	UPrimitiveComponent* MovementPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);
	if (MovementPrimitive)
	{
		if (AActor* PreviousCarrier = CarrierActor.Get())
		{
			MovementPrimitive->IgnoreActorWhenMoving(PreviousCarrier, false);
		}
		if (InCarrierActor)
		{
			MovementPrimitive->IgnoreActorWhenMoving(InCarrierActor, true);
		}
	}
	CarrierActor = InCarrierActor;
}

void UGuLiWingmanFlightMovementComponent::AdvanceFixedSteps(
	const float FrameDeltaSeconds,
	const bool bBypassNavigationForTests)
{
	AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	if (!Pawn || !Pawn->IsOwnerSimulationPawn() || !UpdatedComponent)
	{
		return;
	}
	FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
	State.Dynamics.FixedStepAccumulator = FMath::Min(
		State.Dynamics.FixedStepAccumulator + FMath::Clamp(FrameDeltaSeconds, 0.0f, 0.25f),
		GuLiWingmanSteering::FixedStepSeconds
			* static_cast<float>(GuLiWingmanSteering::MaximumFixedStepsPerFrame));

	int32 Steps = 0;
	while (State.Dynamics.FixedStepAccumulator + UE_KINDA_SMALL_NUMBER
		>= GuLiWingmanSteering::FixedStepSeconds
		&& Steps < GuLiWingmanSteering::MaximumFixedStepsPerFrame)
	{
		State.Dynamics.FixedStepAccumulator -= GuLiWingmanSteering::FixedStepSeconds;
		SimulateFixedStep(
			GuLiWingmanSteering::FixedStepSeconds,
			bBypassNavigationForTests);
		++Steps;
	}
}

FVector UGuLiWingmanFlightMovementComponent::BuildRequestedVelocity() const
{
	const AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	if (!Pawn)
	{
		return FVector::ZeroVector;
	}
	const FGuLiWingmanRuntimeState& State = Pawn->GetRuntimeState();
	const EGuLiWingmanMemberBehavior Behavior = Pawn->GetSelectedBehavior();
	if (!State.Dynamics.bAlive || Behavior == EGuLiWingmanMemberBehavior::Dead)
	{
		return FVector::ZeroVector;
	}

	FVector Preferred = FVector::ZeroVector;
	if ((Behavior == EGuLiWingmanMemberBehavior::GroundAttack
			|| Behavior == EGuLiWingmanMemberBehavior::AirAttack)
		&& State.Attack.bGuiding && !State.Attack.PreferredVelocity.IsNearlyZero())
	{
		Preferred = State.Attack.PreferredVelocity;
	}
	else if (Behavior == EGuLiWingmanMemberBehavior::Rejoin
		&& State.Navigation.bHasPath)
	{
		const FVector ToWaypoint = State.Navigation.Waypoint - Pawn->GetActorLocation();
		Preferred = ToWaypoint.GetSafeNormal()
			* State.Tuning.Formation.CatchUpSpeedCentimetersPerSecond;
	}
	else if (Behavior == EGuLiWingmanMemberBehavior::EmergencyAvoid)
	{
		const FVector SafePoint = State.Avoidance.bHasVerifiedSafePoint
			? State.Avoidance.LastVerifiedSafePoint
			: State.Carrier.Transform.GetLocation();
		const FVector Direction = GuLiWingmanSteering::BuildRecoveryOrbitDirection(
			Pawn->GetActorLocation(),
			BuildForwardFromTransform(Pawn->GetActorTransform()),
			SafePoint,
			FMath::Max(State.Tuning.Formation.AgentRadiusCentimeters * 4.0f, 6000.0f));
		Preferred = Direction * State.Tuning.Formation.MinimumSpeedCentimetersPerSecond;
	}
	else
	{
		Preferred = GuLiWingmanSwarmFlow::BuildPreferredVelocity(
			Pawn->GetActorLocation(),
			State.Dynamics.Velocity,
			State.Carrier.Transform.GetLocation(),
			State.Carrier.Velocity,
			State.SwarmAgent,
			State.Tuning.Formation,
			State.Dynamics.Mode);
	}

	const FVector FromCarrier = Pawn->GetActorLocation()
		- State.Carrier.Transform.GetLocation();
	const float CarrierDistance = static_cast<float>(FromCarrier.Size());
	if (CarrierDistance > State.Tuning.Formation.CatchUpDistanceCentimeters)
	{
		const FVector Home = (-FromCarrier).GetSafeNormal();
		const float HomeWeight = FMath::Clamp(
			(CarrierDistance - State.Tuning.Formation.CatchUpDistanceCentimeters)
			/ FMath::Max(1.0f, State.Tuning.Formation.RecoveryDistanceCentimeters
				- State.Tuning.Formation.CatchUpDistanceCentimeters),
			0.25f, 1.0f);
		Preferred = FMath::Lerp(Preferred, Home
			* State.Tuning.Formation.CatchUpSpeedCentimetersPerSecond, HomeWeight);
	}
	return Preferred.ContainsNaN() ? FVector::ZeroVector : Preferred;
}

GuLiWingmanSteering::FHeadingProbeResult
UGuLiWingmanFlightMovementComponent::ProbeHeading(
	const FVector& Start,
	const FVector& Direction,
	const float Distance,
	const float AgentRadius,
	const bool bBypassNavigationForTests) const
{
	GuLiWingmanSteering::FHeadingProbeResult Result;
	Result.ClearanceCentimeters = 0.0f;
	Result.bFlightNavSegmentValid = false;
	const FVector UnitDirection = Direction.GetSafeNormal();
	UWorld* World = GetWorld();
	if (!World || UnitDirection.IsNearlyZero() || Distance <= 0.0f)
	{
		return Result;
	}
	const FVector End = Start + UnitDirection * Distance;
	Result.bFlightNavSegmentValid = bBypassNavigationForTests;
	if (!bBypassNavigationForTests)
	{
		const UGuLiFlightNavigationSubsystem* Navigation =
			World->GetSubsystem<UGuLiFlightNavigationSubsystem>();
		Result.bFlightNavSegmentValid = Navigation
			&& Navigation->ValidateAuthoritativeSegment(Start, End, AgentRadius)
				== EGuLiFlightNavSegmentStatus::Valid;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiWingmanPawnProbe), false);
	if (const AActor* Owner = GetOwner())
	{
		QueryParams.AddIgnoredActor(Owner);
	}
	if (const AActor* Carrier = CarrierActor.Get())
	{
		QueryParams.AddIgnoredActor(Carrier);
	}
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, ObjectTypes,
		FCollisionShape::MakeSphere(FMath::Max(1.0f, AgentRadius)), QueryParams);
	Result.ClearanceCentimeters = Distance;
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		const AActor* HitActor = Hit.GetActor();
		if (!Component || (!Hit.bBlockingHit && !Hit.bStartPenetrating)
			|| (HitActor && (HitActor->IsA<AGuLiWingmanPawn>()
				|| HitActor->ActorHasTag(TEXT("WingmanAvoidanceIgnore")))))
		{
			continue;
		}
		const ECollisionChannel ObjectType = Component->GetCollisionObjectType();
		Result.bWorldStatic |= ObjectType == ECC_WorldStatic;
		Result.bWorldDynamic |= ObjectType == ECC_WorldDynamic;
		Result.ClearanceCentimeters = FMath::Min(Result.ClearanceCentimeters,
			Hit.bStartPenetrating ? 0.0f : FMath::Clamp(Hit.Distance, 0.0f, Distance));
	}
	return Result;
}

bool UGuLiWingmanFlightMovementComponent::TryMoveAlong(
	const FVector& Direction,
	const float Speed,
	const float StepSeconds,
	FHitResult& OutHit)
{
	AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	if (!Pawn || !UpdatedComponent || Direction.IsNearlyZero()
		|| Speed <= 0.0f || StepSeconds <= 0.0f)
	{
		return false;
	}
	FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
	const FVector CurrentForward = BuildForwardFromTransform(Pawn->GetActorTransform());
	const FVector NewForward = Direction.GetSafeNormal();
	const float SignedYaw = FMath::RadiansToDegrees(FMath::Atan2(
		FVector::CrossProduct(CurrentForward, NewForward).Z,
		FVector::DotProduct(CurrentForward, NewForward)));
	State.Dynamics.BankDegrees = FMath::FInterpTo(
		State.Dynamics.BankDegrees,
		FMath::Clamp(-SignedYaw * 2.0f,
			-State.Tuning.Formation.MaximumBankDegrees,
			State.Tuning.Formation.MaximumBankDegrees),
		StepSeconds, 4.0f);
	const FRotator Facing = NewForward.Rotation();
	const FQuat Rotation = FRotator(
		Facing.Pitch, Facing.Yaw, State.Dynamics.BankDegrees).Quaternion();
	SafeMoveUpdatedComponent(NewForward * Speed * StepSeconds, Rotation, true, OutHit);
	return !OutHit.bStartPenetrating && !OutHit.bBlockingHit;
}

void UGuLiWingmanFlightMovementComponent::SimulateFixedStep(
	const float StepSeconds,
	const bool bBypassNavigationForTests)
{
	AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	if (!Pawn || !UpdatedComponent)
	{
		return;
	}
	FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
	if (++State.Dynamics.CaptureSimulationTick == 0u)
	{
		State.Dynamics.CaptureSimulationTick = 1u;
	}
	State.SwarmAgent.FlowSimulationTick = State.Dynamics.CaptureSimulationTick;

	const FVector Start = Pawn->GetActorLocation();
	const FVector CurrentForward = BuildForwardFromTransform(Pawn->GetActorTransform());
	const FVector RequestedVelocity = BuildRequestedVelocity();
	const bool bHovering = RequestedVelocity.IsNearlyZero();
	if (bHovering)
	{
		State.Dynamics.Velocity = FVector::ZeroVector;
		if (!State.Dynamics.bAlive)
		{
			return;
		}
		const FVector LookDirection = State.Avoidance.bHasRecoveryEscapeDirection
			? State.Avoidance.RecoveryEscapeDirection
			: (State.Carrier.Transform.GetLocation() - Start).GetSafeNormal();
		if (!LookDirection.IsNearlyZero() && State.Avoidance.bControlledRecovery)
		{
			const FVector Turned = GuLiWingmanSteering::TurnDirectionToward(
				CurrentForward, LookDirection,
				FMath::DegreesToRadians(
					EmergencyHoverTurnRateDegreesPerSecond * StepSeconds));
			UpdatedComponent->SetWorldRotation(Turned.Rotation(), false, nullptr,
				ETeleportType::TeleportPhysics);
		}
		State.Dynamics.Mode = EGuLiWingmanFlightMode::Recover;
		UpdateProgressState(StepSeconds, 0.0f, false, false);
		if (State.Avoidance.NoProgressSeconds + UE_KINDA_SMALL_NUMBER
			>= LocalDeadlockRecoverySeconds)
		{
			PerformLocalDeadlockRecovery(bBypassNavigationForTests);
		}
		return;
	}

	const FGuLiWingmanFormationRuntimeConfig& Tuning = State.Tuning.Formation;
	const float CurrentSpeed = static_cast<float>(State.Dynamics.Velocity.Size());
	float DesiredSpeed = static_cast<float>(RequestedVelocity.Size());
	const bool bEmergency = State.Avoidance.bControlledRecovery;
	DesiredSpeed = FMath::Clamp(DesiredSpeed,
		bEmergency ? 0.0f : Tuning.MinimumSpeedCentimetersPerSecond,
		Tuning.CatchUpSpeedCentimetersPerSecond);
	// PX4/ArduPilot-style layering: behavior may change the requested course,
	// but every translating step remains inside the authored airframe turn limit.
	// The faster emergency turn is reserved for the zero-translation hover below.
	const float TurnRate = Tuning.MaximumTurnRateDegreesPerSecond;
	const FVector DesiredDirection = GuLiWingmanSteering::TurnDirectionToward(
		CurrentForward,
		RequestedVelocity.GetSafeNormal(),
		FMath::DegreesToRadians(TurnRate * StepSeconds));

	const GuLiWingmanSteering::FHeadingSelection Selection =
		GuLiWingmanSteering::SelectSafeHeading(
			CurrentForward, DesiredDirection, CurrentSpeed, Tuning,
			[this, Start, &Tuning, bBypassNavigationForTests](
				const FVector& Direction, const float Distance)
			{
				return ProbeHeading(Start, Direction, Distance,
					Tuning.AgentRadiusCentimeters, bBypassNavigationForTests);
			},
			StepSeconds);

	State.Avoidance.SafeDirection = Selection.Direction;
	State.Avoidance.NextStepSafeDirection = Selection.Direction;
	State.Avoidance.HeadingProbeCount = Selection.ProbeCount;
	State.Avoidance.bHasSafeDirection = Selection.bFullLookAheadSafe;
	State.Avoidance.bHasNextStepSafeDirection = Selection.bImmediateStepSafe;
	State.Avoidance.bDetectedWorldStatic = Selection.bEncounteredWorldStatic;
	State.Avoidance.bDetectedWorldDynamic = Selection.bEncounteredWorldDynamic;
	State.Avoidance.bDetectedFlightNavBoundary = Selection.bEncounteredFlightNavBoundary;

	// Treat obstacle clearance as a speed constraint, like a fixed-wing guidance
	// layer feeding a separate speed controller. When the full look-ahead closes,
	// begin bounded deceleration while there is still stopping distance instead of
	// holding cruise speed until the next sweep has no legal translation.
	if (!Selection.bFullLookAheadSafe)
	{
		const float ClearanceReserve = Tuning.AgentRadiusCentimeters * 1.25f;
		const float UsableStoppingDistance = FMath::Max(
			0.0f, Selection.ClearanceCentimeters - ClearanceReserve);
		const float ClearanceLimitedSpeed = FMath::Sqrt(
			2.0f * FMath::Max(1.0f,
				Tuning.MaximumDecelerationCentimetersPerSecondSquared)
			* UsableStoppingDistance);
		DesiredSpeed = FMath::Min(DesiredSpeed, ClearanceLimitedSpeed);
	}
	const float Acceleration = DesiredSpeed >= CurrentSpeed
		? Tuning.MaximumAccelerationCentimetersPerSecondSquared
		: Tuning.MaximumDecelerationCentimetersPerSecondSquared;
	const float NewSpeed = FMath::FInterpConstantTo(
		CurrentSpeed, DesiredSpeed, StepSeconds, FMath::Max(1.0f, Acceleration));

	const FVector FromCarrier = Start - State.Carrier.Transform.GetLocation();
	const bool bBeyondRecoveryBoundary =
		FromCarrier.Size() > Tuning.RecoveryDistanceCentimeters;
	const float MaximumTurnRadians = FMath::DegreesToRadians(
		Tuning.MaximumTurnRateDegreesPerSecond * StepSeconds);
	FVector MoveDirection = Selection.bImmediateStepSafe
		? Selection.Direction.GetSafeNormal() : FVector::ZeroVector;
	if (bBeyondRecoveryBoundary)
	{
		const FVector Home = GuLiWingmanSteering::TurnDirectionToward(
			CurrentForward, (-FromCarrier).GetSafeNormal(), MaximumTurnRadians);
		const auto HomeProbe = ProbeHeading(Start, Home,
			FMath::Max(Tuning.ObstacleLookAheadCentimeters, Tuning.AgentRadiusCentimeters * 2.0f),
			Tuning.AgentRadiusCentimeters, bBypassNavigationForTests);
		if (HomeProbe.bFlightNavSegmentValid
			&& HomeProbe.ClearanceCentimeters >= Tuning.AgentRadiusCentimeters * 1.5f)
		{
			MoveDirection = Home;
		}
	}

	const FQuat StartRotation = UpdatedComponent->GetComponentQuat();
	const auto RestoreStepStart = [this, &Start, &StartRotation]()
	{
		UpdatedComponent->SetWorldLocationAndRotation(
			Start, StartRotation, false, nullptr, ETeleportType::TeleportPhysics);
	};
	FHitResult Hit;
	bool bMoved = false;
	if (!MoveDirection.IsNearlyZero())
	{
		bMoved = TryMoveAlong(MoveDirection, NewSpeed, StepSeconds, Hit);
		if (!bMoved)
		{
			// SafeMove may advance to the impact point before reporting a block. Roll
			// the fixed step back so the published pose is either one complete,
			// kinematically coherent step or a constraint hold at the last safe pose.
			RestoreStepStart();
			if (Hit.bBlockingHit)
			{
				const FVector RawSlide = FVector::VectorPlaneProject(
					MoveDirection, Hit.Normal).GetSafeNormal();
				const FVector Slide = GuLiWingmanSteering::TurnDirectionToward(
					CurrentForward, RawSlide, MaximumTurnRadians);
				const auto SlideProbe = ProbeHeading(Start, Slide, NewSpeed * StepSeconds,
					Tuning.AgentRadiusCentimeters, bBypassNavigationForTests);
				if (!Slide.IsNearlyZero() && SlideProbe.bFlightNavSegmentValid
					&& SlideProbe.ClearanceCentimeters + 0.5f >= NewSpeed * StepSeconds)
				{
					Hit = FHitResult();
					bMoved = TryMoveAlong(Slide, NewSpeed, StepSeconds, Hit);
					if (bMoved)
					{
						MoveDirection = Slide;
					}
					else
					{
						RestoreStepStart();
					}
				}
			}
		}
	}

	if (!bMoved && State.Avoidance.bControlledRecovery)
	{
		TArray<FVector, TInlineAllocator<8>> RecoveryDirections;
		RecoveryDirections.Add(FVector::UpVector);
		RecoveryDirections.Add((FVector::UpVector + CurrentForward).GetSafeNormal());
		if (State.Avoidance.bHasVerifiedSafePoint)
		{
			RecoveryDirections.Add((State.Avoidance.LastVerifiedSafePoint - Start).GetSafeNormal());
		}
		RecoveryDirections.Add((State.Carrier.Transform.GetLocation() - Start).GetSafeNormal());
		for (const FVector& RecoveryDirection : RecoveryDirections)
		{
			const FVector FeasibleRecoveryDirection =
				GuLiWingmanSteering::TurnDirectionToward(
					CurrentForward, RecoveryDirection, MaximumTurnRadians);
			const auto Probe = ProbeHeading(Start, FeasibleRecoveryDirection,
				FMath::Max(Tuning.AgentRadiusCentimeters * 2.0f, NewSpeed * StepSeconds),
				Tuning.AgentRadiusCentimeters, bBypassNavigationForTests);
			if (FeasibleRecoveryDirection.IsNearlyZero() || !Probe.bFlightNavSegmentValid
				|| Probe.ClearanceCentimeters < NewSpeed * StepSeconds)
			{
				continue;
			}
			Hit = FHitResult();
			if (TryMoveAlong(FeasibleRecoveryDirection, NewSpeed, StepSeconds, Hit))
			{
				MoveDirection = FeasibleRecoveryDirection;
				bMoved = true;
				break;
			}
			RestoreStepStart();
		}
	}

	const FVector ActualDelta = Pawn->GetActorLocation() - Start;
	const float MovedDistance = static_cast<float>(ActualDelta.Size());
	if (bMoved && MovedDistance > UE_KINDA_SMALL_NUMBER)
	{
		// The accepted pose and velocity must describe the same fixed step. This
		// also lets a stopped aircraft accelerate out of recovery instead of being
		// reset to zero while its first sub-3 cm steps are still valid motion.
		State.Dynamics.Velocity = ActualDelta / StepSeconds;
		State.Avoidance.LastVerifiedSafePoint = Pawn->GetActorLocation();
		State.Avoidance.bHasVerifiedSafePoint = true;
		State.Avoidance.bRecoveryPointCurrentlyValid = true;
	}
	else
	{
		State.Dynamics.Velocity = FVector::ZeroVector;
		// A collision/navigation constraint is a recovery mode immediately on the
		// wire. StateTree still waits for the 0.5 s deadlock threshold before it
		// selects EmergencyAvoid, but the authority can distinguish this hold from
		// unconstrained normal flight in the first retained trail sample.
		if (State.Dynamics.bAlive)
		{
			State.Dynamics.Mode = EGuLiWingmanFlightMode::Recover;
		}
		State.Dynamics.BankDegrees = FMath::FInterpTo(
			State.Dynamics.BankDegrees, 0.0f, StepSeconds, 5.0f);
	}
	UpdateProgressState(StepSeconds, MovedDistance,
		Selection.bImmediateStepSafe, bBeyondRecoveryBoundary);
	if (!bMoved
		&& State.Avoidance.NoProgressSeconds + UE_KINDA_SMALL_NUMBER
			>= LocalDeadlockRecoverySeconds)
	{
		PerformLocalDeadlockRecovery(bBypassNavigationForTests);
	}
}

bool UGuLiWingmanFlightMovementComponent::PerformLocalDeadlockRecovery(
	const bool bBypassNavigationForTests)
{
	AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	UWorld* World = GetWorld();
	if (!Pawn || !World || !UpdatedComponent)
	{
		return false;
	}

	FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
	const FGuLiWingmanFormationRuntimeConfig& Formation = State.Tuning.Formation;
	const float AgentRadius = FMath::Max(1.0f, Formation.AgentRadiusCentimeters);
	const FVector CarrierPosition = State.Carrier.Transform.GetLocation();
	const int32 StableSlot = State.Identity.Handle.GetGroupMemberIndex();
	const float BaseAngle = State.FormationSlot.PhaseRadians;
	const FVector FormationLocal(
		FMath::Cos(BaseAngle) * State.FormationSlot.RadiusCentimeters,
		FMath::Sin(BaseAngle) * State.FormationSlot.RadiusCentimeters,
		State.FormationSlot.HeightCentimeters);

	TArray<FVector, TInlineAllocator<16>> Candidates;
	if (!State.Guidance.DesiredPosition.IsNearlyZero())
	{
		Candidates.Add(State.Guidance.DesiredPosition);
	}
	Candidates.Add(State.Carrier.Transform.TransformPositionNoScale(FormationLocal));
	const float RingRadius = FMath::Max(
		AgentRadius * 6.0f,
		0.5f * (Formation.SwarmOrbit.InnerSoftRadiusCentimeters
			+ Formation.SwarmOrbit.OuterSoftRadiusCentimeters));
	const float StableAngle = UE_TWO_PI
		* static_cast<float>(FMath::Max(0, StableSlot))
		/ static_cast<float>(GULI_WINGMAN_GROUP_SIZE);
	for (const float AngleOffsetDegrees : { 0.0f, 25.0f, -25.0f, 60.0f, -60.0f, 120.0f, -120.0f, 180.0f })
	{
		const float Angle = StableAngle + FMath::DegreesToRadians(AngleOffsetDegrees);
		Candidates.Add(CarrierPosition + State.Carrier.Transform.TransformVectorNoScale(FVector(
			FMath::Cos(Angle) * RingRadius,
			FMath::Sin(Angle) * RingRadius,
			State.FormationSlot.HeightCentimeters + AgentRadius * 2.0f)));
	}

	FVector ChosenPosition = FVector::ZeroVector;
	FVector ChosenDirection = FVector::ZeroVector;
	for (const FVector& Candidate : Candidates)
	{
		if (Candidate.ContainsNaN()
			|| FVector::Distance(Candidate, Pawn->GetActorLocation())
				< AgentRadius * 2.0f
			|| FVector::Distance(Candidate, CarrierPosition)
				> Formation.RecoveryDistanceCentimeters - AgentRadius)
		{
			continue;
		}
		FVector FlyOutDirection = (Candidate - CarrierPosition).GetSafeNormal();
		if (FlyOutDirection.IsNearlyZero())
		{
			FlyOutDirection = State.Carrier.Transform.GetUnitAxis(EAxis::X).GetSafeNormal();
		}
		const GuLiWingmanSteering::FHeadingProbeResult Probe = ProbeHeading(
			Candidate,
			FlyOutDirection,
			FMath::Max(AgentRadius * 2.0f, 2000.0f),
			AgentRadius,
			bBypassNavigationForTests);
		if (Probe.bFlightNavSegmentValid
			&& Probe.ClearanceCentimeters >= AgentRadius * 1.5f)
		{
			ChosenPosition = Candidate;
			ChosenDirection = FlyOutDirection;
			break;
		}
	}

	if (ChosenDirection.IsNearlyZero())
	{
		// Last-resort client failsafe: preserve a live aircraft even if local nav data
		// is temporarily unavailable or every sampled point is obstructed. Rotate the
		// deterministic fallback on each half-second attempt so an enclosed aircraft
		// can never remain parked at one failed point while waiting for a server reply.
		const uint32 RecoveryAttempt = State.Dynamics.CaptureSimulationTick / 15u;
		const float FallbackRadius = FMath::Min(
			FMath::Max(RingRadius, AgentRadius * 8.0f),
			FMath::Max(AgentRadius * 8.0f,
				Formation.RecoveryDistanceCentimeters * 0.5f));
		const float FallbackAngle = StableAngle
			+ static_cast<float>(RecoveryAttempt % 16u) * (UE_TWO_PI / 16.0f);
		const float HeightStep = static_cast<float>(
			static_cast<int32>(RecoveryAttempt % 5u) - 2) * AgentRadius * 2.0f;
		ChosenPosition = CarrierPosition
			+ State.Carrier.Transform.TransformVectorNoScale(FVector(
				FMath::Cos(FallbackAngle) * FallbackRadius,
				FMath::Sin(FallbackAngle) * FallbackRadius,
				State.FormationSlot.HeightCentimeters + HeightStep));
		ChosenDirection = (ChosenPosition - CarrierPosition).GetSafeNormal();
		if (ChosenDirection.IsNearlyZero())
		{
			ChosenDirection = State.Carrier.Transform.GetUnitAxis(EAxis::X).GetSafeNormal();
		}
	}

	Pawn->CancelFrozenAttackForRecovery();
	ApplyReposition(
		FTransform(ChosenDirection.Rotation(), ChosenPosition),
		ChosenDirection * Formation.MinimumSpeedCentimetersPerSecond);
	return true;
}

void UGuLiWingmanFlightMovementComponent::UpdateProgressState(
	const float StepSeconds,
	const float MovedDistance,
	const bool bHadSafeHeading,
	const bool bBlockedByCarrierBoundary)
{
	AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	if (!Pawn)
	{
		return;
	}
	FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
	const bool bProgress = MovedDistance >= MinimumProgressPerStepCentimeters;
	if (bProgress)
	{
		State.Avoidance.NoProgressSeconds = 0.0f;
		State.Avoidance.ConsecutiveBlockedSeconds = 0.0f;
		State.Avoidance.NormalProgressSeconds = FMath::Min(
			State.Avoidance.NormalProgressSeconds + StepSeconds,
			RecoveryClearSeconds + 0.25f);
		State.Avoidance.ConsecutiveClearSeconds = State.Avoidance.NormalProgressSeconds;
		if (State.Avoidance.bControlledRecovery
			&& State.Avoidance.NormalProgressSeconds + UE_KINDA_SMALL_NUMBER
				>= RecoveryClearSeconds)
		{
			ResetRecovery();
		}
		if (!State.Avoidance.bControlledRecovery)
		{
			State.Dynamics.ModeEvaluationAccumulator += StepSeconds;
			if (State.Dynamics.ModeEvaluationAccumulator + UE_KINDA_SMALL_NUMBER
				>= FlightModeEvaluationPeriodSeconds)
			{
				State.Dynamics.ModeEvaluationAccumulator = FMath::Fmod(
					State.Dynamics.ModeEvaluationAccumulator,
					FlightModeEvaluationPeriodSeconds);
				State.Dynamics.Mode = EvaluateNominalFlightMode(
					Pawn->GetActorLocation(), State);
			}
		}
		return;
	}

	State.Dynamics.ModeEvaluationAccumulator = 0.0f;
	State.Avoidance.NormalProgressSeconds = 0.0f;
	State.Avoidance.ConsecutiveClearSeconds = 0.0f;
	State.Avoidance.NoProgressSeconds = FMath::Min(
		State.Avoidance.NoProgressSeconds + StepSeconds,
		LocalDeadlockRecoverySeconds + 1.0f);
	State.Avoidance.ConsecutiveBlockedSeconds = State.Avoidance.NoProgressSeconds;
	if (State.Avoidance.NoProgressSeconds + UE_KINDA_SMALL_NUMBER >= EmergencyEntrySeconds)
	{
		if (!State.Avoidance.bControlledRecovery)
		{
			State.Avoidance.bControlledRecovery = true;
			State.Avoidance.RecoveryEscapeDirection =
				(State.Carrier.Transform.GetLocation() - Pawn->GetActorLocation()).GetSafeNormal();
			State.Avoidance.bHasRecoveryEscapeDirection =
				!State.Avoidance.RecoveryEscapeDirection.IsNearlyZero();
			Pawn->CancelFrozenAttackForRecovery();
		}
	}
}

void UGuLiWingmanFlightMovementComponent::ApplyAuthorityRebase(
	const FTransform& Transform,
	const FVector& InitialVelocity)
{
	ApplyReposition(Transform, InitialVelocity);
}

void UGuLiWingmanFlightMovementComponent::ApplyReposition(
	const FTransform& Transform,
	const FVector& InitialVelocity)
{
	AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	if (!Pawn || !UpdatedComponent)
	{
		return;
	}
	UpdatedComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
	State.Dynamics.Velocity = InitialVelocity;
	State.Dynamics.Mode = EGuLiWingmanFlightMode::CatchUp;
	State.Avoidance.LastVerifiedSafePoint = Transform.GetLocation();
	State.Avoidance.bHasVerifiedSafePoint = true;
	ResetRecovery();
}

void UGuLiWingmanFlightMovementComponent::ResetRecovery()
{
	if (AGuLiWingmanPawn* Pawn = WingmanPawn.Get())
	{
		FGuLiWingmanAvoidanceState& Avoidance = Pawn->GetMutableRuntimeState().Avoidance;
		const FVector SafePoint = Avoidance.LastVerifiedSafePoint;
		const bool bHasSafePoint = Avoidance.bHasVerifiedSafePoint;
		Avoidance = FGuLiWingmanAvoidanceState{};
		Avoidance.LastVerifiedSafePoint = SafePoint;
		Avoidance.bHasVerifiedSafePoint = bHasSafePoint;
		Avoidance.bRecoveryPointCurrentlyValid = bHasSafePoint;
	}
}

FVector UGuLiWingmanFlightMovementComponent::GetFlightVelocity() const
{
	const AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	return Pawn ? Pawn->GetRuntimeState().Dynamics.Velocity : FVector::ZeroVector;
}

uint32 UGuLiWingmanFlightMovementComponent::GetCaptureSimulationTick() const
{
	const AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	return Pawn ? Pawn->GetRuntimeState().Dynamics.CaptureSimulationTick : 0u;
}

bool UGuLiWingmanFlightMovementComponent::IsInEmergencyRecovery() const
{
	const AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	return Pawn && Pawn->GetRuntimeState().Avoidance.bControlledRecovery;
}

bool UGuLiWingmanFlightMovementComponent::IsAwaitingAuthorityRebase() const
{
	const AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	return Pawn && Pawn->GetRuntimeState().Avoidance.bAwaitingRebase;
}
