// Copyright Epic Games, Inc. All Rights Reserved.
#include "Battle/Framework/GuLiBattleGameMode.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

AGuLiBattleGameMode::AGuLiBattleGameMode()
{
    GameStateClass=AGuLiBattleGameState::StaticClass();
    PlayerStateClass=AGuLiBattlePlayerState::StaticClass();
    PlayerControllerClass=AGuLiBattlePlayerController::StaticClass();
    DefaultPawnClass=nullptr;
    bUseSeamlessTravel=true;
}

// Object grants own combat births. Engine login/death callbacks cannot create
// another carrier, assign a seat or start a local respawn timer.
void AGuLiBattleGameMode::RestartPlayer(AController*) {}

UClass* AGuLiBattleGameMode::ResolveConfiguredPawnClass(const EGuLiCommanderRole AssignedRole) const
{
	const TSubclassOf<APawn>* ConfiguredClass = RolePawnClasses.Find(AssignedRole);
	UClass* PawnClass = ConfiguredClass ? ConfiguredClass->Get() : nullptr;
	return IsValid(PawnClass)
		&& PawnClass->IsChildOf(APawn::StaticClass())
		&& !PawnClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		? PawnClass : nullptr;
}

bool AGuLiBattleGameMode::HasCurrentRoleSlot(const AGuLiBattlePlayerState& PlayerState) const
{
	const AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>();
	if (!BattleState || !PlayerState.GetPlayerGuid().IsValid())
	{
		return false;
	}

	for (const FGuLiCommanderRoleSlotState& Slot : BattleState->GetRoleSlots())
	{
		if (Slot.SlotIndex == PlayerState.GetBattleSlotIndex())
		{
			return Slot.bOccupied && Slot.PlayerGuid == PlayerState.GetPlayerGuid()
				&& Slot.Team == PlayerState.GetTeam() && Slot.Role == PlayerState.GetBattleRole();
		}
	}
	return false;
}

bool AGuLiBattleGameMode::EnsureHybridCommanderPawn(APlayerController& Controller)
{
    const auto* Player=Controller.GetPlayerState<AGuLiBattlePlayerState>();
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !Controller.IsLocalController() || !Player
        || !Player->IsCommander() || !Player->IsBattleReady() || !HasCurrentRoleSlot(*Player)) return false;
    if (IsValid(Controller.GetPawn())) return true;
    auto* Class=ResolveConfiguredPawnClass(EGuLiCommanderRole::Commander); if (!Class) return false;
    auto* Start=FindPlayerStart(&Controller);
    const FTransform Transform=Start?Start->GetActorTransform():FTransform::Identity;
    if (auto* Pawn=SpawnDefaultPawnAtTransform(&Controller,Transform))
    { Pawn->SetReplicates(false); Controller.Possess(Pawn); return Controller.GetPawn()==Pawn; }
    return false;
}

UClass* AGuLiBattleGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	APlayerController* PlayerController = Cast<APlayerController>(InController);
	const AGuLiBattlePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	if (!PlayerState || PlayerState->GetBattleRole() == EGuLiCommanderRole::Observer
		|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Unassigned)
	{
		return nullptr;
	}

	// UE 无缝初始化会先为恢复后的 PS 查询 PlayerStart，再进入 GenericPlayerInitialization。
	// 此时新 GameState 尚未认领席位；这里只解析 Pawn 类，不能清掉待恢复的角色/席位。
	return ResolveConfiguredPawnClass(PlayerState->GetBattleRole());
}
