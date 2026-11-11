#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiCircleAvoidance.h"
#include "GuLiAvoidanceCatalog.generated.h"

class UDataTable;

UCLASS(Config=Game,DefaultConfig)
class GULISTRIKE_API UGuLiAvoidanceDataSettings final : public UObject
{
    GENERATED_BODY()
public:
    UGuLiAvoidanceDataSettings();
    UPROPERTY(Config,EditAnywhere,Category="Avoidance") TSoftObjectPtr<UDataTable> UnitsTable;
    UPROPERTY(Config,EditAnywhere,Category="Avoidance") TSoftObjectPtr<UDataTable> BuildingCirclesTable;
    UPROPERTY(Config,EditAnywhere,Category="Avoidance") TSoftObjectPtr<UDataTable> OreClusteringTable;
};

struct FGuLiUnitAvoidanceProfile
{
    GuLiCircleAvoidance::FConfig Solver;
    float GridReferenceDiameterCm = 0;
};

struct FGuLiBuildingAvoidanceCircle
{
    int32 Index = 0;
    FVector LocalCenter = FVector::ZeroVector;
    float Radius = 0;
    float MinimumHeight = 0;
    float MaximumHeight = 0;
    uint8 PhaseMask = 0;
};

/** Immutable match input. Owned by the unit directory; no per-frame table access. */
struct GULISTRIKE_API FGuLiAvoidanceCatalog
{
    TMap<uint16,FGuLiUnitAvoidanceProfile> Units;
    TMap<int32,TArray<FGuLiBuildingAvoidanceCircle>> Buildings;
    float OreMaximumRadiusRatio = 0;
    float OreMaximumAreaRatio = 0;
    FString Fingerprint;
    static bool Load(TConstArrayView<uint16> UnitIds,FGuLiAvoidanceCatalog& Out,FString& Error);
};
