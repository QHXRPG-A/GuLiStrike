// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Ship/Aiming/GuLiShipReticleTypes.h"
#include "GuLiShipAimComponent.generated.h"

class AGuLiStrikeShip;
class APlayerController;
class UGuLiShipHangarCapabilityComponent;
class USpringArmComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiShipAimModeChanged, EGuLiShipReticleMode);

/**
 * Local-only bridge between active capability actions, the Ship camera and a virtual screen reticle.
 * It never selects targets, mutates gameplay authority, sends RPCs, or owns visual widgets.
 */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiShipAimComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiShipAimComponent();

	/** Re-evaluates local ownership and capability bindings after possession changes. */
	void HandleOwnerControllerChanged();

	/** Clears presentation state when the owning Ship dies without changing gameplay authority. */
	void ResetForOwnerUnavailable();

	/** Returns true while aiming, meaning the caller must not also orbit the camera. */
	bool ConsumeLookInput(const FVector2D& InputDelta);

	UFUNCTION(BlueprintPure, Category = "Ship|Aiming")
	bool IsAiming() const { return ActiveMode != EGuLiShipReticleMode::None; }

	UFUNCTION(BlueprintPure, Category = "Ship|Aiming")
	EGuLiShipReticleMode GetReticleMode() const { return ActiveMode; }

	UFUNCTION(BlueprintPure, Category = "Ship|Aiming")
	FVector2D GetReticleScreenPositionNormalized() const { return ReticlePositionNormalized; }

	UFUNCTION(BlueprintPure, Category = "Ship|Aiming")
	FGuLiShipReticleConfig GetActiveReticleConfig() const { return ActiveConfig; }

	/** Returns the owning local player's deprojected ray through the virtual reticle. */
	UFUNCTION(BlueprintPure, Category = "Ship|Aiming")
	bool GetReticleAimRay(FVector& OutOrigin, FVector& OutDirection) const;

	FBox2D GetActiveBoundsNormalized() const;
	FGuLiShipAimModeChanged& OnAimModeChanged() { return AimModeChangedDelegate; }

#if !UE_BUILD_SHIPPING
	/** Non-shipping visual QA hook; production abilities remain unmodified. */
	void SetDebugReticleMode(EGuLiShipReticleMode Mode);
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void BindHangar(UGuLiShipHangarCapabilityComponent* Hangar);
	void UnbindHangar();
	void HandleAbilityPresentation(FGameplayTag AbilityId, bool bActive);
	void ApplyActiveClaim();
	void EnterAimMode();
	void ExitAimMode();
	void ResetAimState();
	void AlignCameraToShip() const;
	bool GetViewportSize(FVector2D& OutViewportSize) const;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Aiming", meta = (ClampMin = "0.01"))
	float ReticleInputSensitivity = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Aiming", meta = (ClampMin = "0.0"))
	float SafeViewportMarginPixels = 24.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Aiming", meta = (ClampMin = "0.0"))
	float ReticleHalfSizePixels = 48.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Aiming", meta = (ClampMin = "0.0"))
	float BoundedFrameInnerPaddingPixels = 12.0f;

	TWeakObjectPtr<AGuLiStrikeShip> OwnerShip;
	TWeakObjectPtr<APlayerController> LocalController;
	TWeakObjectPtr<UGuLiShipHangarCapabilityComponent> BoundHangar;
	TWeakObjectPtr<USpringArmComponent> ShipSpringArm;
	TWeakObjectPtr<UObject> CurrentClaimOwner;
	FDelegateHandle PresentationHandle;
	UPROPERTY(Transient) TMap<FGameplayTag, TObjectPtr<UObject>> PresentationOwners;
	FGuLiShipReticleActivationStack ActivationStack;
	FGuLiShipAimModeChanged AimModeChangedDelegate;
	FGuLiShipReticleConfig ActiveConfig;
	FVector2D ReticlePositionNormalized = FVector2D(0.5, 0.5);
	EGuLiShipReticleMode ActiveMode = EGuLiShipReticleMode::None;
	bool bSavedCameraRotationLagEnabled = false;
	bool bHasSavedCameraRotationLagState = false;
	bool bSavedShowMouseCursor = false;
	bool bHasSavedMouseCursorState = false;

#if !UE_BUILD_SHIPPING
	EGuLiShipReticleMode DebugMode = EGuLiShipReticleMode::None;
#endif
};
