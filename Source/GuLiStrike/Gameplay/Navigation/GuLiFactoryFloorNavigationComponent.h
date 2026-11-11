#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "GuLiFactoryFloorNavigationComponent.generated.h"

/** Navigation-only copy of the existing floor/ramp hulls; adds no physics body. */
UCLASS()
class GULISTRIKE_API UGuLiFactoryFloorNavigationComponent final : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    UGuLiFactoryFloorNavigationComponent();
    bool InitializeFromFactory(AActor& Factory);
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& Export) const override;
private:
    TArray<FKConvexElem> Hulls;
    TArray<FTransform> HullTransforms;
    FBox WorldBounds=FBox(ForceInit);
};
