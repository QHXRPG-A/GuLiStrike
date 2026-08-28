// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/HUD.h"
#include "GuLiCommanderHUD.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiCommanderCameraPawn;
class AGuLiCommanderPresentationActor;
class AGuLiSoldierStateReplicator;

/** Canvas-only commander prototype HUD; it does not own replicated state. */
UCLASS()
class GULISTRIKE_API AGuLiCommanderHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

	/** Prevents world selection/move commands from leaking through the tactical HUD. */
	bool IsScreenPositionOverCommanderUI(const FVector2D& ScreenPosition) const;

private:
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
	void DrawActiveCommandLine(const AGuLiCommanderPlayerController& Controller);

	mutable TWeakObjectPtr<AGuLiSoldierStateReplicator> CachedSoldierStateReplicator;
	mutable TWeakObjectPtr<AGuLiCommanderPresentationActor> CachedPresentationActor;
	FBox2D MiniMapWorldBounds = FBox2D(ForceInit);
	TArray<float> MiniMapTerrainHeights;
	TArray<uint8> MiniMapTerrainValidity;
	float MiniMapTerrainMinimumHeight = 0.0f;
	float MiniMapTerrainMaximumHeight = 1.0f;
	bool bMiniMapTerrainCacheInitialized = false;
};
