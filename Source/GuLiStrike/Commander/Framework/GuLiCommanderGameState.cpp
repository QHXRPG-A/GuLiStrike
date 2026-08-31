// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderGameState.h"

#include "Net/UnrealNetwork.h"

namespace GuLiCommanderRoleSlots
{
	constexpr uint8 SlotCount = 10;

	// The first two connections receive opposing Commander slots. Remaining
	// connections alternate teams while filling Ground x2 then Air x2.
	constexpr uint8 AssignmentOrder[SlotCount] = {0, 5, 1, 6, 2, 7, 3, 8, 4, 9};
}

AGuLiCommanderGameState::AGuLiCommanderGameState()
{
	InitializeDefaultRoleSlots();
}

// 复制注册只声明字段及条件；ForceNetUpdate 促使尽早更新，不保证本帧到达所有客户端。
void AGuLiCommanderGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiCommanderGameState, ProtocolVersion);
	DOREPLIFETIME(AGuLiCommanderGameState, MatchEpoch);
	DOREPLIFETIME(AGuLiCommanderGameState, MatchId);
	DOREPLIFETIME(AGuLiCommanderGameState, RoleSlots);
	DOREPLIFETIME(AGuLiCommanderGameState, EffectiveSoldierMoveSpeedCmPerSecond);
	DOREPLIFETIME(AGuLiCommanderGameState, RuntimeTuningRevision);
}

void AGuLiCommanderGameState::InitializeServerMatchState()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!MatchId.IsValid())
	{
		MatchId = FGuid::NewGuid();
	}

	if (MatchEpoch == 0)
	{
		MatchEpoch = FMath::Max(1u, static_cast<uint32>(FPlatformTime::Cycles64()));
	}

	if (RoleSlots.Num() != GuLiCommanderRoleSlots::SlotCount)
	{
		InitializeDefaultRoleSlots();
	}

	ForceNetUpdate();
}

bool AGuLiCommanderGameState::ClaimRoleSlot(
	const FGuid& PlayerGuid,
	uint8 PreferredSlotIndex,
	uint8& OutSlotIndex,
	EGuLiTeam& OutTeam,
	EGuLiCommanderRole& OutRole)
{
	OutSlotIndex = MAX_uint8;
	OutTeam = EGuLiTeam::Unassigned;
	OutRole = EGuLiCommanderRole::Observer;

	if (!HasAuthority() || !PlayerGuid.IsValid())
	{
		return false;
	}

	// Idempotent claim: a reconnect path may ask again after its slot was restored.
	for (const FGuLiCommanderRoleSlotState& Slot : RoleSlots)
	{
		if (Slot.bOccupied && Slot.PlayerGuid == PlayerGuid)
		{
			OutSlotIndex = Slot.SlotIndex;
			OutTeam = Slot.Team;
			OutRole = Slot.Role;
			return true;
		}
	}

	if (PreferredSlotIndex < RoleSlots.Num()
		&& TryClaimRoleSlot(PreferredSlotIndex, PlayerGuid, OutSlotIndex, OutTeam, OutRole))
	{
		return true;
	}

	for (const uint8 SlotIndex : GuLiCommanderRoleSlots::AssignmentOrder)
	{
		if (TryClaimRoleSlot(SlotIndex, PlayerGuid, OutSlotIndex, OutTeam, OutRole))
		{
			return true;
		}
	}

	return false;
}

void AGuLiCommanderGameState::ReleaseRoleSlot(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid)
{
	if (!HasAuthority() || !RoleSlots.IsValidIndex(SlotIndex))
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
	Slot.bSyncReady = false;
	NotifyRoleSlotsChanged();
}

void AGuLiCommanderGameState::SetRoleSlotSyncReady(
	uint8 SlotIndex,
	const FGuid& ExpectedPlayerGuid,
	bool bReady)
{
	if (!HasAuthority() || !RoleSlots.IsValidIndex(SlotIndex))
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

void AGuLiCommanderGameState::SetAuthoritativeSoldierMovementTuning(
	const float EffectiveMoveSpeedCmPerSecond,
	const uint32 TuningRevision)
{
	if (!HasAuthority() || !FMath::IsFinite(EffectiveMoveSpeedCmPerSecond)
		|| EffectiveMoveSpeedCmPerSecond <= 0.0f)
	{
		return;
	}

	this->EffectiveSoldierMoveSpeedCmPerSecond = EffectiveMoveSpeedCmPerSecond;
	RuntimeTuningRevision = TuningRevision;
	OnRuntimeTuningChanged.Broadcast(
		this->EffectiveSoldierMoveSpeedCmPerSecond,
		RuntimeTuningRevision);
	ForceNetUpdate();
}

void AGuLiCommanderGameState::InitializeDefaultRoleSlots()
{
	RoleSlots.Reset(GuLiCommanderRoleSlots::SlotCount);

	auto AddSlot = [this](uint8 SlotIndex, EGuLiTeam Team, EGuLiCommanderRole SlotRole)
	{
		FGuLiCommanderRoleSlotState& Slot = RoleSlots.AddDefaulted_GetRef();
		Slot.SlotIndex = SlotIndex;
		Slot.Team = Team;
		Slot.Role = SlotRole;
	};

	AddSlot(0, EGuLiTeam::Red, EGuLiCommanderRole::Commander);
	AddSlot(1, EGuLiTeam::Red, EGuLiCommanderRole::Ground);
	AddSlot(2, EGuLiTeam::Red, EGuLiCommanderRole::Ground);
	AddSlot(3, EGuLiTeam::Red, EGuLiCommanderRole::Air);
	AddSlot(4, EGuLiTeam::Red, EGuLiCommanderRole::Air);
	AddSlot(5, EGuLiTeam::Blue, EGuLiCommanderRole::Commander);
	AddSlot(6, EGuLiTeam::Blue, EGuLiCommanderRole::Ground);
	AddSlot(7, EGuLiTeam::Blue, EGuLiCommanderRole::Ground);
	AddSlot(8, EGuLiTeam::Blue, EGuLiCommanderRole::Air);
	AddSlot(9, EGuLiTeam::Blue, EGuLiCommanderRole::Air);
}

bool AGuLiCommanderGameState::TryClaimRoleSlot(
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
	Slot.bSyncReady = false;
	OutSlotIndex = Slot.SlotIndex;
	OutTeam = Slot.Team;
	OutRole = Slot.Role;
	NotifyRoleSlotsChanged();
	return true;
}

// 服务器赋值后主动通知本地订阅者；客户端则从 OnRep_RoleSlots 进入同一类本地通知。
void AGuLiCommanderGameState::NotifyRoleSlotsChanged()
{
	OnRoleSlotsChanged.Broadcast();
	ForceNetUpdate();
}

void AGuLiCommanderGameState::OnRep_RoleSlots()
{
	OnRoleSlotsChanged.Broadcast();
}

// 客户端读取此刻的速度/版本并广播；订阅者应容忍重复通知，不能假设跨属性通知顺序。
void AGuLiCommanderGameState::OnRep_RuntimeTuning()
{
	OnRuntimeTuningChanged.Broadcast(
		EffectiveSoldierMoveSpeedCmPerSecond,
		RuntimeTuningRevision);
}
