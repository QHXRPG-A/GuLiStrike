// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattleGameState.h"

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "Battle/Combat/GuLiMissileVisualSubsystem.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace GuLiBattleRoleSlots
{
	constexpr uint8 SlotCount = 10;

	// 默认顺序先分配红蓝指挥官，再交替阵营填充每方两个 Ground、两个 Air 席位。
	// 测试预设可调整角色优先级；同角色的阵营选择仍沿用这个顺序。
	constexpr uint8 AssignmentOrder[SlotCount] = {0, 5, 1, 6, 2, 7, 3, 8, 4, 9};
}

AGuLiBattleGameState::AGuLiBattleGameState()
	: WingmanRelayAuthorityRegistry(MakeUnique<FGuLiWingmanRelayAuthorityRegistry>())
{
	InitializeDefaultRoleSlots();
}

AGuLiBattleGameState::~AGuLiBattleGameState() = default;

void AGuLiBattleGameState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && GetWorld())
	{
		GetWorldTimerManager().SetTimer(
			WingmanLeaseMaintenanceTimer,
			this,
			&AGuLiBattleGameState::RunWingmanLeaseMaintenance,
			1.0f,
			true,
			1.0f);
		if (UGuLiLogicalMissileSubsystem* Missiles =
			GetWorld()->GetSubsystem<UGuLiLogicalMissileSubsystem>())
		{
			BoundLogicalMissiles = Missiles;
			Missiles->OnLaunch.AddUObject(this, &AGuLiBattleGameState::HandleLogicalMissileLaunch);
			Missiles->OnCorrection.AddUObject(this, &AGuLiBattleGameState::HandleLogicalMissileCorrection);
			Missiles->OnFinished.AddUObject(this, &AGuLiBattleGameState::HandleLogicalMissileTerminal);
		}
	}
	if (GetWorld() && MatchEpoch != 0u)
	{
		if (UGuLiMissileVisualSubsystem* Visuals =
			GetWorld()->GetSubsystem<UGuLiMissileVisualSubsystem>())
		{
			Visuals->BeginEpoch(MatchEpoch);
		}
	}
}

void AGuLiBattleGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(WingmanLeaseMaintenanceTimer);
	}
	if (UGuLiLogicalMissileSubsystem* Missiles = BoundLogicalMissiles.Get())
	{
		Missiles->OnLaunch.RemoveAll(this);
		Missiles->OnCorrection.RemoveAll(this);
		Missiles->OnFinished.RemoveAll(this);
	}
	BoundLogicalMissiles.Reset();
	Super::EndPlay(EndPlayReason);
}

void AGuLiBattleGameState::RunWingmanLeaseMaintenance()
{
	if (!HasAuthority() || !GetWorld() || !WingmanRelayAuthorityRegistry)
	{
		return;
	}
	const TArray<FGuLiWingmanOwnerLossAssignment> NewOffers =
		WingmanRelayAuthorityRegistry->RunLeaseMaintenance(GetWorld()->GetTimeSeconds());
	for (const FGuLiWingmanOwnerLossAssignment& Assignment : NewOffers)
	{
		if (Assignment.Disposition != EGuLiWingmanOwnerLossDisposition::OfferStarted)
		{
			continue;
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			const AGuLiBattlePlayerState* PlayerState = Controller
				? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
			if (!PlayerState || PlayerState->GetPlayerGuid() != Assignment.NewOwnerPlayerGuid)
			{
				continue;
			}
			if (UGuLiWingmanRelayComponent* Transport =
				Controller->FindComponentByClass<UGuLiWingmanRelayComponent>())
			{
				Transport->ServerDeliverLeaseOffer(Assignment.Group);
			}
			break;
		}
	}
}

FGuLiWingmanRelayAuthorityRegistry* AGuLiBattleGameState::GetWingmanRelayAuthorityRegistry()
{
	return HasAuthority() ? WingmanRelayAuthorityRegistry.Get() : nullptr;
}

const FGuLiWingmanRelayAuthorityRegistry* AGuLiBattleGameState::GetWingmanRelayAuthorityRegistry() const
{
	return HasAuthority() ? WingmanRelayAuthorityRegistry.Get() : nullptr;
}

// 复制注册只声明字段及条件；ForceNetUpdate 促使尽早更新，不保证本帧到达所有客户端。
void AGuLiBattleGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGuLiBattleGameState, ProtocolVersion);
	DOREPLIFETIME(AGuLiBattleGameState, MatchEpoch);
	DOREPLIFETIME(AGuLiBattleGameState, MatchId);
	DOREPLIFETIME(AGuLiBattleGameState, RoleSlots);
	DOREPLIFETIME(AGuLiBattleGameState, PublicWingmanBootstraps);
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
		OnRep_MatchEpoch();
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

bool AGuLiBattleGameState::ServerPublishWingmanBootstrap(
	const FGuLiWingmanBootstrapBundle& Bootstrap,
	const EGuLiWingmanGroupLifecycle Lifecycle)
{
	if (!HasAuthority() || !Bootstrap.IsWellFormed()
		|| Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable
		|| Lifecycle == EGuLiWingmanGroupLifecycle::Revoked)
	{
		return false;
	}

	FGuLiWingmanPublicBootstrapState* Existing = PublicWingmanBootstraps.FindByPredicate(
		[&Bootstrap](const FGuLiWingmanPublicBootstrapState& Entry)
		{
			return Entry.Group == Bootstrap.Commit.Group;
		});
	if (Existing && Existing->Bootstrap.Commit.CutId == Bootstrap.Commit.CutId
		&& Existing->Bootstrap.AbilityConfig.SnapshotRevision
			== Bootstrap.AbilityConfig.SnapshotRevision
		&& Existing->Bootstrap.AbilityConfig.SnapshotHash == Bootstrap.AbilityConfig.SnapshotHash
		&& Existing->Lifecycle == Lifecycle)
	{
		return true;
	}

	FGuLiWingmanPublicBootstrapState PublicState;
	PublicState.Group = Bootstrap.Commit.Group;
	PublicState.Bootstrap = Bootstrap;
	PublicState.Lifecycle = Lifecycle;
	PublicState.PublicationRevision = NextWingmanPublicationRevision++;
	if (NextWingmanPublicationRevision == 0u)
	{
		NextWingmanPublicationRevision = 1u;
	}

	if (Existing)
	{
		*Existing = PublicState;
	}
	else
	{
		PublicWingmanBootstraps.Add(PublicState);
	}
	ForceNetUpdate();
	MulticastReceiveWingmanBootstrap(PublicState);
	return true;
}

void AGuLiBattleGameState::ServerRevokeWingmanGroup(const FGuLiWingmanGroupHandle& Group)
{
	if (!HasAuthority() || !Group.IsValid())
	{
		return;
	}
	PublicWingmanBootstraps.RemoveAll([&Group](const FGuLiWingmanPublicBootstrapState& Entry)
	{
		return Entry.Group == Group;
	});
	LastPublicWingmanAcceptedPublishTimes.Remove(Group);
	ForceNetUpdate();
	MulticastRevokeWingmanGroup(Group);
}

void AGuLiBattleGameState::ServerPublishWingmanAcceptedBatch(
	const FGuLiWingmanAcceptedBatch& AcceptedBatch)
{
	if (!HasAuthority() || !AcceptedBatch.IsWellFormed()
		|| !PublicWingmanBootstraps.ContainsByPredicate(
			[&AcceptedBatch](const FGuLiWingmanPublicBootstrapState& Entry)
			{
				return Entry.Group == AcceptedBatch.Group && Entry.IsWellFormed();
			}))
	{
		return;
	}
	// Owner candidates are produced at 10 Hz. Retain that hard public-stream
	// ceiling even if deferred validation results arrive in a short burst.
	constexpr double MinimumAcceptedPublishIntervalSeconds = 0.1;
	const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (const double* LastTime = LastPublicWingmanAcceptedPublishTimes.Find(AcceptedBatch.Group))
	{
		if (NowSeconds < *LastTime
			|| NowSeconds - *LastTime < MinimumAcceptedPublishIntervalSeconds)
		{
			return;
		}
	}
	LastPublicWingmanAcceptedPublishTimes.Add(AcceptedBatch.Group, NowSeconds);
	MulticastReceiveWingmanAcceptedBatch(AcceptedBatch);
}

void AGuLiBattleGameState::OnRep_PublicWingmanBootstraps()
{
	ApplyRetainedWingmanBootstraps();
}

void AGuLiBattleGameState::MulticastReceiveWingmanBootstrap_Implementation(
	const FGuLiWingmanPublicBootstrapState& PublicState)
{
	HandlePublicWingmanBootstrap(PublicState);
}

void AGuLiBattleGameState::MulticastRevokeWingmanGroup_Implementation(
	const FGuLiWingmanGroupHandle& Group)
{
	HandlePublicWingmanRevocation(Group);
}

void AGuLiBattleGameState::MulticastReceiveWingmanAcceptedBatch_Implementation(
	const FGuLiWingmanAcceptedBatch& AcceptedBatch)
{
	HandlePublicWingmanAcceptedBatch(AcceptedBatch);
}

void AGuLiBattleGameState::MulticastReceiveMissileLaunch_Implementation(
	const FGuLiMissileVisualLaunchDTO& Event)
{
	ApplyMissileVisualLaunch(Event);
}

void AGuLiBattleGameState::MulticastReceiveMissileCorrection_Implementation(
	const FGuLiMissileVisualCorrectionDTO& Event)
{
	ApplyMissileVisualCorrection(Event);
}

void AGuLiBattleGameState::MulticastReceiveMissileTerminal_Implementation(
	const FGuLiMissileVisualTerminalDTO& Event)
{
	ApplyMissileVisualTerminal(Event);
}

void AGuLiBattleGameState::HandleLogicalMissileLaunch(
	const FGuLiLogicalMissileState& Missile)
{
	if (!HasAuthority() || Missile.MatchEpoch != MatchEpoch || !GetWorld())
	{
		return;
	}
	FGuLiMissileVisualLaunchDTO Event;
	Event.MatchEpoch = Missile.MatchEpoch;
	Event.MissileId = Missile.MissileId;
	Event.Emitter = Missile.Emitter;
	Event.Target = Missile.Target;
	Event.Position = Missile.Position;
	Event.Velocity = Missile.Velocity;
	Event.ServerWorldTimeSeconds = GetWorld()->GetTimeSeconds();
	if (Event.IsWellFormed())
	{
		MulticastReceiveMissileLaunch(Event);
	}
}

void AGuLiBattleGameState::HandleLogicalMissileCorrection(
	const FGuLiLogicalMissileState& Missile)
{
	if (!HasAuthority() || Missile.MatchEpoch != MatchEpoch || !GetWorld())
	{
		return;
	}
	FGuLiMissileVisualCorrectionDTO Event;
	Event.MatchEpoch = Missile.MatchEpoch;
	Event.MissileId = Missile.MissileId;
	Event.SimulationSequence = Missile.SimulationSequence;
	Event.Position = Missile.Position;
	Event.Velocity = Missile.Velocity;
	Event.ServerWorldTimeSeconds = GetWorld()->GetTimeSeconds();
	if (Event.IsWellFormed())
	{
		MulticastReceiveMissileCorrection(Event);
	}
}

void AGuLiBattleGameState::HandleLogicalMissileTerminal(
	const FGuLiLogicalMissileTerminalEvent& Terminal)
{
	if (!HasAuthority() || !GetWorld()
		|| (Terminal.MatchEpoch != MatchEpoch
			&& Terminal.Reason != EGuLiLogicalMissileTerminalReason::MatchEpochEnded))
	{
		return;
	}
	FGuLiMissileVisualTerminalDTO Event;
	Event.MatchEpoch = Terminal.MatchEpoch;
	Event.MissileId = Terminal.MissileId;
	Event.SimulationSequence = Terminal.SimulationSequence;
	Event.Reason = Terminal.Reason;
	Event.Location = Terminal.Location;
	Event.ServerWorldTimeSeconds = GetWorld()->GetTimeSeconds();
	if (Event.IsWellFormed())
	{
		MulticastReceiveMissileTerminal(Event);
	}
}

void AGuLiBattleGameState::ApplyMissileVisualLaunch(
	const FGuLiMissileVisualLaunchDTO& Event)
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer
		|| (MatchEpoch != 0u && Event.MatchEpoch != MatchEpoch))
	{
		return;
	}
	if (UGuLiMissileVisualSubsystem* Visuals =
		GetWorld()->GetSubsystem<UGuLiMissileVisualSubsystem>())
	{
		Visuals->BeginEpoch(Event.MatchEpoch);
		Visuals->ApplyLaunch(Event);
	}
}

void AGuLiBattleGameState::ApplyMissileVisualCorrection(
	const FGuLiMissileVisualCorrectionDTO& Event)
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer
		|| (MatchEpoch != 0u && Event.MatchEpoch != MatchEpoch))
	{
		return;
	}
	if (UGuLiMissileVisualSubsystem* Visuals =
		GetWorld()->GetSubsystem<UGuLiMissileVisualSubsystem>())
	{
		if (Visuals->GetVisualMatchEpoch() == Event.MatchEpoch)
		{
			Visuals->ApplyCorrection(Event);
		}
	}
}

void AGuLiBattleGameState::ApplyMissileVisualTerminal(
	const FGuLiMissileVisualTerminalDTO& Event)
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UGuLiMissileVisualSubsystem* Visuals =
		GetWorld()->GetSubsystem<UGuLiMissileVisualSubsystem>())
	{
		if (Visuals->GetVisualMatchEpoch() == Event.MatchEpoch)
		{
			Visuals->ApplyTerminal(Event);
		}
	}
}

void AGuLiBattleGameState::ApplyRetainedWingmanBootstraps()
{
	TSet<FGuLiWingmanGroupHandle> RetainedGroups;
	for (const FGuLiWingmanPublicBootstrapState& PublicState : PublicWingmanBootstraps)
	{
		if (PublicState.IsWellFormed())
		{
			RetainedGroups.Add(PublicState.Group);
			HandlePublicWingmanBootstrap(PublicState);
		}
	}

	TArray<FGuLiWingmanGroupHandle> RemovedGroups;
	for (const FGuLiWingmanGroupHandle& AppliedGroup : AppliedPublicWingmanGroups)
	{
		if (!RetainedGroups.Contains(AppliedGroup))
		{
			RemovedGroups.Add(AppliedGroup);
		}
	}
	for (const FGuLiWingmanGroupHandle& RemovedGroup : RemovedGroups)
	{
		HandlePublicWingmanRevocation(RemovedGroup);
	}
}

void AGuLiBattleGameState::HandlePublicWingmanBootstrap(
	const FGuLiWingmanPublicBootstrapState& PublicState)
{
	if (!PublicState.IsWellFormed() || !GetWorld())
	{
		return;
	}
	ClientWingmanBootstrapCache.Add(PublicState.Group, PublicState.Bootstrap);
	if (AGuLiWingmanPresentationActor* Presentation =
		AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld()))
	{
		if (Presentation->ApplyBootstrap(
			PublicState.Bootstrap,
			IsLocalWingmanLeaseOwner(PublicState.Bootstrap),
			GetWorld()->GetTimeSeconds()))
		{
			AppliedPublicWingmanGroups.Add(PublicState.Group);
		}
	}
}

void AGuLiBattleGameState::HandlePublicWingmanRevocation(
	const FGuLiWingmanGroupHandle& Group)
{
	ClientWingmanBootstrapCache.Remove(Group);
	AppliedPublicWingmanGroups.Remove(Group);
	if (GetWorld())
	{
		if (AGuLiWingmanPresentationActor* Presentation =
			AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld()))
		{
			Presentation->RemoveGroup(Group);
		}
	}
}

void AGuLiBattleGameState::HandlePublicWingmanAcceptedBatch(
	const FGuLiWingmanAcceptedBatch& AcceptedBatch)
{
	if (!AcceptedBatch.IsWellFormed() || !GetWorld())
	{
		return;
	}
	const FGuLiWingmanBootstrapBundle* Bootstrap =
		ClientWingmanBootstrapCache.Find(AcceptedBatch.Group);
	if (!Bootstrap || IsLocalWingmanLeaseOwner(*Bootstrap))
	{
		// The Lease Owner renders its current local Candidate. Accepted poses are
		// strictly a remote-viewer input and are never fed back to authority.
		return;
	}
	if (AGuLiWingmanPresentationActor* Presentation =
		AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld()))
	{
		Presentation->ApplyAcceptedSnapshot(
			AcceptedBatch,
			GetWorld()->GetTimeSeconds());
	}
}

FGuid AGuLiBattleGameState::GetLocalWingmanViewerPlayerGuid() const
{
	if (!GetWorld())
	{
		return FGuid{};
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const AGuLiBattlePlayerState* PlayerState = Controller && Controller->IsLocalController()
			? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
		if (PlayerState && PlayerState->GetPlayerGuid().IsValid())
		{
			return PlayerState->GetPlayerGuid();
		}
	}
	return FGuid{};
}

bool AGuLiBattleGameState::IsLocalWingmanLeaseOwner(
	const FGuLiWingmanBootstrapBundle& Bootstrap) const
{
	const FGuid LocalPlayerGuid = GetLocalWingmanViewerPlayerGuid();
	return LocalPlayerGuid.IsValid() && !Bootstrap.AuthorityMap.IsEmpty()
		&& Bootstrap.AuthorityMap[0].LeaseOwnerPlayerGuid == LocalPlayerGuid;
}

