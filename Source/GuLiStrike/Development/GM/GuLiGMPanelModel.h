// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

class AGuLiBattlePlayerController;
class UWorld;

namespace GuLiGMPanel
{
	inline constexpr int32 ItemsPerPage = 10;
	inline constexpr double ConfirmationLifetimeSeconds = 5.0;


	enum class EConfirmationAction : uint8
	{
		None,
		ResetAllRuntime,
		BenchmarkTenThousand
	};

	enum class EToggleAction : uint8
	{
		None,
		Open,
		Close
	};

	struct GULISTRIKE_API FInputRestorePolicy
	{
		bool bGameAndUI = false;
		bool bShowCursor = false;
		bool bEnableClickEvents = false;
		bool bEnableMouseOverEvents = false;
	};

	struct GULISTRIKE_API FPanelLayout
	{
		float Width = 0.0f;
		float Height = 0.0f;
		float RightMargin = 0.0f;
	};

	struct GULISTRIKE_API FAccessPolicy
	{
		bool bCanReadRuntimeRegistry = false;
		bool bCanReadSkillProfiles = false;
		bool bCanMutateAuthorityState = false;
		bool bCanReadAuthorityDiagnostics = false;
		bool bCanRunLocalBenchmark = false;
		bool bCanToggleLocalCameraDebug = false;
		FString AuthorityUnavailableReason;
	};

	struct GULISTRIKE_API FConfirmationGate
	{
		EConfirmationAction ArmedAction = EConfirmationAction::None;
		double ExpiresAtSeconds = 0.0;

		/** Returns true only when an already-armed action is confirmed within its deadline. */
		bool ConsumeOrArm(EConfirmationAction Action, double NowSeconds);
		bool IsArmed(EConfirmationAction Action, double NowSeconds) const;
		void Cancel();
	};

	struct GULISTRIKE_API FActionResult
	{
		bool bSuccess = false;
		bool bPending = false;
		int32 AppliedCount = 0;
		FString Summary;
		FString Detail;

		static FActionResult Success(
			FString InSummary,
			FString InDetail = FString(),
			bool bInPending = false,
			int32 InAppliedCount = 0);
		static FActionResult Failure(FString InSummary, FString InDetail = FString());
	};

	struct GULISTRIKE_API FRuntimeTuningRow
	{
		FName Key = NAME_None;
		double Minimum = 0.0;
		double Maximum = 0.0;
		double Baseline = 0.0;
		double Effective = 0.0;
		FString Source;
		bool bIntegral = false;
		int32 AppliedInstanceCount = 0;

		FString ToSearchText() const;
		FString ToDisplayText() const;
	};

	struct GULISTRIKE_API FSkillProfileRow
	{
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		uint16 UnitTypeId = 0;
		FName SlotId = NAME_None;
		FName SkillId = NAME_None;
		FName ExecutorId = NAME_None;
		float Damage = 0.0f;
		float AttackRatePerSecond = 0.0f;
		float RangeCentimeters = 0.0f;
		uint32 Revision = 0u;

		FString ToSearchText() const;
		FString ToDisplayText() const;
	};


	GULISTRIKE_API int32 GetPageCount(int32 ItemCount);
	GULISTRIKE_API int32 ClampPageIndex(int32 RequestedPageIndex, int32 ItemCount);
	GULISTRIKE_API FPanelLayout CalculatePanelLayout(const FVector2D& ViewportSize);
	GULISTRIKE_API FAccessPolicy ResolveAccessPolicy(ENetMode NetMode, bool bHasGameWorld = true);
	GULISTRIKE_API EToggleAction ResolveToggleAction(bool bIsLocalController, bool bHasViewportContext, bool bPanelOpen);
	GULISTRIKE_API FInputRestorePolicy ResolveInputRestorePolicy(bool bCommanderRole);
	GULISTRIKE_API const TCHAR* LexToString(EGuLiTeam Team);

	/** Typed, World-scoped adapter used by Slate. It never dispatches console strings or RPCs. */
	class GULISTRIKE_API FModel
	{
	public:
		explicit FModel(AGuLiBattlePlayerController* InController = nullptr);

		void SetController(AGuLiBattlePlayerController* InController);
		AGuLiBattlePlayerController* GetController() const;
		UWorld* GetWorld() const;
		FAccessPolicy GetAccessPolicy() const;
		FString DescribeContext() const;

		TArray<FRuntimeTuningRow> QueryRuntimeTuning(FString& OutUnavailableReason) const;
		FActionResult GetRuntimeTuning(const FString& Key) const;
		FActionResult SetRuntimeTuning(const FString& Key, const FString& Value) const;
		FActionResult ResetRuntimeTuning(const FString& KeyOrAll) const;

		TArray<FSkillProfileRow> QuerySkillProfiles(FString& OutUnavailableReason) const;

		FActionResult ExplainSkill(EGuLiTeam Team, const FString& UnitTypeId, const FString& SlotId) const;

		FActionResult InspectSkillSoldier(const FString& SoldierId) const;

		FActionResult RunBenchmark(int32 PopulationCount, const FString& Steps) const;

		FActionResult InspectNavigationSoldier(const FString& SoldierId) const;
		FActionResult QueryNavigationStats() const;
		FActionResult QueryStrongholds() const;
		FActionResult QueryLastMove(const FString& OptionalCohortId) const;
		FActionResult SetCameraDebugEnabled(bool bEnabled) const;
		bool IsCameraDebugEnabled() const;

	private:
		TWeakObjectPtr<AGuLiBattlePlayerController> Controller;
	};
}
