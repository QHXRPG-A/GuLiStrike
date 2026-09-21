#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/UI/GuLiCommanderUITheme.h"
#include "Commander/UI/GuLiCommanderActionButton.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace GuLiConsoleLayout
{
	const FLinearColor Ink(.018f,.024f,.034f,.98f);
	const FLinearColor Text(.86f,.91f,.94f);
	FSlateBrush Brush(UTexture2D* Texture, FLinearColor Tint)
	{
		FSlateBrush B; B.SetResourceObject(Texture); B.TintColor = Tint;
		B.DrawAs = ESlateBrushDrawType::Box; B.Margin = FMargin(.22f); B.ImageSize = FVector2D(64);
		return B;
	}
	void StyleHealth(UProgressBar* Bar, UGuLiCommanderUITheme* Theme)
	{
		FProgressBarStyle Style=Bar->GetWidgetStyle();
		Style.FillImage=Brush(Theme?Theme->Bar.Get():nullptr,FLinearColor::White);
		Style.BackgroundImage=Brush(nullptr,FLinearColor(.018f,.024f,.034f));
		Bar->SetWidgetStyle(Style); Bar->SetFillColorAndOpacity(FLinearColor(.12f,.78f,.5f));
	}
}

TSharedRef<SWidget> UGuLiCommanderHUDWidget::RebuildWidget()
{
	BuildConsoleLayout();
	return Super::RebuildWidget();
}

void UGuLiCommanderHUDWidget::PlaceConsoleWidget(UCanvasPanel* Parent, UWidget* Child,
	FVector2D Position, FVector2D Size, int32 Z)
{
	auto* CanvasSlot = Parent->AddChildToCanvas(Child); CanvasSlot->SetPosition(Position); CanvasSlot->SetSize(Size); CanvasSlot->SetZOrder(Z);
}

UTextBlock* UGuLiCommanderHUDWidget::MakeConsoleText(FName Name, const FString& Value, int32 Size)
{
	auto* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Text->SetText(FText::FromString(Value)); Text->SetColorAndOpacity(GuLiConsoleLayout::Text);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size));
	if(Name==TEXT("TXT_Time") && ConsoleTheme && ConsoleTheme->NumericFont)
		Text->SetFont(FSlateFontInfo(ConsoleTheme->NumericFont,Size));
	Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	Text->SetClipping(EWidgetClipping::ClipToBounds);
	return Text;
}

UCanvasPanel* UGuLiCommanderHUDWidget::MakeConsolePanel(UCanvasPanel* Parent, FName Name,
	FVector2D Position, FVector2D Size)
{
	auto* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString(Name.ToString()+TEXT("_Frame")));
	Border->SetBrush(GuLiConsoleLayout::Brush(ConsoleTheme ? ConsoleTheme->Panel.Get() : nullptr, FLinearColor::White));
	Border->SetBrushColor(GuLiConsoleLayout::Ink); Border->SetPadding(FMargin(0));
	Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto* Panel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), Name);
	Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible); Border->SetContent(Panel);
	PlaceConsoleWidget(Parent, Border, Position, Size);
	InteractionIslands.Add(Panel);
	return Panel;
}

UGuLiCommanderActionButton* UGuLiCommanderHUDWidget::MakeActionButton(FName Name,
	const FString& Label, FName Action, int32 Argument, FName IconKey)
{
	auto* Button = WidgetTree->ConstructWidget<UGuLiCommanderActionButton>(UGuLiCommanderActionButton::StaticClass(), Name);
	Button->Action = Action; Button->Argument = Argument;
	Button->Invoked.BindUObject(this, &ThisClass::HandleUIAction);
	Button->HoverChanged.BindUObject(this, &ThisClass::ShowActionTooltip);
	FButtonStyle Style = Button->GetStyle();
	auto* Texture = ConsoleTheme ? ConsoleTheme->Button.Get() : nullptr;
	Style.Normal = GuLiConsoleLayout::Brush(Texture,FLinearColor(.11f,.15f,.19f));
	Style.Hovered = GuLiConsoleLayout::Brush(Texture,FLinearColor(.12f,.34f,.4f));
	Style.Pressed = GuLiConsoleLayout::Brush(Texture,FLinearColor(.06f,.23f,.3f));
	Style.Disabled = GuLiConsoleLayout::Brush(Texture,FLinearColor(.045f,.055f,.07f));
	Style.NormalPadding = Style.PressedPadding = FMargin(0);
	Button->SetStyle(Style); Button->SetDescription(FText::FromString(Label));
	auto* Content = WidgetTree->ConstructWidget<UCanvasPanel>(); Content->SetVisibility(ESlateVisibility::HitTestInvisible);
	Button->SetContent(Content);
	if(auto* ContentSlot=Cast<UButtonSlot>(Content->Slot))
	{ ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill); ContentSlot->SetPadding(FMargin(0)); }
	Button->Icon = WidgetTree->ConstructWidget<UImage>();
	if (ConsoleTheme) Button->Icon->SetBrushFromTexture(ConsoleTheme->FindIcon(IconKey),true);
	Button->Icon->SetVisibility(IconKey.IsNone() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	auto* IconFit=WidgetTree->ConstructWidget<UScaleBox>(); IconFit->SetStretch(EStretch::ScaleToFit);
	IconFit->SetVisibility(ESlateVisibility::HitTestInvisible); IconFit->SetContent(Button->Icon);
	auto* IconSlot = Content->AddChildToCanvas(IconFit); IconSlot->SetAnchors(FAnchors(.5f,0));
	IconSlot->SetAlignment(FVector2D(.5f,0)); IconSlot->SetPosition(FVector2D(0,5)); IconSlot->SetSize(FVector2D(28,28));
	Button->Label = MakeConsoleText(NAME_None, Label, 14); Button->Label->SetJustification(ETextJustify::Center);
	auto* TextSlot = Content->AddChildToCanvas(Button->Label); TextSlot->SetAnchors(FAnchors(0,1,1,1));
	TextSlot->SetOffsets(FMargin(2,-25,-2,22));
	if(IconKey.IsNone()) { TextSlot->SetAnchors(FAnchors(0,.5f,1,.5f)); TextSlot->SetOffsets(FMargin(2,-11,2,22)); }
	if(Action==TEXT("Portrait"))
	{
		IconSlot->SetAnchors(FAnchors(0,0)); IconSlot->SetAlignment(FVector2D::ZeroVector);
		IconSlot->SetPosition({4,3}); IconSlot->SetSize({44,38});
		TextSlot->SetAnchors(FAnchors(0,.5f,1,.5f)); TextSlot->SetOffsets(FMargin(50,-10,2,22));
		Button->Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12));
	}
	const bool IconOnly=Name.ToString().StartsWith(TEXT("BTN_Cmd_")) || Action==TEXT("Building");
	if(IconOnly)
	{
		Button->Label->SetVisibility(ESlateVisibility::Collapsed);
		IconSlot->SetAnchors(FAnchors(.5f,.5f)); IconSlot->SetAlignment({.5f,.5f});
		IconSlot->SetPosition({0,0}); IconSlot->SetSize({36,36});
	}
	if(Action==TEXT("Group") || Action==TEXT("Attribute") || IconOnly)
	{
		const bool Passive=Action==TEXT("Attribute");
		Style.Normal=FSlateRoundedBoxBrush(Passive?FLinearColor::Transparent:FLinearColor(.028f,.041f,.057f,.98f),4.f,
			FLinearColor(.13f,.2f,.24f,Passive?0.f:1.f),1.f);
		Style.Hovered=FSlateRoundedBoxBrush(FLinearColor(.055f,.16f,.19f),4.f,FLinearColor(.12f,.64f,.75f),1.f);
		Style.Pressed=FSlateRoundedBoxBrush(FLinearColor(.04f,.25f,.3f),4.f);
		Style.Disabled=FSlateRoundedBoxBrush(FLinearColor(.023f,.03f,.04f),4.f,FLinearColor(.075f,.1f,.13f),1.f);
		Button->SetStyle(Style);
	}
	if(Action==TEXT("Group"))
	{
		TextSlot->SetAnchors(FAnchors(0,0)); TextSlot->SetOffsets(FMargin(7,5,20,24));
		Button->Label->SetJustification(ETextJustify::Left);
		Button->Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),16));
	}
	if(Action==TEXT("Attribute"))
	{
		IconSlot->SetAnchors(FAnchors(0,.5f)); IconSlot->SetAlignment({0,.5f});
		IconSlot->SetPosition({0,0}); IconSlot->SetSize({18,18});
		TextSlot->SetAnchors(FAnchors(0,.5f,1,.5f)); TextSlot->SetOffsets(FMargin(24,-9,0,20));
		Button->Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12));
		Button->Label->SetJustification(ETextJustify::Left);
	}
	return Button;
}

void UGuLiCommanderHUDWidget::BuildConsoleLayout()
{
	if (!ConsoleTheme) ConsoleTheme = LoadObject<UGuLiCommanderUITheme>(nullptr,TEXT("/Game/Commander/UI/DA_CommanderUITheme.DA_CommanderUITheme"));
	// A fresh tree avoids retaining old authored decoration and stale delegate-bound controls on rebuild.
	WidgetTree = NewObject<UWidgetTree>(this); WidgetCache.Reset(); InteractionIslands.Reset();
	PortraitButtons.Reset(); PortraitHealth.Reset(); TypeButtons.Reset(); GroupButtons.Reset(); GroupCounts.Reset(); AttributeRows.Reset();
	MiniMapWidget = nullptr; TaskPanel = nullptr; TaskText = nullptr; FocusButton = StopButton = nullptr;
	bMenuOpen=bQuitConfirmation=bHelpOpen=bTaskDrawerOpen=false; LastTaskDescription.Reset(); HoveredAction.Reset();
	auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible); WidgetTree->RootWidget = Root;
	SetIsFocusable(true);
	// One bottom design surface scales down on narrow displays. Wide displays keep the panel readable.
	auto* Scale = WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit);
	Scale->SetStretchDirection(EStretchDirection::DownOnly); Scale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto* ScaleSlot = Root->AddChildToCanvas(Scale); ScaleSlot->SetAnchors(FAnchors(0,1,1,1));
	ScaleSlot->SetOffsets(FMargin(16,-292,16,276));
	auto* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("SB_CommandDockDesign")); Size->SetWidthOverride(1888); Size->SetHeightOverride(276);
	Cast<UScaleBoxSlot>(Scale->AddChild(Size))->SetVerticalAlignment(VAlign_Bottom);
	auto* Surface = WidgetTree->ConstructWidget<UCanvasPanel>(); Size->SetContent(Surface);
	Surface->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto* Map = MakeConsolePanel(Surface,TEXT("SB_MapDesign"),{0,0},{276,276});
	auto* MapCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("C_Map"));
	MapCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible); PlaceConsoleWidget(Map,MapCanvas,{0,0},{276,276});
	auto* MapHint=MakeConsoleText(TEXT("TXT_MapHint"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.001")),14);
	PlaceConsoleWidget(Surface,MapHint,{0,0},{0,0}); MapHint->SetVisibility(ESlateVisibility::Collapsed);
	auto* Groups = MakeConsolePanel(Surface,TEXT("SB_ShortcutsDesign"),{292,0},{644,44});
	Cast<UBorder>(Groups->GetParent())->SetBrushColor(FLinearColor::Transparent);
	for(int32 I=0;I<10;++I)
	{
		auto* B=MakeActionButton(*FString::Printf(TEXT("BTN_Group_%d"),I),FString::FromInt(I),TEXT("Group"),I);
		B->SetDescription(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleLayout.002"))));
		PlaceConsoleWidget(Groups,B,{float(I*64),2},{58,38}); GroupButtons.Add(B);
		auto* Count=MakeConsoleText(*FString::Printf(TEXT("TXT_GroupCount_%d"),I),TEXT(""),11);
		Count->SetJustification(ETextJustify::Right); Count->SetColorAndOpacity(FLinearColor(.4f,.76f,.82f));
		PlaceConsoleWidget(Cast<UCanvasPanel>(B->GetContent()),Count,{22,19},{30,15}); GroupCounts.Add(Count);
	}
	auto* Selection=MakeConsolePanel(Surface,TEXT("SB_DockDesign"),{292,52},{924,224});
	auto* TypeScroll=WidgetTree->ConstructWidget<UScrollBox>(); TypeScroll->SetOrientation(Orient_Horizontal); TypeScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	PlaceConsoleWidget(Selection,TypeScroll,{10,7},{756,30}); TypeStrip=WidgetTree->ConstructWidget<UHorizontalBox>(); TypeScroll->AddChild(TypeStrip);
	auto* Prev=MakeActionButton(TEXT("BTN_PagePrevious"),TEXT("‹"),TEXT("Page"),-1);
	auto* Next=MakeActionButton(TEXT("BTN_PageNext"),TEXT("›"),TEXT("Page"),1);
	PlaceConsoleWidget(Selection,Prev,{772,6},{34,30}); PlaceConsoleWidget(Selection,Next,{874,6},{34,30});
	PageCaption=MakeConsoleText(TEXT("TXT_Page"),TEXT("1 / 1"),14); PageCaption->SetJustification(ETextJustify::Center);
	PlaceConsoleWidget(Selection,PageCaption,{808,12},{64,23});
	for(int32 I=0;I<24;++I)
	{
		auto* B=MakeActionButton(*FString::Printf(TEXT("BTN_Portrait_%02d"),I),TEXT(""),TEXT("Portrait"),I,TEXT("Unit"));
		PlaceConsoleWidget(Selection,B,{float(10+(I%8)*113),float(44+(I/8)*58)},{106,54}); PortraitButtons.Add(B);
		auto* HP=WidgetTree->ConstructWidget<UProgressBar>(); GuLiConsoleLayout::StyleHealth(HP,ConsoleTheme);
		HP->SetVisibility(ESlateVisibility::HitTestInvisible);
		PlaceConsoleWidget(Selection,HP,{float(12+(I%8)*113),float(95+(I/8)*58)},{102,3},3); PortraitHealth.Add(HP);
	}
	SelectionCaption=MakeConsoleText(TEXT("TXT_SelectionCaption"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.003")),14);
	PlaceConsoleWidget(Selection,SelectionCaption,{12,199},{890,24});
	SelectionCaption->SetVisibility(ESlateVisibility::Collapsed);
	auto* Detail=MakeConsolePanel(Surface,TEXT("C_UnitTypeCard"),{1232,52},{196,224});
	PortraitImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("I_UnitTypePortrait"));
	PortraitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	auto* PortraitFit=WidgetTree->ConstructWidget<UScaleBox>(); PortraitFit->SetStretch(EStretch::ScaleToFit); PortraitFit->SetContent(PortraitImage);
	PlaceConsoleWidget(Detail,PortraitFit,{8,6},{180,82});
	SelectionDetails=MakeConsoleText(TEXT("TXT_UnitTypeName"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.004")),14);
	PlaceConsoleWidget(Detail,SelectionDetails,{10,91},{176,22});
	const TCHAR* AttributeKeys[]={TEXT("Health"),TEXT("Shield"),TEXT("Speed"),TEXT("Defense"),TEXT("CargoBlue"),TEXT("CargoRed")};
	const TCHAR* AttributeTips[]={GuLiGameText::Text(TEXT("UI.ConsoleLayout.005")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.006")),
		GuLiGameText::Text(TEXT("UI.ConsoleLayout.007")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.008")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.009")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.010"))};
	for(int32 I=0;I<6;++I)
	{
		const FName Key=AttributeKeys[I];
		auto* Row=MakeActionButton(*FString::Printf(TEXT("BTN_Attribute_%s"),AttributeKeys[I]),TEXT("—"),TEXT("Attribute"),I,Key);
		Row->SetDescription(FText::FromString(AttributeTips[I]));
		const float X=I<2?10.f:(I%2?104.f:10.f), Y=I<2?116.f+I*22.f:160.f+(I/2-1)*24.f;
		PlaceConsoleWidget(Detail,Row,{X,Y},{I<2?176.f:82.f,22}); AttributeRows.Add(Key,Row);
	}
	SelectionHealth=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("PB_UnitTypeHealth")); GuLiConsoleLayout::StyleHealth(SelectionHealth,ConsoleTheme);
	SelectionHealth->SetVisibility(ESlateVisibility::HitTestInvisible); PlaceConsoleWidget(Detail,SelectionHealth,{10,210},{176,5});
	auto* Commands=MakeConsolePanel(Surface,TEXT("C_Commands"),{1444,0},{444,276});
	auto* CommandTitle=MakeConsoleText(TEXT("TXT_CommandTitle"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.011")),16);
	PlaceConsoleWidget(Commands,CommandTitle,{0,0},{0,0}); CommandTitle->SetVisibility(ESlateVisibility::Collapsed);
	const TCHAR* Labels[]={GuLiGameText::Text(TEXT("UI.ConsoleLayout.012")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.013")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.014")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.015")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.016")),
		GuLiGameText::Text(TEXT("UI.ConsoleLayout.017")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.018")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.019")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.020")),TEXT(""),GuLiGameText::Text(TEXT("UI.ConsoleLayout.021")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.022")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.023")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.024")),TEXT("")};
	const TCHAR* Actions[]={TEXT("Move"),TEXT("Stop"),TEXT("Skill"),TEXT("Focus"),TEXT("Tasks"),TEXT("Mine"),TEXT("Return"),TEXT("Construct"),TEXT("Transit"),TEXT(""),TEXT("Build"),TEXT("Teleport"),TEXT("Help"),TEXT("Menu"),TEXT("")};
	for(int32 I=0;I<15;++I)
	{
		const FName Action=Actions[I]; const FName Name=*FString::Printf(TEXT("BTN_Cmd_%s"),Actions[I][0]?Actions[I]:*FString::FromInt(I));
		auto* B=MakeActionButton(Name,Labels[I],Action,0,Action);
		PlaceConsoleWidget(Commands,B,{float(10+(I%5)*86),float(10+(I/5)*86)},{80,80});
		if(Action.IsNone()) B->SetIsEnabled(false);
		if(Action==TEXT("Stop")) StopButton=B;
		if(Action==TEXT("Focus")) FocusButton=B;
	}
	auto* Tasks=MakeConsolePanel(Surface,TEXT("C_TaskStrip"),{292,284},{1596,28});
	TaskCaption=MakeConsoleText(TEXT("TXT_TaskSummary"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.025")),14);
	PlaceConsoleWidget(Tasks,TaskCaption,{10,3},{1572,24});
	Tasks->GetParent()->SetVisibility(ESlateVisibility::Collapsed);
	// Top strip uses the same design scale, independently anchored from the bottom islands.
	auto* TopScale=WidgetTree->ConstructWidget<UScaleBox>(); TopScale->SetStretch(EStretch::ScaleToFit); TopScale->SetStretchDirection(EStretchDirection::DownOnly);
	auto* TS=Root->AddChildToCanvas(TopScale); TS->SetAnchors(FAnchors(0,0,1,0)); TS->SetOffsets(FMargin(16,12,16,44));
	auto* TopSize=WidgetTree->ConstructWidget<USizeBox>(); TopSize->SetWidthOverride(1400); TopSize->SetHeightOverride(44); TopScale->AddChild(TopSize);
	auto* TopCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); TopSize->SetContent(TopCanvas);
	auto* Top=MakeConsolePanel(TopCanvas,TEXT("SB_TopStatus"),{0,0},{1400,44});
	PlaceConsoleWidget(Top,MakeConsoleText(TEXT("TXT_CoreSystem"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.026")),17),{16,11},{340,27});
	PlaceConsoleWidget(Top,MakeConsoleText(TEXT("TXT_Time"),TEXT("00:00"),18),{510,10},{130,28});
	PlaceConsoleWidget(Top,MakeConsoleText(TEXT("TXT_RosterValue"),TEXT("— / —"),17),{745,11},{230,28});
	PlaceConsoleWidget(Top,MakeConsoleText(TEXT("TXT_Energy"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.027")),17),{1020,11},{364,28});
	// Context and queue drawers remain separate from the battle view when not needed.
	ContextPanel=MakeConsolePanel(Root,TEXT("C_Context"),{0,0},{460,324});
	if(auto* S=Cast<UCanvasPanelSlot>(ContextPanel->GetParent()->Slot))
	{ S->SetAnchors(FAnchors(1,1)); S->SetAlignment({1,1}); S->SetPosition({-16,-344}); }
	ContextCaption=MakeConsoleText(TEXT("TXT_Context"),TEXT(""),16); ContextCaption->SetAutoWrapText(true);
	PlaceConsoleWidget(ContextPanel,ContextCaption,{16,12},{428,116});
	for(int32 I=1;I<=6;++I)
	{
		auto* B=MakeActionButton(*FString::Printf(TEXT("BTN_Building_%d"),I),FString::FromInt(I),TEXT("Building"),I,*FString::Printf(TEXT("Building.%d"),I));
		PlaceConsoleWidget(ContextPanel,B,{float(14+((I-1)%3)*144),float(132+((I-1)/3)*87)},{136,78});
	}
	ContextPanel->GetParent()->SetVisibility(ESlateVisibility::Collapsed);
	auto* Drawer=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C_TaskDrawer")); TaskPanel=Drawer;
	Drawer->SetBrushColor(GuLiConsoleLayout::Ink); Drawer->SetPadding(FMargin(14));
	auto* DS=Root->AddChildToCanvas(Drawer); DS->SetAnchors(FAnchors(.5f,1)); DS->SetAlignment({.5f,1}); DS->SetPosition({0,-344}); DS->SetSize({880,324}); DS->SetZOrder(80);
	auto* DrawerBox=WidgetTree->ConstructWidget<UVerticalBox>(); Drawer->SetContent(DrawerBox);
	auto* Close=MakeActionButton(TEXT("BTN_CloseTasks"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.028")),TEXT("Tasks"),0);
	auto* CloseBox=WidgetTree->ConstructWidget<USizeBox>(); CloseBox->SetHeightOverride(36); CloseBox->SetContent(Close);
	DrawerBox->AddChildToVerticalBox(CloseBox)->SetPadding(FMargin(0,0,0,6));
	auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); DrawerBox->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	FScrollBarStyle ScrollStyle=Scroll->GetWidgetBarStyle();
	ScrollStyle.NormalThumbImage=GuLiConsoleLayout::Brush(ConsoleTheme?ConsoleTheme->ScrollThumb.Get():nullptr,FLinearColor(.15f,.5f,.6f));
	ScrollStyle.HoveredThumbImage=ScrollStyle.DraggedThumbImage=ScrollStyle.NormalThumbImage; Scroll->SetWidgetBarStyle(ScrollStyle);
	TaskRows=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(TaskRows);
	TaskText=MakeConsoleText(TEXT("TXT_TaskDetail"),TEXT(""),16); TaskText->SetAutoWrapText(true); TaskRows->AddChild(TaskText);
	Drawer->SetVisibility(ESlateVisibility::Collapsed); InteractionIslands.Add(Drawer);
	MenuLayer=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C_MenuModal")); MenuLayer->SetBrushColor(FLinearColor(0,0,0,.75f));
	auto* MS=Root->AddChildToCanvas(MenuLayer); MS->SetAnchors(FAnchors(0,0,1,1)); MS->SetOffsets(FMargin(0)); MS->SetZOrder(300);
	MenuLayer->SetHorizontalAlignment(HAlign_Center); MenuLayer->SetVerticalAlignment(VAlign_Center);
	auto* MenuSize=WidgetTree->ConstructWidget<USizeBox>(); MenuSize->SetWidthOverride(540); MenuLayer->SetContent(MenuSize);
	auto* MenuBox=WidgetTree->ConstructWidget<UVerticalBox>(); MenuSize->SetContent(MenuBox);
	MenuCaption=MakeConsoleText(TEXT("TXT_Menu"),GuLiGameText::Text(TEXT("UI.ConsoleLayout.029")),22); MenuCaption->SetAutoWrapText(true); MenuBox->AddChildToVerticalBox(MenuCaption)->SetPadding(FMargin(12));
	const TCHAR* MenuLabels[]={GuLiGameText::Text(TEXT("UI.ConsoleLayout.030")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.031")),GuLiGameText::Text(TEXT("UI.ConsoleLayout.032"))};
	const TCHAR* MenuActions[]={TEXT("Resume"),TEXT("Help"),TEXT("Quit")};
	for(int32 I=0;I<3;++I)
	{
		auto* B=MakeActionButton(*FString::Printf(TEXT("BTN_Menu_%d"),I),MenuLabels[I],MenuActions[I],0);
		auto* Box=WidgetTree->ConstructWidget<USizeBox>(); Box->SetHeightOverride(48); Box->SetContent(B);
		MenuBox->AddChildToVerticalBox(Box)->SetPadding(FMargin(8));
	}
	MenuLayer->SetVisibility(ESlateVisibility::Collapsed);
	ActionTooltip=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C_SelectionTooltip"));
	ActionTooltip->SetBrushColor(FLinearColor(.02f,.035f,.048f,.99f)); ActionTooltip->SetPadding(FMargin(14));
	auto* TooltipBody=WidgetTree->ConstructWidget<UVerticalBox>(); ActionTooltip->SetContent(TooltipBody);
	ActionTooltipTitle=MakeConsoleText(TEXT("TXT_SelectionTooltipTitle"),TEXT(""),17);
	ActionTooltipTitle->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),17)); ActionTooltipTitle->SetAutoWrapText(true); ActionTooltipTitle->SetWrapTextAt(352);
	TooltipBody->AddChildToVerticalBox(ActionTooltipTitle)->SetPadding(FMargin(0,0,0,6));
	ActionTooltipText=MakeConsoleText(TEXT("TXT_SelectionTooltip"),TEXT(""),14); ActionTooltipText->SetAutoWrapText(true); ActionTooltipText->SetWrapTextAt(352);
	TooltipBody->AddChildToVerticalBox(ActionTooltipText);
	PlaceConsoleWidget(Root,ActionTooltip,{0,0},{380,160},400);
	ActionTooltip->SetVisibility(ESlateVisibility::Collapsed);
}
