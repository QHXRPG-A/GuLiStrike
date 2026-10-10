#pragma once

#include "CoreMinimal.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"

class APlayerController;
class UNetConnection;

/** Local, one-second wall-clock samples. Never sends telemetry or changes transport state. */
class FGuLiNetworkOverlayStats
{
public:
	void Refresh(APlayerController* Controller, double Now);
	const TArray<FString>& GetLines() const { return Lines; }

private:
	TArray<FString> Lines;
	TWeakObjectPtr<UNetConnection> SampleConnection;
	TWeakObjectPtr<UGuLiCombatEffectReplicationComponent> SampleFlights;
	double SampleWallSeconds = -1.0;
	uint32 PreviousTotals[6] = {};
	FGuLiFlightReceiveCounters PreviousFlights;
};
