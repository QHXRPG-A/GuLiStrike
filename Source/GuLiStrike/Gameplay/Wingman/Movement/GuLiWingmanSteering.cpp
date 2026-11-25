#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"

namespace GuLiWingmanSteering
{
	FVector TurnDirectionToward(
		const FVector& CurrentDirection,
		const FVector& DesiredDirection,
		const float MaximumRadians)
	{
		const FVector From = CurrentDirection.GetSafeNormal();
		const FVector To = DesiredDirection.GetSafeNormal();
		if (From.IsNearlyZero()) return To.IsNearlyZero() ? FVector::ForwardVector : To;
		if (To.IsNearlyZero() || MaximumRadians <= 0.0f) return From;
		const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(From, To), -1.0f, 1.0f));
		if (Angle <= MaximumRadians) return To;
		FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			const FVector Reference = FMath::Abs(From.Z) < 0.9f
				? FVector::UpVector : FVector::RightVector;
			Axis = FVector::CrossProduct(From, Reference).GetSafeNormal();
		}
		return FQuat(Axis, MaximumRadians).RotateVector(From).GetSafeNormal();
	}
}
