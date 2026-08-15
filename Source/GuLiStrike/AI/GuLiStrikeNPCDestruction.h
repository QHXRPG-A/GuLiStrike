// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GuLiStrikeNPCDestruction.generated.h"

/**
 *  A NPC destruction proxy for a Twin Stick Shooter game
 *  Replaces the NPC when it is destroyed,
 *  allowing it to play effects without affecting gameplay 
 */
UCLASS(abstract)
class AGuLiStrikeNPCDestruction : public AActor
{
	GENERATED_BODY()
	
public:

	/** Constructor */
	AGuLiStrikeNPCDestruction();

};
