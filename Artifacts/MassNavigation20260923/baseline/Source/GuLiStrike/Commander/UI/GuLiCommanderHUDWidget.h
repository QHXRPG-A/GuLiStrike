// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Events.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/UI/GuLiCommanderUIPresentation.h"
#include "GuLiCommanderHUDWidget.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiBattlePlayerState;
class AGuLiSoldierStateReplicator;
class UButton;
class UGuLiCommanderMiniMapWidget;
class UGuLiCommanderNetSyncComponent;
class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;
class UCanvasPanel;
class UVerticalBox;
class UHorizontalBox;
class UGuLiCommanderActionButton;
class UGuLiCommanderUITheme;
class UProgressBar;
class UBorder;
enum class EGuLiCommanderToolMode : uint8;
enum class EGuLiCommanderSelectionShape : uint8;
namespace GuLiOrderNetworkProbe { struct FRun; }

/** Authored button/icon names keep shortcut layout in UMG and behavior in C++. */
USTRUCT(BlueprintType)
struct FGuLiCommanderShortcutEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shortcut")
	FName ButtonName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shortcut")
	FName IconName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shortcut")
	FText Tooltip;
};

namespace GuLiCommanderHUDLayout
{
	/** All coordinates are HUD-local, including under DPI and offscreen captures. */
	GULISTRIKE_API FBox2D PlaceTooltip(
		const FBox2D& Anchor, const FVector2D& DesiredSize, const FVector2D& ViewportSize);
}

/** Local presentation group. Member indices refer to the current immutable presentation snapshot. */
struct FGuLiCommanderPortraitGroupView
{
	uint16 Type = 0;
	TArray<int32> MemberIndices;
};

/**
 * Native, event-driven runtime adapter for the authored commander HUD.
 *
 * Native console components own reproducible layout; the Blueprint child selects
 * the project theme. Presentation facts and local inspection never mutate gameplay.
 * There is no Blueprint binding, MVVM model, or per-portrait tick.
 */
UCLASS(BlueprintType, Blueprintable)
class GULISTRIKE_API UGuLiCommanderHUDWidget : public UUserWidget
{
	GENERATED_BODY()
	friend struct GuLiOrderNetworkProbe::FRun;

public:
	UGuLiCommanderHUDWidget(const FObjectInitializer& ObjectInitializer);

	/** Called by AGuLiCommanderHUD before the widget is added to the viewport. */
	void InitializeForController(AGuLiCommanderPlayerController* InController);

	/** Resolves current runtime sources and paints one coherent initial state. */
	void RefreshInitialState();

	/** Event entry points intentionally mirror the native runtime fact streams. */
	// 本地选择复制通知的消费者；参数是服务器确认的本端副本，函数不是 RPC。
	void RefreshSelection(const FGuLiCommanderSelectionState& Selection);
	// 本地业务 ACK 消费者；刷新命令反馈，不将回执当作移动完成事件。
	void RefreshCommandAck(const FGuLiCommandAck& Ack);
	void RefreshSoldierStates(uint32 SnapshotRevision);

	UFUNCTION()
	void RefreshPlayerState();

	/** Tests the real cached Slate geometry of all four HUD islands in pixels. */
	bool IsScreenPositionBlocked(const FVector2D& ScreenPixelPosition) const;
	bool HasValidBlockingGeometry() const;

	UGuLiCommanderMiniMapWidget* GetMiniMapWidget() const { return MiniMapWidget; }
	void CycleInspectionType();
	UFUNCTION(BlueprintCallable, Category="Commander|UI")
	bool RequestPortraitSelection(EGuLiPanelSelectionAction Action, int32 PageSlot);
	void ToggleLocalMenu();
	bool DismissTopLayer();
	bool IsLocalMenuOpen() const { return MenuLayer && bMenuOpen; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Add an authored button/icon pair here to extend the passive shortcut strip. */
	UPROPERTY(EditDefaultsOnly, Category = "Commander|UI")
	TArray<FGuLiCommanderShortcutEntry> ShortcutEntries;

private:
	void BuildConsoleLayout();
	void RefreshConsoleSelection();
	void RefreshConsoleContext();
	void RefreshConsoleTasks();
	void HandleUIAction(FName Action, int32 Argument);
	void ShowActionTooltip(UGuLiCommanderActionButton* Button);
	void RefreshActionTooltipAtCursor();
	void SetMenuOpen(bool bOpen);
	UGuLiCommanderActionButton* MakeActionButton(FName Name, const FString& Label, FName Action, int32 Argument, FName Icon = NAME_None);
	UTextBlock* MakeConsoleText(FName Name, const FString& Value, int32 Size = 16);
	UCanvasPanel* MakeConsolePanel(UCanvasPanel* Parent, FName Name, FVector2D Position, FVector2D Size);
	void PlaceConsoleWidget(UCanvasPanel* Parent, UWidget* Child, FVector2D Position, FVector2D Size, int32 Z = 0);
	FGuLiCommanderUIPresentation Presentation;
	TArray<int32> VisibleMemberIndices;
	TArray<FGuLiCommanderPortraitGroupView> VisiblePortraitGroups;
	TArray<uint16> InspectionTypes;
	int32 InspectionType = 0, PortraitPage = 0;
	bool bMenuOpen = false, bQuitConfirmation = false, bHelpOpen = false, bTaskDrawerOpen = false;
	FModifierKeysState PendingActionModifiers;
	FString LastTaskDescription;
	FGuid LastSkillReceipt;
	FString SkillFeedback;
	double SkillFeedbackUntil = 0;
	TWeakObjectPtr<UGuLiCommanderActionButton> HoveredAction;
	TArray<FVector> TaskLocations;
	UPROPERTY(EditDefaultsOnly, Category="Commander|UI") TObjectPtr<UGuLiCommanderUITheme> ConsoleTheme;
	UPROPERTY(Transient) TArray<TObjectPtr<UGuLiCommanderActionButton>> PortraitButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> PortraitHealth;
	UPROPERTY(Transient) TArray<TObjectPtr<UGuLiCommanderActionButton>> TypeButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UGuLiCommanderActionButton>> GroupButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GroupCounts;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UGuLiCommanderActionButton>> AttributeRows;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> InteractionIslands;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> TypeStrip;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> TaskRows;
	UPROPERTY(Transient) TObjectPtr<UBorder> MenuLayer;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ContextPanel;
	UPROPERTY(Transient) TObjectPtr<UImage> PortraitImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectionCaption;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectionDetails;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PageCaption;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TaskCaption;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ContextCaption;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuCaption;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> SelectionHealth;
	UPROPERTY(Transient) TObjectPtr<UBorder> ActionTooltip;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ActionTooltipText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ActionTooltipTitle;
	void ResolveRuntimeSources();
	// 订阅本端 NetSync/PlayerState/名册委托；控件销毁时必须对应解绑。
	void BindRuntimeSources();
	void UnbindRuntimeSources();
	void RefreshElapsedTime();
	void RefreshCoreAndRoster();
	void RefreshCommandControls();
	void RefreshUnitTypeCard();
	uint32 GetCurrentMatchEpoch() const;
	void BuildMiniMapLayer();
	void BuildTaskPanel();
	void RefreshTaskPanel();
	UFUNCTION() void HandleFocusClicked();
	UFUNCTION() void HandleStopClicked();
	void HideReviewOnlyMapWidgets();
	void BindShortcutControls();
	void UnbindShortcutControls();
	void PositionShortcutTooltip();
	void HideShortcutTooltip();

	UFUNCTION()
	void HandlePlayerStateChanged();

	UFUNCTION()
	void HandleResourcePrivateStateChanged();

	void HandleToolModeChanged(EGuLiCommanderToolMode NewMode);
	void HandleSelectionShapeChanged(EGuLiCommanderSelectionShape NewShape);
	void HandleSelectionRadiusChanged(EGuLiSelectionRadiusPreset NewPreset);

	UFUNCTION()
	void HandleShortcutHovered();

	UFUNCTION()
	void HandleShortcutUnhovered();

	UFUNCTION()
	void HandleMoveClicked();

	UFUNCTION()
	void HandleSelectClicked();

	UFUNCTION()
	void HandleMiniMapClicked();

	UTextBlock* FindText(FName WidgetName) const;
	UImage* FindImage(FName WidgetName) const;
	UWidget* FindRuntimeWidget(FName WidgetName) const;
	void SetText(FName WidgetName, const FText& Value) const;
	void SetImageFraction(FName WidgetName, float Fraction) const;
	void SetCommandSlotOpacity(int32 SlotIndex, float Opacity) const;
	bool IsWidgetGeometryHit(const UWidget* Widget, const FVector2D& ScreenPixelPosition) const;
	bool IsWidgetGeometryReady(const UWidget* Widget) const;

	TWeakObjectPtr<AGuLiCommanderPlayerController> CommanderController;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> NetSyncComponent;
	TWeakObjectPtr<AGuLiBattlePlayerState> CommanderPlayerState;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> SoldierStateReplicator;
	TMap<FName, TWeakObjectPtr<UWidget>> WidgetCache;

	UPROPERTY(Transient)
	TObjectPtr<UGuLiCommanderMiniMapWidget> MiniMapWidget;
	UPROPERTY(Transient) TObjectPtr<UWidget> TaskPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TaskText;
	UPROPERTY(Transient) TObjectPtr<UButton> FocusButton;
	UPROPERTY(Transient) TObjectPtr<UButton> StopButton;
	FTimerHandle TaskRefreshTimer;

	FGuLiCommanderSelectionState CachedSelection;
	FGuLiCommandAck CachedCommandAck;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle CommandAckChangedHandle;
	FDelegateHandle SoldierStatesChangedHandle;
	FDelegateHandle ToolModeChangedHandle;
	FDelegateHandle SelectionShapeChangedHandle;
	FDelegateHandle SelectionRadiusChangedHandle;
	FName ActiveShortcutButton;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BoxSelectionTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RadiusSelectionTexture;
	FTimerHandle SourceResolveTimer;
	FTimerHandle ElapsedTimeTimer;
	int32 InitialBlueRoster = 0;
	int32 InitialRedRoster = 0;
	uint32 CachedRosterMatchEpoch = 0u;
	uint32 CachedSelectionMatchEpoch = 0u;
	bool bRuntimeSourcesBound = false;
	bool bNativeConstructed = false;
};
