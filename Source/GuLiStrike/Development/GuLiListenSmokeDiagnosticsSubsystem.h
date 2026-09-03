// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiListenSmokeDiagnosticsSubsystem.generated.h"

/**
 * Explicit-command-line, non-Shipping diagnostics for a real two-process Listen smoke.
 *
 * The subsystem is inert unless -GuLiListenSmoke is present. It samples read-only
 * networking/Wingman state once per second and asks the engine to exit after the
 * requested duration. It never owns gameplay state, never submits Relay traffic,
 * and never writes a Wingman transform.
 */
UCLASS()
class GULISTRIKE_API UGuLiListenSmokeDiagnosticsSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void EmitSample(bool bFinalSample);

	double StartedWallSeconds = 0.0;
	double NextSampleWallSeconds = 0.0;
	double DurationSeconds = 30.0;
	FString RequestedRole;
	FString RunId;
	bool bFinalSampleEmitted = false;
};

