// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Immutable definition for one formal WingmanAcceptanceCatalogV2 run role. */
struct GULISTRIKE_API FGuLiWingmanAcceptanceRoleDefinition
{
	FName RoleId;
	TArray<FName> RequiredGateIds;
};

/**
 * Parsed campaign evidence presented to the fail-closed catalog validator.
 * File/JSON loading stays in the non-shipping runner; this type has no I/O.
 */
struct GULISTRIKE_API FGuLiWingmanAcceptanceRunEvidence
{
	FString RunId;
	FName RoleId;
	int32 CatalogVersion = 0;
	FString CatalogHash;
	FString EvidencePath;
	bool bRunCompleted = false;
	bool bRunPassed = false;
	TMap<FName, int64> GateSampleCounts;
	TMap<FName, int64> InvariantCounts;
};

/**
 * Single source of truth for the 22-role formal campaign. Validation rejects
 * missing/duplicate/extra roles, catalog drift, unknown gates/invariants,
 * empty required samples, and any non-zero invariant.
 */
class GULISTRIKE_API FGuLiWingmanAcceptanceCatalogV2
{
public:
	static constexpr int32 Version = 2;
	static constexpr int32 ExpectedRoleCount = 22;

	static const TArray<FName>& GetCommonRequiredGateIds();
	static const TArray<FName>& GetInvariantKeys();
	static const TArray<FGuLiWingmanAcceptanceRoleDefinition>& GetRoles();

	/** Canonical ASCII payload used for the SHA-256 embedded in every run. */
	static FString BuildCanonicalCatalog();
	/** Platform-independent SHA-256 used by headless Editor/Game campaign tools. */
	static FString ComputeSha256Hex(FStringView Utf8Text);
	static FString GetCatalogHashSha256();

	static const FGuLiWingmanAcceptanceRoleDefinition* FindRole(FName RoleId);
	/** Validates one role independently; campaign orchestration adds exact-set/uniqueness checks. */
	static bool ValidateRun(
		const FGuLiWingmanAcceptanceRunEvidence& Run,
		TArray<FString>& OutErrors);
	static bool ValidateCampaign(
		TConstArrayView<FGuLiWingmanAcceptanceRunEvidence> Runs,
		TArray<FString>& OutErrors);
};
