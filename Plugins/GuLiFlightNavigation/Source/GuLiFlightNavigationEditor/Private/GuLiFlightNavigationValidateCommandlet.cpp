#include "GuLiFlightNavigationValidateCommandlet.h"

#include "FileHelpers.h"
#include "GuLiFlightNavigationCookGate.h"
#include "GuLiFlightNavigationCookSettings.h"
#include "GuLiFlightNavigationLog.h"
#include "Misc/PackageName.h"

UGuLiFlightNavigationValidateCommandlet::UGuLiFlightNavigationValidateCommandlet()
{
	IsClient = false;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UGuLiFlightNavigationValidateCommandlet::Main(const FString& Params)
{
	const UGuLiFlightNavigationCookSettings* Settings = GetDefault<UGuLiFlightNavigationCookSettings>();
	if (Settings->RequiredWorldPackages.IsEmpty())
	{
		UE_LOG(
			LogGuLiFlightNav,
			Error,
			TEXT("[FLIGHTNAV_COOK_GATE][Configuration] No RequiredWorldPackages are configured."));
		return 1;
	}

	int32 FailureCount = 0;
	for (const FName WorldPackage : Settings->RequiredWorldPackages)
	{
		const FString PackageName = WorldPackage.ToString();
		if (!FPackageName::IsValidLongPackageName(PackageName)
			|| !FPackageName::DoesPackageExist(PackageName))
		{
			UE_LOG(
				LogGuLiFlightNav,
				Error,
				TEXT("[FLIGHTNAV_COOK_GATE][MissingWorld] Required map package '%s' does not exist."),
				*PackageName);
			++FailureCount;
			continue;
		}

		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(PackageName);
		if (!IsValid(World))
		{
			UE_LOG(
				LogGuLiFlightNav,
				Error,
				TEXT("[FLIGHTNAV_COOK_GATE][MissingWorld] Failed to load required map '%s'."),
				*PackageName);
			++FailureCount;
			continue;
		}

		TArray<FGuLiFlightNavigationCookIssue> Issues;
		if (!FGuLiFlightNavigationCookGate::ValidateWorld(
			World,
			Settings->RequiredWorldPackages,
			Issues))
		{
			for (const FGuLiFlightNavigationCookIssue& Issue : Issues)
			{
				UE_LOG(LogGuLiFlightNav, Error, TEXT("%s"), *Issue.ToLogString());
			}
			FailureCount += Issues.Num();
			continue;
		}

		UE_LOG(
			LogGuLiFlightNav,
			Display,
			TEXT("[FLIGHTNAV_COOK_GATE][Valid] %s"),
			*PackageName);
	}

	UE_LOG(
		LogGuLiFlightNav,
		Display,
		TEXT("Flight Navigation pre-cook validation completed: %d map(s), %d failure(s)."),
		Settings->RequiredWorldPackages.Num(),
		FailureCount);
	return FailureCount == 0 ? 0 : 1;
}
