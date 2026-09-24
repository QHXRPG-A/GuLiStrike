// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiResourceAuthoringLibrary.generated.h"

class UGuLiResourceMapDefinition;

USTRUCT(BlueprintType)
struct GULISTRIKEEDITOR_API FGuLiResourceBakeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	TArray<FString> Issues;

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	FString SourceHash;

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	FString LayoutHash;

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	int32 TerritoryCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	int32 ClusterCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Resources|Bake")
	int32 NodeCount = 0;
};

/**
 * Editor-only adapter between GuLiMapAuthoring and the cooked runtime resource definition.
 * The map plugin remains an authoring dependency and never enters the runtime module.
 */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiResourceAuthoringLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Idempotently prepares the canonical 7x7 authoring data, then performs a strict bake. */
	UFUNCTION(BlueprintCallable, Category = "GuLiStrike|Editor|Resources")
	static FGuLiResourceBakeResult PrepareCanonicalAuthoringAndBake(
		bool bInitializeDensityWhenEmpty = true,
		bool bSavePackages = true);

	/** Strictly bakes the current, already-authored map. No fallback points are synthesized. */
	UFUNCTION(BlueprintCallable, Category = "GuLiStrike|Editor|Resources")
	static FGuLiResourceBakeResult BakeCurrentMap(bool bSavePackages = true);

	/** Compares current authoring source, baked source hash and baked layout validation. */
	UFUNCTION(BlueprintCallable, Category = "GuLiStrike|Editor|Resources")
	static FGuLiResourceBakeResult ValidateCurrentBake();

	/** Configures one in-process dedicated server plus N clients without persisting user settings. */
	UFUNCTION(BlueprintCallable, Category = "GuLiStrike|Editor|Resources")
	static bool ConfigureDedicatedServerPIE(int32 ClientCount);

	/** Configures one in-process listen server and the requested total client count. */
	UFUNCTION(BlueprintCallable, Category = "GuLiStrike|Editor|Resources")
	static bool ConfigureListenServerPIE(int32 ClientCount);

	/** Repairs duplicate local SCS parent metadata produced by legacy UE Python authoring tools. */
	UFUNCTION(BlueprintCallable, Category = "GuLiStrike|Editor|Resources")
	static bool NormalizeBlueprintLocalComponentHierarchy(const FString& BlueprintPath);

	static bool CalculateCurrentSourceHash(FString& OutHash, TArray<FString>& OutIssues);
	static bool IsCanonicalResourceMap(const UWorld* World);
};
