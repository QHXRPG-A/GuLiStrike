// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattlePlayerState.h"

#include "Net/UnrealNetwork.h"

// 身份、分配结果与就绪位走属性复制；C++ 服务器 setter 另行广播本地通知。
void AGuLiBattlePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiBattlePlayerState, PlayerGuid);
	DOREPLIFETIME(AGuLiBattlePlayerState, Team);
	DOREPLIFETIME(AGuLiBattlePlayerState, CommanderRole);
	DOREPLIFETIME(AGuLiBattlePlayerState, SlotIndex);
	DOREPLIFETIME(AGuLiBattlePlayerState, bBattleReady);
	DOREPLIFETIME(AGuLiBattlePlayerState, bSyncReady);
}

void AGuLiBattlePlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (AGuLiBattlePlayerState* NewPlayerState = Cast<AGuLiBattlePlayerState>(PlayerState))
	{
		NewPlayerState->PlayerGuid = PlayerGuid;
		NewPlayerState->Team = Team;
		NewPlayerState->CommanderRole = CommanderRole;
		NewPlayerState->SlotIndex = SlotIndex;
		// 身份跨通用/指挥官派生类迁移，公共与士兵流握手都必须重新完成。
		NewPlayerState->bBattleReady = false;
		NewPlayerState->bSyncReady = false;
	}
}

// FindInactivePlayer 会在恢复的 PS 上调用本函数，参数可能是本次登录刚创建、尚无自定义身份的 PS。
// 不能用这个空身份覆盖已经找回的 GUID/席位；无论是否复制身份，两种就绪资格都必须重新确认。
void AGuLiBattlePlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);

	if (const AGuLiBattlePlayerState* IncomingPlayerState = Cast<AGuLiBattlePlayerState>(PlayerState))
	{
		if (IncomingPlayerState->PlayerGuid.IsValid())
		{
			PlayerGuid = IncomingPlayerState->PlayerGuid;
			Team = IncomingPlayerState->Team;
			CommanderRole = IncomingPlayerState->CommanderRole;
			SlotIndex = IncomingPlayerState->SlotIndex;
		}
	}
	bBattleReady = false;
	bSyncReady = false;
	NotifyStateChanged();
}

void AGuLiBattlePlayerState::EnsureServerPlayerGuid()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!PlayerGuid.IsValid())
	{
		PlayerGuid = FGuid::NewGuid();
		NotifyStateChanged();
		ForceNetUpdate();
	}
}

void AGuLiBattlePlayerState::SetServerRoleAssignment(
	EGuLiTeam NewTeam,
	EGuLiCommanderRole NewRole,
	uint8 NewSlotIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	Team = NewTeam;
	CommanderRole = NewRole;
	SlotIndex = NewSlotIndex;
	bBattleReady = false;
	bSyncReady = false;
	NotifyStateChanged();
	ForceNetUpdate();
}

// 公共握手仅开放通用玩法资格；此函数故意不写 bSyncReady。
void AGuLiBattlePlayerState::SetServerBattleReady(bool bNewBattleReady)
{
	if (!HasAuthority() || bBattleReady == bNewBattleReady)
	{
		return;
	}

	bBattleReady = bNewBattleReady;
	NotifyStateChanged();
	ForceNetUpdate();
}

void AGuLiBattlePlayerState::SetServerSoldierStreamReady(bool bNewSyncReady)
{
	if (!HasAuthority() || bSyncReady == bNewSyncReady)
	{
		return;
	}

	bSyncReady = bNewSyncReady;
	NotifyStateChanged();
	ForceNetUpdate();
}

void AGuLiBattlePlayerState::SetServerObserver()
{
	SetServerRoleAssignment(EGuLiTeam::Unassigned, EGuLiCommanderRole::Observer, InvalidSlotIndex);
}

void AGuLiBattlePlayerState::NotifyStateChanged()
{
	OnCommanderPlayerStateChanged.Broadcast();
}

// 客户端副本更新后的本地消费入口；不能在这里向服务器反向提交权威分配。
void AGuLiBattlePlayerState::OnRep_Assignment()
{
	NotifyStateChanged();
}

void AGuLiBattlePlayerState::OnRep_BattleReady()
{
	NotifyStateChanged();
}

void AGuLiBattlePlayerState::OnRep_SyncReady()
{
	NotifyStateChanged();
}
