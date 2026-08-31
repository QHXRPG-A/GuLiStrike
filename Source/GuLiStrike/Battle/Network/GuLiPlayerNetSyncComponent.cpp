// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Network/GuLiPlayerNetSyncComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

bool FGuLiNetworkRequestWindow::Consume(
	const double NowSeconds,
	const int32 MaximumRequests,
	const double WindowSeconds)
{
	if (!FMath::IsFinite(NowSeconds) || !FMath::IsFinite(WindowSeconds)
		|| MaximumRequests <= 0 || WindowSeconds <= 0.0)
	{
		return false;
	}
	RequestTimes.RemoveAll([NowSeconds, WindowSeconds](const double RequestTime)
	{
		return NowSeconds < RequestTime || NowSeconds - RequestTime >= WindowSeconds;
	});
	if (RequestTimes.Num() >= MaximumRequests)
	{
		return false;
	}
	RequestTimes.Add(NowSeconds);
	return true;
}

bool GuLiConnectionBootstrap::IsValidIdentity(const FGuLiConnectionBootstrapState& State)
{
	if (State.ProtocolVersion != GULI_BATTLE_PROTOCOL_VERSION
		|| State.MatchEpoch == 0u || !State.PlayerGuid.IsValid())
	{
		return false;
	}
	if (State.Role == EGuLiCommanderRole::Observer)
	{
		return State.Team == EGuLiTeam::Unassigned && State.SlotIndex == MAX_uint8;
	}
	const bool bGameplayRole = State.Role == EGuLiCommanderRole::Commander
		|| State.Role == EGuLiCommanderRole::Ground || State.Role == EGuLiCommanderRole::Air;
	return bGameplayRole && (State.Team == EGuLiTeam::Red || State.Team == EGuLiTeam::Blue)
		&& State.SlotIndex != MAX_uint8;
}

bool GuLiConnectionBootstrap::HasSameIdentity(
	const FGuLiConnectionBootstrapState& Lhs,
	const FGuLiConnectionBootstrapState& Rhs)
{
	return Lhs.ProtocolVersion == Rhs.ProtocolVersion && Lhs.MatchEpoch == Rhs.MatchEpoch
		&& Lhs.PlayerGuid == Rhs.PlayerGuid && Lhs.Team == Rhs.Team
		&& Lhs.Role == Rhs.Role && Lhs.SlotIndex == Rhs.SlotIndex;
}

UGuLiPlayerNetSyncComponent::UGuLiPlayerNetSyncComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.0f;
}

void UGuLiPlayerNetSyncComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGuLiPlayerNetSyncComponent, ConnectionBootstrap, COND_OwnerOnly);
}

void UGuLiPlayerNetSyncComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	EnsureServerConnectionBootstrap();
	UpdateClientConnectionBootstrap();
}

APlayerController* UGuLiPlayerNetSyncComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}

AGuLiBattlePlayerState* UGuLiPlayerNetSyncComponent::GetBattlePlayerState() const
{
	const APlayerController* Controller = GetOwningPlayerController();
	return Controller ? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
}

AGuLiBattleGameState* UGuLiPlayerNetSyncComponent::GetBattleGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
}

bool UGuLiPlayerNetSyncComponent::BuildCurrentConnectionIdentity(
	FGuLiConnectionBootstrapState& OutState) const
{
	const AGuLiBattleGameState* GameState = GetBattleGameState();
	const AGuLiBattlePlayerState* PlayerState = GetBattlePlayerState();
	if (!GameState || !PlayerState)
	{
		return false;
	}
	OutState.ProtocolVersion = GameState->GetProtocolVersion();
	OutState.MatchEpoch = GameState->GetMatchEpoch();
	OutState.PlayerGuid = PlayerState->GetPlayerGuid();
	OutState.Team = PlayerState->GetTeam();
	OutState.Role = PlayerState->GetBattleRole();
	OutState.SlotIndex = PlayerState->GetBattleSlotIndex();
	return GuLiConnectionBootstrap::IsValidIdentity(OutState);
}

bool UGuLiPlayerNetSyncComponent::IsConnectionReady() const
{
	FGuLiConnectionBootstrapState CurrentIdentity;
	if (!GetOwner() || ConnectionBootstrap.Generation == 0u
		|| !ConnectionBootstrap.bServerAcknowledged
		|| !BuildCurrentConnectionIdentity(CurrentIdentity)
		|| !GuLiConnectionBootstrap::HasSameIdentity(ConnectionBootstrap, CurrentIdentity))
	{
		return false;
	}
	if (GetOwner()->HasAuthority())
	{
		const AGuLiBattlePlayerState* PlayerState = GetBattlePlayerState();
		return bServerConnectionReady && BootstrapPlayerState.Get() == PlayerState
			&& PlayerState && PlayerState->IsBattleReady();
	}
	return bClientConnectionReady;
}

void UGuLiPlayerNetSyncComponent::ResetServerConnectionBootstrap()
{
	// 先撤销两类资格；派生类随后清理自己的代次与缓存，不能保留上局士兵门。
	if (AGuLiBattlePlayerState* PlayerState = GetBattlePlayerState())
	{
		PlayerState->SetServerBattleReady(false);
		PlayerState->SetServerSoldierStreamReady(false);
	}
	MirrorBattleReadyToRoleSlot(false);
	ConnectionBootstrap = FGuLiConnectionBootstrapState{};
	BootstrapPlayerState.Reset();
	bServerConnectionReady = false;
	bClientConnectionReady = false;
	ConnectionAcknowledgementWindow.Reset();
	OnConnectionBootstrapReset();
	if (GetOwner())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void UGuLiPlayerNetSyncComponent::EnsureServerConnectionBootstrap()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	FGuLiConnectionBootstrapState CurrentIdentity;
	if (!BuildCurrentConnectionIdentity(CurrentIdentity))
	{
		if (ConnectionBootstrap.Generation != 0u || bServerConnectionReady)
		{
			ResetServerConnectionBootstrap();
		}
		return;
	}
	AGuLiBattlePlayerState* PlayerState = GetBattlePlayerState();
	const bool bIdentityChanged = ConnectionBootstrap.Generation == 0u
		|| BootstrapPlayerState.Get() != PlayerState
		|| !GuLiConnectionBootstrap::HasSameIdentity(ConnectionBootstrap, CurrentIdentity);
	const bool bReadyWasRevoked = bServerConnectionReady && !PlayerState->IsBattleReady();
	if (!bIdentityChanged && !bReadyWasRevoked)
	{
		return;
	}
	ResetServerConnectionBootstrap();
	++NextConnectionGeneration;
	if (NextConnectionGeneration == 0u)
	{
		++NextConnectionGeneration;
	}
	CurrentIdentity.Generation = NextConnectionGeneration;
	ConnectionBootstrap = CurrentIdentity;
	BootstrapPlayerState = PlayerState;
	GetOwner()->ForceNetUpdate();
}

void UGuLiPlayerNetSyncComponent::ServerAcknowledgeConnectionBootstrap_Implementation(
	const FGuLiConnectionBootstrapState& AppliedState)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld()
		|| !ConnectionAcknowledgementWindow.Consume(GetWorld()->GetRealTimeSeconds(), 4, 1.0))
	{
		return;
	}
	// ACK 可能先于下一次 Tick 抵达；先重新评估身份，保证旧角色/旧 World 的 ACK 不能开门。
	EnsureServerConnectionBootstrap();
	FGuLiConnectionBootstrapState CurrentIdentity;
	if (AppliedState.Generation == 0u || AppliedState.Generation != ConnectionBootstrap.Generation
		|| !GuLiConnectionBootstrap::IsValidIdentity(AppliedState)
		|| !BuildCurrentConnectionIdentity(CurrentIdentity)
		|| !GuLiConnectionBootstrap::HasSameIdentity(AppliedState, ConnectionBootstrap)
		|| !GuLiConnectionBootstrap::HasSameIdentity(AppliedState, CurrentIdentity))
	{
		return;
	}
	if (bServerConnectionReady)
	{
		return;
	}
	AGuLiBattlePlayerState* PlayerState = GetBattlePlayerState();
	ConnectionBootstrap.bServerAcknowledged = true;
	bServerConnectionReady = true;
	PlayerState->SetServerBattleReady(true);
	MirrorBattleReadyToRoleSlot(true);
	GetOwner()->ForceNetUpdate();
	OnConnectionBootstrapReady();
}

void UGuLiPlayerNetSyncComponent::OnRep_ConnectionBootstrap()
{
	UpdateClientConnectionBootstrap();
}

void UGuLiPlayerNetSyncComponent::UpdateClientConnectionBootstrap()
{
	APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || !Controller->IsLocalController() || !GetWorld())
	{
		return;
	}
	FGuLiConnectionBootstrapState CurrentIdentity;
	const bool bIdentityMatches = ConnectionBootstrap.Generation != 0u
		&& BuildCurrentConnectionIdentity(CurrentIdentity)
		&& GuLiConnectionBootstrap::HasSameIdentity(ConnectionBootstrap, CurrentIdentity);
	if (ClientObservedGeneration != ConnectionBootstrap.Generation
		|| (bClientConnectionReady && !bIdentityMatches))
	{
		ClientObservedGeneration = ConnectionBootstrap.Generation;
		bClientConnectionReady = false;
		NextClientAcknowledgementTime = 0.0;
		// Listen Server 已在服务器路径复位；不能再把刚完成的服务端专业同步清掉。
		if (!Controller->HasAuthority())
		{
			OnConnectionBootstrapReset();
		}
	}
	if (!bIdentityMatches)
	{
		return;
	}
	if (ConnectionBootstrap.bServerAcknowledged)
	{
		if (!bClientConnectionReady)
		{
			bClientConnectionReady = true;
			if (!Controller->HasAuthority())
			{
				OnConnectionBootstrapReady();
			}
		}
		return;
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (Now >= NextClientAcknowledgementTime)
	{
		NextClientAcknowledgementTime = Now + 1.0;
		ServerAcknowledgeConnectionBootstrap(ConnectionBootstrap);
	}
}

void UGuLiPlayerNetSyncComponent::MirrorBattleReadyToRoleSlot(const bool bReady) const
{
	const AGuLiBattlePlayerState* PlayerState = GetBattlePlayerState();
	AGuLiBattleGameState* GameState = GetBattleGameState();
	if (PlayerState && GameState && PlayerState->GetBattleSlotIndex() != MAX_uint8)
	{
		GameState->SetRoleSlotBattleReady(
			PlayerState->GetBattleSlotIndex(), PlayerState->GetPlayerGuid(), bReady);
	}
}

void UGuLiPlayerNetSyncComponent::OnConnectionBootstrapReset()
{
}

void UGuLiPlayerNetSyncComponent::OnConnectionBootstrapReady()
{
}
