// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Network/GuLiNetworkSettings.h"

#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiMissileVisualSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"

namespace GuLiBattleRoleSlots { constexpr uint8 SlotCount = 6; }

AGuLiBattleGameState::AGuLiBattleGameState()
{
	InitializeDefaultRoleSlots();
	CreateDefaultSubobject<UGuLiCombatEffectReplicationComponent>(TEXT("CombatEffectReplication"));
}

AGuLiBattleGameState::~AGuLiBattleGameState() = default;

void AGuLiBattleGameState::BeginPlay()
{
    Super::BeginPlay();
    if (MatchEpoch) OnRep_MatchEpoch();
}





bool AGuLiBattleGameState::ApplyHybridMatchIdentity(const FGuid& Identity,const uint64 Epoch)
{
	if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !Identity.IsValid() || !Epoch || Epoch>MAX_uint32
		|| (MatchId.IsValid() && MatchId!=Identity) || Epoch<MatchEpoch) return false;
	MatchId=Identity;
	if (MatchEpoch!=Epoch)
	{
		MatchEpoch=static_cast<uint32>(Epoch); OnRep_MatchEpoch();
		if (auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()) Ledger->BindMatchEpoch(MatchEpoch);
	}
	if (RoleSlots.IsEmpty()) InitializeDefaultRoleSlots();
	return true;
}

bool AGuLiBattleGameState::ClaimExactRoleSlot(const FGuid& PlayerGuid, uint8 SlotIndex)
{
	if (!PlayerGuid.IsValid() || !RoleSlots.IsValidIndex(SlotIndex)) return false;
	for (const auto& Slot : RoleSlots)
		if (Slot.bOccupied && Slot.PlayerGuid==PlayerGuid) return Slot.SlotIndex==SlotIndex;
	uint8 Claimed; EGuLiTeam Team; EGuLiCommanderRole ClaimedRole;
	return TryClaimRoleSlot(SlotIndex,PlayerGuid,Claimed,Team,ClaimedRole);
}

void AGuLiBattleGameState::ReleaseRoleSlot(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid)
{
	if (!RoleSlots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FGuLiCommanderRoleSlotState& Slot = RoleSlots[SlotIndex];
	if (!Slot.bOccupied || Slot.PlayerGuid != ExpectedPlayerGuid)
	{
		return;
	}

	Slot.PlayerGuid.Invalidate();
	Slot.bOccupied = false;
	Slot.bBattleReady = false;
	Slot.bSyncReady = false;
	NotifyRoleSlotsChanged();
}

void AGuLiBattleGameState::SetRoleSlotBattleReady(
	uint8 SlotIndex,
	const FGuid& ExpectedPlayerGuid,
	bool bReady)
{
	if (!RoleSlots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FGuLiCommanderRoleSlotState& Slot = RoleSlots[SlotIndex];
	if (!Slot.bOccupied || Slot.PlayerGuid != ExpectedPlayerGuid || Slot.bBattleReady == bReady)
	{
		return;
	}

	Slot.bBattleReady = bReady;
	NotifyRoleSlotsChanged();
}

void AGuLiBattleGameState::SetRoleSlotSyncReady(
	uint8 SlotIndex,
	const FGuid& ExpectedPlayerGuid,
	bool bReady)
{
	if (!RoleSlots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FGuLiCommanderRoleSlotState& Slot = RoleSlots[SlotIndex];
	if (!Slot.bOccupied || Slot.PlayerGuid != ExpectedPlayerGuid || Slot.bSyncReady == bReady)
	{
		return;
	}

	Slot.bSyncReady = bReady;
	NotifyRoleSlotsChanged();
}

void AGuLiBattleGameState::InitializeDefaultRoleSlots()
{
    RoleSlots.Reset(GuLiBattleRoleSlots::SlotCount);
    for (const auto Team : {EGuLiTeam::Red,EGuLiTeam::Blue})
        for (const auto SeatRole : {EGuLiCommanderRole::Commander,EGuLiCommanderRole::Ground,EGuLiCommanderRole::Air})
        {
            const uint8 Index=uint8(RoleSlots.Num());
            auto& Slot=RoleSlots.AddDefaulted_GetRef();Slot.SlotIndex=Index;Slot.Team=Team;Slot.Role=SeatRole;
        }
}

bool AGuLiBattleGameState::TryClaimRoleSlot(
	uint8 SlotIndex,
	const FGuid& PlayerGuid,
	uint8& OutSlotIndex,
	EGuLiTeam& OutTeam,
	EGuLiCommanderRole& OutRole)
{
	if (!RoleSlots.IsValidIndex(SlotIndex))
	{
		return false;
	}

	FGuLiCommanderRoleSlotState& Slot = RoleSlots[SlotIndex];
	if (Slot.bOccupied)
	{
		return false;
	}

	Slot.PlayerGuid = PlayerGuid;
	Slot.bOccupied = true;
	Slot.bBattleReady = false;
	Slot.bSyncReady = false;
	OutSlotIndex = Slot.SlotIndex;
	OutTeam = Slot.Team;
	OutRole = Slot.Role;
	NotifyRoleSlotsChanged();
	return true;
}

// Apply the accepted room view, then notify local subscribers.
void AGuLiBattleGameState::NotifyRoleSlotsChanged()
{
	OnRoleSlotsChanged.Broadcast();
}

void AGuLiBattleGameState::OnRep_RoleSlots()
{
	OnRoleSlotsChanged.Broadcast();
}

void AGuLiBattleGameState::OnRep_MatchEpoch()
{
	if (GetWorld())
	{
		if (UGuLiMissileVisualSubsystem* Visuals =
			GetWorld()->GetSubsystem<UGuLiMissileVisualSubsystem>())
		{
			Visuals->BeginEpoch(MatchEpoch);
		}
	}
}

