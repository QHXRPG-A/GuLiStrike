// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattleGameState.h"

#include "Net/UnrealNetwork.h"

namespace GuLiBattleRoleSlots
{
	constexpr uint8 SlotCount = 10;

	// 默认顺序先分配红蓝指挥官，再交替阵营填充每方两个 Ground、两个 Air 席位。
	// 测试预设可调整角色优先级；同角色的阵营选择仍沿用这个顺序。
	constexpr uint8 AssignmentOrder[SlotCount] = {0, 5, 1, 6, 2, 7, 3, 8, 4, 9};
}

AGuLiBattleGameState::AGuLiBattleGameState()
{
	InitializeDefaultRoleSlots();
}

// 复制注册只声明字段及条件；ForceNetUpdate 促使尽早更新，不保证本帧到达所有客户端。
void AGuLiBattleGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiBattleGameState, ProtocolVersion);
	DOREPLIFETIME(AGuLiBattleGameState, MatchEpoch);
	DOREPLIFETIME(AGuLiBattleGameState, MatchId);
	DOREPLIFETIME(AGuLiBattleGameState, RoleSlots);
}

void AGuLiBattleGameState::InitializeServerMatchState()
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

	if (RoleSlots.Num() != GuLiBattleRoleSlots::SlotCount)
	{
		InitializeDefaultRoleSlots();
	}

	ForceNetUpdate();
}

bool AGuLiBattleGameState::ClaimRoleSlot(
	const FGuid& PlayerGuid,
	uint8 PreferredSlotIndex,
	uint8& OutSlotIndex,
	EGuLiTeam& OutTeam,
	EGuLiCommanderRole& OutRole)
{
	return ClaimRoleSlotWithPriority(
		PlayerGuid, PreferredSlotIndex, TArray<EGuLiCommanderRole>(), OutSlotIndex, OutTeam, OutRole);
}

bool AGuLiBattleGameState::ClaimRoleSlotWithPriority(
	const FGuid& PlayerGuid,
	uint8 PreferredSlotIndex,
	const TArray<EGuLiCommanderRole>& InitialRolePriority,
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

	// 重连恢复后可能再次申请；同一 GUID 直接返回已占席位，不重复占席。
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

	// 优先级只改变服务器新玩家的候选顺序；重复或非法角色不会产生额外席位。
	for (const EGuLiCommanderRole PreferredRole : InitialRolePriority)
	{
		for (const uint8 SlotIndex : GuLiBattleRoleSlots::AssignmentOrder)
		{
			if (RoleSlots.IsValidIndex(SlotIndex) && RoleSlots[SlotIndex].Role == PreferredRole
				&& TryClaimRoleSlot(SlotIndex, PlayerGuid, OutSlotIndex, OutTeam, OutRole))
			{
				return true;
			}
		}
	}

	for (const uint8 SlotIndex : GuLiBattleRoleSlots::AssignmentOrder)
	{
		if (TryClaimRoleSlot(SlotIndex, PlayerGuid, OutSlotIndex, OutTeam, OutRole))
		{
			return true;
		}
	}

	return false;
}

void AGuLiBattleGameState::ReleaseRoleSlot(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid)
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
	Slot.bBattleReady = false;
	Slot.bSyncReady = false;
	NotifyRoleSlotsChanged();
}

void AGuLiBattleGameState::SetRoleSlotBattleReady(
	uint8 SlotIndex,
	const FGuid& ExpectedPlayerGuid,
	bool bReady)
{
	if (!HasAuthority() || !RoleSlots.IsValidIndex(SlotIndex))
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

void AGuLiBattleGameState::InitializeDefaultRoleSlots()
{
	RoleSlots.Reset(GuLiBattleRoleSlots::SlotCount);

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

// 服务器赋值后主动通知本地订阅者；客户端则从 OnRep_RoleSlots 进入同一类本地通知。
void AGuLiBattleGameState::NotifyRoleSlotsChanged()
{
	OnRoleSlotsChanged.Broadcast();
	ForceNetUpdate();
}

void AGuLiBattleGameState::OnRep_RoleSlots()
{
	OnRoleSlotsChanged.Broadcast();
}

