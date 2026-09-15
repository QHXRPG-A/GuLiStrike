// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/HUD.h"
#include "GuLiCommanderHUD.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiCommanderCameraPawn;
class AGuLiCommanderHealthBarRenderer;
class AGuLiCommanderPresentationActor;
class AGuLiSoldierStateReplicator;
class UGuLiCommanderHUDWidget;
class UGuLiBuildingPlacementComponent;
enum class EGuLiBuildingFeedbackTone : uint8;

namespace GuLiCommanderHUD
{
	/** Pure visibility policy for one authoritative route; used by HUD and regression tests. */
	GULISTRIKE_API bool ShouldDrawMoveEndpoint(
		const FGuLiMoveEndpointItem& Endpoint,
		bool bIsSelected,
		const FGuLiSoldierStateItem* SoldierState);

	/** Clips a projected line against the player canvas without animating or changing its direction. */
	GULISTRIKE_API bool ClipScreenLineToBounds(
		const FBox2D& Bounds,
		FVector2D& InOutStart,
		FVector2D& InOutEnd);
}

/** Canvas-only commander prototype HUD; it does not own replicated state. */
// 本地界面消费层；读取复制状态与业务回执，不承担网络身份分配或权威命令执行。
UCLASS()
class GULISTRIKE_API AGuLiCommanderHUD : public AHUD
{
	GENERATED_BODY()

public:
	void ShowCommandFeedback(const FText& Message, bool bAccepted);
	AGuLiCommanderHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

	// 本地角色生命周期入口，由 PC 角色变化及 HUD BeginPlay 调用；不依赖 Canvas/bShowHUD。
	void RefreshCommanderRole();

	/** Prevents world selection/move commands from leaking through the tactical HUD. */
	bool IsScreenPositionOverCommanderUI(const FVector2D& ScreenPosition) const;

private:
	void CreateRuntimeHUD();
	void DestroyRuntimeHUD();
	AGuLiSoldierStateReplicator* FindSoldierStateReplicator() const;
	AGuLiCommanderPresentationActor* FindPresentationActor() const;
	void GatherSelectedSoldierValues(
		const FGuLiCommanderSelectionState& Selection,
		TSet<uint32>& OutSelectedSoldierValues) const;
	void DrawCohortCards(
		const FGuLiCommanderSelectionState& Selection,
		const FGuLiCommandAck& LastAck,
		EGuLiTeam LocalTeam);
	void DrawMiniMap(
		const AGuLiSoldierStateReplicator* SoldierStates,
		AGuLiCommanderPresentationActor* PresentationActor,
		const TSet<uint32>& SelectedSoldierValues);
	bool EnsureMiniMapTerrainCache();
	bool TryMiniMapScreenToWorld(const FVector2D& ScreenPosition, FVector& OutWorldLocation);
	bool TrySampleMiniMapTerrainHeight(const FVector2D& WorldPosition, float& OutHeight) const;
	float GetMiniMapCameraYawDegrees() const;
	void DrawMiniMapTerrain(float MapX, float MapY, float MapSize, float CameraYawDegrees);
	void DrawMiniMapCameraFrame(float MapX, float MapY, float MapSize, float CameraYawDegrees);
	void DrawSelectionPresetButtons(EGuLiSelectionRadiusPreset ActivePreset);
	void DrawSelectionCircle(const AGuLiCommanderPlayerController& Controller);
	void DrawSelectionRectangle(const AGuLiCommanderPlayerController& Controller);
	void DrawActiveCommandLine(const AGuLiCommanderPlayerController& Controller);
	void BindBuildingFeedback();
	void UnbindBuildingFeedback();
	void HandleBuildingFeedback(const FText& Message, EGuLiBuildingFeedbackTone Tone);
	void DrawBuildingFeedback();

	mutable TWeakObjectPtr<AGuLiSoldierStateReplicator> CachedSoldierStateReplicator;
	mutable TWeakObjectPtr<AGuLiCommanderPresentationActor> CachedPresentationActor;
	FBox2D MiniMapWorldBounds = FBox2D(ForceInit);
	TArray<float> MiniMapTerrainHeights;
	TArray<uint8> MiniMapTerrainValidity;
	float MiniMapTerrainMinimumHeight = 0.0f;
	float MiniMapTerrainMaximumHeight = 1.0f;
	uint32 MiniMapLandscapeRevision = 0u;
	bool bMiniMapTerrainCacheInitialized = false;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|UI")
	TSubclassOf<UGuLiCommanderHUDWidget> CommanderHUDWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UGuLiCommanderHUDWidget> RuntimeHUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiCommanderHealthBarRenderer> HealthBarRenderer;

	TWeakObjectPtr<UGuLiBuildingPlacementComponent> BuildingPlacementComponent;
	FDelegateHandle BuildingFeedbackHandle;
	FText BuildingFeedbackMessage;
	double BuildingFeedbackExpireTime = 0.0;
	EGuLiBuildingFeedbackTone BuildingFeedbackTone;
};
