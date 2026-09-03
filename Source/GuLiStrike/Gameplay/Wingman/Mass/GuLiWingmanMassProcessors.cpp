// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Mass/GuLiWingmanMassProcessors.h"

#include "Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassProcessingTypes.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanMassProcessors)

namespace GuLiWingmanMass
{
	const FName ModeGroup(TEXT("GuLiWingmanMode"));
	const FName GuidanceGroup(TEXT("GuLiWingmanGuidance"));
	const FName AvoidanceGroup(TEXT("GuLiWingmanAvoidance"));
	const FName IntegrationGroup(TEXT("GuLiWingmanIntegration"));

	constexpr float FixedStepSeconds = 1.0f / 30.0f;
	constexpr float MaximumAccumulatorSeconds = FixedStepSeconds * 4.0f;
	constexpr float ModeEvaluationPeriodSeconds = 0.2f;

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

	uint8 ClientExecutionFlags()
	{
		return static_cast<uint8>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	}
}

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	GuLiWingmanAvoidance::FFlightNavSegmentValidatorForTests GFlightNavSegmentValidatorForTests;
	TWeakObjectPtr<UWorld> GFlightNavSegmentValidatorWorldForTests;
	GuLiWingmanAvoidance::FWorldObstacleSegmentProbeForTests GWorldObstacleSegmentProbeForTests;
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

	GuLiWingmanAvoidance::FHeadingProbeResult ProbeClientWorldObstacleSegment(
		UWorld* World,
		const FVector& Start,
		const FVector& End,
		const float AgentRadius,
		const FCollisionObjectQueryParams& ObjectTypes,
		const FCollisionQueryParams& QueryParams)
	{
		GuLiWingmanAvoidance::FHeadingProbeResult Result;
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
			if (HitActor && (HitActor->IsA<AGuLiWingmanPresentationActor>()
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
		FGuLiWingmanAvoidanceFragment& State,
		const GuLiWingmanAvoidance::FHeadingSelection& Selection)
	{
		State.bDetectedWorldStatic |= Selection.bEncounteredWorldStatic;
		State.bDetectedWorldDynamic |= Selection.bEncounteredWorldDynamic;
		State.bDetectedFlightNavBoundary |= Selection.bEncounteredFlightNavBoundary;
	}

	bool GuidLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}

	bool GroupLess(const FGuLiWingmanGroupHandle& A, const FGuLiWingmanGroupHandle& B)
	{
		if (A.ShipInstanceId != B.ShipInstanceId) return GuidLess(A.ShipInstanceId, B.ShipInstanceId);
		if (A.ShipGeneration != B.ShipGeneration) return A.ShipGeneration < B.ShipGeneration;
		return A.GroupGeneration < B.GroupGeneration;
	}

	bool WingmanLess(const FGuLiWingmanHandle& A, const FGuLiWingmanHandle& B)
	{
		if (A.Flight.Group != B.Flight.Group) return GroupLess(A.Flight.Group, B.Flight.Group);
		if (A.Flight.FlightIndex != B.Flight.FlightIndex) return A.Flight.FlightIndex < B.Flight.FlightIndex;
		if (A.MemberIndex != B.MemberIndex) return A.MemberIndex < B.MemberIndex;
		return A.EntityGeneration < B.EntityGeneration;
	}

	int32 QuantizeSpatialCoordinate(const double Coordinate, const float CellSize)
	{
		const double Quantized = FMath::FloorToDouble(Coordinate / static_cast<double>(CellSize));
		return static_cast<int32>(FMath::Clamp(
			Quantized, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32)));
	}

	FIntVector MakeSpatialCell(const FVector& Position, const float CellSize)
	{
		return FIntVector(
			QuantizeSpatialCoordinate(Position.X, CellSize),
			QuantizeSpatialCoordinate(Position.Y, CellSize),
			QuantizeSpatialCoordinate(Position.Z, CellSize));
	}

	FIntVector OffsetSpatialCell(
		const FIntVector& Cell,
		const int32 OffsetX,
		const int32 OffsetY,
		const int32 OffsetZ)
	{
		const auto SaturatedOffset = [](const int32 Value, const int32 Offset)
		{
			return static_cast<int32>(FMath::Clamp<int64>(
				static_cast<int64>(Value) + Offset,
				static_cast<int64>(MIN_int32),
				static_cast<int64>(MAX_int32)));
		};
		return FIntVector(
			SaturatedOffset(Cell.X, OffsetX),
			SaturatedOffset(Cell.Y, OffsetY),
			SaturatedOffset(Cell.Z, OffsetZ));
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
}

#if WITH_DEV_AUTOMATION_TESTS
void GuLiWingmanAvoidance::SetFlightNavSegmentValidatorForTests(
	UWorld* TransientTestWorld,
	FFlightNavSegmentValidatorForTests Validator)
{
	if (!IsTransientAutomationTestWorld(TransientTestWorld) || !Validator)
	{
		ensureMsgf(false,
			TEXT("FlightNav Mass validator overrides require a transient standalone automation Game World"));
		GFlightNavSegmentValidatorForTests = nullptr;
		GFlightNavSegmentValidatorWorldForTests.Reset();
		return;
	}
	GFlightNavSegmentValidatorWorldForTests = TransientTestWorld;
	GFlightNavSegmentValidatorForTests = MoveTemp(Validator);
}

void GuLiWingmanAvoidance::ResetFlightNavSegmentValidatorForTests()
{
	GFlightNavSegmentValidatorForTests = nullptr;
	GFlightNavSegmentValidatorWorldForTests.Reset();
}

void GuLiWingmanAvoidance::SetWorldObstacleSegmentProbeForTests(
	UWorld* TransientTestWorld,
	FWorldObstacleSegmentProbeForTests Probe)
{
	if (!IsTransientAutomationTestWorld(TransientTestWorld) || !Probe)
	{
		ensureMsgf(false,
			TEXT("World obstacle Mass probe overrides require a transient standalone automation Game World"));
		GWorldObstacleSegmentProbeForTests = nullptr;
		GWorldObstacleSegmentProbeWorldForTests.Reset();
		return;
	}
	GWorldObstacleSegmentProbeWorldForTests = TransientTestWorld;
	GWorldObstacleSegmentProbeForTests = MoveTemp(Probe);
}

void GuLiWingmanAvoidance::ResetWorldObstacleSegmentProbeForTests()
{
	GWorldObstacleSegmentProbeForTests = nullptr;
	GWorldObstacleSegmentProbeWorldForTests.Reset();
}

void GuLiWingmanAvoidance::AccumulateHeadingThreatsForTests(
	FGuLiWingmanAvoidanceFragment& State,
	const FHeadingSelection& Selection)
{
	AccumulateHeadingThreats(State, Selection);
}
#endif

void GuLiWingmanAvoidance::ComputeSpatialHashSeparation(
	const TConstArrayView<FSpatialSample> Samples,
	TArray<FSpatialResult>& OutResults)
{
	struct FGroupGrid
	{
		float CellSizeCentimeters = 1.0f;
		TMap<FIntVector, TArray<int32>> Buckets;
	};

	TArray<FSpatialSample> SortedSamples;
	SortedSamples.Append(Samples.GetData(), Samples.Num());
	SortedSamples.Sort([](const FSpatialSample& A, const FSpatialSample& B)
	{
		return WingmanLess(A.Handle, B.Handle);
	});

	TMap<FGuLiWingmanGroupHandle, FGroupGrid> Grids;
	for (const FSpatialSample& Sample : SortedSamples)
	{
		FGroupGrid& Grid = Grids.FindOrAdd(Sample.Handle.Flight.Group);
		Grid.CellSizeCentimeters = FMath::Max(
			Grid.CellSizeCentimeters,
			FMath::Max(1.0f, Sample.SeparationRadiusCentimeters));
	}
	for (int32 SampleIndex = 0; SampleIndex < SortedSamples.Num(); ++SampleIndex)
	{
		const FSpatialSample& Sample = SortedSamples[SampleIndex];
		FGroupGrid& Grid = Grids.FindChecked(Sample.Handle.Flight.Group);
		Grid.Buckets.FindOrAdd(MakeSpatialCell(Sample.Position, Grid.CellSizeCentimeters)).Add(SampleIndex);
	}

	OutResults.Reset(SortedSamples.Num());
	for (int32 LeftIndex = 0; LeftIndex < SortedSamples.Num(); ++LeftIndex)
	{
		const FSpatialSample& Left = SortedSamples[LeftIndex];
		FSpatialResult& Result = OutResults.AddDefaulted_GetRef();
		Result.Handle = Left.Handle;
		const FGroupGrid& Grid = Grids.FindChecked(Left.Handle.Flight.Group);
		Result.CellSizeCentimeters = Grid.CellSizeCentimeters;
		Result.Cell = MakeSpatialCell(Left.Position, Grid.CellSizeCentimeters);
		if (!Left.bAlive || Left.SeparationRadiusCentimeters <= UE_SMALL_NUMBER)
		{
			continue;
		}

		uint32 NeighborTests = 0u;
		uint32 ContributingNeighbors = 0u;
		FVector Acceleration = FVector::ZeroVector;
		TArray<FIntVector, TInlineAllocator<27>> VisitedCells;
		// Fixed X/Y/Z loop ordering plus stable bucket insertion makes accumulation deterministic.
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				for (int32 OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
				{
					const FIntVector NeighborCell = OffsetSpatialCell(
						Result.Cell, OffsetX, OffsetY, OffsetZ);
					if (VisitedCells.Contains(NeighborCell))
					{
						continue;
					}
					VisitedCells.Add(NeighborCell);
					const TArray<int32>* Bucket = Grid.Buckets.Find(NeighborCell);
					if (!Bucket)
					{
						continue;
					}
					for (const int32 RightIndex : *Bucket)
					{
						if (RightIndex == LeftIndex)
						{
							continue;
						}
						++NeighborTests;
						const FSpatialSample& Right = SortedSamples[RightIndex];
						if (!Right.bAlive || Left.Handle.Flight.Group != Right.Handle.Flight.Group)
						{
							continue;
						}

						FVector Delta = Left.Position - Right.Position;
						const double Distance = Delta.Size();
						if (Distance >= static_cast<double>(Left.SeparationRadiusCentimeters))
						{
							continue;
						}
						if (Distance <= UE_DOUBLE_SMALL_NUMBER)
						{
							const float Angle = static_cast<float>(Left.Handle.GetGroupMemberIndex())
								* UE_TWO_PI / static_cast<float>(GULI_WINGMAN_GROUP_SIZE);
							Delta = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.25f).GetSafeNormal();
						}
						else
						{
							Delta /= Distance;
						}
						++ContributingNeighbors;
						Acceleration += Delta * Left.MaximumAccelerationCentimetersPerSecondSquared
							* static_cast<float>(1.0 - Distance / Left.SeparationRadiusCentimeters);
					}
				}
			}
		}
		Result.Acceleration = Acceleration.GetClampedToMaxSize(
			FMath::Max(0.0f, Left.MaximumAccelerationCentimetersPerSecondSquared));
		Result.NeighborTests = static_cast<uint16>(FMath::Min<uint32>(NeighborTests, MAX_uint16));
		Result.ContributingNeighbors = static_cast<uint16>(
			FMath::Min<uint32>(ContributingNeighbors, MAX_uint16));
	}
}

GuLiWingmanAvoidance::FHeadingSelection GuLiWingmanAvoidance::SelectSafeHeading(
	const FVector& CurrentDirection,
	const FVector& DesiredDirection,
	const float CurrentSpeedCentimetersPerSecond,
	const FGuLiWingmanFormationRuntimeConfig& Tuning,
	TFunctionRef<FHeadingProbeResult(const FVector&, float)> Probe)
{
	FHeadingSelection Result;
	const FVector Current = CurrentDirection.IsNearlyZero()
		? FVector::ForwardVector : CurrentDirection.GetSafeNormal();
	const FVector Desired = DesiredDirection.IsNearlyZero()
		? Current : DesiredDirection.GetSafeNormal();
	const float Speed = FMath::Max(
		FMath::Abs(CurrentSpeedCentimetersPerSecond),
		FMath::Max(1.0f, Tuning.MinimumSpeedCentimetersPerSecond));
	const float ConfiguredLookAhead = FMath::Max(
		Tuning.ObstacleLookAheadCentimeters,
		Tuning.AgentRadiusCentimeters * 2.0f);
	const float PredictionSeconds = FMath::Clamp(ConfiguredLookAhead / Speed, 0.5f, 2.0f);
	const float LookAheadDistance = FMath::Max(ConfiguredLookAhead, Speed * PredictionSeconds);
	const float ImmediateSafeDistance = FMath::Min(
		LookAheadDistance,
		FMath::Max(Tuning.AgentRadiusCentimeters * 1.5f,
			Tuning.MinimumSpeedCentimetersPerSecond * 0.25f));
	const float MaximumTurnRadians = FMath::DegreesToRadians(FMath::Clamp(
		Tuning.MaximumTurnRateDegreesPerSecond * PredictionSeconds, 10.0f, 65.0f));
	const float MaximumPitchRadians = FMath::Min(MaximumTurnRadians, FMath::DegreesToRadians(35.0f));
	const FVector FeasibleDesired = GuLiWingmanMass::TurnDirectionToward(
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
			BestImmediateClearance = ImmediateProbe.ClearanceCentimeters;
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

void GuLiWingmanAvoidance::AdvanceRecoveryClock(
	FRecoveryClockState& State,
	const float DeltaSeconds,
	const bool bFullLookAheadSafe,
	const bool bHasVerifiedReopenedPath)
{
	constexpr float EnterRecoverySeconds = 3.0f;
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

FVector GuLiWingmanAvoidance::BuildRecoveryOrbitDirection(
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

UGuLiWingmanModeProcessor::UGuLiWingmanModeProcessor()
	: EntityQuery(*this)
{
	// The explicit per-group behavior runner owns low-frequency policy evaluation.
	// Keep this processor available for focused tests/tools, but never double-run it.
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = GuLiWingmanMass::ClientExecutionFlags();
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteInGroup = GuLiWingmanMass::ModeGroup;
}

void UGuLiWingmanModeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanCarrierFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanAbilityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanTuningFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanFlightDynamicsFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FGuLiWingmanOwnerMassTag>(EMassFragmentPresence::All);
}

void UGuLiWingmanModeProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaSeconds = FMath::Max(0.0f, Context.GetDeltaTimeSeconds());
	EntityQuery.ForEachEntityChunk(Context, [DeltaSeconds](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FGuLiWingmanCarrierFragment> Carriers = ChunkContext.GetFragmentView<FGuLiWingmanCarrierFragment>();
		const TConstArrayView<FGuLiWingmanAbilityFragment> Abilities = ChunkContext.GetFragmentView<FGuLiWingmanAbilityFragment>();
		const TConstArrayView<FGuLiWingmanTuningFragment> Tunings = ChunkContext.GetFragmentView<FGuLiWingmanTuningFragment>();
		const TArrayView<FGuLiWingmanFlightDynamicsFragment> Dynamics =
			ChunkContext.GetMutableFragmentView<FGuLiWingmanFlightDynamicsFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FGuLiWingmanFlightDynamicsFragment& Dynamic = Dynamics[It];
			Dynamic.ModeEvaluationAccumulator += DeltaSeconds;
			if (Dynamic.ModeEvaluationAccumulator < GuLiWingmanMass::ModeEvaluationPeriodSeconds)
			{
				continue;
			}
			Dynamic.ModeEvaluationAccumulator = FMath::Fmod(
				Dynamic.ModeEvaluationAccumulator, GuLiWingmanMass::ModeEvaluationPeriodSeconds);
			if (!Dynamic.bAlive || !Carriers[It].Source.IsValid()
				|| Abilities[It].AbilitySetRevision == 0u || Abilities[It].FormationCommandRevision == 0u)
			{
				Dynamic.Mode = EGuLiWingmanFlightMode::Stale;
				continue;
			}
			const double Distance = FVector::Distance(
				Transforms[It].GetTransform().GetLocation(), Carriers[It].Transform.GetLocation());
			if (Distance > Tunings[It].Formation.RecoveryDistanceCentimeters)
			{
				Dynamic.Mode = EGuLiWingmanFlightMode::Recover;
			}
			else if (Distance > Tunings[It].Formation.CatchUpDistanceCentimeters)
			{
				Dynamic.Mode = EGuLiWingmanFlightMode::CatchUp;
			}
			else if (Carriers[It].Velocity.SizeSquared() > FMath::Square(100.0))
			{
				Dynamic.Mode = EGuLiWingmanFlightMode::Follow;
			}
			else
			{
				Dynamic.Mode = EGuLiWingmanFlightMode::Orbit;
			}
		}
	});
}

UGuLiWingmanFormationGuidanceProcessor::UGuLiWingmanFormationGuidanceProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = GuLiWingmanMass::ClientExecutionFlags();
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteInGroup = GuLiWingmanMass::GuidanceGroup;
	ExecutionOrder.ExecuteAfter.Add(GuLiWingmanMass::ModeGroup);
}

void UGuLiWingmanFormationGuidanceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanCarrierFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanAbilityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanFormationSlotFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FGuLiWingmanTuningFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanFlightDynamicsFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanNavigationGuidanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanGuidanceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FGuLiWingmanOwnerMassTag>(EMassFragmentPresence::All);
}

void UGuLiWingmanFormationGuidanceProcessor::Execute(
	FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaSeconds = FMath::Max(0.0f, Context.GetDeltaTimeSeconds());
	EntityQuery.ForEachEntityChunk(Context, [DeltaSeconds](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FGuLiWingmanCarrierFragment> Carriers = ChunkContext.GetFragmentView<FGuLiWingmanCarrierFragment>();
		const TConstArrayView<FGuLiWingmanAbilityFragment> Abilities =
			ChunkContext.GetFragmentView<FGuLiWingmanAbilityFragment>();
		const TArrayView<FGuLiWingmanFormationSlotFragment> Slots =
			ChunkContext.GetMutableFragmentView<FGuLiWingmanFormationSlotFragment>();
		const TConstArrayView<FGuLiWingmanTuningFragment> Tunings =
			ChunkContext.GetFragmentView<FGuLiWingmanTuningFragment>();
		const TConstArrayView<FGuLiWingmanFlightDynamicsFragment> Dynamics =
			ChunkContext.GetFragmentView<FGuLiWingmanFlightDynamicsFragment>();
		const TConstArrayView<FGuLiWingmanNavigationGuidanceFragment> NavigationGuidance =
			ChunkContext.GetFragmentView<FGuLiWingmanNavigationGuidanceFragment>();
		const TArrayView<FGuLiWingmanGuidanceFragment> Guidance =
			ChunkContext.GetMutableFragmentView<FGuLiWingmanGuidanceFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FGuLiWingmanFormationSlotFragment& Slot = Slots[It];
			const float DirectionSign = Slot.bClockwise ? -1.0f : 1.0f;
			Slot.PhaseRadians = FMath::Fmod(
				Slot.PhaseRadians + DirectionSign * Slot.AngularSpeedRadiansPerSecond * DeltaSeconds + UE_TWO_PI,
				UE_TWO_PI);
			const float CosPhase = FMath::Cos(Slot.PhaseRadians);
			const float SinPhase = FMath::Sin(Slot.PhaseRadians);
			const FVector LocalOffset(Slot.RadiusCentimeters * CosPhase,
				Slot.RadiusCentimeters * SinPhase, Slot.HeightCentimeters);
			const FVector LocalTangent = DirectionSign * FVector(-SinPhase, CosPhase, 0.0f);
			const FTransform& Carrier = Carriers[It].Transform;
			FGuLiWingmanGuidanceFragment& Output = Guidance[It];
			const FVector FormationPosition = Carrier.TransformPositionNoScale(LocalOffset);
			Output.DesiredPosition = FormationPosition;
			const FGuLiWingmanNavigationGuidanceFragment& Navigation = NavigationGuidance[It];
			const bool bUseNavigationPath =
				(Dynamics[It].Mode == EGuLiWingmanFlightMode::CatchUp
					|| Dynamics[It].Mode == EGuLiWingmanFlightMode::Recover)
				&& Navigation.bHasPath
				&& !Navigation.Waypoint.ContainsNaN()
				&& !Navigation.PathGoal.ContainsNaN()
				&& Navigation.AbilitySetRevision == Abilities[It].AbilitySetRevision
				&& Navigation.FormationCommandRevision == Abilities[It].FormationCommandRevision;
			if (bUseNavigationPath)
			{
				// Preserve each member's formation-relative offset around the shared
				// Flight waypoint. The path changes Guidance only; no teleport occurs.
				Output.DesiredPosition = Navigation.Waypoint + (FormationPosition - Navigation.PathGoal);
			}
			const FVector ToSlot = Output.DesiredPosition - Transforms[It].GetTransform().GetLocation();
			const FVector Tangent = Carrier.TransformVectorNoScale(LocalTangent).GetSafeNormal();
			const float SlotWeight = bUseNavigationPath ? 0.95f
				: (Dynamics[It].Mode == EGuLiWingmanFlightMode::Orbit ? 0.35f : 0.8f);
			Output.DesiredForward = (Tangent * (1.0f - SlotWeight) + ToSlot.GetSafeNormal() * SlotWeight).GetSafeNormal();
			if (Output.DesiredForward.IsNearlyZero())
			{
				Output.DesiredForward = Tangent.IsNearlyZero() ? Carrier.GetRotation().GetForwardVector() : Tangent;
			}
			switch (Dynamics[It].Mode)
			{
			case EGuLiWingmanFlightMode::CatchUp:
			case EGuLiWingmanFlightMode::Recover:
				Output.DesiredSpeedCentimetersPerSecond = Tunings[It].Formation.CatchUpSpeedCentimetersPerSecond;
				break;
			case EGuLiWingmanFlightMode::Stale:
				Output.DesiredSpeedCentimetersPerSecond = 0.0f;
				break;
			default:
				Output.DesiredSpeedCentimetersPerSecond = Tunings[It].Formation.CruiseSpeedCentimetersPerSecond;
				break;
			}
		}
	});
}

UGuLiWingmanAvoidanceProcessor::UGuLiWingmanAvoidanceProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = GuLiWingmanMass::ClientExecutionFlags();
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteInGroup = GuLiWingmanMass::AvoidanceGroup;
	ExecutionOrder.ExecuteAfter.Add(GuLiWingmanMass::GuidanceGroup);
}

void UGuLiWingmanAvoidanceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanIdentityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanTuningFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanGuidanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanNavigationGuidanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanFlightDynamicsFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanAvoidanceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FGuLiWingmanOwnerMassTag>(EMassFragmentPresence::All);
}

void UGuLiWingmanAvoidanceProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	struct FSample
	{
		FGuLiWingmanHandle Handle;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FGuLiWingmanGuidanceFragment Guidance;
		FGuLiWingmanNavigationGuidanceFragment Navigation;
		FGuLiWingmanAvoidanceFragment PreviousAvoidance;
		FGuLiWingmanFormationRuntimeConfig Tuning;
		bool bAlive = false;
	};
	TArray<FSample> Samples;
	EntityQuery.ForEachEntityChunk(Context, [&Samples](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FGuLiWingmanIdentityFragment> Identities = ChunkContext.GetFragmentView<FGuLiWingmanIdentityFragment>();
		const TConstArrayView<FGuLiWingmanTuningFragment> Tunings = ChunkContext.GetFragmentView<FGuLiWingmanTuningFragment>();
		const TConstArrayView<FGuLiWingmanGuidanceFragment> Guidance =
			ChunkContext.GetFragmentView<FGuLiWingmanGuidanceFragment>();
		const TConstArrayView<FGuLiWingmanNavigationGuidanceFragment> Navigation =
			ChunkContext.GetFragmentView<FGuLiWingmanNavigationGuidanceFragment>();
		const TConstArrayView<FGuLiWingmanFlightDynamicsFragment> Dynamics =
			ChunkContext.GetFragmentView<FGuLiWingmanFlightDynamicsFragment>();
		const TConstArrayView<FGuLiWingmanAvoidanceFragment> PreviousAvoidance =
			ChunkContext.GetFragmentView<FGuLiWingmanAvoidanceFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FSample& Sample = Samples.AddDefaulted_GetRef();
			Sample.Handle = Identities[It].Handle;
			Sample.Position = Transforms[It].GetTransform().GetLocation();
			Sample.Velocity = Dynamics[It].Velocity;
			Sample.Guidance = Guidance[It];
			Sample.Navigation = Navigation[It];
			Sample.PreviousAvoidance = PreviousAvoidance[It];
			Sample.Tuning = Tunings[It].Formation;
			Sample.bAlive = Dynamics[It].bAlive;
		}
	});

	TArray<GuLiWingmanAvoidance::FSpatialSample> SpatialSamples;
	SpatialSamples.Reserve(Samples.Num());
	for (const FSample& Sample : Samples)
	{
		GuLiWingmanAvoidance::FSpatialSample& Spatial = SpatialSamples.AddDefaulted_GetRef();
		Spatial.Handle = Sample.Handle;
		Spatial.Position = Sample.Position;
		Spatial.SeparationRadiusCentimeters = Sample.Tuning.SeparationRadiusCentimeters;
		Spatial.MaximumAccelerationCentimetersPerSecondSquared =
			Sample.Tuning.MaximumAccelerationCentimetersPerSecondSquared;
		Spatial.bAlive = Sample.bAlive;
	}
	TArray<GuLiWingmanAvoidance::FSpatialResult> SpatialResults;
	GuLiWingmanAvoidance::ComputeSpatialHashSeparation(SpatialSamples, SpatialResults);
	TMap<FGuLiWingmanHandle, GuLiWingmanAvoidance::FSpatialResult> SpatialByHandle;
	SpatialByHandle.Reserve(SpatialResults.Num());
	for (const GuLiWingmanAvoidance::FSpatialResult& Spatial : SpatialResults)
	{
		SpatialByHandle.Add(Spatial.Handle, Spatial);
	}

	UWorld* World = EntityManager.GetWorld();
	FCollisionObjectQueryParams ObstacleObjectTypes;
	ObstacleObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObstacleObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams ObstacleQueryParams(SCENE_QUERY_STAT(GuLiWingmanObstacleAvoidance), false);
	AddLocallyControlledPawnsToIgnoredActors(World, ObstacleQueryParams);

	struct FAvoidanceOutput
	{
		FGuLiWingmanAvoidanceFragment State;
	};
	TMap<FGuLiWingmanHandle, FAvoidanceOutput> Outputs;
	Outputs.Reserve(Samples.Num());
	const float DeltaSeconds = FMath::Max(0.0f, Context.GetDeltaTimeSeconds());
	for (const FSample& Sample : Samples)
	{
		FGuLiWingmanAvoidanceFragment State = Sample.PreviousAvoidance;
		State.Acceleration = FVector::ZeroVector;
		State.bHasSafeDirection = false;
		State.bDetectedWorldStatic = false;
		State.bDetectedWorldDynamic = false;
		State.bDetectedFlightNavBoundary = false;
		State.HeadingProbeCount = 0u;
		const GuLiWingmanAvoidance::FSpatialResult* Spatial = SpatialByHandle.Find(Sample.Handle);
		if (Spatial)
		{
			State.SpatialCell = Spatial->Cell;
			State.SpatialCellSizeCentimeters = Spatial->CellSizeCentimeters;
			State.SpatialNeighborTests = Spatial->NeighborTests;
			State.SeparationNeighborCount = Spatial->ContributingNeighbors;
		}
		else
		{
			State.SpatialCell = FIntVector::ZeroValue;
			State.SpatialCellSizeCentimeters = 0.0f;
			State.SpatialNeighborTests = 0u;
			State.SeparationNeighborCount = 0u;
		}

		if (!Sample.bAlive)
		{
			State.ConsecutiveBlockedSeconds = 0.0f;
			State.ConsecutiveClearSeconds = 0.0f;
			State.bControlledRecovery = false;
			FAvoidanceOutput Output;
			Output.State = MoveTemp(State);
			Outputs.Add(Sample.Handle, MoveTemp(Output));
			continue;
		}

		State.RecoveryPointValidationAccumulator += FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
		const bool bNeedsRecoveryAnchor = Sample.Navigation.bHasPath
			|| State.bControlledRecovery || State.ConsecutiveBlockedSeconds > 0.0f;
		const bool bWaypointChanged = Sample.Navigation.bHasPath
			&& (State.VerifiedWaypointRequestSerial != Sample.Navigation.RequestSerial
				|| State.VerifiedWaypointPathPointIndex != Sample.Navigation.PathPointIndex);
		const bool bShouldValidateRecoveryPoint = bNeedsRecoveryAnchor
			&& (bWaypointChanged || !State.bHasVerifiedSafePoint
				|| State.RecoveryPointValidationAccumulator >= 0.2f);
		if (bShouldValidateRecoveryPoint)
		{
			State.RecoveryPointValidationAccumulator = 0.0f;
			bool bFoundVerifiedPoint = false;
			double BestDistanceSquared = MAX_dbl;
			FVector BestPoint = FVector::ZeroVector;
			uint32 BestRequestSerial = State.VerifiedWaypointRequestSerial;
			uint16 BestPointIndex = State.VerifiedWaypointPathPointIndex;
			const auto ConsiderVerifiedPoint = [&](const FVector& Point, const uint32 RequestSerial,
				const uint16 PointIndex)
			{
				if (Point.ContainsNaN())
				{
					return;
				}
				if (!ValidateClientFlightNavSegment(
					World, Sample.Position, Point, Sample.Tuning.AgentRadiusCentimeters))
				{
					return;
				}
				const double DistanceSquared = FVector::DistSquared(Sample.Position, Point);
				if (!bFoundVerifiedPoint || DistanceSquared < BestDistanceSquared)
				{
					bFoundVerifiedPoint = true;
					BestDistanceSquared = DistanceSquared;
					BestPoint = Point;
					BestRequestSerial = RequestSerial;
					BestPointIndex = PointIndex;
				}
			};
			if (Sample.Navigation.bHasPath)
			{
				ConsiderVerifiedPoint(Sample.Navigation.Waypoint,
					Sample.Navigation.RequestSerial, Sample.Navigation.PathPointIndex);
			}
			if (State.bHasVerifiedSafePoint)
			{
				ConsiderVerifiedPoint(State.LastVerifiedSafePoint,
					State.VerifiedWaypointRequestSerial, State.VerifiedWaypointPathPointIndex);
			}
			// A containing baked cell is the final bounded holding anchor when a Flight
			// has not yet received its replacement waypoint. No path search is performed.
			if (!bFoundVerifiedPoint)
			{
				ConsiderVerifiedPoint(Sample.Position, 0u, 0u);
			}
			State.bHasVerifiedSafePoint = bFoundVerifiedPoint;
			State.bRecoveryPointCurrentlyValid = bFoundVerifiedPoint;
			if (bFoundVerifiedPoint)
			{
				State.LastVerifiedSafePoint = BestPoint;
				State.VerifiedWaypointRequestSerial = BestRequestSerial;
				State.VerifiedWaypointPathPointIndex = BestPointIndex;
			}
		}

		const FVector CurrentDirection = Sample.Velocity.IsNearlyZero()
			? Sample.Guidance.DesiredForward.GetSafeNormal()
			: Sample.Velocity.GetSafeNormal();
		const FVector ToGuidance = Sample.Guidance.DesiredPosition - Sample.Position;
		FVector DesiredDirection = (Sample.Guidance.DesiredForward.GetSafeNormal() * 0.4f
			+ ToGuidance.GetSafeNormal() * 0.6f).GetSafeNormal();
		if (DesiredDirection.IsNearlyZero())
		{
			DesiredDirection = CurrentDirection.IsNearlyZero() ? FVector::ForwardVector : CurrentDirection;
		}
		if (Spatial && !Spatial->Acceleration.IsNearlyZero())
		{
			const float DesiredSpeed = FMath::Max(
				Sample.Tuning.MinimumSpeedCentimetersPerSecond,
				Sample.Guidance.DesiredSpeedCentimetersPerSecond);
			DesiredDirection = (DesiredDirection * DesiredSpeed + Spatial->Acceleration).GetSafeNormal();
		}

		const auto ProbeWorld = [&](const FVector& Direction, const float Distance)
		{
			const FVector End = Sample.Position + Direction.GetSafeNormal() * Distance;
			GuLiWingmanAvoidance::FHeadingProbeResult ProbeResult =
				ProbeClientWorldObstacleSegment(
					World,
					Sample.Position,
					End,
					Sample.Tuning.AgentRadiusCentimeters,
					ObstacleObjectTypes,
					ObstacleQueryParams);
			ProbeResult.bFlightNavSegmentValid = ValidateClientFlightNavSegment(
				World, Sample.Position, End, Sample.Tuning.AgentRadiusCentimeters);
			return ProbeResult;
		};

		const float CurrentSpeed = static_cast<float>(Sample.Velocity.Size());
		const GuLiWingmanAvoidance::FHeadingSelection NormalSelection =
			GuLiWingmanAvoidance::SelectSafeHeading(
				CurrentDirection, DesiredDirection, CurrentSpeed, Sample.Tuning, ProbeWorld);
		AccumulateHeadingThreats(State, NormalSelection);
		GuLiWingmanAvoidance::FRecoveryClockState RecoveryClock;
		RecoveryClock.ConsecutiveBlockedSeconds = State.ConsecutiveBlockedSeconds;
		RecoveryClock.ConsecutiveClearSeconds = State.ConsecutiveClearSeconds;
		RecoveryClock.bControlledRecovery = State.bControlledRecovery;
		GuLiWingmanAvoidance::AdvanceRecoveryClock(
			RecoveryClock,
			DeltaSeconds,
			NormalSelection.bFullLookAheadSafe,
			Sample.Navigation.bHasPath && State.bRecoveryPointCurrentlyValid);
		State.ConsecutiveBlockedSeconds = RecoveryClock.ConsecutiveBlockedSeconds;
		State.ConsecutiveClearSeconds = RecoveryClock.ConsecutiveClearSeconds;
		State.bControlledRecovery = RecoveryClock.bControlledRecovery;

		GuLiWingmanAvoidance::FHeadingSelection AppliedSelection = NormalSelection;
		if (State.bControlledRecovery)
		{
			const FVector RecoveryPoint = State.bHasVerifiedSafePoint
				? State.LastVerifiedSafePoint : Sample.Position;
			const FVector RecoveryDesired = GuLiWingmanAvoidance::BuildRecoveryOrbitDirection(
				Sample.Position,
				CurrentDirection,
				RecoveryPoint,
				FMath::Max(Sample.Tuning.AgentRadiusCentimeters * 4.0f,
					Sample.Tuning.SeparationRadiusCentimeters));
			const GuLiWingmanAvoidance::FHeadingSelection RecoverySelection =
				GuLiWingmanAvoidance::SelectSafeHeading(
					CurrentDirection, RecoveryDesired, CurrentSpeed, Sample.Tuning, ProbeWorld);
			AccumulateHeadingThreats(State, RecoverySelection);
			State.HeadingProbeCount = static_cast<uint16>(FMath::Min<uint32>(
				static_cast<uint32>(NormalSelection.ProbeCount) + RecoverySelection.ProbeCount,
				MAX_uint16));
			if (RecoverySelection.bImmediateStepSafe)
			{
				AppliedSelection = RecoverySelection;
			}
		}
		else
		{
			State.HeadingProbeCount = NormalSelection.ProbeCount;
		}

		if (AppliedSelection.bImmediateStepSafe)
		{
			State.SafeDirection = AppliedSelection.Direction.GetSafeNormal();
			State.bHasSafeDirection = !State.SafeDirection.IsNearlyZero();
			if (State.bHasSafeDirection)
			{
				const float SteeringSpeed = State.bControlledRecovery
					? Sample.Tuning.MinimumSpeedCentimetersPerSecond
					: FMath::Max(Sample.Tuning.MinimumSpeedCentimetersPerSecond, CurrentSpeed);
				State.Acceleration = (State.SafeDirection * SteeringSpeed - Sample.Velocity)
					.GetClampedToMaxSize(
						Sample.Tuning.MaximumAccelerationCentimetersPerSecondSquared);
			}
		}
		FAvoidanceOutput Output;
		Output.State = MoveTemp(State);
		Outputs.Add(Sample.Handle, MoveTemp(Output));
	}

	EntityQuery.ForEachEntityChunk(Context, [&Outputs](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FGuLiWingmanIdentityFragment> Identities = ChunkContext.GetFragmentView<FGuLiWingmanIdentityFragment>();
		const TArrayView<FGuLiWingmanAvoidanceFragment> Avoidance =
			ChunkContext.GetMutableFragmentView<FGuLiWingmanAvoidanceFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			if (const FAvoidanceOutput* Output = Outputs.Find(Identities[It].Handle))
			{
				Avoidance[It] = Output->State;
			}
		}
	});
}

UGuLiWingmanFlightIntegrationProcessor::UGuLiWingmanFlightIntegrationProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = GuLiWingmanMass::ClientExecutionFlags();
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	ExecutionOrder.ExecuteInGroup = GuLiWingmanMass::IntegrationGroup;
	ExecutionOrder.ExecuteAfter.Add(GuLiWingmanMass::AvoidanceGroup);
}

void UGuLiWingmanFlightIntegrationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FGuLiWingmanGuidanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanAvoidanceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FGuLiWingmanTuningFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGuLiWingmanFlightDynamicsFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FGuLiWingmanOwnerMassTag>(EMassFragmentPresence::All);
}

void UGuLiWingmanFlightIntegrationProcessor::Execute(
	FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float FrameDeltaSeconds = FMath::Max(0.0f, Context.GetDeltaTimeSeconds());
	UWorld* World = EntityManager.GetWorld();
	FCollisionObjectQueryParams ObstacleObjectTypes;
	ObstacleObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObstacleObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams ObstacleQueryParams(SCENE_QUERY_STAT(GuLiWingmanIntegrationFinalStep), false);
	AddLocallyControlledPawnsToIgnoredActors(World, ObstacleQueryParams);
	EntityQuery.ForEachEntityChunk(Context,
		[FrameDeltaSeconds, World, ObstacleObjectTypes, ObstacleQueryParams](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FGuLiWingmanGuidanceFragment> Guidance = ChunkContext.GetFragmentView<FGuLiWingmanGuidanceFragment>();
		const TArrayView<FGuLiWingmanAvoidanceFragment> Avoidance =
			ChunkContext.GetMutableFragmentView<FGuLiWingmanAvoidanceFragment>();
		const TConstArrayView<FGuLiWingmanTuningFragment> Tunings = ChunkContext.GetFragmentView<FGuLiWingmanTuningFragment>();
		const TArrayView<FGuLiWingmanFlightDynamicsFragment> Dynamics =
			ChunkContext.GetMutableFragmentView<FGuLiWingmanFlightDynamicsFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FGuLiWingmanFlightDynamicsFragment& Dynamic = Dynamics[It];
			Dynamic.FixedStepAccumulator = FMath::Min(
				Dynamic.FixedStepAccumulator + FrameDeltaSeconds, GuLiWingmanMass::MaximumAccumulatorSeconds);
			FTransform Transform = Transforms[It].GetTransform();
			bool bTransformChanged = false;
			while (Dynamic.FixedStepAccumulator >= GuLiWingmanMass::FixedStepSeconds)
			{
				Dynamic.FixedStepAccumulator -= GuLiWingmanMass::FixedStepSeconds;
				if (!Dynamic.bAlive || Dynamic.Mode == EGuLiWingmanFlightMode::Stale
					|| Guidance[It].DesiredSpeedCentimetersPerSecond <= 0.0f)
				{
					continue;
				}
				const FVector Location = Transform.GetLocation();
				const FVector ToTarget = Guidance[It].DesiredPosition - Location;
				FVector DesiredDirection = (Guidance[It].DesiredForward * 0.4f
					+ ToTarget.GetSafeNormal() * 0.6f).GetSafeNormal();
				if (DesiredDirection.IsNearlyZero()) DesiredDirection = Transform.GetRotation().GetForwardVector();
				const FGuLiWingmanFormationRuntimeConfig& Tuning = Tunings[It].Formation;
				const FVector CurrentDirection = Dynamic.Velocity.IsNearlyZero()
					? Transform.GetRotation().GetForwardVector() : Dynamic.Velocity.GetSafeNormal();
				if (Avoidance[It].bHasSafeDirection)
				{
					// The obstacle processor only publishes a direction after its full or
					// immediate-step sphere sweep succeeds. Integration owns the turn.
					DesiredDirection = Avoidance[It].SafeDirection.GetSafeNormal();
				}
				else if (Avoidance[It].ConsecutiveBlockedSeconds > 0.0f
					|| Avoidance[It].bControlledRecovery)
				{
					// No sampled heading was safe even for the bounded immediate step.
					// Do not inject an unverified new heading; retain fixed-wing attitude
					// while decelerating to (never below) the configured minimum speed.
					DesiredDirection = CurrentDirection;
				}
				const float MaximumTurnRadians = FMath::DegreesToRadians(Tuning.MaximumTurnRateDegreesPerSecond)
					* GuLiWingmanMass::FixedStepSeconds;
				const FVector NewDirection = GuLiWingmanMass::TurnDirectionToward(
					CurrentDirection, DesiredDirection, MaximumTurnRadians);
				const float CurrentSpeed = FMath::Max(
					static_cast<float>(Dynamic.Velocity.Size()), Tuning.MinimumSpeedCentimetersPerSecond);
				const float RequestedSpeed = (Avoidance[It].bControlledRecovery
					|| Avoidance[It].ConsecutiveBlockedSeconds > 0.0f)
					? Tuning.MinimumSpeedCentimetersPerSecond
					: Guidance[It].DesiredSpeedCentimetersPerSecond;
				const float TargetSpeed = FMath::Clamp(RequestedSpeed,
					Tuning.MinimumSpeedCentimetersPerSecond, Tuning.CatchUpSpeedCentimetersPerSecond);
				const float MaximumSpeedDelta = (TargetSpeed >= CurrentSpeed
					? Tuning.MaximumAccelerationCentimetersPerSecondSquared
					: Tuning.MaximumDecelerationCentimetersPerSecondSquared)
					* GuLiWingmanMass::FixedStepSeconds;
				const float NewSpeed = FMath::Clamp(FMath::FInterpConstantTo(
					CurrentSpeed, TargetSpeed, GuLiWingmanMass::FixedStepSeconds,
					MaximumSpeedDelta / GuLiWingmanMass::FixedStepSeconds),
					Tuning.MinimumSpeedCentimetersPerSecond, Tuning.CatchUpSpeedCentimetersPerSecond);
				Dynamic.Velocity = NewDirection * NewSpeed;
				const FVector ProposedLocation = Location
					+ Dynamic.Velocity * GuLiWingmanMass::FixedStepSeconds;
				const float StepDistance = static_cast<float>(
					FVector::Distance(Location, ProposedLocation));
				const GuLiWingmanAvoidance::FHeadingProbeResult PhysicalProbe =
					ProbeClientWorldObstacleSegment(
						World,
						Location,
						ProposedLocation,
						Tuning.AgentRadiusCentimeters,
						ObstacleObjectTypes,
						ObstacleQueryParams);
				const bool bFlightNavSegmentValid = ValidateClientFlightNavSegment(
					World, Location, ProposedLocation, Tuning.AgentRadiusCentimeters);
				const bool bPhysicalSegmentClear =
					PhysicalProbe.ClearanceCentimeters + 0.5f >= StepDistance;
				// Final fail-closed gate validates the exact finite-turn segment directly
				// in front of the sole 30 Hz owner Transform mutation. Avoidance may
				// publish a sweep-safe target heading, but Integration can only turn
				// toward it by the configured rate and therefore travels a different arc.
				if (!bFlightNavSegmentValid || !bPhysicalSegmentClear)
				{
					FGuLiWingmanAvoidanceFragment& FailedAvoidance = Avoidance[It];
					FailedAvoidance.bDetectedWorldStatic |= PhysicalProbe.bWorldStatic;
					FailedAvoidance.bDetectedWorldDynamic |= PhysicalProbe.bWorldDynamic;
					FailedAvoidance.bDetectedFlightNavBoundary |= !bFlightNavSegmentValid;
					FailedAvoidance.bHasSafeDirection = false;
					FailedAvoidance.Acceleration = FVector::ZeroVector;
					FailedAvoidance.ConsecutiveBlockedSeconds = FMath::Max(
						FailedAvoidance.ConsecutiveBlockedSeconds,
						GuLiWingmanMass::FixedStepSeconds);
					FailedAvoidance.ConsecutiveClearSeconds = 0.0f;
					Dynamic.Mode = EGuLiWingmanFlightMode::Recover;
					Dynamic.Velocity = CurrentDirection.GetSafeNormal()
						* Tuning.MinimumSpeedCentimetersPerSecond;
					Dynamic.BankDegrees = FMath::FInterpTo(
						Dynamic.BankDegrees, 0.0f, GuLiWingmanMass::FixedStepSeconds, 4.0f);
					Dynamic.FixedStepAccumulator = 0.0f;
					break;
				}
				Transform.AddToTranslation(Dynamic.Velocity * GuLiWingmanMass::FixedStepSeconds);
				bTransformChanged = true;
				if (!Dynamic.Velocity.IsNearlyZero())
				{
					const FVector Forward = Transform.GetRotation().GetForwardVector();
					const float TurnSign = FVector::DotProduct(
						FVector::CrossProduct(Forward, Dynamic.Velocity.GetSafeNormal()), FVector::UpVector);
					Dynamic.BankDegrees = FMath::FInterpTo(Dynamic.BankDegrees,
						FMath::Clamp(-TurnSign * Tuning.MaximumBankDegrees * 4.0f,
							-Tuning.MaximumBankDegrees, Tuning.MaximumBankDegrees),
						GuLiWingmanMass::FixedStepSeconds, 4.0f);
					FRotator Rotation = Dynamic.Velocity.Rotation();
					Rotation.Roll = Dynamic.BankDegrees;
					Transform.SetRotation(Rotation.Quaternion());
				}
			}
			if (bTransformChanged)
			{
				Transforms[It].SetTransform(Transform);
			}
		}
	});
}
