#pragma once
#include "CoreMinimal.h"
#include "GuLiHybridObjectKey.generated.h"

UENUM(BlueprintType)
enum class EGuLiHybridObjectKind : uint8
{ None, Soldier, Wingman, Ship, GroundPlayer, Engineer, Miner, Building, Flight, Field, Territory, ResourceNode, BusinessWork };

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridObjectKey
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EGuLiHybridObjectKind Kind = EGuLiHybridObjectKind::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid StableId;
    UPROPERTY(EditAnywhere) uint32 LifeGeneration = 1;
    bool IsValid() const { return Kind != EGuLiHybridObjectKind::None && StableId.IsValid() && LifeGeneration != 0; }
    bool operator==(const FGuLiHybridObjectKey& Other) const
    { return Kind == Other.Kind && StableId == Other.StableId && LifeGeneration == Other.LifeGeneration; }
    friend uint32 GetTypeHash(const FGuLiHybridObjectKey& K)
    { return HashCombine(HashCombine(GetTypeHash(K.StableId), K.LifeGeneration), static_cast<uint32>(K.Kind)); }
};
