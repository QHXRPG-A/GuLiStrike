// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikeGameMode.h"
#include "GuLiStrikeUI.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

void AGuLiStrikeGameMode::BeginPlay()
{
	// create the UI widget if it hasn't already
	CreateUI();
}

void AGuLiStrikeGameMode::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	
	// clear the combo timer
	GetWorld()->GetTimerManager().ClearTimer(ComboTimer);
}

void AGuLiStrikeGameMode::ItemUsed(int32 Value)
{
	// ensure the UI widget is available
	if (!UIWidget)
	{
		CreateUI();
	}

	// update the UI
	UIWidget->UpdateItems(Value);
}

void AGuLiStrikeGameMode::ScoreUpdate(int32 Value)
{
	// multiply the base score by the combo multiplier and add it to the score
	Score += Value * Combo;

	// update the UI
	UIWidget->UpdateScore(Score);

	// update the combo multiplier
	ComboUpdate();
}

void AGuLiStrikeGameMode::CreateUI()
{
	// avoid creating the UI multiple times
	if(UIWidget)
		return;

	// create the UI widget and add it to the viewport
	UIWidget = CreateWidget<UGuLiStrikeUI>(UGameplayStatics::GetPlayerController(GetWorld(), 0), UIWidgetClass);
	UIWidget->AddToViewport(0);
}

void AGuLiStrikeGameMode::ComboUpdate()
{
	// return
	if (Combo > ComboCap)
	{
		return;
	}

	// update the combo increment
	++ComboIncrement;

	// is it time to increase the multiplier?
	if (ComboIncrement > ComboIncrementMax)
	{
		// reset the combo increment
		ComboIncrement = 0;

		// increase the combo multiplier
		++Combo;

		// update the UI
		UIWidget->UpdateCombo(Combo);

	}

	// reset the cooldown timer
	ResetComboCooldown();
}

void AGuLiStrikeGameMode::ResetComboCooldown()
{
	// reset the combo cooldown timer
	GetWorld()->GetTimerManager().SetTimer(ComboTimer, this, &AGuLiStrikeGameMode::ResetCombo, ComboCooldown, false);
}

void AGuLiStrikeGameMode::ResetCombo()
{
	// is the combo multiplier above min?
	if (Combo > 1)
	{
		// reset the combo increment
		ComboIncrement = 0;

		// tick down the multiplier
		--Combo;

		// update the UI
		UIWidget->UpdateCombo(Combo);

		// reset the cooldown timer
		ResetComboCooldown();
	}
}

bool AGuLiStrikeGameMode::CanSpawnNPCs()
{
	// is the NPC counter under the cap?
	return NPCCount < NPCCap;
}

void AGuLiStrikeGameMode::IncreaseNPCs()
{
	// increase the NPC counter
	++NPCCount;
}

void AGuLiStrikeGameMode::DecreaseNPCs()
{
	// decrease the NPC counter
	--NPCCount;
}
