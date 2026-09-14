#include "Gameplay/Building/GuLiGroundAccessRampComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/ConstructorHelpers.h"

UGuLiGroundAccessRampComponent::UGuLiGroundAccessRampComponent()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	SetStaticMesh(Cube.Object);
	SetMobility(EComponentMobility::Movable);
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	SetCanEverAffectNavigation(true);
	FitRamp(GroundEdge);
}

void UGuLiGroundAccessRampComponent::FitRamp(const FVector& LowerEdge)
{
	const FVector Along = LowerEdge - ApronEdge;
	const FRotator Rotation = Along.Rotation();
	const FVector Center = (LowerEdge + ApronEdge) * 0.5f - Rotation.RotateVector(FVector(0, 0, Thickness * 0.5f));
	SetRelativeTransform(FTransform(Rotation, Center, FVector(Along.Length() / 100.0f, Width / 100.0f, Thickness / 100.0f)));
}

void UGuLiGroundAccessRampComponent::BeginPlay()
{
	Super::BeginPlay();
	const FTransform Building = GetOwner()->GetActorTransform();
	const FVector Point = Building.TransformPosition(GroundEdge);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiAccessRampGround), false, GetOwner());
	Params.AddIgnoredActor(GetOwner()->GetParentActor());
	FHitResult Ground;
	const bool bHasGround = GetWorld()->LineTraceSingleByObjectType(Ground,
		FVector(Point.X, Point.Y, Building.GetLocation().Z + 500),
		FVector(Point.X, Point.Y, Building.GetLocation().Z - 5000),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	checkf(bHasGround, TEXT("Building access ramp needs a ground surface at its entrance."));
	FitRamp(Building.InverseTransformPosition(Ground.ImpactPoint));
}
