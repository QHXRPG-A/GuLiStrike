// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikeAIController.h"
#include "Components/StateTreeAIComponent.h"
#include "TimerManager.h"

AGuLiStrikeAIController::AGuLiStrikeAIController()
{
	// create the StateTree AI Component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	check(StateTreeAI);

	// Start explicitly after possession so StateTree can resolve the controlled pawn as context.
	bStartAILogicOnPossess = false;
	StateTreeAI->SetStartLogicAutomatically(false);

	// ensure we're attached to the possessed character.
	// this is necessary for EnvQueries to work correctly
	bAttachToPawn = true;
}

void AGuLiStrikeAIController::BeginPlay()
{
	Super::BeginPlay();

	if (StateTreeAI)
	{
		StateTreeAI->SetStartLogicAutomatically(false);
		if (StateTreeAI->IsRunning())
		{
			StateTreeAI->StopLogic(TEXT("Waiting for possession"));
		}
	}
}

void AGuLiStrikeAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	UE_LOG(LogTemp, Log, TEXT("GuLiStrikeAIController possessed %s; scheduling StateTree start."), *GetNameSafe(InPawn));
	GetWorldTimerManager().SetTimerForNextTick(this, &AGuLiStrikeAIController::StartStateTreeLogic);
}

void AGuLiStrikeAIController::OnUnPossess()
{
	if (StateTreeAI && StateTreeAI->IsRunning())
	{
		StateTreeAI->StopLogic(TEXT("Unpossessed"));
	}

	Super::OnUnPossess();
}

void AGuLiStrikeAIController::StartStateTreeLogic()
{
	if (StateTreeAI && GetPawn())
	{
		UE_LOG(LogTemp, Log, TEXT("GuLiStrikeAIController starting StateTree with pawn %s (%s)."), *GetNameSafe(GetPawn()), *GetNameSafe(GetPawn()->GetClass()));
		if (StateTreeAI->IsRunning())
		{
			StateTreeAI->RestartLogic();
		}
		else
		{
			StateTreeAI->StartLogic();
		}
	}
}
