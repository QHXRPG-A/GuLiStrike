// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderGameMode.h"

#include "Commander/Framework/GuLiCommanderGameState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "UObject/ConstructorHelpers.h"

AGuLiCommanderGameMode::AGuLiCommanderGameMode()
{
	GameStateClass = AGuLiCommanderGameState::StaticClass();
	PlayerStateClass = AGuLiCommanderPlayerState::StaticClass();
	PlayerControllerClass = AGuLiCommanderPlayerController::StaticClass();
	DefaultPawnClass = AGuLiCommanderCameraPawn::StaticClass();
	RolePawnClasses.Add(EGuLiCommanderRole::Commander, AGuLiCommanderCameraPawn::StaticClass());
	// Loading this class from the CDO constructor also loads its AnimBlueprint before
	// uncooked animation-node modules have registered. Resolve it on a role query.
	DefaultGroundPawnClass = TSoftClassPtr<APawn>(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C")));
	// 经编辑器核验的现有飞船蓝图；加载失败时保留缺项，由公共出生流程明确转观察者。
	static ConstructorHelpers::FClassFinder<APawn> AirPawn(TEXT("/Game/GuLiStrike/Ship/BP_CombatAvatarFly01"));
	if (AirPawn.Succeeded())
	{
		RolePawnClasses.Add(EGuLiCommanderRole::Air, AirPawn.Class);
	}
	HUDClass = AGuLiCommanderHUD::StaticClass();
}

UClass* AGuLiCommanderGameMode::ResolveConfiguredPawnClass(const EGuLiCommanderRole AssignedRole) const
{
	// Both editor manifest authoring (CDO) and runtime spawning use this same path.
	// Preserve explicit map/Blueprint overrides, including an explicitly empty entry.
	if (AssignedRole != EGuLiCommanderRole::Ground || RolePawnClasses.Contains(AssignedRole))
		return Super::ResolveConfiguredPawnClass(AssignedRole);
	UClass* PawnClass = DefaultGroundPawnClass.LoadSynchronous();
	return IsValid(PawnClass) && PawnClass->IsChildOf(APawn::StaticClass())
		&& !PawnClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		? PawnClass : nullptr;
}
