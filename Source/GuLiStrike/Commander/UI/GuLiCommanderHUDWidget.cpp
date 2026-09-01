// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/UI/GuLiCommanderHUDWidget.h"

#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/UI/GuLiCommanderMiniMapWidget.h"
#include "Commander/UI/GuLiCommanderUnitTypeSummary.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameState.h"
#include "TimerManager.h"

namespace GuLiCommanderHUDWidget
{
	const FLinearColor Cyan(0.094f, 0.843f, 1.0f, 1.0f);
	const FLinearColor Metal(0.820f, 0.686f, 0.525f, 1.0f);

	FText TeamText(const EGuLiTeam Team)
	{
		switch (Team)
		{
		case EGuLiTeam::Blue:
			return FText::FromString(TEXT("蓝方"));
		case EGuLiTeam::Red:
			return FText::FromString(TEXT("红方"));
		default:
			return FText::FromString(TEXT("未分配"));
		}
	}

	FText RoleText(const EGuLiCommanderRole Role)
	{
		switch (Role)
		{
		case EGuLiCommanderRole::Commander:
			return FText::FromString(TEXT("指挥官"));
		case EGuLiCommanderRole::Ground:
			return FText::FromString(TEXT("地面"));
		case EGuLiCommanderRole::Air:
			return FText::FromString(TEXT("空中"));
		case EGuLiCommanderRole::Observer:
			return FText::FromString(TEXT("观察"));
		default:
			return FText::FromString(TEXT("未分配"));
		}
	}
}

FBox2D GuLiCommanderHUDLayout::PlaceTooltip(
	const FBox2D& Anchor, const FVector2D& DesiredSize, const FVector2D& ViewportSize)
{
	const FVector2D ViewSize(FMath::Max(1.0, ViewportSize.X), FMath::Max(1.0, ViewportSize.Y));
	const FVector2D Margin(FMath::Min(8.0, ViewSize.X * 0.1), FMath::Min(8.0, ViewSize.Y * 0.1));
	const FVector2D Size(
		FMath::Clamp(DesiredSize.X, 1.0, FMath::Max(1.0, ViewSize.X - Margin.X * 2.0)),
		FMath::Clamp(DesiredSize.Y, 1.0, FMath::Max(1.0, ViewSize.Y - Margin.Y * 2.0)));
	FVector2D Position(Anchor.Min.X, Anchor.Min.Y - Size.Y - 8.0);
	if (Position.Y < Margin.Y)
	{
		Position.Y = Anchor.Max.Y + 8.0;
	}
	Position.X = FMath::Clamp(Position.X, Margin.X, ViewSize.X - Margin.X - Size.X);
	Position.Y = FMath::Clamp(Position.Y, Margin.Y, ViewSize.Y - Margin.Y - Size.Y);
	return FBox2D(Position, Position + Size);
}

UGuLiCommanderHUDWidget::UGuLiCommanderHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FGuLiCommanderShortcutEntry SameType;
	SameType.ButtonName = TEXT("BTN_Shortcut_SameType");
	SameType.IconName = TEXT("I_Shortcut_SameType");
	SameType.Tooltip = FText::FromString(TEXT(
		"Alt + 左键点击士兵\n选择同兵种：以该士兵为中心，从近到远选择最多 1000 人；附近不足时从全地图补足。\nAlt + Shift + 左键：追加到当前选择。\n只选择己方可控、存活的同兵种。"));
	FGuLiCommanderShortcutEntry AddSelection;
	AddSelection.ButtonName = TEXT("BTN_Shortcut_AddSelection");
	AddSelection.IconName = TEXT("I_Shortcut_AddSelection");
	AddSelection.Tooltip = FText::FromString(TEXT(
		"Shift + 左键点击 / 拖拽 / 范围选取\n追加选择：保留已选单位，再加入本次命中的单位。\n重复命中不会取消或重复计数；未命中时保持原选择。"));
	ShortcutEntries = {SameType, AddSelection};
}

void UGuLiCommanderHUDWidget::InitializeForController(
	AGuLiCommanderPlayerController* InController)
{
	if (CommanderController.Get() == InController)
	{
		return;
	}

	UnbindRuntimeSources();
	CachedSelection = FGuLiCommanderSelectionState();
	CachedCommandAck = FGuLiCommandAck();
	InitialBlueRoster = 0;
	InitialRedRoster = 0;
	CachedRosterMatchEpoch = 0u;
	CachedSelectionMatchEpoch = 0u;
	CommanderController = InController;
	if (MiniMapWidget)
	{
		MiniMapWidget->InitializeForController(InController);
	}
	if (bNativeConstructed)
	{
		BindRuntimeSources();
		RefreshInitialState();
	}
}

void UGuLiCommanderHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bNativeConstructed = true;

	if (IsDesignTime())
	{
		return;
	}

	if (!CommanderController.IsValid())
	{
		CommanderController = Cast<AGuLiCommanderPlayerController>(GetOwningPlayer());
	}
	WidgetCache.Reset();
	if (WidgetTree)
	{
		TArray<UWidget*> AllWidgets;
		WidgetTree->GetAllWidgets(AllWidgets);
		WidgetCache.Reserve(AllWidgets.Num());
		for (UWidget* Widget : AllWidgets)
		{
			if (Widget)
			{
				WidgetCache.Add(Widget->GetFName(), Widget);
			}
		}
	}

	BuildMiniMapLayer();
	HideReviewOnlyMapWidgets();
	BindShortcutControls();
	BoxSelectionTexture = LoadObject<UTexture2D>(nullptr,
		TEXT("/Game/Commander/UI/Textures/Icons/T_UI_Cmd_Box.T_UI_Cmd_Box"));
	RadiusSelectionTexture = LoadObject<UTexture2D>(nullptr,
		TEXT("/Game/Commander/UI/Textures/Icons/T_UI_Cmd_Radius.T_UI_Cmd_Radius"));

	if (UButton* MoveButton = Cast<UButton>(FindRuntimeWidget(TEXT("BTN_Cmd_Move"))))
	{
		MoveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleMoveClicked);
	}
	if (UButton* SelectButton = Cast<UButton>(FindRuntimeWidget(TEXT("BTN_Cmd_Select"))))
	{
		SelectButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSelectClicked);
	}
	if (UButton* MapButton = Cast<UButton>(FindRuntimeWidget(TEXT("BTN_MiniMapJump"))))
	{
		MapButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleMiniMapClicked);
	}

	BindRuntimeSources();
	RefreshInitialState();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SourceResolveTimer,
			this,
			&ThisClass::ResolveRuntimeSources,
			0.25f,
			true);
		World->GetTimerManager().SetTimer(
			ElapsedTimeTimer,
			this,
			&ThisClass::RefreshElapsedTime,
			1.0f,
			true);
	}
}

void UGuLiCommanderHUDWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SourceResolveTimer);
		World->GetTimerManager().ClearTimer(ElapsedTimeTimer);
	}
	UnbindRuntimeSources();
	UnbindShortcutControls();
	HideShortcutTooltip();
	for (const FName ButtonName : {FName(TEXT("BTN_Cmd_Move")), FName(TEXT("BTN_Cmd_Select")), FName(TEXT("BTN_MiniMapJump"))})
	{
		if (UButton* Button = Cast<UButton>(FindRuntimeWidget(ButtonName)))
		{
			Button->OnClicked.RemoveAll(this);
		}
	}
	CachedSelection = FGuLiCommanderSelectionState();
	CachedCommandAck = FGuLiCommandAck();
	CachedSelectionMatchEpoch = 0u;
	WidgetCache.Reset();
	bNativeConstructed = false;
	Super::NativeDestruct();
}

void UGuLiCommanderHUDWidget::ResolveRuntimeSources()
{
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> NewNetSync;
	TWeakObjectPtr<AGuLiBattlePlayerState> NewPlayerState;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> NewReplicator = SoldierStateReplicator;
	if (NewReplicator.IsValid() && NewReplicator->GetWorld() != GetWorld())
	{
		NewReplicator.Reset();
	}

	if (AGuLiCommanderPlayerController* Controller = CommanderController.Get())
	{
		NewNetSync = Controller->GetCommanderNetSyncComponent();
		NewPlayerState = Controller->GetPlayerState<AGuLiBattlePlayerState>();
	}

	if (!NewReplicator.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
			{
				NewReplicator = *It;
				break;
			}
		}
	}

	if (NewNetSync != NetSyncComponent
		|| NewPlayerState != CommanderPlayerState
		|| NewReplicator != SoldierStateReplicator)
	{
		const bool bSelectionSourceChanged = NewNetSync != NetSyncComponent;
		UnbindRuntimeSources();
		NetSyncComponent = NewNetSync;
		CommanderPlayerState = NewPlayerState;
		SoldierStateReplicator = NewReplicator;
		BindRuntimeSources();
		if (bSelectionSourceChanged)
		{
			CachedSelection = FGuLiCommanderSelectionState();
			CachedCommandAck = FGuLiCommandAck();
			CachedSelectionMatchEpoch = 0u;
			if (const UGuLiCommanderNetSyncComponent* NetSync = NetSyncComponent.Get())
			{
				CachedSelection = NetSync->GetSelectionState();
				CachedCommandAck = NetSync->GetLastCommandAck();
				CachedSelectionMatchEpoch = GetCurrentMatchEpoch();
			}
		}
		RefreshPlayerState();
		RefreshSoldierStates(
			SoldierStateReplicator.IsValid()
				? SoldierStateReplicator->GetSnapshotRevision()
				: 0u);
	}
}

// 网络边界已在上游处理；HUD 只订阅本地通知，不拥有 RPC 或权威状态写权限。
void UGuLiCommanderHUDWidget::BindRuntimeSources()
{
	if (!bNativeConstructed || IsDesignTime() || bRuntimeSourcesBound)
	{
		return;
	}

	if (AGuLiCommanderPlayerController* Controller = CommanderController.Get())
	{
		NetSyncComponent = Controller->GetCommanderNetSyncComponent();
		CommanderPlayerState = Controller->GetPlayerState<AGuLiBattlePlayerState>();
		ToolModeChangedHandle = Controller->OnCommanderToolModeChanged.AddUObject(
			this,
			&ThisClass::HandleToolModeChanged);
		SelectionShapeChangedHandle = Controller->OnSelectionShapeChanged.AddUObject(
			this, &ThisClass::HandleSelectionShapeChanged);
		SelectionRadiusChangedHandle = Controller->OnSelectionRadiusPresetChanged.AddUObject(
			this, &ThisClass::HandleSelectionRadiusChanged);
	}

	if (UGuLiCommanderNetSyncComponent* NetSync = NetSyncComponent.Get())
	{
		SelectionChangedHandle = NetSync->OnSelectionChanged.AddUObject(
			this,
			&ThisClass::RefreshSelection);
		CommandAckChangedHandle = NetSync->OnCommandAckChanged.AddUObject(
			this,
			&ThisClass::RefreshCommandAck);
	}

	if (AGuLiBattlePlayerState* PlayerState = CommanderPlayerState.Get())
	{
		PlayerState->OnCommanderPlayerStateChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandlePlayerStateChanged);
	}

	if (AGuLiSoldierStateReplicator* Replicator = SoldierStateReplicator.Get())
	{
		SoldierStatesChangedHandle = Replicator->OnSoldierStatesChanged.AddUObject(
			this,
			&ThisClass::RefreshSoldierStates);
	}
	bRuntimeSourcesBound = true;
}

// 解绑旧数据源，避免控件销毁/重建后重复响应同一条复制或 ACK 通知。
void UGuLiCommanderHUDWidget::UnbindRuntimeSources()
{
	if (AGuLiCommanderPlayerController* Controller = CommanderController.Get())
	{
		Controller->OnCommanderToolModeChanged.Remove(ToolModeChangedHandle);
		Controller->OnSelectionShapeChanged.Remove(SelectionShapeChangedHandle);
		Controller->OnSelectionRadiusPresetChanged.Remove(SelectionRadiusChangedHandle);
	}
	if (UGuLiCommanderNetSyncComponent* NetSync = NetSyncComponent.Get())
	{
		NetSync->OnSelectionChanged.Remove(SelectionChangedHandle);
		NetSync->OnCommandAckChanged.Remove(CommandAckChangedHandle);
	}
	if (AGuLiBattlePlayerState* PlayerState = CommanderPlayerState.Get())
	{
		PlayerState->OnCommanderPlayerStateChanged.RemoveDynamic(
			this,
			&ThisClass::HandlePlayerStateChanged);
	}
	if (AGuLiSoldierStateReplicator* Replicator = SoldierStateReplicator.Get())
	{
		Replicator->OnSoldierStatesChanged.Remove(SoldierStatesChangedHandle);
	}

	SelectionChangedHandle.Reset();
	CommandAckChangedHandle.Reset();
	SoldierStatesChangedHandle.Reset();
	ToolModeChangedHandle.Reset();
	SelectionShapeChangedHandle.Reset();
	SelectionRadiusChangedHandle.Reset();
	bRuntimeSourcesBound = false;
}

// 首次绑定要主动读取当前副本，避免订阅之前已到达的复制状态没有再次触发事件。
void UGuLiCommanderHUDWidget::RefreshInitialState()
{
	ResolveRuntimeSources();
	SetText(TEXT("TXT_Target"), FText::FromString(TEXT("目标未启用")));
	SetText(TEXT("TXT_Score"), FText::FromString(TEXT("-- : --")));
	SetText(TEXT("TXT_TeamA"), FText::FromString(TEXT("蓝方")));
	SetText(TEXT("TXT_TeamB"), FText::FromString(TEXT("红方")));
	SetText(TEXT("TXT_TacticalValue"), FText::FromString(TEXT("--")));
	SetText(TEXT("TXT_Energy"), FText::FromString(TEXT("轨道能量  --")));
	SetText(TEXT("TXT_SquadsTitle"), FText::FromString(TEXT("编队")));
	SetText(TEXT("TXT_CommandTitle"), FText::FromString(TEXT("指令矩阵")));
	SetImageFraction(TEXT("I_EnergyFill"), 0.0f);

	if (const UGuLiCommanderNetSyncComponent* NetSync = NetSyncComponent.Get())
	{
		CachedSelection = NetSync->GetSelectionState();
		CachedCommandAck = NetSync->GetLastCommandAck();
		CachedSelectionMatchEpoch = GetCurrentMatchEpoch();
	}
	else
	{
		CachedSelection = FGuLiCommanderSelectionState();
		CachedCommandAck = FGuLiCommandAck();
		CachedSelectionMatchEpoch = 0u;
	}
	RefreshElapsedTime();
	RefreshPlayerState();
	RefreshSoldierStates(
		SoldierStateReplicator.IsValid()
			? SoldierStateReplicator->GetSnapshotRevision()
			: 0u);
	RefreshCommandControls();
}

void UGuLiCommanderHUDWidget::RefreshSelection(
	const FGuLiCommanderSelectionState& Selection)
{
	CachedSelection = Selection;
	CachedSelectionMatchEpoch = GetCurrentMatchEpoch();
	if (Selection.Cohorts.IsEmpty())
	{
		CachedCommandAck = FGuLiCommandAck();
	}
	RefreshUnitTypeCard();
	RefreshCommandControls();
	if (MiniMapWidget)
	{
		MiniMapWidget->RequestImmediateRefresh();
	}
}

void UGuLiCommanderHUDWidget::RefreshCommandAck(const FGuLiCommandAck& Ack)
{
	CachedCommandAck = Ack;
	RefreshUnitTypeCard();
}

void UGuLiCommanderHUDWidget::RefreshSoldierStates(const uint32 SnapshotRevision)
{
	(void)SnapshotRevision;
	if (const AGuLiSoldierStateReplicator* Replicator = SoldierStateReplicator.Get())
	{
		const uint32 MatchEpoch = Replicator->GetSnapshotMatchEpoch();
		if (MatchEpoch != 0u && MatchEpoch != CachedRosterMatchEpoch)
		{
			CachedRosterMatchEpoch = MatchEpoch;
			InitialBlueRoster = 0;
			InitialRedRoster = 0;
			if (CachedSelectionMatchEpoch != 0u && CachedSelectionMatchEpoch != MatchEpoch)
			{
				CachedSelection = FGuLiCommanderSelectionState();
				CachedCommandAck = FGuLiCommandAck();
				CachedSelectionMatchEpoch = MatchEpoch;
			}
		}
	}
	RefreshCoreAndRoster();
	RefreshUnitTypeCard();
	RefreshCommandControls();
	if (MiniMapWidget)
	{
		MiniMapWidget->RequestImmediateRefresh();
	}
}

void UGuLiCommanderHUDWidget::RefreshPlayerState()
{
	RefreshCoreAndRoster();
	RefreshUnitTypeCard();
	RefreshCommandControls();
}

void UGuLiCommanderHUDWidget::HandlePlayerStateChanged()
{
	RefreshPlayerState();
}

void UGuLiCommanderHUDWidget::RefreshElapsedTime()
{
	int32 ElapsedSeconds = 0;
	if (const UWorld* World = GetWorld())
	{
		if (const AGameState* GameState = World->GetGameState<AGameState>())
		{
			ElapsedSeconds = FMath::Max(0, GameState->ElapsedTime);
		}
	}
	SetText(
		TEXT("TXT_Time"),
		FText::FromString(FString::Printf(
			TEXT("%02d:%02d"),
			ElapsedSeconds / 60,
			ElapsedSeconds % 60)));
}

void UGuLiCommanderHUDWidget::RefreshCoreAndRoster()
{
	const AGuLiBattlePlayerState* PlayerState = CommanderPlayerState.Get();
	const EGuLiTeam LocalTeam = PlayerState ? PlayerState->GetTeam() : EGuLiTeam::Unassigned;
	const EGuLiCommanderRole Role = PlayerState
		? PlayerState->GetCommanderRole()
		: EGuLiCommanderRole::Unassigned;
	const bool bOnline = PlayerState && PlayerState->IsBattleReady();
	const bool bRosterReady = PlayerState && PlayerState->IsSoldierStreamReady();
	SetText(
		TEXT("TXT_CoreSystem"),
		FText::Format(
			FText::FromString(TEXT("{0} · {1} · {2}")),
			GuLiCommanderHUDWidget::TeamText(LocalTeam),
			GuLiCommanderHUDWidget::RoleText(Role),
			FText::FromString(bOnline ? TEXT("在线") : TEXT("同步中"))));

	int32 BlueTotal = 0;
	int32 BlueAlive = 0;
	int32 RedTotal = 0;
	int32 RedAlive = 0;
	if (const AGuLiSoldierStateReplicator* Replicator = SoldierStateReplicator.Get())
	{
		for (const FGuLiSoldierStateItem& Soldier : Replicator->GetItems())
		{
			if (Soldier.Team == EGuLiTeam::Blue)
			{
				++BlueTotal;
				BlueAlive += Soldier.IsAlive() ? 1 : 0;
			}
			else if (Soldier.Team == EGuLiTeam::Red)
			{
				++RedTotal;
				RedAlive += Soldier.IsAlive() ? 1 : 0;
			}
		}
	}
	InitialBlueRoster = FMath::Max(InitialBlueRoster, BlueTotal);
	InitialRedRoster = FMath::Max(InitialRedRoster, RedTotal);

	int32 Alive = 0;
	int32 Initial = 0;
	if (LocalTeam == EGuLiTeam::Blue)
	{
		Alive = BlueAlive;
		Initial = InitialBlueRoster;
	}
	else if (LocalTeam == EGuLiTeam::Red)
	{
		Alive = RedAlive;
		Initial = InitialRedRoster;
	}
	SetText(
		TEXT("TXT_RosterValue"),
		FText::FromString(
			bRosterReady && Initial > 0
				? FString::Printf(TEXT("%d / %d"), Alive, Initial)
				: FString(TEXT("-- / --"))));
	SetImageFraction(
		TEXT("I_RosterFill"),
		bRosterReady && Initial > 0 ? static_cast<float>(Alive) / static_cast<float>(Initial) : 0.0f);
}

uint32 UGuLiCommanderHUDWidget::GetCurrentMatchEpoch() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGuLiBattleGameState* GameState = World->GetGameState<AGuLiBattleGameState>())
		{
			return GameState->GetMatchEpoch();
		}
	}
	return 0u;
}

void UGuLiCommanderHUDWidget::RefreshUnitTypeCard()
{
	const uint32 MatchEpoch = GetCurrentMatchEpoch();
	if (MatchEpoch != 0u && CachedSelectionMatchEpoch != 0u
		&& CachedSelectionMatchEpoch != MatchEpoch)
	{
		// Soldier IDs are match-local. Never resolve last match's selection against a new roster.
		CachedSelection = FGuLiCommanderSelectionState();
		CachedCommandAck = FGuLiCommandAck();
		CachedSelectionMatchEpoch = MatchEpoch;
	}
	const AGuLiSoldierStateReplicator* Replicator = SoldierStateReplicator.Get();
	const AGuLiBattlePlayerState* PlayerState = CommanderPlayerState.Get();
	const bool bReliableStateReady = PlayerState && PlayerState->IsSoldierStreamReady()
		&& Replicator && Replicator->GetSnapshotRevision() != 0u
		&& MatchEpoch != 0u && Replicator->GetSnapshotMatchEpoch() == MatchEpoch;
	if (bReliableStateReady && CachedSelectionMatchEpoch == 0u)
	{
		// Selection can arrive before replicated match metadata during initial bootstrap.
		CachedSelectionMatchEpoch = MatchEpoch;
	}
	const FGuLiCommanderUnitTypeSummary Summary = BuildGuLiCommanderUnitTypeSummary(
		CachedSelection,
		CachedCommandAck,
		Replicator ? MakeArrayView(Replicator->GetItems()) : TConstArrayView<FGuLiSoldierStateItem>(),
		bReliableStateReady && CachedSelectionMatchEpoch == MatchEpoch);

	if (UWidget* Card = FindRuntimeWidget(TEXT("C_UnitTypeCard")))
	{
		Card->SetVisibility(Summary.IsVisible()
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	SetText(TEXT("TXT_UnitTypeName"), FText::FromString(TEXT("士兵")));
	SetText(
		TEXT("TXT_UnitTypeCount"),
		FText::FromString(Summary.bSyncing
			? FString(TEXT("-- 人"))
			: FString::Printf(TEXT("%d 人"), Summary.AliveCount)));
	SetText(
		TEXT("TXT_UnitTypeHealth"),
		FText::FromString(Summary.bSyncing
			? FString(TEXT("-- / --"))
			: FString::Printf(TEXT("%.2f / %.2f"), Summary.TotalHealth, Summary.TotalMaxHealth)));
	SetText(TEXT("TXT_UnitTypeStatus"), Summary.CommandStatus);
	SetImageFraction(TEXT("I_UnitTypeHealthFill"), Summary.GetHealthFraction());
}

void UGuLiCommanderHUDWidget::RefreshCommandControls()
{
	const AGuLiCommanderPlayerController* Controller = CommanderController.Get();
	const bool bSelect = !Controller
		|| Controller->GetCommanderToolMode() == EGuLiCommanderToolMode::Select;
	const bool bCanMove = Controller
		&& Controller->CanIssueCommanderOrders()
		&& !CachedSelection.Cohorts.IsEmpty();

	for (int32 SlotIndex = 1; SlotIndex <= 5; ++SlotIndex)
	{
		SetCommandSlotOpacity(SlotIndex, 0.26f);
	}
	SetCommandSlotOpacity(0, bCanMove ? 1.0f : 0.38f);
	SetCommandSlotOpacity(6, 1.0f);

	if (UImage* MoveOuter = FindImage(TEXT("I_CmdOuter_00")))
	{
		MoveOuter->SetColorAndOpacity(
			!bSelect ? GuLiCommanderHUDWidget::Cyan : GuLiCommanderHUDWidget::Metal);
	}
	if (UImage* SelectOuter = FindImage(TEXT("I_CmdOuter_06")))
	{
		SelectOuter->SetColorAndOpacity(
			bSelect ? GuLiCommanderHUDWidget::Cyan : GuLiCommanderHUDWidget::Metal);
	}
	if (UButton* MoveButton = Cast<UButton>(FindRuntimeWidget(TEXT("BTN_Cmd_Move"))))
	{
		MoveButton->SetIsEnabled(bCanMove);
	}
	const bool bRadius = Controller && Controller->GetSelectionShape() == EGuLiCommanderSelectionShape::Radius;
	SetText(TEXT("TXT_CmdLabel_Select"), FText::FromString(bRadius ? TEXT("范围") : TEXT("框选")));
	if (UImage* SelectionIcon = FindImage(TEXT("I_CmdIcon_Select")))
	{
		SelectionIcon->SetBrushFromTexture(bRadius ? RadiusSelectionTexture.Get() : BoxSelectionTexture.Get());
	}
	if (UButton* SelectButton = Cast<UButton>(FindRuntimeWidget(TEXT("BTN_Cmd_Select"))))
	{
		const int32 RadiusMeters = Controller
			? FMath::RoundToInt(GuLiCommanderProtocol::GetSelectionRadiusCentimeters(Controller->GetSelectionRadiusPreset()) / 100.0f)
			: 80;
		SelectButton->SetToolTipText(FText::FromString(bRadius
			? FString::Printf(TEXT("7：切换为矩形框选\n当前范围半径 %dm；+ 切换 80 / 200 / 450m。\nShift：追加选择。"), RadiusMeters)
			: FString(TEXT("7：切换为范围选取\n左键点击单选；按住左键拖拽框选。\nShift：追加选择。"))));
	}
}

void UGuLiCommanderHUDWidget::HandleToolModeChanged(const EGuLiCommanderToolMode NewMode)
{
	(void)NewMode;
	RefreshCommandControls();
}

void UGuLiCommanderHUDWidget::HandleSelectionShapeChanged(const EGuLiCommanderSelectionShape NewShape)
{
	(void)NewShape;
	RefreshCommandControls();
}

void UGuLiCommanderHUDWidget::HandleSelectionRadiusChanged(const EGuLiSelectionRadiusPreset NewPreset)
{
	(void)NewPreset;
	RefreshCommandControls();
}

void UGuLiCommanderHUDWidget::HandleMoveClicked()
{
	const AGuLiCommanderPlayerController* InputOwner = CommanderController.Get();
	if (!InputOwner || !InputOwner->IsCommanderViewActive())
	{
		return;
	}

	if (AGuLiCommanderPlayerController* Controller = CommanderController.Get())
	{
		Controller->ArmMoveTool();
	}
}

void UGuLiCommanderHUDWidget::HandleSelectClicked()
{
	const AGuLiCommanderPlayerController* InputOwner = CommanderController.Get();
	if (!InputOwner || !InputOwner->IsCommanderViewActive())
	{
		return;
	}

	if (AGuLiCommanderPlayerController* Controller = CommanderController.Get())
	{
		Controller->ToggleSelectionShape();
	}
}

void UGuLiCommanderHUDWidget::BindShortcutControls()
{
	for (const FGuLiCommanderShortcutEntry& Entry : ShortcutEntries)
	{
		if (UButton* Button = Cast<UButton>(FindRuntimeWidget(Entry.ButtonName)))
		{
			// A button consumes presses/releases but deliberately has no command callback.
			Button->SetIsEnabled(true);
			Button->SetRenderOpacity(1.0f);
			Button->OnHovered.AddUniqueDynamic(this, &ThisClass::HandleShortcutHovered);
			Button->OnUnhovered.AddUniqueDynamic(this, &ThisClass::HandleShortcutUnhovered);
		}
		if (UWidget* Icon = FindRuntimeWidget(Entry.IconName))
		{
			Icon->SetRenderOpacity(1.0f);
		}
	}
	HideShortcutTooltip();
}

void UGuLiCommanderHUDWidget::UnbindShortcutControls()
{
	for (const FGuLiCommanderShortcutEntry& Entry : ShortcutEntries)
	{
		if (UButton* Button = Cast<UButton>(FindRuntimeWidget(Entry.ButtonName)))
		{
			Button->OnHovered.RemoveDynamic(this, &ThisClass::HandleShortcutHovered);
			Button->OnUnhovered.RemoveDynamic(this, &ThisClass::HandleShortcutUnhovered);
		}
	}
}

void UGuLiCommanderHUDWidget::HandleShortcutHovered()
{
	for (const FGuLiCommanderShortcutEntry& Entry : ShortcutEntries)
	{
		const UButton* Button = Cast<UButton>(FindRuntimeWidget(Entry.ButtonName));
		if (Button && Button->IsHovered())
		{
			ActiveShortcutButton = Entry.ButtonName;
			SetText(TEXT("TXT_SelectionTooltip"), Entry.Tooltip);
			if (UWidget* Tooltip = FindRuntimeWidget(TEXT("C_SelectionTooltip")))
			{
				Tooltip->SetVisibility(ESlateVisibility::HitTestInvisible);
				Tooltip->ForceLayoutPrepass();
			}
			PositionShortcutTooltip();
			return;
		}
	}
}

void UGuLiCommanderHUDWidget::HandleShortcutUnhovered()
{
	HideShortcutTooltip();
	// Enter/leave may share a Slate event. Resolve another active icon, if any.
	HandleShortcutHovered();
}

void UGuLiCommanderHUDWidget::HideShortcutTooltip()
{
	ActiveShortcutButton = NAME_None;
	if (UWidget* Tooltip = FindRuntimeWidget(TEXT("C_SelectionTooltip")))
	{
		Tooltip->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UGuLiCommanderHUDWidget::PositionShortcutTooltip()
{
	UWidget* Tooltip = FindRuntimeWidget(TEXT("C_SelectionTooltip"));
	const UWidget* Anchor = FindRuntimeWidget(ActiveShortcutButton);
	const UWidget* Root = FindRuntimeWidget(TEXT("RootCanvas"));
	UTextBlock* TooltipText = FindText(TEXT("TXT_SelectionTooltip"));
	UCanvasPanelSlot* TooltipSlot = Tooltip ? Cast<UCanvasPanelSlot>(Tooltip->Slot) : nullptr;
	if (!TooltipSlot || !Anchor || !Root || !TooltipText)
	{
		return;
	}
	const FGeometry& RootGeometry = Root->GetCachedGeometry();
	const FVector2D ViewSize = RootGeometry.GetLocalSize();
	if (ViewSize.X <= 1.0 || ViewSize.Y <= 1.0)
	{
		return;
	}
	const double Width = FMath::Min(368.0, FMath::Max(1.0, ViewSize.X - 16.0));
	TooltipText->SetWrapTextAt(FMath::Max(1.0, Width - 24.0));
	TooltipText->ForceLayoutPrepass();
	const double Height = TooltipText->GetDesiredSize().Y + 24.0;
	const FGeometry& AnchorGeometry = Anchor->GetCachedGeometry();
	const FBox2D AnchorRect(
		RootGeometry.AbsoluteToLocal(AnchorGeometry.LocalToAbsolute(FVector2D::ZeroVector)),
		RootGeometry.AbsoluteToLocal(AnchorGeometry.LocalToAbsolute(AnchorGeometry.GetLocalSize())));
	const FBox2D Bounds = GuLiCommanderHUDLayout::PlaceTooltip(AnchorRect, FVector2D(Width, Height), ViewSize);
	TooltipSlot->SetPosition(Bounds.Min);
	TooltipSlot->SetSize(Bounds.GetSize());
}

FReply UGuLiCommanderHUDWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!ActiveShortcutButton.IsNone())
	{
		PositionShortcutTooltip();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void UGuLiCommanderHUDWidget::HandleMiniMapClicked()
{
	const AGuLiCommanderPlayerController* InputOwner = CommanderController.Get();
	if (!InputOwner || !InputOwner->IsCommanderViewActive())
	{
		return;
	}

	if (!MiniMapWidget)
	{
		return;
	}
	if (AGuLiCommanderPlayerController* Controller = CommanderController.Get())
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (Controller->GetMousePosition(MouseX, MouseY))
		{
			MiniMapWidget->HandleMapClickAtScreenPosition(FVector2D(MouseX, MouseY));
		}
	}
}

void UGuLiCommanderHUDWidget::BuildMiniMapLayer()
{
	if (MiniMapWidget || !CommanderController.IsValid())
	{
		return;
	}
	UPanelWidget* MapPanel = Cast<UPanelWidget>(FindRuntimeWidget(TEXT("C_Map")));
	if (!MapPanel)
	{
		return;
	}

	MiniMapWidget = CreateWidget<UGuLiCommanderMiniMapWidget>(
		CommanderController.Get(),
		UGuLiCommanderMiniMapWidget::StaticClass());
	if (!MiniMapWidget)
	{
		return;
	}
	MiniMapWidget->InitializeForController(CommanderController.Get());
	MapPanel->AddChild(MiniMapWidget);
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(MiniMapWidget->Slot))
	{
		CanvasSlot->SetPosition(FVector2D(8.0f, 8.0f));
		CanvasSlot->SetSize(FVector2D(260.0f, 260.0f));
		CanvasSlot->SetZOrder(100);
	}
	MiniMapWidget->SetVisibility(ESlateVisibility::Visible);
}

void UGuLiCommanderHUDWidget::HideReviewOnlyMapWidgets()
{
	if (!WidgetTree)
	{
		return;
	}
	TArray<UWidget*> Widgets;
	WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Widget : Widgets)
	{
		if (!Widget)
		{
			continue;
		}
		const FString Name = Widget->GetName();
		if (Name.StartsWith(TEXT("I_MapPip_"))
			|| Name.StartsWith(TEXT("I_MapView")))
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

UTextBlock* UGuLiCommanderHUDWidget::FindText(const FName WidgetName) const
{
	return Cast<UTextBlock>(FindRuntimeWidget(WidgetName));
}

UImage* UGuLiCommanderHUDWidget::FindImage(const FName WidgetName) const
{
	return Cast<UImage>(FindRuntimeWidget(WidgetName));
}

UWidget* UGuLiCommanderHUDWidget::FindRuntimeWidget(const FName WidgetName) const
{
	if (const TWeakObjectPtr<UWidget>* CachedWidget = WidgetCache.Find(WidgetName))
	{
		return CachedWidget->Get();
	}
	return nullptr;
}

void UGuLiCommanderHUDWidget::SetText(
	const FName WidgetName,
	const FText& Value) const
{
	if (UTextBlock* Text = FindText(WidgetName))
	{
		Text->SetText(Value);
	}
}

void UGuLiCommanderHUDWidget::SetImageFraction(
	const FName WidgetName,
	const float Fraction) const
{
	if (UImage* Image = FindImage(WidgetName))
	{
		Image->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
		Image->SetRenderScale(FVector2D(FMath::Clamp(Fraction, 0.0f, 1.0f), 1.0f));
	}
}

void UGuLiCommanderHUDWidget::SetCommandSlotOpacity(
	const int32 SlotIndex,
	const float Opacity) const
{
	const FString SlotSuffix = FString::Printf(TEXT("%02d"), SlotIndex);
	if (UWidget* Outer = FindRuntimeWidget(
		*FString::Printf(TEXT("I_CmdOuter_%s"), *SlotSuffix)))
	{
		Outer->SetRenderOpacity(Opacity);
	}
	if (UWidget* Background = FindRuntimeWidget(
		*FString::Printf(TEXT("I_CmdBG_%s"), *SlotSuffix)))
	{
		Background->SetRenderOpacity(Opacity);
	}

	static const TCHAR* CommandNames[] = {
		TEXT("Move"), TEXT("Attack"), TEXT("Stop"), TEXT("Hold"), TEXT("Patrol"), TEXT("Rally")
	};
	if (SlotIndex >= 0 && SlotIndex < UE_ARRAY_COUNT(CommandNames))
	{
		const TCHAR* CommandName = CommandNames[SlotIndex];
		const TCHAR* Prefixes[] = {TEXT("I_CmdIcon_"), TEXT("TXT_CmdKey_"), TEXT("TXT_CmdLabel_")};
		for (const TCHAR* Prefix : Prefixes)
		{
			if (UWidget* Widget = FindRuntimeWidget(*FString::Printf(TEXT("%s%s"), Prefix, CommandName)))
			{
				Widget->SetRenderOpacity(Opacity);
			}
		}
	}
}

bool UGuLiCommanderHUDWidget::IsWidgetGeometryHit(
	const UWidget* Widget,
	const FVector2D& ScreenPixelPosition) const
{
	if (!Widget || Widget->GetVisibility() == ESlateVisibility::Collapsed)
	{
		return false;
	}
	const FGeometry& Geometry = Widget->GetCachedGeometry();
	const FVector2D LocalSize = Geometry.GetLocalSize();
	if (LocalSize.X <= 1.0f || LocalSize.Y <= 1.0f)
	{
		return false;
	}
	FVector2D PixelMinimum;
	FVector2D ViewportMinimum;
	FVector2D PixelMaximum;
	FVector2D ViewportMaximum;
	USlateBlueprintLibrary::LocalToViewport(
		this,
		Geometry,
		FVector2D::ZeroVector,
		PixelMinimum,
		ViewportMinimum);
	USlateBlueprintLibrary::LocalToViewport(
		this,
		Geometry,
		LocalSize,
		PixelMaximum,
		ViewportMaximum);
	return FBox2D(PixelMinimum, PixelMaximum).IsInsideOrOn(ScreenPixelPosition);
}

bool UGuLiCommanderHUDWidget::IsWidgetGeometryReady(const UWidget* Widget) const
{
	if (!Widget || Widget->GetVisibility() == ESlateVisibility::Collapsed)
	{
		return false;
	}
	const FVector2D LocalSize = Widget->GetCachedGeometry().GetLocalSize();
	return LocalSize.X > 1.0f && LocalSize.Y > 1.0f;
}

bool UGuLiCommanderHUDWidget::IsScreenPositionBlocked(
	const FVector2D& ScreenPixelPosition) const
{
	return IsWidgetGeometryHit(FindRuntimeWidget(TEXT("SB_TopStatus")), ScreenPixelPosition)
		|| IsWidgetGeometryHit(FindRuntimeWidget(TEXT("SB_MapDesign")), ScreenPixelPosition)
		|| IsWidgetGeometryHit(FindRuntimeWidget(TEXT("SB_DockDesign")), ScreenPixelPosition)
		|| IsWidgetGeometryHit(FindRuntimeWidget(TEXT("SB_ShortcutsDesign")), ScreenPixelPosition);
}

bool UGuLiCommanderHUDWidget::HasValidBlockingGeometry() const
{
	return IsWidgetGeometryReady(FindRuntimeWidget(TEXT("SB_TopStatus")))
		&& IsWidgetGeometryReady(FindRuntimeWidget(TEXT("SB_MapDesign")))
		&& IsWidgetGeometryReady(FindRuntimeWidget(TEXT("SB_DockDesign")))
		&& IsWidgetGeometryReady(FindRuntimeWidget(TEXT("SB_ShortcutsDesign")));
}
