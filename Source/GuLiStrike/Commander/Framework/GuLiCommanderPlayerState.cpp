// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderPlayerState.h"

#include "Net/UnrealNetwork.h"

// 身份、分配结果与就绪位走属性复制；C++ 服务器 setter 另行广播本地通知。
void AGuLiCommanderPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiCommanderPlayerState, PlayerGuid);
	DOREPLIFETIME(AGuLiCommanderPlayerState, Team);
	DOREPLIFETIME(AGuLiCommanderPlayerState, CommanderRole);
	DOREPLIFETIME(AGuLiCommanderPlayerState, SlotIndex);
	DOREPLIFETIME(AGuLiCommanderPlayerState, bSyncReady);
}

void AGuLiCommanderPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (AGuLiCommanderPlayerState* NewPlayerState = Cast<AGuLiCommanderPlayerState>(PlayerState))
	{
		NewPlayerState->PlayerGuid = PlayerGuid;
		NewPlayerState->Team = Team;
		NewPlayerState->CommanderRole = CommanderRole;
		NewPlayerState->SlotIndex = SlotIndex;
		// Every travel/reconnect starts a fresh bootstrap generation.
		NewPlayerState->bSyncReady = false;
	}
}

// 恢复旧 PlayerState 的身份，不恢复 bSyncReady；新连接/World 必须重新完成初始同步。
void AGuLiCommanderPlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);

	if (const AGuLiCommanderPlayerState* OldPlayerState = Cast<AGuLiCommanderPlayerState>(PlayerState))
	{
		PlayerGuid = OldPlayerState->PlayerGuid;
		Team = OldPlayerState->Team;
		CommanderRole = OldPlayerState->CommanderRole;
		SlotIndex = OldPlayerState->SlotIndex;
		bSyncReady = false;
		NotifyStateChanged();
	}
}

void AGuLiCommanderPlayerState::EnsureServerPlayerGuid()
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

void AGuLiCommanderPlayerState::SetServerRoleAssignment(
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
	bSyncReady = false;
	NotifyStateChanged();
	ForceNetUpdate();
}

void AGuLiCommanderPlayerState::SetServerSyncReady(bool bNewSyncReady)
{
	if (!HasAuthority() || bSyncReady == bNewSyncReady)
	{
		return;
	}

	bSyncReady = bNewSyncReady;
	NotifyStateChanged();
	ForceNetUpdate();
}

void AGuLiCommanderPlayerState::SetServerObserver()
{
	SetServerRoleAssignment(EGuLiTeam::Unassigned, EGuLiCommanderRole::Observer, InvalidSlotIndex);
}

void AGuLiCommanderPlayerState::NotifyStateChanged()
{
	OnCommanderPlayerStateChanged.Broadcast();
}

// 客户端副本更新后的本地消费入口；不能在这里向服务器反向提交权威分配。
void AGuLiCommanderPlayerState::OnRep_Assignment()
{
	NotifyStateChanged();
}

void AGuLiCommanderPlayerState::OnRep_SyncReady()
{
	NotifyStateChanged();
}
