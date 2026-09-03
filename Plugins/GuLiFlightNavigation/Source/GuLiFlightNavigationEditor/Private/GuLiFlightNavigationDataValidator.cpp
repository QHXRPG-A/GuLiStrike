#include "GuLiFlightNavigationDataValidator.h"

#include "GuLiFlightNavigationCookGate.h"
#include "GuLiFlightNavigationData.h"
#include "Misc/DataValidation.h"

bool UGuLiFlightNavigationDataValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InObject,
	FDataValidationContext& InContext) const
{
	return IsValid(InObject) && InObject->IsA<UGuLiFlightNavigationData>();
}

EDataValidationResult UGuLiFlightNavigationDataValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	const UGuLiFlightNavigationData* NavigationData = Cast<UGuLiFlightNavigationData>(InAsset);
	if (NavigationData == nullptr)
	{
		return EDataValidationResult::NotValidated;
	}

	FGuLiFlightNavigationCookIssue Issue;
	if (!FGuLiFlightNavigationCookGate::ValidateData(NavigationData, Issue))
	{
		AssetFails(InAsset, FText::FromString(Issue.ToLogString()));
		return EDataValidationResult::Invalid;
	}

	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}
