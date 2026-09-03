#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "GuLiFlightNavigationWorldValidator.generated.h"

/** Data Validation bridge that makes required-map failures fatal when cook uses validation flags. */
UCLASS()
class GULIFLIGHTNAVIGATIONEDITOR_API UGuLiFlightNavigationWorldValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	virtual bool CanValidateAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InObject,
		FDataValidationContext& InContext) const override;

	virtual EDataValidationResult ValidateLoadedAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InAsset,
		FDataValidationContext& Context) override;
};
