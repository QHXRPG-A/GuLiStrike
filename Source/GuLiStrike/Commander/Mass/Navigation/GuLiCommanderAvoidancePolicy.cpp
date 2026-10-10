// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderAvoidancePolicy.h"

#include "Avoidance/MassAvoidanceFragments.h"
#include "Commander/GuLiCommanderSimulationTiming.h"
#include "MassMovementFragments.h"
#include "Commander/Mass/GuLiMassMovementTuning.h"

namespace GuLiCommanderAvoidancePolicy
{
	bool SimilarReuseMotion(const FVector& Previous, const FVector& Current)
	{
		const double Speed = Previous.Size2D(), NextSpeed = Current.Size2D();
		return Speed > 1 && NextSpeed > 1 && FMath::Abs(NextSpeed - Speed) <= .1 * Speed
			&& FVector::DotProduct(Previous.GetSafeNormal2D(), Current.GetSafeNormal2D()) >= FMath::Cos(FMath::DegreesToRadians(15.0));
	}
	bool IsZeroCacheCurrent(const FZeroResultCache& Cache, const FAgentSnapshot& Agent,
		double Now, float Lifetime, uint32 OrderRevision, float Horizon, int32 CandidateLimit,
		float CellSize, FIntPoint MinimumCell, FIntPoint MaximumCell, uint64 MotionSignature)
	{
		const double Age = Now - Cache.CreatedAt;
		return FMath::IsFinite(Age) && Age >= 0 && Age < FMath::Clamp(Lifetime, 0.f, .3f)
			&& Agent.bParticipates && Agent.bMoving && Cache.Identity == Agent.StableKey
			&& Cache.OrderRevision == OrderRevision && Cache.Horizon == Horizon && Cache.Radius == Agent.Radius
			&& Cache.CellSize == CellSize && Cache.CandidateLimit == CandidateLimit
			&& Cache.MinimumCell == MinimumCell && Cache.MaximumCell == MaximumCell
			&& Cache.MotionSignature == MotionSignature
			&& SimilarReuseMotion(Cache.DesiredVelocity, Agent.DesiredVelocity) && SimilarReuseMotion(Cache.Velocity, Agent.Velocity)
			&& FVector::DistSquared(Cache.Location + Cache.Velocity * Age, Agent.Location)
				<= FMath::Square(.1 * Cache.Velocity.Size2D() * Age + 1);
	}
	namespace
	{
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

	FIntPoint MakeSpatialCell(const FVector& Location, const float CellSize)
	{
		if (Location.ContainsNaN() || !FMath::IsFinite(CellSize) || CellSize <= 0.0f)
		{
			return FIntPoint::ZeroValue;
		}
		return FIntPoint(
			FMath::FloorToInt(Location.X / CellSize),
			FMath::FloorToInt(Location.Y / CellSize));
	}

	void FAvoidanceSpatialGrid::Reset()
	{
		OrderedCells.Reset(); OrderedRows.Reset();
		TMap<FIntPoint, FAvoidanceBucket>::Reset();
	}

	void FAvoidanceSpatialGrid::RebuildOrderedCells()
	{
		OrderedCells.Reset(Num()); OrderedRows.Reset();
		for (const auto& Entry : *this) OrderedCells.Add({Entry.Key, &Entry.Value});
		OrderedCells.Sort([](const FCell& A, const FCell& B)
		{ return A.Key.X < B.Key.X || (A.Key.X == B.Key.X && A.Key.Y < B.Key.Y); });
		for (int32 Index = 0; Index < OrderedCells.Num(); ++Index)
		{
			const int32 X = OrderedCells[Index].Key.X;
			if (OrderedRows.IsEmpty() || OrderedRows.Last().X != X) OrderedRows.Add({X, Index, Index + 1});
			else OrderedRows.Last().End = Index + 1;
		}
	}

	void FAvoidanceSpatialGrid::GatherOrderedBuckets(const FIntPoint Minimum, const FIntPoint Maximum,
		TArray<const FAvoidanceBucket*, TInlineAllocator<64>>& Out, uint64& Comparisons) const
	{
		int32 Low = 0, High = OrderedRows.Num();
		while (Low < High)
		{
			const int32 Middle = Low + (High - Low) / 2; ++Comparisons;
			if (OrderedRows[Middle].X < Minimum.X) Low = Middle + 1; else High = Middle;
		}
		for (int32 RowIndex = Low; RowIndex < OrderedRows.Num(); ++RowIndex)
		{
			const FRow& Row = OrderedRows[RowIndex]; ++Comparisons;
			if (Row.X > Maximum.X) break;
			Low = Row.Begin; High = Row.End;
			while (Low < High)
			{
				const int32 Middle = Low + (High - Low) / 2; ++Comparisons;
				if (OrderedCells[Middle].Key.Y < Minimum.Y) Low = Middle + 1; else High = Middle;
			}
			for (int32 CellIndex = Low; CellIndex < Row.End; ++CellIndex)
			{
				const FCell& Cell = OrderedCells[CellIndex]; ++Comparisons;
				if (Cell.Key.Y > Maximum.Y) break;
				Out.Add(Cell.Bucket);
			}
		}
	}

	int32 BuildSpatialGrid(
		const TConstArrayView<FAgentSnapshot> Agents,
		FAvoidanceSpatialGrid& InOutGrid,
		const float CellSize)
	{
		InOutGrid.Reset();
		if (!FMath::IsFinite(CellSize) || CellSize <= 0.0f) return 0;
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
			const auto Min=MakeSpatialCell(Agent.Location-Extent,CellSize),Max=MakeSpatialCell(Agent.Location+Extent,CellSize);
			for (int32 X=Min.X; X<=Max.X; ++X) for (int32 Y=Min.Y; Y<=Max.Y; ++Y)
			{
				auto& Bucket=InOutGrid.FindOrAdd({X,Y}); Bucket.Add(AgentIndex);
				MaximumBucketOccupancy=FMath::Max(MaximumBucketOccupancy,Bucket.Num());
			}
		}
		InOutGrid.RebuildOrderedCells();
		return MaximumBucketOccupancy;
	}

	void FCandidateQueryScratch::BeginQuery(const int32 Count)
	{
		EnvironmentMarks.SetNumZeroed(Count, EAllowShrinking::No);
		if (++Generation == 0)
		{
			for (uint32& Mark : EnvironmentMarks) Mark = 0;
			Generation = 1;
		}
	}

	bool FCandidateQueryScratch::VisitEnvironment(const int32 Index)
	{
		if (EnvironmentMarks[Index] == Generation) return false;
		EnvironmentMarks[Index] = Generation;
		return true;
	}

	FCandidateQueryMetrics SelectNearestCandidates(
		const int32 AgentIndex,
		const TConstArrayView<FAgentSnapshot> Agents,
		const FAvoidanceSpatialGrid& Grid,
		FNearestCandidateList& OutCandidates,
		const float DetectionDistance,
		const float MaximumHeightDifference,
		const float CellSize,
		const float TimeHorizon,
		FCandidateQueryScratch* Scratch)
	{
		FCandidateQueryMetrics Metrics;
		OutCandidates.Reset();
		if (!Agents.IsValidIndex(AgentIndex)
			|| !FMath::IsFinite(DetectionDistance) || DetectionDistance <= 0.0f
			|| !FMath::IsFinite(MaximumHeightDifference) || MaximumHeightDifference < 0.0f
			|| !FMath::IsFinite(CellSize) || CellSize <= 0.0f
			|| !FMath::IsFinite(TimeHorizon) || TimeHorizon <= 0.0f)
		{
			return Metrics;
		}

		const FAgentSnapshot& Agent = Agents[AgentIndex];
		if (!Agent.bParticipates || Agent.StableKey == 0u || Agent.Location.ContainsNaN())
		{
			return Metrics;
		}
		const int32 CellRadius = FMath::CeilToInt(DetectionDistance / CellSize);
		const FIntPoint CenterCell = MakeSpatialCell(Agent.Location, CellSize);
		const double DistanceCutoffSquared = FMath::Square(static_cast<double>(DetectionDistance));
		FCandidateQueryScratch LocalScratch;
		FCandidateQueryScratch& QueryScratch = Scratch ? *Scratch : LocalScratch;
		QueryScratch.BeginQuery(Agents.Num());
		const int32 CandidateLimit = GuLiMassMovementTuning::GetCandidateLimit();
		FNearestCandidateList Environment;
		QueryScratch.Buckets.Reset();
		if (!Grid.OrderedRows.IsEmpty())
			Grid.GatherOrderedBuckets({CenterCell.X - CellRadius, CenterCell.Y - CellRadius},
				{CenterCell.X + CellRadius, CenterCell.Y + CellRadius}, QueryScratch.Buckets, Metrics.SparseComparisons);
		else
		{
			for (int32 CellX = CenterCell.X - CellRadius; CellX <= CenterCell.X + CellRadius; ++CellX)
				for (int32 CellY = CenterCell.Y - CellRadius; CellY <= CenterCell.Y + CellRadius; ++CellY)
			{
				++Metrics.CellLookups;
				if (const FAvoidanceBucket* Bucket = Grid.Find({CellX, CellY})) QueryScratch.Buckets.Add(Bucket);
			}
		}
		for (const FAvoidanceBucket* Bucket : QueryScratch.Buckets)
		{
			++Metrics.CellHits;
			for (const int32 OtherIndex : *Bucket)
			{
				if (OtherIndex == AgentIndex || !Agents.IsValidIndex(OtherIndex))
				{
					continue;
				}
				++Metrics.BucketEntriesVisited;
				const FAgentSnapshot& Other = Agents[OtherIndex];
				if (Other.bEnvironment && !QueryScratch.VisitEnvironment(OtherIndex)) { ++Metrics.EnvironmentDuplicates; continue; }
				++Metrics.UniqueVisits;
				if (!Other.bParticipates || Other.StableKey == 0u
					|| FMath::Abs(Agent.Location.Z - Other.Location.Z) > MaximumHeightDifference)
				{
					continue;
				}
				const double CenterDistanceSquared = FVector::DistSquaredXY(Agent.Location, Other.Location);
				const double CombinedRadius = Agent.Radius + Other.Radius;
				const double DistanceSquared = Other.bEnvironment
					? FMath::Square(FMath::Max(0., FMath::Sqrt(CenterDistanceSquared) - Other.Radius - Agent.Radius))
					: CenterDistanceSquared;
				if (!FMath::IsFinite(DistanceSquared) || DistanceSquared > DistanceCutoffSquared)
				{
					continue;
				}
				++Metrics.ExactCandidates;
				FNearestCandidate Candidate;
				Candidate.AgentIndex = OtherIndex;
				Candidate.DistanceSquared = DistanceSquared;
				Candidate.StableKey = Other.StableKey;
				Candidate.bOverlapping = CenterDistanceSquared < FMath::Square(CombinedRadius);

				auto& List=Other.bEnvironment ? Environment : OutCandidates;
				const int32 Limit=Other.bEnvironment ? FMath::Min(2, CandidateLimit) : CandidateLimit;
				int32 InsertIndex = 0;
				while (InsertIndex < List.Num()
					&& !(Candidate.DistanceSquared < List[InsertIndex].DistanceSquared
						|| (Candidate.DistanceSquared == List[InsertIndex].DistanceSquared && Candidate.StableKey < List[InsertIndex].StableKey)))
				{
					++InsertIndex;
				}
				if (InsertIndex < Limit)
				{
					if (List.Num() == Limit) List.Pop(EAllowShrinking::No);
					List.Insert(Candidate, InsertIndex);
				}
			}
		}
		const int32 Reserved=FMath::Min(CandidateLimit, FMath::Min(2,Environment.Num()));
		for (int32 I=Reserved-1; I>=0; --I)
		{ if (OutCandidates.Num() == CandidateLimit) OutCandidates.Pop(EAllowShrinking::No); OutCandidates.Insert(Environment[I],0); }
		if (OutCandidates.Num()>CandidateLimit) OutCandidates.SetNum(CandidateLimit,EAllowShrinking::No);
		Metrics.RetainedCandidates=OutCandidates.Num(); Metrics.ConsumedCandidates=OutCandidates.Num();
		return Metrics;
	}

	FPredictiveParameters MakePredictiveParameters(
		const FMassMovingAvoidanceParameters& AvoidanceParameters,
		const FMassMovementParameters& MovementParameters)
	{
		FPredictiveParameters Result;
		Result.PredictiveAvoidanceTime = GuLiMassMovementTuning::GetLookaheadSeconds();
		Result.PredictiveAvoidanceRadiusScale = 1.0f;
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
		return FMath::Max(0.5f, NearStartScaling * NearEndScaling);
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

		FVector DesiredVelocity = (Agent.DesiredVelocity.IsNearlyZero() ? Agent.Velocity : Agent.DesiredVelocity)
			.GetClampedToMaxSize(Parameters.MaximumSpeed);
		DesiredVelocity.Z = 0.0f;
		FVector SteeringForce = FVector::ZeroVector;
		FVector PassingAcceleration = FVector::ZeroVector;
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
			FVector ColliderVelocity = Collider.bMoving && !Collider.DesiredVelocity.IsNearlyZero()
				? Collider.DesiredVelocity : Collider.Velocity;
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
			// A shared right-hand convention breaks symmetric head-on deadlocks.
			if (!Collider.bEnvironment && DesiredVelocity.SizeSquared2D() > 1.0
				&& FVector::DotProduct(DesiredVelocity.GetSafeNormal2D(), ColliderVelocity.GetSafeNormal2D()) < -0.5)
			{
				const FVector Right = FVector::CrossProduct(FVector::UpVector, DesiredVelocity.GetSafeNormal2D());
				PassingAcceleration += Right * (Parameters.MaximumSpeed * 0.25f
					/ GuLiCommanderSimulationTiming::StepSeconds) * Magnitude * TimeScale;
			}
		}

		SteeringForce += PassingAcceleration.GetClampedToMaxSize(Parameters.MaximumSpeed * 0.25f
			/ GuLiCommanderSimulationTiming::StepSeconds);
		SteeringForce *= FMath::Max(PathFade, 0.0f);
		if (SteeringForce.ContainsNaN())
		{
			return FVector::ZeroVector;
		}
		return SteeringForce.GetClampedToMaxSize(Parameters.MaximumAcceleration);
	}
}
