#pragma once

#include "CoreMinimal.h"
#include "NavRelevantComponent.h"
#include "Gameplay/Navigation/GuLiCircleAvoidance.h"
#include "GuLiCircularNavigationComponent.generated.h"

/** Exports exactly the same world-space circles that movement and work queries consume. */
UCLASS()
class GULISTRIKE_API UGuLiCircularNavigationComponent final : public UNavRelevantComponent
{
    GENERATED_BODY()
public:
    UGuLiCircularNavigationComponent();
    void SetCircles(TConstArrayView<GuLiCircleAvoidance::FCircle> InCircles);
    virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
    virtual void CalcAndCacheBounds() const override;
private:
    TArray<GuLiCircleAvoidance::FCircle> Circles;
};
