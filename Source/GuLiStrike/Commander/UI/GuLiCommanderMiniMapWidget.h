// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiCommanderMiniMapWidget.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiCommanderPresentationActor;
class AGuLiSoldierStateReplicator;
class USlateBrushAsset;
class UGuLiCommanderNetSyncComponent;
class SGuLiCommanderMiniMapLayer;
struct FGuLiSoldierRosterDelta;

/** One immutable point consumed by the native Slate paint pass. */
struct FGuLiCommanderMiniMapSoldierPoint
{
	FVector2D WorldPosition = FVector2D::ZeroVector;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	bool bSelected = false;
};

/**
 * Native, event/timer-driven tactical map used by WBP_CommanderHUD.
 *
 * The widget owns no gameplay state. Every tenth of a second it snapshots the
 * reliable Soldier facts, final interpolated presentation transforms and the
 * local confirmed selection. Terrain and dynamic markers have separate Slate caches.
 */
UCLASS(BlueprintType, Blueprintable)
class GULISTRIKE_API UGuLiCommanderMiniMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Supplies the sole local input/data owner. Safe to call before NativeConstruct. */
	void InitializeForController(AGuLiCommanderPlayerController* InController);

	/** Rebuilds the current native snapshot immediately without enabling Tick. */
	void RequestImmediateRefresh();

	/** Consumes a viewport-pixel click routed by the transparent map button. */
	bool HandleMapClickAtScreenPosition(const FVector2D& ScreenPixelPosition);
	virtual void SetVisibility(ESlateVisibility InVisibility) override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	friend class SGuLiCommanderMiniMapLayer;
	friend class FGuLiCommanderMiniMapCacheTest;
	int32 PaintMapLayer(bool bTerrain, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const;
	void BindRuntimeEvents();
	void UnbindRuntimeEvents();
	void HandleRosterDelta(const FGuLiSoldierRosterDelta& Delta);
	void HandleVisualStatesChanged(const TArray<FGuLiSoldierId>& Ids);
	void HandleSelectionChanged(const FGuLiCommanderSelectionState& Selection);
	bool RefreshPoint(FGuLiSoldierId Id, bool bRefreshPosition);
	void InvalidateMapLayer(bool bTerrain);
	bool IsHierarchyVisible() const;
	void StartRefreshTimer();
	void StopRefreshTimer();
	void RefreshSnapshot();
	void ResolveRuntimeSources();
	void EnsureTerrainCache();
	bool TrySampleTerrainHeight(const FVector2D& WorldPosition, float& OutHeight) const;
	float ResolveCameraYawDegrees() const;
	void RefreshCameraFootprint();
	FBox2D GetLocalContentBounds(const FGeometry& Geometry) const;

	TWeakObjectPtr<AGuLiCommanderPlayerController> CommanderController;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> SoldierStateReplicator;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> PresentationActor;

	UPROPERTY(Transient)
	TObjectPtr<USlateBrushAsset> SolidBrushAsset;

	TMap<FGuLiSoldierId, FGuLiCommanderMiniMapSoldierPoint> SoldierPoints;
	TSet<FGuLiSoldierId> SelectedSoldiers;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> BoundRosterEvents;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> BoundSelectionEvents;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> BoundPresentationEvents;
	FDelegateHandle VisualStateHandle;
	FDelegateHandle RosterDeltaHandle;
	FDelegateHandle SelectionChangedHandle;
	TSharedPtr<SWidget> TerrainLayer;
	TSharedPtr<SWidget> DynamicLayer;
	uint64 TerrainInvalidations = 0;
	uint64 DynamicInvalidations = 0;
	bool bWasHierarchyVisible = false;
	TArray<FVector2D> CameraFootprintWorld;
	FVector2D CameraWorldPosition = FVector2D::ZeroVector;
	float CameraYawDegrees = 0.0f;
	bool bHasCameraWorldPosition = false;

	FBox2D TerrainWorldBounds = FBox2D(ForceInit);
	TArray<float> TerrainHeights;
	TArray<uint8> TerrainValidity;
	float TerrainMinimumHeight = 0.0f;
	float TerrainMaximumHeight = 1.0f;
	uint32 TerrainLandscapeRevision = 0u;
	bool bTerrainCacheInitialized = false;
	bool bNativeConstructed = false;

	FTimerHandle RefreshTimerHandle;
};
