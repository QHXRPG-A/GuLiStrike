#pragma once

#include "CoreMinimal.h"

class ARecastNavMesh;
struct FNavDataConfig;

/** Authored Commander generation contract; used only before editor baking/validation. */
namespace GuLiCommanderGroundNavigationProfile
{
	inline constexpr float StepHeightCentimeters = 20.0f;

	bool ValidateAgent(const FNavDataConfig& Agent, FString& Error);
	bool Matches(const ARecastNavMesh& Navigation, const FNavDataConfig& Agent);
	void Apply(ARecastNavMesh& Navigation, const FNavDataConfig& Agent);
	bool Validate(const ARecastNavMesh& Navigation, const FNavDataConfig& Agent, FString& Error);
	bool MatchesCommanderMapQueryBudget(const ARecastNavMesh& Navigation);
	void ApplyCommanderMapQueryBudget(ARecastNavMesh& Navigation);
	bool MatchesPresentation(const ARecastNavMesh& Navigation, bool bCommander);
	void ApplyPresentation(ARecastNavMesh& Navigation, bool bCommander);
}
