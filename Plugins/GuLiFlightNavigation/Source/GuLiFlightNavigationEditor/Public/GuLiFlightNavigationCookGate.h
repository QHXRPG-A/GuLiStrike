#pragma once

#include "CoreMinimal.h"

class AGuLiFlightNavigationVolume;
class UGuLiFlightNavigationData;
class UWorld;

/** Stable failure categories emitted by automation, commandlets, Data Validation and cook. */
enum class EGuLiFlightNavigationCookIssue : uint8
{
	Configuration,
	MissingWorld,
	MissingVolume,
	MissingData,
	SchemaMismatch,
	ChecksumMismatch,
	InvalidData,
	StaleSourceWorld,
	StaleSourceVolume,
	StaleBounds,
	StaleSettings,
	StaleGeometry
};

struct GULIFLIGHTNAVIGATIONEDITOR_API FGuLiFlightNavigationCookIssue
{
	EGuLiFlightNavigationCookIssue Code = EGuLiFlightNavigationCookIssue::Configuration;
	FString SubjectPath;
	FString Message;

	FString ToLogString() const;
};

/**
 * Read-only, deterministic preflight shared by Data Validation, the explicit
 * commandlet and the cook pre-save fallback. It never calls Bake or mutates a
 * world, volume, or navigation asset.
 */
class GULIFLIGHTNAVIGATIONEDITOR_API FGuLiFlightNavigationCookGate final
{
public:
	static const TCHAR* LexToString(EGuLiFlightNavigationCookIssue Code);

	static bool IsWorldRequired(FName WorldPackage, TConstArrayView<FName> RequiredWorldPackages);

	static bool ValidateData(
		const UGuLiFlightNavigationData* NavigationData,
		FGuLiFlightNavigationCookIssue& OutIssue);

	static bool ValidateVolume(
		const AGuLiFlightNavigationVolume* Volume,
		FGuLiFlightNavigationCookIssue& OutIssue);

	/** Required maps fail when no enabled volume exists. Any map containing an enabled volume is validated. */
	static bool ValidateWorld(
		UWorld* World,
		TConstArrayView<FName> RequiredWorldPackages,
		TArray<FGuLiFlightNavigationCookIssue>& OutIssues);
};
