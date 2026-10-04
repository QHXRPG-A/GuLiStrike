#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Presentation/GuLiVATAnimation.h"

FTransform FGuLiSoldierDefinition::MakeModelTransform(const FTransform& LogicalPose) const
{
	FTransform Result = LogicalPose;
	// Logical soldier poses never carry presentation scale. Assign, do not multiply.
	Result.SetScale3D(FVector(PresentationScale));
	return Result;
}

FBox FGuLiSoldierDefinition::GetModelBoundsCentimeters() const
{
	const FBox Bounds = VATDefinition ? VATDefinition->GameplayBounds : Model ? Model->GetBoundingBox() : FBox(ForceInit);
	return Bounds.IsValid ? Bounds.TransformBy(
		FTransform(FQuat::Identity, FVector::ZeroVector, FVector(PresentationScale))) : FBox(ForceInit);
}

FVector FGuLiSoldierDefinition::ResolveModelOffsetCentimeters(const FVector& AuthoredOffset) const
{
	return AuthoredOffset * PresentationScale;
}
