// Copyright Epic Games, Inc. All Rights Reserved.

// Opt-in process-local measurement driver. No automation tests or persistent map/config edits.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "GuLiStrike.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Net/NetworkProfiler.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Serialization/JsonSerializer.h"

namespace GuLiCommanderMoveStress
{
constexpr int32 BatchSize = 100;

struct FTeamRun
{
	TWeakObjectPtr<AGuLiBattlePlayerState> Owner;
	TArray<FGuLiSoldierId> Ids;
	TArray<FVector> BatchOrigins;
	int32 SpawnCursor = 0;
	int32 Next = 0;
	uint32 Command = 0;
	bool bPending = false;
	double CommandStart = 0;
	TArray<double> PlanningMilliseconds;
};

struct FRun : TSharedFromThis<FRun>
{
	int32 Population = 0;
	double Duration = 0;
	FString Directory;
	bool bClient = false;
	int32 ExpectedClients = 0;
	bool bJoinBeforePopulation = false;
	bool bAnnouncedBaseline = false;
	bool bSpawnInitialized = false;
	bool bPopulationGrowthFrozen = false;
	bool bCameraLocked = false;
	bool bCaptureTrace = false;
	bool bTraceStarted = false;
	bool bCaptureNetwork = false;
	bool bNetworkProfileStarted = false;
	int32 SpawnAttempts = 0;
	double NextSpawnAt = 0;
	bool bShuttle = false;
	int32 ShuttleRound = 0;
	double NextShuttle = 0;
	double NextPlanningLog = 0;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Authority;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> Presentation;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> Roster;
	TWeakObjectPtr<AGuLiCommanderPlayerController> Controller;
	FTeamRun Teams[2];
	double Started = FPlatformTime::Seconds();
	double WarmupStarted = 0;
	double CaptureStarted = 0;
	double Finished = 0;
	double PreviousFrameTime = 0;
	double NextCensus = 0;
	uint64 PreviousSteps = 0;
	uint64 InitialSteps = 0;
	uint64 InitialDropped = 0;
	uint64 InitialMoves = 0;
	uint64 InitialPoseFrames = 0;
	double PreviousSimulationMs = 0;
	double PreviousCombatMs = 0;
	int32 Alive = 0;
	int32 Moving = 0;
	int32 Blocked = 0;
	int32 Arrived = 0;
	int32 ObservedMoving = 0;
	uint32 ProbeId = 0;
	FVector ProbeStart = FVector::ZeroVector;
	FVector ProbeEnd = FVector::ZeroVector;
	TMap<uint32, FVector> LastLocations;
	TArray<FString> Frames;
	TArray<FString> NetworkFrames;
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TSharedFuture<FString> CsvCompletion;
	bool bPrepared = false;
	bool bSaved = false;

	void SampleNetwork(double Now)
	{
		if (!bCaptureNetwork) return;
		const auto* Driver = World->GetNetDriver();
		check(Driver);
		const auto Record = [&](UNetConnection* Connection)
		{
			NetworkFrames.Add(FString::Printf(TEXT("%.9f,%.9f,%s,%d,%d,%d"),
				Now, Now - GStartTime, *Connection->LowLevelGetRemoteAddress(true),
				Connection->InTotalBytes, Connection->OutTotalBytes, Connection->PacketOverhead));
		};
		if (Driver->ServerConnection) Record(Driver->ServerConnection);
		else for (const auto& Connection : Driver->ClientConnections) Record(Connection);
	}

	void Save(const FString& Error)
	{
		if (bSaved) return;
		bSaved = true;
		Finished = FPlatformTime::Seconds();
		if (bTraceStarted) FTraceAuxiliary::Stop();
#if USE_NETWORK_PROFILER
		if (bNetworkProfileStarted)
		{
			GNetworkProfiler.TrackFrameBegin();
			GNetworkProfiler.EnableTracking(false);
		}
#endif
		Report->SetStringField(TEXT("error"), Error);
		Report->SetStringField(TEXT("mode"), bClient ? TEXT("client") : TEXT("server"));
		Report->SetStringField(TEXT("fixture"), TEXT("authored_terrain"));
		Report->SetStringField(TEXT("movement_pattern"), bShuttle ? TEXT("8_second_shuttle") : TEXT("long_march"));
		Report->SetNumberField(TEXT("shuttle_rounds"), ShuttleRound);
		Report->SetStringField(TEXT("camera"), TEXT("locked commander view: X=-150000 Y=110000; terrain-constrained default arm/yaw; camera pawn tick disabled"));
		Report->SetBoolField(TEXT("camera_locked"), bCameraLocked);
		Report->SetBoolField(TEXT("cpu_gpu_trace_started"), bTraceStarted);
		Report->SetBoolField(TEXT("network_profile_started"), bNetworkProfileStarted);
		Report->SetStringField(TEXT("join_order"), bJoinBeforePopulation ? TEXT("join_first_incremental_spawn") : TEXT("population_before_join"));
		Report->SetStringField(TEXT("map"), World.IsValid() ? World->GetMapName() : TEXT("unavailable"));
		Report->SetNumberField(TEXT("requested_population"), Population);
		Report->SetNumberField(TEXT("authority_hz"), GuLiCommanderSimulationTiming::RateHz);
		Report->SetNumberField(TEXT("measured_seconds"), CaptureStarted > 0 ? Finished - CaptureStarted : 0);
		Report->SetNumberField(TEXT("probe_id"), ProbeId);
		Report->SetNumberField(TEXT("probe_distance_cm"), FVector::Dist2D(ProbeStart, ProbeEnd));
		if (Authority.IsValid())
		{
			Report->SetNumberField(TEXT("simulation_steps"), Authority->GetServerSimTick() - InitialSteps);
			Report->SetNumberField(TEXT("dropped_steps"), Authority->GetDroppedFixedStepCount() - InitialDropped);
			Report->SetNumberField(TEXT("movement_updates"), Authority->GetNavigationStats().MovementUpdateCalls - InitialMoves);
			Report->SetNumberField(TEXT("speed_cm_s"), Authority->GetCommittedMovementSpeedCmPerSecond());
			TArray<TSharedPtr<FJsonValue>> Latencies;
			for (const auto& Team : Teams) for (double Ms : Team.PlanningMilliseconds)
				Latencies.Add(MakeShared<FJsonValueNumber>(Ms));
			Report->SetArrayField(TEXT("server_plan_commit_ms"), Latencies);
		}
		if (Controller.IsValid())
			Report->SetNumberField(TEXT("accepted_pose_frames"), Controller->GetCommanderNetSyncComponent()->GetAcceptedPoseFrameCount() - InitialPoseFrames);
		FString Json;
		FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
		FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("summary.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FFileHelper::SaveStringArrayToFile(Frames, *(Directory / TEXT("frames.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (bCaptureNetwork) FFileHelper::SaveStringArrayToFile(NetworkFrames, *(Directory / TEXT("network.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (CaptureStarted > 0) CsvCompletion = FCsvProfiler::Get()->EndCapture();
		UE_LOG(LogGuLiStrike, Display, TEXT("MoveStress finished: mode=%s requested=%d error=%s output=%s"),
			bClient ? TEXT("client") : TEXT("server"), Population, *Error, *Directory);
	}

	bool FindWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (!Candidate || !Candidate->HasBegunPlay() || Candidate->WorldType != EWorldType::Game) continue;
			if ((Candidate->GetNetMode() == NM_Client) != bClient) continue;
			World = Candidate;
			Authority = Candidate->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
			for (TActorIterator<AGuLiCommanderPresentationActor> It(Candidate); It; ++It) Presentation = *It;
			for (TActorIterator<AGuLiSoldierStateReplicator> It(Candidate); It; ++It) Roster = *It;
			for (auto It = Candidate->GetPlayerControllerIterator(); It; ++It)
			{
				auto* PC = Cast<AGuLiCommanderPlayerController>(It->Get());
				if (PC && PC->IsLocalController()) Controller = PC;
			}
			return bClient ? Presentation.IsValid() && Roster.IsValid() && Controller.IsValid()
				&& Controller->GetCommanderNetSyncComponent()->IsSoldierStreamReady()
				: Authority.IsValid() && Authority->HasSpawnedAuthorityPopulation();
		}
		return false;
	}

	int32 CountReadyClients() const
	{
		int32 Count = 0;
		for (auto It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const auto* PS = It->Get()->GetPlayerState<AGuLiBattlePlayerState>();
			if (PS && PS->IsCommander() && PS->IsSoldierStreamReady()) ++Count;
		}
		return Count;
	}

	bool PrepareServer()
	{
		if (!bPopulationGrowthFrozen)
		{
			// Keep the authored world running, but make population an explicit fixture input.
			IConsoleManager::Get().FindConsoleVariable(TEXT("guli.stronghold.CaptureSeconds"))->Set(1.e9f, ECVF_SetByConsole);
			int32 PausedProducers = 0;
			for (TActorIterator<AActor> It(World.Get()); It; ++It)
			{
				if (auto* Production = It->FindComponentByClass<UGuLiBuildingProductionComponent>())
				{
					Production->SetComponentTickEnabled(false);
					++PausedProducers;
				}
			}
			Report->SetNumberField(TEXT("paused_ambient_producers"), PausedProducers);
			bPopulationGrowthFrozen = true;
		}
		if (bJoinBeforePopulation && !bSpawnInitialized)
		{
			if (!bAnnouncedBaseline)
			{
				bAnnouncedBaseline = true;
				UE_LOG(LogGuLiStrike, Display, TEXT("MoveStress awaiting baseline clients"));
			}
			if (CountReadyClients() < ExpectedClients) return false;
		}
		auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		IConsoleManager::Get().FindConsoleVariable(TEXT("guli.stronghold.TeamUnitCap"))->Set(Population / 2, ECVF_SetByConsole);
		if (!bSpawnInitialized)
		{
			TArray<FGuLiSoldierStateItem> Existing;
			Authority->BuildSoldierStateSnapshot(Existing);
			for (const auto& State : Existing) Authority->ApplyDamage(State.SoldierId, 1.e9f);
			for (int32 TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
			{
				auto& Run = Teams[TeamIndex];
				Run.Owner = World->SpawnActor<AGuLiBattlePlayerState>();
				Run.Owner->SetReplicates(false);
				Run.Owner->SetServerRoleAssignment(TeamIndex == 0 ? EGuLiTeam::Red : EGuLiTeam::Blue, EGuLiCommanderRole::Commander, TeamIndex);
			}
			bSpawnInitialized = true;
		}
		const double Now = FPlatformTime::Seconds();
		if (Now < NextSpawnAt) return false;
		const int32 PerTeam = Population / 2;
		const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(PerTeam)));
		auto* NavData = GuLiCommanderNavigationPolicy::ResolveRequiredNavigationData(*Navigation);
		for (int32 TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
		{
			const EGuLiTeam Team = TeamIndex == 0 ? EGuLiTeam::Red : EGuLiTeam::Blue;
			FTeamRun& Run = Teams[TeamIndex];
			const int32 Wanted = bJoinBeforePopulation ? FMath::Min(Run.Ids.Num() + 50, PerTeam) : PerTeam;
			// The authored terrain is well below Z=0. Select legal cells from a finite ordered grid;
			// rejected terrain/obstacle cells are counted as fixture setup, never as moving units.
			for (; Run.SpawnCursor < PerTeam * 2 && Run.Ids.Num() < Wanted; ++Run.SpawnCursor)
			{
				const int32 Index = Run.SpawnCursor;
				++SpawnAttempts;
				const FVector Desired(-175000.0 + (Index % Columns - (Columns - 1) * .5) * 1800.0,
					(TeamIndex == 0 ? 110000.0 : -110000.0) + (Index / Columns - (Columns - 1) * .5) * 1800.0, 0.0);
				FNavLocation Ground;
				if (!Navigation->ProjectPointToNavigation(Desired, Ground, FVector(100,100,50000), NavData)) continue;
				FGuLiSoldierId Id;
				if (Authority->SpawnDebugSoldier(Team, 1, Ground.Location, Id)) Run.Ids.Add(Id);
			}
			const int32 Spawned = Run.Ids.Num();
			if (Spawned != Wanted)
			{
				Save(FString::Printf(TEXT("spawn_failed: team=%d requested=%d actual=%d"), TeamIndex, PerTeam, Spawned));
				return false;
			}
		}
		if (Teams[0].Ids.Num() + Teams[1].Ids.Num() != Population)
		{
			NextSpawnAt = Now + .25;
			return false;
		}
		Report->SetNumberField(TEXT("spawn_candidate_count"), SpawnAttempts);
		Report->SetStringField(TEXT("command_source"), TEXT("server synthetic owners; production BeginMovePlanning/PollMovePlanning; 100 members per batch, both teams"));
		Report->SetNumberField(TEXT("travel_distance_cm"), bShuttle ? 150000.0 : 350000.0);
		if (bShuttle)
		{
			Report->SetNumberField(TEXT("shuttle_east_offset_cm"), 100000.0);
			Report->SetNumberField(TEXT("shuttle_west_offset_cm"), -50000.0);
		}
		ProbeId = Teams[0].Ids[0].Value;
		return true;
	}

	bool AdvancePlans(double Now)
	{
		bool bAllReady = true;
		for (FTeamRun& Team : Teams)
		{
			if (Team.bPending)
			{
				FGuLiCommandAck Ack;
				FGuLiCommanderSelectionState Selection;
				bool bChanged = false;
				const auto Status = Authority->PollMovePlanning(*Team.Owner, Team.Command, Ack, Selection, bChanged);
				if (Status == EGuLiMovePlanningStatus::NotFound)
				{
					Save(TEXT("pending_move_plan_disappeared"));
					return false;
				}
				if (Status == EGuLiMovePlanningStatus::Completed)
				{
					Team.bPending = false;
					Team.PlanningMilliseconds.Add((Now - Team.CommandStart) * 1000.0);
					if (Ack.Result != EGuLiCommandAckResult::Accepted)
					{
						Save(FString::Printf(TEXT("move_plan_not_fully_accepted: result=%d batch=%u"), static_cast<int32>(Ack.Result), Team.Command));
						return false;
					}
				}
			}
			if (!Team.bPending && Team.Next < Team.Ids.Num())
			{
				FGuLiCommanderSelectionState Selection;
				Selection.SelectionRevision = 1;
				FVector Center = FVector::ZeroVector;
				const int32 End = FMath::Min(Team.Next + BatchSize, Team.Ids.Num());
				const int32 Begin = Team.Next;
				for (int32 Index = Begin; Index < End; ++Index)
				{
					if ((Index - Begin) % 25 == 0)
					{
						auto& Cohort = Selection.Cohorts.AddDefaulted_GetRef();
						Cohort.CohortId = FGuLiControlCohortId(Index / 25 + 1);
					}
					auto& Cohort = Selection.Cohorts.Last();
					Cohort.MemberIds.Add(Team.Ids[Index]); ++Cohort.AliveCount;
					FTransform Pose; Authority->TryGetSoldierTransform(Team.Ids[Index], Pose);
					Center += Pose.GetLocation();
				}
				FGuLiMoveRequest Request;
				Request.ClientCommandId = ++Team.Command;
				Request.SelectionRevision = 1;
				if (ShuttleRound == 0) Team.BatchOrigins.Add(Center / (End - Begin));
				Request.Target = Team.BatchOrigins[Begin / BatchSize]
					+ FVector(bShuttle ? (ShuttleRound % 2 == 0 ? 100000.0 : -50000.0) : 350000.0, 0, 0);
				FGuLiCommandAck Ack;
				if (!Authority->BeginMovePlanning(*Team.Owner, Request, Selection, Ack))
				{
					Save(FString::Printf(TEXT("move_plan_rejected: result=%d"), static_cast<int32>(Ack.Result)));
					return false;
				}
				Team.Next = End; Team.bPending = true; Team.CommandStart = Now;
			}
			bAllReady &= !Team.bPending && Team.Next == Team.Ids.Num();
		}
		if (!bAllReady && Now >= NextPlanningLog)
		{
			NextPlanningLog = Now + 10.0;
			const auto Stats = Authority->GetNavigationStats();
			UE_LOG(LogGuLiStrike, Display, TEXT("MoveStress planning pending=%d projections=%llu paths=%llu round=%d"),
				Stats.PendingMovePlanningTasks, Stats.MoveCandidateProjectionQueries, Stats.MovePlanningPathQueries, ShuttleRound);
		}
		return bAllReady;
	}

	void Census()
	{
		Alive = Moving = Blocked = Arrived = ObservedMoving = 0;
		TArray<FGuLiSoldierStateItem> States;
		if (bClient) States = Roster->GetItems(); else Authority->BuildSoldierStateSnapshot(States);
		for (const auto& State : States)
		{
			if (!State.IsAlive()) continue;
			++Alive;
			FTransform Pose;
			bool bHasPose;
			if (bClient)
			{
				Moving += State.ActiveOrderId != 0u ? 1 : 0;
				bHasPose = Presentation->TryGetPresentedSoldierTransform(State.SoldierId, Pose);
			}
			else
			{
				FGuLiSoldierNavigationDebug Nav;
				Authority->TryGetSoldierNavigationDebug(State.SoldierId, Nav);
				Moving += Nav.bMoving ? 1 : 0;
				Blocked += Nav.State == EGuLiSoldierNavigationState::Blocked ? 1 : 0;
				Arrived += Nav.State == EGuLiSoldierNavigationState::Arrived ? 1 : 0;
				bHasPose = Authority->TryGetSoldierTransform(State.SoldierId, Pose);
			}
			if (!bHasPose) continue;
			if (const FVector* Last = LastLocations.Find(State.SoldierId.Value))
				ObservedMoving += FVector::DistSquared2D(*Last, Pose.GetLocation()) > 100.0 ? 1 : 0;
			LastLocations.Add(State.SoldierId.Value, Pose.GetLocation());
			if (ProbeId == 0u) ProbeId = State.SoldierId.Value;
			if (State.SoldierId.Value == ProbeId) ProbeEnd = Pose.GetLocation();
		}
		if (bClient && Controller.IsValid() && !bCameraLocked)
		{
			if (auto* Camera = Cast<AGuLiCommanderCameraPawn>(Controller->GetPawn()))
			{
				Camera->JumpToWorldLocation(FVector(-150000.0, 110000.0, 0.0));
				// The controller can keep submitting edge-pan input in an offscreen window.
				// Freeze only this fixture's camera; a periodic teleport does not lock the view.
				Camera->SetActorTickEnabled(false);
				bCameraLocked = true;
			}
		}
	}

	bool Tick(float)
	{
		const double Now = FPlatformTime::Seconds();
		if (bSaved)
		{
			if ((!CsvCompletion.IsValid() || CsvCompletion.IsReady()) && Now - Finished > 2.0)
			{
				FPlatformMisc::RequestExit(false);
				return false;
			}
			return true;
		}
		if (Now - Started > Duration + 240.0) { Save(TEXT("setup_or_capture_timeout")); return true; }
		if (!bPrepared)
		{
			if (!FindWorld()) return true;
			if (bClient)
			{
				Census();
				if (Alive != Population || Moving < Population * .95) return true;
			}
			else if (!PrepareServer()) return true;
			bPrepared = true;
			UE_LOG(LogGuLiStrike, Display, TEXT("MoveStress prepared mode=%s count=%d"), bClient ? TEXT("client") : TEXT("server"), Population);
		}
		if (WarmupStarted == 0)
		{
			if (!bClient)
			{
				if (CountReadyClients() < ExpectedClients || !AdvancePlans(Now)) return true;
			}
			WarmupStarted = Now;
			NextShuttle = Now + 8.0;
			Report->SetNumberField(TEXT("setup_seconds"), Now - Started);
		}
		if (Now - WarmupStarted < 8.0) return true;
		if (!bClient && bShuttle)
		{
			if (NextShuttle > 0 && Now >= NextShuttle)
			{
				++ShuttleRound;
				for (auto& Team : Teams) Team.Next = 0;
				NextShuttle = 0;
			}
			if (NextShuttle == 0 && AdvancePlans(Now)) NextShuttle = Now + 8.0;
			if (bSaved) return true;
		}
		if (CaptureStarted == 0)
		{
			CaptureStarted = Now; PreviousFrameTime = Now;
			Report->SetNumberField(TEXT("capture_start_monotonic_seconds"), Now);
			Report->SetNumberField(TEXT("capture_start_engine_seconds"), Now - GStartTime);
			if (bCaptureNetwork)
			{
				NetworkFrames.Add(TEXT("monotonic_seconds,engine_seconds,connection,in_total_bytes,out_total_bytes,packet_overhead"));
				SampleNetwork(Now);
				if (!bClient)
				{
#if USE_NETWORK_PROFILER
					check(!GNetworkProfiler.IsTrackingEnabled());
					FString ProfileName = Directory / TEXT("network.nprof");
					const FString ProfilingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProfilingDir());
					check(FPaths::MakePathRelativeTo(ProfileName, *ProfilingDirectory));
					GNetworkProfiler.SetNextFileName(ProfileName);
					GNetworkProfiler.EnableTracking(true);
					GNetworkProfiler.TrackSessionChange(true, World->URL);
					GNetworkProfiler.TrackFrameBegin();
					bNetworkProfileStarted = GNetworkProfiler.IsTrackingEnabled();
#else
					Save(TEXT("network_profiler_not_compiled")); return true;
#endif
				}
			}
			Census(); ProbeStart = ProbeEnd;
			Frames.Add(TEXT("seconds,frame_ms,sim_steps,sim_ms,combat_ms,alive,ordered_moving,observed_moving,blocked,arrived,in_bytes_s,out_bytes_s,pose_age_ms"));
			if (Authority.IsValid())
			{
				const auto& Stats = Authority->GetPerformanceCounters();
				PreviousSteps = Stats.Steps; PreviousSimulationMs = Stats.SimulationMilliseconds; PreviousCombatMs = Stats.CombatMilliseconds;
				InitialSteps = Authority->GetServerSimTick(); InitialDropped = Authority->GetDroppedFixedStepCount();
				InitialMoves = Authority->GetNavigationStats().MovementUpdateCalls;
			}
			if (Controller.IsValid()) InitialPoseFrames = Controller->GetCommanderNetSyncComponent()->GetAcceptedPoseFrameCount();
			FCsvProfiler::Get()->BeginCapture(-1, Directory, TEXT("engine.csv"));
			if (bCaptureTrace)
			{
				FTraceAuxiliary::FOptions Options;
				Options.bExcludeTail = true;
				bTraceStarted = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File,
					*(Directory / TEXT("client.utrace")), TEXT("cpu,gpu,frame,bookmark,task"), &Options);
				if (!bTraceStarted) { Save(TEXT("trace_start_failed")); return true; }
			}
			UE_LOG(LogGuLiStrike, Display, TEXT("MoveStress capture started count=%d seconds=%.1f"), Population, Duration);
			return true;
		}
		if (Now >= NextCensus) { Census(); NextCensus = Now + .5; }
		uint64 Steps = 0; double SimMs = 0, CombatMs = 0;
		if (Authority.IsValid())
		{
			const auto& Stats = Authority->GetPerformanceCounters();
			Steps = Stats.Steps - PreviousSteps; SimMs = Stats.SimulationMilliseconds - PreviousSimulationMs;
			CombatMs = Stats.CombatMilliseconds - PreviousCombatMs;
			PreviousSteps = Stats.Steps; PreviousSimulationMs = Stats.SimulationMilliseconds; PreviousCombatMs = Stats.CombatMilliseconds;
		}
		double In = 0, Out = 0, PoseAge = 0;
		if (const auto* Driver = World->GetNetDriver())
		{
			if (Driver->ServerConnection) { In = Driver->ServerConnection->InBytesPerSecond; Out = Driver->ServerConnection->OutBytesPerSecond; }
			else for (const auto& Connection : Driver->ClientConnections) { In += Connection->InBytesPerSecond; Out += Connection->OutBytesPerSecond; }
		}
		if (Controller.IsValid()) PoseAge = (Now - Controller->GetCommanderNetSyncComponent()->GetLastAcceptedPoseReceiveTimeSeconds()) * 1000.0;
		Frames.Add(FString::Printf(TEXT("%.6f,%.5f,%llu,%.5f,%.5f,%d,%d,%d,%d,%d,%.0f,%.0f,%.3f"),
			Now - CaptureStarted, (Now - PreviousFrameTime) * 1000.0, Steps, SimMs, CombatMs,
			Alive, Moving, ObservedMoving, Blocked, Arrived, In, Out, PoseAge));
		PreviousFrameTime = Now;
		SampleNetwork(Now);
		if (Now - CaptureStarted >= Duration) Save(TEXT(""));
		return true;
	}
};

void Start(const TArray<FString>& Args, UWorld*)
{
	if (Args.Num() != 6 && Args.Num() != 7) { UE_LOG(LogGuLiStrike, Error, TEXT("MoveStress: count seconds output_dir server|client expected_clients march|shuttle [join-first|late-join]")); return; }
	auto Run = MakeShared<FRun>();
	Run->Population = FCString::Atoi(*Args[0]); Run->Duration = FCString::Atod(*Args[1]);
	Run->Directory = FPaths::ConvertRelativePathToFull(Args[2]); Run->bClient = Args[3] == TEXT("client");
	Run->bCaptureTrace = Run->bClient && FParse::Param(FCommandLine::Get(), TEXT("MoveStressTrace"));
	Run->bCaptureNetwork = FParse::Param(FCommandLine::Get(), TEXT("MoveStressNetworkProfile"));
	Run->ExpectedClients = FCString::Atoi(*Args[4]);
	check(Args[5] == TEXT("march") || Args[5] == TEXT("shuttle"));
	Run->bShuttle = Args[5] == TEXT("shuttle");
	Run->bJoinBeforePopulation = Args.Num() == 7 && Args[6] == TEXT("join-first");
	check(Run->Population >= 100 && Run->Population <= 16000 && Run->Population % 2 == 0 && Run->Duration >= 10);
	checkf(!FPaths::FileExists(Run->Directory / TEXT("summary.json")), TEXT("Do not overwrite stress evidence."));
	IFileManager::Get().MakeDirectory(*Run->Directory, true);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Delta) { return Run->Tick(Delta); }));
}
FAutoConsoleCommandWithWorldAndArgs Command(TEXT("gs.Commander.MoveStress"),
	TEXT("Opt-in independent-process movement profiling: count seconds output_dir server|client expected_clients march|shuttle [join-first|late-join]. Exits after capture."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
}
#endif
