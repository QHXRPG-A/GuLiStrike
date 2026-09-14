// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattleGameState.h"

#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiMissileVisualSubsystem.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace GuLiBattleRoleSlots
{
	constexpr uint8 SlotCount = 10;
	constexpr float WingmanPublicPosePublishIntervalSeconds = 0.1f;

	// 默认顺序先分配红蓝指挥官，再交替阵营填充每方两个 Ground、两个 Air 席位。
	// 测试预设可调整角色优先级；同角色的阵营选择仍沿用这个顺序。
	constexpr uint8 AssignmentOrder[SlotCount] = {0, 5, 1, 6, 2, 7, 3, 8, 4, 9};
}

AGuLiBattleGameState::AGuLiBattleGameState()
	: WingmanRelayAuthorityRegistry(MakeUnique<FGuLiWingmanRelayAuthorityRegistry>())
{
	InitializeDefaultRoleSlots();
	CreateDefaultSubobject<UGuLiCombatEffectReplicationComponent>(TEXT("CombatEffectReplication"));
}

AGuLiBattleGameState::~AGuLiBattleGameState() = default;

void AGuLiBattleGameState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && GetWorld())
	{
		GetWorldTimerManager().SetTimer(
			WingmanPublicPosePublishTimer,
			this,
			&AGuLiBattleGameState::FlushPublicWingmanAcceptedBatches,
			GuLiBattleRoleSlots::WingmanPublicPosePublishIntervalSeconds,
			true,
			GuLiBattleRoleSlots::WingmanPublicPosePublishIntervalSeconds);
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
		GetWorldTimerManager().ClearTimer(WingmanPublicPosePublishTimer);
	}
	LatestPublicWingmanAcceptedBatches.Reset();
	if (UGuLiLogicalMissileSubsystem* Missiles = BoundLogicalMissiles.Get())
	{
		Missiles->OnLaunch.RemoveAll(this);
		Missiles->OnCorrection.RemoveAll(this);
		Missiles->OnFinished.RemoveAll(this);
	}
	BoundLogicalMissiles.Reset();
	Super::EndPlay(EndPlayReason);
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
	// All public combat domains share this epoch, including a Commander-only match.
	// Do not rely on a Wingman/Ship projectile being launched to initialize the ledger.
	if (GetWorld())
	{
		if (auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()) Ledger->BeginServerEpoch(MatchEpoch);
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
	LatestPublicWingmanAcceptedBatches.RemoveAll(
		[&Group](const FGuLiWingmanAcceptedBatch& Entry)
		{
			return Entry.Group == Group;
		});
	ForceNetUpdate();
	MulticastRevokeWingmanGroup(Group);
}

void AGuLiBattleGameState::ServerPublishWingmanExternalControl(const FGuLiWingmanGroupHandle& Group,
	bool bPhased, bool bLocked, const TArray<FGuLiWingmanAcceptedBatch>& Baselines)
{
	if (!HasAuthority()) { return; }
	auto* State = PublicWingmanBootstraps.FindByPredicate([&](const auto& Entry) { return Entry.Group == Group; });
	if (!State) { return; }
	State->bPhased = bPhased; State->bExternalActionsLocked = bLocked;
	State->ExternalDisplacementBaselines = Baselines;
	State->PublicationRevision = NextWingmanPublicationRevision++;
	if (!NextWingmanPublicationRevision) { ++NextWingmanPublicationRevision; }
	ForceNetUpdate(); MulticastReceiveWingmanBootstrap(*State);
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
	// Relay validation has already accepted the authoritative endpoint. Coalesce each
	// Flight to one latest value for the next 10 Hz public frame.
	const int32 ExistingIndex = LatestPublicWingmanAcceptedBatches.IndexOfByPredicate(
		[&AcceptedBatch](const FGuLiWingmanAcceptedBatch& Entry)
		{
			return Entry.Group == AcceptedBatch.Group
				&& Entry.FlightIndex == AcceptedBatch.FlightIndex;
		});
	if (ExistingIndex == INDEX_NONE)
	{
		LatestPublicWingmanAcceptedBatches.Add(AcceptedBatch);
	}
	else
	{
		LatestPublicWingmanAcceptedBatches[ExistingIndex] = AcceptedBatch;
	}
}

void AGuLiBattleGameState::ServerPublishWingmanAcceptedAtomicBatch(
	const TArray<FGuLiWingmanAcceptedBatch>& AcceptedFlights)
{
	if (!HasAuthority() || AcceptedFlights.IsEmpty())
	{
		return;
	}
	const FGuLiWingmanGroupHandle Group = AcceptedFlights[0].Group;
	const bool bHasPublicGroup = Group.IsValid()
		&& PublicWingmanBootstraps.ContainsByPredicate(
			[&Group](const FGuLiWingmanPublicBootstrapState& Entry)
			{
				return Entry.Group == Group && Entry.IsWellFormed();
			});
	uint8 SeenFlightMask = 0u;
	for (const FGuLiWingmanAcceptedBatch& Accepted : AcceptedFlights)
	{
		if (!bHasPublicGroup || !Accepted.IsWellFormed() || Accepted.Group != Group
			|| Accepted.FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
			|| (SeenFlightMask & (1u << Accepted.FlightIndex)) != 0u)
		{
			return;
		}
		SeenFlightMask |= static_cast<uint8>(1u << Accepted.FlightIndex);
	}
	const uint8 AllFlightsMask = static_cast<uint8>((1u << GULI_WINGMAN_FLIGHT_COUNT) - 1u);
	if (SeenFlightMask != AllFlightsMask)
	{
		return;
	}
	for (const FGuLiWingmanAcceptedBatch& Accepted : AcceptedFlights)
	{
		ServerPublishWingmanAcceptedBatch(Accepted);
	}
}

void AGuLiBattleGameState::FlushPublicWingmanAcceptedBatches()
{
	if (!HasAuthority())
	{
		return;
	}
	// Retain the latest endpoints until replaced and publish one compact 10 Hz
	// frame per viewer, ordered with the reliable lifecycle cuts.
	const TArray<FGuLiWingmanAcceptedBatch>& Published = LatestPublicWingmanAcceptedBatches;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AGuLiBattlePlayerController* Controller = CastChecked<AGuLiBattlePlayerController>(It->Get());
		Controller->GetWingmanRelayComponent()->SendPublicWingmanAcceptedBatches(Published);
	}
}

void AGuLiBattleGameState::ReceivePublicWingmanAcceptedBatches(
	const TArray<FGuLiWingmanAcceptedBatch>& AcceptedBatches)
{
	for (const FGuLiWingmanAcceptedBatch& AcceptedBatch : AcceptedBatches)
	{
		HandlePublicWingmanAcceptedBatch(AcceptedBatch);
	}
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
	Event.RootEventId = Missile.RootEventId;
	Event.WeaponBinding = Missile.WeaponBinding;
	Event.SkillId = Missile.SkillId;
	Event.ProfileRevision = Missile.ProfileRevision;
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
	Event.RootEventId = Terminal.RootEventId;
	Event.WeaponBinding = Terminal.WeaponBinding;
	Event.SkillId = Terminal.SkillId;
	Event.ProfileRevision = Terminal.ProfileRevision;
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
			Presentation->SetGroupExternalControlState(PublicState.Group,PublicState.bPhased,PublicState.bExternalActionsLocked);
			for (const auto& Batch : PublicState.ExternalDisplacementBaselines)
			{ Presentation->ApplyAcceptedSnapshot(Batch,GetWorld()->GetTimeSeconds()); }
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
	if (LocalPlayerGuid.IsValid() && !Bootstrap.AuthorityMap.IsEmpty()
		&& Bootstrap.AuthorityMap[0].LeaseOwnerPlayerGuid == LocalPlayerGuid)
	{
		return true;
	}
	if (!GetWorld())
	{
		return false;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const UGuLiWingmanRelayComponent* Relay = Controller && Controller->IsLocalController()
			? Controller->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
		if (Relay && Relay->IsLocalLeaseOwner(Bootstrap.Commit.Group))
		{
			return true;
		}
	}
	return false;
}

