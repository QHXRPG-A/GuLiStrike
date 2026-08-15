// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GuLiStrikeAIController.generated.h"

class UStateTreeAIComponent;

/**
 *  A StateTree-Enabled AI Controller for a Twin Stick Shooter game
 *  Runs NPC logic through a StateTree
 */
UCLASS(abstract)
class AGuLiStrikeAIController : public AAIController
{
	GENERATED_BODY()
	
	/** StateTree Component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UStateTreeAIComponent* StateTreeAI;

public:

	/** Constructor */
	AGuLiStrikeAIController();

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Starts StateTree logic after possession has fully settled. */
	virtual void OnPossess(APawn* InPawn) override;

	/** Stops StateTree logic before the controller releases its pawn. */
	virtual void OnUnPossess() override;

private:

	void StartStateTreeLogic();
};
