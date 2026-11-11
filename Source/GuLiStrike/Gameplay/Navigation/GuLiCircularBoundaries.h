#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiCircleAvoidance.h"

struct FGuLiAvoidanceCatalog;
struct FGuLiResourceNodeDefinition;
class UGuLiResourceEconomyConfig;

namespace GuLiCircleAvoidance
{
// Mass IDs use the low 32 bits. Source namespaces prevent unit/static identity collisions.
constexpr uint64 BuildingNamespace=uint64(1)<<62;
constexpr uint64 OreNamespace=uint64(2)<<62;
constexpr uint64 PlayerNamespace=uint64(3)<<62;

bool BuildBuildingCircles(const FGuLiAvoidanceCatalog& Catalog,int32 Definition,uint32 Instance,
    uint8 PhaseMask,const FTransform& ActorTransform,uint64 Version,TArray<FCircle>& Out);

/** Match-owned model geometry cache. Load once, use current ore amount on each local update. */
class FOreShapeCache
{
public:
    bool Initialize(const UGuLiResourceEconomyConfig& Config,FString& Error);
    bool BuildNode(const FGuLiResourceNodeDefinition& Node,uint8 Remaining,uint64 Version,FCircle& Out) const;
    SIZE_T GetAllocatedSize() const;
private:
    struct FShape {TArray<FVector> Vertices;};
    TMap<int32,FShape> Shapes;
    TMap<uint32,int32> ModelByStage;
    static uint32 StageKey(uint8 Resource,uint8 Family,uint8 Amount);
};
}
