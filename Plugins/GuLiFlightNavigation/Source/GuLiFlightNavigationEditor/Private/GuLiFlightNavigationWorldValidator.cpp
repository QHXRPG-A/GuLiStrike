#include "GuLiFlightNavigationWorldValidator.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GuLiFlightNavigationCookGate.h"
#include "GuLiFlightNavigationCookSettings.h"
#include "GuLiFlightNavigationVolume.h"
#include "Misc/DataValidation.h"

bool UGuLiFlightNavigationWorldValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InObject,
	FDataValidationContext& InContext) const
{
	return IsValid(InObject) && InObject->IsA<UWorld>();
}

EDataValidationResult UGuLiFlightNavigationWorldValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	UWorld* World = Cast<UWorld>(InAsset);
	if (!IsValid(World))
	{
		return EDataValidationResult::NotValidated;
	}

	const UGuLiFlightNavigationCookSettings* Settings = GetDefault<UGuLiFlightNavigationCookSettings>();
	const bool bRequiredWorld = FGuLiFlightNavigationCookGate::IsWorldRequired(
		World->GetOutermost()->GetFName(),
		Settings->RequiredWorldPackages);
	bool bContainsEnabledVolume = false;
	for (TActorIterator<AGuLiFlightNavigationVolume> Iterator(World); Iterator; ++Iterator)
	{
		if (IsValid(*Iterator) && Iterator->bNavigationEnabled)
		{
			bContainsEnabledVolume = true;
			break;
		}
	}
	if (!bRequiredWorld && !bContainsEnabledVolume)
	{
		return EDataValidationResult::NotValidated;
	}

	TArray<FGuLiFlightNavigationCookIssue> Issues;
	if (!FGuLiFlightNavigationCookGate::ValidateWorld(
		World,
		Settings->RequiredWorldPackages,
		Issues))
	{
		for (const FGuLiFlightNavigationCookIssue& Issue : Issues)
		{
			AssetFails(InAsset, FText::FromString(Issue.ToLogString()));
		}
		return EDataValidationResult::Invalid;
	}

	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}
