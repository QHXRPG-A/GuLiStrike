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
 * local confirmed selection, then paints the whole map through one Slate layer.
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

protected:
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

	TArray<FGuLiCommanderMiniMapSoldierPoint> SoldierPoints;
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
