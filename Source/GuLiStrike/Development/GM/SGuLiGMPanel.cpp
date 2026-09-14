// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GM/SGuLiGMPanel.h"

#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "GuLiStrike.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Input/Reply.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#if !UE_BUILD_SHIPPING

namespace
{
	constexpr float RowGap = 4.0f;

	FString ReadEntry(const TSharedPtr<SEditableTextBox>& Entry)
	{
		return Entry.IsValid() ? Entry->GetText().ToString() : FString();
	}

	FText Text(const FString& Value)
	{
		return FText::FromString(Value);
	}

	FText Text(const TCHAR* Value)
	{
		return FText::FromString(Value);
	}

	bool Matches(const FString& Haystack, const FString& Filter)
	{
		return Filter.IsEmpty() || Haystack.Contains(Filter, ESearchCase::IgnoreCase);
	}
}

void SGuLiGMPanel::Construct(const FArguments& InArgs)
{
	Model.SetController(InArgs._Controller);
	LastResult = GuLiGMPanel::FActionResult::Success(
		TEXT("GM 面板就绪"),
		TEXT("F10 呼出/关闭，打开时 Esc 关闭。所有列表每页 10 条。"));

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBox)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding_Lambda([this]() { return FMargin(0.0f, 0.0f, Layout.RightMargin, 0.0f); })
			[
				SNew(SBox)
				.WidthOverride_Lambda([this]() { return Layout.Width; })
				.HeightOverride_Lambda([this]() { return Layout.Height; })
				[
					SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
				.BorderBackgroundColor(FLinearColor(0.018f, 0.025f, 0.038f, 0.97f))
				.Padding(FMargin(12.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(Text(TEXT("GuLiStrike GM 分页面板")))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
								.ColorAndOpacity(FLinearColor(0.85f, 0.93f, 1.0f))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 2.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(this, &SGuLiGMPanel::GetContextText)
								.ColorAndOpacity(FLinearColor(0.55f, 0.65f, 0.72f))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Top)
						[
							SNew(SButton)
							.Text(Text(TEXT("关闭  Esc / F10")))
							.OnClicked_Lambda([this]()
							{
								if (AGuLiBattlePlayerController* Controller = Model.GetController())
								{
									Controller->CloseGMPanel();
								}
								return FReply::Handled();
							})
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 7.0f, 0.0f, 7.0f)
					[
						SNew(STextBlock)
						.Text(this, &SGuLiGMPanel::GetAccessText)
						.ColorAndOpacity(this, &SGuLiGMPanel::GetAccessColor)
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f)[MakeTabButton(ETab::Runtime, TEXT("运行时调参"))]
						+ SHorizontalBox::Slot().FillWidth(1.0f)[MakeTabButton(ETab::Skills, TEXT("技能"))]
						+ SHorizontalBox::Slot().FillWidth(1.0f)[MakeTabButton(ETab::Soldiers, TEXT("士兵工具"))]
						+ SHorizontalBox::Slot().FillWidth(1.0f)[MakeTabButton(ETab::Commander, TEXT("指挥官诊断"))]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 8.0f)
					[
						SAssignNew(TabSwitcher, SWidgetSwitcher)
						.WidgetIndex(static_cast<int32>(CurrentTab))
						+ SWidgetSwitcher::Slot()[BuildRuntimeTab()]
						+ SWidgetSwitcher::Slot()[BuildSkillsTab()]
						+ SWidgetSwitcher::Slot()[BuildSoldiersTab()]
						+ SWidgetSwitcher::Slot()[BuildCommanderTab()]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
						.BorderBackgroundColor(FLinearColor(0.03f, 0.04f, 0.055f, 1.0f))
						.Padding(8.0f)
						[
							SNew(SBox)
							.MaxDesiredHeight(122.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SNew(STextBlock)
									.Text(this, &SGuLiGMPanel::GetResultText)
									.ColorAndOpacity(this, &SGuLiGMPanel::GetResultColor)
									.AutoWrapText(true)
								]
							]
						]
					]
				]
				]
			]
		]
	];
}

SGuLiGMPanel::~SGuLiGMPanel()
{
	UnbindSkillDelegate();
}

void SGuLiGMPanel::HandlePanelOpened()
{
	bPanelOpen = true;
	CancelConfirmation();
	if (StateWorld.Get() != Model.GetWorld())
	{
		StateWorld = Model.GetWorld();
		ResetViewStateForWorld();
	}
	RefreshAll();
	BindSkillDelegate();
}

void SGuLiGMPanel::HandlePanelClosed()
{
	bPanelOpen = false;
	CancelConfirmation();
	UnbindSkillDelegate();
}

FReply SGuLiGMPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F10)
	{
		if (AGuLiBattlePlayerController* Controller = Model.GetController())
		{
			Controller->CloseGMPanel();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SGuLiGMPanel::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Editable text boxes may consume Escape themselves. Handle the tunnel phase so close is unconditional.
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F10)
	{
		if (AGuLiBattlePlayerController* Controller = Model.GetController())
		{
			Controller->CloseGMPanel();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

void SGuLiGMPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Layout = GuLiGMPanel::CalculatePanelLayout(AllottedGeometry.GetLocalSize());
	if (bPanelOpen && StateWorld.Get() != Model.GetWorld())
	{
		UnbindSkillDelegate();
		StateWorld = Model.GetWorld();
		ResetViewStateForWorld();
		RefreshAll();
		BindSkillDelegate();
	}
}

void SGuLiGMPanel::ResetViewStateForWorld()
{
	CurrentTab = ETab::Runtime;
	RuntimePage = 0;
	SkillProfilePage = 0;
	SkillSourcePage = 0;
	RuntimeFilter.Reset();
	ProfileFilter.Reset();
	UnitFilter.Reset();
	SlotFilter.Reset();
	SourceFilter.Reset();
	SkillFilterTeam = EGuLiTeam::Unassigned;
	SkillFormTeam = EGuLiTeam::Red;
	SpawnTeam = EGuLiTeam::Red;
	SkillAttribute = GuLiGMPanel::ESkillAttribute::Damage;
	ModifierOperation = GuLiGMPanel::EModifierOperation::Flat;
	BenchmarkPopulation = 500;
	if (TabSwitcher.IsValid()) TabSwitcher->SetActiveWidgetIndex(static_cast<int32>(CurrentTab));
	if (RuntimeFilterEntry.IsValid()) RuntimeFilterEntry->SetText(FText::GetEmpty());
	if (ProfileFilterEntry.IsValid()) ProfileFilterEntry->SetText(FText::GetEmpty());
	if (UnitFilterEntry.IsValid()) UnitFilterEntry->SetText(FText::GetEmpty());
	if (SlotFilterEntry.IsValid()) SlotFilterEntry->SetText(FText::GetEmpty());
	if (SourceFilterEntry.IsValid()) SourceFilterEntry->SetText(FText::GetEmpty());
}

TSharedRef<SWidget> SGuLiGMPanel::MakeSectionTitle(const FString& Title, const FString& CommandNames)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Text(Title))
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(FLinearColor(0.75f, 0.88f, 1.0f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f, 0.0f, 5.0f)
		[
			SNew(STextBlock)
			.Text(Text(CommandNames))
			.ColorAndOpacity(FLinearColor(0.45f, 0.55f, 0.62f))
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> SGuLiGMPanel::MakeEntryRow(
	const FString& Label,
	TSharedPtr<SEditableTextBox>& OutEntry,
	const FString& InitialText,
	const FString& Hint)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(116.0f)
			[
				SNew(STextBlock).Text(Text(Label))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SAssignNew(OutEntry, SEditableTextBox)
			.Text(Text(InitialText))
			.HintText(Text(Hint))
			.SelectAllTextWhenFocused(true)
			.OnTextChanged(this, &SGuLiGMPanel::HandleFormTextChanged)
		];
}

TSharedRef<SWidget> SGuLiGMPanel::MakeTabButton(const ETab Tab, const FString& Label)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.Text(Text(Label))
		.ButtonColorAndOpacity_Lambda([this, Tab]()
		{
			return CurrentTab == Tab
				? FLinearColor(0.08f, 0.30f, 0.52f, 1.0f)
				: FLinearColor(0.08f, 0.09f, 0.12f, 1.0f);
		})
		.OnClicked(this, &SGuLiGMPanel::SelectTab, Tab);
}

TSharedRef<SWidget> SGuLiGMPanel::BuildRuntimeTab()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("动态白名单"), TEXT("gs.GM.List / Get / Set / Reset"))]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, RowGap)
			[
				SAssignNew(RuntimeFilterEntry, SEditableTextBox)
				.HintText(Text(TEXT("过滤 key / 来源")))
				.OnTextChanged(this, &SGuLiGMPanel::HandleRuntimeFilterChanged)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(RuntimeRowsBox, SVerticalBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Key"), RuntimeKeyEntry, TEXT(""), TEXT("点击上方条目自动填入"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Value"), RuntimeValueEntry, TEXT(""), TEXT("不会静默钳制"))]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("Get"))).OnClicked(this, &SGuLiGMPanel::RuntimeGet)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanReadRuntimeRegistry; })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("Set"))).OnClicked(this, &SGuLiGMPanel::RuntimeSet)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("Reset 单项"))).OnClicked(this, &SGuLiGMPanel::RuntimeReset)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(Text(TEXT("Reset All（二次确认）"))).OnClicked(this, &SGuLiGMPanel::RuntimeResetAll)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
			]
		];
}

TSharedRef<SWidget> SGuLiGMPanel::BuildSkillsTab()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("已解析技能"), TEXT("gs.GM.Skill.List / Get（数值来源在 Get 结果中展开）"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(this, &SGuLiGMPanel::GetSkillFilterTeamText).OnClicked(this, &SGuLiGMPanel::CycleSkillFilterTeam)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SAssignNew(ProfileFilterEntry, SEditableTextBox).HintText(Text(TEXT("关键词：Skill / Executor")))
					.OnTextChanged(this, &SGuLiGMPanel::HandleProfileFilterChanged)
				]
				+ SHorizontalBox::Slot().FillWidth(0.45f).Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SAssignNew(UnitFilterEntry, SEditableTextBox).HintText(Text(TEXT("Unit")))
					.OnTextChanged(this, &SGuLiGMPanel::HandleUnitFilterChanged)
				]
				+ SHorizontalBox::Slot().FillWidth(0.65f)
				[
					SAssignNew(SlotFilterEntry, SEditableTextBox).HintText(Text(TEXT("Slot")))
					.OnTextChanged(this, &SGuLiGMPanel::HandleSlotFilterChanged)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[SAssignNew(SkillProfileRowsBox, SVerticalBox)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("来源账本"), TEXT("gs.GM.Skill.Source / Replace / Remove"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
			[
				SAssignNew(SourceFilterEntry, SEditableTextBox)
				.HintText(Text(TEXT("过滤 Label / SourceId")))
				.OnTextChanged(this, &SGuLiGMPanel::HandleSourceFilterChanged)
			]
			+ SVerticalBox::Slot().AutoHeight()[SAssignNew(SkillSourceRowsBox, SVerticalBox)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(116.0f)[SNew(STextBlock).Text(Text(TEXT("队伍 / 属性 / 算子")))]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(this, &SGuLiGMPanel::GetSkillFormTeamText).OnClicked(this, &SGuLiGMPanel::CycleSkillFormTeam)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(this, &SGuLiGMPanel::GetSkillAttributeText).OnClicked(this, &SGuLiGMPanel::CycleSkillAttribute)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(this, &SGuLiGMPanel::GetModifierOperationText).OnClicked(this, &SGuLiGMPanel::CycleModifierOperation)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("UnitTypeId"), SkillUnitEntry, TEXT("1"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("SlotId"), SkillSlotEntry, TEXT("BasicAttack"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Value"), SkillValueEntry, TEXT(""), TEXT("Set 要求非负；Source 允许有限负数"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Label"), SkillLabelEntry, TEXT("PanelSource"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Required Skill"), RequiredSkillEntry, TEXT("*"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Required Tag"), RequiredTagEntry, TEXT("*"), TEXT("未知 GameplayTag 会被拒绝"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Replace Skill"), ReplacementSkillEntry, TEXT(""))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Priority"), ReplacementPriorityEntry, TEXT("0"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)[SNew(SButton).Text(Text(TEXT("Get"))).OnClicked(this, &SGuLiGMPanel::SkillGet)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("Set"))).OnClicked(this, &SGuLiGMPanel::SkillSet)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("Reset"))).OnClicked(this, &SGuLiGMPanel::SkillReset)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("Source"))).OnClicked(this, &SGuLiGMPanel::SkillSource)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("Replace"))).OnClicked(this, &SGuLiGMPanel::SkillReplace)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(Text(TEXT("Remove"))).OnClicked(this, &SGuLiGMPanel::SkillRemove)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
				]
			]
		];
}

TSharedRef<SWidget> SGuLiGMPanel::BuildSoldiersTab()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("单兵检查"), TEXT("gs.GM.Skill.Soldier"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("SoldierId"), SkillSoldierIdEntry, TEXT("1"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).Text(Text(TEXT("检查单兵"))).OnClicked(this, &SGuLiGMPanel::InspectSkillSoldier)
				.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanReadAuthorityDiagnostics; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("生成调试士兵"), TEXT("gs.GM.Skill.Spawn"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SBox).WidthOverride(116.0f)[SNew(STextBlock).Text(Text(TEXT("Team")))]]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(this, &SGuLiGMPanel::GetSpawnTeamText).OnClicked(this, &SGuLiGMPanel::CycleSpawnTeam)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("UnitTypeId"), SpawnUnitEntry, TEXT("1"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("X cm"), SpawnXEntry, TEXT("0"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Y cm"), SpawnYEntry, TEXT("0"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("Z cm"), SpawnZEntry, TEXT("0"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).Text(Text(TEXT("生成"))).OnClicked(this, &SGuLiGMPanel::SpawnSoldier)
				.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanMutateAuthorityState; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("本地无 World 战斗基准"), TEXT("gs.GM.Skill.Bench（不包含渲染/导航/网络/World）"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SButton).Text(this, &SGuLiGMPanel::GetBenchmarkPopulationText).OnClicked(this, &SGuLiGMPanel::ToggleBenchmarkPopulation)]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[MakeEntryRow(TEXT("Steps 1..1800"), BenchmarkStepsEntry, TEXT("300"))]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).Text(Text(TEXT("运行 Bench（10000 需二次确认）"))).OnClicked(this, &SGuLiGMPanel::RunBenchmark)
				.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanRunLocalBenchmark; })
			]
		];
}

TSharedRef<SWidget> SGuLiGMPanel::BuildCommanderTab()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("权威导航诊断（仅手动刷新）"), TEXT("gs.GM.Commander.Nav.Soldier / Stats / LastMove"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("SoldierId"), NavigationSoldierIdEntry, TEXT("1"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[MakeEntryRow(TEXT("CohortId"), CohortIdEntry, TEXT(""), TEXT("可留空"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("刷新 Soldier"))).OnClicked(this, &SGuLiGMPanel::InspectNavigationSoldier)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanReadAuthorityDiagnostics; })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, RowGap, 0.0f)
				[
					SNew(SButton).Text(Text(TEXT("刷新 Stats"))).OnClicked(this, &SGuLiGMPanel::RefreshNavigationStats)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanReadAuthorityDiagnostics; })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(Text(TEXT("刷新 LastMove"))).OnClicked(this, &SGuLiGMPanel::RefreshLastMove)
					.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanReadAuthorityDiagnostics; })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[MakeSectionTitle(TEXT("本地相机调试"), TEXT("gs.GM.Commander.Camera.Debug"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).Text(Text(TEXT("刷新据点 / 建筑 / 空中运输"))).OnClicked_Lambda([this]()
				{ PublishResult(Model.QueryStrongholds()); return FReply::Handled(); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).Text(this, &SGuLiGMPanel::GetCameraDebugText).OnClicked(this, &SGuLiGMPanel::ToggleCameraDebug)
				.IsEnabled_Lambda([this]() { return Model.GetAccessPolicy().bCanToggleLocalCameraDebug; })
			]
		];
}

FReply SGuLiGMPanel::SelectTab(const ETab Tab)
{
	CurrentTab = Tab;
	CancelConfirmation();
	if (TabSwitcher.IsValid())
	{
		TabSwitcher->SetActiveWidgetIndex(static_cast<int32>(Tab));
	}
	return FReply::Handled();
}

FReply SGuLiGMPanel::CycleSkillFilterTeam()
{
	SkillFilterTeam = SkillFilterTeam == EGuLiTeam::Unassigned
		? EGuLiTeam::Red
		: (SkillFilterTeam == EGuLiTeam::Red ? EGuLiTeam::Blue : EGuLiTeam::Unassigned);
	SkillProfilePage = 0;
	SkillSourcePage = 0;
	CancelConfirmation();
	RefreshSkills();
	return FReply::Handled();
}

FReply SGuLiGMPanel::CycleSkillFormTeam()
{
	SkillFormTeam = SkillFormTeam == EGuLiTeam::Red ? EGuLiTeam::Blue : EGuLiTeam::Red;
	CancelConfirmation();
	return FReply::Handled();
}

FReply SGuLiGMPanel::CycleSpawnTeam()
{
	SpawnTeam = SpawnTeam == EGuLiTeam::Red ? EGuLiTeam::Blue : EGuLiTeam::Red;
	CancelConfirmation();
	return FReply::Handled();
}

FReply SGuLiGMPanel::CycleSkillAttribute()
{
	using GuLiGMPanel::ESkillAttribute;
	SkillAttribute = SkillAttribute == ESkillAttribute::Damage
		? ESkillAttribute::AttackRate
		: (SkillAttribute == ESkillAttribute::AttackRate ? ESkillAttribute::Range : ESkillAttribute::Damage);
	CancelConfirmation();
	return FReply::Handled();
}

FReply SGuLiGMPanel::CycleModifierOperation()
{
	using GuLiGMPanel::EModifierOperation;
	ModifierOperation = ModifierOperation == EModifierOperation::Flat ? EModifierOperation::Percent : EModifierOperation::Flat;
	CancelConfirmation();
	return FReply::Handled();
}

FReply SGuLiGMPanel::ToggleBenchmarkPopulation()
{
	BenchmarkPopulation = BenchmarkPopulation == 500 ? 10000 : 500;
	CancelConfirmation();
	return FReply::Handled();
}

FReply SGuLiGMPanel::ChangeRuntimePage(const int32 Delta)
{
	RuntimePage = GuLiGMPanel::ClampPageIndex(RuntimePage + Delta, FilteredRuntimeIndices.Num());
	CancelConfirmation();
	RebuildRuntimeRows();
	return FReply::Handled();
}

FReply SGuLiGMPanel::ChangeSkillProfilePage(const int32 Delta)
{
	SkillProfilePage = GuLiGMPanel::ClampPageIndex(SkillProfilePage + Delta, FilteredSkillProfileIndices.Num());
	CancelConfirmation();
	RebuildSkillProfileRows();
	return FReply::Handled();
}

FReply SGuLiGMPanel::ChangeSkillSourcePage(const int32 Delta)
{
	SkillSourcePage = GuLiGMPanel::ClampPageIndex(SkillSourcePage + Delta, FilteredSkillSourceIndices.Num());
	CancelConfirmation();
	RebuildSkillSourceRows();
	return FReply::Handled();
}

void SGuLiGMPanel::HandleRuntimeFilterChanged(const FText& Value)
{
	RuntimeFilter = Value.ToString().TrimStartAndEnd();
	RuntimePage = 0;
	CancelConfirmation();
	RebuildRuntimeRows();
}

void SGuLiGMPanel::HandleProfileFilterChanged(const FText& Value)
{
	ProfileFilter = Value.ToString().TrimStartAndEnd();
	SkillProfilePage = 0;
	CancelConfirmation();
	RebuildSkillProfileRows();
}

void SGuLiGMPanel::HandleUnitFilterChanged(const FText& Value)
{
	UnitFilter = Value.ToString().TrimStartAndEnd();
	SkillProfilePage = 0;
	CancelConfirmation();
	RebuildSkillProfileRows();
}

void SGuLiGMPanel::HandleSlotFilterChanged(const FText& Value)
{
	SlotFilter = Value.ToString().TrimStartAndEnd();
	SkillProfilePage = 0;
	CancelConfirmation();
	RebuildSkillProfileRows();
}

void SGuLiGMPanel::HandleSourceFilterChanged(const FText& Value)
{
	SourceFilter = Value.ToString().TrimStartAndEnd();
	SkillSourcePage = 0;
	CancelConfirmation();
	RebuildSkillSourceRows();
}

void SGuLiGMPanel::HandleFormTextChanged(const FText& TextValue)
{
	CancelConfirmation();
}

void SGuLiGMPanel::RefreshAll()
{
	RefreshRuntime();
	RefreshSkills();
	const GuLiGMPanel::FAccessPolicy Access = Model.GetAccessPolicy();
	if (!Access.bCanMutateAuthorityState && !Access.AuthorityUnavailableReason.IsEmpty())
	{
		LastResult = GuLiGMPanel::FActionResult::Success(TEXT("只读/本地工具模式"), Access.AuthorityUnavailableReason);
	}
}

void SGuLiGMPanel::RefreshRuntime()
{
	RuntimeRows = Model.QueryRuntimeTuning(RuntimeUnavailableReason);
	RebuildRuntimeRows();
}

void SGuLiGMPanel::RefreshSkills()
{
	SkillProfileRows = Model.QuerySkillProfiles(SkillsUnavailableReason);
	SkillSourceRows.Reset();
	SourcesUnavailableReason.Reset();
	if (SkillFilterTeam == EGuLiTeam::Unassigned)
	{
		FString RedReason;
		FString BlueReason;
		SkillSourceRows.Append(Model.QuerySkillSources(EGuLiTeam::Red, RedReason));
		SkillSourceRows.Append(Model.QuerySkillSources(EGuLiTeam::Blue, BlueReason));
		SourcesUnavailableReason = !RedReason.IsEmpty() ? RedReason : BlueReason;
	}
	else
	{
		SkillSourceRows = Model.QuerySkillSources(SkillFilterTeam, SourcesUnavailableReason);
	}
	SkillSourceRows.Sort([](const GuLiGMPanel::FSkillSourceRow& A, const GuLiGMPanel::FSkillSourceRow& B)
	{
		if (A.Team != B.Team)
		{
			return static_cast<uint8>(A.Team) < static_cast<uint8>(B.Team);
		}
		const int32 LabelOrder = A.Label.Compare(B.Label, ESearchCase::IgnoreCase);
		return LabelOrder != 0 ? LabelOrder < 0 : A.SourceId.ToString().Compare(B.SourceId.ToString()) < 0;
	});
	RebuildSkillProfileRows();
	RebuildSkillSourceRows();
}

void SGuLiGMPanel::RebuildRuntimeRows()
{
	FilteredRuntimeIndices.Reset();
	for (int32 Index = 0; Index < RuntimeRows.Num(); ++Index)
	{
		if (Matches(RuntimeRows[Index].ToSearchText(), RuntimeFilter))
		{
			FilteredRuntimeIndices.Add(Index);
		}
	}
	RuntimePage = GuLiGMPanel::ClampPageIndex(RuntimePage, FilteredRuntimeIndices.Num());
	if (!RuntimeRowsBox.IsValid())
	{
		return;
	}
	RuntimeRowsBox->ClearChildren();
	if (!RuntimeUnavailableReason.IsEmpty())
	{
		RuntimeRowsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(Text(RuntimeUnavailableReason)).AutoWrapText(true)];
	}
	else if (FilteredRuntimeIndices.IsEmpty())
	{
		RuntimeRowsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(Text(TEXT("无匹配调参。")))];
	}
	const int32 Begin = RuntimePage * GuLiGMPanel::ItemsPerPage;
	const int32 End = FMath::Min(Begin + GuLiGMPanel::ItemsPerPage, FilteredRuntimeIndices.Num());
	for (int32 Position = Begin; Position < End; ++Position)
	{
		const int32 Index = FilteredRuntimeIndices[Position];
		RuntimeRowsBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
		[
			SNew(SButton)
			.HAlign(HAlign_Left)
			.OnClicked_Lambda([this, Index]() { SelectRuntimeRow(Index); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Text(RuntimeRows[Index].ToDisplayText())).AutoWrapText(true)
			]
		];
	}
	RuntimeRowsBox->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Text(TEXT("上一页"))).OnClicked(this, &SGuLiGMPanel::ChangeRuntimePage, -1).IsEnabled(RuntimePage > 0)]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SGuLiGMPanel::GetRuntimePageText)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Text(TEXT("下一页"))).OnClicked(this, &SGuLiGMPanel::ChangeRuntimePage, 1).IsEnabled(RuntimePage + 1 < GuLiGMPanel::GetPageCount(FilteredRuntimeIndices.Num()))]
	];
}

void SGuLiGMPanel::RebuildSkillProfileRows()
{
	FilteredSkillProfileIndices.Reset();
	for (int32 Index = 0; Index < SkillProfileRows.Num(); ++Index)
	{
		const GuLiGMPanel::FSkillProfileRow& Row = SkillProfileRows[Index];
		if ((SkillFilterTeam == EGuLiTeam::Unassigned || Row.Team == SkillFilterTeam)
			&& Matches(Row.ToSearchText(), ProfileFilter)
			&& Matches(FString::FromInt(Row.UnitTypeId), UnitFilter)
			&& Matches(Row.SlotId.ToString(), SlotFilter))
		{
			FilteredSkillProfileIndices.Add(Index);
		}
	}
	SkillProfilePage = GuLiGMPanel::ClampPageIndex(SkillProfilePage, FilteredSkillProfileIndices.Num());
	if (!SkillProfileRowsBox.IsValid())
	{
		return;
	}
	SkillProfileRowsBox->ClearChildren();
	if (!SkillsUnavailableReason.IsEmpty())
	{
		SkillProfileRowsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(Text(SkillsUnavailableReason)).AutoWrapText(true)];
	}
	else if (FilteredSkillProfileIndices.IsEmpty())
	{
		SkillProfileRowsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(Text(TEXT("无匹配技能。")))];
	}
	const int32 Begin = SkillProfilePage * GuLiGMPanel::ItemsPerPage;
	const int32 End = FMath::Min(Begin + GuLiGMPanel::ItemsPerPage, FilteredSkillProfileIndices.Num());
	for (int32 Position = Begin; Position < End; ++Position)
	{
		const int32 Index = FilteredSkillProfileIndices[Position];
		SkillProfileRowsBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
		[
			SNew(SButton).HAlign(HAlign_Left)
			.OnClicked_Lambda([this, Index]() { SelectSkillProfileRow(Index); return FReply::Handled(); })
			[SNew(STextBlock).Text(Text(SkillProfileRows[Index].ToDisplayText())).AutoWrapText(true)]
		];
	}
	SkillProfileRowsBox->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Text(TEXT("上一页"))).OnClicked(this, &SGuLiGMPanel::ChangeSkillProfilePage, -1).IsEnabled(SkillProfilePage > 0)]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SGuLiGMPanel::GetSkillProfilePageText)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Text(TEXT("下一页"))).OnClicked(this, &SGuLiGMPanel::ChangeSkillProfilePage, 1).IsEnabled(SkillProfilePage + 1 < GuLiGMPanel::GetPageCount(FilteredSkillProfileIndices.Num()))]
	];
}

void SGuLiGMPanel::RebuildSkillSourceRows()
{
	FilteredSkillSourceIndices.Reset();
	for (int32 Index = 0; Index < SkillSourceRows.Num(); ++Index)
	{
		if (Matches(SkillSourceRows[Index].ToSearchText(), SourceFilter))
		{
			FilteredSkillSourceIndices.Add(Index);
		}
	}
	SkillSourcePage = GuLiGMPanel::ClampPageIndex(SkillSourcePage, FilteredSkillSourceIndices.Num());
	if (!SkillSourceRowsBox.IsValid())
	{
		return;
	}
	SkillSourceRowsBox->ClearChildren();
	if (!SourcesUnavailableReason.IsEmpty())
	{
		SkillSourceRowsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(Text(SourcesUnavailableReason)).AutoWrapText(true)];
	}
	else if (FilteredSkillSourceIndices.IsEmpty())
	{
		SkillSourceRowsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(Text(TEXT("无匹配来源。")))];
	}
	const int32 Begin = SkillSourcePage * GuLiGMPanel::ItemsPerPage;
	const int32 End = FMath::Min(Begin + GuLiGMPanel::ItemsPerPage, FilteredSkillSourceIndices.Num());
	for (int32 Position = Begin; Position < End; ++Position)
	{
		const int32 Index = FilteredSkillSourceIndices[Position];
		SkillSourceRowsBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
		[
			SNew(SButton).HAlign(HAlign_Left)
			.OnClicked_Lambda([this, Index]() { SelectSkillSourceRow(Index); return FReply::Handled(); })
			[SNew(STextBlock).Text(Text(SkillSourceRows[Index].ToDisplayText())).AutoWrapText(true)]
		];
	}
	SkillSourceRowsBox->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Text(TEXT("上一页"))).OnClicked(this, &SGuLiGMPanel::ChangeSkillSourcePage, -1).IsEnabled(SkillSourcePage > 0)]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SGuLiGMPanel::GetSkillSourcePageText)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Text(TEXT("下一页"))).OnClicked(this, &SGuLiGMPanel::ChangeSkillSourcePage, 1).IsEnabled(SkillSourcePage + 1 < GuLiGMPanel::GetPageCount(FilteredSkillSourceIndices.Num()))]
	];
}

void SGuLiGMPanel::SelectRuntimeRow(const int32 Index)
{
	if (!RuntimeRows.IsValidIndex(Index)) return;
	RuntimeKeyEntry->SetText(Text(RuntimeRows[Index].Key.ToString()));
	RuntimeValueEntry->SetText(Text(FString::Printf(TEXT("%.17g"), RuntimeRows[Index].Effective)));
	CancelConfirmation();
}

void SGuLiGMPanel::SelectSkillProfileRow(const int32 Index)
{
	if (!SkillProfileRows.IsValidIndex(Index)) return;
	const GuLiGMPanel::FSkillProfileRow& Row = SkillProfileRows[Index];
	SkillFormTeam = Row.Team;
	SkillUnitEntry->SetText(Text(FString::FromInt(Row.UnitTypeId)));
	SkillSlotEntry->SetText(Text(Row.SlotId.ToString()));
	CancelConfirmation();
}

void SGuLiGMPanel::SelectSkillSourceRow(const int32 Index)
{
	if (!SkillSourceRows.IsValidIndex(Index)) return;
	SkillFormTeam = SkillSourceRows[Index].Team;
	SkillLabelEntry->SetText(Text(SkillSourceRows[Index].Label));
	CancelConfirmation();
}

FReply SGuLiGMPanel::RuntimeGet()
{
	PublishResult(Model.GetRuntimeTuning(ReadEntry(RuntimeKeyEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::RuntimeSet()
{
	GuLiGMPanel::FActionResult Result = Model.SetRuntimeTuning(ReadEntry(RuntimeKeyEntry), ReadEntry(RuntimeValueEntry));
	const bool bRefresh = Result.bSuccess;
	PublishResult(MoveTemp(Result));
	if (bRefresh) RefreshRuntime();
	return FReply::Handled();
}

FReply SGuLiGMPanel::RuntimeReset()
{
	GuLiGMPanel::FActionResult Result = Model.ResetRuntimeTuning(ReadEntry(RuntimeKeyEntry));
	const bool bRefresh = Result.bSuccess;
	PublishResult(MoveTemp(Result));
	if (bRefresh) RefreshRuntime();
	return FReply::Handled();
}

FReply SGuLiGMPanel::RuntimeResetAll()
{
	if (!ConfirmationGate.ConsumeOrArm(GuLiGMPanel::EConfirmationAction::ResetAllRuntime, FPlatformTime::Seconds()))
	{
		PublishResult(GuLiGMPanel::FActionResult::Success(
			TEXT("等待二次确认"),
			TEXT("5 秒内再次点击 Reset All；切换页面或修改参数会取消。"),
			true));
		return FReply::Handled();
	}
	GuLiGMPanel::FActionResult Result = Model.ResetRuntimeTuning(TEXT("all"));
	const bool bRefresh = Result.bSuccess;
	PublishResult(MoveTemp(Result));
	if (bRefresh) RefreshRuntime();
	return FReply::Handled();
}

FReply SGuLiGMPanel::SkillGet()
{
	PublishResult(Model.ExplainSkill(SkillFormTeam, ReadEntry(SkillUnitEntry), ReadEntry(SkillSlotEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::SkillSet()
{
	GuLiGMPanel::FSkillNumericRequest Request;
	Request.Team = SkillFormTeam;
	Request.UnitTypeId = ReadEntry(SkillUnitEntry);
	Request.SlotId = ReadEntry(SkillSlotEntry);
	Request.Attribute = SkillAttribute;
	Request.Value = ReadEntry(SkillValueEntry);
	PublishResult(Model.SetSkillNumericOverride(Request));
	return FReply::Handled();
}

FReply SGuLiGMPanel::SkillReset()
{
	PublishResult(Model.ClearSkillNumericOverride(SkillFormTeam, ReadEntry(SkillUnitEntry), ReadEntry(SkillSlotEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::SkillSource()
{
	GuLiGMPanel::FSkillSourceRequest Request;
	Request.Team = SkillFormTeam;
	Request.UnitTypeId = ReadEntry(SkillUnitEntry);
	Request.SlotId = ReadEntry(SkillSlotEntry);
	Request.Label = ReadEntry(SkillLabelEntry);
	Request.Attribute = SkillAttribute;
	Request.Operation = ModifierOperation;
	Request.Value = ReadEntry(SkillValueEntry);
	Request.RequiredSkillId = ReadEntry(RequiredSkillEntry);
	Request.RequiredGameplayTag = ReadEntry(RequiredTagEntry);
	PublishResult(Model.UpsertSkillSource(Request));
	return FReply::Handled();
}

FReply SGuLiGMPanel::SkillReplace()
{
	GuLiGMPanel::FSkillReplacementRequest Request;
	Request.Team = SkillFormTeam;
	Request.UnitTypeId = ReadEntry(SkillUnitEntry);
	Request.SlotId = ReadEntry(SkillSlotEntry);
	Request.Label = ReadEntry(SkillLabelEntry);
	Request.SkillId = ReadEntry(ReplacementSkillEntry);
	Request.Priority = ReadEntry(ReplacementPriorityEntry);
	PublishResult(Model.UpsertSkillReplacement(Request));
	return FReply::Handled();
}

FReply SGuLiGMPanel::SkillRemove()
{
	PublishResult(Model.RemoveSkillSource(SkillFormTeam, ReadEntry(SkillLabelEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::InspectSkillSoldier()
{
	PublishResult(Model.InspectSkillSoldier(ReadEntry(SkillSoldierIdEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::SpawnSoldier()
{
	GuLiGMPanel::FSpawnRequest Request;
	Request.Team = SpawnTeam;
	Request.UnitTypeId = ReadEntry(SpawnUnitEntry);
	Request.X = ReadEntry(SpawnXEntry);
	Request.Y = ReadEntry(SpawnYEntry);
	Request.Z = ReadEntry(SpawnZEntry);
	PublishResult(Model.SpawnDebugSoldier(Request));
	return FReply::Handled();
}

FReply SGuLiGMPanel::RunBenchmark()
{
	if (BenchmarkPopulation == 10000
		&& !ConfirmationGate.ConsumeOrArm(GuLiGMPanel::EConfirmationAction::BenchmarkTenThousand, FPlatformTime::Seconds()))
	{
		PublishResult(GuLiGMPanel::FActionResult::Success(
			TEXT("等待 10000 人 Bench 二次确认"),
			TEXT("5 秒内再次点击运行；切换页面或修改参数会取消。"),
			true));
		return FReply::Handled();
	}
	PublishResult(Model.RunBenchmark(BenchmarkPopulation, ReadEntry(BenchmarkStepsEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::InspectNavigationSoldier()
{
	PublishResult(Model.InspectNavigationSoldier(ReadEntry(NavigationSoldierIdEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::RefreshNavigationStats()
{
	PublishResult(Model.QueryNavigationStats());
	return FReply::Handled();
}

FReply SGuLiGMPanel::RefreshLastMove()
{
	PublishResult(Model.QueryLastMove(ReadEntry(CohortIdEntry)));
	return FReply::Handled();
}

FReply SGuLiGMPanel::ToggleCameraDebug()
{
	PublishResult(Model.SetCameraDebugEnabled(!Model.IsCameraDebugEnabled()));
	return FReply::Handled();
}

void SGuLiGMPanel::BindSkillDelegate()
{
	UnbindSkillDelegate();
	if (UWorld* World = Model.GetWorld())
	{
		if (UGuLiArmySkillSubsystem* Skills = World->GetSubsystem<UGuLiArmySkillSubsystem>())
		{
			BoundSkillSubsystem = Skills;
			SkillCommittedHandle = Skills->OnWeaponLoadoutCommitted().AddSP(
				SharedThis(this),
				&SGuLiGMPanel::HandleSkillLoadoutCommitted);
		}
	}
}

void SGuLiGMPanel::UnbindSkillDelegate()
{
	if (UGuLiArmySkillSubsystem* Skills = BoundSkillSubsystem.Get(); Skills && SkillCommittedHandle.IsValid())
	{
		Skills->OnWeaponLoadoutCommitted().Remove(SkillCommittedHandle);
	}
	SkillCommittedHandle.Reset();
	BoundSkillSubsystem.Reset();
}

void SGuLiGMPanel::HandleSkillLoadoutCommitted(const uint32 Revision)
{
	if (!bPanelOpen)
	{
		return;
	}
	RefreshSkills();
	if (LastResult.bPending)
	{
		PublishResult(GuLiGMPanel::FActionResult::Success(
			TEXT("技能变更已在权威固定步提交"),
			FString::Printf(TEXT("committed revision=%u"), Revision)));
	}
}

void SGuLiGMPanel::CancelConfirmation()
{
	ConfirmationGate.Cancel();
}

void SGuLiGMPanel::PublishResult(GuLiGMPanel::FActionResult Result)
{
	LastResult = MoveTemp(Result);
	const FString Status = LastResult.bPending ? TEXT("PENDING") : (LastResult.bSuccess ? TEXT("OK") : TEXT("FAILED"));
	if (LastResult.bSuccess)
	{
		UE_LOG(LogGuLiStrike, Display, TEXT("[GM.Panel][%s] %s | applied=%d | %s"), *Status, *LastResult.Summary, LastResult.AppliedCount, *LastResult.Detail);
	}
	else
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("[GM.Panel][%s] %s | %s"), *Status, *LastResult.Summary, *LastResult.Detail);
	}
}

FText SGuLiGMPanel::GetContextText() const
{
	return Text(Model.DescribeContext());
}

FText SGuLiGMPanel::GetAccessText() const
{
	const GuLiGMPanel::FAccessPolicy Access = Model.GetAccessPolicy();
	if (Access.bCanMutateAuthorityState)
	{
		return Text(TEXT("权限：Standalone / Listen Host，权威写操作可用。不暂停 World。"));
	}
	return Text(FString::Printf(
		TEXT("权限：客户端只读 + 本地 Bench/相机。%s"),
		*Access.AuthorityUnavailableReason));
}

FText SGuLiGMPanel::GetResultText() const
{
	const TCHAR* Status = LastResult.bPending ? TEXT("PENDING") : (LastResult.bSuccess ? TEXT("成功") : TEXT("失败"));
	return Text(FString::Printf(
		TEXT("[%s] %s    影响数量: %d\n%s"),
		Status,
		*LastResult.Summary,
		LastResult.AppliedCount,
		*LastResult.Detail));
}

FText SGuLiGMPanel::GetRuntimePageText() const
{
	return Text(FString::Printf(TEXT("第 %d / %d 页，共 %d 条"), RuntimePage + 1, GuLiGMPanel::GetPageCount(FilteredRuntimeIndices.Num()), FilteredRuntimeIndices.Num()));
}

FText SGuLiGMPanel::GetSkillProfilePageText() const
{
	return Text(FString::Printf(TEXT("第 %d / %d 页，共 %d 条"), SkillProfilePage + 1, GuLiGMPanel::GetPageCount(FilteredSkillProfileIndices.Num()), FilteredSkillProfileIndices.Num()));
}

FText SGuLiGMPanel::GetSkillSourcePageText() const
{
	return Text(FString::Printf(TEXT("第 %d / %d 页，共 %d 条"), SkillSourcePage + 1, GuLiGMPanel::GetPageCount(FilteredSkillSourceIndices.Num()), FilteredSkillSourceIndices.Num()));
}

FText SGuLiGMPanel::GetSkillFilterTeamText() const
{
	return Text(SkillFilterTeam == EGuLiTeam::Unassigned
		? TEXT("Team: 全部")
		: FString::Printf(TEXT("Team: %s"), GuLiGMPanel::LexToString(SkillFilterTeam)));
}

FText SGuLiGMPanel::GetSkillFormTeamText() const
{
	return Text(FString::Printf(TEXT("Team: %s"), GuLiGMPanel::LexToString(SkillFormTeam)));
}

FText SGuLiGMPanel::GetSpawnTeamText() const
{
	return Text(FString::Printf(TEXT("Team: %s"), GuLiGMPanel::LexToString(SpawnTeam)));
}

FText SGuLiGMPanel::GetSkillAttributeText() const
{
	return Text(FString::Printf(TEXT("Attr: %s"), GuLiGMPanel::LexToString(SkillAttribute)));
}

FText SGuLiGMPanel::GetModifierOperationText() const
{
	return Text(FString::Printf(TEXT("Op: %s"), GuLiGMPanel::LexToString(ModifierOperation)));
}

FText SGuLiGMPanel::GetBenchmarkPopulationText() const
{
	return Text(FString::Printf(TEXT("Population: %d"), BenchmarkPopulation));
}

FText SGuLiGMPanel::GetCameraDebugText() const
{
	return Text(Model.IsCameraDebugEnabled() ? TEXT("关闭本地相机调试") : TEXT("开启本地相机调试"));
}

FSlateColor SGuLiGMPanel::GetResultColor() const
{
	if (LastResult.bPending)
	{
		return FLinearColor(1.0f, 0.72f, 0.22f);
	}
	return LastResult.bSuccess ? FLinearColor(0.45f, 0.95f, 0.62f) : FLinearColor(1.0f, 0.35f, 0.35f);
}

FSlateColor SGuLiGMPanel::GetAccessColor() const
{
	return Model.GetAccessPolicy().bCanMutateAuthorityState
		? FLinearColor(0.45f, 0.85f, 0.62f)
		: FLinearColor(1.0f, 0.68f, 0.24f);
}

#endif
