// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikeNPC.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GuLiStrikeCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GuLiStrikeGameMode.h"
#include "GuLiStrikePickup.h"
#include "Engine/World.h"
#include "GuLiStrikeNPCDestruction.h"
#include "TimerManager.h"

AGuLiStrikeNPC::AGuLiStrikeNPC()
{
	PrimaryActorTick.bCanEverTick = true;

	// ensure we spawn an AI controller when we're spawned
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// configure the inherited components
	GetCapsuleComponent()->SetCapsuleRadius(45.0f);
	GetCapsuleComponent()->SetNotifyRigidBodyCollision(true);

	GetMesh()->SetCollisionProfileName(FName("NoCollision"));

	GetCharacterMovement()->GravityScale = 1.5f;
	GetCharacterMovement()->MaxAcceleration = 1000.0f;
	GetCharacterMovement()->BrakingFriction = 1.0f;
	GetCharacterMovement()->MaxWalkSpeed = 200.0f;
	GetCharacterMovement()->MaxWalkSpeedCrouched = 100.0f;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 640.0f, 0.0f);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->bUseRVOAvoidance = true;
	GetCharacterMovement()->AvoidanceConsiderationRadius = 250.0f;
	GetCharacterMovement()->AvoidanceWeight = 1.0f;
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;
}

void AGuLiStrikeNPC::BeginPlay()
{
	Super::BeginPlay();

	// increment the NPC counter so we can cap spawning if necessary
	if (AGuLiStrikeGameMode* GM = Cast<AGuLiStrikeGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncreaseNPCs();
	}

}

void AGuLiStrikeNPC::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the destruction timer
	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
}

void AGuLiStrikeNPC::Destroyed()
{
	// decrease the NPC counter so we can cap spawning if necessary
	if (AGuLiStrikeGameMode* GM = Cast<AGuLiStrikeGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->DecreaseNPCs();
	}

	Super::Destroyed();
}

void AGuLiStrikeNPC::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	// have we collided against the player?
	if (AGuLiStrikeCharacter* PlayerCharacter = Cast<AGuLiStrikeCharacter>(Other))
	{
		// apply damage to the character
		PlayerCharacter->HandleDamage(1.0f, GetActorForwardVector());
	}
}

void AGuLiStrikeNPC::ProjectileImpact(const FVector& ForwardVector)
{
	// only handle damage if we haven't been hit yet
	if (bHit)
	{
		return;
	}

	// raise the hit flag
	bHit = true;

	// deactivate character movement
	GetCharacterMovement()->Deactivate();

	// award points
	if (AGuLiStrikeGameMode* GM = Cast<AGuLiStrikeGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->ScoreUpdate(Score);
	}

	// randomly spawn a pickup
	if (FMath::RandRange(0, 100) < PickupSpawnChance)
	{
		AGuLiStrikePickup* Pickup = GetWorld()->SpawnActor<AGuLiStrikePickup>(PickupClass, GetActorTransform());
	}
	
	// spawn the NPC destruction proxy
	AGuLiStrikeNPCDestruction* DestructionProxy = GetWorld()->SpawnActor<AGuLiStrikeNPCDestruction>(DestructionProxyClass, GetActorTransform());

	// hide this actor
	SetActorHiddenInGame(true);

	// disable collision
	SetActorEnableCollision(false);

	// defer destruction
	GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this, &AGuLiStrikeNPC::DeferredDestroy, DeferredDestructionTime, false);
}

void AGuLiStrikeNPC::DeferredDestroy()
{
	// destroy this actor
	Destroy();
}
