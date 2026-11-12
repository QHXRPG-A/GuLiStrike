#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Resources/GuLiResourceInventoryComponent.h"
#include "GuLiShieldGeneratorActor.generated.h"

/** Shield supply and perimeter ore delivery share the building's existing lifecycle. */
UCLASS()
class GULISTRIKE_API AGuLiShieldGeneratorActor final : public AGuLiPlacedBuilding
{
    GENERATED_BODY()
public:
    AGuLiShieldGeneratorActor();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    EGuLiTeam GetTeam() const { return GetBuildingTeam(); }
    FGuLiControllableActorId GetStableActorId() const;
    FGuLiResourceFactoryPrivateState MakePrivateState() const;
    UFUNCTION(BlueprintPure, Category="Resources|Delivery")
    TArray<FVector> GetUnloadPoints(FVector From, float VehicleRadius, float ContactMargin) const;
    int32 GetQueuedRawAmount() const { return Inventory->GetQueuedRawAmount(); }
    TConstArrayView<FGuLiFactoryQueueEntry> GetDeliveryQueue() const { return Inventory->GetQueue(); }
    FGuid GetPendingResourceEvent() const { return Inventory->GetPendingEvent(); }
    uint64 GetAppliedResourceRevision() const { return Inventory->GetAppliedRevision(); }
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiResourceInventoryComponent> Inventory;
    float BoundaryRadius=0;
};
