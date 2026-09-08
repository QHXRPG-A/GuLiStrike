#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#include "Engine/World.h"
#include "GuLiFlightNavigationSubsystem.h"

bool GuLiWingmanAttack::GroundRunClearsTerrain(UWorld* World, const FGuLiWingmanGroundRunPath& Path, float Radius)
{
	if (!World || !Path.IsValid() || !FMath::IsFinite(Radius) || Radius <= 0) return false;
	const auto* Nav = World->GetSubsystem<UGuLiFlightNavigationSubsystem>();
	if (!Nav) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiWingmanAttackPath), false);
	FVector Previous = Path.Entry;
	for (int32 Index = 0; Index <= 24; ++Index)
	{
		const FVector Point = Path.PositionAt(Path.TotalSeconds() * Index / 24.0f);
		FHitResult Hit;
		if (Nav->ValidateAuthoritativeSegment(Previous, Point, Radius) != EGuLiFlightNavSegmentStatus::Valid
			|| World->SweepSingleByObjectType(Hit, Previous, Point, FQuat::Identity,
				FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius), Params)) return false;
		// Starting above the terrain also detects points already inside a hillside.
		if (!World->LineTraceSingleByObjectType(Hit, Point + FVector(0, 0, 500000), Point - FVector(0, 0, 500000),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			|| Point.Z - Hit.ImpactPoint.Z < MinimumGroundHeight) return false;
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
