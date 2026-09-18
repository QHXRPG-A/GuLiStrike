// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikePickup.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "GuLiStrikeCharacter.h"
#include "Components/StaticMeshComponent.h"

AGuLiStrikePickup::AGuLiStrikePickup()
{
 	PrimaryActorTick.bCanEverTick = true;

	// create the root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the collision sphere
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Sphere"));
	CollisionSphere->SetupAttachment(RootComponent);

	CollisionSphere->SetSphereRadius(20.0f);
	CollisionSphere->SetRelativeLocation(FVector(0.0f, 0.0f, 25.0f));
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// create the mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(CollisionSphere);
	Mesh->SetRelativeScale3D(FVector(0.2f));

	Mesh->SetCollisionProfileName(FName("NoCollision"));

}

void AGuLiStrikePickup::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	// have we overlapped the player character?
	if (AGuLiStrikeCharacter* PlayerCharacter = Cast<AGuLiStrikeCharacter>(OtherActor))
	{
		// give the pickup to the player
		PlayerCharacter->AddPickup();

		// destroy this pickup
		Destroy();
	}
}
