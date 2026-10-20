#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiResourceInventoryComponent.generated.h"

struct FGuLiHybridResourceInventoryState;
struct FGuLiHybridManifestData;

// Keep the existing reflected struct path while sharing the FIFO capability.
USTRUCT()
struct GULISTRIKE_API FGuLiFactoryQueueEntry
{
    GENERATED_BODY()
    UPROPERTY() EGuLiResourceType ResourceType=EGuLiResourceType::Blue;
    UPROPERTY() int32 Amount=0;
    UPROPERTY() EGuLiTeam SettlementTeam=EGuLiTeam::Unassigned;
    FGuid DeliveryEvent;
    FGuLiHybridObjectKey Source;
};

/** Ledger-backed raw inventory, FIFO processing and outstanding receipt state. */
UCLASS(ClassGroup=(GuLiStrike),meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiResourceInventoryComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiResourceInventoryComponent();
    bool BindEntity(UWorld& World,const FGuLiHybridObjectState& Object);
    void UnbindEntity();
    FGuLiHybridObjectKey GetInventoryKey() const;
    FGuid RequestDeliveryKey(const FGuLiHybridObjectKey& Destination);
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Resources|Capabilities") bool bCargo=false;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Resources|Capabilities") bool bProcessing=false;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Resources|Capabilities") bool bUseEconomyDefaults=true;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Resources|Capabilities",meta=(ClampMin="1",ClampMax="1000000")) int32 CargoCapacity=10;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Resources|Capabilities",meta=(ClampMin="1",ClampMax="10000000")) int32 ProcessingCapacity=10000000;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Resources|Capabilities",meta=(ClampMin="0.000012",ClampMax="60")) float ExtractionRate=2;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Resources|Capabilities",meta=(ClampMin="0.000012",ClampMax="60")) float ProcessingRate=2;
    UFUNCTION(BlueprintPure,Category="Resources|Inventory") FGuLiResourceAmounts GetCargo() const {return Cargo;}
    UFUNCTION(BlueprintPure,Category="Resources|Inventory") int32 GetCargoCapacity() const {return CargoCapacity;}
    UFUNCTION(BlueprintCallable,Category="Resources|Inventory") FGuid RequestExtraction(int32 NodeId,FGuid MiningToken);
    UFUNCTION(BlueprintCallable,Category="Resources|Inventory") FGuid RequestDelivery(AActor* Destination);
    bool ExportDefinition(FGuLiHybridManifestData& Manifest,const FString& Definition,FString& Error) const;
    void ApplyResourceResult(const FGuLiHybridTransactionResult& Result);
    void ConfigureProcessing(float RatePerSecond,FName Rule);

    void AdvanceLogical(float Delta);
    TConstArrayView<FGuLiFactoryQueueEntry> GetQueue() const {return Queue;}
    FGuid GetPendingEvent() const {return PendingEvent;}
    uint64 GetAppliedRevision() const {return AppliedRevision;}
    uint64 GetCargoRevision() const {return CargoRevision;}
    int32 GetQueuedRawAmount() const;
    bool CaptureState(FGuLiHybridResourceInventoryState& Out) const;
    bool ValidateState(const FGuLiHybridResourceInventoryState& State) const;
    bool RestoreState(const FGuLiHybridResourceInventoryState& State);
    void ResetResourceRequests(uint64 Epoch);
private:
    bool RefreshCommittedInventory();
    UFUNCTION() void ApplyCommittedResource(const FGuLiHybridTransactionResult& Result);
    UPROPERTY() TArray<FGuLiFactoryQueueEntry> Queue;
    float RatePerSecond=2,Accumulator=0;
    FName ProcessingRule;
    FGuid PendingEvent;
    uint64 PendingAt=0,NextProcessingTick=0,AppliedRevision=0;
    FGuLiResourceAmounts Cargo;
    FGuid PendingCargo;
    uint64 CargoRevision=0;
    bool ConfigureFromManifest();
    TWeakObjectPtr<UWorld> EntityWorld;
    FGuLiHybridObjectKey EntityKey;
    FName EntityDefinition;
};
