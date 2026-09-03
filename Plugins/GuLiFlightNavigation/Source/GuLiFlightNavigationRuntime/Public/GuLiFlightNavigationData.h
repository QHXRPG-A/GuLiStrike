#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GuLiFlightNavigationTypes.h"
#include "GuLiFlightNavigationData.generated.h"

class FObjectPreSaveContext;
struct FGuLiFlightNavRuntimeGraph;

/** Map-specific, deterministic output of the editor bake. Runtime consumers treat it as immutable. */
UCLASS(BlueprintType)
class GULIFLIGHTNAVIGATIONRUNTIME_API UGuLiFlightNavigationData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FGuLiFlightNavBakeMetadata Metadata;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	TArray<FGuLiFlightNavOctreeNode> Nodes;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	TArray<FGuLiFlightNavCell> Cells;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	TArray<FGuLiFlightNavPortal> Portals;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	TArray<FGuLiFlightNavLink> Links;

	bool HasBakedData() const;
	bool ValidateData(FString& OutError) const;
	uint64 ComputeContentChecksum() const;
	TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> CreateRuntimeGraph(FString* OutError = nullptr) const;
	void ResetBakedData();

	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
};
