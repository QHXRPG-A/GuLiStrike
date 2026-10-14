#pragma once
#include "CoreMinimal.h"
class APlayerController;
class UGuLiHybridSessionSubsystem;

/** One-second wall-clock samples from the active P2P transport; no telemetry is sent. */
class FGuLiNetworkOverlayStats
{
public:
    void Refresh(APlayerController* Controller,double Now);
    const TArray<FString>& GetLines() const {return Lines;}
private:
    TArray<FString> Lines;
    TWeakObjectPtr<UGuLiHybridSessionSubsystem> SampleSession;
    FGuid SampleRoom;
    double SampleWallSeconds=-1;
    double PreviousTotals[4]={};
};
