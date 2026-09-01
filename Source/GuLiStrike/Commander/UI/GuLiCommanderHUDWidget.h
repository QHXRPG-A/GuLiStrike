// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Commander/Network/GuLiCommanderTypes.h"
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
enum class EGuLiCommanderToolMode : uint8;
enum class EGuLiCommanderSelectionShape : uint8;

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

/**
 * Native, event-driven runtime adapter for the authored commander HUD.
 *
 * The Blueprint child owns layout and brushes only. Runtime facts, input and
 * refresh scheduling stay here so the WBP needs no graph, binding, MVVM model
 * or per-frame tick.
 */
UCLASS(BlueprintType, Blueprintable)
class GULISTRIKE_API UGuLiCommanderHUDWidget : public UUserWidget
{
	GENERATED_BODY()

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

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Add an authored button/icon pair here to extend the passive shortcut strip. */
	UPROPERTY(EditDefaultsOnly, Category = "Commander|UI")
	TArray<FGuLiCommanderShortcutEntry> ShortcutEntries;

private:
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
	void HideReviewOnlyMapWidgets();
	void BindShortcutControls();
	void UnbindShortcutControls();
	void PositionShortcutTooltip();
	void HideShortcutTooltip();

	UFUNCTION()
	void HandlePlayerStateChanged();

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
