// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderMiniMapTransform.h"

namespace GuLiCommanderMiniMap
{
	namespace Private
	{
		bool IsFiniteVector(const FVector2D& Value)
		{
			return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
		}

		void GetHeadingBasis(
			const float CameraYawDegrees,
			FVector2D& OutForward,
			FVector2D& OutRight)
		{
			const float YawRadians = FMath::DegreesToRadians(CameraYawDegrees);
			float SinYaw = 0.0f;
			float CosYaw = 1.0f;
			FMath::SinCos(&SinYaw, &CosYaw, YawRadians);
			OutForward = FVector2D(CosYaw, SinYaw);
			OutRight = FVector2D(-SinYaw, CosYaw);
		}

		bool ClipTest(
			const double Direction,
			const double DistanceToBoundary,
			double& InOutMinimumTime,
			double& InOutMaximumTime)
		{
			if (FMath::Abs(Direction) <= UE_DOUBLE_SMALL_NUMBER)
			{
				return DistanceToBoundary >= 0.0;
			}

			const double CandidateTime = DistanceToBoundary / Direction;
			if (Direction < 0.0)
			{
				if (CandidateTime > InOutMaximumTime)
				{
					return false;
				}
				InOutMinimumTime = FMath::Max(InOutMinimumTime, CandidateTime);
			}
			else
			{
				if (CandidateTime < InOutMinimumTime)
				{
					return false;
				}
				InOutMaximumTime = FMath::Min(InOutMaximumTime, CandidateTime);
			}
			return true;
		}
	}

	bool FHeadingUpTransform::IsValid() const
	{
		if (!WorldBounds.bIsValid || !ScreenBounds.bIsValid
			|| !FMath::IsFinite(CameraYawDegrees))
		{
			return false;
		}

		const FVector2D WorldSize = WorldBounds.GetSize();
		const FVector2D ScreenSize = ScreenBounds.GetSize();
		return Private::IsFiniteVector(WorldBounds.Min)
			&& Private::IsFiniteVector(WorldBounds.Max)
			&& Private::IsFiniteVector(ScreenBounds.Min)
			&& Private::IsFiniteVector(ScreenBounds.Max)
			&& WorldSize.X > UE_DOUBLE_SMALL_NUMBER
			&& WorldSize.Y > UE_DOUBLE_SMALL_NUMBER
			&& ScreenSize.X > UE_DOUBLE_SMALL_NUMBER
			&& ScreenSize.Y > UE_DOUBLE_SMALL_NUMBER;
	}

	FVector2D FHeadingUpTransform::WorldToScreenUnchecked(
		const FVector2D& WorldPosition) const
	{
		if (!IsValid() || !Private::IsFiniteVector(WorldPosition))
		{
			return FVector2D::ZeroVector;
		}

		FVector2D Forward;
		FVector2D Right;
		Private::GetHeadingBasis(CameraYawDegrees, Forward, Right);
		const FVector2D WorldDelta = WorldPosition - WorldBounds.GetCenter();
		const double WorldSpan = FMath::Max(
			WorldBounds.GetSize().X,
			WorldBounds.GetSize().Y);
		const double ContentX = FVector2D::DotProduct(WorldDelta, Right) / WorldSpan;
		const double ContentUp = FVector2D::DotProduct(WorldDelta, Forward) / WorldSpan;
		const FVector2D ScreenSize = ScreenBounds.GetSize();
		const FVector2D ScreenCenter = ScreenBounds.GetCenter();
		return FVector2D(
			ScreenCenter.X + ContentX * ScreenSize.X,
			ScreenCenter.Y - ContentUp * ScreenSize.Y);
	}

	FVector2D FHeadingUpTransform::ScreenToWorldUnchecked(
		const FVector2D& ScreenPosition) const
	{
		if (!IsValid() || !Private::IsFiniteVector(ScreenPosition))
		{
			return FVector2D::ZeroVector;
		}

		FVector2D Forward;
		FVector2D Right;
		Private::GetHeadingBasis(CameraYawDegrees, Forward, Right);
		const FVector2D ScreenSize = ScreenBounds.GetSize();
		const FVector2D ScreenDelta = ScreenPosition - ScreenBounds.GetCenter();
		const double ContentX = ScreenDelta.X / ScreenSize.X;
		const double ContentUp = -ScreenDelta.Y / ScreenSize.Y;
		const double WorldSpan = FMath::Max(
			WorldBounds.GetSize().X,
			WorldBounds.GetSize().Y);
		return WorldBounds.GetCenter()
			+ (Right * ContentX + Forward * ContentUp) * WorldSpan;
	}

	bool FHeadingUpTransform::TryWorldToScreen(
		const FVector2D& WorldPosition,
		FVector2D& OutScreenPosition) const
	{
		if (!IsValid() || !Private::IsFiniteVector(WorldPosition)
			|| !WorldBounds.IsInsideOrOn(WorldPosition))
		{
			return false;
		}

		OutScreenPosition = WorldToScreenUnchecked(WorldPosition);
		return Private::IsFiniteVector(OutScreenPosition)
			&& ScreenBounds.IsInsideOrOn(OutScreenPosition);
	}

	bool FHeadingUpTransform::TryScreenToWorld(
		const FVector2D& ScreenPosition,
		FVector2D& OutWorldPosition) const
	{
		if (!IsValid() || !Private::IsFiniteVector(ScreenPosition)
			|| !ScreenBounds.IsInsideOrOn(ScreenPosition))
		{
			return false;
		}

		OutWorldPosition = ScreenToWorldUnchecked(ScreenPosition);
		return Private::IsFiniteVector(OutWorldPosition)
			&& WorldBounds.IsInsideOrOn(OutWorldPosition);
	}

	bool ClipLineToScreenBounds(
		const FBox2D& ScreenBounds,
		FVector2D& InOutStart,
		FVector2D& InOutEnd)
	{
		if (!ScreenBounds.bIsValid
			|| !Private::IsFiniteVector(InOutStart)
			|| !Private::IsFiniteVector(InOutEnd))
		{
			return false;
		}

		const FVector2D OriginalStart = InOutStart;
		const FVector2D Direction = InOutEnd - InOutStart;
		double MinimumTime = 0.0;
		double MaximumTime = 1.0;
		if (!Private::ClipTest(-Direction.X, OriginalStart.X - ScreenBounds.Min.X, MinimumTime, MaximumTime)
			|| !Private::ClipTest(Direction.X, ScreenBounds.Max.X - OriginalStart.X, MinimumTime, MaximumTime)
			|| !Private::ClipTest(-Direction.Y, OriginalStart.Y - ScreenBounds.Min.Y, MinimumTime, MaximumTime)
			|| !Private::ClipTest(Direction.Y, ScreenBounds.Max.Y - OriginalStart.Y, MinimumTime, MaximumTime))
		{
			return false;
		}

		InOutStart = OriginalStart + Direction * MinimumTime;
		InOutEnd = OriginalStart + Direction * MaximumTime;
		return true;
	}

	bool ClipRectToScreenBounds(
		const FBox2D& ScreenBounds,
		FVector2D& InOutMinimum,
		FVector2D& InOutSize)
	{
		if (!ScreenBounds.bIsValid
			|| !Private::IsFiniteVector(InOutMinimum)
			|| !Private::IsFiniteVector(InOutSize)
			|| InOutSize.X <= 0.0
			|| InOutSize.Y <= 0.0)
		{
			return false;
		}

		const FVector2D ClippedMinimum(
			FMath::Max(InOutMinimum.X, ScreenBounds.Min.X),
			FMath::Max(InOutMinimum.Y, ScreenBounds.Min.Y));
		const FVector2D ClippedMaximum(
			FMath::Min(InOutMinimum.X + InOutSize.X, ScreenBounds.Max.X),
			FMath::Min(InOutMinimum.Y + InOutSize.Y, ScreenBounds.Max.Y));
		if (ClippedMaximum.X <= ClippedMinimum.X
			|| ClippedMaximum.Y <= ClippedMinimum.Y)
		{
			return false;
		}

		InOutMinimum = ClippedMinimum;
		InOutSize = ClippedMaximum - ClippedMinimum;
		return true;
	}
}
