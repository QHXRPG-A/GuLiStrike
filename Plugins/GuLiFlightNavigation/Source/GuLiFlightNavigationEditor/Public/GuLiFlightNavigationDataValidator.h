#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "GuLiFlightNavigationDataValidator.generated.h"

UCLASS()
class GULIFLIGHTNAVIGATIONEDITOR_API UGuLiFlightNavigationDataValidator : public UEditorValidatorBase
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
