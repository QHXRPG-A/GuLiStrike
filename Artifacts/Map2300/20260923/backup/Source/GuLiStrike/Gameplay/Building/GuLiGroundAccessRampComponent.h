#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "GuLiGroundAccessRampComponent.generated.h"

struct FCollisionQueryParams;

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
	/** Shared by spawn preflight and BeginPlay; source geometry is queried, not NavMesh. */
	bool ResolveGroundEdge(UWorld& World, const FTransform& Building, const FCollisionQueryParams& Query,
		FVector& OutLocalEdge, FString& OutReason) const;
	/** Invalid terrain disables this component; it must never terminate the match. */
	bool RefreshGroundFit();
	/** Includes native and Blueprint SCS component templates without spawning the presentation. */
	static bool ValidatePresentationGround(UWorld& World, UClass* PresentationClass, const FTransform& Building,
		const FCollisionQueryParams& Query, FString& OutReason);
	static constexpr double GroundTraceDistance = 50000.0;
	static constexpr double MaximumGroundRise = 100.0;
	static constexpr double MaximumGroundDrop = 1000.0;
protected:
	virtual void BeginPlay() override;
private:
	void FitRamp(const FVector& LowerEdge);
};
