#pragma once

#include "Components/PrimitiveComponent.h"
#include "GuLiShipBlastComponent.generated.h"

struct FGuLiShipBlast
{
	FVector Center;
	double StartTime;
	float Radius;
};

/** One depth-tested render batch for fresh explosions seen from this pilot's seat. */
UCLASS(Transient)
class UGuLiShipBlastComponent final : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UGuLiShipBlastComponent();
	static void NotifyPilots(UWorld& World, const FVector& Location, float Radius);
	void AddBlast(const FVector& Location, float Radius);
	void Publish(double Now);
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	UFUNCTION(BlueprintPure, Category="Ship|Presentation") int32 GetActiveBlastCount() const { return Blasts.Num(); }
	UFUNCTION(BlueprintPure, Category="Ship|Presentation") int64 GetPublishedBlastCount() const { return PublishedBlasts; }
private:
	TArray<FGuLiShipBlast> Blasts;
	int64 PublishedBlasts = 0;
	int32 PendingBlasts = 0;
};
