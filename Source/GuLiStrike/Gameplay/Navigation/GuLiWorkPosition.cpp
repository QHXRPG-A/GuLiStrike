#include "Gameplay/Navigation/GuLiWorkPosition.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

bool GuLiWorkPosition::ProjectPose(const ACharacter& Vehicle, const FVector& Desired, const FRotator& Rotation, FTransform& Out)
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Vehicle.GetWorld());
	const auto* Model = Cast<IGuLiEngineeringVehicle>(&Vehicle);
	if (!Nav || !Model) return false;
	// The character capsule is the physical/nav body. The mining arm may overhang
	// ore without colliding; using its visual bounds would make inner ore unreachable.
	const double Radius=Vehicle.GetCapsuleComponent()->GetScaledCapsuleRadius();
	const double HalfHeight=Vehicle.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FBox Bounds=FBox::BuildAABB(FVector::ZeroVector,FVector(Radius,Radius,HalfHeight));
	const auto* Data = Nav->GetNavDataForProps(Vehicle.GetNavAgentPropertiesRef(), Desired);
	FNavLocation Ground;
	if (!Data || !Nav->ProjectPointToNavigation(Desired, Ground, FVector(80,80,5000), Data)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiWorkPosition), false, &Vehicle);
	TInlineComponentArray<UChildActorComponent*> Children(&Vehicle);
	for (const auto* Child : Children) Query.AddIgnoredActor(Child->GetChildActor());
	// Vehicles are temporary occupants, never a reason to remove a static work position.
	FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
	const FTransform GroundPose(Rotation, Ground.Location);
	double MinZ = TNumericLimits<double>::Max(), MaxZ = -TNumericLimits<double>::Max();
	for (const FVector2D Corner : {FVector2D::ZeroVector, FVector2D(Bounds.Min.X,Bounds.Min.Y),
		FVector2D(Bounds.Min.X,Bounds.Max.Y), FVector2D(Bounds.Max.X,Bounds.Min.Y), FVector2D(Bounds.Max.X,Bounds.Max.Y)})
	{
		const FVector Point = GroundPose.TransformPosition(FVector(Corner.X,Corner.Y,0));
		FHitResult Hit;
		if (!Vehicle.GetWorld()->LineTraceSingleByObjectType(Hit, Point+FVector(0,0,100), Point-FVector(0,0,150), StaticObjects, Query)
			|| Hit.ImpactNormal.Z < Vehicle.GetCharacterMovement()->GetWalkableFloorZ()) return false;
		MinZ = FMath::Min(MinZ, Hit.ImpactPoint.Z); MaxZ = FMath::Max(MaxZ, Hit.ImpactPoint.Z);
	}
	if (MaxZ-MinZ > Vehicle.GetCharacterMovement()->MaxStepHeight) return false;
	Out = FTransform(Rotation, FVector(Ground.Location.X,Ground.Location.Y,
		MaxZ+FMath::Max(double(Vehicle.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),-Bounds.Min.Z)+1));
	return !Vehicle.GetWorld()->OverlapAnyTestByObjectType(Out.TransformPosition(Bounds.GetCenter()), Out.GetRotation(),
		StaticObjects, FCollisionShape::MakeCapsule(Radius,HalfHeight*.99), Query);
}
