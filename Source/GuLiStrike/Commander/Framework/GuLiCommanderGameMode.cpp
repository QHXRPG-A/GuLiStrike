// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderGameMode.h"

#include "Commander/Framework/GuLiCommanderGameState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Framework/GuLiCommanderWorldReplicationComponent.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Gameplay/WarMachine/GuLiWarMachinePlaceholderPawn.h"
#include "UObject/ConstructorHelpers.h"

AGuLiCommanderGameMode::AGuLiCommanderGameMode()
{
	GameStateClass = AGuLiCommanderGameState::StaticClass();
	PlayerStateClass = AGuLiCommanderPlayerState::StaticClass();
	PlayerControllerClass = AGuLiCommanderPlayerController::StaticClass();
	DefaultPawnClass = AGuLiCommanderCameraPawn::StaticClass();
	RolePawnClasses.Add(EGuLiCommanderRole::Commander, AGuLiCommanderCameraPawn::StaticClass());
	RolePawnClasses.Add(EGuLiCommanderRole::Ground, AGuLiWarMachinePlaceholderPawn::StaticClass());
	// 经编辑器核验的现有飞船蓝图；加载失败时保留缺项，由公共出生流程明确转观察者。
	static ConstructorHelpers::FClassFinder<APawn> AirPawn(TEXT("/Game/GuLiStrike/Ship/BP_CombatAvatarFly01"));
	if (AirPawn.Succeeded())
	{
		RolePawnClasses.Add(EGuLiCommanderRole::Air, AirPawn.Class);
	}
	HUDClass = AGuLiCommanderHUD::StaticClass();
	WorldReplicationComponent = CreateDefaultSubobject<UGuLiCommanderWorldReplicationComponent>(TEXT("SoldierWorldReplication"));
}
