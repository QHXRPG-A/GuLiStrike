#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiFlightNavigationBaker.h"
#include "GuLiFlightNavigationEditorLibrary.generated.h"

class AGuLiFlightNavigationVolume;
class UGuLiFlightNavigationData;
class UWorld;

UCLASS()
class GULIFLIGHTNAVIGATIONEDITOR_API UGuLiFlightNavigationEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Flight Navigation")
	static bool BakeVolume(
		AGuLiFlightNavigationVolume* Volume,
		const FGuLiFlightNavBakeSettings& Settings,
		FText& OutError);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Flight Navigation", meta = (WorldContext = "WorldContextObject"))
	static int32 BakeAllVolumes(
		UObject* WorldContextObject,
		const FGuLiFlightNavBakeSettings& Settings,
		TArray<FText>& OutErrors);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Flight Navigation")
	static bool ValidateNavigationData(const UGuLiFlightNavigationData* NavigationData, FText& OutError);

	/** Read-only saved-data point probe used by deployment and map audits. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Flight Navigation")
	static bool ValidateNavigationDataPoint(
		const UGuLiFlightNavigationData* NavigationData,
		const FVector& Point,
		float AgentRadius,
		int32& OutCellIndex,
		uint8& OutStatus,
		FText& OutError);

	/** Read-only connected-component probe for saved-data deployment checks. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Flight Navigation")
	static bool ValidateNavigationDataEndpoints(
		const UGuLiFlightNavigationData* NavigationData,
		const FVector& Start,
		const FVector& End,
		float AgentRadius,
		int32& OutStartCellIndex,
		int32& OutEndCellIndex,
		uint8& OutStatus,
		FText& OutError);

	/** Explicitly rescans static collision metadata and reports Stale without mutating the bake. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Flight Navigation")
	static bool ValidateVolume(
		const AGuLiFlightNavigationVolume* Volume,
		const FGuLiFlightNavBakeSettings& Settings,
		FText& OutError);

	/** Deterministic read-only signature of the static collision that influences one volume. */
	static uint64 ComputeSourceGeometrySignature(
		UWorld* World,
		const AGuLiFlightNavigationVolume* Volume,
		const FGuLiFlightNavBakeSettings& Settings);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Flight Navigation")
	static bool ClearVolume(AGuLiFlightNavigationVolume* Volume, FText& OutError);
};
