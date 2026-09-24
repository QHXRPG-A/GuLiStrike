// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace GuLiCommanderMiniMap
{
	/**
	 * Pure heading-up tactical-map transform.
	 *
	 * Camera forward maps to screen up and camera right maps to screen right.
	 * The fixed panel is never rotated; only positions expressed through this
	 * transform move as the camera heading changes.
	 */
	struct GULISTRIKE_API FHeadingUpTransform
	{
		FBox2D WorldBounds = FBox2D(ForceInit);
		FBox2D ScreenBounds = FBox2D(ForceInit);
		float CameraYawDegrees = 0.0f;

		bool IsValid() const;

		/** Projects without clipping so callers can clip line segments at the panel edge. */
		FVector2D WorldToScreenUnchecked(const FVector2D& WorldPosition) const;

		/** Inverse of WorldToScreenUnchecked. */
		FVector2D ScreenToWorldUnchecked(const FVector2D& ScreenPosition) const;

		/** Requires both the source world point and projected point to be visible. */
		bool TryWorldToScreen(const FVector2D& WorldPosition, FVector2D& OutScreenPosition) const;

		/** Rejects fixed-panel corners that are outside the rotated battlefield. */
		bool TryScreenToWorld(const FVector2D& ScreenPosition, FVector2D& OutWorldPosition) const;
	};

	/** Clips a projected content segment to the fixed tactical-map panel. */
	GULISTRIKE_API bool ClipLineToScreenBounds(
		const FBox2D& ScreenBounds,
		FVector2D& InOutStart,
		FVector2D& InOutEnd);

	/** Clips an axis-aligned content rectangle to the fixed tactical-map panel. */
	GULISTRIKE_API bool ClipRectToScreenBounds(
		const FBox2D& ScreenBounds,
		FVector2D& InOutMinimum,
		FVector2D& InOutSize);
}
