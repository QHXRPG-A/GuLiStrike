#pragma once

#include "CoreMinimal.h"

/** Shared presentation policy, not a mesh LOD index or a replicated gameplay state. */
enum class EGuLiCommanderLODLevel : uint8
{
	Full = 0,
	Reduced = 1,
	Minimal = 2
};

enum class EGuLiCommanderLODReason : uint8
{
	DistanceAndScreen,
	MinimumResidence,
	InvalidBounds,
	NoLocalView,
	OutsideFrustum
};

struct GULISTRIKE_API FGuLiCommanderLODSettings
{
	float NearEnterDistance = 7200.f;
	float NearHoldDistance = 8800.f;
	float NearEnterScreen = .012f;
	float NearHoldScreen = .008f;
	float MidEnterDistance = 14000.f;
	float MidHoldDistance = 16000.f;
	float MidEnterScreen = .003f;
	float MidHoldScreen = .002f;
	float MinimumResidenceSeconds = .35f;

	bool Validate(FString& Error) const;
	EGuLiCommanderLODLevel Classify(double Distance, float Screen,
		TOptional<EGuLiCommanderLODLevel> CurrentLevel) const;
};

struct GULISTRIKE_API FGuLiCommanderLODQuery
{
	// Include displaced geometry, history and any retiring presentation here.
	FBox Bounds = FBox(ForceInit);
	TOptional<EGuLiCommanderLODLevel> CurrentLevel;
	double LastChangeWorldSeconds = -1;
};

struct GULISTRIKE_API FGuLiCommanderLODDecision
{
	EGuLiCommanderLODLevel TargetLevel = EGuLiCommanderLODLevel::Minimal;
	EGuLiCommanderLODReason Reason = EGuLiCommanderLODReason::NoLocalView;
	double DistanceCentimeters = TNumericLimits<double>::Max();
	float ScreenFraction = 0;
	bool bVisible = false;
	// Also applies to resource-driven changes. Initial allocation/reentry is immediate.
	bool bCanTransition = false;
};

/** Counts objects in the consumer's own batching unit; never combines resource budgets. */
struct GULISTRIKE_API FGuLiCommanderLODConsumerStats
{
	int32 Objects = 0;
	int32 Visible = 0;
	int32 Target[3] = {};
	int32 Applied[3] = {};
	int32 NoView = 0;
	int32 FrustumCulled = 0;
	int32 InvalidBounds = 0;
	int32 DistanceCulled = 0;
	int32 ScreenCulled = 0;
	int32 BudgetDowngraded = 0;
	int32 TransitionPending = 0;
	int32 ResourcesUnavailable = 0;
	uint64 ReportFrame = 0;
};

struct GULISTRIKE_API FGuLiCommanderLODSettingView
{
	FString Key;
	float Baseline = 0;
	float Effective = 0;
	FString Source;
};
