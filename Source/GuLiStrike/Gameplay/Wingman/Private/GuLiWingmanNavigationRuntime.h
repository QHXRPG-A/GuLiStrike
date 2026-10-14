#pragma once
#include "CoreMinimal.h"
#include "GuLiFlightNavigationSubsystem.h"
inline constexpr int32 GuLiWingmanNavigationMaximumExpandedNodes=8192;

/** Futures and cancellation tokens are process-local; snapshots retain request inputs only. */
struct FGuLiWingmanFlightNavigationRuntime
{
    TSharedPtr<FGuLiFlightNavCancellationToken,ESPMode::ThreadSafe> CancellationToken;
    TSharedPtr<TFuture<FGuLiFlightNavPathResult>> PendingResult;
    TArray<FVector> PathPoints;
    FVector RequestedStart=FVector::ZeroVector,RequestedGoal=FVector::ZeroVector,ActivePathGoal=FVector::ZeroVector;
    double NextRequestSeconds=0;
    uint32 RequestSerial=0,PendingRequestSerial=0,RequestedAbilitySetRevision=0,RequestedFormationCommandRevision=0;
    int32 NextPathPointIndex=INDEX_NONE;
    bool bRequestPending=false,bUsingSafeFallback=false;
};
