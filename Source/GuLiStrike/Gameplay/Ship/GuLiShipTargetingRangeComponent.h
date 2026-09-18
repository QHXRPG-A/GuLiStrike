#pragma once

#include "Components/PrimitiveComponent.h"
#include "GuLiShipTargetingRangeComponent.generated.h"

/** Local presentation of the Ship-centered 3D acquisition radius used by automatic wingman targeting. */
UCLASS(ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiShipTargetingRangeComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UGuLiShipTargetingRangeComponent();

	void SetTargetingRadius(float InRadiusCentimeters);
	float GetTargetingRadius() const { return TargetingRadiusCentimeters; }
	FLinearColor GetRangeColor() const { return RangeColor; }
	float GetLineThickness() const { return LineThickness; }
	int32 GetSegmentCount() const { return SegmentCount; }

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	UPROPERTY(EditAnywhere, Category="Targeting Range", meta=(ClampMin="0.0", Units="Centimeters"))
	float TargetingRadiusCentimeters = 30000.0f;

	UPROPERTY(EditAnywhere, Category="Targeting Range")
	FLinearColor RangeColor = FLinearColor(0.0f, 0.75f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Targeting Range", meta=(ClampMin="0.0"))
	float LineThickness = 2.0f;

	UPROPERTY(EditAnywhere, Category="Targeting Range", meta=(ClampMin="16", ClampMax="256"))
	int32 SegmentCount = 96;
};
