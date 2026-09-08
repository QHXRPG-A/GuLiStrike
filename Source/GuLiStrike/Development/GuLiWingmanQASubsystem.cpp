// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanQASubsystem.h"
#include "Development/GuLiWingmanQARoleProbes.h"

#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiWingmanReplenishmentController.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Battle/Relay/GuLiWingmanRelayServer.h"
#include "Battle/Relay/GuLiWingmanWorldValidator.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "GuLiStrike.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanQASubsystem)

namespace GuLiWingmanQA
{
	int32 DebugDrawMode = 0;

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

	FString GroupId(const FGuLiWingmanGroupHandle& Group)
	{
		return FString::Printf(TEXT("%s:%u:%u"),
			*Group.ShipInstanceId.ToString(EGuidFormats::Digits),
			Group.ShipGeneration, Group.GroupGeneration);
	}

	FString FlightId(const FGuLiWingmanFlightHandle& Flight)
	{
		return FString::Printf(TEXT("%s:%u"), *GroupId(Flight.Group), Flight.FlightIndex);
	}

	FString WingmanId(const FGuLiWingmanHandle& Wingman)
	{
		return FString::Printf(TEXT("%s:%u:%u"), *FlightId(Wingman.Flight),
			Wingman.MemberIndex, Wingman.EntityGeneration);
	}

	FString Number(const int64 Value)
	{
		return FString::Printf(TEXT("%lld"), Value);
	}

	FString UnsignedNumber(const uint64 Value)
	{
		return FString::Printf(TEXT("%llu"), Value);
	}

	bool EqualsId(const FString& Candidate, const FString& Requested)
	{
		return Candidate.Equals(Requested, ESearchCase::IgnoreCase);
	}

	UGuLiWingmanQASubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UGuLiWingmanQASubsystem>() : nullptr;
	}

	bool ParseProfile(const FString& Value)
	{
		return Value.Equals(TEXT("CoreWingmanSmoke"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("CoreWingman"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("GroundMachineIntegration"), ESearchCase::IgnoreCase);
	}

	bool ParseNetworkProfile(const FString& Value)
	{
		return Value.Equals(TEXT("Baseline"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("LagLoss"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("LeaseLoss"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("LateJoin"), ESearchCase::IgnoreCase);
	}

	FName DefaultRoleForNetworkProfile(const FString& Value)
	{
		if (Value.Equals(TEXT("LagLoss"), ESearchCase::IgnoreCase))
		{
			return TEXT("S6-CorrectionReverse");
		}
		if (Value.Equals(TEXT("LeaseLoss"), ESearchCase::IgnoreCase))
		{
			return TEXT("S7-LeaseLoss");
		}
		if (Value.Equals(TEXT("LateJoin"), ESearchCase::IgnoreCase))
		{
			return TEXT("S8B-MidCombatJoin");
		}
		return TEXT("Main-S0-S5");
	}

	void StatsCommand(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		if (const UGuLiWingmanQASubsystem* QA = GetSubsystem(World))
		{
			QA->LogStats();
		}
	}

	void UnitCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogGuLiWingmanQA, Warning,
				TEXT("Usage: gs.Wingman.Unit <guid:shipGen:groupGen:flight:slot:entityGen>"));
			return;
		}
		if (const UGuLiWingmanQASubsystem* QA = GetSubsystem(World))
		{
			QA->LogUnit(Args[0]);
		}
	}

	void GroupCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogGuLiWingmanQA, Warning,
				TEXT("Usage: gs.Wingman.Group <guid:shipGen:groupGen>"));
			return;
		}
		if (const UGuLiWingmanQASubsystem* QA = GetSubsystem(World))
		{
			QA->LogGroup(Args[0]);
		}
	}

	void NavCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogGuLiWingmanQA, Warning,
				TEXT("Usage: gs.Wingman.Nav <guid:shipGen:groupGen:flight>"));
			return;
		}
		if (const UGuLiWingmanQASubsystem* QA = GetSubsystem(World))
		{
			QA->LogNavigation(Args[0]);
		}
	}

	void DebugDrawCommand(const TArray<FString>& Args, UWorld* World)
	{
		(void)World;
		int32 Mode = INDEX_NONE;
		if (Args.Num() != 1 || !LexTryParseString(Mode, *Args[0])
			|| !UGuLiWingmanQASubsystem::SetDebugDrawMode(Mode))
		{
			UE_LOG(LogGuLiWingmanQA, Warning, TEXT("Usage: gs.Wingman.DebugDraw 0|1|2|3"));
			return;
		}
		UE_LOG(LogGuLiWingmanQA, Display, TEXT("Wingman debug draw mode=%d"), Mode);
	}

	void RelayStatsCommand(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		if (const UGuLiWingmanQASubsystem* QA = GetSubsystem(World))
		{
			QA->LogRelayStats();
		}
	}

	void LeaseWatchdogCommand(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		if (const UGuLiWingmanQASubsystem* QA = GetSubsystem(World))
		{
			QA->LogLeaseWatchdog();
		}
	}

	void QAStartCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2 || Args.Num() > 4
			|| !ParseProfile(Args[0]) || !ParseNetworkProfile(Args[1]))
		{
			UE_LOG(LogGuLiWingmanQA, Warning,
				TEXT("Usage: gs.Wingman.QA.Start <CoreWingmanSmoke|CoreWingman|GroundMachineIntegration> <Baseline|LagLoss|LeaseLoss|LateJoin> [SuiteRunRole] [CampaignId]"));
			return;
		}
		UGuLiWingmanQASubsystem* QA = GetSubsystem(World);
		if (!QA)
		{
			UE_LOG(LogGuLiWingmanQA, Error, TEXT("Wingman QA subsystem is unavailable."));
			return;
		}

		FGuLiWingmanQARunDescriptor Descriptor;
		Descriptor.AcceptanceProfile = Args[0];
		Descriptor.SuiteRunRole = Args.Num() >= 3
			? FName(*Args[2]) : DefaultRoleForNetworkProfile(Args[1]);
		Descriptor.CampaignId = Args.Num() >= 4 ? Args[3]
			: FString::Printf(TEXT("manual-%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
		Descriptor.RunId = FString::Printf(TEXT("%s-%u"),
			*Descriptor.SuiteRunRole.ToString(), FPlatformProcess::GetCurrentProcessId());
		Descriptor.PairId = Args[1];
		Descriptor.EndpointId = World->GetNetMode() == NM_Client
			? FString::Printf(TEXT("client-%u"), FPlatformProcess::GetCurrentProcessId())
			: TEXT("server");
		Descriptor.Map = World->GetPackage() ? World->GetPackage()->GetName() : TEXT("Unknown");
		Descriptor.NetMode = NetModeName(World->GetNetMode());
		Descriptor.Seed = 1977u;
		Descriptor.bServerEndpoint = World->GetNetMode() != NM_Client;
		FString Error;
		if (!QA->StartSession(Descriptor, 0.0, Error))
		{
			UE_LOG(LogGuLiWingmanQA, Error, TEXT("Wingman QA start failed: %s"), *Error);
		}
	}

	void QAStopCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty())
		{
			UE_LOG(LogGuLiWingmanQA, Warning, TEXT("Usage: gs.Wingman.QA.Stop"));
			return;
		}
		if (UGuLiWingmanQASubsystem* QA = GetSubsystem(World))
		{
			FGuLiWingmanAcceptanceRunEvidence Evidence;
			FString Error;
			QA->StopSession(true, Evidence, Error);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs StatsConsoleCommand(
		TEXT("gs.Wingman.Stats"), TEXT("Print read-only Wingman world summary."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StatsCommand));
	FAutoConsoleCommandWithWorldAndArgs UnitConsoleCommand(
		TEXT("gs.Wingman.Unit"), TEXT("Inspect one stable Wingman identity."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UnitCommand));
	FAutoConsoleCommandWithWorldAndArgs GroupConsoleCommand(
		TEXT("gs.Wingman.Group"), TEXT("Inspect one simulation group."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GroupCommand));
	FAutoConsoleCommandWithWorldAndArgs NavConsoleCommand(
		TEXT("gs.Wingman.Nav"), TEXT("Inspect one flight navigation state."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NavCommand));
	FAutoConsoleCommandWithWorldAndArgs DebugDrawConsoleCommand(
		TEXT("gs.Wingman.DebugDraw"), TEXT("Set accepted-state debug draw mode 0..3."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DebugDrawCommand));
	FAutoConsoleCommandWithWorldAndArgs RelayStatsConsoleCommand(
		TEXT("gs.Wingman.RelayStats"), TEXT("Print Relay acceptance and lease summary."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RelayStatsCommand));
	FAutoConsoleCommandWithWorldAndArgs LeaseWatchdogConsoleCommand(
		TEXT("gs.Wingman.LeaseWatchdog"), TEXT("Print 1 Hz watchdog state and events."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LeaseWatchdogCommand));
	FAutoConsoleCommandWithWorldAndArgs QAStartConsoleCommand(
		TEXT("gs.Wingman.QA.Start"), TEXT("Start one fail-closed Wingman QA evidence run."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QAStartCommand));
	FAutoConsoleCommandWithWorldAndArgs QAStopConsoleCommand(
		TEXT("gs.Wingman.QA.Stop"), TEXT("Finalize the active Wingman QA evidence run."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QAStopCommand));
}

bool UGuLiWingmanQASubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	const UPackage* Package = World ? World->GetPackage() : nullptr;
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& Package && Package->HasAnyPackageFlags(PKG_ContainsMap)
		&& !Package->GetName().StartsWith(TEXT("/Temp/"));
#endif
}

void UGuLiWingmanQASubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	bCommandLineStartPending = FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanQA"));
#endif
}

void UGuLiWingmanQASubsystem::Deinitialize()
{
	if (EvidenceWriter.IsActive())
	{
		FGuLiWingmanAcceptanceRunEvidence Evidence;
		FString Error;
		StopSession(false, Evidence, Error);
	}
	Super::Deinitialize();
}

void UGuLiWingmanQASubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	if (bCommandLineStartPending && GetWorld() && GetWorld()->HasBegunPlay())
	{
		FString RequestedEndpoint;
		FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQAEndpoint="), RequestedEndpoint);
		const bool bClientEndpoint = !RequestedEndpoint.IsEmpty()
			&& !RequestedEndpoint.Equals(TEXT("server"), ESearchCase::IgnoreCase);
		if (bClientEndpoint && GetWorld()->GetNetMode() != NM_Client)
		{
			// Packaged clients briefly run the Entry map as Standalone while the network
			// travel is pending. Leave the command-line start armed so only the replicated
			// destination world can own this endpoint's formal evidence.
			return;
		}
		// World subsystems initialize while the map is still loading. Starting the
		// measurement there would include blocking map setup in the fixed-step window.
		bCommandLineStartPending = false;
		TryStartFromCommandLine();
	}
	DrawDiagnostics();
	if (!EvidenceWriter.IsActive())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextSampleWallSeconds)
	{
		EmitSample(false);
		NextSampleWallSeconds = Now + 1.0;
	}
	if (DurationSeconds > 0.0 && Now - StartedWallSeconds >= DurationSeconds)
	{
		FGuLiWingmanAcceptanceRunEvidence Evidence;
		FString Error;
		const bool bPassed = StopSession(true, Evidence, Error);
		if (bAutoExit)
		{
			FGenericPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 2);
		}
	}
}

TStatId UGuLiWingmanQASubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiWingmanQASubsystem, STATGROUP_Tickables);
}

bool UGuLiWingmanQASubsystem::StartSession(
	const FGuLiWingmanQARunDescriptor& Descriptor,
	const double InDurationSeconds,
	FString& OutError)
{
	if (EvidenceWriter.IsActive())
	{
		OutError = TEXT("A Wingman QA session is already active in this World.");
		return false;
	}
	FGuLiWingmanQARunDescriptor Effective = Descriptor;
	if (Effective.OutputRoot.IsEmpty())
	{
		Effective.OutputRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WingmanQA"));
	}
	if (Effective.InsightsTracePath.IsEmpty())
	{
		Effective.InsightsTracePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			Effective.OutputRoot, Effective.CampaignId, Effective.RunId,
			FString::Printf(TEXT("endpoint-%s.utrace"), *Effective.EndpointId)));
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Effective.InsightsTracePath), true);
	if (FTraceAuxiliary::IsConnected())
	{
		const FString ExistingTrace = FTraceAuxiliary::GetTraceDestinationString();
		if (!ExistingTrace.IsEmpty())
		{
			Effective.InsightsTracePath = FPaths::ConvertRelativePathToFull(ExistingTrace);
		}
	}
	else
	{
		FTraceAuxiliary::FOptions Options;
		Options.bTruncateFile = true;
		bOwnsTrace = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File,
			*Effective.InsightsTracePath, TEXT("default,cpu,frame,bookmark,net"), &Options,
			LogGuLiWingmanQA);
		if (!bOwnsTrace)
		{
			UE_LOG(LogGuLiWingmanQA, Warning,
				TEXT("Could not start Insights trace; EVIDENCE_COMPLETE will fail closed: %s"),
				*Effective.InsightsTracePath);
		}
	}

	FGuLiWingmanQAInvariantRegistry::Reset();
	if (!EvidenceWriter.Start(Effective, OutError))
	{
		if (bOwnsTrace)
		{
			FTraceAuxiliary::Stop();
			bOwnsTrace = false;
		}
		return false;
	}
	StartedWallSeconds = FPlatformTime::Seconds();
	NextSampleWallSeconds = StartedWallSeconds;
	DurationSeconds = InDurationSeconds > 0.0 ? FMath::Clamp(InDurationSeconds, 10.0, 7200.0) : 0.0;
	SampleIndex = 0u;
	PreviousMinimumAcceptedFrame = 0u;
	CommanderFixedStepAtStart = 0u;
	CommanderDroppedStepAtStart = 0u;
	NetworkMetricSampleCount = 0u;
	NetworkIncomingBytesPerSecondSum = 0u;
	NetworkOutgoingBytesPerSecondSum = 0u;
	InitialProcessPhysicalBytes = FPlatformMemory::GetStats().UsedPhysical;
	CombatFixtureWingman = FGuLiWingmanHandle{};
	CombatFixtureSource = FGuLiTargetHandle{};
	CombatFixtureTarget = FGuLiTargetHandle{};
	CombatFixtureDamageEventId = FGuid{};
	CombatFixtureShotId = FGuid{};
	CombatFixtureDeathEventId = FGuid{};
	CombatFixtureRewardEventId = FGuid{};
	CombatFixtureDeathServerSeconds = 0.0;
	CombatFixtureReplenishDueServerSeconds = 0.0;
	CombatFixtureHealthBefore = 0.0f;
	CombatFixtureHealthAfter = 0.0f;
	CombatFixtureCommitOrdinal = 0u;
	CombatFixtureReplenishScheduleId = 0u;
	bCombatFixtureAttempted = false;
	bObservedCombatLedgerSingleCommit = false;
	bObservedDeathRosterCommit = false;
	bObservedReplenishmentSchedule = false;
	bObservedReplenishAfter15Seconds = false;
	bRoleProbeAttempted = false;
	ObservedRoleProbeGates.Reset();
	RoleProbeError.Reset();
	S8FixtureWingman = FGuLiWingmanHandle{};
	S8FixtureSource = FGuLiTargetHandle{};
	S8FixtureTarget = FGuLiTargetHandle{};
	S8NonLethalDamageEventId = FGuid{};
	S8LethalDamageEventId = FGuid{};
	S8ObserverWoundedWingman = FGuLiWingmanHandle{};
	S8TransferGroup = FGuLiWingmanGroupHandle{};
	S8TransferCandidateGuid = FGuid{};
	S8TransferOfferRevision = 0u;
	S8ObserverFirstCutId = 0u;
	S8TransferPreviewCutId = 0u;
	S8ObserverFirstBootstrapElapsedSeconds = -1.0;
	S8ObserverDetectedServerSeconds = 0.0;
	S8FixtureRemainingHealth = 0.0f;
	bS8FixtureAttempted = false;
	bS8NonLethalCommitted = false;
	bS8NonLethalPublicCutReady = false;
	bS8ObserverDetectedInWindow = false;
	bS8LethalCommitted = false;
	bS8LethalPublicCutReady = false;
	bS8TransferOfferStarted = false;
	bS8TransferOfferDelivered = false;
	bS8TransferRecoveredActive = false;
	bS8ObserverInitialCutAllFull = false;
	bS8ObserverInitialCutHadWoundedAlive = false;
	bS8ObserverAtomicBootstrapObserved = false;
	bS8ObserverNoAuthorityObserved = false;
	bS8ObserverDeathCutObserved = false;
	ExpectedClientEndpoints = FMath::Max(0, Effective.ExpectedClientEndpoints);
	ActiveRole = Effective.SuiteRunRole;
	bFinalized = false;
	if (const UWorld* World = GetWorld())
	{
		if (const UGuLiBattleAuthoritySubsystem* Commander =
			World->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
		{
			CommanderFixedStepAtStart = Commander->GetServerSimTick();
			CommanderDroppedStepAtStart = Commander->GetDroppedFixedStepCount();
		}
	}

	FGuLiWingmanQAEvent Event;
	Event.Stream = EGuLiWingmanQALogStream::WingmanQA;
	Event.Event = TEXT("QA_RUN_BEGIN");
	Event.ServerTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Event.Fields.Add(TEXT("trace_path"), EvidenceWriter.GetInsightsTracePath());
	EvidenceWriter.RecordEvent(Event, OutError);
	Event.Event = TEXT("PROFILE_SELECTED");
	Event.Fields.Reset();
	Event.Fields.Add(TEXT("message"), Effective.AcceptanceProfile);
	EvidenceWriter.RecordEvent(Event, OutError);
	Event.Event = TEXT("PROCESS_READY");
	Event.Fields.Reset();
	Event.Fields.Add(TEXT("process_role"), Effective.bServerEndpoint ? TEXT("server") : TEXT("client"));
	EvidenceWriter.RecordEvent(Event, OutError);
	Event.Event = TEXT("MAP_LOADED");
	Event.Fields.Reset();
	Event.Fields.Add(TEXT("message"), Effective.Map);
	EvidenceWriter.RecordEvent(Event, OutError);
	return true;
}

bool UGuLiWingmanQASubsystem::StopSession(
	const bool bScenarioPassed,
	FGuLiWingmanAcceptanceRunEvidence& OutEvidence,
	FString& OutError)
{
	if (!EvidenceWriter.IsActive())
	{
		OutError = TEXT("No active Wingman QA session in this World.");
		return false;
	}
	EmitSample(true);
	FGuLiWingmanQAEvent End;
	End.Stream = EGuLiWingmanQALogStream::WingmanQA;
	End.Event = TEXT("QA_RUN_END");
	End.ServerTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	End.Fields.Add(TEXT("scenario_passed"), bScenarioPassed ? TEXT("true") : TEXT("false"));
	FString EventError;
	EvidenceWriter.RecordEvent(End, EventError);
	if (bOwnsTrace)
	{
		FTraceAuxiliary::Stop();
		bOwnsTrace = false;
	}
	const bool bPassed = EvidenceWriter.Stop(bScenarioPassed, bScenarioPassed,
		OutEvidence, OutError);
	bFinalized = true;
	UE_LOG(LogGuLiWingmanQA, Display, TEXT("Wingman QA verdict role=%s passed=%d path=%s error=%s"),
		*OutEvidence.RoleId.ToString(), bPassed ? 1 : 0, *OutEvidence.EvidencePath,
		OutError.IsEmpty() ? TEXT("none") : *OutError);
	return bPassed;
}

void UGuLiWingmanQASubsystem::TryStartFromCommandLine()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanQA")))
	{
		return;
	}
	FGuLiWingmanQARunDescriptor Descriptor;
	FString Role = TEXT("Main-S0-S5");
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQACampaign="), Descriptor.CampaignId);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQARunId="), Descriptor.RunId);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQARole="), Role);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQAPair="), Descriptor.PairId);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQAEndpoint="), Descriptor.EndpointId);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQAProfile="), Descriptor.AcceptanceProfile);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQAOutput="), Descriptor.OutputRoot);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQATrace="), Descriptor.InsightsTracePath);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQASeed="), Descriptor.Seed);
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQAExpectedClients="),
		Descriptor.ExpectedClientEndpoints);
	double RequestedDuration = 0.0;
	FParse::Value(FCommandLine::Get(), TEXT("-GuLiWingmanQASeconds="), RequestedDuration);
	Descriptor.SuiteRunRole = FName(*Role);
	const UWorld* World = GetWorld();
	Descriptor.bServerEndpoint = World && World->GetNetMode() != NM_Client;
	Descriptor.NetMode = World ? GuLiWingmanQA::NetModeName(World->GetNetMode()) : TEXT("Unknown");
	Descriptor.Map = World && World->GetPackage() ? World->GetPackage()->GetName() : TEXT("Unknown");
	if (Descriptor.CampaignId.IsEmpty())
	{
		Descriptor.CampaignId = FString::Printf(TEXT("campaign-%s"),
			*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
	}
	if (Descriptor.RunId.IsEmpty())
	{
		Descriptor.RunId = FString::Printf(TEXT("%s-%u"), *Role,
			FPlatformProcess::GetCurrentProcessId());
	}
	if (Descriptor.PairId.IsEmpty())
	{
		Descriptor.PairId = Descriptor.RunId;
	}
	if (Descriptor.EndpointId.IsEmpty())
	{
		Descriptor.EndpointId = Descriptor.bServerEndpoint ? TEXT("server")
			: FString::Printf(TEXT("client-%u"), FPlatformProcess::GetCurrentProcessId());
	}
	if (Descriptor.AcceptanceProfile.IsEmpty())
	{
		Descriptor.AcceptanceProfile = TEXT("CoreWingman");
	}
	bAutoExit = FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanQAAutoExit"));
	FString Error;
	if (!StartSession(Descriptor, RequestedDuration, Error))
	{
		UE_LOG(LogGuLiWingmanQA, Error, TEXT("Command-line Wingman QA start failed: %s"), *Error);
		if (bAutoExit)
		{
			FGenericPlatformMisc::RequestExitWithStatus(true, 2);
		}
	}
#endif
}

void UGuLiWingmanQASubsystem::EmitSample(const bool bFinalSample)
{
	UWorld* World = GetWorld();
	if (!World || !EvidenceWriter.IsActive())
	{
		return;
	}
	++SampleIndex;
	const double Elapsed = FPlatformTime::Seconds() - StartedWallSeconds;
	const AGuLiBattleGameState* BattleState = World->GetGameState<AGuLiBattleGameState>();
	const FGuLiWingmanRelayAuthorityRegistry* Registry = BattleState
		? BattleState->GetWingmanRelayAuthorityRegistry() : nullptr;
	const TArray<FGuLiWingmanPublicBootstrapState>* PublicStates = BattleState
		? &BattleState->GetPublicWingmanBootstraps() : nullptr;
	const UGuLiWingmanSimulationSubsystem* Simulation =
		World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	const UGuLiBattleAuthoritySubsystem* Commander =
		World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	const bool bServer = World->GetNetMode() != NM_Client;

	int32 ShipCount = 0;
	const AGuLiStrikeShip* FirstShip = nullptr;
	for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed())
		{
			++ShipCount;
			FirstShip = FirstShip ? FirstShip : *It;
		}
	}
	const int32 GroupCount = PublicStates ? PublicStates->Num() : 0;
	int32 WellFormedBootstrapCount = 0;
	int32 ActiveGroupCount = 0;
	int32 AtomicCommittedCount = 0;
	int32 WingmanCount = 0;
	int32 StrictFlightGroupCount = 0;
	uint32 MinimumAcceptedFrame = MAX_uint32;
	uint64 ServerMovementWrites = 0u;
	bool bFormationContract = GroupCount > 0;
	if (PublicStates)
	{
		for (const FGuLiWingmanPublicBootstrapState& PublicState : *PublicStates)
		{
			WellFormedBootstrapCount += PublicState.IsWellFormed() ? 1 : 0;
			for (const FGuLiWingmanRosterEntry& Entry : PublicState.Bootstrap.Roster)
			{
				WingmanCount += Entry.bDead ? 0 : 1;
			}
			const FGuLiWingmanFormationRuntimeConfig& Formation =
				PublicState.Bootstrap.AbilityConfig.FormationRuntime;
			bFormationContract &= PublicState.Bootstrap.AbilityConfig.IsUsableByLeaseOwner()
				&& Formation.InnerRingSlots + Formation.OuterRingSlots == GULI_WINGMAN_GROUP_SIZE
				&& Formation.InnerRingSlots > 0u && Formation.OuterRingSlots > 0u
				&& Formation.InnerRingRadiusCentimeters > 0.0f
				&& Formation.OuterRingRadiusCentimeters > Formation.InnerRingRadiusCentimeters;
			// Public Bootstrap state is the client-visible six-scope truth.  Count it on
			// every endpoint; the durable Relay Registry intentionally exists only on
			// authority.  Keeping these observations behind Registry made late-join and
			// spectator clients report zero Wingmen despite holding a valid roster.
			const FGuLiWingmanRelayServer* Relay = Registry
				? Registry->FindGroup(PublicState.Group) : nullptr;
			if (!Relay)
			{
				continue;
			}
			ActiveGroupCount += Relay->GetLeaseState().Lifecycle
				== EGuLiWingmanGroupLifecycle::Active ? 1 : 0;
			AtomicCommittedCount += Relay->IsAtomicCandidateBatchCommitted() ? 1 : 0;
			ServerMovementWrites += Relay->GetServerWingmanMovementWriteCount();
			uint8 FlightMask = 0u;
			uint32 GroupMinimumFrame = MAX_uint32;
			for (uint8 Flight = 0u; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
			{
				const uint32 Sequence = Relay->GetAcceptedSequenceForFlight(Flight);
				const uint32 Frame = Relay->GetLastAcceptedFrameForFlight(Flight);
				if (Sequence > 0u && Frame > 0u)
				{
					FlightMask |= static_cast<uint8>(1u << Flight);
				}
				GroupMinimumFrame = FMath::Min(GroupMinimumFrame, Frame);
			}
			if (FlightMask == static_cast<uint8>((1u << GULI_WINGMAN_FLIGHT_COUNT) - 1u))
			{
				++StrictFlightGroupCount;
				MinimumAcceptedFrame = FMath::Min(MinimumAcceptedFrame, GroupMinimumFrame);
			}
		}
	}
	if (MinimumAcceptedFrame == MAX_uint32)
	{
		MinimumAcceptedFrame = 0u;
	}

	bool bNavReady = false;
	FString NavMessage = TEXT("No ship location or usable authored FlightNav volume.");
	if (const UGuLiFlightNavigationSubsystem* FlightNav =
		World->GetSubsystem<UGuLiFlightNavigationSubsystem>(); FirstShip && FlightNav)
	{
		bNavReady = FlightNav->HasUsableNavigationAt(FirstShip->GetActorLocation(), NavMessage);
	}
	const GuLiWingmanWorldValidation::FListenSmokeDiagnostics WorldValidation =
		GuLiWingmanWorldValidation::GetListenSmokeDiagnostics();
	bNavReady |= WorldValidation.AcceptedCount > 0u;

	const int32 CommanderCount = Commander ? Commander->GetAuthoritativeMemberCount() : 0;
	const uint32 CommanderSteps = Commander ? Commander->GetServerSimTick() : 0u;
	const uint64 CommanderDropped = Commander ? Commander->GetDroppedFixedStepCount() : 0u;
	const int32 OwnerMassCount = Simulation ? Simulation->GetTotalOwnedEntityCount() : 0;
	const APlayerController* LocalPlayerController = World->GetFirstPlayerController();
	const AGuLiBattlePlayerState* LocalPlayerState = LocalPlayerController
		? LocalPlayerController->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const UGuLiWingmanRelayComponent* LocalRelay = LocalPlayerController
		? LocalPlayerController->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
	const bool bLocalObserver = !bServer && LocalPlayerState
		&& LocalPlayerState->GetBattleRole() == EGuLiCommanderRole::Observer
		&& LocalPlayerState->IsOnlyASpectator() && LocalPlayerController->GetPawn() == nullptr;
	const bool bObserverHasNoPrivateAuthority = bLocalObserver && OwnerMassCount == 0
		&& (!LocalRelay || (!LocalRelay->GetRelayState().Lease.Group.IsValid()
			&& !LocalRelay->GetLastClientBootstrap().Commit.Group.IsValid()));
	const AGuLiWingmanPresentationActor* Presentation = nullptr;
	if (!bServer)
	{
		for (TActorIterator<AGuLiWingmanPresentationActor> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				Presentation = *It;
				break;
			}
		}
	}
	bool bAllPublicCutsApplied = GroupCount > 0 && WellFormedBootstrapCount == GroupCount
		&& Presentation != nullptr;
	if (bAllPublicCutsApplied && PublicStates)
	{
		for (const FGuLiWingmanPublicBootstrapState& PublicState : *PublicStates)
		{
			bAllPublicCutsApplied &= Presentation->HasAppliedBootstrap(
				PublicState.Group, PublicState.Bootstrap.Commit.CutId);
		}
	}
	const bool bObserverAtomicNow = bObserverHasNoPrivateAuthority && bAllPublicCutsApplied;
	if (bObserverAtomicNow && !bS8ObserverAtomicBootstrapObserved)
	{
		bS8ObserverAtomicBootstrapObserved = true;
		bS8ObserverNoAuthorityObserved = true;
		S8ObserverFirstBootstrapElapsedSeconds = Elapsed;
		S8ObserverFirstCutId = PublicStates && !PublicStates->IsEmpty()
			? (*PublicStates)[0].Bootstrap.Commit.CutId : 0u;
		bS8ObserverInitialCutAllFull = true;
		if (PublicStates)
		{
			for (const FGuLiWingmanPublicBootstrapState& PublicState : *PublicStates)
			{
				bS8ObserverInitialCutAllFull &= PublicState.Bootstrap.Dead.IsEmpty();
				for (const FGuLiWingmanHealthEntry& Health : PublicState.Bootstrap.Health)
				{
					bS8ObserverInitialCutAllFull &=
						Health.CurrentHealthPermille == Health.MaximumHealthPermille;
					if (!S8ObserverWoundedWingman.IsValid()
						&& Health.CurrentHealthPermille > 0u
						&& Health.CurrentHealthPermille < Health.MaximumHealthPermille)
					{
						const FGuLiWingmanRosterEntry* Roster =
							PublicState.Bootstrap.Roster.FindByPredicate(
								[&Health](const FGuLiWingmanRosterEntry& Entry)
								{
									return Entry.Wingman == Health.Wingman && !Entry.bDead;
								});
						if (Roster)
						{
							S8ObserverWoundedWingman = Health.Wingman;
							bS8ObserverInitialCutHadWoundedAlive = true;
						}
					}
				}
			}
		}
	}
	if (bS8ObserverInitialCutHadWoundedAlive && PublicStates)
	{
		for (const FGuLiWingmanPublicBootstrapState& PublicState : *PublicStates)
		{
			if (PublicState.Group != S8ObserverWoundedWingman.Flight.Group
				|| !Presentation || !Presentation->HasAppliedBootstrap(
					PublicState.Group, PublicState.Bootstrap.Commit.CutId))
			{
				continue;
			}
			const FGuLiWingmanRosterEntry* Roster = PublicState.Bootstrap.Roster.FindByPredicate(
				[this](const FGuLiWingmanRosterEntry& Entry)
				{
					return Entry.Wingman == S8ObserverWoundedWingman;
				});
			const FGuLiWingmanHealthEntry* Health = PublicState.Bootstrap.Health.FindByPredicate(
				[this](const FGuLiWingmanHealthEntry& Entry)
				{
					return Entry.Wingman == S8ObserverWoundedWingman;
				});
			bS8ObserverDeathCutObserved |= Roster && Roster->bDead && Health
				&& Health->CurrentHealthPermille == 0u
				&& PublicState.Bootstrap.Dead.Contains(S8ObserverWoundedWingman);
		}
	}
	const UNetDriver* NetDriver = World->GetNetDriver();
	uint64 IncomingBytesPerSecond = 0u;
	uint64 OutgoingBytesPerSecond = 0u;
	if (NetDriver)
	{
		// UNetDriver's aggregate rates stay zero unless optional engine stats collection is
		// enabled. UNetConnection always maintains its wire-rate counters, so summing the
		// active connection(s) records real compressed transport traffic without changing
		// production net-driver settings. A client has one ServerConnection; a server sums
		// all of its ClientConnections.
		if (const UNetConnection* ServerConnection = NetDriver->ServerConnection)
		{
			IncomingBytesPerSecond = static_cast<uint64>(FMath::Max(0, ServerConnection->InBytesPerSecond));
			OutgoingBytesPerSecond = static_cast<uint64>(FMath::Max(0, ServerConnection->OutBytesPerSecond));
		}
		else
		{
			for (const UNetConnection* ClientConnection : NetDriver->ClientConnections)
			{
				if (!ClientConnection)
				{
					continue;
				}
				IncomingBytesPerSecond += static_cast<uint64>(FMath::Max(0, ClientConnection->InBytesPerSecond));
				OutgoingBytesPerSecond += static_cast<uint64>(FMath::Max(0, ClientConnection->OutBytesPerSecond));
			}
		}
	}
	if (NetDriver)
	{
		++NetworkMetricSampleCount;
		NetworkIncomingBytesPerSecondSum += IncomingBytesPerSecond;
		NetworkOutgoingBytesPerSecondSum += OutgoingBytesPerSecond;
	}
	const bool bProtocolVersionsMatch = GULI_WINGMAN_PROTOCOL_VERSION == 8u
		&& (!BattleState || BattleState->GetProtocolVersion() == GULI_BATTLE_PROTOCOL_VERSION);
	const bool bExpectedGroupCount = GroupCount > 0
		&& (!bServer || ExpectedClientEndpoints <= 0
			|| GroupCount == FMath::Min(ExpectedClientEndpoints, 4));
	const bool bAllActive = bExpectedGroupCount && ActiveGroupCount == GroupCount;
	const bool bAllBootstrap = bExpectedGroupCount && WellFormedBootstrapCount == GroupCount;
	const bool bAllAtomic = bExpectedGroupCount && AtomicCommittedCount == GroupCount;
	const bool bAllStrict = bExpectedGroupCount && StrictFlightGroupCount == GroupCount;
	const bool bFramesProgressed = bAllStrict && PreviousMinimumAcceptedFrame > 0u
		&& MinimumAcceptedFrame > PreviousMinimumAcceptedFrame;
	if (MinimumAcceptedFrame > 0u)
	{
		PreviousMinimumAcceptedFrame = MinimumAcceptedFrame;
	}
	const bool bRoleReady = bServer
		? (bProtocolVersionsMatch && bAllActive && bAllBootstrap && bAllAtomic && bAllStrict
			&& ServerMovementWrites == 0u)
		: (bProtocolVersionsMatch && (OwnerMassCount >= GULI_WINGMAN_GROUP_SIZE
			|| bObserverAtomicNow));

	TMap<FName, FString> CommonFields;
	CommonFields.Add(TEXT("sample_index"), FString::FromInt(SampleIndex));
	CommonFields.Add(TEXT("elapsed_seconds"), FString::Printf(TEXT("%.6f"), Elapsed));
	CommonFields.Add(TEXT("protocol"), FString::FromInt(GULI_WINGMAN_PROTOCOL_VERSION));
	CommonFields.Add(TEXT("match_epoch"), BattleState
		? FString::FromInt(static_cast<int32>(BattleState->GetMatchEpoch())) : TEXT("0"));
	CommonFields.Add(TEXT("ship_count"), FString::FromInt(ShipCount));
	CommonFields.Add(TEXT("wingman_count"), FString::FromInt(WingmanCount));
	CommonFields.Add(TEXT("commander_count"), FString::FromInt(CommanderCount));
	CommonFields.Add(TEXT("relay_group_count"), FString::FromInt(GroupCount));
	CommonFields.Add(TEXT("active_relay_group_count"), FString::FromInt(ActiveGroupCount));
	CommonFields.Add(TEXT("well_formed_bootstrap_count"), FString::FromInt(WellFormedBootstrapCount));
	CommonFields.Add(TEXT("atomic_committed_count"), FString::FromInt(AtomicCommittedCount));
	CommonFields.Add(TEXT("strict_flight_group_count"), FString::FromInt(StrictFlightGroupCount));
	CommonFields.Add(TEXT("minimum_accepted_frame"), FString::FromInt(
		static_cast<int32>(MinimumAcceptedFrame)));
	CommonFields.Add(TEXT("owner_mass_entity_count"), FString::FromInt(OwnerMassCount));
	CommonFields.Add(TEXT("server_wingman_motion_step_count"),
		GuLiWingmanQA::UnsignedNumber(ServerMovementWrites));
	CommonFields.Add(TEXT("sample_count"), FString::FromInt(
		static_cast<int32>(CommanderSteps - CommanderFixedStepAtStart)));
	const uint64 ProcessPhysicalBytes = FPlatformMemory::GetStats().UsedPhysical;
	CommonFields.Add(TEXT("memory_private_bytes"),
		GuLiWingmanQA::UnsignedNumber(ProcessPhysicalBytes));
	CommonFields.Add(TEXT("observer_no_private_authority"),
		bObserverHasNoPrivateAuthority ? TEXT("true") : TEXT("false"));
	CommonFields.Add(TEXT("observer_public_cuts_applied"),
		bAllPublicCutsApplied ? TEXT("true") : TEXT("false"));
	CommonFields.Add(TEXT("observer_first_cut_id"),
		GuLiWingmanQA::UnsignedNumber(S8ObserverFirstCutId));
	CommonFields.Add(TEXT("observer_first_bootstrap_elapsed_seconds"),
		FString::Printf(TEXT("%.6f"), S8ObserverFirstBootstrapElapsedSeconds));
	if (const APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		if (const AGuLiBattlePlayerState* PlayerState =
			PlayerController->GetPlayerState<AGuLiBattlePlayerState>())
		{
			CommonFields.Add(TEXT("slot"), FString::FromInt(PlayerState->GetBattleSlotIndex()));
			CommonFields.Add(TEXT("team"), StaticEnum<EGuLiTeam>()->GetNameStringByValue(
				static_cast<int64>(PlayerState->GetTeam())));
			CommonFields.Add(TEXT("authority_mode"),
				StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(
					static_cast<int64>(PlayerState->GetBattleRole())));
		}
		if (const APawn* Pawn = PlayerController->GetPawn())
		{
			CommonFields.Add(TEXT("position"), Pawn->GetActorLocation().ToCompactString());
			CommonFields.Add(TEXT("velocity"), Pawn->GetVelocity().ToCompactString());
		}
	}
	CommonFields.Add(TEXT("role_ready"), bRoleReady ? TEXT("true") : TEXT("false"));
	CommonFields.Add(TEXT("trace_path"), EvidenceWriter.GetInsightsTracePath());
	CommonFields.Add(TEXT("completed"), bFinalSample ? TEXT("true") : TEXT("false"));

	auto WriteEvent = [this, World, &CommonFields](const EGuLiWingmanQALogStream Stream,
		const FName EventName, TMap<FName, FString> Fields)
	{
		for (const TPair<FName, FString>& Field : CommonFields)
		{
			Fields.FindOrAdd(Field.Key) = Field.Value;
		}
		FGuLiWingmanQAEvent Event;
		Event.Stream = Stream;
		Event.Event = EventName;
		Event.ServerTimeSeconds = World->GetTimeSeconds();
		Event.Fields = MoveTemp(Fields);
		FString Error;
		EvidenceWriter.RecordEvent(Event, Error);
	};

	WriteEvent(EGuLiWingmanQALogStream::WingmanQA, TEXT("QA_CHECK"), {});
	WriteEvent(EGuLiWingmanQALogStream::Wingman, TEXT("QA_CHECK"), {});
	WriteEvent(EGuLiWingmanQALogStream::WingmanAI, TEXT("QA_CHECK"), {});
	WriteEvent(EGuLiWingmanQALogStream::WingmanRelay, TEXT("QA_CHECK"), {});
	WriteEvent(EGuLiWingmanQALogStream::BattleCombat, TEXT("QA_CHECK"), {});
	if (NetDriver)
	{
		TMap<FName, FString> IncomingNetworkFields;
		IncomingNetworkFields.Add(TEXT("bytes"),
			GuLiWingmanQA::UnsignedNumber(IncomingBytesPerSecond));
		IncomingNetworkFields.Add(TEXT("measurement_direction"), TEXT("inbound"));
		IncomingNetworkFields.Add(TEXT("stream_tag"), TEXT("UE.NetDriver.InRate"));
		WriteEvent(EGuLiWingmanQALogStream::WingmanNet, TEXT("QA_CHECK"),
			MoveTemp(IncomingNetworkFields));

		TMap<FName, FString> OutgoingNetworkFields;
		OutgoingNetworkFields.Add(TEXT("bytes"),
			GuLiWingmanQA::UnsignedNumber(OutgoingBytesPerSecond));
		OutgoingNetworkFields.Add(TEXT("measurement_direction"), TEXT("outbound"));
		OutgoingNetworkFields.Add(TEXT("stream_tag"), TEXT("UE.NetDriver.OutRate"));
		WriteEvent(EGuLiWingmanQALogStream::WingmanNet, TEXT("QA_CHECK"),
			MoveTemp(OutgoingNetworkFields));
	}
	else
	{
		WriteEvent(EGuLiWingmanQALogStream::WingmanNet, TEXT("QA_CHECK"), {});
	}
	TMap<FName, FString> NavFields;
	NavFields.Add(TEXT("validation_result"), bNavReady ? TEXT("valid") : TEXT("unavailable"));
	NavFields.Add(TEXT("message"), NavMessage);
	WriteEvent(EGuLiWingmanQALogStream::FlightNav,
		bNavReady ? FName(TEXT("NAV_READY")) : FName(TEXT("QA_CHECK")), MoveTemp(NavFields));
	TArray<FGuLiWingmanQAEvent> CombatFixtureEvents;
	// Observe at least one strict frame transition before the destructive fixture transaction.
	// This proves the 30 Hz owner path independently of death/replenishment roster revisions.
	AdvanceMainCombatFixture(bServer && bRoleReady && bFramesProgressed, CombatFixtureEvents);
	for (FGuLiWingmanQAEvent& Event : CombatFixtureEvents)
	{
		WriteEvent(Event.Stream, Event.Event, MoveTemp(Event.Fields));
	}
	TArray<FGuLiWingmanQAEvent> S8FixtureEvents;
	AdvanceS8Fixture(bServer && bRoleReady, S8FixtureEvents);
	for (FGuLiWingmanQAEvent& Event : S8FixtureEvents)
	{
		WriteEvent(Event.Stream, Event.Event, MoveTemp(Event.Fields));
	}
	const bool bObserverConvergedWithinTwoSeconds = bS8ObserverAtomicBootstrapObserved
		&& bS8ObserverNoAuthorityObserved
		&& S8ObserverFirstBootstrapElapsedSeconds >= 0.0
		&& S8ObserverFirstBootstrapElapsedSeconds <= 2.0;
	if (ActiveRole == TEXT("S8A-OpeningSpectator")
		&& bObserverConvergedWithinTwoSeconds && bS8ObserverInitialCutAllFull)
	{
		ObservedRoleProbeGates.Add(TEXT("OPENING_SPECTATOR_ATOMIC_BOOTSTRAP"));
	}
	else if (ActiveRole == TEXT("S8B-MidCombatJoin"))
	{
		if (bObserverConvergedWithinTwoSeconds && bS8ObserverInitialCutHadWoundedAlive)
		{
			ObservedRoleProbeGates.Add(TEXT("MID_COMBAT_JOIN_ATOMIC_BOOTSTRAP"));
		}
		if (bS8ObserverDeathCutObserved)
		{
			ObservedRoleProbeGates.Add(TEXT("LATEJOIN_DAMAGE_LEDGER_CONSISTENT"));
		}
	}
	else if (ActiveRole == TEXT("S8C-TransferPendingJoin")
		&& bObserverConvergedWithinTwoSeconds)
	{
		ObservedRoleProbeGates.Add(TEXT("TRANSFER_PENDING_JOIN_FROZEN_CUT"));
	}
	const bool bRoleProbeRunsHere =
		GuLiWingmanQARoleProbes::RequiresClientExecutionDomain(ActiveRole)
			? !bServer : bServer;
	if (bRoleProbeRunsHere && bRoleReady && !bRoleProbeAttempted
		&& GuLiWingmanQARoleProbes::Supports(ActiveRole))
	{
		bRoleProbeAttempted = true;
		FGuLiWingmanQARoleProbeResult Probe = GuLiWingmanQARoleProbes::Run(ActiveRole);
		ObservedRoleProbeGates.Append(Probe.PassedGateIds);
		RoleProbeError = MoveTemp(Probe.Error);
		for (FGuLiWingmanQAEvent& Event : Probe.Events)
		{
			WriteEvent(Event.Stream, Event.Event, MoveTemp(Event.Fields));
		}
		if (!RoleProbeError.IsEmpty())
		{
			TMap<FName, FString> FailureFields;
			FailureFields.Add(TEXT("message"), FString::Printf(
				TEXT("role_probe_failed role=%s error=%s"),
				*ActiveRole.ToString(), *RoleProbeError));
			WriteEvent(EGuLiWingmanQALogStream::WingmanQA, TEXT("QA_CHECK"),
				MoveTemp(FailureFields));
		}
	}

	FString GateError;
	if (bProtocolVersionsMatch)
	{
		// PROTOCOL_V7 is a frozen acceptance-schema key, not the current wire value.
		EvidenceWriter.RecordGate(TEXT("PROTOCOL_V7"), true, 1, GateError);
		EvidenceWriter.RecordGate(TEXT("WINGMAN_PROTOCOL_V8"), true, 1, GateError);
	}
	if (bServer && ServerMovementWrites == 0u)
	{
		EvidenceWriter.RecordGate(TEXT("SERVER_WINGMAN_MOTION_ZERO"), true, 1, GateError);
	}
	for (const TPair<FName, int64>& Invariant : FGuLiWingmanQAInvariantRegistry::Snapshot())
	{
		EvidenceWriter.RecordInvariant(Invariant.Key, Invariant.Value, GateError);
	}

	// Role-specific gates are emitted only after their production observations become true.
	// Missing observations remain missing and therefore fail closed during finalization.
	const FGuLiWingmanAcceptanceRoleDefinition* RoleDefinition =
		FGuLiWingmanAcceptanceCatalogV2::FindRole(ActiveRole);
	auto TryGate = [this, RoleDefinition, &GateError](const FName Gate, const bool bObserved)
	{
		if (bObserved && RoleDefinition && RoleDefinition->RequiredGateIds.Contains(Gate))
		{
			EvidenceWriter.RecordGate(Gate, true, 1, GateError);
		}
	};
	TryGate(TEXT("DEDICATED_SERVER_READY"),
		World->GetNetMode() == NM_DedicatedServer && World->GetNetDriver() != nullptr);
	TryGate(TEXT("BOOTSTRAP_SIX_SCOPE_ATOMIC"), bAllBootstrap && bAllAtomic);
	TryGate(TEXT("OWNER_30HZ_SIMULATION"), bFramesProgressed);
	TryGate(TEXT("FORMATION_DOUBLE_RING"), bFormationContract && bAllStrict);
	TryGate(TEXT("NAVIGATION_VALID"), bNavReady && bAllStrict);
	TryGate(TEXT("COMBAT_LEDGER_SINGLE_COMMIT"), bObservedCombatLedgerSingleCommit);
	TryGate(TEXT("DEATH_ROSTER_COMMIT"), bObservedDeathRosterCommit);
	TryGate(TEXT("REPLENISH_15_SECONDS"), bObservedReplenishAfter15Seconds);
	TryGate(TEXT("COMMANDER_1800_FIXED_STEPS"),
		CommanderSteps - CommanderFixedStepAtStart >= 1800u);
	TryGate(TEXT("COMMANDER_DROPPED_STEPS_ZERO"),
		Commander && CommanderDropped == CommanderDroppedStepAtStart && Elapsed >= 60.0);
	TryGate(TEXT("WINGMAN_FAULT_INJECTION_DISABLED"),
		!FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanFault")));
	TryGate(TEXT("COMMANDER_500_CONTROL"),
		ShipCount == 4 && CommanderCount == 500 && Elapsed >= 600.0);
	TryGate(TEXT("WINGMAN_COUNT_ZERO"), WingmanCount == 0 && Elapsed >= 600.0);
	TryGate(TEXT("FOUR_SHIPS_100_WINGMEN_500_COMMANDER"),
		ShipCount == 4 && WingmanCount == 100 && CommanderCount == 500);
	TryGate(TEXT("TEN_MINUTE_PERFORMANCE_CAPTURE"), Elapsed >= 600.0);
	TryGate(TEXT("NETWORK_BUDGET_CAPTURE"),
		Elapsed >= 600.0 && NetworkMetricSampleCount >= 540u
		&& (NetworkIncomingBytesPerSecondSum > 0u || NetworkOutgoingBytesPerSecondSum > 0u)
		&& ProcessPhysicalBytes > 0u && InitialProcessPhysicalBytes > 0u);
	if (RoleDefinition)
	{
		for (const FName Gate : RoleDefinition->RequiredGateIds)
		{
			TryGate(Gate, ObservedRoleProbeGates.Contains(Gate));
		}
	}
}

void UGuLiWingmanQASubsystem::AdvanceMainCombatFixture(
	const bool bCoreReady,
	TArray<FGuLiWingmanQAEvent>& OutEvents)
{
	OutEvents.Reset();
#if UE_BUILD_SHIPPING
	(void)bCoreReady;
	return;
#else
	UWorld* World = GetWorld();
	const FGuLiWingmanAcceptanceRoleDefinition* RoleDefinition =
		FGuLiWingmanAcceptanceCatalogV2::FindRole(ActiveRole);
	const bool bRoleRequiresFixture = RoleDefinition
		&& (RoleDefinition->RequiredGateIds.Contains(TEXT("COMBAT_LEDGER_SINGLE_COMMIT"))
			|| RoleDefinition->RequiredGateIds.Contains(TEXT("DEATH_ROSTER_COMMIT"))
			|| RoleDefinition->RequiredGateIds.Contains(TEXT("REPLENISH_15_SECONDS")));
	if (!World || World->GetNetMode() == NM_Client || !bRoleRequiresFixture
		|| !FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanQA")))
	{
		return;
	}

	const AGuLiBattleGameState* BattleState = World->GetGameState<AGuLiBattleGameState>();
	const FGuLiWingmanRelayAuthorityRegistry* Registry = BattleState
		? BattleState->GetWingmanRelayAuthorityRegistry() : nullptr;
	UGuLiDamageLedgerSubsystem* Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!BattleState || !Registry || !Ledger || BattleState->GetMatchEpoch() == 0u)
	{
		return;
	}

	const double NowSeconds = static_cast<double>(World->GetTimeSeconds());
	auto QueueEvent = [&OutEvents, NowSeconds](
		const FName EventName,
		TMap<FName, FString> Fields)
	{
		FGuLiWingmanQAEvent& Event = OutEvents.AddDefaulted_GetRef();
		Event.Stream = EGuLiWingmanQALogStream::BattleCombat;
		Event.Event = EventName;
		Event.ServerTimeSeconds = NowSeconds;
		Event.Fields = MoveTemp(Fields);
	};
	auto AddTargetIdentity = [](TMap<FName, FString>& Fields,
		const TCHAR* Prefix,
		const FGuLiTargetHandle& Handle)
	{
		const FString PrefixString(Prefix);
		Fields.Add(FName(*(PrefixString + TEXT("_kind"))),
			StaticEnum<EGuLiTargetKind>()->GetNameStringByValue(static_cast<int64>(Handle.Kind)));
		Fields.Add(FName(*(PrefixString + TEXT("_id"))),
			Handle.AuthorityId.ToString(EGuidFormats::Digits));
		Fields.Add(FName(*(PrefixString + TEXT("_generation"))),
			FString::FromInt(static_cast<int32>(Handle.Generation)));
	};
	auto MakeCombatFields = [this, &AddTargetIdentity]()
	{
		TMap<FName, FString> Fields;
		AddTargetIdentity(Fields, TEXT("source"), CombatFixtureSource);
		AddTargetIdentity(Fields, TEXT("target"), CombatFixtureTarget);
		Fields.Add(TEXT("flight_id"), GuLiWingmanQA::FlightId(CombatFixtureWingman.Flight));
		Fields.Add(TEXT("wingman_id"), GuLiWingmanQA::WingmanId(CombatFixtureWingman));
		Fields.Add(TEXT("entity_generation"), FString::FromInt(
			static_cast<int32>(CombatFixtureWingman.EntityGeneration)));
		Fields.Add(TEXT("shot_id"), CombatFixtureShotId.ToString(EGuidFormats::Digits));
		Fields.Add(TEXT("damage_event_id"),
			CombatFixtureDamageEventId.ToString(EGuidFormats::Digits));
		Fields.Add(TEXT("damage"), FString::Printf(TEXT("%.3f"), CombatFixtureHealthBefore));
		Fields.Add(TEXT("health_before"), FString::Printf(TEXT("%.3f"), CombatFixtureHealthBefore));
		Fields.Add(TEXT("health_after"), FString::Printf(TEXT("%.3f"), CombatFixtureHealthAfter));
		Fields.Add(TEXT("commit_count"), GuLiWingmanQA::UnsignedNumber(CombatFixtureCommitOrdinal));
		if (CombatFixtureDeathEventId.IsValid())
		{
			Fields.Add(TEXT("death_event_id"),
				CombatFixtureDeathEventId.ToString(EGuidFormats::Digits));
		}
		if (CombatFixtureRewardEventId.IsValid())
		{
			Fields.Add(TEXT("reward_event_id"),
				CombatFixtureRewardEventId.ToString(EGuidFormats::Digits));
		}
		return Fields;
	};

	if (!bCombatFixtureAttempted && bCoreReady)
	{
		TArray<FGuLiCombatTargetSnapshot> Snapshots;
		Ledger->GetTargetSnapshots(Snapshots);
		const FGuLiCombatTargetSnapshot* SelectedTarget = nullptr;
		FGuLiWingmanHandle SelectedWingman;
		for (const FGuLiCombatTargetSnapshot& Candidate : Snapshots)
		{
			if (Candidate.Handle.Kind != EGuLiTargetKind::Wingman || !Candidate.bAlive
				|| Candidate.Team == EGuLiTeam::Unassigned)
			{
				continue;
			}
			for (const FGuLiWingmanPublicBootstrapState& PublicState
				: BattleState->GetPublicWingmanBootstraps())
			{
				const FGuLiWingmanRosterEntry* Entry = PublicState.Bootstrap.Roster.FindByPredicate(
					[&Candidate](const FGuLiWingmanRosterEntry& RosterEntry)
					{
						return !RosterEntry.bDead
							&& GuLiCombatTargets::MakeWingmanTargetHandle(RosterEntry.Wingman)
								== Candidate.Handle;
					});
				if (Entry)
				{
					SelectedTarget = &Candidate;
					SelectedWingman = Entry->Wingman;
					break;
				}
			}
			if (SelectedTarget)
			{
				break;
			}
		}

		const FGuLiCombatTargetSnapshot* SelectedSource = nullptr;
		if (SelectedTarget)
		{
			for (const FGuLiCombatTargetSnapshot& Candidate : Snapshots)
			{
				if (Candidate.Handle.Kind == EGuLiTargetKind::Ship && Candidate.bAlive
					&& Candidate.Team != EGuLiTeam::Unassigned
					&& Candidate.Team != SelectedTarget->Team)
				{
					SelectedSource = &Candidate;
					break;
				}
			}
		}

		if (SelectedSource && SelectedTarget && SelectedWingman.IsValid())
		{
			bCombatFixtureAttempted = true;
			CombatFixtureWingman = SelectedWingman;
			CombatFixtureSource = SelectedSource->Handle;
			CombatFixtureTarget = SelectedTarget->Handle;
			CombatFixtureHealthBefore = SelectedTarget->Health;
			const uint32 MatchEpoch = BattleState->GetMatchEpoch();
			CombatFixtureDamageEventId = FGuid(
				0x5141444du,
				MatchEpoch,
				CombatFixtureTarget.AuthorityId.A ^ CombatFixtureTarget.LocalId,
				CombatFixtureTarget.AuthorityId.B ^ CombatFixtureTarget.Generation);
			CombatFixtureShotId = FGuid(
				0x51415348u,
				MatchEpoch,
				CombatFixtureTarget.AuthorityId.C ^ CombatFixtureTarget.LocalId,
				CombatFixtureTarget.AuthorityId.D ^ CombatFixtureTarget.Generation);

			FGuLiDamageRequest Request;
			Request.MatchEpoch = MatchEpoch;
			Request.DamageEventId = CombatFixtureDamageEventId;
			Request.ShotId = CombatFixtureShotId;
			Request.Source = CombatFixtureSource;
			Request.Target = CombatFixtureTarget;
			Request.Damage = FMath::Max(1.0f, CombatFixtureHealthBefore);
			Request.HitLocation = SelectedTarget->Location;
			const uint64 CommitCountBefore = Ledger->GetCommitCount();
			const uint64 DeathCountBefore = Ledger->GetDeathCommitCount();
			const FGuLiDamageCommitResult First = Ledger->CommitDamage(Request);
			const FGuLiDamageCommitResult Duplicate = Ledger->CommitDamage(Request);
			CombatFixtureHealthAfter = First.RemainingHealth;
			CombatFixtureCommitOrdinal = First.CommitOrdinal;
			CombatFixtureDeathEventId = First.DeathEventId;
			CombatFixtureRewardEventId = First.RewardEventId;
			CombatFixtureDeathServerSeconds = NowSeconds;
			bObservedCombatLedgerSingleCommit = First.Status == EGuLiDamageCommitStatus::Committed
				&& Duplicate.Status == EGuLiDamageCommitStatus::Duplicate
				&& First.CommitOrdinal != 0u
				&& Duplicate.CommitOrdinal == First.CommitOrdinal
				&& Ledger->GetCommitCount() == CommitCountBefore + 1u;

			if (First.Status == EGuLiDamageCommitStatus::Committed)
			{
				QueueEvent(TEXT("DAMAGE_APPLIED"), MakeCombatFields());
			}
			if (Duplicate.Status == EGuLiDamageCommitStatus::Duplicate)
			{
				QueueEvent(TEXT("DAMAGE_DEDUPED"), MakeCombatFields());
			}
			FGuLiDeathCommitRecord DeathRecord;
			const bool bDeathCommitted = First.bKilled && First.DeathEventId.IsValid()
				&& Ledger->GetDeathCommitCount() == DeathCountBefore + 1u
				&& Ledger->TryGetDeathRecord(First.DeathEventId, DeathRecord)
				&& DeathRecord.DamageEventId == Request.DamageEventId
				&& DeathRecord.Target == Request.Target;
			if (!bObservedCombatLedgerSingleCommit || !bDeathCommitted)
			{
				TMap<FName, FString> FailureFields = MakeCombatFields();
				FailureFields.Add(TEXT("reject_reason"), FString::Printf(
					TEXT("first=%s duplicate=%s killed=%d death_record=%d"),
					*StaticEnum<EGuLiDamageCommitStatus>()->GetNameStringByValue(
						static_cast<int64>(First.Status)),
					*StaticEnum<EGuLiDamageCommitStatus>()->GetNameStringByValue(
						static_cast<int64>(Duplicate.Status)),
					First.bKilled ? 1 : 0, bDeathCommitted ? 1 : 0));
				QueueEvent(TEXT("QA_CHECK"), MoveTemp(FailureFields));
			}
		}
	}

	if (!bCombatFixtureAttempted || !CombatFixtureWingman.IsValid())
	{
		return;
	}

	const FGuLiWingmanRelayServer* Relay = Registry->FindGroup(
		CombatFixtureWingman.Flight.Group);
	if (!Relay)
	{
		return;
	}
	const FGuLiWingmanRosterEntry* CurrentSlot = Relay->GetRoster().FindByPredicate(
		[this](const FGuLiWingmanRosterEntry& Entry)
		{
			return Entry.Wingman.Flight.FlightIndex == CombatFixtureWingman.Flight.FlightIndex
				&& Entry.Wingman.MemberIndex == CombatFixtureWingman.MemberIndex;
		});
	FGuLiDeathCommitRecord DeathRecord;
	const bool bHasDeathRecord = CombatFixtureDeathEventId.IsValid()
		&& Ledger->TryGetDeathRecord(CombatFixtureDeathEventId, DeathRecord)
		&& DeathRecord.DamageEventId == CombatFixtureDamageEventId
		&& DeathRecord.Target == CombatFixtureTarget;
	if (!bObservedDeathRosterCommit && CurrentSlot
		&& CurrentSlot->Wingman == CombatFixtureWingman && CurrentSlot->bDead
		&& bHasDeathRecord)
	{
		bObservedDeathRosterCommit = true;
		QueueEvent(TEXT("HEALTH_ZERO"), MakeCombatFields());
		QueueEvent(TEXT("DEATH_COMMITTED"), MakeCombatFields());
	}

	if (bObservedDeathRosterCommit && !bObservedReplenishmentSchedule)
	{
		for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
		{
			const AGuLiStrikeShip* Ship = *It;
			if (!IsValid(Ship) || Ship->IsActorBeingDestroyed())
			{
				continue;
			}
			const FGuLiGroupAbilityConfigSnapshot& Config = Ship->GetGroupAbilityConfig();
			const FGuLiWingmanGroupHandle& Group = CombatFixtureWingman.Flight.Group;
			if (Config.ShipInstanceId != Group.ShipInstanceId
				|| Config.ShipGeneration != Group.ShipGeneration
				|| Config.GroupGeneration != Group.GroupGeneration)
			{
				continue;
			}
			bool bDue = false;
			if (Ship->TryGetWingmanReplenishmentSchedule(
				CombatFixtureWingman,
				CombatFixtureReplenishScheduleId,
				CombatFixtureReplenishDueServerSeconds,
				bDue))
			{
				bObservedReplenishmentSchedule = true;
				TMap<FName, FString> Fields = MakeCombatFields();
				Fields.Add(TEXT("replenish_schedule_id"),
					GuLiWingmanQA::UnsignedNumber(CombatFixtureReplenishScheduleId));
				Fields.Add(TEXT("replenish_due_time_ms"), FString::Printf(TEXT("%.3f"),
					CombatFixtureReplenishDueServerSeconds * 1000.0));
				QueueEvent(TEXT("REPLENISH_SCHEDULED"), MoveTemp(Fields));
				break;
			}
		}
	}

	if (!bObservedReplenishAfter15Seconds && bObservedReplenishmentSchedule && CurrentSlot
		&& !CurrentSlot->bDead
		&& CurrentSlot->Wingman.Flight == CombatFixtureWingman.Flight
		&& CurrentSlot->Wingman.MemberIndex == CombatFixtureWingman.MemberIndex
		&& CurrentSlot->Wingman.EntityGeneration == CombatFixtureWingman.EntityGeneration + 1u)
	{
		const double ScheduledDelay = CombatFixtureReplenishDueServerSeconds
			- CombatFixtureDeathServerSeconds;
		const bool bFullDelayObserved = ScheduledDelay + 0.001
			>= FGuLiWingmanReplenishmentController::DefaultDelaySeconds
			&& NowSeconds + 0.001 >= CombatFixtureReplenishDueServerSeconds;
		if (bFullDelayObserved)
		{
			bObservedReplenishAfter15Seconds = true;
			const FGuLiTargetHandle ReplacementTarget =
				GuLiCombatTargets::MakeWingmanTargetHandle(CurrentSlot->Wingman);
			TMap<FName, FString> Fields = MakeCombatFields();
			Fields[TEXT("wingman_id")] = GuLiWingmanQA::WingmanId(CurrentSlot->Wingman);
			Fields[TEXT("entity_generation")] = FString::FromInt(
				static_cast<int32>(CurrentSlot->Wingman.EntityGeneration));
			Fields[TEXT("target_id")] = ReplacementTarget.AuthorityId.ToString(EGuidFormats::Digits);
			Fields[TEXT("target_generation")] = FString::FromInt(
				static_cast<int32>(ReplacementTarget.Generation));
			Fields.Add(TEXT("replenish_schedule_id"),
				GuLiWingmanQA::UnsignedNumber(CombatFixtureReplenishScheduleId));
			Fields.Add(TEXT("replenish_due_time_ms"), FString::Printf(TEXT("%.3f"),
				CombatFixtureReplenishDueServerSeconds * 1000.0));
			QueueEvent(TEXT("REPLENISH_SPAWNED"), MoveTemp(Fields));
		}
	}
#endif
}

void UGuLiWingmanQASubsystem::AdvanceS8Fixture(
	const bool bCoreReady,
	TArray<FGuLiWingmanQAEvent>& OutEvents)
{
	OutEvents.Reset();
#if UE_BUILD_SHIPPING
	(void)bCoreReady;
	return;
#else
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client
		|| !ActiveRole.ToString().StartsWith(TEXT("S8"))
		|| !FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanQA")))
	{
		return;
	}
	AGuLiBattleGameState* BattleState = World->GetGameState<AGuLiBattleGameState>();
	FGuLiWingmanRelayAuthorityRegistry* Registry = BattleState
		? BattleState->GetWingmanRelayAuthorityRegistry() : nullptr;
	UGuLiDamageLedgerSubsystem* Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!BattleState || !Registry || !Ledger || BattleState->GetMatchEpoch() == 0u)
	{
		return;
	}
	const double NowSeconds = static_cast<double>(World->GetTimeSeconds());
	auto QueueEvent = [&OutEvents, NowSeconds](
		const EGuLiWingmanQALogStream Stream,
		const FName EventName,
		TMap<FName, FString> Fields = {})
	{
		FGuLiWingmanQAEvent& Event = OutEvents.AddDefaulted_GetRef();
		Event.Stream = Stream;
		Event.Event = EventName;
		Event.ServerTimeSeconds = NowSeconds;
		Event.Fields = MoveTemp(Fields);
	};
	auto FindObserver = [World]() -> APlayerController*
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			const AGuLiBattlePlayerState* PlayerState = Controller
				? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
			const UGuLiWingmanRelayComponent* Transport = Controller
				? Controller->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
			if (Controller && PlayerState && Controller->GetPawn() == nullptr
				&& PlayerState->GetBattleRole() == EGuLiCommanderRole::Observer
				&& PlayerState->IsOnlyASpectator()
				&& (!Transport || (Transport->GetServerRelay() == nullptr
					&& !Transport->GetRelayState().Lease.Group.IsValid())))
			{
				return Controller;
			}
		}
		return nullptr;
	};
	auto FindPublicState = [BattleState](const FGuLiWingmanGroupHandle& Group)
		-> const FGuLiWingmanPublicBootstrapState*
	{
		return BattleState->GetPublicWingmanBootstraps().FindByPredicate(
			[&Group](const FGuLiWingmanPublicBootstrapState& State)
			{
				return State.Group == Group;
			});
	};

	APlayerController* Observer = FindObserver();
	if (ActiveRole == TEXT("S8A-OpeningSpectator"))
	{
		if (!bCoreReady || !Observer)
		{
			return;
		}
		bool bAllFullAtomicCuts = BattleState->GetPublicWingmanBootstraps().Num() == 4;
		for (const FGuLiWingmanPublicBootstrapState& State
			: BattleState->GetPublicWingmanBootstraps())
		{
			bAllFullAtomicCuts &= State.IsWellFormed() && State.Lifecycle
				== EGuLiWingmanGroupLifecycle::Active && State.Bootstrap.Dead.IsEmpty();
			for (const FGuLiWingmanHealthEntry& Health : State.Bootstrap.Health)
			{
				bAllFullAtomicCuts &= Health.CurrentHealthPermille
					== Health.MaximumHealthPermille;
			}
		}
		if (bAllFullAtomicCuts)
		{
			ObservedRoleProbeGates.Add(TEXT("OPENING_SPECTATOR_ATOMIC_BOOTSTRAP"));
			if (!bS8FixtureAttempted)
			{
				bS8FixtureAttempted = true;
				QueueEvent(EGuLiWingmanQALogStream::WingmanNet,
					TEXT("S8_OPENING_SPECTATOR_OBSERVED"), {
						{TEXT("observer_player"), Observer->GetName()},
						{TEXT("relay_group_count"), TEXT("4")},
						{TEXT("candidate_accept_count"), TEXT("0")},
						{TEXT("fire_accept_count"), TEXT("0")}});
			}
		}
		return;
	}

	if (ActiveRole == TEXT("S8B-MidCombatJoin"))
	{
		if (!bS8FixtureAttempted && bCoreReady && !Observer)
		{
			TArray<FGuLiCombatTargetSnapshot> Snapshots;
			Ledger->GetTargetSnapshots(Snapshots);
			const FGuLiCombatTargetSnapshot* Target = nullptr;
			const FGuLiCombatTargetSnapshot* Source = nullptr;
			for (const FGuLiCombatTargetSnapshot& Candidate : Snapshots)
			{
				if (Candidate.Handle.Kind != EGuLiTargetKind::Wingman || !Candidate.bAlive
					|| Candidate.Team == EGuLiTeam::Unassigned)
				{
					continue;
				}
				for (const FGuLiWingmanPublicBootstrapState& State
					: BattleState->GetPublicWingmanBootstraps())
				{
					const FGuLiWingmanRosterEntry* Entry = State.Bootstrap.Roster.FindByPredicate(
						[&Candidate](const FGuLiWingmanRosterEntry& Roster)
						{
							return !Roster.bDead
								&& GuLiCombatTargets::MakeWingmanTargetHandle(Roster.Wingman)
									== Candidate.Handle;
						});
					if (Entry)
					{
						Target = &Candidate;
						S8FixtureWingman = Entry->Wingman;
						break;
					}
				}
				if (Target)
				{
					break;
				}
			}
			if (Target)
			{
				for (const FGuLiCombatTargetSnapshot& Candidate : Snapshots)
				{
					if (Candidate.Handle.Kind == EGuLiTargetKind::Ship && Candidate.bAlive
						&& Candidate.Team != EGuLiTeam::Unassigned
						&& Candidate.Team != Target->Team)
					{
						Source = &Candidate;
						break;
					}
				}
			}
			if (Source && Target && S8FixtureWingman.IsValid())
			{
				bS8FixtureAttempted = true;
				S8FixtureSource = Source->Handle;
				S8FixtureTarget = Target->Handle;
				const uint32 Epoch = BattleState->GetMatchEpoch();
				S8NonLethalDamageEventId = FGuid(
					0x5338424eu, Epoch, S8FixtureTarget.AuthorityId.A, S8FixtureTarget.LocalId + 1u);
				FGuLiDamageRequest Request;
				Request.MatchEpoch = Epoch;
				Request.DamageEventId = S8NonLethalDamageEventId;
				Request.ShotId = FGuid(
					0x53384253u, Epoch, S8FixtureTarget.AuthorityId.B, S8FixtureTarget.Generation);
				Request.Source = S8FixtureSource;
				Request.Target = S8FixtureTarget;
				Request.Damage = FMath::Clamp(Target->Health * 0.25f, 1.0f, Target->Health - 1.0f);
				Request.HitLocation = Target->Location;
				const FGuLiDamageCommitResult Result = Ledger->CommitDamage(Request);
				bS8NonLethalCommitted = Result.Status == EGuLiDamageCommitStatus::Committed
					&& !Result.bKilled && Result.RemainingHealth > 0.0f
					&& Result.RemainingHealth < Target->Health;
				S8FixtureRemainingHealth = Result.RemainingHealth;
				if (!bS8NonLethalCommitted)
				{
					RoleProbeError = TEXT("S8B non-lethal production-ledger transaction failed.");
				}
			}
		}

		if (bS8NonLethalCommitted && !bS8NonLethalPublicCutReady)
		{
			const FGuLiWingmanPublicBootstrapState* State =
				FindPublicState(S8FixtureWingman.Flight.Group);
			const FGuLiWingmanHealthEntry* Health = State
				? State->Bootstrap.Health.FindByPredicate([this](const FGuLiWingmanHealthEntry& Entry)
				{
					return Entry.Wingman == S8FixtureWingman;
				}) : nullptr;
			if (State && State->IsWellFormed() && Health
				&& Health->CurrentHealthPermille > 0u
				&& Health->CurrentHealthPermille < Health->MaximumHealthPermille)
			{
				bS8NonLethalPublicCutReady = true;
				QueueEvent(EGuLiWingmanQALogStream::BattleCombat,
					TEXT("S8_NONLETHAL_READY_FOR_OBSERVER"), {
						{TEXT("damage_event_id"), S8NonLethalDamageEventId.ToString(EGuidFormats::Digits)},
						{TEXT("wingman_id"), GuLiWingmanQA::WingmanId(S8FixtureWingman)},
						{TEXT("cut_id"), GuLiWingmanQA::UnsignedNumber(State->Bootstrap.Commit.CutId)},
						{TEXT("health_permille"), FString::FromInt(Health->CurrentHealthPermille)}});
			}
		}

		if (bS8NonLethalPublicCutReady && !bS8ObserverDetectedInWindow && Observer)
		{
			bS8ObserverDetectedInWindow = true;
			S8ObserverDetectedServerSeconds = NowSeconds;
			ObservedRoleProbeGates.Add(TEXT("MID_COMBAT_JOIN_ATOMIC_BOOTSTRAP"));
			QueueEvent(EGuLiWingmanQALogStream::WingmanNet,
				TEXT("S8_MIDCOMBAT_OBSERVER_CONNECTED"), {
					{TEXT("observer_player"), Observer->GetName()},
					{TEXT("candidate_accept_count"), TEXT("0")},
					{TEXT("fire_accept_count"), TEXT("0")}});
		}

		if (bS8ObserverDetectedInWindow && !bS8LethalCommitted
			&& NowSeconds - S8ObserverDetectedServerSeconds >= 1.0)
		{
			FGuLiCombatTargetSnapshot Current;
			if (Ledger->TryGetTargetSnapshot(S8FixtureTarget, Current) && Current.bAlive
				&& Current.Health > 0.0f)
			{
				const uint32 Epoch = BattleState->GetMatchEpoch();
				S8LethalDamageEventId = FGuid(
					0x5338424cu, Epoch, S8FixtureTarget.AuthorityId.C, S8FixtureTarget.LocalId + 2u);
				FGuLiDamageRequest Request;
				Request.MatchEpoch = Epoch;
				Request.DamageEventId = S8LethalDamageEventId;
				Request.ShotId = FGuid(
					0x53384246u, Epoch, S8FixtureTarget.AuthorityId.D, S8FixtureTarget.Generation);
				Request.Source = S8FixtureSource;
				Request.Target = S8FixtureTarget;
				Request.Damage = FMath::Max(1.0f, Current.Health);
				Request.HitLocation = Current.Location;
				const FGuLiDamageCommitResult Result = Ledger->CommitDamage(Request);
				bS8LethalCommitted = Result.Status == EGuLiDamageCommitStatus::Committed
					&& Result.bKilled && Result.RemainingHealth <= 0.0f
					&& Result.DeathEventId.IsValid();
				if (!bS8LethalCommitted)
				{
					RoleProbeError = TEXT("S8B lethal production-ledger transaction failed.");
				}
			}
		}

		if (bS8LethalCommitted && !bS8LethalPublicCutReady)
		{
			const FGuLiWingmanPublicBootstrapState* State =
				FindPublicState(S8FixtureWingman.Flight.Group);
			const FGuLiWingmanRosterEntry* Roster = State
				? State->Bootstrap.Roster.FindByPredicate([this](const FGuLiWingmanRosterEntry& Entry)
				{
					return Entry.Wingman == S8FixtureWingman;
				}) : nullptr;
			const FGuLiWingmanHealthEntry* Health = State
				? State->Bootstrap.Health.FindByPredicate([this](const FGuLiWingmanHealthEntry& Entry)
				{
					return Entry.Wingman == S8FixtureWingman;
				}) : nullptr;
			if (State && State->IsWellFormed() && Roster && Roster->bDead && Health
				&& Health->CurrentHealthPermille == 0u
				&& State->Bootstrap.Dead.Contains(S8FixtureWingman))
			{
				bS8LethalPublicCutReady = true;
				ObservedRoleProbeGates.Add(TEXT("LATEJOIN_DAMAGE_LEDGER_CONSISTENT"));
				QueueEvent(EGuLiWingmanQALogStream::BattleCombat,
					TEXT("S8_LETHAL_AFTER_OBSERVER_COMMITTED"), {
						{TEXT("damage_event_id"), S8LethalDamageEventId.ToString(EGuidFormats::Digits)},
						{TEXT("wingman_id"), GuLiWingmanQA::WingmanId(S8FixtureWingman)},
						{TEXT("cut_id"), GuLiWingmanQA::UnsignedNumber(State->Bootstrap.Commit.CutId)}});
			}
		}
		return;
	}

	if (ActiveRole != TEXT("S8C-TransferPendingJoin"))
	{
		return;
	}
	if (!bS8TransferOfferStarted && bCoreReady && !Observer)
	{
		APlayerController* CandidateController = nullptr;
		AGuLiBattlePlayerState* CandidateState = nullptr;
		UGuLiWingmanRelayComponent* CandidateTransport = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			AGuLiBattlePlayerState* PlayerState = Controller
				? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
			UGuLiWingmanRelayComponent* Transport = Controller
				? Controller->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
			if (PlayerState && Transport && Transport->CanServerAttachPersistentGroup()
				&& PlayerState->GetPlayerGuid().IsValid()
				&& PlayerState->GetTeam() != EGuLiTeam::Unassigned
				&& PlayerState->GetBattleRole() != EGuLiCommanderRole::Air
				&& PlayerState->GetBattleRole() != EGuLiCommanderRole::Observer
				&& PlayerState->GetBattleRole() != EGuLiCommanderRole::Unassigned)
			{
				CandidateController = Controller;
				CandidateState = PlayerState;
				CandidateTransport = Transport;
				break;
			}
		}
		if (CandidateController && CandidateState && CandidateTransport)
		{
			for (const FGuLiWingmanPublicBootstrapState& PublicState
				: BattleState->GetPublicWingmanBootstraps())
			{
				FGuLiWingmanRelayServer* Relay = Registry->FindGroup(PublicState.Group);
				if (!Relay || Relay->GetLeaseState().Lifecycle
						!= EGuLiWingmanGroupLifecycle::Active
					|| Registry->GetGroupOwnerCohort(PublicState.Group)
						!= static_cast<uint8>(CandidateState->GetTeam()))
				{
					continue;
				}
				FGuLiWingmanPendingLeaseOffer Offer;
				if (!Relay->BeginLeaseOffer(
					CandidateState->GetPlayerGuid(), Relay->GetLeaseState().OwnerPlayerGuid,
					NowSeconds, Offer))
				{
					continue;
				}
				bS8FixtureAttempted = true;
				bS8TransferOfferStarted = true;
				S8TransferGroup = PublicState.Group;
				S8TransferCandidateGuid = CandidateState->GetPlayerGuid();
				S8TransferOfferRevision = Offer.OfferRevision;
				S8TransferPreviewCutId = PublicState.Bootstrap.Commit.CutId;
				const bool bFrozenPreview = PublicState.IsWellFormed()
					&& Offer.PreviewRosterRevision == PublicState.Bootstrap.RosterRevision
					&& Offer.PreviewAbilityConfigRevision
						== PublicState.Bootstrap.AbilityConfig.SnapshotRevision
					&& Offer.PreviewAbilityConfigHash
						== PublicState.Bootstrap.AbilityConfig.SnapshotHash;
				if (!bFrozenPreview)
				{
					RoleProbeError = TEXT("S8C offer preview did not bind the retained public Cut.");
				}
				QueueEvent(EGuLiWingmanQALogStream::WingmanRelay,
					TEXT("S8_TRANSFER_OFFER_READY_FOR_OBSERVER"), {
						{TEXT("group"), GuLiWingmanQA::GroupId(S8TransferGroup)},
						{TEXT("offer_revision"), FString::FromInt(S8TransferOfferRevision)},
						{TEXT("preview_cut_id"), GuLiWingmanQA::UnsignedNumber(S8TransferPreviewCutId)},
						{TEXT("preview_frozen"), bFrozenPreview ? TEXT("true") : TEXT("false")}});
				break;
			}
		}
	}

	FGuLiWingmanRelayServer* TransferRelay = S8TransferGroup.IsValid()
		? Registry->FindGroup(S8TransferGroup) : nullptr;
	if (bS8TransferOfferStarted && !bS8ObserverDetectedInWindow && Observer
		&& TransferRelay && TransferRelay->GetPendingLeaseOffer().IsPending()
		&& TransferRelay->GetPendingLeaseOffer().OfferRevision == S8TransferOfferRevision)
	{
		bS8ObserverDetectedInWindow = true;
		S8ObserverDetectedServerSeconds = NowSeconds;
		QueueEvent(EGuLiWingmanQALogStream::WingmanNet,
			TEXT("S8_TRANSFER_PENDING_OBSERVER_CONNECTED"), {
				{TEXT("observer_player"), Observer->GetName()},
				{TEXT("offer_revision"), FString::FromInt(S8TransferOfferRevision)},
				{TEXT("candidate_accept_count"), TEXT("0")},
				{TEXT("fire_accept_count"), TEXT("0")}});
	}
	if (bS8ObserverDetectedInWindow && !bS8TransferOfferDelivered && TransferRelay
		&& TransferRelay->GetPendingLeaseOffer().IsPending())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			const AGuLiBattlePlayerState* PlayerState = Controller
				? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
			UGuLiWingmanRelayComponent* Transport = Controller
				? Controller->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
			if (PlayerState && Transport
				&& PlayerState->GetPlayerGuid() == S8TransferCandidateGuid)
			{
				bS8TransferOfferDelivered = Transport->ServerDeliverLeaseOffer(S8TransferGroup);
				break;
			}
		}
	}
	if (bS8TransferOfferDelivered && !bS8TransferRecoveredActive && TransferRelay
		&& TransferRelay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active
		&& !TransferRelay->IsTransferInProgress()
		&& TransferRelay->GetLeaseState().OwnerPlayerGuid == S8TransferCandidateGuid)
	{
		const FGuLiWingmanPublicBootstrapState* PublicState = FindPublicState(S8TransferGroup);
		const bool bPublicOwnerUpdated = PublicState && PublicState->IsWellFormed()
			&& PublicState->Lifecycle == EGuLiWingmanGroupLifecycle::Active
			&& PublicState->Bootstrap.Commit.CutId > S8TransferPreviewCutId
			&& Algo::AllOf(PublicState->Bootstrap.AuthorityMap,
				[this](const FGuLiWingmanAuthorityEntry& Entry)
				{
					return Entry.LeaseOwnerPlayerGuid == S8TransferCandidateGuid;
				});
		if (bPublicOwnerUpdated)
		{
			bS8TransferRecoveredActive = true;
			ObservedRoleProbeGates.Add(TEXT("TRANSFER_PENDING_JOIN_FROZEN_CUT"));
			QueueEvent(EGuLiWingmanQALogStream::WingmanRelay,
				TEXT("S8_TRANSFER_COMMITTED_AFTER_OBSERVER"), {
					{TEXT("group"), GuLiWingmanQA::GroupId(S8TransferGroup)},
					{TEXT("offer_revision"), FString::FromInt(S8TransferOfferRevision)},
					{TEXT("active_cut_id"), GuLiWingmanQA::UnsignedNumber(
						PublicState->Bootstrap.Commit.CutId)}});
		}
	}
#endif
}

void UGuLiWingmanQASubsystem::LogStats() const
{
	const UWorld* World = GetWorld();
	const AGuLiBattleGameState* State = World ? World->GetGameState<AGuLiBattleGameState>() : nullptr;
	const UGuLiWingmanSimulationSubsystem* Simulation = World
		? World->GetSubsystem<UGuLiWingmanSimulationSubsystem>() : nullptr;
	const UGuLiBattleAuthoritySubsystem* Commander = World
		? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	FGuLiWingmanMotionDiagnostics Motion;
	if (Simulation)
	{
		Simulation->GetMotionDiagnostics(Motion);
	}
	UE_LOG(LogGuLiWingman, Display,
		TEXT("WingmanStats net_mode=%s groups=%d owner_mass=%d commander=%d commander_tick=%u "
			"dropped_steps=%llu qa_active=%d debug_draw=%d motion=(alive=%d orbit=%d follow=%d "
			"catchup=%d recover=%d stale=%d radius=%.1f/%.1f/%.1f speed=%.1f first=%s)"),
		World ? GuLiWingmanQA::NetModeName(World->GetNetMode()) : TEXT("None"),
		State ? State->GetPublicWingmanBootstraps().Num() : 0,
		Simulation ? Simulation->GetTotalOwnedEntityCount() : 0,
		Commander ? Commander->GetAuthoritativeMemberCount() : 0,
		Commander ? Commander->GetServerSimTick() : 0u,
		Commander ? Commander->GetDroppedFixedStepCount() : 0u,
		EvidenceWriter.IsActive() ? 1 : 0, GetDebugDrawMode(),
		Motion.AliveEntities, Motion.OrbitEntities, Motion.FollowEntities,
		Motion.CatchUpEntities, Motion.RecoverEntities, Motion.StaleEntities,
		Motion.MinimumCarrierDistanceCentimeters,
		Motion.MeanCarrierDistanceCentimeters,
		Motion.MaximumCarrierDistanceCentimeters,
		Motion.MeanSpeedCentimetersPerSecond,
		*Motion.FirstAliveLocation.ToCompactString());
}

void UGuLiWingmanQASubsystem::LogUnit(const FString& RequestedWingmanId) const
{
	const AGuLiBattleGameState* State = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	if (!State)
	{
		UE_LOG(LogGuLiWingman, Warning, TEXT("Wingman Unit unavailable: no BattleGameState."));
		return;
	}
	for (const FGuLiWingmanPublicBootstrapState& PublicState : State->GetPublicWingmanBootstraps())
	{
		for (const FGuLiWingmanRosterEntry& Entry : PublicState.Bootstrap.Roster)
		{
			const FString Id = GuLiWingmanQA::WingmanId(Entry.Wingman);
			if (!GuLiWingmanQA::EqualsId(Id, RequestedWingmanId))
			{
				continue;
			}
			const FGuLiWingmanHealthEntry* Health = PublicState.Bootstrap.Health.FindByPredicate(
				[&Entry](const FGuLiWingmanHealthEntry& Candidate)
				{
					return Candidate.Wingman == Entry.Wingman;
				});
			UE_LOG(LogGuLiWingman, Display,
				TEXT("WingmanUnit id=%s dead=%d health=%u/%u roster_revision=%u lifecycle=%d"),
				*Id, Entry.bDead ? 1 : 0,
				Health ? Health->CurrentHealthPermille : 0u,
				Health ? Health->MaximumHealthPermille : 0u,
				PublicState.Bootstrap.RosterRevision, static_cast<int32>(PublicState.Lifecycle));
			return;
		}
	}
	UE_LOG(LogGuLiWingman, Warning, TEXT("Wingman Unit not found: %s"), *RequestedWingmanId);
}

void UGuLiWingmanQASubsystem::LogGroup(const FString& RequestedGroupId) const
{
	const AGuLiBattleGameState* State = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const FGuLiWingmanRelayAuthorityRegistry* Registry = State
		? State->GetWingmanRelayAuthorityRegistry() : nullptr;
	if (!State)
	{
		UE_LOG(LogGuLiWingman, Warning, TEXT("Wingman Group unavailable: no BattleGameState."));
		return;
	}
	for (const FGuLiWingmanPublicBootstrapState& PublicState : State->GetPublicWingmanBootstraps())
	{
		const FString Id = GuLiWingmanQA::GroupId(PublicState.Group);
		if (!GuLiWingmanQA::EqualsId(Id, RequestedGroupId))
		{
			continue;
		}
		const FGuLiWingmanRelayServer* Relay = Registry ? Registry->FindGroup(PublicState.Group) : nullptr;
		UE_LOG(LogGuLiWingman, Display,
			TEXT("WingmanGroup id=%s roster=%d dead=%d bootstrap_valid=%d lifecycle=%d relay=%d lease_epoch=%u accepted_batches=%d server_motion_writes=%llu"),
			*Id, PublicState.Bootstrap.Roster.Num(), PublicState.Bootstrap.Dead.Num(),
			PublicState.IsWellFormed() ? 1 : 0, static_cast<int32>(PublicState.Lifecycle),
			Relay ? 1 : 0, Relay ? Relay->GetLeaseState().LeaseEpoch : 0u,
			Relay ? Relay->GetAcceptedHistory().Num() : 0,
			Relay ? Relay->GetServerWingmanMovementWriteCount() : 0u);
		return;
	}
	UE_LOG(LogGuLiWingman, Warning, TEXT("Wingman Group not found: %s"), *RequestedGroupId);
}

void UGuLiWingmanQASubsystem::LogNavigation(const FString& RequestedFlightId) const
{
	const AGuLiBattleGameState* State = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const UGuLiWingmanSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>() : nullptr;
	if (State)
	{
		for (const FGuLiWingmanPublicBootstrapState& PublicState : State->GetPublicWingmanBootstraps())
		{
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				FGuLiWingmanFlightHandle Flight;
				Flight.Group = PublicState.Group;
				Flight.FlightIndex = FlightIndex;
				if (!GuLiWingmanQA::EqualsId(GuLiWingmanQA::FlightId(Flight), RequestedFlightId))
				{
					continue;
				}
				FGuLiWingmanNavigationDiagnostics Nav;
				FGuLiWingmanAvoidanceDiagnostics Avoidance;
				const bool bNav = Simulation && Simulation->GetNavigationDiagnostics(PublicState.Group, Nav);
				const bool bAvoidance = Simulation && Simulation->GetAvoidanceDiagnostics(PublicState.Group, Avoidance);
				UE_LOG(LogGuLiWingmanAI, Display,
					TEXT("WingmanNav flight=%s local=%d pending=%d paths=%d guided=%d requests=%llu accepted=%llu fallback=%llu cancelled=%llu avoidance=%d evaluated=%d probes=%llu"),
					*RequestedFlightId, bNav ? 1 : 0, Nav.PendingFlightRequests, Nav.ActiveFlightPaths,
					Nav.NavigationGuidedEntities, Nav.AsyncRequestsIssued, Nav.ResultsAccepted,
					Nav.SafeFallbackResults, Nav.PendingRequestsCancelled, bAvoidance ? 1 : 0,
					Avoidance.EvaluatedEntities, Avoidance.HeadingProbes);
				return;
			}
		}
	}
	UE_LOG(LogGuLiWingmanAI, Warning, TEXT("Wingman Nav flight not found: %s"), *RequestedFlightId);
}

void UGuLiWingmanQASubsystem::LogRelayStats() const
{
	const AGuLiBattleGameState* State = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const FGuLiWingmanRelayAuthorityRegistry* Registry = State
		? State->GetWingmanRelayAuthorityRegistry() : nullptr;
	if (!State || !Registry)
	{
		UE_LOG(LogGuLiWingmanRelay, Warning, TEXT("Wingman RelayStats unavailable on this endpoint."));
		return;
	}
	for (const FGuLiWingmanPublicBootstrapState& PublicState : State->GetPublicWingmanBootstraps())
	{
		const FGuLiWingmanRelayServer* Relay = Registry->FindGroup(PublicState.Group);
		if (!Relay)
		{
			continue;
		}
		UE_LOG(LogGuLiWingmanRelay, Display,
			TEXT("RelayStats group=%s lifecycle=%d owner=%s lease_epoch=%u roster_revision=%u atomic_required=%d atomic_committed=%d history=%d movement_writes=%llu"),
			*GuLiWingmanQA::GroupId(PublicState.Group),
			static_cast<int32>(Relay->GetLeaseState().Lifecycle),
			*Relay->GetLeaseState().OwnerPlayerGuid.ToString(EGuidFormats::Digits),
			Relay->GetLeaseState().LeaseEpoch, Relay->GetRosterRevision(),
			Relay->IsAtomicCandidateBatchRequired() ? 1 : 0,
			Relay->IsAtomicCandidateBatchCommitted() ? 1 : 0,
			Relay->GetAcceptedHistory().Num(), Relay->GetServerWingmanMovementWriteCount());
	}
}

void UGuLiWingmanQASubsystem::LogLeaseWatchdog() const
{
	const AGuLiBattleGameState* State = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const FGuLiWingmanRelayAuthorityRegistry* Registry = State
		? State->GetWingmanRelayAuthorityRegistry() : nullptr;
	if (!State || !Registry)
	{
		UE_LOG(LogGuLiWingmanRelay, Warning,
			TEXT("Wingman LeaseWatchdog unavailable on this endpoint."));
		return;
	}
	for (const FGuLiWingmanPublicBootstrapState& PublicState : State->GetPublicWingmanBootstraps())
	{
		const FGuLiWingmanRelayServer* Relay = Registry->FindGroup(PublicState.Group);
		if (!Relay)
		{
			continue;
		}
		UE_LOG(LogGuLiWingmanRelay, Display,
			TEXT("LeaseWatchdog group=%s executions=%llu last=%.6f next=%.6f heartbeat=%.6f events=%d transfer=%d revoked=%d"),
			*GuLiWingmanQA::GroupId(PublicState.Group), Relay->GetLeaseMaintenanceExecutionCount(),
			Relay->GetLastLeaseMaintenanceTimeSeconds(), Relay->GetNextLeaseMaintenanceTimeSeconds(),
			Relay->GetLastHeartbeatTimeSeconds(), Relay->GetLeaseEvents().Num(),
			Relay->IsTransferInProgress() ? 1 : 0, Relay->IsActiveLeaseRevoked() ? 1 : 0);
	}
}

bool UGuLiWingmanQASubsystem::SetDebugDrawMode(const int32 Mode)
{
	if (Mode < 0 || Mode > 3)
	{
		return false;
	}
	GuLiWingmanQA::DebugDrawMode = Mode;
	return true;
}

int32 UGuLiWingmanQASubsystem::GetDebugDrawMode()
{
	return GuLiWingmanQA::DebugDrawMode;
}

void UGuLiWingmanQASubsystem::DrawDiagnostics() const
{
	UWorld* World = GetWorld();
	const int32 Mode = GetDebugDrawMode();
	const AGuLiBattleGameState* State = World ? World->GetGameState<AGuLiBattleGameState>() : nullptr;
	const FGuLiWingmanRelayAuthorityRegistry* Registry = State
		? State->GetWingmanRelayAuthorityRegistry() : nullptr;
	if (!World || Mode == 0 || !State || !Registry)
	{
		return;
	}
	for (const FGuLiWingmanPublicBootstrapState& PublicState : State->GetPublicWingmanBootstraps())
	{
		const FGuLiWingmanRelayServer* Relay = Registry->FindGroup(PublicState.Group);
		if (!Relay)
		{
			continue;
		}
		for (const FGuLiWingmanAcceptedBatch& Batch : Relay->GetAcceptedHistory())
		{
			for (const FGuLiWingmanCandidateSample& Sample : Batch.Samples)
			{
				const FVector Position(
					Sample.PositionCentimeters.X,
					Sample.PositionCentimeters.Y,
					Sample.PositionCentimeters.Z);
				DrawDebugPoint(World, Position, Mode >= 3 ? 14.0f : 8.0f,
					FColor::Cyan, false, 0.1f, 0);
				if (Mode >= 2)
				{
					DrawDebugDirectionalArrow(World, Position,
						Position + FVector(
							Sample.VelocityCentimetersPerSecond.X,
							Sample.VelocityCentimetersPerSecond.Y,
							Sample.VelocityCentimetersPerSecond.Z).GetSafeNormal() * 1000.0,
						150.0f, FColor::Green, false, 0.1f, 0, 3.0f);
				}
				if (Mode >= 3)
				{
					DrawDebugString(World, Position + FVector(0.0, 0.0, 200.0),
						GuLiWingmanQA::WingmanId(Sample.Wingman), nullptr,
						FColor::White, 0.1f, false, 0.8f);
				}
			}
		}
	}
}
