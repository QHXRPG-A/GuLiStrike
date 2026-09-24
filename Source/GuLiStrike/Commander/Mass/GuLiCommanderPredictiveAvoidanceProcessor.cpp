// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderPredictiveAvoidanceProcessor.h"

#include "Avoidance/MassAvoidanceFragments.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Commander/Mass/Navigation/GuLiCommanderAvoidancePolicy.h"
#include "Engine/World.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassLODTypes.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
		bool bReceiver = false;
		bool bShouldReceive = false;
		bool bForced = false;
		bool bSolved = false;
	};
}

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

	TArray<FProcessorAgent> Agents;
	Agents.Reserve(512);
	TMap<FMassEntityHandle, int32> AgentIndexByEntity;
	FAvoidanceSpatialGrid SpatialGrid;
	int32 MaximumBucketOccupancy = 0;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_PredictiveAvoidanceBuildGrid);
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

		TArray<FAgentSnapshot> PolicyAgents;
		PolicyAgents.Reserve(Agents.Num());
		for (const FProcessorAgent& Entry : Agents)
		{
			PolicyAgents.Add(Entry.Agent);
		}
		MaximumBucketOccupancy = BuildSpatialGrid(PolicyAgents, SpatialGrid);
	}

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

	TArray<FAgentSnapshot> PolicyAgents;
	PolicyAgents.Reserve(Agents.Num());
	for (const FProcessorAgent& Entry : Agents)
	{
		PolicyAgents.Add(Entry.Agent);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_PredictiveAvoidanceCandidateQuery);
		for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
		{
			FProcessorAgent& Entry = Agents[AgentIndex];
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
			const FCandidateQueryMetrics Metrics = SelectNearestCandidates(
				AgentIndex,
				PolicyAgents,
				SpatialGrid,
				Entry.Candidates);
			Entry.CandidateCount = Metrics.ExactCandidates;
			Entry.bSolved = true;
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_PredictiveAvoidanceSolve);
		for (FProcessorAgent& Entry : Agents)
		{
			if (!Entry.bSolved)
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
		}
	}

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
