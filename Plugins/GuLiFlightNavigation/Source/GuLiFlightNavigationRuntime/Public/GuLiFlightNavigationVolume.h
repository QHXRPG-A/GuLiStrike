#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "GuLiFlightNavigationTypes.h"
#include "GuLiFlightNavigationVolume.generated.h"

class UGuLiFlightNavigationData;

/** Associates a world-space flight volume with one baked data asset. */
UCLASS(BlueprintType)
class GULIFLIGHTNAVIGATIONRUNTIME_API AGuLiFlightNavigationVolume : public AVolume
{
	GENERATED_BODY()

public:
	AGuLiFlightNavigationVolume(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	TObjectPtr<UGuLiFlightNavigationData> NavigationData;

	/** Settings used by the last successful explicit Bake. Editing them makes the bake stale until Bake runs again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	FGuLiFlightNavBakeSettings AuthoringBakeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	bool bNavigationEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	int32 QueryPriority = 0;

	bool ContainsNavigationPoint(const FVector& Point) const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** FlightNav is a query envelope, never physical world geometry. */
	void EnforceNoPhysicalCollision();
};
