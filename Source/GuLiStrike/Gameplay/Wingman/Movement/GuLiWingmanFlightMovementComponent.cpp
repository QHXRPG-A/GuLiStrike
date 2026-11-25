// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSwarmFlow.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanPredictiveFlight.h"
#include "Network/GuLiHybridActorComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanFlightMovementComponent)

namespace
{
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
	Hulls.Reset();
	FGuLiWingmanRuntimeState& State = InPawn.GetMutableRuntimeState();
	State.Dynamics.FixedStepAccumulator = 0.0f;
	State.Dynamics.CaptureSimulationTick = FMath::Max(1u, State.Dynamics.CaptureSimulationTick);
	State.SwarmAgent.FlowSimulationTick = State.Dynamics.CaptureSimulationTick;
	State.Avoidance.LastVerifiedSafePoint = InPawn.GetActorLocation();
	State.Avoidance.bHasVerifiedSafePoint = true;
	PreviousSimulationTransform = InPawn.GetActorTransform();
	CurrentSimulationTransform = PreviousSimulationTransform;
	InPawn.ApplyOwnerPresentationTransform(CurrentSimulationTransform);
}

void UGuLiWingmanFlightMovementComponent::AdvanceFixedSteps(
	const float FrameDeltaSeconds)
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
		PreviousSimulationTransform = CurrentSimulationTransform;
		SimulateFixedStep(GuLiWingmanSteering::FixedStepSeconds);
		CurrentSimulationTransform = UpdatedComponent->GetComponentTransform();
		++Steps;
	}
	if (!Pawn->UsesHybridPresentation())
		UpdateOwnerPresentation(State.Dynamics.FixedStepAccumulator / GuLiWingmanSteering::FixedStepSeconds);
}

void UGuLiWingmanFlightMovementComponent::UpdateOwnerPresentation(const float Alpha)
{
	const FQuat Rotation = FQuat::Slerp(
		PreviousSimulationTransform.GetRotation(),
		CurrentSimulationTransform.GetRotation(), Alpha).GetNormalized();
	const FVector Location = FMath::Lerp(
		PreviousSimulationTransform.GetLocation(),
		CurrentSimulationTransform.GetLocation(), Alpha);
	const FVector Scale = FMath::Lerp(
		PreviousSimulationTransform.GetScale3D(),
		CurrentSimulationTransform.GetScale3D(), Alpha);
	WingmanPawn.Get()->ApplyOwnerPresentationTransform(
		FTransform(Rotation, Location, Scale));
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
	else if (State.Tuning.Formation.Model == EGuLiWingmanFormationModel::DoubleRingLegacy)
	{
		const FGuLiWingmanFormationSlotState& Slot = State.FormationSlot;
		const FVector Offset = Slot.Offset();
		const FVector Target = State.Carrier.Transform.GetLocation() + Offset;
		const FVector OrbitVelocity = FVector::CrossProduct(FVector::UpVector, Offset) * Slot.SignedAngularSpeed();
		constexpr double SlotResponseSeconds = 1.0;
		Preferred = State.Carrier.Velocity + OrbitVelocity + (Target - Pawn->GetActorLocation()) / SlotResponseSeconds;
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

bool UGuLiWingmanFlightMovementComponent::TryMoveAlong(
	const FVector& Direction,
	const float Speed,
	const float StepSeconds,
	FHitResult& OutHit)
{
	AGuLiWingmanPawn* Pawn = WingmanPawn.Get();
	if (!Pawn || !UpdatedComponent || Direction.IsNearlyZero()
		|| StepSeconds <= 0.0f)
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
    const FVector Start = UpdatedComponent->GetComponentLocation();
    using namespace GuLiWingmanFlightCollision;
    const float Radius = State.Tuning.Formation.AgentRadiusCentimeters;
    const FVector Delta = NewForward * Speed * StepSeconds;
    FHitResult FirstHull;
    FVector EndPlane = FVector::ZeroVector;
    for (const FHull& Hull : Hulls)
    {
        FHitResult Hit;
        FVector Plane;
        if (HullContact(Hull, Start, Start + Delta, Radius, -StepSeconds, 0, Hit, Plane) &&
            (!FirstHull.bBlockingHit || Hit.Time < FirstHull.Time))
        {
            FirstHull = Hit;
            EndPlane = Plane;
        }
    }
    auto MoveAgainstTerrain = [&](const FVector& Move) {
        FHitResult Hit;
        SafeMoveUpdatedComponent(Move, Rotation, true, Hit);
        if (Hit.bBlockingHit)
        {
            OutHit = Hit;
            SlideAlongSurface(Move, 1.f - Hit.Time, Hit.Normal, Hit, true);
            return true;
        }
        return false;
    };
    if (FirstHull.bBlockingHit)
    {
        OutHit = FirstHull;
        MoveAgainstTerrain(Delta * FirstHull.Time);
        const FVector Goal = Start + Delta;
        const double Push = FMath::Max(0., FVector::DotProduct(EndPlane - Goal, FirstHull.Normal) + 1.);
        MoveAgainstTerrain(Goal + FirstHull.Normal * Push - UpdatedComponent->GetComponentLocation());
    }
    else
        MoveAgainstTerrain(Delta);
    // A terrain slide or a second moving ship can change the first contact plane.
    for (const FHull& Hull : Hulls)
    {
        const FVector At = UpdatedComponent->GetComponentLocation();
        FHitResult Hit;
        FVector Plane;
        if (HullContact(Hull, At, At, Radius, 0, 0, Hit, Plane))
        {
            OutHit = Hit;
            if (MoveAgainstTerrain(Plane + Hit.Normal - At))
            {
                // A low hull can press the sphere against the ground. If its normal
                // correction is blocked, slide toward the hull edge along the terrain.
                const FVector TerrainNormal = OutHit.Normal;
                const FVector Contact = UpdatedComponent->GetComponentLocation();
                if (HullContact(Hull, Contact, Contact, Radius, 0, 0, Hit, Plane))
                {
                    const FVector AlongGround = FVector::VectorPlaneProject(Hit.Normal, TerrainNormal);
                    const FVector Radial =
                        FVector::VectorPlaneProject(Contact - Hull.Current.GetLocation(), TerrainNormal);
                    FVector Tangent, Bitangent;
                    TerrainNormal.FindBestAxisVectors(Tangent, Bitangent);
                    const FVector Escape =
                        AlongGround.GetSafeNormal(0.01, Radial.GetSafeNormal(UE_SMALL_NUMBER, Tangent));
                    const double Distance = FMath::Clamp(FVector::Distance(Contact, Plane), 1.0, double(Radius));
                    MoveAgainstTerrain(Escape * Distance);
                }
            }
        }
    }
    return !UpdatedComponent->GetComponentLocation().Equals(Start, UE_KINDA_SMALL_NUMBER);
}

void UGuLiWingmanFlightMovementComponent::SimulateFixedStep(const float StepSeconds)
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

    if (!State.Dynamics.bAlive)
    {
        State.Dynamics.Velocity = FVector::ZeroVector;
        return;
    }
    if (State.Tuning.Formation.Model == EGuLiWingmanFormationModel::DoubleRingLegacy)
    {
        auto& Slot = State.FormationSlot;
        Slot.PhaseRadians = FMath::UnwindRadians(Slot.PhaseRadians + Slot.SignedAngularSpeed() * StepSeconds);
    }
    UWorld& World = *GetWorld();
    GuLiWingmanFlightCollision::SampleHulls(World, StepSeconds, Hulls);
    const FVector Start = Pawn->GetActorLocation();
    const auto& Tuning = State.Tuning.Formation;
    const float SpeedMultiplier = UGuLiHybridActorComponent::MovementMultiplier(Pawn);
    const float MaxSpeed = Tuning.CatchUpSpeedCentimetersPerSecond * SpeedMultiplier;
    const FVector Requested = BuildRequestedVelocity() * SpeedMultiplier;
    GuLiWingmanPredictiveFlight::FPose Pose{Start, BuildForwardFromTransform(Pawn->GetActorTransform()),
                                            FMath::Min(float(State.Dynamics.Velocity.Size()), MaxSpeed)};
    GuLiWingmanPredictiveFlight::Choose(World, Hulls, State, Pose, Requested, MaxSpeed, StepSeconds);
    auto& Plan = State.FlightPlan;
    GuLiWingmanPredictiveFlight::Integrate(Pose, Plan.Direction, FMath::Min(Plan.Speed, MaxSpeed), Tuning,
                                           StepSeconds);
    float VerifiedFraction = FMath::Clamp(Plan.SafeSeconds / StepSeconds, 0.f, 1.f);
    // When starting inside the safety band the separating half-space is reachable
    // before a full inflated corridor is clear. The actual body still sweeps.
    if (Plan.Mode == EGuLiWingmanSteer::Escape && FVector::DotProduct(Pose.Forward, Plan.ContactNormal) > 0)
        VerifiedFraction = 1.f;
    FHitResult Hit;
    TryMoveAlong(Pose.Forward, Pose.Speed * VerifiedFraction, StepSeconds, Hit);
    Plan.SafeSeconds = FMath::Max(0.f, Plan.SafeSeconds - StepSeconds);
    // Ship displacement is external; it cannot create extra thrust in the next step.
    State.Dynamics.Velocity = Pose.Forward * (Pose.Speed * VerifiedFraction);
    if (Hit.bBlockingHit)
    {
        State.Dynamics.Velocity = FVector::VectorPlaneProject(State.Dynamics.Velocity, Hit.Normal);
        Plan.ContactNormal = Hit.Normal;
        Plan.SenseAfter = 0;
    }
    auto& Avoidance = State.Avoidance;
    Avoidance.bControlledRecovery = Plan.Mode == EGuLiWingmanSteer::Escape;
    Avoidance.ConsecutiveBlockedSeconds =
        Hit.bBlockingHit || VerifiedFraction < 1 ? Avoidance.ConsecutiveBlockedSeconds + StepSeconds : 0.f;
    Avoidance.LastVerifiedSafePoint = Pawn->GetActorLocation();
    Avoidance.bHasVerifiedSafePoint = true;
    State.Dynamics.Mode = EvaluateNominalFlightMode(Pawn->GetActorLocation(), State);
}

void UGuLiWingmanFlightMovementComponent::ApplyAuthorityRebase(const FTransform& Transform,
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
	PreviousSimulationTransform = UpdatedComponent->GetComponentTransform();
	CurrentSimulationTransform = PreviousSimulationTransform;
	if (Pawn->UsesHybridPresentation()) Pawn->ResetHybridPresentation();
	else Pawn->ApplyOwnerPresentationTransform(CurrentSimulationTransform);
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
		Hulls.Reset();
		Pawn->GetMutableRuntimeState().FlightPlan = {};
		FGuLiWingmanAvoidanceState& Avoidance = Pawn->GetMutableRuntimeState().Avoidance;
		const FVector SafePoint = Avoidance.LastVerifiedSafePoint;
		const bool bHasSafePoint = Avoidance.bHasVerifiedSafePoint;
		Avoidance = FGuLiWingmanAvoidanceState{};
		Avoidance.LastVerifiedSafePoint = SafePoint;
		Avoidance.bHasVerifiedSafePoint = bHasSafePoint;
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
