#pragma once
#include "CoreMinimal.h"

class APawn;
class UCharacterMovementComponent;
class UWorld;
struct FHitResult;

/** Physical support policy for ground-unit teleport/transit destinations. */
namespace GuLiLandingGround
{
	// Buildings (including attached presentation geometry) and other units are
	// never landing support. This does not change normal factory ramp/interior movement.
	bool IsWalkableSupport(const FHitResult& Hit, const UCharacterMovementComponent* Movement = nullptr);

	// Authority requires navigation for this pawn, or commander Mass navigation
	// when Pawn is null, on the same physical surface. Clients check support only.
	bool Resolve(UWorld& World, const FVector& Point, FVector& OutNavLocation,
		double* OutSurfaceHeight = nullptr, const APawn* Pawn = nullptr);
}
