// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GuLiStrikePickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 *  A simple pickup for a Twin Stick Shooter game
 */
UCLASS(abstract)
class AGuLiStrikePickup : public AActor
{
	GENERATED_BODY()
	
	/** Pickup collision sphere */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USphereComponent* CollisionSphere;

	/** Provides visual representation for the pickup */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Mesh;

public:	

	/** Constructor */
	AGuLiStrikePickup();

	/** Collision handling */
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;

};
