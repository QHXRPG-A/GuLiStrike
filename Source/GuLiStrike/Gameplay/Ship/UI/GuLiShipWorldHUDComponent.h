// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Ship/Aiming/GuLiShipReticleTypes.h"
#include "GuLiShipWorldHUDComponent.generated.h"

class AGuLiStrikeShip;
class APlayerController;
class UGuLiCombatHealthComponent;
class UGuLiShipAimComponent;
class UGuLiShipMovementComponent;
class UGuLiShipAbilitySystemComponent;
class UGuLiShipWorldAimBoundsWidget;
class UGuLiShipWorldCombatWidget;
class UGuLiShipWorldFlightWidget;
class UGuLiShipWorldReticleWidget;
class UGuLiShipWorldStatusWidget;
class UMaterialInterface;
class UStaticMeshComponent;
class UUserWidget;
class UWidgetComponent;

/**
 * Local-only presenter for five world-space Ship HUD nodes.
 * Gameplay state remains owned by the Ship, movement, health and ASC components.
 */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiShipWorldHUDComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiShipWorldHUDComponent();

	/** Re-evaluates local ownership and creates/destroys transient WidgetComponents. */
	void HandleOwnerControllerChanged();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	UWidgetComponent* CreateWidgetNode(
		FName ComponentName,
		TSubclassOf<UUserWidget> WidgetClass,
		const FVector2D& DrawSize,
		int32 TranslucencySortPriority);
	void CreateRuntimeWidgets();
	void DestroyRuntimeWidgets();
	void BindAimComponent(UGuLiShipAimComponent* AimComponent);
	void HandleAimModeChanged(EGuLiShipReticleMode NewMode);
	void RefreshDataIfDue(bool bForce);
	void RefreshData();
	bool UpdateNodeTransforms();
	bool ProjectHullBounds(
		APlayerController& Controller,
		const FVector& CameraLocation,
		const FVector& CameraForward,
		FVector2D& OutMinimum,
		FVector2D& OutMaximum,
		FVector& OutPlaneOrigin) const;
	bool SetNodeTransform(
		UWidgetComponent& Node,
		APlayerController& Controller,
		const FVector2D& ScreenPosition,
		const FVector& PlaneOrigin,
		const FVector& CameraForward,
		const FRotator& CameraRotation,
		float CentimetersPerPixel) const;
	void SetNodeVisible(UWidgetComponent* Node, bool bVisible) const;
	void HideAllNodes() const;
	bool ShouldPresent() const;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Widgets")
	TSoftClassPtr<UGuLiShipWorldStatusWidget> StatusWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Widgets")
	TSoftClassPtr<UGuLiShipWorldFlightWidget> FlightWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Widgets")
	TSoftClassPtr<UGuLiShipWorldCombatWidget> CombatWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Widgets")
	TSoftClassPtr<UGuLiShipWorldReticleWidget> ReticleWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Widgets")
	TSoftClassPtr<UGuLiShipWorldAimBoundsWidget> AimBoundsWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Widgets")
	TSoftObjectPtr<UMaterialInterface> NoDepthWidgetMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Layout", meta = (ClampMin = "0.0"))
	float HullPanelGapPixels = 28.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Layout", meta = (ClampMin = "0.0"))
	float ViewportSafeMarginPixels = 24.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|World HUD|Performance", meta = (ClampMin = "0.02"))
	float DataRefreshIntervalSeconds = 0.1f;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> StatusNode;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> FlightNode;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> CombatNode;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> ReticleNode;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> AimBoundsNode;

	TWeakObjectPtr<AGuLiStrikeShip> OwnerShip;
	TWeakObjectPtr<APlayerController> LocalController;
	TWeakObjectPtr<UStaticMeshComponent> HullMesh;
	TWeakObjectPtr<UGuLiShipAimComponent> BoundAimComponent;
	FDelegateHandle AimModeChangedHandle;
	double NextDataRefreshTimeSeconds = 0.0;
	int32 CachedHealth = MIN_int32;
	int32 CachedMaximumHealth = MIN_int32;
	int32 CachedCurrentSpeed = MIN_int32;
	int32 CachedMaximumSpeed = MIN_int32;
	bool bCachedReady = false;
	bool bCachedAlive = false;
	bool bCachedBoost = false;
	bool bCachedBasicAvailable = false;
	bool bCachedMissileAvailable = false;
	bool bCachedMissileCoolingDown = false;
	bool bHasStatusSnapshot = false;
	bool bHasFlightSnapshot = false;
	bool bHasCombatSnapshot = false;
	bool bLoggedMissingAssets = false;
};
