#include "Gameplay/Building/GuLiConstructionShape.h"

bool UGuLiConstructionShape::IsUsable() const
{
	return Bounds.IsValid && Contours.ContainsByPredicate([](const FGuLiConstructionContour& C)
	{
		return !C.bHole && C.Points.Num() >= 3;
	});
}
