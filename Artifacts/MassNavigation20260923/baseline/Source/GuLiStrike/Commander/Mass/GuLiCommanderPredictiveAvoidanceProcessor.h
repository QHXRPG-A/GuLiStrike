// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "GuLiCommanderPredictiveAvoidanceProcessor.generated.h"

/**
 * Commander-only predictive avoidance. It snapshots participants into a project-owned
 * 1500 cm grid at 30 Hz and solves one SoldierId phase per step, yielding 10 Hz per Soldier.
 * Instant separation remains owned by the authority subsystem's symmetric manual solver.
 */
UCLASS()
class GULISTRIKE_API UGuLiCommanderPredictiveAvoidanceProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGuLiCommanderPredictiveAvoidanceProcessor();

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
};
