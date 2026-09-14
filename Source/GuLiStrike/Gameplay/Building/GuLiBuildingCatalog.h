// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "GuLiBuildingCatalog.generated.h"

class UMaterialInterface;

/** One eagerly loaded MVP catalog. Its mesh references are hard references once the catalog is loaded. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiBuildingCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	const FGuLiBuildingDefinition* FindDefinition(EGuLiBuildingType Type) const;
	const FGuLiBuildingDefinition* FindById(int32 Id) const;
	bool ResolveTable();
	bool IsUsable() const;

	static const TCHAR* GetDefaultCatalogObjectPath();
	static UGuLiBuildingCatalog* LoadDefaultCatalog();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TArray<FGuLiBuildingDefinition> Definitions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TObjectPtr<UMaterialInterface> PreviewMaterial;

private:
	bool bTableResolved = false;
};
