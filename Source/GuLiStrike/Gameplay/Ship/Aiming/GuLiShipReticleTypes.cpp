// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Aiming/GuLiShipReticleTypes.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Slate/SceneViewport.h"

FVector2D GuLiShipReticle::ResolveVisibleViewportSize(
	const APlayerController& Controller)
{
	int32 ProjectionWidth = 0;
	int32 ProjectionHeight = 0;
	Controller.GetViewportSize(ProjectionWidth, ProjectionHeight);
	FVector2D Result(ProjectionWidth, ProjectionHeight);
	if (ProjectionWidth <= 0 || ProjectionHeight <= 0)
	{
		return FVector2D::ZeroVector;
	}

	const UWorld* World = Controller.GetWorld();
	const UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
	const FSceneViewport* SceneViewport = ViewportClient ? ViewportClient->GetGameViewport() : nullptr;
	if (!SceneViewport)
	{
		return Result;
	}

	const FVector2D VisibleDrawSize = SceneViewport->GetCachedGeometry().GetDrawSize();
	if (FMath::IsFinite(VisibleDrawSize.X) && VisibleDrawSize.X > 1.0)
	{
		Result.X = FMath::Min(Result.X, VisibleDrawSize.X);
	}
	if (FMath::IsFinite(VisibleDrawSize.Y) && VisibleDrawSize.Y > 1.0)
	{
		Result.Y = FMath::Min(Result.Y, VisibleDrawSize.Y);
	}
	return Result;
}

bool FGuLiShipReticleConfig::IsValid() const
{
	if (!FMath::IsFinite(BoundsCenterNormalized.X)
		|| !FMath::IsFinite(BoundsCenterNormalized.Y)
		|| !FMath::IsFinite(BoundsSizeNormalized.X)
		|| !FMath::IsFinite(BoundsSizeNormalized.Y)
		|| BoundsSizeNormalized.X <= UE_SMALL_NUMBER
		|| BoundsSizeNormalized.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FBox2D Bounds = GetNormalizedBounds();
	return Bounds.Min.X >= 0.0 && Bounds.Min.Y >= 0.0
		&& Bounds.Max.X <= 1.0 && Bounds.Max.Y <= 1.0;
}

FBox2D FGuLiShipReticleConfig::GetNormalizedBounds() const
{
	const FVector2D HalfSize = BoundsSizeNormalized * 0.5;
	return FBox2D(BoundsCenterNormalized - HalfSize, BoundsCenterNormalized + HalfSize);
}

bool FGuLiShipReticleActivationStack::Activate(
	UObject* Owner,
	const EGuLiShipReticleMode Mode,
	const FGuLiShipReticleConfig& Config)
{
	if (!IsValid(Owner) || Mode == EGuLiShipReticleMode::None
		|| (Mode == EGuLiShipReticleMode::Bounded && !Config.IsValid()))
	{
		return false;
	}

	PruneInvalid();
	Entries.RemoveAll([Owner](const FGuLiShipReticleActivation& Entry)
	{
		return Entry.Owner.Get() == Owner;
	});
	FGuLiShipReticleActivation& Entry = Entries.AddDefaulted_GetRef();
	Entry.Owner = Owner;
	Entry.Mode = Mode;
	Entry.Config = Config;
	return true;
}

bool FGuLiShipReticleActivationStack::End(UObject* Owner)
{
	PruneInvalid();
	return Entries.RemoveAll([Owner](const FGuLiShipReticleActivation& Entry)
	{
		return Entry.Owner.Get() == Owner;
	}) > 0;
}

void FGuLiShipReticleActivationStack::Reset()
{
	Entries.Reset();
}

const FGuLiShipReticleActivation* FGuLiShipReticleActivationStack::GetActive()
{
	PruneInvalid();
	return Entries.IsEmpty() ? nullptr : &Entries.Last();
}

void FGuLiShipReticleActivationStack::PruneInvalid()
{
	Entries.RemoveAll([](const FGuLiShipReticleActivation& Entry)
	{
		return !Entry.Owner.IsValid();
	});
}

EGuLiShipReticleMode GuLiShipReticle::ResolveMode(
	const FGameplayTagContainer& AssetTags,
	bool* bOutConflict)
{
	const bool bOmni = AssetTags.HasTagExact(TAG_GuLi_ShipAbility_Reticle_Omni);
	const bool bBounded = AssetTags.HasTagExact(TAG_GuLi_ShipAbility_Reticle_Bounded);
	const bool bConflict = bOmni && bBounded;
	if (bOutConflict)
	{
		*bOutConflict = bConflict;
	}
	if (bConflict)
	{
		return EGuLiShipReticleMode::None;
	}
	return bOmni ? EGuLiShipReticleMode::Omni
		: bBounded ? EGuLiShipReticleMode::Bounded
		: EGuLiShipReticleMode::None;
}

FBox2D GuLiShipReticle::BuildUsablePixelBounds(
	const EGuLiShipReticleMode Mode,
	const FGuLiShipReticleConfig& Config,
	const FVector2D& ViewportSize,
	const float OuterPaddingPixels)
{
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0 || !FMath::IsFinite(OuterPaddingPixels))
	{
		return FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
	}

	FVector2D Min(0.0, 0.0);
	FVector2D Max = ViewportSize;
	if (Mode == EGuLiShipReticleMode::Bounded && Config.IsValid())
	{
		const FBox2D NormalizedBounds = Config.GetNormalizedBounds();
		Min = NormalizedBounds.Min * ViewportSize;
		Max = NormalizedBounds.Max * ViewportSize;
	}

	const float Padding = FMath::Max(0.0f, OuterPaddingPixels);
	Min += FVector2D(Padding, Padding);
	Max -= FVector2D(Padding, Padding);
	if (Min.X > Max.X)
	{
		Min.X = Max.X = (Min.X + Max.X) * 0.5;
	}
	if (Min.Y > Max.Y)
	{
		Min.Y = Max.Y = (Min.Y + Max.Y) * 0.5;
	}
	return FBox2D(Min, Max);
}

FVector2D GuLiShipReticle::ClampPixelPosition(
	const FVector2D& PixelPosition,
	const FBox2D& UsableBounds)
{
	return FVector2D(
		FMath::Clamp(PixelPosition.X, UsableBounds.Min.X, UsableBounds.Max.X),
		FMath::Clamp(PixelPosition.Y, UsableBounds.Min.Y, UsableBounds.Max.Y));
}

float GuLiShipReticle::CalculateCentimetersPerPixel(
	const float PlaneDistanceCentimeters,
	const float HorizontalFieldOfViewDegrees,
	const int32 ViewportWidth,
	const int32 ViewportHeight)
{
	if (!FMath::IsFinite(PlaneDistanceCentimeters)
		|| !FMath::IsFinite(HorizontalFieldOfViewDegrees)
		|| PlaneDistanceCentimeters <= UE_SMALL_NUMBER
		|| HorizontalFieldOfViewDegrees <= UE_SMALL_NUMBER
		|| HorizontalFieldOfViewDegrees >= 179.0f
		|| ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return 0.0f;
	}

	const double AspectRatio = static_cast<double>(ViewportWidth) / static_cast<double>(ViewportHeight);
	const double HorizontalHalfFovRadians = FMath::DegreesToRadians(HorizontalFieldOfViewDegrees * 0.5);
	const double VerticalHalfFovRadians = FMath::Atan(FMath::Tan(HorizontalHalfFovRadians) / AspectRatio);
	return static_cast<float>(
		2.0 * static_cast<double>(PlaneDistanceCentimeters) * FMath::Tan(VerticalHalfFovRadians)
		/ static_cast<double>(ViewportHeight));
}

FRotator GuLiShipReticle::CalculateScreenFacingWidgetRotation(
	const FRotator& CameraRotation)
{
	const FQuat CameraOrientation = CameraRotation.Quaternion();
	return FRotationMatrix::MakeFromXZ(
		-CameraOrientation.GetForwardVector(),
		CameraOrientation.GetUpVector()).Rotator();
}

FRotator GuLiShipReticle::CalculateAlignedCameraRotation(
	const FVector& ShipForward,
	const FVector& ShipUp)
{
	const FVector Forward = ShipForward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return FRotator::ZeroRotator;
	}

	FVector UpReference = FVector::UpVector;
	if (FMath::Abs(FVector::DotProduct(Forward, UpReference)) > 0.98)
	{
		UpReference = ShipUp.GetSafeNormal();
	}
	if (UpReference.IsNearlyZero() || FMath::Abs(FVector::DotProduct(Forward, UpReference)) > 0.999)
	{
		UpReference = FVector::RightVector;
	}
	return FRotationMatrix::MakeFromXZ(Forward, UpReference).Rotator();
}
