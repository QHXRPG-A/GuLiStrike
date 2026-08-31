// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiCommanderHUDWidget.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiCommanderPlayerState;
class AGuLiSoldierStateReplicator;
class UButton;
class UGuLiCommanderMiniMapWidget;
class UGuLiCommanderNetSyncComponent;
class UImage;
class UTextBlock;
class UWidget;
enum class EGuLiCommanderToolMode : uint8;

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

	/** Tests the real cached Slate geometry of the three HUD islands in pixels. */
	bool IsScreenPositionBlocked(const FVector2D& ScreenPixelPosition) const;
	bool HasValidBlockingGeometry() const;

	UGuLiCommanderMiniMapWidget* GetMiniMapWidget() const { return MiniMapWidget; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

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

	UFUNCTION()
	void HandlePlayerStateChanged();

	void HandleToolModeChanged(EGuLiCommanderToolMode NewMode);

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
	TWeakObjectPtr<AGuLiCommanderPlayerState> CommanderPlayerState;
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
	FTimerHandle SourceResolveTimer;
	FTimerHandle ElapsedTimeTimer;
	int32 InitialBlueRoster = 0;
	int32 InitialRedRoster = 0;
	uint32 CachedRosterMatchEpoch = 0u;
	uint32 CachedSelectionMatchEpoch = 0u;
	bool bRuntimeSourcesBound = false;
	bool bNativeConstructed = false;
};
