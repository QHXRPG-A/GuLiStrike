#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceInventoryComponent.h"
#include "GuLiResourceInventorySubsystem.generated.h"

/** Per-definition cargo/processing roles for Actor components or non-Actor entities. */
USTRUCT()
struct FGuLiEntityInventoryDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FName Definition;
    UPROPERTY(EditAnywhere) bool bCargo=true;
    UPROPERTY(EditAnywhere) bool bProcessing=false;
    UPROPERTY(EditAnywhere) int32 CargoCapacity=10;
    UPROPERTY(EditAnywhere) int32 ProcessingCapacity=10000000;
    UPROPERTY(EditAnywhere) float ExtractionRate=2;
    UPROPERTY(EditAnywhere) float ProcessingRate=2;
};
UCLASS(Config=Game,DefaultConfig)
class GULISTRIKE_API UGuLiResourceInventorySettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config,EditAnywhere,Category="Resources|Capabilities") TArray<FGuLiEntityInventoryDefinition> Entities;
};
/** Stable-key access shared by tasks, abilities and UI, regardless of carrier. */
UCLASS()
class GULISTRIKE_API UGuLiResourceInventorySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure,Category="Resources|Inventory") UGuLiResourceInventoryComponent* FindInventory(FGuLiHybridObjectKey Object) const;
    UFUNCTION(BlueprintCallable,Category="Resources|Inventory") FGuid RequestExtraction(FGuLiHybridObjectKey Object,int32 NodeId,FGuid MiningToken);
    UFUNCTION(BlueprintCallable,Category="Resources|Inventory") FGuid RequestDelivery(FGuLiHybridObjectKey Object,FGuLiHybridObjectKey Destination);
    bool Bind(FGuLiHybridObjectKey Key,UGuLiResourceInventoryComponent& Inventory);
    void Unbind(FGuLiHybridObjectKey Key,const UGuLiResourceInventoryComponent& Inventory);
private:
    TMap<FGuLiHybridObjectKey,TWeakObjectPtr<UGuLiResourceInventoryComponent>> Entities;
};
