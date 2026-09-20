#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Commander/UI/GuLiCommanderTaskButton.h"
#include "Components/TextBlock.h"

void UGuLiCommanderHUDWidget::BuildTaskPanel()
{
	if (TaskPanel || !WidgetTree) return;
	auto* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!Root) return;
	auto* Border = WidgetTree->ConstructWidget<UBorder>(); TaskPanel = Border;
	Border->SetBrushColor(FLinearColor(.015f,.035f,.05f,.92f)); Border->SetPadding(FMargin(10));
	auto* Box = WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Box);
	auto* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(); Box->AddChild(Buttons);
	auto AddButton = [&](const TCHAR* Label)
	{
		auto* Button = WidgetTree->ConstructWidget<UGuLiCommanderTaskButton>();
		auto* Text = WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Label));
		auto Font = Text->GetFont(); Font.Size = 13; Text->SetFont(Font);
		Text->SetJustification(ETextJustify::Center);
		auto Style = Button->GetStyle(); Style.Normal.TintColor = FSlateColor(FLinearColor(.02f,.13f,.18f));
		Style.Hovered.TintColor = FSlateColor(FLinearColor(.03f,.28f,.34f));
		Style.Pressed.TintColor = FSlateColor(FLinearColor(.02f,.2f,.26f));
		Style.NormalPadding = FMargin(6,5); Style.PressedPadding = Style.NormalPadding;
		Button->SetStyle(Style); Button->SetContent(Text);
		auto* Slot = Buttons->AddChildToHorizontalBox(Button); Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Slot->SetPadding(FMargin(0,0,4,6)); return Button;
	};
	FocusButton = AddButton(TEXT("定位单位")); FocusButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleFocusClicked);
	StopButton = AddButton(TEXT("停止 [S]")); StopButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleStopClicked);
	auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TaskText = WidgetTree->ConstructWidget<UTextBlock>(); TaskText->SetAutoWrapText(true);
	auto Font = TaskText->GetFont(); Font.Size = 13; TaskText->SetFont(Font); Scroll->AddChild(TaskText);
	auto* PanelSlot = Root->AddChildToCanvas(Border);
	PanelSlot->SetAnchors(FAnchors(1, .5f)); PanelSlot->SetAlignment(FVector2D(1,.5f));
	PanelSlot->SetPosition(FVector2D(-16,0)); PanelSlot->SetSize(FVector2D(300,240)); PanelSlot->SetZOrder(80);
	RefreshTaskPanel();
}
void UGuLiCommanderHUDWidget::HandleFocusClicked() { if (auto* PC = CommanderController.Get()) PC->FocusSelectedUnits(); }
void UGuLiCommanderHUDWidget::HandleStopClicked() { if (auto* PC = CommanderController.Get()) PC->StopSelectedUnits(); }

void UGuLiCommanderHUDWidget::RefreshTaskPanel()
{
	const auto* Sync = NetSyncComponent.Get(); if (!TaskText || !Sync) return;
	const auto& Summaries = Sync->GetTaskSummaries();
	FString Text;
	const auto& Counts = Sync->GetControlGroupCounts();
	for (int32 I=0; I<Counts.Num(); ++I) Text += FString::Printf(TEXT("%d:%d%s"), I, Counts[I], I==4 ? TEXT("\n") : TEXT("  "));
	Text += TEXT("\n单按编队选择 · 双按定位\n");
	int32 Members = 0; for (const auto& Summary : Summaries) Members += Summary.UnitCount;
	if (Summaries.IsEmpty()) Text += TEXT("尚未选中单位\n");
	else
	{
		int32 Prefix = Summaries[0].Tasks.Num();
		auto Same = [](const FGuLiUnitTaskView& A, const FGuLiUnitTaskView& B)
		{
			return A.bAutomatic == B.bAutomatic && A.Command.Kind == B.Command.Kind && A.Command.SpecialTaskId == B.Command.SpecialTaskId
				&& A.Command.BuildingId == B.Command.BuildingId && A.Command.ClusterId == B.Command.ClusterId
				&& A.Command.TerritoryId == B.Command.TerritoryId && FVector(A.Command.Target).Equals(B.Command.Target, 1);
		};
		for (const auto& Summary : Summaries)
		{
			Prefix = FMath::Min(Prefix, Summary.Tasks.Num());
			for (int32 I=0; I<Prefix; ++I) if (!Same(Summaries[0].Tasks[I], Summary.Tasks[I])) { Prefix = I; break; }
		}
		Text += FString::Printf(TEXT("选中 %d · 共同前缀 %d · 差异 %d 组\n"), Members, Prefix, Summaries.Num()>1 ? Summaries.Num() : 0);
		for (int32 I=0; I<Prefix; ++I)
		{
			const auto& Task = Summaries[0].Tasks[I];
			Text += FString::Printf(TEXT("%d. [%s] %s%s\n"), I+1, Task.bAutomatic ? TEXT("自动") : TEXT("手动"), *Task.DisplayName,
				Task.Status == EGuLiTaskStatus::Waiting ? TEXT(" · 等待") : Task.Status == EGuLiTaskStatus::WaitingSafeExit ? TEXT(" · 等待安全退出") : TEXT(""));
		}
		for (const auto& Summary : Summaries)
		{
			Text += FString::Printf(TEXT("%d 个单位 · 手动 %d/32%s\n"), Summary.UnitCount, Summary.ManualTaskCount,
				Summary.bWaitingSafeExit ? TEXT(" · 等待安全停止") : Summary.bStopped ? TEXT(" · 持续停止") : TEXT(""));
			if (!Prefix && !Summary.Tasks.IsEmpty()) Text += FString::Printf(TEXT("当前 [%s] %s\n"), Summary.Tasks[0].bAutomatic ? TEXT("自动") : TEXT("手动"), *Summary.Tasks[0].DisplayName);
			if (Summary.Tasks.IsEmpty() && !Summary.bStopped && !Summary.RecoverableTasks.IsEmpty()) Text += TEXT("等待合法自动目标\n");
			if (!Summary.RecoverableTasks.IsEmpty()) Text += TEXT("保留资格：") + FString::Join(Summary.RecoverableTasks,TEXT("、")) + TEXT("\n");
			if (!Summary.Error.IsEmpty()) Text += Summary.Error + TEXT("\n");
		}
	}
	Text += Sync->GetLastTaskFeedback();
	TaskText->SetText(FText::FromString(Text));
	FocusButton->SetIsEnabled(Members > 0); StopButton->SetIsEnabled(Members > 0);
}
