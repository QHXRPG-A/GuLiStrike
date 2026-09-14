// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Development/GM/GuLiGMPanelModel.h"
#include "Widgets/SCompoundWidget.h"

#if !UE_BUILD_SHIPPING

class AGuLiBattlePlayerController;
class SEditableTextBox;
class STextBlock;
class SVerticalBox;
class SWidgetSwitcher;
class UGuLiArmySkillSubsystem;

/** Non-shipping, per-local-player GM surface. The surrounding viewport widget is visually transparent. */
class SGuLiGMPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGuLiGMPanel) {}
		SLATE_ARGUMENT(AGuLiBattlePlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SGuLiGMPanel() override;

	void HandlePanelOpened();
	void HandlePanelClosed();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		const double InCurrentTime,
		const float InDeltaTime) override;

private:
	enum class ETab : uint8
	{
		Runtime,
		Skills,
		Soldiers,
		Commander
	};

	TSharedRef<SWidget> BuildRuntimeTab();
	TSharedRef<SWidget> BuildSkillsTab();
	TSharedRef<SWidget> BuildSoldiersTab();
	TSharedRef<SWidget> BuildCommanderTab();
	TSharedRef<SWidget> MakeEntryRow(
		const FString& Label,
		TSharedPtr<SEditableTextBox>& OutEntry,
		const FString& InitialText,
		const FString& Hint = FString());
	TSharedRef<SWidget> MakeTabButton(ETab Tab, const FString& Label);
	TSharedRef<SWidget> MakeSectionTitle(const FString& Title, const FString& CommandNames);

	FReply SelectTab(ETab Tab);
	FReply CycleSkillFilterTeam();
	FReply CycleSkillFormTeam();
	FReply CycleSpawnTeam();
	FReply CycleSkillAttribute();
	FReply CycleModifierOperation();
	FReply ToggleBenchmarkPopulation();
	FReply ChangeRuntimePage(int32 Delta);
	FReply ChangeSkillProfilePage(int32 Delta);
	FReply ChangeSkillSourcePage(int32 Delta);

	FReply RuntimeGet();
	FReply RuntimeSet();
	FReply RuntimeReset();
	FReply RuntimeResetAll();
	FReply SkillGet();
	FReply SkillSet();
	FReply SkillReset();
	FReply SkillSource();
	FReply SkillReplace();
	FReply SkillRemove();
	FReply InspectSkillSoldier();
	FReply SpawnSoldier();
	FReply RunBenchmark();
	FReply InspectNavigationSoldier();
	FReply RefreshNavigationStats();
	FReply RefreshLastMove();
	FReply ToggleCameraDebug();

	void HandleRuntimeFilterChanged(const FText& Text);
	void HandleProfileFilterChanged(const FText& Text);
	void HandleUnitFilterChanged(const FText& Text);
	void HandleSlotFilterChanged(const FText& Text);
	void HandleSourceFilterChanged(const FText& Text);
	void HandleFormTextChanged(const FText& Text);
	void HandleSkillLoadoutCommitted(uint32 Revision);
	void ResetViewStateForWorld();
	void RefreshAll();
	void RefreshRuntime();
	void RefreshSkills();
	void RebuildRuntimeRows();
	void RebuildSkillProfileRows();
	void RebuildSkillSourceRows();
	void SelectRuntimeRow(int32 Index);
	void SelectSkillProfileRow(int32 Index);
	void SelectSkillSourceRow(int32 Index);
	void BindSkillDelegate();
	void UnbindSkillDelegate();
	void CancelConfirmation();
	void PublishResult(GuLiGMPanel::FActionResult Result);

	FText GetContextText() const;
	FText GetAccessText() const;
	FText GetResultText() const;
	FText GetRuntimePageText() const;
	FText GetSkillProfilePageText() const;
	FText GetSkillSourcePageText() const;
	FText GetSkillFilterTeamText() const;
	FText GetSkillFormTeamText() const;
	FText GetSpawnTeamText() const;
	FText GetSkillAttributeText() const;
	FText GetModifierOperationText() const;
	FText GetBenchmarkPopulationText() const;
	FText GetCameraDebugText() const;
	FSlateColor GetResultColor() const;
	FSlateColor GetAccessColor() const;

	GuLiGMPanel::FModel Model;
	GuLiGMPanel::FPanelLayout Layout { 720.0f, 760.0f, 20.0f };
	GuLiGMPanel::FConfirmationGate ConfirmationGate;
	GuLiGMPanel::FActionResult LastResult;
	ETab CurrentTab = ETab::Runtime;
	bool bPanelOpen = false;
	TWeakObjectPtr<UWorld> StateWorld;

	TWeakObjectPtr<UGuLiArmySkillSubsystem> BoundSkillSubsystem;
	FDelegateHandle SkillCommittedHandle;

	TSharedPtr<SWidgetSwitcher> TabSwitcher;
	TSharedPtr<SVerticalBox> RuntimeRowsBox;
	TSharedPtr<SVerticalBox> SkillProfileRowsBox;
	TSharedPtr<SVerticalBox> SkillSourceRowsBox;

	TSharedPtr<SEditableTextBox> RuntimeFilterEntry;
	TSharedPtr<SEditableTextBox> RuntimeKeyEntry;
	TSharedPtr<SEditableTextBox> RuntimeValueEntry;
	TSharedPtr<SEditableTextBox> ProfileFilterEntry;
	TSharedPtr<SEditableTextBox> UnitFilterEntry;
	TSharedPtr<SEditableTextBox> SlotFilterEntry;
	TSharedPtr<SEditableTextBox> SourceFilterEntry;
	TSharedPtr<SEditableTextBox> SkillUnitEntry;
	TSharedPtr<SEditableTextBox> SkillSlotEntry;
	TSharedPtr<SEditableTextBox> SkillValueEntry;
	TSharedPtr<SEditableTextBox> SkillLabelEntry;
	TSharedPtr<SEditableTextBox> RequiredSkillEntry;
	TSharedPtr<SEditableTextBox> RequiredTagEntry;
	TSharedPtr<SEditableTextBox> ReplacementSkillEntry;
	TSharedPtr<SEditableTextBox> ReplacementPriorityEntry;
	TSharedPtr<SEditableTextBox> SkillSoldierIdEntry;
	TSharedPtr<SEditableTextBox> SpawnUnitEntry;
	TSharedPtr<SEditableTextBox> SpawnXEntry;
	TSharedPtr<SEditableTextBox> SpawnYEntry;
	TSharedPtr<SEditableTextBox> SpawnZEntry;
	TSharedPtr<SEditableTextBox> BenchmarkStepsEntry;
	TSharedPtr<SEditableTextBox> NavigationSoldierIdEntry;
	TSharedPtr<SEditableTextBox> CohortIdEntry;

	TArray<GuLiGMPanel::FRuntimeTuningRow> RuntimeRows;
	TArray<int32> FilteredRuntimeIndices;
	TArray<GuLiGMPanel::FSkillProfileRow> SkillProfileRows;
	TArray<int32> FilteredSkillProfileIndices;
	TArray<GuLiGMPanel::FSkillSourceRow> SkillSourceRows;
	TArray<int32> FilteredSkillSourceIndices;
	FString RuntimeUnavailableReason;
	FString SkillsUnavailableReason;
	FString SourcesUnavailableReason;
	FString RuntimeFilter;
	FString ProfileFilter;
	FString UnitFilter;
	FString SlotFilter;
	FString SourceFilter;
	int32 RuntimePage = 0;
	int32 SkillProfilePage = 0;
	int32 SkillSourcePage = 0;
	EGuLiTeam SkillFilterTeam = EGuLiTeam::Unassigned;
	EGuLiTeam SkillFormTeam = EGuLiTeam::Red;
	EGuLiTeam SpawnTeam = EGuLiTeam::Red;
	GuLiGMPanel::ESkillAttribute SkillAttribute = GuLiGMPanel::ESkillAttribute::Damage;
	GuLiGMPanel::EModifierOperation ModifierOperation = GuLiGMPanel::EModifierOperation::Flat;
	int32 BenchmarkPopulation = 500;
};

#endif
