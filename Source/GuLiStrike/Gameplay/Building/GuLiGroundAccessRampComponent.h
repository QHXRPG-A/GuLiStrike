#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "GuLiGroundAccessRampComponent.generated.h"

/** Building presentation geometry joining a fixed apron edge to the local terrain. */
UCLASS(ClassGroup=(Buildings), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiGroundAccessRampComponent final : public UStaticMeshComponent
{
	GENERATED_BODY()
public:
	UGuLiGroundAccessRampComponent();
	UPROPERTY(EditAnywhere, Category="Access Ramp") FVector ApronEdge = FVector(2400, 0, 69);
	UPROPERTY(EditAnywhere, Category="Access Ramp") FVector GroundEdge = FVector(4000, 0, 0);
	UPROPERTY(EditAnywhere, Category="Access Ramp") float Width = 3600.0f;
	UPROPERTY(EditAnywhere, Category="Access Ramp") float Thickness = 15.0f;
protected:
	virtual void BeginPlay() override;
private:
	void FitRamp(const FVector& LowerEdge);
};
