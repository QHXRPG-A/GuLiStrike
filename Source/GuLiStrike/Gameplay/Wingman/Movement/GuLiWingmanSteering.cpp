// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"

#include "Gameplay/Wingman/GuLiWingmanRuntimeTypes.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSwarmFlow.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "UObject/Package.h"


namespace GuLiWingmanSteering
{
	FVector TurnDirectionToward(
		const FVector& CurrentDirection,
		const FVector& DesiredDirection,
		const float MaximumRadians)
	{
		const FVector From = CurrentDirection.GetSafeNormal();
		const FVector To = DesiredDirection.GetSafeNormal();
		if (From.IsNearlyZero()) return To.IsNearlyZero() ? FVector::ForwardVector : To;
		if (To.IsNearlyZero() || MaximumRadians <= 0.0f) return From;
		const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(From, To), -1.0f, 1.0f));
		if (Angle <= MaximumRadians) return To;
		FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			const FVector Reference = FMath::Abs(From.Z) < 0.9f
				? FVector::UpVector : FVector::RightVector;
			Axis = FVector::CrossProduct(From, Reference).GetSafeNormal();
		}
		return FQuat(Axis, MaximumRadians).RotateVector(From).GetSafeNormal();
	}

}

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	GuLiWingmanSteering::FFlightNavSegmentValidatorForTests GFlightNavSegmentValidatorForTests;
	TWeakObjectPtr<UWorld> GFlightNavSegmentValidatorWorldForTests;
	GuLiWingmanSteering::FWorldObstacleSegmentProbeForTests GWorldObstacleSegmentProbeForTests;
	TWeakObjectPtr<UWorld> GWorldObstacleSegmentProbeWorldForTests;

	bool IsTransientAutomationTestWorld(const UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::Game
			|| World->GetNetMode() != NM_Standalone)
		{
			return false;
		}
		const UPackage* WorldPackage = World->GetOutermost();
		return World->HasAnyFlags(RF_Transient)
			// UWorld::CreateWorld gives automation Worlds their own /Temp package;
			// they are intentionally not outered to the global TransientPackage.
			|| (WorldPackage && WorldPackage->GetName().StartsWith(TEXT("/Temp/")));
	}
#endif

	bool ValidateClientFlightNavSegment(
		const UWorld* World,
		const FVector& Start,
		const FVector& End,
		const float AgentRadius)
	{
		if (!World || Start.ContainsNaN() || End.ContainsNaN()
			|| !FMath::IsFinite(AgentRadius) || AgentRadius < 0.0f)
		{
			return false;
		}
#if WITH_DEV_AUTOMATION_TESTS
		if (GFlightNavSegmentValidatorForTests
			&& GFlightNavSegmentValidatorWorldForTests.Get() == World
			&& IsTransientAutomationTestWorld(World))
		{
			return GFlightNavSegmentValidatorForTests(Start, End, AgentRadius);
		}
#endif
		const UGuLiFlightNavigationSubsystem* Navigation =
			World->GetSubsystem<UGuLiFlightNavigationSubsystem>();
		return Navigation
			&& Navigation->ValidateAuthoritativeSegment(Start, End, AgentRadius)
				== EGuLiFlightNavSegmentStatus::Valid;
	}

	float MeasureClientFlightNavClearance(
		const UWorld* World,
		const FVector& Start,
		const FVector& Direction,
		const float Distance,
		const float AgentRadius,
		bool& bOutCompleteSegmentValid)
	{
		bOutCompleteSegmentValid = false;
		if (!World || Start.ContainsNaN() || Direction.ContainsNaN()
			|| !FMath::IsFinite(Distance) || Distance <= UE_SMALL_NUMBER
			|| !FMath::IsFinite(AgentRadius) || AgentRadius < 0.0f)
		{
			return 0.0f;
		}

		const FVector UnitDirection = Direction.GetSafeNormal();
		if (UnitDirection.IsNearlyZero())
		{
			return 0.0f;
		}

		const FVector End = Start + UnitDirection * Distance;
		bOutCompleteSegmentValid = ValidateClientFlightNavSegment(
			World, Start, End, AgentRadius);
		if (bOutCompleteSegmentValid)
		{
			return Distance;
		}

		// FlightNav's authoritative segment predicate is prefix-monotonic for a
		// fixed ray: once that ray crosses a Volume/cell/portal boundary, extending
		// it cannot repair the already-invalid prefix. A short fixed-count bisection
		// therefore supplies the missing distance signal without route-searching.
		// Heading selection uses this signal to begin a finite-rate turn while there
		// is still room, instead of treating every out-of-nav ray as equally clear.
		if (!ValidateClientFlightNavSegment(World, Start, Start, AgentRadius))
		{
			return 0.0f;
		}

		float ValidDistance = 0.0f;
		float InvalidDistance = Distance;
		constexpr int32 ClearanceRefinementIterations = 7;
		for (int32 Iteration = 0; Iteration < ClearanceRefinementIterations; ++Iteration)
		{
			const float CandidateDistance = (ValidDistance + InvalidDistance) * 0.5f;
			const FVector CandidateEnd = Start + UnitDirection * CandidateDistance;
			if (ValidateClientFlightNavSegment(World, Start, CandidateEnd, AgentRadius))
			{
				ValidDistance = CandidateDistance;
			}
			else
			{
				InvalidDistance = CandidateDistance;
			}
		}
		return ValidDistance;
	}

	void AddLocallyControlledPawnsToIgnoredActors(
		const UWorld* World,
		FCollisionQueryParams& QueryParams)
	{
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator ControllerIt = World->GetPlayerControllerIterator();
			ControllerIt; ++ControllerIt)
		{
			if (const APlayerController* Controller = ControllerIt->Get();
				Controller && Controller->IsLocalController())
			{
				if (const APawn* Pawn = Controller->GetPawn())
				{
					QueryParams.AddIgnoredActor(Pawn);
				}
			}
		}
	}

	GuLiWingmanSteering::FHeadingProbeResult ProbeClientWorldObstacleSegment(
		UWorld* World,
		const FVector& Start,
		const FVector& End,
		const float AgentRadius,
		const FCollisionObjectQueryParams& ObjectTypes,
		const FCollisionQueryParams& QueryParams)
	{
		GuLiWingmanSteering::FHeadingProbeResult Result;
		if (!World || Start.ContainsNaN() || End.ContainsNaN()
			|| !FMath::IsFinite(AgentRadius) || AgentRadius < 0.0f)
		{
			return Result;
		}
		const float Distance = static_cast<float>(FVector::Distance(Start, End));
		Result.ClearanceCentimeters = Distance;
#if WITH_DEV_AUTOMATION_TESTS
		if (GWorldObstacleSegmentProbeForTests
			&& GWorldObstacleSegmentProbeWorldForTests.Get() == World
			&& IsTransientAutomationTestWorld(World))
		{
			Result = GWorldObstacleSegmentProbeForTests(Start, End, AgentRadius);
			Result.ClearanceCentimeters = FMath::Clamp(
				Result.ClearanceCentimeters, 0.0f, Distance);
			return Result;
		}
#endif
		if (!World->GetPhysicsScene() || Distance <= UE_SMALL_NUMBER)
		{
			return Result;
		}

		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(
			Hits,
			Start,
			End,
			FQuat::Identity,
			ObjectTypes,
			FCollisionShape::MakeSphere(FMath::Max(1.0f, AgentRadius)),
			QueryParams);
		for (const FHitResult& Hit : Hits)
		{
			const AActor* HitActor = Hit.GetActor();
			if (HitActor && (HitActor->IsA<AGuLiWingmanPawn>()
				|| HitActor->ActorHasTag(TEXT("WingmanAvoidanceIgnore"))))
			{
				continue;
			}
			const UPrimitiveComponent* Component = Hit.GetComponent();
			if (!Component || (!Hit.bBlockingHit && !Hit.bStartPenetrating))
			{
				continue;
			}
			const ECollisionChannel ObjectType = Component->GetCollisionObjectType();
			if (ObjectType == ECC_WorldStatic)
			{
				Result.bWorldStatic = true;
			}
			else if (ObjectType == ECC_WorldDynamic)
			{
				Result.bWorldDynamic = true;
			}
			else
			{
				continue;
			}
			const float HitDistance = Hit.bStartPenetrating
				? 0.0f : FMath::Clamp(Hit.Distance, 0.0f, Distance);
			Result.ClearanceCentimeters = FMath::Min(
				Result.ClearanceCentimeters, HitDistance);
		}
		return Result;
	}

	void AccumulateHeadingThreats(
		FGuLiWingmanAvoidanceState& State,
		const GuLiWingmanSteering::FHeadingSelection& Selection)
	{
		State.bDetectedWorldStatic |= Selection.bEncounteredWorldStatic;
		State.bDetectedWorldDynamic |= Selection.bEncounteredWorldDynamic;
		State.bDetectedFlightNavBoundary |= Selection.bEncounteredFlightNavBoundary;
	}

	void AddUniqueHeading(TArray<FVector, TInlineAllocator<16>>& Headings, const FVector& Candidate)
	{
		const FVector Normalized = Candidate.GetSafeNormal();
		if (Normalized.IsNearlyZero() || Normalized.ContainsNaN())
		{
			return;
		}
		for (const FVector& Existing : Headings)
		{
			if (FVector::DotProduct(Existing, Normalized) > 0.9999f)
			{
				return;
			}
		}
		Headings.Add(Normalized);
	}

	float CalculateKinematicLookAheadDistance(
		const float CurrentSpeedCentimetersPerSecond,
		const FGuLiWingmanFormationRuntimeConfig& Tuning)
	{
		const float Speed = FMath::Max(
			FMath::Abs(CurrentSpeedCentimetersPerSecond),
			FMath::Max(1.0f, Tuning.MinimumSpeedCentimetersPerSecond));
		const float MaximumDeceleration = FMath::Max(
			1.0f, Tuning.MaximumDecelerationCentimetersPerSecondSquared);
		const float BrakingDistance = Speed * Speed / (2.0f * MaximumDeceleration);
		const float ControlReserve = FMath::Max(
			Tuning.AgentRadiusCentimeters,
			Speed * 0.25f);
		return FMath::Max3(
			Tuning.ObstacleLookAheadCentimeters,
			Tuning.AgentRadiusCentimeters * 2.0f,
			BrakingDistance + ControlReserve);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void GuLiWingmanSteering::SetFlightNavSegmentValidatorForTests(
	UWorld* TransientTestWorld,
	FFlightNavSegmentValidatorForTests Validator)
{
	if (!IsTransientAutomationTestWorld(TransientTestWorld) || !Validator)
	{
		ensureMsgf(false,
			TEXT("FlightNav steering validator overrides require a transient standalone automation Game World"));
		GFlightNavSegmentValidatorForTests = nullptr;
		GFlightNavSegmentValidatorWorldForTests.Reset();
		return;
	}
	GFlightNavSegmentValidatorWorldForTests = TransientTestWorld;
	GFlightNavSegmentValidatorForTests = MoveTemp(Validator);
}

void GuLiWingmanSteering::ResetFlightNavSegmentValidatorForTests()
{
	GFlightNavSegmentValidatorForTests = nullptr;
	GFlightNavSegmentValidatorWorldForTests.Reset();
}

void GuLiWingmanSteering::SetWorldObstacleSegmentProbeForTests(
	UWorld* TransientTestWorld,
	FWorldObstacleSegmentProbeForTests Probe)
{
	if (!IsTransientAutomationTestWorld(TransientTestWorld) || !Probe)
	{
		ensureMsgf(false,
			TEXT("World obstacle steering probe overrides require a transient standalone automation Game World"));
		GWorldObstacleSegmentProbeForTests = nullptr;
		GWorldObstacleSegmentProbeWorldForTests.Reset();
		return;
	}
	GWorldObstacleSegmentProbeWorldForTests = TransientTestWorld;
	GWorldObstacleSegmentProbeForTests = MoveTemp(Probe);
}

void GuLiWingmanSteering::ResetWorldObstacleSegmentProbeForTests()
{
	GWorldObstacleSegmentProbeForTests = nullptr;
	GWorldObstacleSegmentProbeWorldForTests.Reset();
}

void GuLiWingmanSteering::AccumulateHeadingThreatsForTests(
	FGuLiWingmanAvoidanceState& State,
	const FHeadingSelection& Selection)
{
	AccumulateHeadingThreats(State, Selection);
}
#endif

GuLiWingmanSteering::FHeadingSelection GuLiWingmanSteering::SelectSafeHeading(
	const FVector& CurrentDirection,
	const FVector& DesiredDirection,
	const float CurrentSpeedCentimetersPerSecond,
	const FGuLiWingmanFormationRuntimeConfig& Tuning,
	TFunctionRef<FHeadingProbeResult(const FVector&, float)> Probe,
	const float CandidateTurnHorizonSeconds)
{
	FHeadingSelection Result;
	const FVector Current = CurrentDirection.IsNearlyZero()
		? FVector::ForwardVector : CurrentDirection.GetSafeNormal();
	const FVector Desired = DesiredDirection.IsNearlyZero()
		? Current : DesiredDirection.GetSafeNormal();
	const float Speed = FMath::Max(
		FMath::Abs(CurrentSpeedCentimetersPerSecond),
		FMath::Max(1.0f, Tuning.MinimumSpeedCentimetersPerSecond));
	const float LookAheadDistance = CalculateKinematicLookAheadDistance(
		CurrentSpeedCentimetersPerSecond, Tuning);
	const float PredictionSeconds = FMath::Clamp(LookAheadDistance / Speed, 0.5f, 4.0f);
	const float ImmediateSafeDistance = FMath::Min(
		LookAheadDistance,
		FMath::Max(Tuning.AgentRadiusCentimeters * 1.5f,
			Tuning.MinimumSpeedCentimetersPerSecond * 0.25f));
	// Runtime Integration supplies its fixed-step horizon so every sampled heading
	// is physically reachable by the very next Transform mutation. The default
	// retains the wider look-ahead fan for UObject-free callers and diagnostics.
	const bool bUseExplicitTurnHorizon = FMath::IsFinite(CandidateTurnHorizonSeconds)
		&& CandidateTurnHorizonSeconds > 0.0f;
	const float TurnHorizonSeconds = bUseExplicitTurnHorizon
		? CandidateTurnHorizonSeconds : PredictionSeconds;
	const float MinimumCandidateTurnDegrees = bUseExplicitTurnHorizon ? 0.0f : 10.0f;
	const float MaximumTurnRadians = FMath::DegreesToRadians(FMath::Clamp(
		Tuning.MaximumTurnRateDegreesPerSecond * TurnHorizonSeconds,
		MinimumCandidateTurnDegrees, 65.0f));
	const float MaximumPitchRadians = FMath::Min(MaximumTurnRadians, FMath::DegreesToRadians(35.0f));
	const FVector FeasibleDesired = GuLiWingmanSteering::TurnDirectionToward(
		Current, Desired, MaximumTurnRadians);

	FVector Up = FVector::UpVector;
	if (FMath::Abs(FVector::DotProduct(Current, Up)) > 0.98f)
	{
		Up = FVector::RightVector;
	}
	const FVector Right = FVector::CrossProduct(Up, Current).GetSafeNormal();
	TArray<FVector, TInlineAllocator<16>> Headings;
	AddUniqueHeading(Headings, FeasibleDesired);
	AddUniqueHeading(Headings, Current);
	for (const float Scale : {0.5f, 1.0f})
	{
		AddUniqueHeading(Headings, FQuat(Up, MaximumTurnRadians * Scale).RotateVector(Current));
		AddUniqueHeading(Headings, FQuat(Up, -MaximumTurnRadians * Scale).RotateVector(Current));
		AddUniqueHeading(Headings, FQuat(Right, MaximumPitchRadians * Scale).RotateVector(Current));
		AddUniqueHeading(Headings, FQuat(Right, -MaximumPitchRadians * Scale).RotateVector(Current));
	}
	for (const float YawSign : {-1.0f, 1.0f})
	{
		for (const float PitchSign : {-1.0f, 1.0f})
		{
			const FVector Yawed = FQuat(Up, MaximumTurnRadians * 0.5f * YawSign).RotateVector(Current);
			const FVector YawedRight = FVector::CrossProduct(Up, Yawed).GetSafeNormal();
			AddUniqueHeading(Headings,
				FQuat(YawedRight, MaximumPitchRadians * 0.5f * PitchSign).RotateVector(Yawed));
		}
	}

	float BestFullScore = -BIG_NUMBER;
	float BestImmediateScore = -BIG_NUMBER;
	FVector BestFullDirection = FVector::ZeroVector;
	FVector BestImmediateDirection = FVector::ZeroVector;
	float BestFullClearance = 0.0f;
	float BestImmediateClearance = 0.0f;
	for (int32 CandidateIndex = 0; CandidateIndex < Headings.Num(); ++CandidateIndex)
	{
		const FVector& Candidate = Headings[CandidateIndex];
		FHeadingProbeResult ProbeResult = Probe(Candidate, LookAheadDistance);
		++Result.ProbeCount;
		ProbeResult.ClearanceCentimeters = FMath::Clamp(
			ProbeResult.ClearanceCentimeters, 0.0f, LookAheadDistance);
		Result.bEncounteredFlightNavBoundary |= !ProbeResult.bFlightNavSegmentValid;
		Result.bEncounteredWorldStatic |= ProbeResult.bWorldStatic;
		Result.bEncounteredWorldDynamic |= ProbeResult.bWorldDynamic;
		const float Alignment = FVector::DotProduct(Candidate, FeasibleDesired);
		const float ClearanceScore = ProbeResult.ClearanceCentimeters / FMath::Max(1.0f, LookAheadDistance);
		const float StableTieBreak = static_cast<float>(CandidateIndex) * 0.000001f;
		const float Score = ClearanceScore * 2.0f + Alignment - StableTieBreak;
		const bool bFullSafe = ProbeResult.bFlightNavSegmentValid
			&& ProbeResult.ClearanceCentimeters + 0.5f >= LookAheadDistance;
		if (bFullSafe && Score > BestFullScore)
		{
			BestFullScore = Score;
			BestFullDirection = Candidate;
			BestFullClearance = ProbeResult.ClearanceCentimeters;
			// Candidate zero is the feasible desired direction and cannot be outscored.
			if (CandidateIndex == 0)
			{
				break;
			}
		}

		// A full look-ahead failure never implicitly authorizes the shorter
		// fallback. Re-sweep and revalidate FlightNav for the exact immediate
		// distance that Integration may consume.
		FHeadingProbeResult ImmediateProbe = Probe(Candidate, ImmediateSafeDistance);
		++Result.ProbeCount;
		ImmediateProbe.ClearanceCentimeters = FMath::Clamp(
			ImmediateProbe.ClearanceCentimeters, 0.0f, ImmediateSafeDistance);
		Result.bEncounteredFlightNavBoundary |= !ImmediateProbe.bFlightNavSegmentValid;
		Result.bEncounteredWorldStatic |= ImmediateProbe.bWorldStatic;
		Result.bEncounteredWorldDynamic |= ImmediateProbe.bWorldDynamic;
		const bool bImmediateSafe = ImmediateProbe.bFlightNavSegmentValid
			&& ImmediateProbe.ClearanceCentimeters + 0.5f >= ImmediateSafeDistance;
		if (bImmediateSafe && Score > BestImmediateScore)
		{
			BestImmediateScore = Score;
			BestImmediateDirection = Candidate;
			// Preserve the long-probe clearance for braking. ImmediateProbe only
			// establishes that the next short movement horizon is executable.
			BestImmediateClearance = ProbeResult.ClearanceCentimeters;
		}
	}

	if (!BestFullDirection.IsNearlyZero())
	{
		Result.Direction = BestFullDirection;
		Result.ClearanceCentimeters = BestFullClearance;
		Result.bFullLookAheadSafe = true;
		Result.bImmediateStepSafe = true;
	}
	else if (!BestImmediateDirection.IsNearlyZero())
	{
		Result.Direction = BestImmediateDirection;
		Result.ClearanceCentimeters = BestImmediateClearance;
		Result.bImmediateStepSafe = true;
	}
	return Result;
}

void GuLiWingmanSteering::AdvanceRecoveryClock(
	FRecoveryClockState& State,
	const float DeltaSeconds,
	const bool bFullLookAheadSafe,
	const bool bHasVerifiedReopenedPath)
{
	constexpr float EnterRecoverySeconds = 0.5f;
	constexpr float ExitRecoveryClearSeconds = 0.5f;
	const float SafeDeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
	if (!bFullLookAheadSafe)
	{
		State.ConsecutiveClearSeconds = 0.0f;
		State.ConsecutiveBlockedSeconds = FMath::Min(
			State.ConsecutiveBlockedSeconds + SafeDeltaSeconds, EnterRecoverySeconds + 1.0f);
		if (State.ConsecutiveBlockedSeconds + UE_KINDA_SMALL_NUMBER >= EnterRecoverySeconds)
		{
			State.bControlledRecovery = true;
		}
		return;
	}

	State.ConsecutiveBlockedSeconds = 0.0f;
	if (!State.bControlledRecovery)
	{
		State.ConsecutiveClearSeconds = 0.0f;
		return;
	}
	if (!bHasVerifiedReopenedPath)
	{
		State.ConsecutiveClearSeconds = 0.0f;
		return;
	}
	State.ConsecutiveClearSeconds = FMath::Min(
		State.ConsecutiveClearSeconds + SafeDeltaSeconds, ExitRecoveryClearSeconds + 0.25f);
	if (State.ConsecutiveClearSeconds + UE_KINDA_SMALL_NUMBER >= ExitRecoveryClearSeconds)
	{
		State = FRecoveryClockState();
	}
}

FVector GuLiWingmanSteering::BuildRecoveryOrbitDirection(
	const FVector& Position,
	const FVector& CurrentDirection,
	const FVector& SafePoint,
	const float OrbitRadiusCentimeters)
{
	const FVector Current = CurrentDirection.IsNearlyZero()
		? FVector::ForwardVector : CurrentDirection.GetSafeNormal();
	FVector Radial = Position - SafePoint;
	if (Radial.IsNearlyZero())
	{
		Radial = FVector::CrossProduct(Current, FVector::UpVector).GetSafeNormal();
		if (Radial.IsNearlyZero()) Radial = FVector::RightVector;
	}
	const float Distance = static_cast<float>(Radial.Size());
	Radial.Normalize();
	FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial).GetSafeNormal();
	if (Tangent.IsNearlyZero()) Tangent = FVector::CrossProduct(FVector::RightVector, Radial).GetSafeNormal();
	if (FVector::DotProduct(Tangent, Current) < 0.0f) Tangent *= -1.0f;
	const float DesiredRadius = FMath::Max(1.0f, OrbitRadiusCentimeters);
	const float RadiusError = FMath::Clamp(
		(Distance - DesiredRadius) / DesiredRadius, -1.0f, 1.0f);
	const FVector Inward = -Radial * RadiusError * 0.35f;
	const FVector Result = (Tangent + Inward).GetSafeNormal();
	return Result.IsNearlyZero() ? Current : Result;
}
