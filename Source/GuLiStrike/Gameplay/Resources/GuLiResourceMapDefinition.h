// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "GuLiResourceMapDefinition.generated.h"

class UStaticMesh;
enum class EGuLiBuildingType : uint8;

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiOreVisualAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Visual")
	EGuLiResourceType ResourceType = EGuLiResourceType::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Visual", meta = (ClampMin = "0", ClampMax = "3"))
	uint8 FamilyIndex = 0u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Visual")
	EGuLiOreVisualStage Stage = EGuLiOreVisualStage::Full;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Visual")
	TSoftObjectPtr<UStaticMesh> Mesh;

	bool IsWellFormed() const;
};

/** Cooked, immutable result of the editor-only GuLiMapAuthoring adapter. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiResourceMapDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Bake")
	FName MapPackage = TEXT("/Game/Maps/LVL_CommanderMassPrototype");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Bake")
	int32 LayoutVersion = GULI_RESOURCE_LAYOUT_VERSION;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Bake")
	int32 DeterministicSeed = GULI_RESOURCE_BAKE_SEED;

	/** Hash of markers, property bags, territories, density payloads and referenced map package. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Bake")
	FString SourceHash;

	/** Hash of every runtime field below; clients compare this before becoming Ready. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Bake")
	FString LayoutHash;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	FVector2D PlayableMinimum = FVector2D(-GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	FVector2D PlayableMaximum = FVector2D(GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Spawn")
	FGuLiResourceSpawnAnchors SpawnAnchors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	TArray<FGuLiTerritoryDefinition> Territories;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	TArray<FGuLiResourceClusterDefinition> Clusters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	TArray<FGuLiResourceNodeDefinition> Nodes;

	/** Full validation is intentionally strict: an invalid bake blocks match startup. */
	UFUNCTION(BlueprintCallable, Category = "Resources|Validation")
	bool ValidateDefinition(FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "Resources|Validation")
	FString CalculateLayoutHash() const;

	const FGuLiTerritoryDefinition* FindTerritory(uint8 TerritoryIndex) const;
	const FGuLiResourceClusterDefinition* FindCluster(uint16 ClusterId) const;
};

/** Match-scoped economy and presentation tuning. It is cooked separately from map geometry. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiResourceEconomyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UGuLiResourceEconomyConfig();

	/** Every controllable unit is authored in Soldiers, including Actor-based miners. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Mining", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 MiningVehicleUnitTypeId = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Mining", meta = (ClampMin = "1.0"))
	float MiningDistanceCentimeters = 5400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Factory", meta = (ClampMin = "1.0"))
	float FactoryManeuverSpeedCentimetersPerSecond = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Mining", meta = (ClampMin = "0.01"))
	float MiningRatePerSecond = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Mining", meta = (ClampMin = "1"))
	int32 CargoCapacity = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Mining", meta = (ClampMin = "0.0"))
	float DockingSeconds = 1.0f;

	/** Distance from factory center toward the map interior; keeps the stop outside factory nav clearance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Factory", meta = (ClampMin = "3250.0"))
	float FactoryDockOffsetCentimeters = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Factory", meta = (ClampMin = "0.01"))
	float FactoryProcessingRatePerSecond = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Mining", meta = (ClampMin = "0.0"))
	float PlayerOrderGraceSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Mining", meta = (ClampMin = "0.1"))
	float AutoRetrySeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Economy", meta = (ClampMin = "0"))
	int32 InitialBlueInventory = 40;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Economy", meta = (ClampMin = "0"))
	int32 InitialRedInventory = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Economy", meta = (ClampMin = "0"))
	int32 SentryTurretBlueCost = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Economy", meta = (ClampMin = "0"))
	int32 MissileTurretBlueCost = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Economy", meta = (ClampMin = "0"))
	int32 OutpostBlueCost = 40;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Visual")
	TArray<FGuLiOreVisualAsset> OreVisuals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Visual")
	TSoftClassPtr<AActor> FactoryPresentationClass;

	UFUNCTION(BlueprintCallable, Category = "Resources|Validation")
	bool ValidateConfig(FString& OutError) const;

	/** Legacy C++ compatibility query; runtime building code uses IGuLiTeamEconomy. */
	int32 GetBlueBuildingCost(EGuLiBuildingType Type) const;

	const FGuLiOreVisualAsset* FindOreVisual(
		EGuLiResourceType ResourceType,
		uint8 FamilyIndex,
		EGuLiOreVisualStage Stage) const;
};
