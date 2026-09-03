// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiListenSmokeDiagnosticsSubsystem.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"
#include "Battle/Relay/GuLiWingmanRelayServer.h"
#include "Battle/Relay/GuLiWingmanWorldValidator.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "GuLiStrike.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiListenSmokeDiagnosticsSubsystem)

namespace GuLiListenSmokeDiagnostics
{
	const TCHAR* BoolJson(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	const TCHAR* NetModeName(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	FString JsonSafeToken(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("_"));
		Value.ReplaceInline(TEXT("\""), TEXT("_"));
		Value.ReplaceInline(TEXT("\r"), TEXT("_"));
		Value.ReplaceInline(TEXT("\n"), TEXT("_"));
		return Value.Left(96);
	}
}

bool UGuLiListenSmokeDiagnosticsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& World && World->IsGameWorld()
		&& FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke"));
#endif
}

void UGuLiListenSmokeDiagnosticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StartedWallSeconds = FPlatformTime::Seconds();
	NextSampleWallSeconds = StartedWallSeconds;

	FParse::Value(FCommandLine::Get(), TEXT("-GuLiListenSmokeRole="), RequestedRole);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiListenSmokeRunId="), RunId);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiListenSmokeSeconds="), DurationSeconds);
	DurationSeconds = FMath::Clamp(DurationSeconds, 10.0, 300.0);
	RequestedRole = GuLiListenSmokeDiagnostics::JsonSafeToken(RequestedRole);
	RunId = GuLiListenSmokeDiagnostics::JsonSafeToken(RunId);

	UE_LOG(LogGuLiStrike, Display,
		TEXT("[GULI_LISTEN_SMOKE] {\"schema\":\"guli.listen-smoke.v1\",\"phase\":\"start\",\"run_id\":\"%s\",\"requested_role\":\"%s\",\"duration_seconds\":%.3f,\"read_only\":true,\"sample_hz\":1}"),
		*RunId, *RequestedRole, DurationSeconds);
}

void UGuLiListenSmokeDiagnosticsSubsystem::Deinitialize()
{
	if (!bFinalSampleEmitted)
	{
		EmitSample(true);
	}
	Super::Deinitialize();
}

void UGuLiListenSmokeDiagnosticsSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	const double NowWallSeconds = FPlatformTime::Seconds();
	if (NowWallSeconds >= NextSampleWallSeconds)
	{
		EmitSample(false);
		NextSampleWallSeconds = NowWallSeconds + 1.0;
	}

	if (!bFinalSampleEmitted && NowWallSeconds - StartedWallSeconds >= DurationSeconds)
	{
		EmitSample(true);
		bFinalSampleEmitted = true;
		FGenericPlatformMisc::RequestExit(false);
	}
}

TStatId UGuLiListenSmokeDiagnosticsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiListenSmokeDiagnosticsSubsystem, STATGROUP_Tickables);
}

void UGuLiListenSmokeDiagnosticsSubsystem::EmitSample(const bool bFinalSample)
{
	using namespace GuLiListenSmokeDiagnostics;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const ENetMode NetMode = World->GetNetMode();
	UNetDriver* NetDriver = World->GetNetDriver();
	int32 OpenClientConnections = 0;
	FString FirstOpenClientRemoteAddress;
	const int32 ClientConnectionCount = NetDriver ? NetDriver->ClientConnections.Num() : 0;
	if (NetDriver)
	{
		for (UNetConnection* Connection : NetDriver->ClientConnections)
		{
			const bool bOpen = Connection && Connection->GetConnectionState() == USOCK_Open;
			OpenClientConnections += bOpen ? 1 : 0;
			if (bOpen && FirstOpenClientRemoteAddress.IsEmpty())
			{
				FirstOpenClientRemoteAddress = Connection->LowLevelGetRemoteAddress(true);
			}
		}
	}
	UNetConnection* ServerConnection = NetDriver ? NetDriver->ServerConnection : nullptr;
	const bool bServerConnectionOpen = ServerConnection
		&& ServerConnection->GetConnectionState() == USOCK_Open;
	const bool bSocketConnected = NetMode == NM_ListenServer
		? OpenClientConnections > 0
		: (NetMode == NM_Client && bServerConnectionOpen);
	int32 PacketLagMilliseconds = 0;
	int32 PacketLossPercent = 0;
	int32 PacketOrderEnabled = 0;
	int32 PacketDuplicatePercent = 0;
#if DO_ENABLE_NET_TEST
	if (NetDriver)
	{
		PacketLagMilliseconds = NetDriver->PacketSimulationSettings.PktLag;
		PacketLossPercent = NetDriver->PacketSimulationSettings.PktLoss;
		PacketOrderEnabled = NetDriver->PacketSimulationSettings.PktOrder;
		PacketDuplicatePercent = NetDriver->PacketSimulationSettings.PktDup;
	}
#endif
	const FString NetDriverLocalAddress = JsonSafeToken(
		NetDriver ? NetDriver->LowLevelGetNetworkNumber() : FString());
	const FString ClientServerRemoteAddress = JsonSafeToken(
		ServerConnection ? ServerConnection->LowLevelGetRemoteAddress(true) : FString());
	FirstOpenClientRemoteAddress = JsonSafeToken(MoveTemp(FirstOpenClientRemoteAddress));

	AGuLiBattleGameState* BattleState = World->GetGameState<AGuLiBattleGameState>();
	const int32 PlayerStateCount = BattleState ? BattleState->PlayerArray.Num() : 0;
	const TArray<FGuLiWingmanPublicBootstrapState>* PublicBootstraps = BattleState
		? &BattleState->GetPublicWingmanBootstraps() : nullptr;
	const int32 PublicBootstrapCount = PublicBootstraps ? PublicBootstraps->Num() : 0;
	int32 WellFormedPublicBootstrapCount = 0;
	if (PublicBootstraps)
	{
		for (const FGuLiWingmanPublicBootstrapState& PublicState : *PublicBootstraps)
		{
			WellFormedPublicBootstrapCount += PublicState.IsWellFormed() ? 1 : 0;
		}
	}
	const bool bAllPublicBootstrapsWellFormed = PublicBootstrapCount > 0
		&& WellFormedPublicBootstrapCount == PublicBootstrapCount;

	int32 ShipCount = 0;
	for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
	{
		ShipCount += IsValid(*It) && !It->IsActorBeingDestroyed() ? 1 : 0;
	}

	const FGuLiWingmanRelayAuthorityRegistry* Registry = BattleState
		? BattleState->GetWingmanRelayAuthorityRegistry() : nullptr;
	const int32 RelayGroupCount = Registry ? Registry->Num() : 0;
	int32 ActiveRelayGroupCount = 0;
	int32 StrictReadyRelayGroupCount = 0;
	int32 StrictGrowingRelayGroupCount = 0;
	uint32 MinimumAcceptedFrameAcrossStrictGroups = 0u;
	int32 StrictAcceptedBatchCount = 0;
	uint8 StrictAcceptedFlightMask = 0u;
	int32 InitializingRelayGroupCount = 0;
	int32 RevokedRelayGroupCount = 0;
	int32 AbilityConfigAcknowledgedGroupCount = 0;
	int32 BootstrapAcknowledgedGroupCount = 0;
	int32 OutstandingBootstrapGroupCount = 0;
	int32 AtomicRequiredGroupCount = 0;
	int32 AtomicCommittedGroupCount = 0;
	uint64 LeaseMaintenanceExecutionCount = 0u;
	int32 LatestLeaseEventType = INDEX_NONE;
	double LatestLeaseEventDetectedSeconds = -1.0;
	uint64 ServerWingmanMovementWriteCount = 0u;
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> MaximumAcceptedSequenceByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> MaximumAcceptedFrameByFlight{};
	if (Registry && PublicBootstraps)
	{
		for (const FGuLiWingmanPublicBootstrapState& PublicState : *PublicBootstraps)
		{
			const FGuLiWingmanRelayServer* Relay = Registry->FindGroup(PublicState.Group);
			if (!Relay)
			{
				continue;
			}
			ActiveRelayGroupCount += Relay->GetLeaseState().Lifecycle
				== EGuLiWingmanGroupLifecycle::Active ? 1 : 0;
			InitializingRelayGroupCount += Relay->GetLeaseState().Lifecycle
				== EGuLiWingmanGroupLifecycle::Initializing ? 1 : 0;
			RevokedRelayGroupCount += Relay->GetLeaseState().Lifecycle
				== EGuLiWingmanGroupLifecycle::Revoked ? 1 : 0;
			AbilityConfigAcknowledgedGroupCount += Relay->IsAbilityConfigAcknowledged() ? 1 : 0;
			BootstrapAcknowledgedGroupCount += Relay->IsBootstrapAcknowledged() ? 1 : 0;
			OutstandingBootstrapGroupCount += Relay->HasOutstandingBootstrap() ? 1 : 0;
			AtomicRequiredGroupCount += Relay->IsAtomicCandidateBatchRequired() ? 1 : 0;
			AtomicCommittedGroupCount += Relay->IsAtomicCandidateBatchCommitted() ? 1 : 0;
			LeaseMaintenanceExecutionCount += Relay->GetLeaseMaintenanceExecutionCount();
			for (const FGuLiWingmanLeaseEvent& Event : Relay->GetLeaseEvents())
			{
				if (Event.DetectedTimeSeconds >= LatestLeaseEventDetectedSeconds)
				{
					LatestLeaseEventDetectedSeconds = Event.DetectedTimeSeconds;
					LatestLeaseEventType = static_cast<int32>(Event.Type);
				}
			}
			ServerWingmanMovementWriteCount += Relay->GetServerWingmanMovementWriteCount();
			uint8 GroupFlightMask = 0u;
			uint32 GroupMinimumAcceptedFrame = MAX_uint32;
			bool bEveryFlightAdvancedPastInitial = true;
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				const uint32 Sequence = Relay->GetAcceptedSequenceForFlight(FlightIndex);
				const uint32 Frame = Relay->GetLastAcceptedFrameForFlight(FlightIndex);
				MaximumAcceptedSequenceByFlight[FlightIndex] = FMath::Max(
					MaximumAcceptedSequenceByFlight[FlightIndex], Sequence);
				MaximumAcceptedFrameByFlight[FlightIndex] = FMath::Max(
					MaximumAcceptedFrameByFlight[FlightIndex], Frame);
				if (Sequence != 0u && Frame != 0u)
				{
					GroupFlightMask |= static_cast<uint8>(1u << FlightIndex);
				}
				GroupMinimumAcceptedFrame = FMath::Min(GroupMinimumAcceptedFrame, Frame);
				bEveryFlightAdvancedPastInitial &= Frame > 1u;
			}
			StrictAcceptedFlightMask |= GroupFlightMask;
			const bool bStrictGroupReady = GroupFlightMask
				== static_cast<uint8>((1u << GULI_WINGMAN_FLIGHT_COUNT) - 1u);
			StrictReadyRelayGroupCount += bStrictGroupReady ? 1 : 0;
			if (bStrictGroupReady && bEveryFlightAdvancedPastInitial)
			{
				++StrictGrowingRelayGroupCount;
				MinimumAcceptedFrameAcrossStrictGroups = MinimumAcceptedFrameAcrossStrictGroups == 0u
					? GroupMinimumAcceptedFrame
					: FMath::Min(MinimumAcceptedFrameAcrossStrictGroups, GroupMinimumAcceptedFrame);
			}
			for (const FGuLiWingmanAcceptedBatch& Batch : Relay->GetAcceptedHistory())
			{
				StrictAcceptedBatchCount += Batch.IsWellFormed()
					&& Batch.FlightIndex < GULI_WINGMAN_FLIGHT_COUNT
					&& Batch.Samples.Num() == GULI_WINGMAN_MEMBERS_PER_FLIGHT ? 1 : 0;
			}
		}
	}

	AGuLiBattlePlayerController* LocalController = nullptr;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AGuLiBattlePlayerController* Candidate = Cast<AGuLiBattlePlayerController>(It->Get());
		if (Candidate && Candidate->IsLocalController())
		{
			LocalController = Candidate;
			break;
		}
	}
	const UGuLiWingmanRelayComponent* LocalRelay = LocalController
		? LocalController->GetWingmanRelayComponent() : nullptr;
	const bool bClientBootstrapWellFormed = LocalRelay
		&& LocalRelay->GetLastClientBootstrap().IsWellFormed();
	const FGuLiWingmanRelayReplicatedState* ClientRelayState = LocalRelay
		? &LocalRelay->GetRelayState() : nullptr;
	const APawn* LocalPawn = LocalController ? LocalController->GetPawn() : nullptr;
	const AGuLiBattlePlayerState* LocalPlayerState = LocalController
		? LocalController->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const bool bClientAbilityConfigUsable = ClientRelayState
		&& ClientRelayState->AbilityConfig.IsUsableByLeaseOwner();
	const uint32 ClientAtomicBuildAttemptCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildAttemptCount() : 0u;
	const uint32 ClientAtomicBuildPreconditionFailureCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildPreconditionFailureCount() : 0u;
	const uint32 ClientAtomicBuildMissingSimulationFailureCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildMissingSimulationFailureCount() : 0u;
	const uint32 ClientAtomicBuildFlightCandidateFailureCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildFlightCandidateFailureCount() : 0u;
	const int32 ClientAtomicBuildLastFailedFlightIndex = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildLastFailedFlightIndex() : INDEX_NONE;
	const uint32 ClientAtomicBuildEmptyFailureCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildEmptyFailureCount() : 0u;
	const uint32 ClientAtomicBuildFragmentFailureCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildFragmentFailureCount() : 0u;
	const uint32 ClientAtomicBuildSubmittedCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicBuildSubmittedCount() : 0u;
	const uint32 ClientAtomicResultCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicResultCount() : 0u;
	const int32 ClientLastAtomicResultDisposition = LocalRelay
		? static_cast<int32>(LocalRelay->GetListenSmokeLastAtomicResultDisposition()) : INDEX_NONE;
	const int32 ClientLastAtomicRejectReason = LocalRelay
		? static_cast<int32>(LocalRelay->GetListenSmokeLastAtomicResultRejectReason()) : INDEX_NONE;
	const uint32 ClientOwnerEligibleTickCount = LocalRelay
		? LocalRelay->GetListenSmokeOwnerEligibleTickCount() : 0u;
	const uint32 ClientOwnerInactiveGateCount = LocalRelay
		? LocalRelay->GetListenSmokeOwnerInactiveGateCount() : 0u;
	const uint32 ClientOwnerMissingRuntimeGateCount = LocalRelay
		? LocalRelay->GetListenSmokeOwnerMissingRuntimeGateCount() : 0u;
	const uint32 ClientNormalBuildAttemptCount = LocalRelay
		? LocalRelay->GetListenSmokeNormalBuildAttemptCount() : 0u;
	const uint32 ClientNormalBuildFailureCount = LocalRelay
		? LocalRelay->GetListenSmokeNormalBuildFailureCount() : 0u;
	const int32 ClientNormalBuildLastFailedFlightIndex = LocalRelay
		? LocalRelay->GetListenSmokeNormalBuildLastFailedFlightIndex() : INDEX_NONE;
	const uint32 ClientNormalSubmittedCount = LocalRelay
		? LocalRelay->GetListenSmokeNormalSubmittedCount() : 0u;
	const uint32 ClientNormalResultCount = LocalRelay
		? LocalRelay->GetListenSmokeNormalResultCount() : 0u;
	const uint32 ClientNormalAcceptedCount = LocalRelay
		? LocalRelay->GetListenSmokeNormalAcceptedCount() : 0u;
	const uint32 ClientLastNormalFlightModeMask = LocalRelay
		? LocalRelay->GetListenSmokeLastNormalFlightModeMask() : 0u;
	const uint32 ClientLastAtomicFlightModeMask = LocalRelay
		? LocalRelay->GetListenSmokeLastAtomicFlightModeMask() : 0u;
	const int32 ClientLastNormalResultDisposition = LocalRelay
		? static_cast<int32>(LocalRelay->GetListenSmokeLastNormalResultDisposition()) : INDEX_NONE;
	const int32 ClientLastNormalRejectReason = LocalRelay
		? static_cast<int32>(LocalRelay->GetListenSmokeLastNormalResultRejectReason()) : INDEX_NONE;
	const uint32 ClientSimulationTick = LocalRelay
		? LocalRelay->GetListenSmokeClientSimulationTick() : 0u;
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> ClientNextFrameSequenceByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> ClientAcceptedSequenceByFlight{};
	if (LocalRelay)
	{
		for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
		{
			ClientNextFrameSequenceByFlight[FlightIndex] =
				LocalRelay->GetListenSmokeNextFrameSequence(FlightIndex);
			ClientAcceptedSequenceByFlight[FlightIndex] =
				LocalRelay->GetListenSmokeAcceptedSequence(FlightIndex);
		}
	}
	const UGuLiWingmanSimulationSubsystem* Simulation =
		World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	const int32 OwnerMassEntityCount = Simulation ? Simulation->GetTotalOwnedEntityCount() : 0;
	const bool bClientRelayGroupValid = ClientRelayState
		&& ClientRelayState->Lease.Group.IsValid();
	const bool bClientRelayOwnerMatchesLocal = ClientRelayState && LocalPlayerState
		&& LocalPlayerState->GetPlayerGuid().IsValid()
		&& ClientRelayState->Lease.OwnerPlayerGuid == LocalPlayerState->GetPlayerGuid();
	const bool bClientRelayCarrierSourceValid = ClientRelayState
		&& ClientRelayState->LatestCarrierSource.IsValid();
	const bool bClientUploadRateGrantWellFormed = ClientRelayState
		&& ClientRelayState->UploadRateGrant.IsWellFormed();
	const bool bClientOwnedRelayGroupPresent = Simulation && bClientRelayGroupValid
		&& Simulation->HasOwnedGroup(ClientRelayState->Lease.Group);
	const FGuLiWingmanBootstrapBundle* ClientBootstrap = LocalRelay
		? &LocalRelay->GetLastClientBootstrap() : nullptr;

	const bool bBootstrapReady = NetMode == NM_ListenServer
		? bAllPublicBootstrapsWellFormed
		: (NetMode == NM_Client && bClientBootstrapWellFormed && bClientAbilityConfigUsable);
	const bool bStrictFlightReady = NetMode == NM_ListenServer
		&& ActiveRelayGroupCount > 0 && StrictReadyRelayGroupCount > 0
		&& StrictAcceptedBatchCount >= GULI_WINGMAN_FLIGHT_COUNT;
	const bool bServerMovementCounterZero = ServerWingmanMovementWriteCount == 0u;
	const bool bRoleReady = NetMode == NM_ListenServer
		? (bSocketConnected && bBootstrapReady && bStrictFlightReady && bServerMovementCounterZero)
		: (NetMode == NM_Client && bSocketConnected && bBootstrapReady
			&& OwnerMassEntityCount >= GULI_WINGMAN_GROUP_SIZE);

	const FString DriverName = JsonSafeToken(GetNameSafe(NetDriver));
	const FString Phase = bFinalSample ? TEXT("final") : TEXT("sample");
	const GuLiWingmanWorldValidation::FListenSmokeDiagnostics WorldValidatorDiagnostics =
		GuLiWingmanWorldValidation::GetListenSmokeDiagnostics();
	const FString LastStaticHitActor = JsonSafeToken(WorldValidatorDiagnostics.LastStaticHitActor);
	const FString LastStaticHitComponent = JsonSafeToken(WorldValidatorDiagnostics.LastStaticHitComponent);
	UE_LOG(LogGuLiStrike, Display,
		TEXT("[GULI_LISTEN_SMOKE] {\"schema\":\"guli.listen-smoke.v1\",\"phase\":\"%s\",\"run_id\":\"%s\",\"requested_role\":\"%s\",\"elapsed_seconds\":%.3f,\"net_mode\":\"%s\",\"net_driver\":\"%s\",\"net_driver_local_address\":\"%s\",\"client_server_remote_address\":\"%s\",\"first_open_client_remote_address\":\"%s\",\"has_net_driver\":%s,\"socket_connected\":%s,\"packet_lag_ms\":%d,\"packet_loss_percent\":%d,\"packet_order_enabled\":%d,\"packet_duplicate_percent\":%d,\"client_connection_count\":%d,\"open_client_connection_count\":%d,\"server_connection_open\":%s,\"player_state_count\":%d,\"ship_count\":%d,\"public_bootstrap_count\":%d,\"well_formed_six_scope_bootstrap_count\":%d,\"all_public_bootstraps_well_formed\":%s,\"relay_group_count\":%d,\"active_relay_group_count\":%d,\"initializing_relay_group_count\":%d,\"revoked_relay_group_count\":%d,\"ability_config_acknowledged_group_count\":%d,\"bootstrap_acknowledged_group_count\":%d,\"outstanding_bootstrap_group_count\":%d,\"atomic_required_group_count\":%d,\"atomic_committed_group_count\":%d,\"lease_maintenance_execution_count\":%llu,\"latest_lease_event_type\":%d,\"latest_lease_event_detected_seconds\":%.3f,\"strict_ready_relay_group_count\":%d,\"strict_growing_relay_group_count\":%d,\"minimum_accepted_frame_across_strict_groups\":%u,\"strict_accepted_batch_count\":%d,\"strict_accepted_flight_mask\":%u,\"flight_accepted_sequences\":[%u,%u,%u,%u,%u],\"flight_last_frames\":[%u,%u,%u,%u,%u],\"server_wingman_movement_write_count\":%llu,\"client_bootstrap_well_formed\":%s,\"client_ability_config_usable\":%s,\"client_atomic_build_attempt_count\":%u,\"client_atomic_build_precondition_failure_count\":%u,\"client_atomic_build_missing_simulation_failure_count\":%u,\"client_atomic_build_flight_candidate_failure_count\":%u,\"client_atomic_build_last_failed_flight_index\":%d,\"client_atomic_build_empty_failure_count\":%u,\"client_atomic_build_fragment_failure_count\":%u,\"client_atomic_build_submitted_count\":%u,\"client_atomic_result_count\":%u,\"client_last_atomic_result_disposition\":%d,\"client_last_atomic_reject_reason\":%d,\"client_owner_eligible_tick_count\":%u,\"client_owner_inactive_gate_count\":%u,\"client_owner_missing_runtime_gate_count\":%u,\"client_normal_build_attempt_count\":%u,\"client_normal_build_failure_count\":%u,\"client_normal_build_last_failed_flight_index\":%d,\"client_normal_submitted_count\":%u,\"client_normal_result_count\":%u,\"client_normal_accepted_count\":%u,\"client_last_normal_flight_mode_mask\":%u,\"client_last_atomic_flight_mode_mask\":%u,\"client_last_normal_result_disposition\":%d,\"client_last_normal_reject_reason\":%d,\"client_simulation_tick\":%u,\"client_next_frame_sequences\":[%u,%u,%u,%u,%u],\"client_accepted_sequences\":[%u,%u,%u,%u,%u],\"client_local_controller_present\":%s,\"client_local_pawn_present\":%s,\"client_simulation_subsystem_present\":%s,\"client_relay_group_valid\":%s,\"client_relay_owner_matches_local\":%s,\"client_relay_lifecycle\":%d,\"client_relay_match_epoch\":%u,\"client_relay_connection_generation\":%u,\"client_relay_roster_revision\":%u,\"client_relay_carrier_source_valid\":%s,\"client_relay_carrier_canonical_epoch\":%u,\"client_relay_carrier_move_revision\":%u,\"client_upload_rate_grant_well_formed\":%s,\"client_owned_relay_group_present\":%s,\"client_last_bootstrap_requires_atomic\":%s,\"client_last_bootstrap_atomic_kind\":%d,\"world_validator_invocation_count\":%llu,\"world_validator_context_reject_count\":%llu,\"world_validator_invalid_radius_reject_count\":%llu,\"world_validator_missing_flight_nav_reject_count\":%llu,\"world_validator_static_collision_reject_count\":%llu,\"world_validator_tagged_dynamic_reject_count\":%llu,\"world_validator_flight_nav_segment_reject_count\":%llu,\"world_validator_accepted_count\":%llu,\"world_validator_last_flight_nav_status\":%d,\"world_validator_last_static_hit_actor\":\"%s\",\"world_validator_last_static_hit_component\":\"%s\",\"owner_mass_entity_count\":%d,\"bootstrap_ready\":%s,\"strict_flight_ready\":%s,\"role_ready\":%s,\"final_sample\":%s}"),
		*Phase,
		*RunId,
		*RequestedRole,
		FPlatformTime::Seconds() - StartedWallSeconds,
		NetModeName(NetMode),
		*DriverName,
		*NetDriverLocalAddress,
		*ClientServerRemoteAddress,
		*FirstOpenClientRemoteAddress,
		BoolJson(NetDriver != nullptr),
		BoolJson(bSocketConnected),
		PacketLagMilliseconds,
		PacketLossPercent,
		PacketOrderEnabled,
		PacketDuplicatePercent,
		ClientConnectionCount,
		OpenClientConnections,
		BoolJson(bServerConnectionOpen),
		PlayerStateCount,
		ShipCount,
		PublicBootstrapCount,
		WellFormedPublicBootstrapCount,
		BoolJson(bAllPublicBootstrapsWellFormed),
		RelayGroupCount,
		ActiveRelayGroupCount,
		InitializingRelayGroupCount,
		RevokedRelayGroupCount,
		AbilityConfigAcknowledgedGroupCount,
		BootstrapAcknowledgedGroupCount,
		OutstandingBootstrapGroupCount,
		AtomicRequiredGroupCount,
		AtomicCommittedGroupCount,
		LeaseMaintenanceExecutionCount,
		LatestLeaseEventType,
		LatestLeaseEventDetectedSeconds,
		StrictReadyRelayGroupCount,
		StrictGrowingRelayGroupCount,
		MinimumAcceptedFrameAcrossStrictGroups,
		StrictAcceptedBatchCount,
		static_cast<uint32>(StrictAcceptedFlightMask),
		MaximumAcceptedSequenceByFlight[0], MaximumAcceptedSequenceByFlight[1],
		MaximumAcceptedSequenceByFlight[2], MaximumAcceptedSequenceByFlight[3],
		MaximumAcceptedSequenceByFlight[4],
		MaximumAcceptedFrameByFlight[0], MaximumAcceptedFrameByFlight[1],
		MaximumAcceptedFrameByFlight[2], MaximumAcceptedFrameByFlight[3],
		MaximumAcceptedFrameByFlight[4],
		ServerWingmanMovementWriteCount,
		BoolJson(bClientBootstrapWellFormed),
		BoolJson(bClientAbilityConfigUsable),
		ClientAtomicBuildAttemptCount,
		ClientAtomicBuildPreconditionFailureCount,
		ClientAtomicBuildMissingSimulationFailureCount,
		ClientAtomicBuildFlightCandidateFailureCount,
		ClientAtomicBuildLastFailedFlightIndex,
		ClientAtomicBuildEmptyFailureCount,
		ClientAtomicBuildFragmentFailureCount,
		ClientAtomicBuildSubmittedCount,
		ClientAtomicResultCount,
		ClientLastAtomicResultDisposition,
		ClientLastAtomicRejectReason,
		ClientOwnerEligibleTickCount,
		ClientOwnerInactiveGateCount,
		ClientOwnerMissingRuntimeGateCount,
		ClientNormalBuildAttemptCount,
		ClientNormalBuildFailureCount,
		ClientNormalBuildLastFailedFlightIndex,
		ClientNormalSubmittedCount,
		ClientNormalResultCount,
		ClientNormalAcceptedCount,
		ClientLastNormalFlightModeMask,
		ClientLastAtomicFlightModeMask,
		ClientLastNormalResultDisposition,
		ClientLastNormalRejectReason,
		ClientSimulationTick,
		ClientNextFrameSequenceByFlight[0], ClientNextFrameSequenceByFlight[1],
		ClientNextFrameSequenceByFlight[2], ClientNextFrameSequenceByFlight[3],
		ClientNextFrameSequenceByFlight[4],
		ClientAcceptedSequenceByFlight[0], ClientAcceptedSequenceByFlight[1],
		ClientAcceptedSequenceByFlight[2], ClientAcceptedSequenceByFlight[3],
		ClientAcceptedSequenceByFlight[4],
		BoolJson(LocalController != nullptr),
		BoolJson(LocalPawn != nullptr),
		BoolJson(Simulation != nullptr),
		BoolJson(bClientRelayGroupValid),
		BoolJson(bClientRelayOwnerMatchesLocal),
		ClientRelayState ? static_cast<int32>(ClientRelayState->Lease.Lifecycle) : INDEX_NONE,
		ClientRelayState ? ClientRelayState->MatchEpoch : 0u,
		ClientRelayState ? ClientRelayState->ConnectionGeneration : 0u,
		ClientRelayState ? ClientRelayState->RosterRevision : 0u,
		BoolJson(bClientRelayCarrierSourceValid),
		ClientRelayState ? ClientRelayState->LatestCarrierSource.CanonicalEpoch : 0u,
		ClientRelayState ? ClientRelayState->LatestCarrierSource.MoveRevision : 0u,
		BoolJson(bClientUploadRateGrantWellFormed),
		BoolJson(bClientOwnedRelayGroupPresent),
		BoolJson(ClientBootstrap && ClientBootstrap->bRequiresAtomicCandidateBatch),
		ClientBootstrap ? static_cast<int32>(ClientBootstrap->AtomicBatchKind) : INDEX_NONE,
		WorldValidatorDiagnostics.InvocationCount,
		WorldValidatorDiagnostics.ContextRejectCount,
		WorldValidatorDiagnostics.InvalidRadiusRejectCount,
		WorldValidatorDiagnostics.MissingNavigationSubsystemRejectCount,
		WorldValidatorDiagnostics.StaticCollisionRejectCount,
		WorldValidatorDiagnostics.DynamicObstacleRejectCount,
		WorldValidatorDiagnostics.NavigationRejectCount,
		WorldValidatorDiagnostics.AcceptedCount,
		WorldValidatorDiagnostics.LastNavigationStatus,
		*LastStaticHitActor,
		*LastStaticHitComponent,
		OwnerMassEntityCount,
		BoolJson(bBootstrapReady),
		BoolJson(bStrictFlightReady),
		BoolJson(bRoleReady),
		BoolJson(bFinalSample));
}
