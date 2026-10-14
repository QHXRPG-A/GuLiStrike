// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "GuLiBuildingCatalog.generated.h"

class UMaterialInterface;
class UDataTable;
class UGuLiBuildingCatalog;

UCLASS(Config=Game,DefaultConfig)
class GULISTRIKE_API UGuLiBuildingDataSettings final : public UObject
{
	GENERATED_BODY()
public:
	UGuLiBuildingDataSettings();
	UPROPERTY(Config,EditAnywhere,Category="Buildings|Data") TSoftObjectPtr<UDataTable> BuildingsTable;
	UPROPERTY(Config,EditAnywhere,Category="Buildings|Data") TSoftObjectPtr<UGuLiBuildingCatalog> Catalog;
};

/** One eagerly loaded MVP catalog. Its mesh references are hard references once the catalog is loaded. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiBuildingCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	const FGuLiBuildingDefinition* FindDefinition(EGuLiBuildingType Type) const;
	const FGuLiBuildingDefinition* FindById(int32 Id) const;
	UFUNCTION(BlueprintCallable, Category="Building|Models")
	bool ResolveTable(bool bForceRefresh = false);
	bool IsUsable() const;

	static const TCHAR* GetDefaultCatalogObjectPath();
	static UGuLiBuildingCatalog* LoadDefaultCatalog();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building", meta=(ToolTip="Resolved cache; edit the Buildings and Models Excel tables."))
	TArray<FGuLiBuildingDefinition> Definitions;

	/** Reuse a capability family with an authored subclass, without adding network routes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Building|Capabilities")
	TMap<int32,TSoftClassPtr<AActor>> GameplayClasses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	int32 PreviewVfxId = 0;

private:
	bool bTableResolved = false;
};
