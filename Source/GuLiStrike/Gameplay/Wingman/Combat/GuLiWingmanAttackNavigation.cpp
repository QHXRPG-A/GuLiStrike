#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#include "Engine/World.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightCollision.h"

bool GuLiWingmanAttack::GroundRunClearsTerrain(UWorld* World, const FGuLiWingmanGroundRunPath& Path,
	float Radius, EGroundPathRejectReason* OutRejectReason)
{
	if (OutRejectReason) *OutRejectReason = EGroundPathRejectReason::None;

	const auto Reject = [OutRejectReason](EGroundPathRejectReason Reason)
	{
		if (OutRejectReason) *OutRejectReason = Reason;
		return false;
	};
	if (!World || !Path.IsValid() || !FMath::IsFinite(Radius) || Radius <= 0)
		return Reject(EGroundPathRejectReason::InvalidInput);
	// Validate the authored dive, pull-up and climb corridor from Entry onward.
	// The live aircraft-to-Entry ingress is checked separately for each member;
	// there is intentionally no fixed speed-scaled lead-in before Entry.
	FVector Previous = GroundRunSetupPoint(Path);
	if (Previous.IsNearlyZero()) return Reject(EGroundPathRejectReason::InvalidInput);
	for (int32 Index = 0; Index <= 24; ++Index)
	{
		const FVector Point = Path.PositionAt(Path.TotalSeconds() * Index / 24.0f);
		FHitResult Hit;
		if (GuLiWingmanFlightCollision::SweepTerrain(*World, Previous, Point, Radius, Hit))
			return Reject(EGroundPathRejectReason::StaticObstacle);
		// Starting above the terrain also detects points already inside a hillside.
		if (!GuLiWingmanFlightCollision::SweepTerrain(*World, Point + FVector(0, 0, 500000),
			Point - FVector(0, 0, 500000), 1.f, Hit))
			return Reject(EGroundPathRejectReason::MissingGround);
		if (Point.Z - Hit.ImpactPoint.Z < MinimumGroundHeight)
			return Reject(EGroundPathRejectReason::GroundClearance);
		Previous = Point;
	}
	return true;
}

bool GuLiWingmanAttack::AirSegmentClearsWorld(UWorld* World, const FVector& Start, const FVector& End, float Radius)
{
	if (!World || Start.ContainsNaN() || End.ContainsNaN() || Start.Equals(End)
		|| !FMath::IsFinite(Radius) || Radius <= 0) return false;
	FHitResult Hit;
	return !GuLiWingmanFlightCollision::SweepTerrain(*World,Start,End,Radius,Hit);
}
