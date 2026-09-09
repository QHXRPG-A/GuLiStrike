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
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
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
	MemberObservations.Reset();
	MotionStallViolationCount = 0u;
	MaximumLowDisplacementSeconds = 0.0;
	bShipMotionDriverEnabled = FParse::Param(
		FCommandLine::Get(), TEXT("GuLiListenSmokeMoveShip"));
	bBoundedShipMotionDriverEnabled = FParse::Param(
		FCommandLine::Get(), TEXT("GuLiListenSmokePersistentTargets"));
	bShipMotionDriverActive = false;
	bHasDrivenShipStartTransform = false;
	ShipTranslationFromStartCentimeters = 0.0f;
	MaximumShipTranslationCentimeters = 0.0f;
	ShipRotationFromStartDegrees = 0.0f;
	MaximumShipRotationDegrees = 0.0f;

	FParse::Value(FCommandLine::Get(), TEXT("-GuLiListenSmokeRole="), RequestedRole);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiListenSmokeRunId="), RunId);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiListenSmokeSeconds="), DurationSeconds);
	DurationSeconds = FMath::Clamp(DurationSeconds, 10.0, 360.0);
	RequestedRole = GuLiListenSmokeDiagnostics::JsonSafeToken(RequestedRole);
	RunId = GuLiListenSmokeDiagnostics::JsonSafeToken(RunId);

	UE_LOG(LogGuLiStrike, Display,
		TEXT("[GULI_LISTEN_SMOKE] {\"schema\":\"guli.listen-smoke.v1\",\"phase\":\"start\",\"run_id\":\"%s\",\"requested_role\":\"%s\",\"duration_seconds\":%.3f,\"read_only\":%s,\"ship_motion_driver\":%s,\"sample_hz\":1}"),
		*RunId, *RequestedRole, DurationSeconds,
		GuLiListenSmokeDiagnostics::BoolJson(!bShipMotionDriverEnabled),
		GuLiListenSmokeDiagnostics::BoolJson(bShipMotionDriverEnabled));
}

void UGuLiListenSmokeDiagnosticsSubsystem::Deinitialize()
{
	if (AGuLiStrikeShip* Ship = DrivenShip.Get())
	{
		if (UGuLiShipMovementComponent* Movement = Ship->GetShipMovement())
		{
			Movement->ClearFlightInput();
		}
	}
	if (!bFinalSampleEmitted)
	{
		EmitSample(true);
	}
	Super::Deinitialize();
}

void UGuLiListenSmokeDiagnosticsSubsystem::Tick(const float DeltaTime)
{
	DriveLocalShip(DeltaTime);
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

void UGuLiListenSmokeDiagnosticsSubsystem::DriveLocalShip(const float DeltaSeconds)
{
	(void)DeltaSeconds;
	bShipMotionDriverActive = false;
	UWorld* World = GetWorld();
	if (!bShipMotionDriverEnabled || !World)
	{
		return;
	}

	AGuLiStrikeShip* Ship = DrivenShip.Get();
	if (!IsValid(Ship) || Ship->IsActorBeingDestroyed() || !Ship->IsLocallyControlled())
	{
		Ship = nullptr;
		for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->IsLocallyControlled())
			{
				Ship = *It;
				break;
			}
		}
	}
	UGuLiShipMovementComponent* Movement = Ship ? Ship->GetShipMovement() : nullptr;
	if (!Ship || !Movement || !Movement->IsMovementConfigReady())
	{
		return;
	}

	if (DrivenShip.Get() != Ship || !bHasDrivenShipStartTransform)
	{
		DrivenShip = Ship;
		DrivenShipStartTransform = Ship->GetActorTransform();
		bHasDrivenShipStartTransform = true;
		ShipTranslationFromStartCentimeters = 0.0f;
		ShipRotationFromStartDegrees = 0.0f;
	}

	// Deterministic course changes exercise both canonical Ship movement and
	// Wingman carrier-following. Inputs go through the real prediction path; the
	// harness never teleports or directly rotates the Ship.
	const double ElapsedSeconds = FMath::Max(
		0.0, FPlatformTime::Seconds() - StartedWallSeconds);
	FVector LocalThrust = FVector::ZeroVector;
	float Turn = 0.0f;
	float Strafe = 0.0f;
	if (bBoundedShipMotionDriverEnabled)
	{
		// The 300-second combat gate needs moving carriers and a continuously legal
		// target field. Follow a closed world-space course around each spawn point so
		// both clients exercise real CMC prediction without drifting out of the
		// 1,800-metre target release radius. A PD correction turns the reference curve
		// into ordinary local flight input; it never writes the Ship transform.
		constexpr double CoursePeriodSeconds = 72.0;
		constexpr double HorizontalRadiusCentimeters = 12000.0;
		constexpr double VerticalRadiusCentimeters = 3000.0;
		const double AngularSpeed = UE_TWO_PI / CoursePeriodSeconds;
		const double Phase = FMath::Fmod(ElapsedSeconds, CoursePeriodSeconds) * AngularSpeed;
		const FVector DesiredOffset(
			HorizontalRadiusCentimeters * FMath::Sin(Phase),
			HorizontalRadiusCentimeters * (1.0 - FMath::Cos(Phase)),
			VerticalRadiusCentimeters * FMath::Sin(2.0 * Phase));
		const FVector DesiredVelocity(
			HorizontalRadiusCentimeters * AngularSpeed * FMath::Cos(Phase),
			HorizontalRadiusCentimeters * AngularSpeed * FMath::Sin(Phase),
			2.0 * VerticalRadiusCentimeters * AngularSpeed * FMath::Cos(2.0 * Phase));
		const FVector DesiredLocation = DrivenShipStartTransform.GetLocation() + DesiredOffset;
		const FVector PositionError = DesiredLocation - Ship->GetActorLocation();
		const FVector VelocityError = DesiredVelocity - Movement->Velocity;
		const FVector DesiredWorldAcceleration = PositionError * 0.02 + VelocityError * 0.65;
		const float MaximumAcceleration = FMath::Max(
			1.0f, Movement->GetEffectiveMovementConfig().MaxAcceleration);
		const FVector WorldInput = (DesiredWorldAcceleration / MaximumAcceleration).GetClampedToMaxSize(1.0);
		LocalThrust = Ship->GetActorQuat().UnrotateVector(WorldInput);
		Turn = static_cast<float>(0.70 * FMath::Sin(Phase));
		Strafe = static_cast<float>(0.20 * FMath::Cos(Phase));
	}
	else
	{
		const double PhaseSeconds = FMath::Fmod(ElapsedSeconds, 24.0);
		LocalThrust = FVector(1.0, 0.0, 0.0);
		if (PhaseSeconds < 6.0)
		{
			LocalThrust.Z = 0.20;
			Turn = 0.65f;
			Strafe = 0.20f;
		}
		else if (PhaseSeconds < 12.0)
		{
			LocalThrust.Z = -0.15;
			Turn = -0.55f;
			Strafe = -0.25f;
		}
		else if (PhaseSeconds < 18.0)
		{
			LocalThrust.Z = 0.10;
			Turn = 0.75f;
			Strafe = -0.15f;
		}
		else
		{
			LocalThrust.Z = -0.10;
			Turn = -0.65f;
			Strafe = 0.20f;
		}
	}
	Movement->AddThrustInput(LocalThrust);
	Movement->AddTurnInput(Turn);
	Movement->AddStrafeInput(Strafe);
	Movement->SetBoostInput(false);
	bShipMotionDriverActive = true;
	UpdateShipMotionObservation();
}

void UGuLiListenSmokeDiagnosticsSubsystem::UpdateShipMotionObservation()
{
	const AGuLiStrikeShip* Ship = DrivenShip.Get();
	if (!Ship || !bHasDrivenShipStartTransform)
	{
		ShipTranslationFromStartCentimeters = 0.0f;
		ShipRotationFromStartDegrees = 0.0f;
		return;
	}
	const FTransform CurrentTransform = Ship->GetActorTransform();
	if (CurrentTransform.ContainsNaN())
	{
		return;
	}
	ShipTranslationFromStartCentimeters = static_cast<float>(FVector::Distance(
		CurrentTransform.GetLocation(), DrivenShipStartTransform.GetLocation()));
	ShipRotationFromStartDegrees = FMath::RadiansToDegrees(
		DrivenShipStartTransform.GetRotation().AngularDistance(
			CurrentTransform.GetRotation()));
	MaximumShipTranslationCentimeters = FMath::Max(
		MaximumShipTranslationCentimeters, ShipTranslationFromStartCentimeters);
	MaximumShipRotationDegrees = FMath::Max(
		MaximumShipRotationDegrees, ShipRotationFromStartDegrees);
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
	UpdateShipMotionObservation();

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
	uint64 ServerEmergencyRebaseAcceptedCount = 0u;
	int32 ServerGroupsWithAutomaticTargets = 0;
	int32 ServerAutomaticTargetCount = 0;
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
			ServerEmergencyRebaseAcceptedCount += Relay->GetEmergencyRebaseAcceptedCount();
			ServerGroupsWithAutomaticTargets += Relay->AttackState.AutomaticTargets.IsEmpty() ? 0 : 1;
			ServerAutomaticTargetCount += Relay->AttackState.AutomaticTargets.Num();
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
	const uint32 ClientAtomicAcceptedCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicAcceptedCount() : 0u;
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
	const uint32 ClientEmergencyRebaseRequestCount = LocalRelay
		? LocalRelay->GetListenSmokeEmergencyRebaseRequestCount() : 0u;
	const uint32 ClientEmergencyRebaseResultCount = LocalRelay
		? LocalRelay->GetListenSmokeEmergencyRebaseResultCount() : 0u;
	const uint32 ClientEmergencyRebaseAcceptedCount = LocalRelay
		? LocalRelay->GetListenSmokeEmergencyRebaseAcceptedCount() : 0u;
	const uint32 ClientEmergencyRebaseAppliedCount = LocalRelay
		? LocalRelay->GetListenSmokeEmergencyRebaseAppliedCount() : 0u;
	// Pending is an expected cross-channel state and must not be reported as a
	// rejection. Relay owns exact terminal-disposition counters for the smoke gate.
	const uint32 ClientNormalRejectCount = LocalRelay
		? LocalRelay->GetListenSmokeNormalRejectedCount() : 0u;
	const uint32 ClientAtomicRejectCount = LocalRelay
		? LocalRelay->GetListenSmokeAtomicRejectedCount() : 0u;
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
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> ClientNormalRejectCountByFlight{};
	TStaticArray<int32, GULI_WINGMAN_FLIGHT_COUNT> ClientLastNormalRejectReasonByFlight{};
	for (int32& RejectReason : ClientLastNormalRejectReasonByFlight)
	{
		RejectReason = INDEX_NONE;
	}
	if (LocalRelay)
	{
		for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
		{
			ClientNextFrameSequenceByFlight[FlightIndex] =
				LocalRelay->GetListenSmokeNextFrameSequence(FlightIndex);
			ClientAcceptedSequenceByFlight[FlightIndex] =
				LocalRelay->GetListenSmokeAcceptedSequence(FlightIndex);
			ClientNormalRejectCountByFlight[FlightIndex] =
				LocalRelay->GetListenSmokeNormalRejectedCount(FlightIndex);
			ClientLastNormalRejectReasonByFlight[FlightIndex] = static_cast<int32>(
				LocalRelay->GetListenSmokeLastNormalRejectReason(FlightIndex));
		}
	}
	const UGuLiWingmanSimulationSubsystem* Simulation =
		World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	const int32 OwnerPawnCount = Simulation ? Simulation->GetTotalOwnedPawnCount() : 0;
	const double NowWallSeconds = FPlatformTime::Seconds();
	int32 OwnerStateTreeRunningCount = 0;
	int32 OwnerMotionEligibleCount = 0;
	int32 OwnerCurrentStallCount = 0;
	int32 OwnerMembersWithTwoAttackRuns = 0;
	int32 OwnerAssignedAttackTargetCount = 0;
	int32 OwnerNonIdleAttackCount = 0;
	int32 OwnerGuidingAttackCount = 0;
	int32 OwnerNonZeroAttackRunCount = 0;
	uint32 OwnerMinimumAttackRunCount = MAX_uint32;
	TSet<FGuLiWingmanHandle> EligibleMemberHandles;
	for (TActorIterator<AGuLiWingmanPawn> It(World); It; ++It)
	{
		AGuLiWingmanPawn* WingmanPawn = *It;
		if (!IsValid(WingmanPawn) || WingmanPawn->IsActorBeingDestroyed()
			|| !WingmanPawn->IsOwnerSimulationPawn()
			|| !WingmanPawn->IsOwnerSimulationActive()
			|| !WingmanPawn->IsPresentationInteractable()
			|| WingmanPawn->IsHidden())
		{
			continue;
		}
		const FGuLiWingmanRuntimeState& Runtime = WingmanPawn->GetRuntimeState();
		if (!Runtime.Identity.Handle.IsValid() || !Runtime.Dynamics.bAlive || Runtime.Dynamics.bStale)
		{
			continue;
		}

		++OwnerMotionEligibleCount;
		OwnerStateTreeRunningCount += WingmanPawn->IsStateTreeRunning() ? 1 : 0;
		OwnerAssignedAttackTargetCount += Runtime.Attack.Target.IsValid() ? 1 : 0;
		OwnerNonIdleAttackCount += Runtime.Attack.Phase != EGuLiWingmanAttackPhase::Idle ? 1 : 0;
		OwnerGuidingAttackCount += Runtime.Attack.bGuiding ? 1 : 0;
		OwnerNonZeroAttackRunCount += Runtime.Attack.RunId != 0u ? 1 : 0;
		EligibleMemberHandles.Add(Runtime.Identity.Handle);
		FGuLiListenSmokeMemberObservation& Observation =
			MemberObservations.FindOrAdd(Runtime.Identity.Handle);
		const FVector CurrentLocation = WingmanPawn->GetActorLocation();
		if (!Observation.bHasLowDisplacementAnchor
			|| FVector::DistSquared(CurrentLocation, Observation.LowDisplacementAnchor)
				>= FMath::Square(100.0f))
		{
			Observation.LowDisplacementAnchor = CurrentLocation;
			Observation.LowDisplacementStartWallSeconds = NowWallSeconds;
			Observation.bHasLowDisplacementAnchor = true;
			Observation.bCurrentStallRecorded = false;
		}
		const double LowDisplacementSeconds = FMath::Max(
			0.0, NowWallSeconds - Observation.LowDisplacementStartWallSeconds);
		Observation.MaximumLowDisplacementSeconds = FMath::Max(
			Observation.MaximumLowDisplacementSeconds, LowDisplacementSeconds);
		MaximumLowDisplacementSeconds = FMath::Max(
			MaximumLowDisplacementSeconds, LowDisplacementSeconds);
		if (LowDisplacementSeconds >= 5.0)
		{
			++OwnerCurrentStallCount;
			if (!Observation.bCurrentStallRecorded)
			{
				Observation.bCurrentStallRecorded = true;
				++MotionStallViolationCount;
			}
		}

		FGuLiListenSmokeSlotAttackObservation& SlotAttack =
			SlotAttackObservations.FindOrAdd(Runtime.Identity.Handle.GetGroupMemberIndex());
		if (Runtime.Attack.RunId != 0u
			&& (SlotAttack.LastObservedMember != Runtime.Identity.Handle
				|| Runtime.Attack.RunId != SlotAttack.LastObservedAttackRunId))
		{
			SlotAttack.LastObservedMember = Runtime.Identity.Handle;
			SlotAttack.LastObservedAttackRunId = Runtime.Attack.RunId;
			++SlotAttack.ObservedAttackRunCount;
		}
	}
	for (auto It = MemberObservations.CreateIterator(); It; ++It)
	{
		if (!EligibleMemberHandles.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (uint8 StableSlot = 0u; StableSlot < GULI_WINGMAN_GROUP_SIZE; ++StableSlot)
	{
		const FGuLiListenSmokeSlotAttackObservation* SlotAttack =
			SlotAttackObservations.Find(StableSlot);
		const uint32 RunCount = SlotAttack ? SlotAttack->ObservedAttackRunCount : 0u;
		OwnerMembersWithTwoAttackRuns += RunCount >= 2u ? 1 : 0;
		OwnerMinimumAttackRunCount = FMath::Min(OwnerMinimumAttackRunCount, RunCount);
	}
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
	const int32 ClientAutomaticTargetCount = ClientRelayState
		? ClientRelayState->AttackState.AutomaticTargets.Num() : 0;
	const uint32 ClientAttackStateRevision = ClientRelayState
		? ClientRelayState->AttackState.Revision : 0u;
	const bool bClientBootstrapPending = LocalRelay
		&& LocalRelay->IsListenSmokeClientBootstrapPending();
	const bool bClientCombatAuthorizationValid = Simulation && ClientRelayState
		&& Simulation->IsOwnedGroupCombatAuthorizationValid(ClientRelayState->Lease.Group);

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
			&& OwnerPawnCount >= GULI_WINGMAN_GROUP_SIZE);

	const FString DriverName = JsonSafeToken(GetNameSafe(NetDriver));
	const FString Phase = bFinalSample ? TEXT("final") : TEXT("sample");
	const GuLiWingmanWorldValidation::FListenSmokeDiagnostics WorldValidatorDiagnostics =
		GuLiWingmanWorldValidation::GetListenSmokeDiagnostics();
	const FString LastStaticHitActor = JsonSafeToken(WorldValidatorDiagnostics.LastStaticHitActor);
	const FString LastStaticHitComponent = JsonSafeToken(WorldValidatorDiagnostics.LastStaticHitComponent);
	UE_LOG(LogGuLiStrike, Display,
		TEXT("[GULI_WINGMAN_OWNER_COMBAT] role=%s server_groups_with_targets=%d server_target_entries=%d client_attack_revision=%u client_target_entries=%d bootstrap_pending=%d combat_auth=%d pawn_targets=%d pawn_non_idle=%d pawn_guiding=%d pawn_runs=%d"),
		*RequestedRole,
		ServerGroupsWithAutomaticTargets,
		ServerAutomaticTargetCount,
		ClientAttackStateRevision,
		ClientAutomaticTargetCount,
		bClientBootstrapPending ? 1 : 0,
		bClientCombatAuthorizationValid ? 1 : 0,
		OwnerAssignedAttackTargetCount,
		OwnerNonIdleAttackCount,
		OwnerGuidingAttackCount,
		OwnerNonZeroAttackRunCount);
	UE_LOG(LogGuLiStrike, Display,
		TEXT("[GULI_LISTEN_SMOKE] {\"schema\":\"guli.listen-smoke.v1\",\"phase\":\"%s\",\"run_id\":\"%s\",\"requested_role\":\"%s\",\"elapsed_seconds\":%.3f,\"net_mode\":\"%s\",\"net_driver\":\"%s\",\"net_driver_local_address\":\"%s\",\"client_server_remote_address\":\"%s\",\"first_open_client_remote_address\":\"%s\",\"has_net_driver\":%s,\"socket_connected\":%s,\"packet_lag_ms\":%d,\"packet_loss_percent\":%d,\"packet_order_enabled\":%d,\"packet_duplicate_percent\":%d,\"client_connection_count\":%d,\"open_client_connection_count\":%d,\"server_connection_open\":%s,\"player_state_count\":%d,\"ship_count\":%d,\"ship_motion_driver_enabled\":%s,\"ship_motion_driver_active\":%s,\"ship_translation_from_start_cm\":%.3f,\"ship_max_translation_cm\":%.3f,\"ship_rotation_from_start_deg\":%.3f,\"ship_max_rotation_deg\":%.3f,\"public_bootstrap_count\":%d,\"well_formed_six_scope_bootstrap_count\":%d,\"all_public_bootstraps_well_formed\":%s,\"relay_group_count\":%d,\"active_relay_group_count\":%d,\"initializing_relay_group_count\":%d,\"revoked_relay_group_count\":%d,\"ability_config_acknowledged_group_count\":%d,\"bootstrap_acknowledged_group_count\":%d,\"outstanding_bootstrap_group_count\":%d,\"atomic_required_group_count\":%d,\"atomic_committed_group_count\":%d,\"lease_maintenance_execution_count\":%llu,\"latest_lease_event_type\":%d,\"latest_lease_event_detected_seconds\":%.3f,\"strict_ready_relay_group_count\":%d,\"strict_growing_relay_group_count\":%d,\"minimum_accepted_frame_across_strict_groups\":%u,\"strict_accepted_batch_count\":%d,\"strict_accepted_flight_mask\":%u,\"flight_accepted_sequences\":[%u,%u,%u,%u,%u],\"flight_last_frames\":[%u,%u,%u,%u,%u],\"server_wingman_movement_write_count\":%llu,\"server_emergency_rebase_accepted_count\":%llu,\"client_bootstrap_well_formed\":%s,\"client_ability_config_usable\":%s,\"client_atomic_build_attempt_count\":%u,\"client_atomic_build_precondition_failure_count\":%u,\"client_atomic_build_missing_simulation_failure_count\":%u,\"client_atomic_build_flight_candidate_failure_count\":%u,\"client_atomic_build_last_failed_flight_index\":%d,\"client_atomic_build_empty_failure_count\":%u,\"client_atomic_build_fragment_failure_count\":%u,\"client_atomic_build_submitted_count\":%u,\"client_atomic_result_count\":%u,\"client_atomic_accepted_count\":%u,\"client_atomic_reject_count\":%u,\"client_last_atomic_result_disposition\":%d,\"client_last_atomic_reject_reason\":%d,\"client_owner_eligible_tick_count\":%u,\"client_owner_inactive_gate_count\":%u,\"client_owner_missing_runtime_gate_count\":%u,\"client_normal_build_attempt_count\":%u,\"client_normal_build_failure_count\":%u,\"client_normal_build_last_failed_flight_index\":%d,\"client_normal_submitted_count\":%u,\"client_normal_result_count\":%u,\"client_normal_accepted_count\":%u,\"client_normal_reject_count\":%u,\"client_normal_reject_counts_by_flight\":[%u,%u,%u,%u,%u],\"client_last_normal_reject_reasons_by_flight\":[%d,%d,%d,%d,%d],\"client_emergency_rebase_request_count\":%u,\"client_emergency_rebase_result_count\":%u,\"client_emergency_rebase_accepted_count\":%u,\"client_emergency_rebase_applied_count\":%u,\"client_last_normal_flight_mode_mask\":%u,\"client_last_atomic_flight_mode_mask\":%u,\"client_last_normal_result_disposition\":%d,\"client_last_normal_reject_reason\":%d,\"client_simulation_tick\":%u,\"client_next_frame_sequences\":[%u,%u,%u,%u,%u],\"client_accepted_sequences\":[%u,%u,%u,%u,%u],\"client_local_controller_present\":%s,\"client_local_pawn_present\":%s,\"client_simulation_subsystem_present\":%s,\"client_relay_group_valid\":%s,\"client_relay_owner_matches_local\":%s,\"client_relay_lifecycle\":%d,\"client_relay_match_epoch\":%u,\"client_relay_connection_generation\":%u,\"client_relay_roster_revision\":%u,\"client_relay_carrier_source_valid\":%s,\"client_relay_carrier_canonical_epoch\":%u,\"client_relay_carrier_move_revision\":%u,\"client_upload_rate_grant_well_formed\":%s,\"client_owned_relay_group_present\":%s,\"client_last_bootstrap_requires_atomic\":%s,\"client_last_bootstrap_atomic_kind\":%d,\"world_validator_invocation_count\":%llu,\"world_validator_context_reject_count\":%llu,\"world_validator_invalid_radius_reject_count\":%llu,\"world_validator_missing_flight_nav_reject_count\":%llu,\"world_validator_static_collision_reject_count\":%llu,\"world_validator_tagged_dynamic_reject_count\":%llu,\"world_validator_flight_nav_segment_reject_count\":%llu,\"world_validator_accepted_count\":%llu,\"world_validator_last_flight_nav_status\":%d,\"world_validator_last_static_hit_actor\":\"%s\",\"world_validator_last_static_hit_component\":\"%s\",\"owner_pawn_count\":%d,\"owner_state_tree_running_count\":%d,\"owner_motion_eligible_count\":%d,\"owner_current_stall_count\":%d,\"owner_motion_stall_violation_count\":%u,\"owner_max_low_displacement_seconds\":%.3f,\"owner_members_with_two_attack_runs\":%d,\"owner_min_attack_runs\":%u,\"bootstrap_ready\":%s,\"strict_flight_ready\":%s,\"role_ready\":%s,\"final_sample\":%s}"),
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
		BoolJson(bShipMotionDriverEnabled),
		BoolJson(bShipMotionDriverActive),
		ShipTranslationFromStartCentimeters,
		MaximumShipTranslationCentimeters,
		ShipRotationFromStartDegrees,
		MaximumShipRotationDegrees,
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
		ServerEmergencyRebaseAcceptedCount,
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
		ClientAtomicAcceptedCount,
		ClientAtomicRejectCount,
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
		ClientNormalRejectCount,
		ClientNormalRejectCountByFlight[0], ClientNormalRejectCountByFlight[1],
		ClientNormalRejectCountByFlight[2], ClientNormalRejectCountByFlight[3],
		ClientNormalRejectCountByFlight[4],
		ClientLastNormalRejectReasonByFlight[0], ClientLastNormalRejectReasonByFlight[1],
		ClientLastNormalRejectReasonByFlight[2], ClientLastNormalRejectReasonByFlight[3],
		ClientLastNormalRejectReasonByFlight[4],
		ClientEmergencyRebaseRequestCount,
		ClientEmergencyRebaseResultCount,
		ClientEmergencyRebaseAcceptedCount,
		ClientEmergencyRebaseAppliedCount,
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
		OwnerPawnCount,
		OwnerStateTreeRunningCount,
		OwnerMotionEligibleCount,
		OwnerCurrentStallCount,
		MotionStallViolationCount,
		MaximumLowDisplacementSeconds,
		OwnerMembersWithTwoAttackRuns,
		OwnerMinimumAttackRunCount,
		BoolJson(bBootstrapReady),
		BoolJson(bStrictFlightReady),
		BoolJson(bRoleReady),
		BoolJson(bFinalSample));
}
