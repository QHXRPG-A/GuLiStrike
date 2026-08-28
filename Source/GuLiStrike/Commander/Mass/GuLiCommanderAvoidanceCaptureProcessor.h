// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "GuLiCommanderAvoidanceCaptureProcessor.generated.h"

/**
 * Captures Epic MovingAvoidance's result after the Avoidance phase and clears the
 * shared force accumulator. The 30 Hz authority integrator then reads the stable
 * project fragment, avoiding frame-rate-dependent accumulation between fixed steps.
 */
UCLASS()
class GULISTRIKE_API UGuLiCommanderAvoidanceCaptureProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGuLiCommanderAvoidanceCaptureProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
