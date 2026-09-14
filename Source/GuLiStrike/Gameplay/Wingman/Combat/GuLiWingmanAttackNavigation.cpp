#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#include "Engine/World.h"
#include "GuLiFlightNavigationSubsystem.h"

bool GuLiWingmanAttack::GroundRunClearsTerrain(UWorld* World, const FGuLiWingmanGroundRunPath& Path,
	float Radius, EGroundPathRejectReason* OutRejectReason,
	EGuLiFlightNavSegmentStatus* OutNavigationStatus)
{
	if (OutRejectReason) *OutRejectReason = EGroundPathRejectReason::None;
	if (OutNavigationStatus) *OutNavigationStatus = EGuLiFlightNavSegmentStatus::Valid;
	const auto Reject = [OutRejectReason](EGroundPathRejectReason Reason)
	{
		if (OutRejectReason) *OutRejectReason = Reason;
		return false;
	};
	if (!World || !Path.IsValid() || !FMath::IsFinite(Radius) || Radius <= 0)
		return Reject(EGroundPathRejectReason::InvalidInput);
	const auto* Nav = World->GetSubsystem<UGuLiFlightNavigationSubsystem>();
	if (!Nav) return Reject(EGroundPathRejectReason::Navigation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiWingmanAttackPath), false);
	// Validate the authored dive, pull-up and climb corridor from Entry onward.
	// The live aircraft-to-Entry ingress is checked separately for each member;
	// there is intentionally no fixed speed-scaled lead-in before Entry.
	FVector Previous = GroundRunSetupPoint(Path);
	if (Previous.IsNearlyZero()) return Reject(EGroundPathRejectReason::InvalidInput);
	for (int32 Index = 0; Index <= 24; ++Index)
	{
		const FVector Point = Path.PositionAt(Path.TotalSeconds() * Index / 24.0f);
		FHitResult Hit;
		const EGuLiFlightNavSegmentStatus NavigationStatus =
			Nav->ValidateAuthoritativeSegment(Previous, Point, Radius);
		if (NavigationStatus != EGuLiFlightNavSegmentStatus::Valid)
		{
			if (OutNavigationStatus) *OutNavigationStatus = NavigationStatus;
			return Reject(EGroundPathRejectReason::Navigation);
		}
		if (World->SweepSingleByObjectType(Hit, Previous, Point, FQuat::Identity,
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius), Params))
			return Reject(EGroundPathRejectReason::StaticObstacle);
		// Starting above the terrain also detects points already inside a hillside.
		if (!World->LineTraceSingleByObjectType(Hit, Point + FVector(0, 0, 500000), Point - FVector(0, 0, 500000),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
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
	const auto* Nav = World->GetSubsystem<UGuLiFlightNavigationSubsystem>();
	if (!Nav || Nav->ValidateAuthoritativeSegment(Start, End, Radius) != EGuLiFlightNavSegmentStatus::Valid) return false;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiWingmanAirManeuver), false);
	return !World->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius), Params);
}
