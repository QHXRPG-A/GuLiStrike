#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Data/GuLiAvoidanceCatalog.h"
#include "GuLiBuildingCircleBoundaryComponent.generated.h"

class UGuLiBuildingLifecycleComponent;
class UGuLiCircularNavigationComponent;
class UGuLiFactoryFloorNavigationComponent;

/** Event-driven publisher; consumes a match-cached profile, never reads tables. */
UCLASS()
class GULISTRIKE_API UGuLiBuildingCircleBoundaryComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiBuildingCircleBoundaryComponent();
    static bool Install(UGuLiBuildingLifecycleComponent& Lifecycle,const FGuLiAvoidanceCatalog& Catalog);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void RefreshBoundary();
    void RefreshCollisionSources();
private:
    UPROPERTY(Transient) TObjectPtr<UGuLiCircularNavigationComponent> Navigation;
    UPROPERTY(Transient) TObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
    UPROPERTY(Transient) TObjectPtr<UGuLiFactoryFloorNavigationComponent> FactoryFloor;
    TArray<FGuLiBuildingAvoidanceCircle> Profile;
    uint64 Source=0, Version=0;
    int32 Definition=0;
    uint8 LastPhase=255;
    FTransform LastTransform;
};
