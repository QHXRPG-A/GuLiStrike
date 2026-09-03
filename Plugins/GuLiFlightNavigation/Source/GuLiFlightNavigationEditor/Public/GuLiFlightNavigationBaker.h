#pragma once

#include "CoreMinimal.h"
#include "GuLiFlightNavigationTypes.h"

class UGuLiFlightNavigationData;

/** Deterministic topology builder. The caller owns geometry sampling through IsBlocked. */
class GULIFLIGHTNAVIGATIONEDITOR_API FGuLiFlightNavigationBaker final
{
public:
	static uint64 ComputeSettingsHash(const FGuLiFlightNavBakeSettings& Settings);

	static bool Build(
		const FBox& WorldBounds,
		const FGuLiFlightNavBakeSettings& Settings,
		FName SourceWorldPackage,
		const FString& SourceVolumePath,
		uint32 DefinitionRevision,
		TFunctionRef<bool(const FBox&)> IsBlocked,
		UGuLiFlightNavigationData& OutData,
		FString& OutError);
};
