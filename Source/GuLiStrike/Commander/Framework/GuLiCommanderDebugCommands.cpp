// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if !UE_BUILD_SHIPPING

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Engine/World.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"
#include "HAL/IConsoleManager.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace GuLiCommanderDebugCommands
{
	const TCHAR* NavigationStateToString(const EGuLiSoldierNavigationState State)
	{
		switch (State)
		{
		case EGuLiSoldierNavigationState::Idle: return TEXT("Idle");
		case EGuLiSoldierNavigationState::Normal: return TEXT("Normal");
		case EGuLiSoldierNavigationState::CenterlineRecovery: return TEXT("CenterlineRecovery");
		case EGuLiSoldierNavigationState::PersonalPathRecovery: return TEXT("PersonalPathRecovery");
		case EGuLiSoldierNavigationState::Arrived: return TEXT("Arrived");
		case EGuLiSoldierNavigationState::Blocked: return TEXT("Blocked");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* NavigationFailureToString(const EGuLiSoldierNavigationFailure Failure)
	{
		switch (Failure)
		{
		case EGuLiSoldierNavigationFailure::None: return TEXT("None");
		case EGuLiSoldierNavigationFailure::NavigationUnavailable: return TEXT("NavigationUnavailable");
		case EGuLiSoldierNavigationFailure::SurfaceMoveFailed: return TEXT("SurfaceMoveFailed");
		case EGuLiSoldierNavigationFailure::ExcessiveHeightDelta: return TEXT("ExcessiveHeightDelta");
		case EGuLiSoldierNavigationFailure::PersonalPathFailed: return TEXT("PersonalPathFailed");
		case EGuLiSoldierNavigationFailure::FinalSlotInvalidated: return TEXT("FinalSlotInvalidated");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* MovePlanFailureStageToString(const EGuLiMovePlanFailureStage Stage)
	{
		switch (Stage)
		{
		case EGuLiMovePlanFailureStage::None: return TEXT("None");
		case EGuLiMovePlanFailureStage::MemberInvalid: return TEXT("MemberInvalid");
		case EGuLiMovePlanFailureStage::StartInvalid: return TEXT("StartInvalid");
		case EGuLiMovePlanFailureStage::CandidateProjection: return TEXT("CandidateProjection");
		case EGuLiMovePlanFailureStage::Separation: return TEXT("Separation");
		case EGuLiMovePlanFailureStage::FriendlyReservation: return TEXT("FriendlyReservation");
		case EGuLiMovePlanFailureStage::RestoredReservation: return TEXT("RestoredReservation");
		case EGuLiMovePlanFailureStage::SharedPath: return TEXT("SharedPath");
		case EGuLiMovePlanFailureStage::Connector: return TEXT("Connector");
		case EGuLiMovePlanFailureStage::PersonalPath: return TEXT("PersonalPath");
		case EGuLiMovePlanFailureStage::CandidatesExhausted: return TEXT("CandidatesExhausted");
		default: return TEXT("Unknown");
		}
	}

	FString BuildMovePlanFailureSummary(
		const uint64* FailureCounts,
		const int32 FailureCount,
		const bool bCompactNonNoneStages)
	{
		TArray<FString> Entries;
		Entries.Reserve(10);
		const uint8 StageCount = static_cast<uint8>(EGuLiMovePlanFailureStage::Count);
		for (uint8 StageValue = 1u; StageValue < StageCount; ++StageValue)
		{
			const int32 CountIndex = bCompactNonNoneStages
				? static_cast<int32>(StageValue) - 1
				: static_cast<int32>(StageValue);
			if (!FailureCounts || CountIndex < 0 || CountIndex >= FailureCount
				|| FailureCounts[CountIndex] == 0u)
			{
				continue;
			}
			Entries.Add(FString::Printf(
				TEXT("%s=%llu"),
				MovePlanFailureStageToString(static_cast<EGuLiMovePlanFailureStage>(StageValue)),
				FailureCounts[CountIndex]));
		}
		return Entries.IsEmpty() ? TEXT("none") : FString::Join(Entries, TEXT(","));
	}

	FString BuildFailedSoldierIdSummary(const TArray<FGuLiSoldierId>& FailedSoldierIds)
	{
		constexpr int32 MaximumLoggedSoldierIds = 32;
		const int32 LoggedCount = FMath::Min(FailedSoldierIds.Num(), MaximumLoggedSoldierIds);
		FString Summary;
		for (int32 Index = 0; Index < LoggedCount; ++Index)
		{
			if (!Summary.IsEmpty())
			{
				Summary += TEXT(",");
			}
			Summary += LexToString(FailedSoldierIds[Index].Value);
		}
		if (FailedSoldierIds.Num() > LoggedCount)
		{
			Summary += FString::Printf(TEXT(",...(+%d)"), FailedSoldierIds.Num() - LoggedCount);
		}
		return Summary.IsEmpty() ? TEXT("none") : Summary;
	}

	void LogCommandFlowSmokeFailure(const TCHAR* Reason)
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Commander command-flow smoke FAIL: %s"), Reason);
	}

	AGuLiCommanderPlayerController* FindCommander(UWorld& World)
	{
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			AGuLiCommanderPlayerController* Controller =
				Cast<AGuLiCommanderPlayerController>(It->Get());
			const AGuLiCommanderPlayerState* PlayerState = Controller
				? Controller->GetPlayerState<AGuLiCommanderPlayerState>()
				: nullptr;
			if (PlayerState && PlayerState->IsCommander() && PlayerState->IsSyncReady())
			{
				return Controller;
			}
		}
		return nullptr;
	}

	constexpr float CommandFlowMoveAckPollIntervalSeconds = 0.05f;
	constexpr int32 CommandFlowMoveAckMaximumPolls = 400;
	constexpr float CommandFlowBootstrapPollIntervalSeconds = 0.25f;
	constexpr int32 CommandFlowBootstrapMaximumPolls = 120;

	struct FCommandFlowMoveAckWaitState
	{
		FTimerHandle TimerHandle;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UGuLiCommanderNetSyncComponent> NetSync;
		TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Authority;
		FGuLiMoveRequest MoveRequest;
		FGuLiControlCohortDescriptor ProbeCohort;
		FName CommanderNavigationName = NAME_None;
		float CommanderNavigationRadius = 0.0f;
		int32 PollCount = 0;
	};

	struct FCommandFlowBootstrapWaitState
	{
		FTimerHandle TimerHandle;
		int32 PollCount = 0;
	};

	struct FCommandFlowMovementVerificationState
	{
		TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Authority;
		FGuLiSoldierId ProbeSoldierId;
		TArray<FGuLiSoldierId> ProbeMembers;
		FVector InitialCentroid = FVector::ZeroVector;
		FName CommanderNavigationName = NAME_None;
		float CommanderNavigationRadius = 0.0f;
	};

	void BeginCommandFlowMovementVerification(
		UWorld& World,
		UGuLiBattleAuthoritySubsystem& Authority,
		const FGuLiSoldierId ProbeSoldierId,
		const TArray<FGuLiSoldierId>& ProbeMembers,
		const FName CommanderNavigationName,
		const float CommanderNavigationRadius)
	{
		FVector InitialCentroid = FVector::ZeroVector;
		int32 InitialCentroidMembers = 0;
		for (const FGuLiSoldierId SoldierId : ProbeMembers)
		{
			FTransform MemberTransform;
			if (Authority.TryGetSoldierTransform(SoldierId, MemberTransform))
			{
				InitialCentroid += MemberTransform.GetLocation();
				++InitialCentroidMembers;
			}
		}
		if (InitialCentroidMembers == 0)
		{
			LogCommandFlowSmokeFailure(TEXT("accepted move members have no authoritative transforms"));
			return;
		}
		InitialCentroid /= static_cast<double>(InitialCentroidMembers);

		const TSharedRef<FCommandFlowMovementVerificationState> VerificationState =
			MakeShared<FCommandFlowMovementVerificationState>();
		VerificationState->Authority = &Authority;
		VerificationState->ProbeSoldierId = ProbeSoldierId;
		VerificationState->ProbeMembers = ProbeMembers;
		VerificationState->InitialCentroid = InitialCentroid;
		VerificationState->CommanderNavigationName = CommanderNavigationName;
		VerificationState->CommanderNavigationRadius = CommanderNavigationRadius;
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Commander command-flow movement verification armed: probe=%u members=%d."),
			VerificationState->ProbeSoldierId.Value,
			VerificationState->ProbeMembers.Num());
		FTimerHandle VerificationTimer;
		World.GetTimerManager().SetTimer(
			VerificationTimer,
			FTimerDelegate::CreateLambda(
				[VerificationState]()
				{
					UGuLiBattleAuthoritySubsystem* AuthorityInstance = VerificationState->Authority.Get();
					FTransform MovedTransform;
					if (!AuthorityInstance)
					{
						LogCommandFlowSmokeFailure(TEXT("authority subsystem disappeared during movement"));
						return;
					}
					if (!AuthorityInstance->TryGetSoldierTransform(
							VerificationState->ProbeSoldierId,
							MovedTransform))
					{
						UE_LOG(
							LogGuLiStrike,
							Error,
							TEXT("Commander command-flow smoke FAIL: probe SoldierId=%u disappeared during movement (population_spawned=%d members=%d sim_tick=%u)."),
							VerificationState->ProbeSoldierId.Value,
							AuthorityInstance->HasSpawnedAuthorityPopulation() ? 1 : 0,
							AuthorityInstance->GetAuthoritativeMemberCount(),
							AuthorityInstance->GetServerSimTick());
						return;
					}
					FVector CurrentCentroid = FVector::ZeroVector;
					int32 CurrentCentroidMembers = 0;
					for (const FGuLiSoldierId SoldierId : VerificationState->ProbeMembers)
					{
						FTransform MemberTransform;
						if (AuthorityInstance->TryGetSoldierTransform(SoldierId, MemberTransform))
						{
							CurrentCentroid += MemberTransform.GetLocation();
							++CurrentCentroidMembers;
						}
					}
					if (CurrentCentroidMembers == 0)
					{
						LogCommandFlowSmokeFailure(TEXT("accepted move members lost every authoritative transform"));
						return;
					}
					CurrentCentroid /= static_cast<double>(CurrentCentroidMembers);
					const float MovedCentimeters = FVector::Dist2D(
						CurrentCentroid,
						VerificationState->InitialCentroid);
					if (MovedCentimeters < 500.0f)
					{
						UE_LOG(
							LogGuLiStrike,
							Error,
							TEXT("Commander command-flow smoke FAIL: accepted cohort centroid advanced only %.0fcm."),
							MovedCentimeters);
						return;
					}
					if (AuthorityInstance->ApplyDamage(FGuLiSoldierId(MAX_uint32), 100.0f))
					{
						LogCommandFlowSmokeFailure(TEXT("unknown SoldierId was accepted"));
						return;
					}

					int32 DestroyedMembers = 0;
					for (const FGuLiSoldierId SoldierId : VerificationState->ProbeMembers)
					{
						DestroyedMembers += AuthorityInstance->ApplyDamage(SoldierId, 100.0f) ? 1 : 0;
					}
					if (DestroyedMembers != VerificationState->ProbeMembers.Num())
					{
						LogCommandFlowSmokeFailure(TEXT("cohort SoldierId damage contract failed"));
						return;
					}
					UE_LOG(
						LogGuLiStrike,
						Display,
						TEXT("Commander command-flow smoke PASS: nav=%s radius=%.0fcm dynamic_members=%d moved=%.0fcm destroyed=%d unknown_id=rejected sim_tick=%u."),
						*VerificationState->CommanderNavigationName.ToString(),
						VerificationState->CommanderNavigationRadius,
						VerificationState->ProbeMembers.Num(),
						MovedCentimeters,
						DestroyedMembers,
						AuthorityInstance->GetServerSimTick());
				}),
			1.5f,
			false);
	}

	/** Returns false only while startup dependencies are still expected to become ready. */
	bool TryBeginSmokeCommandFlow(UWorld* World)
	{
		AGuLiCommanderPlayerController* Controller = FindCommander(*World);
		AGuLiCommanderPlayerState* PlayerState = Controller
			? Controller->GetPlayerState<AGuLiCommanderPlayerState>()
			: nullptr;
		UGuLiCommanderNetSyncComponent* NetSync = Controller
			? Controller->GetCommanderNetSyncComponent()
			: nullptr;
		UGuLiBattleAuthoritySubsystem* Authority =
			World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		if (!PlayerState || !NetSync || !Authority || !Authority->HasSpawnedAuthorityPopulation())
		{
			return false;
		}
		UNavigationSystemV1* NavigationSystem =
			FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		ANavigationData* CommanderNavigationData = NavigationSystem
			? GuLiCommanderNavigationPolicy::ResolveRequiredNavigationData(*NavigationSystem)
			: nullptr;
		if (!CommanderNavigationData)
		{
			return false;
		}
		const FName CommanderNavigationName = CommanderNavigationData->GetConfig().Name;
		const float CommanderNavigationRadius = CommanderNavigationData->GetConfig().AgentRadius;

		TArray<FGuLiSoldierStateItem> States;
		Authority->BuildSoldierStateSnapshot(States);
		FGuLiSoldierId SeedSoldierId;
		FTransform SeedTransform;
		for (const FGuLiSoldierStateItem& State : States)
		{
			if (State.Team == PlayerState->GetTeam() && State.IsAlive()
				&& Authority->TryGetSoldierTransform(State.SoldierId, SeedTransform))
			{
				SeedSoldierId = State.SoldierId;
				break;
			}
		}
		if (!SeedSoldierId.IsValid())
		{
			return false;
		}

		uint32 RequestSerial = FPlatformTime::Cycles();
		RequestSerial = RequestSerial == 0u ? 1u : RequestSerial;
		FGuLiSelectionRequest SelectionRequest;
		SelectionRequest.Center = SeedTransform.GetLocation();
		SelectionRequest.RadiusPreset = EGuLiSelectionRadiusPreset::Small;
		SelectionRequest.Modifier = EGuLiSelectionModifier::Replace;
		SelectionRequest.ClientRequestId = RequestSerial;
		SelectionRequest.KnownSelectionRevision = NetSync->GetSelectionState().SelectionRevision;
		NetSync->ServerRequestSelection(SelectionRequest);

		const FGuLiCommanderSelectionState Selection = NetSync->GetSelectionState();
		if (Selection.Cohorts.IsEmpty() || !NetSync->GetLastCommandAck().IsAccepted()
			|| !Selection.Cohorts[0].Contains(SeedSoldierId)
			|| Selection.Cohorts[0].MemberIds.Num() > static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE))
		{
			LogCommandFlowSmokeFailure(TEXT("dynamic 25-soldier cohort selection was not accepted"));
			return true;
		}

		FVector TravelDirection = -SeedTransform.GetLocation().GetSafeNormal2D();
		if (TravelDirection.IsNearlyZero())
		{
			TravelDirection = FVector::ForwardVector;
		}
		FGuLiMoveRequest MoveRequest;
		// Keep the smoke target in the seed formation's local connected NavMesh island.
		// Long arbitrary probes can legitimately cross a steep ridge or water gap on LVL_Main.
		MoveRequest.Target = SeedTransform.GetLocation() + TravelDirection * 20000.0f;
		MoveRequest.SelectionRevision = Selection.SelectionRevision;
		MoveRequest.ClientCommandId = RequestSerial + 1u;
		MoveRequest.ClientCommandId = MoveRequest.ClientCommandId == 0u ? 1u : MoveRequest.ClientCommandId;
		const FGuLiControlCohortDescriptor ProbeCohort = Selection.Cohorts[0];
		NetSync->ServerIssueMove(MoveRequest);

		const TSharedRef<FCommandFlowMoveAckWaitState> WaitState =
			MakeShared<FCommandFlowMoveAckWaitState>();
		WaitState->World = World;
		WaitState->NetSync = NetSync;
		WaitState->Authority = Authority;
		WaitState->MoveRequest = MoveRequest;
		WaitState->ProbeCohort = ProbeCohort;
		WaitState->CommanderNavigationName = CommanderNavigationName;
		WaitState->CommanderNavigationRadius = CommanderNavigationRadius;
		World->GetTimerManager().SetTimer(
			WaitState->TimerHandle,
			FTimerDelegate::CreateLambda(
				[WaitState]()
				{
					UWorld* WorldInstance = WaitState->World.Get();
					UGuLiCommanderNetSyncComponent* NetSyncInstance = WaitState->NetSync.Get();
					UGuLiBattleAuthoritySubsystem* AuthorityInstance = WaitState->Authority.Get();
					if (!WorldInstance || !NetSyncInstance || !AuthorityInstance)
					{
						LogCommandFlowSmokeFailure(TEXT("world, commander sync, or authority disappeared while waiting for move ACK"));
						if (WorldInstance)
						{
							WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
						}
						return;
					}

					const FGuLiCommandAck MoveAck = NetSyncInstance->GetLastCommandAck();
					if (MoveAck.CommandKind == EGuLiCommandKind::Move
						&& MoveAck.ClientCommandId == WaitState->MoveRequest.ClientCommandId)
					{
						if (!MoveAck.IsAccepted() || MoveAck.BatchOrderId == 0u)
						{
							UE_LOG(
								LogGuLiStrike,
								Error,
								TEXT("Commander command-flow smoke FAIL: async move command was not accepted (command=%u result=%u cohort_results=%d first_result=%u)."),
								MoveAck.ClientCommandId,
								static_cast<uint8>(MoveAck.Result),
								MoveAck.CohortResults.Num(),
								MoveAck.CohortResults.IsEmpty()
									? MAX_uint8
									: static_cast<uint8>(MoveAck.CohortResults[0].Result));
							WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
							return;
						}

						const FGuLiCohortCommandAck* ProbeCohortAck =
							MoveAck.CohortResults.FindByPredicate(
								[WaitState](const FGuLiCohortCommandAck& CohortAck)
								{
									return CohortAck.CohortId == WaitState->ProbeCohort.CohortId;
								});
						if (!ProbeCohortAck
							|| ProbeCohortAck->MemberCount != WaitState->ProbeCohort.MemberIds.Num())
						{
							UE_LOG(
								LogGuLiStrike,
								Error,
								TEXT("Commander command-flow smoke FAIL: final move ACK is missing the frozen cohort membership (cohort=%u expected=%d actual=%d)."),
								WaitState->ProbeCohort.CohortId.Value,
								WaitState->ProbeCohort.MemberIds.Num(),
								ProbeCohortAck ? static_cast<int32>(ProbeCohortAck->MemberCount) : -1);
							WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
							return;
						}

						TArray<FGuLiSoldierId> AcceptedProbeMembers;
						AcceptedProbeMembers.Reserve(WaitState->ProbeCohort.MemberIds.Num());
						for (int32 MemberIndex = 0;
							MemberIndex < WaitState->ProbeCohort.MemberIds.Num();
							++MemberIndex)
						{
							if (ProbeCohortAck->IsMemberAccepted(static_cast<uint8>(MemberIndex)))
							{
								AcceptedProbeMembers.Add(WaitState->ProbeCohort.MemberIds[MemberIndex]);
							}
						}
						if (AcceptedProbeMembers.IsEmpty())
						{
							UE_LOG(
								LogGuLiStrike,
								Error,
								TEXT("Commander command-flow smoke FAIL: probe cohort %u accepted no members (result=%u eligible=0x%08x accepted=0x%08x)."),
								WaitState->ProbeCohort.CohortId.Value,
								static_cast<uint8>(ProbeCohortAck->Result),
								ProbeCohortAck->EligibleMemberMask,
								ProbeCohortAck->AcceptedMemberMask);
							WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
							return;
						}

						BeginCommandFlowMovementVerification(
							*WorldInstance,
							*AuthorityInstance,
							AcceptedProbeMembers[0],
							AcceptedProbeMembers,
							WaitState->CommanderNavigationName,
							WaitState->CommanderNavigationRadius);
						WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
						return;
					}

					++WaitState->PollCount;
					if (WaitState->PollCount < CommandFlowMoveAckMaximumPolls)
					{
						return;
					}

					UE_LOG(
						LogGuLiStrike,
						Error,
						TEXT("Commander command-flow smoke FAIL: timed out after %.1fs waiting for final move ACK command=%u (last_kind=%u last_id=%u last_result=%u planner_pending=%d)."),
						CommandFlowMoveAckPollIntervalSeconds * CommandFlowMoveAckMaximumPolls,
						WaitState->MoveRequest.ClientCommandId,
						static_cast<uint8>(MoveAck.CommandKind),
						MoveAck.ClientCommandId,
						static_cast<uint8>(MoveAck.Result),
						NetSyncInstance->IsServerMovePlanningPending(WaitState->MoveRequest) ? 1 : 0);
					WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
				}),
			CommandFlowMoveAckPollIntervalSeconds,
			true);
		return true;
	}

	void SmokeCommandFlow(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty() || !World || World->GetNetMode() == NM_Client)
		{
			LogCommandFlowSmokeFailure(TEXT("usage requires standalone or server authority with no arguments"));
			return;
		}
		if (TryBeginSmokeCommandFlow(World))
		{
			return;
		}

		TWeakObjectPtr<UWorld> WeakWorld(World);
		const TSharedRef<FCommandFlowBootstrapWaitState> WaitState =
			MakeShared<FCommandFlowBootstrapWaitState>();
		World->GetTimerManager().SetTimer(
			WaitState->TimerHandle,
			FTimerDelegate::CreateLambda([WeakWorld, WaitState]()
			{
				UWorld* WorldInstance = WeakWorld.Get();
				if (!WorldInstance)
				{
					LogCommandFlowSmokeFailure(TEXT("world disappeared while waiting for commander bootstrap"));
					return;
				}
				if (TryBeginSmokeCommandFlow(WorldInstance))
				{
					WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
					return;
				}
				++WaitState->PollCount;
				if (WaitState->PollCount < CommandFlowBootstrapMaximumPolls)
				{
					return;
				}
				WorldInstance->GetTimerManager().ClearTimer(WaitState->TimerHandle);
				UE_LOG(
					LogGuLiStrike,
					Error,
					TEXT("Commander command-flow smoke FAIL: timed out after %.1fs waiting for commander bootstrap, CommanderSoldier NavData, and Mass population."),
					CommandFlowBootstrapPollIntervalSeconds * CommandFlowBootstrapMaximumPolls);
			}),
			CommandFlowBootstrapPollIntervalSeconds,
			true);
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Commander command-flow smoke waiting up to %.1fs for commander bootstrap, CommanderSoldier NavData, and Mass population."),
			CommandFlowBootstrapPollIntervalSeconds * CommandFlowBootstrapMaximumPolls);
	}

	void DamageSelected(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("gs.DamageSelected is server/standalone only."));
			return;
		}
		UGuLiBattleAuthoritySubsystem* Authority =
			World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		AGuLiCommanderPlayerController* Controller = FindCommander(*World);
		UGuLiCommanderNetSyncComponent* NetSync = Controller
			? Controller->GetCommanderNetSyncComponent()
			: nullptr;
		if (!Authority || !NetSync)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander authority or selection is not ready."));
			return;
		}

		double ParsedAmount = 100.0;
		if (Args.Num() > 1
			|| (!Args.IsEmpty() && !GuLiRuntimeTuning::TryParseFiniteNumber(Args[0], ParsedAmount))
			|| ParsedAmount <= 0.0 || ParsedAmount > 1000000000.0
			|| static_cast<float>(ParsedAmount) <= 0.0f)
		{
			UE_LOG(LogGuLiStrike, Warning,
				TEXT("Usage: gs.DamageSelected [amount]; amount must be a finite positive float <= 1e9 (default 100)."));
			return;
		}
		const float DamageAmount = static_cast<float>(ParsedAmount);
		int32 AppliedMemberCount = 0;
		TSet<uint32> DamagedIds;
		for (const FGuLiControlCohortDescriptor& Cohort : NetSync->GetSelectionState().Cohorts)
		{
			for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
			{
				if (!DamagedIds.Contains(SoldierId.Value))
				{
					DamagedIds.Add(SoldierId.Value);
					AppliedMemberCount += Authority->ApplyDamage(SoldierId, DamageAmount) ? 1 : 0;
				}
			}
		}
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("gs.DamageSelected applied %.9g damage to %d selected soldiers."),
			DamageAmount,
			AppliedMemberCount);
	}

	void LogSoldierNavigation(const TArray<FString>& Args, UWorld* World)
	{
		uint32 SoldierValue = 0u;
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 1
			|| !LexTryParseString(SoldierValue, *Args[0]) || SoldierValue == 0u)
		{
			UE_LOG(LogGuLiStrike, Warning,
				TEXT("Usage: gs.GM.Commander.Nav.Soldier <SoldierId> (server/standalone only)."));
			return;
		}
		UGuLiBattleAuthoritySubsystem* Authority =
			World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		FGuLiSoldierNavigationDebug Navigation;
		FGuLiSoldierCombatDebug Combat;
		if (!Authority
			|| !Authority->TryGetSoldierNavigationDebug(FGuLiSoldierId(SoldierValue), Navigation)
			|| !Authority->TryGetSoldierCombatDebug(FGuLiSoldierId(SoldierValue), Combat))
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander nav Soldier %u was not found."), SoldierValue);
			return;
		}
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Commander nav soldier=%u team=%u state=%s moving=%d order=%u completed=%u failed_order=%u failure=%s failure_time=%.3fs location=%s last_nav=%s final=%s distance=%.1fcm waypoint=%s path_index=%d no_progress=%.3fs surface_failures=%d/%d personal_queries=%d combat_target=%u cooldown=%.3fs combat_stop=%s."),
			Navigation.SoldierId.Value,
			static_cast<uint8>(Navigation.Team),
			NavigationStateToString(Navigation.State),
			Navigation.bMoving ? 1 : 0,
			Navigation.ActiveOrderId,
			Navigation.LastCompletedOrderId,
			Navigation.LastFailedOrderId,
			NavigationFailureToString(Navigation.Failure),
			Navigation.FailureSimulationSeconds,
			*Navigation.Location.ToCompactString(),
			*Navigation.LastValidNavLocation.ToCompactString(),
			Navigation.bHasFinalSlot ? *Navigation.FinalSlot.ToCompactString() : TEXT("none"),
			Navigation.DistanceToFinalSlotCentimeters,
			*Navigation.CurrentWaypoint.ToCompactString(),
			Navigation.PathPointIndex,
			Navigation.NoProgressSeconds,
			Navigation.ConsecutiveSurfaceFailures,
			Navigation.TotalSurfaceFailures,
			Navigation.PersonalPathRetries,
			Combat.TargetId.Value,
			Combat.CooldownRemaining,
			GuLiSoldierCombat::LexToString(Combat.StopReason));
	}

	void LogNavigationStats(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || !Args.IsEmpty())
		{
			UE_LOG(LogGuLiStrike, Warning,
				TEXT("Usage: gs.GM.Commander.Nav.Stats (server/standalone only)."));
			return;
		}
		const UGuLiBattleAuthoritySubsystem* Authority =
			World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		if (!Authority)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander authority is not ready."));
			return;
		}
		const FGuLiNavigationStats Stats = Authority->GetNavigationStats();
		const FString PlanningFailureSummary = BuildMovePlanFailureSummary(
			Stats.MovePlanningFailureCounts,
			UE_ARRAY_COUNT(Stats.MovePlanningFailureCounts),
			true);
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Commander nav stats alive=%d active=%d idle=%d arrived=%d centerline=%d personal=%d blocked=%d surface=%llu failed=%llu paths=%llu personal_paths=%llu pending_plans=%d candidate_projections=%llu planning_paths=%llu partial_moves=%llu plan_failures={%s} plan_ms=%.3f max_plan_ms=%.3f slowest=%u no_progress=%.3fs."),
			Stats.Alive,
			Stats.Active,
			Stats.Idle,
			Stats.Arrived,
			Stats.CenterlineRecovery,
			Stats.PersonalPathRecovery,
			Stats.Blocked,
			Stats.SurfaceMoveCalls,
			Stats.SurfaceMoveFailures,
			Stats.PathQueries,
			Stats.PersonalPathQueries,
			Stats.PendingMovePlanningTasks,
			Stats.MoveCandidateProjectionQueries,
			Stats.MovePlanningPathQueries,
			Stats.PartiallyAcceptedMoveCommands,
			*PlanningFailureSummary,
			Stats.LastDestinationPlanningMilliseconds,
			Stats.MaximumDestinationPlanningMilliseconds,
			Stats.SlowestSoldierId.Value,
			Stats.SlowestNoProgressSeconds);
	}

	void LogLastMovePlanning(const TArray<FString>& Args, UWorld* World)
	{
		uint32 CohortFilter = 0u;
		if (!World || World->GetNetMode() == NM_Client || Args.Num() > 1
			|| (Args.Num() == 1
				&& (!LexTryParseString(CohortFilter, *Args[0]) || CohortFilter == 0u)))
		{
			UE_LOG(LogGuLiStrike, Warning,
				TEXT("Usage: gs.GM.Commander.Nav.LastMove [CohortId] (server/standalone only)."));
			return;
		}
		const UGuLiBattleAuthoritySubsystem* Authority =
			World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		if (!Authority)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander authority is not ready."));
			return;
		}
		FGuLiMovePlanningDebug Debug;
		if (!Authority->TryGetLastMovePlanningDebug(Debug))
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander has no completed move-planning diagnostic."));
			return;
		}
		if (CohortFilter != 0u)
		{
			const FGuLiMoveCohortPlanningDebug* Cohort = Debug.Cohorts.FindByPredicate(
				[CohortFilter](const FGuLiMoveCohortPlanningDebug& Candidate)
				{
					return Candidate.CohortId.Value == CohortFilter;
				});
			if (!Cohort)
			{
				UE_LOG(
					LogGuLiStrike,
					Warning,
					TEXT("Commander last move command=%u has no CohortId %u."),
					Debug.ClientCommandId,
					CohortFilter);
				return;
			}
			const FString CohortFailedIds =
				BuildFailedSoldierIdSummary(Cohort->FailedSoldierIds);
			UE_LOG(
				LogGuLiStrike,
				Display,
				TEXT("Commander nav last_move command=%u batch=%u cohort=%u members=%u eligible=0x%08x accepted=0x%08x failed_ids={%s} target=%s plan_ms=%.3f."),
				Debug.ClientCommandId,
				Debug.BatchOrderId,
				Cohort->CohortId.Value,
				Cohort->MemberCount,
				Cohort->EligibleMemberMask,
				Cohort->AcceptedMemberMask,
				*CohortFailedIds,
				*Debug.RequestedTarget.ToCompactString(),
				Debug.PlanningMilliseconds);
			return;
		}
		const FString FailedSoldierSummary = BuildFailedSoldierIdSummary(Debug.FailedSoldierIds);
		const FString FailureSummary = BuildMovePlanFailureSummary(
			Debug.FailureCounts,
			UE_ARRAY_COUNT(Debug.FailureCounts),
			false);
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Commander nav last_move command=%u batch=%u target=%s theoretical=%d projected=%d legal=%d candidate_queries=%d path_queries=%d route_splits=%d reservation_conflicts=%d accepted=%d failed=%d max_radius=%.1fcm plan_ms=%.3f failed_ids={%s} failures={%s}."),
			Debug.ClientCommandId,
			Debug.BatchOrderId,
			*Debug.RequestedTarget.ToCompactString(),
			Debug.TheoreticalCandidates,
			Debug.ProjectedCandidates,
			Debug.LegalCandidates,
			Debug.CandidateProjectionQueries,
			Debug.PathQueries,
			Debug.RouteSplitCount,
			Debug.ReservationConflictCount,
			Debug.AcceptedMembers,
			Debug.FailedMembers,
			Debug.MaximumSearchRadiusCentimeters,
			Debug.PlanningMilliseconds,
			*FailedSoldierSummary,
			*FailureSummary);
	}

	FAutoConsoleCommandWithWorldAndArgs DamageSelectedCommand(
		TEXT("gs.DamageSelected"),
		TEXT("Server-only prototype damage: gs.DamageSelected [positive float <= 1e9], default 100."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DamageSelected));

	FAutoConsoleCommandWithWorldAndArgs SmokeCommandFlowCommand(
		TEXT("gs.Commander.SmokeCommandFlow"),
		TEXT("Non-shipping dynamic cohort select/move/damage smoke."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SmokeCommandFlow));

	FAutoConsoleCommandWithWorldAndArgs NavigationSoldierCommand(
		TEXT("gs.GM.Commander.Nav.Soldier"),
		TEXT("Server navigation state: gs.GM.Commander.Nav.Soldier <SoldierId>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogSoldierNavigation));

	FAutoConsoleCommandWithWorldAndArgs NavigationStatsCommand(
		TEXT("gs.GM.Commander.Nav.Stats"),
		TEXT("Server aggregate Commander navigation statistics."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogNavigationStats));

	FAutoConsoleCommandWithWorldAndArgs NavigationLastMoveCommand(
		TEXT("gs.GM.Commander.Nav.LastMove"),
		TEXT("Last completed server move-plan summary: gs.GM.Commander.Nav.LastMove [CohortId]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogLastMovePlanning));
}

#endif // !UE_BUILD_SHIPPING
