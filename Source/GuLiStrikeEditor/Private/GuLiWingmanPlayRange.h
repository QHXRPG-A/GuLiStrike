#pragma once

#include "GuLiLocalPIERolePicker.h"
#include "Network/GuLiHybridManifest.h"

class UWorld;

/** Opt-in local PIE preparation. Each room owns its startup manifest and equipment request. */
struct FGuLiWingmanPlayRange
{
    bool Prepare(UWorld& World, GuLiLocalPIERolePicker::ETrial Trial, FGuLiHybridManifestData& Manifest,
        const FString& Destination, FString& Error);
    bool Advance(UWorld& World, FString& Error);

    FGuLiHybridObjectKey Carrier;
    TArray<FGuLiHybridObjectKey> Targets;
    int32 ClearSpawnSlots = 0;
    int32 EnemySpawnSlots = 0;
    bool bReady = false;

private:
    double FirstReadyTime = 0;
    FGuid EquipmentRequest;
};
