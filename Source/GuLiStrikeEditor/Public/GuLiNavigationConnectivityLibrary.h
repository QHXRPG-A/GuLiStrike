#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiNavigationConnectivityLibrary.generated.h"

class UWorld;

/** A strongly connected region of the filtered CommanderSoldier NavMesh. Units are cm. */
USTRUCT(BlueprintType)
struct FGuLiNavigationConnectedRegion
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 RegionId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) int32 GroundPolygonCount = 0;
	UPROPERTY(BlueprintReadOnly) FVector RepresentativeLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector BoundsMinimum = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector BoundsMaximum = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) double AreaSquareMeters = 0;
	UPROPERTY(BlueprintReadOnly) bool bMainRegion = false;
};

USTRUCT(BlueprintType)
struct FGuLiNavigationConnectivityResult
{
	GENERATED_BODY()
	/** False means a missing/invalid dependency, never a disconnected-region verdict. */
	UPROPERTY(BlueprintReadOnly) bool bAnalysisSucceeded = false;
	UPROPERTY(BlueprintReadOnly) bool bFullyConnected = false;
	UPROPERTY(BlueprintReadOnly) FString NavigationDataPath;
	UPROPERTY(BlueprintReadOnly) FString Message;
	UPROPERTY(BlueprintReadOnly) int32 GraphPolygonCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 GroundPolygonCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 DirectedEdgeCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 ExcludedPolygonCount = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FGuLiNavigationConnectedRegion> Regions;
	UPROPERTY(BlueprintReadOnly) double AnalysisSeconds = 0;
};

UENUM(BlueprintType)
enum class EGuLiNavigationPointConnectivityStatus : uint8
{
	DependencyError,
	InvalidInput,
	StartNotNavigable,
	EndNotNavigable,
	Disconnected,
	OneWay,
	Connected
};

USTRUCT(BlueprintType)
struct FGuLiNavigationPointConnectivityResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EGuLiNavigationPointConnectivityStatus Status = EGuLiNavigationPointConnectivityStatus::DependencyError;
	UPROPERTY(BlueprintReadOnly) bool bQuerySucceeded = false;
	UPROPERTY(BlueprintReadOnly) bool bStartToEnd = false;
	UPROPERTY(BlueprintReadOnly) bool bEndToStart = false;
	UPROPERTY(BlueprintReadOnly) int32 StartRegionId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) int32 EndRegionId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FVector ProjectedStart = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector ProjectedEnd = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FString NavigationDataPath;
	UPROPERTY(BlueprintReadOnly) FString Message;
	UPROPERTY(BlueprintReadOnly) double QuerySeconds = 0;
};

/** Editor diagnostics over UE's existing directed polygon links; no runtime pathfinding changes. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiNavigationConnectivityLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Read only. Uses the same agent and default query filter as Commander movement. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Navigation", meta=(WorldContext="WorldContextObject"))
	static FGuLiNavigationConnectivityResult AnalyzeWorldConnectivity(UObject* WorldContextObject);

	/** No partial-path success or A* search limit. Projection never exceeds the supplied extent. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Navigation", meta=(WorldContext="WorldContextObject"))
	static FGuLiNavigationPointConnectivityResult CheckPointConnectivity(
		UObject* WorldContextObject, FVector Start, FVector End, FVector ProjectionExtent);

	static bool RequiresConnectedGround(const UWorld& World);
};
