// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "GuLiCommanderPredictiveAvoidanceProcessor.generated.h"

namespace GuLiCommanderPredictiveAvoidancePrivate { struct FWorkspace; }

/**
 * Commander-only predictive avoidance. It snapshots participants into a project-owned
 * radius-sized grid (300 cm minimum) at 30 Hz and solves one SoldierId phase per step.
 * Instant separation remains owned by the authority subsystem's symmetric manual solver.
 */
UCLASS()
class GULISTRIKE_API UGuLiCommanderPredictiveAvoidanceProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGuLiCommanderPredictiveAvoidanceProcessor();
	virtual ~UGuLiCommanderPredictiveAvoidanceProcessor() override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void InitializeInternal(
		UObject& Owner,
		const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<UWorld> World;
	FMassEntityQuery ObstacleQuery;
	FMassEntityQuery ReceiverQuery;
	double FixedStepAccumulatorSeconds = 0.0;
	uint64 NextSolveSequence = 0u;
	TUniquePtr<GuLiCommanderPredictiveAvoidancePrivate::FWorkspace> Workspace;
};
