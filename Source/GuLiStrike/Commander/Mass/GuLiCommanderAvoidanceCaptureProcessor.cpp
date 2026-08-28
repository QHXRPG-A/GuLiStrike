// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderAvoidanceCaptureProcessor.h"

#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiCommanderAvoidanceCaptureProcessor)

UGuLiCommanderAvoidanceCaptureProcessor::UGuLiCommanderAvoidanceCaptureProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(
		EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::ApplyForces;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
}

void UGuLiCommanderAvoidanceCaptureProcessor::ConfigureQueries(
	const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassForceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FGuLiMassAvoidanceOutputFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FGuLiServerAuthorityMassTag>(EMassFragmentPresence::All);
}

void UGuLiCommanderAvoidanceCaptureProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FMassForceFragment> Forces =
			ChunkContext.GetMutableFragmentView<FMassForceFragment>();
		const TArrayView<FGuLiMassAvoidanceOutputFragment> Outputs =
			ChunkContext.GetMutableFragmentView<FGuLiMassAvoidanceOutputFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			Outputs[It].Value = Forces[It].Value;
			Forces[It].Value = FVector::ZeroVector;
		}
	});
}
