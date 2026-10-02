#include "GuLiCommanderGroundNavigationProfile.h"

#include "AI/Navigation/NavQueryFilter.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/UnrealType.h"

namespace GuLiCommanderGroundNavigationProfile
{
	namespace
	{
		constexpr float CellSizesCentimeters[] = { 19.0f, 19.0f, 9.5f };
		constexpr float CellHeightCentimeters = 2.0f;
		constexpr float MaximumSlopeDegrees = 44.0f;
		constexpr float DrawOffsetCentimeters = 50.0f;
		constexpr uint32 CommanderMapSearchNodes = 16384;
		constexpr auto LedgeFilter = ENavigationLedgeSlopeFilterMode::UseStepHeightFromAgentMaxSlope;
		static_assert(UE_ARRAY_COUNT(CellSizesCentimeters) == static_cast<uint8>(ENavigationDataResolution::MAX));

	}

	bool ValidateAgent(const FNavDataConfig& Agent, FString& Error)
	{
		if (Agent.Name != GuLiCommanderNavigationPolicy::GetRequiredAgentName()
			|| !FMath::IsNearlyEqual(Agent.AgentRadius, GuLiCommanderNavigationPolicy::RequiredAgentRadiusCentimeters)
			|| !FMath::IsNearlyEqual(Agent.AgentStepHeight, StepHeightCentimeters))
		{
			Error = FString::Printf(TEXT("CommanderSoldier SupportedAgent must use radius 150 cm and step 20 cm; configured radius=%.3f step=%.3f. Reload the updated project configuration."),
				Agent.AgentRadius, Agent.AgentStepHeight);
			return false;
		}
		return true;
	}

	bool Matches(const ARecastNavMesh& Navigation, const FNavDataConfig& Agent)
	{
		const FNavDataConfig& Actual = Navigation.GetConfig();
		if (!FMath::IsNearlyEqual(Actual.AgentRadius, Agent.AgentRadius)
			|| !FMath::IsNearlyEqual(Actual.AgentHeight, Agent.AgentHeight)
			|| !FMath::IsNearlyEqual(Actual.AgentStepHeight, StepHeightCentimeters)
			|| !FMath::IsNearlyEqual(Navigation.AgentMaxSlope, MaximumSlopeDegrees)
			|| Navigation.LedgeSlopeFilterMode != LedgeFilter)
			return false;
		for (uint8 Index = 0; Index < static_cast<uint8>(ENavigationDataResolution::MAX); ++Index)
		{
			const auto Resolution = static_cast<ENavigationDataResolution>(Index);
			if (!FMath::IsNearlyEqual(Navigation.GetCellSize(Resolution), CellSizesCentimeters[Index])
				|| !FMath::IsNearlyEqual(Navigation.GetCellHeight(Resolution), CellHeightCentimeters)
				|| !FMath::IsNearlyEqual(Navigation.GetAgentMaxStepHeight(Resolution), StepHeightCentimeters))
				return false;
		}
		return true;
	}

	void Apply(ARecastNavMesh& Navigation, const FNavDataConfig& Agent)
	{
		Navigation.SetConfig(Agent);
		for (uint8 Index = 0; Index < static_cast<uint8>(ENavigationDataResolution::MAX); ++Index)
		{
			const auto Resolution = static_cast<ENavigationDataResolution>(Index);
			Navigation.SetCellSize(Resolution, CellSizesCentimeters[Index]);
			Navigation.SetCellHeight(Resolution, CellHeightCentimeters);
			Navigation.SetAgentMaxStepHeight(Resolution, StepHeightCentimeters);
		}
		Navigation.AgentMaxSlope = MaximumSlopeDegrees;
		Navigation.LedgeSlopeFilterMode = LedgeFilter;
	}

	bool Validate(const ARecastNavMesh& Navigation, const FNavDataConfig& Agent, FString& Error)
	{
		if (!ValidateAgent(Agent, Error)) return false;
		if (Matches(Navigation, Agent)) return true;
		Error = TEXT("CommanderSoldier generation profile mismatch: expected slope 44, slope-based ledge filter, cell XY 19/19/9.5 cm, cell Z 2 cm, and step 20 cm. Run navigation Prepare to migrate and rebuild.");
		return false;
	}

	bool MatchesPresentation(const ARecastNavMesh& Navigation, const bool bCommander)
	{
		return Navigation.IsDrawingEnabled() == bCommander
			&& (!bCommander || FMath::IsNearlyEqual(Navigation.DrawOffset, DrawOffsetCentimeters));
	}

	bool MatchesCommanderMapQueryBudget(const ARecastNavMesh& Navigation)
	{
		return FMath::IsNearlyEqual(Navigation.DefaultMaxSearchNodes, static_cast<float>(CommanderMapSearchNodes))
			&& Navigation.GetDefaultQueryFilter()->GetMaxSearchNodes() == CommanderMapSearchNodes;
	}

	void ApplyCommanderMapQueryBudget(ARecastNavMesh& Navigation)
	{
		// A step-height config change can replace the map's NavData during engine registration.
		// Restore its authored budget and the live filter, rather than inheriting class defaults.
		Navigation.DefaultMaxSearchNodes = static_cast<float>(CommanderMapSearchNodes);
		Navigation.RecreateDefaultFilter();
	}

	void ApplyPresentation(ARecastNavMesh& Navigation, const bool bCommander)
	{
		static const FBoolProperty* EnableDrawing = CastFieldChecked<FBoolProperty>(
			ANavigationData::StaticClass()->FindPropertyByName(TEXT("bEnableDrawing")));
		EnableDrawing->SetPropertyValue_InContainer(&Navigation, bCommander);
		if (bCommander) Navigation.DrawOffset = DrawOffsetCentimeters;
		Navigation.UpdateDrawing();
	}
}
