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
	UWorld* World = Cast<UWorld>(InObject);
	if (!IsValid(World))
	{
		return false;
	}

	const UGuLiFlightNavigationCookSettings* Settings = GetDefault<UGuLiFlightNavigationCookSettings>();
	if (FGuLiFlightNavigationCookGate::IsWorldRequired(
		World->GetOutermost()->GetFName(),
		Settings->RequiredWorldPackages))
	{
		return true;
	}
	for (TActorIterator<AGuLiFlightNavigationVolume> Iterator(World); Iterator; ++Iterator)
	{
		if (IsValid(*Iterator) && Iterator->bNavigationEnabled)
		{
			return true;
		}
	}
	// UE 5.7 requires a Valid/Invalid result once this predicate accepts an asset.
	// Worlds without flight navigation must be excluded before entering validation.
	return false;
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
