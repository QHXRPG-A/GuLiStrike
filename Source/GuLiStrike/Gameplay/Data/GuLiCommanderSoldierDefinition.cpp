#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Engine/StaticMesh.h"

FTransform FGuLiSoldierDefinition::MakeModelTransform(const FTransform& LogicalPose) const
{
	FTransform Result = LogicalPose;
	// Logical soldier poses never carry presentation scale. Assign, do not multiply.
	Result.SetScale3D(FVector(PresentationScale));
	return Result;
}

FBox FGuLiSoldierDefinition::GetModelBoundsCentimeters() const
{
	return Model ? Model->GetBoundingBox().TransformBy(
		FTransform(FQuat::Identity, FVector::ZeroVector, FVector(PresentationScale))) : FBox(ForceInit);
}

FVector FGuLiSoldierDefinition::ResolveModelOffsetCentimeters(const FVector& AuthoredOffset) const
{
	return AuthoredOffset * PresentationScale;
}
