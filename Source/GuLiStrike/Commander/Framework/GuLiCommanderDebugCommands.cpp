// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if !UE_BUILD_SHIPPING

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace GuLiCommanderDebugCommands
{
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

	void SmokeCommandFlow(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		if (!World || World->GetNetMode() == NM_Client)
		{
			LogCommandFlowSmokeFailure(TEXT("requires standalone or server authority"));
			return;
		}

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
			LogCommandFlowSmokeFailure(TEXT("commander bootstrap or Mass population is not ready"));
			return;
		}
		UNavigationSystemV1* NavigationSystem =
			FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		ANavigationData* CommanderNavigationData = NavigationSystem
			? GuLiCommanderNavigationPolicy::ResolveRequiredNavigationData(*NavigationSystem)
			: nullptr;
		if (!CommanderNavigationData)
		{
			LogCommandFlowSmokeFailure(
				TEXT("CommanderSoldier 750cm NavData is unavailable; Default fallback is forbidden"));
			return;
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
			LogCommandFlowSmokeFailure(TEXT("no live soldier belongs to the commander team"));
			return;
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
			return;
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
		NetSync->ServerIssueMove(MoveRequest);
		const FGuLiCommandAck MoveAck = NetSync->GetLastCommandAck();
		if (!MoveAck.IsAccepted() || MoveAck.BatchOrderId == 0u)
		{
			UE_LOG(
				LogGuLiStrike,
				Error,
				TEXT("Commander command-flow smoke FAIL: move command was not accepted (result=%u cohort_results=%d first_result=%u)."),
				static_cast<uint8>(MoveAck.Result),
				MoveAck.CohortResults.Num(),
				MoveAck.CohortResults.IsEmpty()
					? MAX_uint8
					: static_cast<uint8>(MoveAck.CohortResults[0].Result));
			return;
		}

		const TArray<FGuLiSoldierId> ProbeMembers = Selection.Cohorts[0].MemberIds;
		FVector InitialCentroid = FVector::ZeroVector;
		int32 InitialCentroidMembers = 0;
		for (const FGuLiSoldierId SoldierId : ProbeMembers)
		{
			FTransform MemberTransform;
			if (Authority->TryGetSoldierTransform(SoldierId, MemberTransform))
			{
				InitialCentroid += MemberTransform.GetLocation();
				++InitialCentroidMembers;
			}
		}
		if (InitialCentroidMembers == 0)
		{
			LogCommandFlowSmokeFailure(TEXT("selected cohort has no authoritative transforms"));
			return;
		}
		InitialCentroid /= static_cast<double>(InitialCentroidMembers);
		TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> WeakAuthority(Authority);
		FTimerHandle VerificationTimer;
		World->GetTimerManager().SetTimer(
			VerificationTimer,
			FTimerDelegate::CreateLambda(
				[WeakAuthority,
					SeedSoldierId,
					ProbeMembers,
					InitialCentroid,
					CommanderNavigationName,
					CommanderNavigationRadius]()
				{
					UGuLiBattleAuthoritySubsystem* AuthorityInstance = WeakAuthority.Get();
					FTransform MovedTransform;
					if (!AuthorityInstance
						|| !AuthorityInstance->TryGetSoldierTransform(SeedSoldierId, MovedTransform))
					{
						LogCommandFlowSmokeFailure(TEXT("probe soldier disappeared during movement"));
						return;
					}
					FVector CurrentCentroid = FVector::ZeroVector;
					int32 CurrentCentroidMembers = 0;
					for (const FGuLiSoldierId SoldierId : ProbeMembers)
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
						LogCommandFlowSmokeFailure(TEXT("selected cohort lost every authoritative transform"));
						return;
					}
					CurrentCentroid /= static_cast<double>(CurrentCentroidMembers);
					const float MovedCentimeters = FVector::Dist2D(CurrentCentroid, InitialCentroid);
					if (MovedCentimeters < 500.0f)
					{
						UE_LOG(
							LogGuLiStrike,
							Error,
							TEXT("Commander command-flow smoke FAIL: accepted cohort centroid advanced only %.0fcm."),
							MovedCentimeters);
						return;
					}
					if (AuthorityInstance->ApplyDamage(FGuLiSoldierId(MAX_uint32), 100u))
					{
						LogCommandFlowSmokeFailure(TEXT("unknown SoldierId was accepted"));
						return;
					}

					int32 DestroyedMembers = 0;
					for (const FGuLiSoldierId SoldierId : ProbeMembers)
					{
						DestroyedMembers += AuthorityInstance->ApplyDamage(SoldierId, 100u) ? 1 : 0;
					}
					if (DestroyedMembers != ProbeMembers.Num())
					{
						LogCommandFlowSmokeFailure(TEXT("cohort SoldierId damage contract failed"));
						return;
					}
					UE_LOG(
						LogGuLiStrike,
						Display,
						TEXT("Commander command-flow smoke PASS: nav=%s radius=%.0fcm dynamic_members=%d moved=%.0fcm destroyed=%d unknown_id=rejected sim_tick=%u."),
						*CommanderNavigationName.ToString(),
						CommanderNavigationRadius,
						ProbeMembers.Num(),
						MovedCentimeters,
						DestroyedMembers,
						AuthorityInstance->GetServerSimTick());
				}),
			1.5f,
			false);
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

		const int32 ParsedAmount = Args.IsEmpty() ? 100 : FCString::Atoi(*Args[0]);
		const uint8 DamageAmount = static_cast<uint8>(FMath::Clamp(ParsedAmount, 1, 255));
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
			TEXT("gs.DamageSelected applied %u damage to %d selected soldiers."),
			DamageAmount,
			AppliedMemberCount);
	}

	FAutoConsoleCommandWithWorldAndArgs DamageSelectedCommand(
		TEXT("gs.DamageSelected"),
		TEXT("Server-only prototype damage: gs.DamageSelected [1..255]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DamageSelected));

	FAutoConsoleCommandWithWorldAndArgs SmokeCommandFlowCommand(
		TEXT("gs.Commander.SmokeCommandFlow"),
		TEXT("Non-shipping dynamic cohort select/move/damage smoke."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SmokeCommandFlow));
}

#endif // !UE_BUILD_SHIPPING
