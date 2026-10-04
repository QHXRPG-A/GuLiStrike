#include "Commander/Presentation/GuLiCommanderCameraGeometry.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Engine/World.h"

bool GuLiCommanderCameraGeometry::GetBattleBounds(const UWorld* World, FBox& OutBounds)
{
	OutBounds = FBox(ForceInit);
	const auto* Landscape = World ? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>() : nullptr;
	if (!Landscape || !Landscape->TryGetWorldBounds(OutBounds)) return false;
	const auto* Resources = World->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (Resources && Resources->IsResourceWorldActive() && Resources->GetMapDefinition())
	{
		const FBox2D Playable = Resources->GetPlayableBounds();
		OutBounds.Min.X = Playable.Min.X; OutBounds.Min.Y = Playable.Min.Y;
		OutBounds.Max.X = Playable.Max.X; OutBounds.Max.Y = Playable.Max.Y;
	}
	return OutBounds.IsValid && !OutBounds.Min.ContainsNaN() && !OutBounds.Max.ContainsNaN();
}

double GuLiCommanderCameraGeometry::SelectionRayLength(const FVector& Origin, const UWorld* World)
{
	FBox Bounds(ForceInit);
	if (Origin.ContainsNaN()) return 0;
	if (!GetBattleBounds(World, Bounds)) return 600000.0; // Existing non-Landscape fixtures.
	return FVector::Distance(Origin, Bounds.GetCenter()) + Bounds.GetExtent().Size() + 1000.0;
}
