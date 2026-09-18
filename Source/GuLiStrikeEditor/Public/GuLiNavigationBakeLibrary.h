#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiNavigationBakeLibrary.generated.h"

class ARecastNavMesh;

USTRUCT(BlueprintType)
struct FGuLiNavigationBakeEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString ObjectPath;
	UPROPERTY(BlueprintReadOnly) FString Kind;
	UPROPERTY(BlueprintReadOnly) FString Status;
	UPROPERTY(BlueprintReadOnly) FString SourceHash;
	UPROPERTY(BlueprintReadOnly) FString Message;
	UPROPERTY(BlueprintReadOnly) double CheckSeconds = 0;
	UPROPERTY(BlueprintReadOnly) double BuildSeconds = 0;
};

USTRUCT(BlueprintType)
struct FGuLiNavigationBakeResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly) FString Message;
	UPROPERTY(BlueprintReadOnly) TArray<FGuLiNavigationBakeEntry> Entries;
	UPROPERTY(BlueprintReadOnly) int32 GroundRebuilds = 0;
	UPROPERTY(BlueprintReadOnly) int32 FlightRebuilds = 0;
	UPROPERTY(BlueprintReadOnly) double SaveSeconds = 0;
	UPROPERTY(BlueprintReadOnly) double TotalSeconds = 0;
};

UCLASS()
class GULISTRIKEEDITOR_API UGuLiNavigationBakeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Explicit one-time scale020 configuration migration. Does not build or save. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Navigation", meta=(WorldContext="WorldContextObject"))
	static FGuLiNavigationBakeResult MigrateObjectScale020(UObject* WorldContextObject);

	/** Check before duplicating the PIE world. Saves rebuilt map tiles and flight assets together. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Navigation", meta=(WorldContext="WorldContextObject"))
	static FGuLiNavigationBakeResult PrepareWorldNavigation(UObject* WorldContextObject, bool bSavePackages = true);

	/** Source-world validation only: never builds, saves, or changes navigation configuration. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Navigation", meta=(WorldContext="WorldContextObject"))
	static FGuLiNavigationBakeResult ValidateWorldNavigation(UObject* WorldContextObject);

	static uint64 ComputeGroundSourceHash(UWorld* World, const ARecastNavMesh* Navigation);
	static TArray<FName> GetPreparationWorldPackages();
};
