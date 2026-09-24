#include "GuLiNavigationPrepareCommandlet.h"

#include "AssetCompilingManager.h"
#include "FileHelpers.h"
#include "GuLiNavigationBakeLibrary.h"
#include "Misc/Parse.h"

UGuLiNavigationPrepareCommandlet::UGuLiNavigationPrepareCommandlet()
{
	IsClient = false;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UGuLiNavigationPrepareCommandlet::Main(const FString& Params)
{
	TArray<FName> Maps = UGuLiNavigationBakeLibrary::GetPreparationWorldPackages();
	FString ExplicitMap;
	if (FParse::Value(*Params, TEXT("Map="), ExplicitMap)) Maps = { FName(*ExplicitMap) };
	if (Maps.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("[GULI_NAV_PREPARE] No packaging maps configured.")); return 1; }
	const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
	int32 Failures = 0;
	for (const FName Map : Maps)
	{
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map.ToString());
		// LoadMap may return while static-mesh compilation is still updating source bounds.
		// Complete source asset loading before the read-only signature check, just as Prepare does.
		FAssetCompilingManager::Get().FinishAllCompilation();
		const FGuLiNavigationBakeResult Result = bValidateOnly
			? UGuLiNavigationBakeLibrary::ValidateWorldNavigation(World)
			: UGuLiNavigationBakeLibrary::PrepareWorldNavigation(World, true);
		if (bValidateOnly)
		{
			for (const auto& Entry : Result.Entries)
				UE_LOG(LogTemp, Display, TEXT("[GULI_NAV_VALIDATE] kind=%s status=%s object=%s hash=%s check_s=%.6f reason=%s"),
					*Entry.Kind, *Entry.Status, *Entry.ObjectPath, *Entry.SourceHash, Entry.CheckSeconds, *Entry.Message);
			UE_LOG(LogTemp, Display, TEXT("[GULI_NAV_VALIDATE] total_s=%.6f"), Result.TotalSeconds);
		}
		if (!Result.bSuccess)
		{
			++Failures;
			UE_LOG(LogTemp, Error, TEXT("[GULI_NAV_PREPARE] map=%s failed: %s"), *Map.ToString(), *Result.Message);
		}
		else UE_LOG(LogTemp, Display, TEXT("[GULI_NAV_PREPARE] map=%s validated"), *Map.ToString());
	}
	return Failures == 0 ? 0 : 1;
}
