// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GuLiShipReticleTypes.generated.h"

class APlayerController;

/** Presentation mode selected exclusively from a Gameplay Ability's AssetTags. */
UENUM(BlueprintType)
enum class EGuLiShipReticleMode : uint8
{
	None = 0,
	Omni,
	Bounded
};

/** Screen-normalized presentation settings used by bounded reticle abilities. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipReticleConfig
{
	GENERATED_BODY()

	/** Center of the bounded aiming region in owning-player viewport coordinates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Reticle",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D BoundsCenterNormalized = FVector2D(0.5, 0.5);

	/** Width and height of the bounded aiming region in owning-player viewport coordinates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Reticle",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D BoundsSizeNormalized = FVector2D(0.4, 0.3);

	bool IsValid() const;
	FBox2D GetNormalizedBounds() const;
};

/** One active, local-only ability's reticle claim. Array order is activation priority. */
struct GULISTRIKE_API FGuLiShipReticleActivation
{
	TWeakObjectPtr<UObject> Owner;
	EGuLiShipReticleMode Mode = EGuLiShipReticleMode::None;
	FGuLiShipReticleConfig Config;
};

/** Latest-active-wins policy shared by runtime and focused automation. */
class GULISTRIKE_API FGuLiShipReticleActivationStack
{
public:
	bool Activate(UObject* Owner, EGuLiShipReticleMode Mode, const FGuLiShipReticleConfig& Config);
	bool End(UObject* Owner);
	void Reset();
	const FGuLiShipReticleActivation* GetActive();
	int32 Num() const { return Entries.Num(); }

private:
	void PruneInvalid();
	TArray<FGuLiShipReticleActivation> Entries;
};

namespace GuLiShipReticle
{
	/**
	 * Returns the portion of the owning game viewport that is actually visible.
	 * Fixed-resolution embedded PIE can render a larger scene viewport than the
	 * Slate viewport displays, so controller dimensions alone are insufficient
	 * for screen-safe HUD placement.
	 */
	GULISTRIKE_API FVector2D ResolveVisibleViewportSize(
		const APlayerController& Controller);

	/** Resolves the two mutually-exclusive native reticle AssetTags. Conflict fails closed to None. */
	GULISTRIKE_API EGuLiShipReticleMode ResolveMode(
		const FGameplayTagContainer& AssetTags,
		bool* bOutConflict = nullptr);

	/** Builds the usable pixel-center region after applying viewport/frame and reticle padding. */
	GULISTRIKE_API FBox2D BuildUsablePixelBounds(
		EGuLiShipReticleMode Mode,
		const FGuLiShipReticleConfig& Config,
		const FVector2D& ViewportSize,
		float OuterPaddingPixels);

	GULISTRIKE_API FVector2D ClampPixelPosition(
		const FVector2D& PixelPosition,
		const FBox2D& UsableBounds);

	/** Converts a horizontal field of view to a camera-plane world-units-per-screen-pixel scale. */
	GULISTRIKE_API float CalculateCentimetersPerPixel(
		float PlaneDistanceCentimeters,
		float HorizontalFieldOfViewDegrees,
		int32 ViewportWidth,
		int32 ViewportHeight);

	/** Builds an upright WidgetComponent transform parallel to the camera screen. */
	GULISTRIKE_API FRotator CalculateScreenFacingWidgetRotation(
		const FRotator& CameraRotation);

	/** Matches camera forward to Ship forward while keeping a stable, non-banked screen up axis. */
	GULISTRIKE_API FRotator CalculateAlignedCameraRotation(
		const FVector& ShipForward,
		const FVector& ShipUp);
}
