// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderAvoidancePolicy.h"

#include "Avoidance/MassAvoidanceFragments.h"
#include "MassMovementFragments.h"

namespace GuLiCommanderAvoidancePolicy
{
	namespace
	{
		bool IsCandidateBefore(const FNearestCandidate& Lhs, const FNearestCandidate& Rhs)
		{
			return Lhs.DistanceSquared < Rhs.DistanceSquared
				|| (Lhs.DistanceSquared == Rhs.DistanceSquared && Lhs.StableKey < Rhs.StableKey);
		}

		double ComputeClosestPointOfApproach(
			const FVector& RelativePosition,
			const FVector& RelativeVelocity,
			const double TotalRadius,
			const double TimeHorizon)
		{
			const double A = FVector::DotProduct(RelativeVelocity, RelativeVelocity);
			const double InverseTwoA = A > UE_SMALL_NUMBER ? 1.0 / (2.0 * A) : 0.0;
			const double B = FMath::Min(0.0, 2.0 * FVector::DotProduct(RelativeVelocity, RelativePosition));
			const double C = FVector::DotProduct(RelativePosition, RelativePosition)
				- FMath::Square(TotalRadius);
			const double Discriminant = FMath::Sqrt(FMath::Max(0.0, B * B - 4.0 * A * C));
			return FMath::Clamp((-B - Discriminant) * InverseTwoA, 0.0, TimeHorizon);
		}
	}

	FPhaseAdvanceResult AdvancePhases(
		const double DeltaSeconds,
		double& InOutAccumulatorSeconds,
		uint64& InOutNextSequence)
	{
		FPhaseAdvanceResult Result;
		if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
		{
			return Result;
		}

		InOutAccumulatorSeconds = FMath::Clamp(
			InOutAccumulatorSeconds + DeltaSeconds,
			0.0,
			MaximumAccumulatedSeconds);
		while (InOutAccumulatorSeconds + UE_DOUBLE_SMALL_NUMBER >= FixedStepSeconds
			&& Result.FixedSteps < static_cast<int32>(SolvePhaseCount))
		{
			InOutAccumulatorSeconds = FMath::Max(0.0, InOutAccumulatorSeconds - FixedStepSeconds);
			const uint32 Phase = static_cast<uint32>(InOutNextSequence % SolvePhaseCount);
			Result.PhaseMask |= static_cast<uint8>(1u << Phase);
			Result.SequenceByPhase[Phase] = InOutNextSequence;
			Result.LastSequence = InOutNextSequence;
			++InOutNextSequence;
			++Result.FixedSteps;
		}
		return Result;
	}

	uint32 ResolveSolvePhase(const uint32 StableSoldierId)
	{
		return StableSoldierId != 0u ? StableSoldierId % SolvePhaseCount : 0u;
	}

	bool ShouldSolve(
		const uint8 DuePhaseMask,
		const uint32 StableSoldierId,
		const uint32 OrderRevision,
		const uint32 LastProcessedOrderRevision,
		const bool bMoving)
	{
		if (!bMoving || StableSoldierId == 0u || DuePhaseMask == 0u)
		{
			return false;
		}
		const uint8 PhaseBit = static_cast<uint8>(1u << ResolveSolvePhase(StableSoldierId));
		return OrderRevision != LastProcessedOrderRevision
			|| (DuePhaseMask & PhaseBit) != 0u;
	}

	FIntPoint MakeSpatialCell(const FVector& Location)
	{
		if (Location.ContainsNaN())
		{
			return FIntPoint::ZeroValue;
		}
		return FIntPoint(
			FMath::FloorToInt(Location.X / SpatialCellSizeCentimeters),
			FMath::FloorToInt(Location.Y / SpatialCellSizeCentimeters));
	}

	int32 BuildSpatialGrid(
		const TConstArrayView<FAgentSnapshot> Agents,
		FAvoidanceSpatialGrid& InOutGrid)
	{
		InOutGrid.Reset();
		int32 MaximumBucketOccupancy = 0;
		for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
		{
			const FAgentSnapshot& Agent = Agents[AgentIndex];
			if (!Agent.bParticipates || Agent.StableKey == 0u || Agent.Location.ContainsNaN()
				|| !FMath::IsFinite(Agent.Radius) || Agent.Radius <= 0.0f)
			{
				continue;
			}
			const FVector Extent=Agent.bEnvironment ? FVector(Agent.Radius,Agent.Radius,0) : FVector::ZeroVector;
			const auto Min=MakeSpatialCell(Agent.Location-Extent),Max=MakeSpatialCell(Agent.Location+Extent);
			for (int32 X=Min.X; X<=Max.X; ++X) for (int32 Y=Min.Y; Y<=Max.Y; ++Y)
			{
				auto& Bucket=InOutGrid.FindOrAdd({X,Y}); Bucket.Add(AgentIndex);
				MaximumBucketOccupancy=FMath::Max(MaximumBucketOccupancy,Bucket.Num());
			}
		}
		return MaximumBucketOccupancy;
	}

	FCandidateQueryMetrics SelectNearestCandidates(
		const int32 AgentIndex,
		const TConstArrayView<FAgentSnapshot> Agents,
		const FAvoidanceSpatialGrid& Grid,
		FNearestCandidateList& OutCandidates,
		const float DetectionDistance,
		const float MaximumHeightDifference)
	{
		FCandidateQueryMetrics Metrics;
		OutCandidates.Reset();
		if (!Agents.IsValidIndex(AgentIndex)
			|| !FMath::IsFinite(DetectionDistance) || DetectionDistance <= 0.0f
			|| !FMath::IsFinite(MaximumHeightDifference) || MaximumHeightDifference < 0.0f)
		{
			return Metrics;
		}

		const FAgentSnapshot& Agent = Agents[AgentIndex];
		if (!Agent.bParticipates || Agent.StableKey == 0u || Agent.Location.ContainsNaN())
		{
			return Metrics;
		}
		const int32 CellRadius = FMath::CeilToInt(DetectionDistance / SpatialCellSizeCentimeters);
		const FIntPoint CenterCell = MakeSpatialCell(Agent.Location);
		const double DistanceCutoffSquared = FMath::Square(static_cast<double>(DetectionDistance));
		TSet<int32> Seen; FNearestCandidateList Environment;
		for (int32 CellX = CenterCell.X - CellRadius; CellX <= CenterCell.X + CellRadius; ++CellX)
		{
			for (int32 CellY = CenterCell.Y - CellRadius; CellY <= CenterCell.Y + CellRadius; ++CellY)
			{
				const FAvoidanceBucket* Bucket = Grid.Find(FIntPoint(CellX, CellY));
				if (!Bucket)
				{
					continue;
				}
				for (const int32 OtherIndex : *Bucket)
				{
					if (OtherIndex == AgentIndex || !Agents.IsValidIndex(OtherIndex))
					{
						continue;
					}
					++Metrics.BucketEntriesVisited;
					if (Seen.Contains(OtherIndex)) continue;
					Seen.Add(OtherIndex);
					const FAgentSnapshot& Other = Agents[OtherIndex];
					if (!Other.bParticipates || Other.StableKey == 0u
						|| FMath::Abs(Agent.Location.Z - Other.Location.Z) > MaximumHeightDifference)
					{
						continue;
					}
					const double CenterDistance = FVector::Dist2D(Agent.Location, Other.Location);
					const double DistanceSquared=FMath::Square(Other.bEnvironment ? FMath::Max(0.,CenterDistance-Other.Radius-Agent.Radius) : CenterDistance);
					if (!FMath::IsFinite(DistanceSquared) || DistanceSquared > DistanceCutoffSquared)
					{
						continue;
					}
					++Metrics.ExactCandidates;
					FNearestCandidate Candidate;
					Candidate.AgentIndex = OtherIndex;
					Candidate.DistanceSquared = DistanceSquared;
					Candidate.StableKey = Other.StableKey;

					auto& List=Other.bEnvironment ? Environment : OutCandidates;
					const int32 Limit=Other.bEnvironment ? 4 : MaximumNearestCandidates;
					int32 InsertIndex = 0;
					while (InsertIndex < List.Num()
						&& !IsCandidateBefore(Candidate, List[InsertIndex]))
					{
						++InsertIndex;
					}
					if (InsertIndex < Limit)
					{
						List.Insert(Candidate, InsertIndex);
						if (List.Num() > Limit)
						{
							List.Pop(EAllowShrinking::No);
						}
					}
				}
			}
		}
		// Reserve two of the six CPA collider slots for environmental geometry.
		const int32 Reserved=FMath::Min(2,Environment.Num());
		for (int32 I=Reserved-1; I>=0; --I) OutCandidates.Insert(Environment[I],0);
		if (OutCandidates.Num()>MaximumNearestCandidates) OutCandidates.SetNum(MaximumNearestCandidates,EAllowShrinking::No);
		return Metrics;
	}

	FPredictiveParameters MakePredictiveParameters(
		const FMassMovingAvoidanceParameters& AvoidanceParameters,
		const FMassMovementParameters& MovementParameters)
	{
		FPredictiveParameters Result;
		Result.PredictiveAvoidanceTime = FMath::Max(
			AvoidanceParameters.PredictiveAvoidanceTime,
			UE_KINDA_SMALL_NUMBER);
		Result.PredictiveAvoidanceRadiusScale = FMath::Max(
			AvoidanceParameters.PredictiveAvoidanceRadiusScale,
			0.0f);
		Result.PredictiveAvoidanceDistance = FMath::Max(
			AvoidanceParameters.PredictiveAvoidanceDistance,
			UE_KINDA_SMALL_NUMBER);
		Result.PredictiveAvoidanceStiffness = FMath::Max(
			AvoidanceParameters.ObstaclePredictiveAvoidanceStiffness,
			0.0f);
		Result.StandingObstacleAvoidanceScale = FMath::Max(
			AvoidanceParameters.StandingObstacleAvoidanceScale,
			0.0f);
		Result.MaximumSpeed = FMath::Max(MovementParameters.MaxSpeed, 0.0f);
		Result.MaximumAcceleration = FMath::Max(MovementParameters.MaxAcceleration, 0.0f);
		return Result;
	}

	float CalculatePathFade(
		const double CurrentWorldSeconds,
		const double CurrentActionStartSeconds,
		const bool bPreviousActionWasMove,
		const bool bWillStandAtGoal,
		const float DistanceToGoal,
		const float DesiredSpeed,
		const FMassMovingAvoidanceParameters& AvoidanceParameters)
	{
		float NearStartFade = 1.0f;
		if (!bPreviousActionWasMove)
		{
			NearStartFade = FMath::Clamp(
				static_cast<float>((CurrentWorldSeconds - CurrentActionStartSeconds)
					/ FMath::Max(AvoidanceParameters.StartOfPathDuration, UE_KINDA_SMALL_NUMBER)),
				0.0f,
				1.0f);
		}

		float NearEndFade = 1.0f;
		if (bWillStandAtGoal)
		{
			const float ApproachDistance = FMath::Max(
				1.0f,
				AvoidanceParameters.EndOfPathDuration * DesiredSpeed);
			NearEndFade = FMath::Clamp(DistanceToGoal / ApproachDistance, 0.0f, 1.0f);
		}
		const float NearStartScaling = FMath::Lerp(
			AvoidanceParameters.StartOfPathAvoidanceScale,
			1.0f,
			NearStartFade);
		const float NearEndScaling = FMath::Lerp(
			AvoidanceParameters.EndOfPathAvoidanceScale,
			1.0f,
			NearEndFade);
		return NearStartScaling * NearEndScaling;
	}

	FVector CalculatePredictiveAvoidance(
		const FAgentSnapshot& Agent,
		const TConstArrayView<FAgentSnapshot> Agents,
		const TConstArrayView<FNearestCandidate> Candidates,
		const FPredictiveParameters& Parameters,
		const float PathFade,
		int32& OutColliderEvaluations)
	{
		OutColliderEvaluations = 0;
		if (!Agent.bParticipates || Agent.StableKey == 0u || Agent.Location.ContainsNaN()
			|| Agent.Velocity.ContainsNaN() || Agent.Radius <= 0.0f
			|| Parameters.PredictiveAvoidanceTime <= 0.0f
			|| Parameters.PredictiveAvoidanceDistance <= 0.0f
			|| Parameters.MaximumAcceleration <= 0.0f)
		{
			return FVector::ZeroVector;
		}

		FVector DesiredVelocity = Agent.Velocity.GetClampedToMaxSize(Parameters.MaximumSpeed);
		DesiredVelocity.Z = 0.0f;
		FVector SteeringForce = FVector::ZeroVector;
		const int32 ColliderCount = FMath::Min(Candidates.Num(), MaximumColliders);
		for (int32 CandidateIndex = 0; CandidateIndex < ColliderCount; ++CandidateIndex)
		{
			const FNearestCandidate& Candidate = Candidates[CandidateIndex];
			if (!Agents.IsValidIndex(Candidate.AgentIndex))
			{
				continue;
			}
			const FAgentSnapshot& Collider = Agents[Candidate.AgentIndex];
			if (!Collider.bParticipates || Collider.Radius <= 0.0f
				|| Collider.Location.ContainsNaN() || Collider.Velocity.ContainsNaN())
			{
				continue;
			}
			++OutColliderEvaluations;

			FVector RelativePosition = Agent.Location - Collider.Location;
			RelativePosition.Z = 0.0f;
			FVector ColliderVelocity = Collider.Velocity;
			ColliderVelocity.Z = 0.0f;
			const FVector RelativeVelocity = DesiredVelocity - ColliderVelocity;
			const double PredictiveRadius = Agent.Radius
				* Parameters.PredictiveAvoidanceRadiusScale;
			const double ClosestTime = ComputeClosestPointOfApproach(
				RelativePosition,
				RelativeVelocity,
				PredictiveRadius + Collider.Radius,
				Parameters.PredictiveAvoidanceTime);
			const FVector AvoidanceRelativePosition = RelativePosition
				+ RelativeVelocity * ClosestTime;
			const double AvoidanceDistance = AvoidanceRelativePosition.Size();
			const FVector AvoidanceNormal = AvoidanceDistance > UE_KINDA_SMALL_NUMBER
				? AvoidanceRelativePosition / AvoidanceDistance
				: FVector(Agent.StableKey < Collider.StableKey ? -1.0 : 1.0, 0.0, 0.0);
			const double Penetration = PredictiveRadius + Collider.Radius
				+ Parameters.PredictiveAvoidanceDistance - AvoidanceDistance;
			const double Magnitude = FMath::Square(FMath::Clamp(
				Penetration / Parameters.PredictiveAvoidanceDistance,
				0.0,
				1.0));
			const double TimeScale = 1.0 - ClosestTime / Parameters.PredictiveAvoidanceTime;
			const double StandingScale = Collider.bMoving
				? 1.0
				: Parameters.StandingObstacleAvoidanceScale;
			SteeringForce += AvoidanceNormal * Magnitude * TimeScale
				* Parameters.PredictiveAvoidanceStiffness * StandingScale;
		}

		SteeringForce *= FMath::Max(PathFade, 0.0f);
		if (SteeringForce.ContainsNaN())
		{
			return FVector::ZeroVector;
		}
		return SteeringForce.GetClampedToMaxSize(Parameters.MaximumAcceleration);
	}
}
