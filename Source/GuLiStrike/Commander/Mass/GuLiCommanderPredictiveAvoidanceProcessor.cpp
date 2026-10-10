#include "Commander/Mass/GuLiCommanderPredictiveAvoidanceProcessor.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
// Copyright Epic Games, Inc. All Rights Reserved.


#include "Avoidance/MassAvoidanceFragments.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/GuLiMassMovementTuning.h"
#include "Commander/Mass/Navigation/GuLiCommanderAvoidancePolicy.h"
#include "Engine/World.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassLODTypes.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/Crc.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiCommanderPredictiveAvoidanceProcessor)

namespace GuLiCommanderPredictiveAvoidancePrivate
{
	using namespace GuLiCommanderAvoidancePolicy;

	// Compact snapshots keep every per-neighbor Mass fragment lookup out of the hot loops.
	struct FProcessorAgent
	{
		FMassEntityHandle Entity;
		FAgentSnapshot Agent;
		FPredictiveParameters Parameters;
		FNearestCandidateList Candidates;
		FVector SolvedOutput = FVector::ZeroVector;
		uint32 SoldierId = 0u;
		uint32 OrderRevision = 0u;
		uint32 LastProcessedOrderRevision = 0u;
		uint64 SolveSequence = 0u;
		uint64 CandidateCount = 0u;
		float PathFade = 1.0f;
		int32 ColliderEvaluations = 0;
		int32 PolicyIndex = INDEX_NONE;
		bool bReceiver = false;
		bool bShouldReceive = false;
		bool bForced = false;
		bool bSolved = false;
		bool bCached = false;
	};
	struct FWorkspace
	{
		TArray<FProcessorAgent> Agents;
		TArray<FAgentSnapshot> PolicyAgents;
		TMap<FMassEntityHandle, int32> AgentIndexByEntity;
		FAvoidanceSpatialGrid SpatialGrid;
		FCandidateQueryScratch QueryScratch;
		TMap<FMassEntityHandle, FZeroResultCache> ZeroResults;
		TMap<FIntPoint, uint64> CellMotionSignatures;
		TArray<uint64> AgentMotionHashes;
	};

	uint64 MotionHash(const FAgentSnapshot& A)
	{
		uint64 H = A.StableKey * 1099511628211ull;
		auto Add = [&H](const auto& V) { H = (H ^ FCrc::MemCrc32(&V, sizeof(V))) * 1099511628211ull; };
		Add(A.Velocity); Add(A.DesiredVelocity); Add(A.Radius); Add(A.bMoving); Add(A.bParticipates);
		if (A.bEnvironment) Add(A.Location);
		return H;
	}
	uint64 RegionSignature(const FAgentSnapshot& A, FIntPoint Minimum, FIntPoint Maximum,
		float CellSize, const TMap<FIntPoint, uint64>& Cells)
	{
		const FIntPoint OwnCell = MakeSpatialCell(A.Location, CellSize);
		const uint64 OwnHash = MotionHash(A);
		uint64 Signature = 1469598103934665603ull;
		for (int32 X = Minimum.X; X <= Maximum.X; ++X) for (int32 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
		{
			const uint64* Found = Cells.Find({X, Y});
			uint64 Value = Found ? *Found : 0;
			if (Found && OwnCell == FIntPoint(X, Y)) Value -= OwnHash;
			Signature = (Signature ^ Value) * 1099511628211ull;
		}
		return Signature;
	}
	// This checks ALL nearby entries, not the six prediction candidates. It covers
	// the next 10 Hz interval and never omits an imminent unit/environment contact.
	bool IsNearRegionSafe(const FAgentSnapshot& A, TConstArrayView<FAgentSnapshot> Agents,
		const FAvoidanceSpatialGrid& Grid, float CellSize, float MaximumRadius, float MaximumSpeed,
		float Padding, FCandidateQueryScratch& Scratch, uint64& Visits, uint64& Checks)
	{
		const FVector Velocity = A.DesiredVelocity.IsNearlyZero() ? A.Velocity : A.DesiredVelocity;
		const float Reach = A.Radius + MaximumRadius + Padding + (A.MaximumSpeed + MaximumSpeed) * .1f;
		const int32 Range = FMath::CeilToInt(Reach / CellSize);
		const FIntPoint Cell = MakeSpatialCell(A.Location, CellSize);
		Scratch.BeginQuery(Agents.Num()); Scratch.Buckets.Reset(); uint64 Comparisons = 0;
		Grid.GatherOrderedBuckets({Cell.X - Range, Cell.Y - Range}, {Cell.X + Range, Cell.Y + Range}, Scratch.Buckets, Comparisons);
		for (const auto* Bucket : Scratch.Buckets) for (const int32 Index : *Bucket)
		{
			const auto& B = Agents[Index];
			if (B.StableKey == A.StableKey || !B.bParticipates) continue;
			if (B.bEnvironment && !Scratch.VisitEnvironment(Index)) continue;
			++Visits;
			if (FMath::Abs(A.Location.Z - B.Location.Z) > MaximumHeightDifferenceCentimeters) continue;
			const FVector P = (A.Location - B.Location) * FVector(1, 1, 0);
			const double R = A.Radius + B.Radius + Padding;
			if (P.SizeSquared2D() > FMath::Square(R + (A.MaximumSpeed + B.MaximumSpeed) * .1)) continue;
			const FVector OtherVelocity = B.bMoving && !B.DesiredVelocity.IsNearlyZero() ? B.DesiredVelocity : B.Velocity;
			if (!B.bEnvironment && !SimilarReuseMotion(Velocity, OtherVelocity)) return false;
			const FVector V = (Velocity - OtherVelocity) * FVector(1, 1, 0);
			++Checks;
			const double ClosestTime = V.SizeSquared() > UE_SMALL_NUMBER
				? FMath::Clamp(-FVector::DotProduct(P, V) / V.SizeSquared(), 0.0, .1) : 0;
			if ((P + V * ClosestTime).SizeSquared2D() <= R * R) return false;
		}
		return true;
	}
}

UGuLiCommanderPredictiveAvoidanceProcessor::~UGuLiCommanderPredictiveAvoidanceProcessor() = default;

UGuLiCommanderPredictiveAvoidanceProcessor::UGuLiCommanderPredictiveAvoidanceProcessor()
	: ObstacleQuery(*this)
	, ReceiverQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true; // Snapshot publication touches the world registry; solver reads immutable values only.
	ExecutionFlags = static_cast<int32>(
		EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Avoidance;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::LOD);
}

void UGuLiCommanderPredictiveAvoidanceProcessor::ConfigureQueries(
	const TSharedRef<FMassEntityManager>& EntityManager)
{
	ObstacleQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	ObstacleQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	ObstacleQuery.AddRequirement<FMassVelocityFragment>(
		EMassFragmentAccess::ReadOnly,
		EMassFragmentPresence::Optional);
	ObstacleQuery.AddRequirement<FMassMoveTargetFragment>(
		EMassFragmentAccess::ReadOnly,
		EMassFragmentPresence::Optional);
	ObstacleQuery.AddRequirement<FGuLiMassIdentityFragment>(
		EMassFragmentAccess::ReadOnly,
		EMassFragmentPresence::Optional);
	ObstacleQuery.AddRequirement<FGuLiMassHealthFragment>(
		EMassFragmentAccess::ReadOnly,
		EMassFragmentPresence::Optional);
	ObstacleQuery.AddTagRequirement<FGuLiMassAvoidanceParticipantTag>(EMassFragmentPresence::All);
	ObstacleQuery.AddTagRequirement<FGuLiServerAuthorityMassTag>(EMassFragmentPresence::All);

	ReceiverQuery.AddRequirement<FGuLiMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
	ReceiverQuery.AddRequirement<FGuLiMassHealthFragment>(EMassFragmentAccess::ReadOnly);
	ReceiverQuery.AddRequirement<FGuLiMassOrderFragment>(EMassFragmentAccess::ReadOnly);
	ReceiverQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly);
	ReceiverQuery.AddRequirement<FGuLiMassAvoidanceOutputFragment>(EMassFragmentAccess::ReadWrite);
	ReceiverQuery.AddRequirement<FGuLiMassAvoidanceStateFragment>(EMassFragmentAccess::ReadWrite);
	ReceiverQuery.AddConstSharedRequirement<FMassMovingAvoidanceParameters>(EMassFragmentPresence::All);
	ReceiverQuery.AddConstSharedRequirement<FMassMovementParameters>(EMassFragmentPresence::All);
	ReceiverQuery.AddTagRequirement<FGuLiMassAvoidanceParticipantTag>(EMassFragmentPresence::All);
	ReceiverQuery.AddTagRequirement<FGuLiServerAuthorityMassTag>(EMassFragmentPresence::All);
}

void UGuLiCommanderPredictiveAvoidanceProcessor::InitializeInternal(
	UObject& Owner,
	const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::InitializeInternal(Owner, EntityManager);
	World = Owner.GetWorld();
	Workspace = MakeUnique<GuLiCommanderPredictiveAvoidancePrivate::FWorkspace>();
	FixedStepAccumulatorSeconds = 0.0;
	NextSolveSequence = 0u;
}

void UGuLiCommanderPredictiveAvoidanceProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	using namespace GuLiCommanderAvoidancePolicy;
	using namespace GuLiCommanderPredictiveAvoidancePrivate;

	if (!World)
	{
		return;
	}
	const FPhaseAdvanceResult PhaseAdvance = AdvancePhases(
		Context.GetDeltaTimeSeconds(),
		FixedStepAccumulatorSeconds,
		NextSolveSequence);
	if (PhaseAdvance.PhaseMask == 0u)
	{
		return;
	}

	FGuLiPerformanceScope TotalTiming(World, TEXT("Avoidance.TotalMs"));
	auto& Agents = Workspace->Agents;
	auto& PolicyAgents = Workspace->PolicyAgents;
	auto& AgentIndexByEntity = Workspace->AgentIndexByEntity;
	auto& SpatialGrid = Workspace->SpatialGrid;
	Agents.Reset();
	PolicyAgents.Reset();
	AgentIndexByEntity.Reset();
	float GridCellSize = SpatialCellSizeCentimeters;
	float MaximumAgentRadius = 0.0f;
	float MaximumAgentSpeed = 0.0f;
	int32 MaximumBucketOccupancy = 0;
	TSharedPtr<const FSharedAvoidanceGeometry> SharedGeometry;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_PredictiveAvoidanceBuildGrid);
		FGuLiPerformanceScope Timing(World, TEXT("Avoidance.GridMs"));
		ObstacleQuery.ForEachEntityChunk(Context, [&Agents, &AgentIndexByEntity](FMassExecutionContext& ChunkContext)
		{
			const TConstArrayView<FTransformFragment> Transforms =
				ChunkContext.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FAgentRadiusFragment> Radii =
				ChunkContext.GetFragmentView<FAgentRadiusFragment>();
			const TConstArrayView<FMassVelocityFragment> Velocities =
				ChunkContext.GetFragmentView<FMassVelocityFragment>();
			const TConstArrayView<FMassMoveTargetFragment> MoveTargets =
				ChunkContext.GetFragmentView<FMassMoveTargetFragment>();
			const TConstArrayView<FGuLiMassIdentityFragment> Identities =
				ChunkContext.GetFragmentView<FGuLiMassIdentityFragment>();
			const TConstArrayView<FGuLiMassHealthFragment> Health =
				ChunkContext.GetFragmentView<FGuLiMassHealthFragment>();

			for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
			{
				FProcessorAgent& Entry = Agents.AddDefaulted_GetRef();
				Entry.Entity = ChunkContext.GetEntity(It);
				Entry.SoldierId = Identities.IsEmpty() ? 0u : Identities[It].SoldierId.Value;
				Entry.Agent.StableKey = Entry.Entity.AsNumber();
				Entry.Agent.Location = Transforms[It].GetTransform().GetLocation();
				Entry.Agent.Velocity = Velocities.IsEmpty()
					? FVector::ZeroVector
					: Velocities[It].Value;
				Entry.Agent.Radius = Radii[It].Radius;
				Entry.Agent.bParticipates = Health.IsEmpty() || (!Health[It].bDead && !Health[It].bPhased);
				Entry.Agent.bMoving = MoveTargets.IsEmpty()
					|| MoveTargets[It].GetCurrentAction() == EMassMovementAction::Move;
				Entry.Agent.DesiredVelocity = !MoveTargets.IsEmpty() && Entry.Agent.bMoving
					? MoveTargets[It].Forward * MoveTargets[It].DesiredSpeed.Get() : FVector::ZeroVector;
				Entry.Agent.MaximumSpeed = FMath::Max(Entry.Agent.Velocity.Size2D(), Entry.Agent.DesiredVelocity.Size2D());
				AgentIndexByEntity.Add(Entry.Entity, Agents.Num() - 1);
			}
		});

		if (auto* Registry=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
		{
			const auto Snapshot=Registry->GetSnapshot();
			for (const auto& Obstacle : Snapshot->Obstacles)
			{
				auto& Entry=Agents.AddDefaulted_GetRef();
				Entry.Agent.StableKey=(uint64(1)<<63)|Obstacle.Handle.Value;
				Entry.Agent.Location=Obstacle.Location; Entry.Agent.Radius=Obstacle.RadiusCentimeters;
				Entry.Agent.bParticipates=true; Entry.Agent.bEnvironment=true;
			}
		}

		PolicyAgents.Reserve(Agents.Num());
		for (const FProcessorAgent& Entry : Agents)
		{
			PolicyAgents.Add(Entry.Agent);
			if (Entry.Agent.bParticipates && !Entry.Agent.bEnvironment)
			{
				MaximumAgentRadius = FMath::Max(MaximumAgentRadius, Entry.Agent.Radius);
				MaximumAgentSpeed = FMath::Max(MaximumAgentSpeed, Entry.Agent.MaximumSpeed);
			}
		}
		GridCellSize = FMath::Max(SpatialCellSizeCentimeters, MaximumAgentRadius * 2.0f);
		if (auto* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
			SharedGeometry = Authority->AcquireAvoidanceGeometry(PolicyAgents);
		if (SharedGeometry)
		{
			GridCellSize = SharedGeometry->CellSize;
			MaximumBucketOccupancy = SharedGeometry->MaximumBucketOccupancy;
			// A reused immutable version can have a different iteration order.
			for (auto& Entry : Agents) Entry.PolicyIndex = SharedGeometry->IndexByIdentity.FindChecked(Entry.Agent.StableKey);
			for (const auto& Entry : Agents) PolicyAgents[Entry.PolicyIndex] = Entry.Agent;
		}
		else
		{
			MaximumBucketOccupancy = BuildSpatialGrid(PolicyAgents, SpatialGrid, GridCellSize);
			for (int32 I = 0; I < Agents.Num(); ++I) Agents[I].PolicyIndex = I;
		}
	}
	const FAvoidanceSpatialGrid& QueryGrid = SharedGeometry ? SharedGeometry->Grid : SpatialGrid;

	if (Agents.IsEmpty())
	{
		return;
	}

	const double CurrentWorldSeconds = World->GetTimeSeconds();
	ReceiverQuery.ForEachEntityChunk(
		Context,
		[&Agents, &AgentIndexByEntity, CurrentWorldSeconds](FMassExecutionContext& ChunkContext)
		{
			const TConstArrayView<FGuLiMassIdentityFragment> Identities =
				ChunkContext.GetFragmentView<FGuLiMassIdentityFragment>();
			const TConstArrayView<FGuLiMassHealthFragment> Health =
				ChunkContext.GetFragmentView<FGuLiMassHealthFragment>();
			const TConstArrayView<FGuLiMassOrderFragment> Orders =
				ChunkContext.GetFragmentView<FGuLiMassOrderFragment>();
			const TConstArrayView<FMassMoveTargetFragment> MoveTargets =
				ChunkContext.GetFragmentView<FMassMoveTargetFragment>();
			const TArrayView<FGuLiMassAvoidanceStateFragment> States =
				ChunkContext.GetMutableFragmentView<FGuLiMassAvoidanceStateFragment>();
			const FMassMovingAvoidanceParameters& AvoidanceParameters =
				ChunkContext.GetConstSharedFragment<FMassMovingAvoidanceParameters>();
			const FMassMovementParameters& MovementParameters =
				ChunkContext.GetConstSharedFragment<FMassMovementParameters>();

			for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
			{
				const int32* AgentIndex = AgentIndexByEntity.Find(ChunkContext.GetEntity(It));
				if (!AgentIndex || !Agents.IsValidIndex(*AgentIndex))
				{
					continue;
				}
				FProcessorAgent& Entry = Agents[*AgentIndex];
				Entry.bReceiver = true;
				Entry.SoldierId = Identities[It].SoldierId.Value;
				Entry.OrderRevision = Orders[It].OrderRevision;
				Entry.LastProcessedOrderRevision = States[It].LastProcessedOrderRevision;
				Entry.bShouldReceive = !Health[It].bDead && !Health[It].bPhased
					&& Entry.SoldierId != 0u
					&& Orders[It].bHasMoveTarget
					&& MoveTargets[It].GetCurrentAction() == EMassMovementAction::Move;
				Entry.Agent.bMoving = Entry.bShouldReceive;
				Entry.Parameters = MakePredictiveParameters(AvoidanceParameters, MovementParameters);
				// Initial population may share parameters with smaller units; use the actual fragment radius.
				Entry.Parameters.PredictiveAvoidanceDistance = Entry.Agent.Radius * 0.35f;
				Entry.Agent.MaximumSpeed = FMath::Max(Entry.Agent.MaximumSpeed, Entry.Parameters.MaximumSpeed);
				Entry.PathFade = CalculatePathFade(
					CurrentWorldSeconds,
					MoveTargets[It].GetCurrentActionStartTime(),
					MoveTargets[It].GetPreviousAction() == EMassMovementAction::Move,
					MoveTargets[It].IntentAtGoal == EMassMovementAction::Stand,
					MoveTargets[It].DistanceToGoal,
					MoveTargets[It].DesiredSpeed.Get(),
					AvoidanceParameters);
			}
		});

	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		const FProcessorAgent& Entry = Agents[Index];
		PolicyAgents[Entry.PolicyIndex] = Entry.Agent;
		if (Entry.Agent.bParticipates && !Entry.Agent.bEnvironment)
			MaximumAgentSpeed = FMath::Max(MaximumAgentSpeed, Entry.Agent.MaximumSpeed);
	}
	for (auto It = Workspace->ZeroResults.CreateIterator(); It; ++It)
		if (!AgentIndexByEntity.Contains(It.Key())) It.RemoveCurrent();
	Workspace->CellMotionSignatures.Reset();
	{
		FGuLiPerformanceScope Timing(World, TEXT("Avoidance.CachePrepareMs"));
		Workspace->AgentMotionHashes.SetNumUninitialized(PolicyAgents.Num());
		for (int32 I=0; I<PolicyAgents.Num(); ++I) Workspace->AgentMotionHashes[I]=MotionHash(PolicyAgents[I]);
		for (const auto& Cell : QueryGrid.OrderedCells)
		{
			uint64 Signature = 0;
			for (int32 Index : *Cell.Bucket) Signature += Workspace->AgentMotionHashes[Index];
			Workspace->CellMotionSignatures.Add(Cell.Key, Signature);
		}
	}
	uint64 CacheHits = 0, CacheInvalidations = 0, CacheExpirations = 0, GuardVisits = 0, GuardChecks = 0, QueryTrends = 0;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_PredictiveAvoidanceCandidateQuery);
		FGuLiPerformanceScope Timing(World, TEXT("Avoidance.QueryMs"));
		uint64 Visits = 0, Exact = 0, Solves = 0, Forced = 0;
		uint64 CellLookups=0,CellHits=0,SparseComparisons=0,Duplicates=0,Unique=0,Retained=0,Consumed=0,Verified=0,Mismatches=0;
		uint64 RadiusBins[3]={},SpeedBins[3]={}; double QueryReachSum=0;
		for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
		{
			FProcessorAgent& Entry = Agents[AgentIndex];
			if (!Entry.bShouldReceive && Workspace->ZeroResults.Remove(Entry.Entity)) ++CacheInvalidations;
			if (!Entry.bReceiver || !ShouldSolve(
				PhaseAdvance.PhaseMask,
				Entry.SoldierId,
				Entry.OrderRevision,
				Entry.LastProcessedOrderRevision,
				Entry.bShouldReceive))
			{
				continue;
			}

			Entry.bForced = Entry.OrderRevision != Entry.LastProcessedOrderRevision;
			const uint32 Phase = ResolveSolvePhase(Entry.SoldierId);
			const bool bScheduledPhase = (PhaseAdvance.PhaseMask & (1u << Phase)) != 0u;
			Entry.SolveSequence = bScheduledPhase
				? PhaseAdvance.SequenceByPhase[Phase]
				: PhaseAdvance.LastSequence;
			const float Reach = FMath::Max(DetectionDistanceCentimeters, Entry.Agent.Radius + MaximumAgentRadius
				+ (Entry.Agent.MaximumSpeed + MaximumAgentSpeed) * Entry.Parameters.PredictiveAvoidanceTime
				+ Entry.Parameters.PredictiveAvoidanceDistance);
			{
				if (const auto* Cached = Workspace->ZeroResults.Find(Entry.Entity))
				{
					const double Age = CurrentWorldSeconds - Cached->CreatedAt;
					const bool bExpired = Age < 0 || Age >= GuLiMassMovementTuning::GetCacheLifetimeSeconds();
					const int32 Range = FMath::CeilToInt(Reach / GridCellSize);
					const FIntPoint Cell = MakeSpatialCell(Entry.Agent.Location, GridCellSize);
					const bool bCurrent = !Entry.bForced && IsZeroCacheCurrent(*Cached, Entry.Agent, CurrentWorldSeconds,
						GuLiMassMovementTuning::GetCacheLifetimeSeconds(), Entry.OrderRevision, Entry.Parameters.PredictiveAvoidanceTime,
						GuLiMassMovementTuning::GetCandidateLimit(), GridCellSize, {Cell.X - Range, Cell.Y - Range}, {Cell.X + Range, Cell.Y + Range},
						RegionSignature(Entry.Agent, Cached->MinimumCell, Cached->MaximumCell, GridCellSize, Workspace->CellMotionSignatures))
						&& IsNearRegionSafe(Entry.Agent, PolicyAgents, QueryGrid, GridCellSize, MaximumAgentRadius, MaximumAgentSpeed,
							Entry.Parameters.PredictiveAvoidanceDistance, Workspace->QueryScratch, GuardVisits, GuardChecks);
					if (bCurrent)
					{
						Entry.bSolved = true; Entry.bCached = true; ++CacheHits;
						continue;
					}
					Workspace->ZeroResults.Remove(Entry.Entity); ++CacheInvalidations; CacheExpirations += bExpired ? 1 : 0;
				}
			}
			const FCandidateQueryMetrics Metrics = SelectNearestCandidates(
				Entry.PolicyIndex,
				PolicyAgents,
				QueryGrid,
				Entry.Candidates,
				FMath::Max(DetectionDistanceCentimeters, Entry.Agent.Radius + MaximumAgentRadius
					+ (Entry.Agent.MaximumSpeed + MaximumAgentSpeed) * Entry.Parameters.PredictiveAvoidanceTime
					+ Entry.Parameters.PredictiveAvoidanceDistance),
				MaximumHeightDifferenceCentimeters, GridCellSize, Entry.Parameters.PredictiveAvoidanceTime,
				&Workspace->QueryScratch);
			Visits += Metrics.BucketEntriesVisited; Exact += Metrics.ExactCandidates; ++Solves; Forced += Entry.bForced ? 1 : 0;
			QueryTrends += Metrics.TrendEvaluations;
			CellLookups+=Metrics.CellLookups; CellHits+=Metrics.CellHits; Duplicates+=Metrics.EnvironmentDuplicates; Unique+=Metrics.UniqueVisits;
			SparseComparisons+=Metrics.SparseComparisons;
			Retained+=Metrics.RetainedCandidates; Consumed+=Metrics.ConsumedCandidates; Verified+=Metrics.VerifiedPrefixes; Mismatches+=Metrics.PrefixMismatches;
			++RadiusBins[Entry.Agent.Radius<=150.f ? 0 : Entry.Agent.Radius<=300.f ? 1 : 2];
			++SpeedBins[Entry.Agent.MaximumSpeed<=800.f ? 0 : Entry.Agent.MaximumSpeed<=1600.f ? 1 : 2];
			QueryReachSum+=FMath::Max(DetectionDistanceCentimeters,Entry.Agent.Radius+MaximumAgentRadius
				+(Entry.Agent.MaximumSpeed+MaximumAgentSpeed)*Entry.Parameters.PredictiveAvoidanceTime+Entry.Parameters.PredictiveAvoidanceDistance);
			Entry.CandidateCount = Metrics.ExactCandidates;
			Entry.bSolved = true;
		}
		if (auto* Capture = World->GetSubsystem<UGuLiPerformanceSubsystem>())
		{
			Capture->Record(TEXT("Avoidance.BucketVisits"), double(Visits));
			Capture->Record(TEXT("Avoidance.ExactCandidates"), double(Exact));
			Capture->Record(TEXT("Avoidance.Solves"), double(Solves));
			Capture->Record(TEXT("Avoidance.ActualQueries"), double(Solves));
			Capture->Record(TEXT("Avoidance.Forced"), double(Forced));
			Capture->Record(TEXT("Avoidance.CellLookups"),double(CellLookups)); Capture->Record(TEXT("Avoidance.CellHits"),double(CellHits));
			Capture->Record(TEXT("Avoidance.SparseComparisons"),double(SparseComparisons));
			Capture->Record(TEXT("Avoidance.EnvironmentDuplicates"),double(Duplicates)); Capture->Record(TEXT("Avoidance.UniqueVisits"),double(Unique));
			Capture->Record(TEXT("Avoidance.Retained"),double(Retained)); Capture->Record(TEXT("Avoidance.Consumed"),double(Consumed));
			Capture->Record(TEXT("Avoidance.VerifiedPrefixes"),double(Verified)); Capture->Record(TEXT("Avoidance.PrefixMismatches"),double(Mismatches));
			Capture->Record(TEXT("Avoidance.QueryReachSumCm"),QueryReachSum);
			Capture->Record(TEXT("Avoidance.RadiusLE150"),double(RadiusBins[0])); Capture->Record(TEXT("Avoidance.RadiusLE300"),double(RadiusBins[1])); Capture->Record(TEXT("Avoidance.RadiusGT300"),double(RadiusBins[2]));
			Capture->Record(TEXT("Avoidance.SpeedLE800"),double(SpeedBins[0])); Capture->Record(TEXT("Avoidance.SpeedLE1600"),double(SpeedBins[1])); Capture->Record(TEXT("Avoidance.SpeedGT1600"),double(SpeedBins[2]));
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_PredictiveAvoidanceSolve);
		FGuLiPerformanceScope Timing(World, TEXT("Avoidance.SolveMs"));
		for (FProcessorAgent& Entry : Agents)
		{
			if (!Entry.bSolved || Entry.bCached)
			{
				continue;
			}
			Entry.SolvedOutput = CalculatePredictiveAvoidance(
				Entry.Agent,
				PolicyAgents,
				Entry.Candidates,
				Entry.Parameters,
				Entry.PathFade,
				Entry.ColliderEvaluations);
			if (Entry.SolvedOutput.IsZero() && Entry.Agent.DesiredVelocity.SizeSquared2D() > 1)
			{
				bool bSafe = true;
				const FVector OwnVelocity = Entry.Agent.DesiredVelocity;
				for (const auto& Candidate : Entry.Candidates)
				{
					const auto& Other = PolicyAgents[Candidate.AgentIndex];
					const FVector OtherVelocity = Other.DesiredVelocity.IsNearlyZero() ? Other.Velocity : Other.DesiredVelocity;
					const FVector P = (Entry.Agent.Location - Other.Location) * FVector(1, 1, 0);
					const double Closing = -FVector::DotProduct(P.GetSafeNormal2D(), OwnVelocity - OtherVelocity);
					if (Other.bEnvironment || Candidate.bOverlapping || !SimilarReuseMotion(OwnVelocity, OtherVelocity)
						|| Closing > .1 * OwnVelocity.Size2D()) { bSafe = false; break; }
				}
				if (bSafe && IsNearRegionSafe(Entry.Agent, PolicyAgents, QueryGrid, GridCellSize, MaximumAgentRadius, MaximumAgentSpeed,
					Entry.Parameters.PredictiveAvoidanceDistance, Workspace->QueryScratch, GuardVisits, GuardChecks))
				{
					const float Reach = FMath::Max(DetectionDistanceCentimeters, Entry.Agent.Radius + MaximumAgentRadius
						+ (Entry.Agent.MaximumSpeed + MaximumAgentSpeed) * Entry.Parameters.PredictiveAvoidanceTime
						+ Entry.Parameters.PredictiveAvoidanceDistance);
					const int32 Range = FMath::CeilToInt(Reach / GridCellSize);
					const FIntPoint Cell = MakeSpatialCell(Entry.Agent.Location, GridCellSize);
					auto& Cache = Workspace->ZeroResults.FindOrAdd(Entry.Entity);
					Cache.Location = Entry.Agent.Location; Cache.Velocity = Entry.Agent.Velocity; Cache.DesiredVelocity = Entry.Agent.DesiredVelocity;
					Cache.Identity = Entry.Agent.StableKey;
					Cache.OrderRevision = Entry.OrderRevision; Cache.CreatedAt = CurrentWorldSeconds;
					Cache.Horizon = Entry.Parameters.PredictiveAvoidanceTime; Cache.CandidateLimit = GuLiMassMovementTuning::GetCandidateLimit();
					Cache.Radius = Entry.Agent.Radius; Cache.CellSize = GridCellSize;
					Cache.MinimumCell = {Cell.X - Range, Cell.Y - Range}; Cache.MaximumCell = {Cell.X + Range, Cell.Y + Range};
					Cache.MotionSignature = RegionSignature(Entry.Agent, Cache.MinimumCell, Cache.MaximumCell,
						GridCellSize, Workspace->CellMotionSignatures);
				}
			}
		}
	}
	if (auto* Capture = World->GetSubsystem<UGuLiPerformanceSubsystem>())
	{
		uint64 SolveTrends = 0; for (const auto& Entry : Agents) SolveTrends += Entry.ColliderEvaluations;
		Capture->Record(TEXT("Avoidance.TrendEvaluations"), double(QueryTrends + SolveTrends));
		Capture->Record(TEXT("Avoidance.QueryTrendEvaluations"), double(QueryTrends));
		Capture->Record(TEXT("Avoidance.CacheHits"), double(CacheHits));
		Capture->Record(TEXT("Avoidance.CacheInvalidations"), double(CacheInvalidations));
		Capture->Record(TEXT("Avoidance.CacheExpirations"), double(CacheExpirations));
		Capture->Record(TEXT("Avoidance.CacheGuardVisits"), double(GuardVisits));
		Capture->Record(TEXT("Avoidance.CacheGuardChecks"), double(GuardChecks));
	}

	FGuLiPerformanceScope SubmitTiming(World, TEXT("Avoidance.SubmitMs"));
	ReceiverQuery.ForEachEntityChunk(
		Context,
		[&Agents, &AgentIndexByEntity, MaximumBucketOccupancy](FMassExecutionContext& ChunkContext)
		{
			const TArrayView<FGuLiMassAvoidanceOutputFragment> Outputs =
				ChunkContext.GetMutableFragmentView<FGuLiMassAvoidanceOutputFragment>();
			const TArrayView<FGuLiMassAvoidanceStateFragment> States =
				ChunkContext.GetMutableFragmentView<FGuLiMassAvoidanceStateFragment>();
			for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
			{
				const int32* AgentIndex = AgentIndexByEntity.Find(ChunkContext.GetEntity(It));
				if (!AgentIndex || !Agents.IsValidIndex(*AgentIndex))
				{
					Outputs[It].Value = FVector::ZeroVector;
					continue;
				}
				const FProcessorAgent& Entry = Agents[*AgentIndex];
				FGuLiMassAvoidanceStateFragment& State = States[It];
				if (!Entry.bShouldReceive)
				{
					Outputs[It].Value = FVector::ZeroVector;
					State.LastProcessedOrderRevision = Entry.OrderRevision;
					continue;
				}
				if (!Entry.bSolved)
				{
					continue;
				}

				Outputs[It].Value = Entry.SolvedOutput;
				State.LastProcessedOrderRevision = Entry.OrderRevision;
				State.LastSolveSequence = Entry.SolveSequence;
				++State.SolveCount;
				State.ForcedSolveCount += Entry.bForced ? 1u : 0u;
				State.CandidateCount += Entry.CandidateCount;
				State.ColliderEvaluationCount += static_cast<uint64>(Entry.ColliderEvaluations);
				State.MaximumObservedBucketOccupancy = FMath::Max(
					State.MaximumObservedBucketOccupancy,
					MaximumBucketOccupancy);
			}
		});
}
