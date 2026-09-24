// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if !UE_BUILD_SHIPPING

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"

namespace GuLiSkillNetworkQA
{
// This opt-in runner mutates only disposable QA matches. All gameplay calls originate
// on the native game thread; there are no Python/Slate calls, synthetic RPCs or ready flags.
constexpr double TimeoutSeconds = 90.0;
constexpr double StageSeconds = 2.0;
constexpr double StressDurationSeconds = 20.0;
const FName BasicAttack(TEXT("BasicAttack"));
const FName Strafe(TEXT("Strafe"));
const FName StrafeTest(TEXT("StrafeTest"));

class FRunner : public TSharedFromThis<FRunner>
{
public:
	FRunner(bool bInServer, UWorld* InWorld)
		: bServer(bInServer), World(InWorld), StartedAt(FPlatformTime::Seconds())
	{
		Report = MakeShared<FJsonObject>();
		Checks = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("mode"), bServer ? TEXT("Server") : TEXT("Client"));
		Report->SetStringField(TEXT("origin"), TEXT("native FTSTicker; server PlayerState authority command"));
		Report->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
		Report->SetNumberField(TEXT("protocol"), GULI_COMMANDER_PROTOCOL_VERSION);
		Report->SetNumberField(TEXT("schema_version"), 1);
		bStress = FParse::Param(FCommandLine::Get(), TEXT("GuLiSkillQAStress"));
		Report->SetBoolField(TEXT("stress_mode"), bStress);
		if (bStress)
		{
			Report->SetStringField(TEXT("origin"), TEXT("native FTSTicker stress observer; no skill commands or debug spawns"));
			Report->SetNumberField(TEXT("stress_duration_s"), StressDurationSeconds);
			Report->SetBoolField(TEXT("shot_count_available"), bServer);
		}
		int32 RequestedPopulation = 500;
		FParse::Value(FCommandLine::Get(), TEXT("GuLiSkillQAPopulation="), RequestedPopulation);
		PopulationTarget = bStress ? 500 : FMath::Clamp(RequestedPopulation, 500, 10000);
		Report->SetNumberField(TEXT("requested_population"), PopulationTarget);
		Report->SetStringField(TEXT("network_measurement_scope"), TEXT("Connection totals include all gameplay traffic, not skill-only bandwidth."));
		OutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("outputs/skill-bridge"),
			FString::Printf(TEXT("qa-%s-%u.json"), bServer ? TEXT("server") : TEXT("client"), FPlatformProcess::GetCurrentProcessId())));
	}

	void Start()
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(AsShared(), &FRunner::Tick), 0.1f);
		UE_LOG(LogGuLiStrike, Display, TEXT("Skill native QA armed mode=%s output=%s timeout=90s"),
			bServer ? TEXT("Server") : TEXT("Client"), *OutputPath);
		WriteReport(false, TEXT("waiting for native World and connection readiness"));
	}
	bool IsFinished() const { return bFinished; }

private:
	void Check(const FString& Key, bool bPassed)
	{
		bool bPrevious = true;
		Checks->TryGetBoolField(Key, bPrevious);
		Checks->SetBoolField(Key, bPrevious && bPassed);
		if (!bPassed)
		{
			Errors.Add(MakeShared<FJsonValueString>(Key));
			UE_LOG(LogGuLiStrike, Warning, TEXT("Skill native QA failed check=%s stage=%d"), *Key, Stage);
		}
	}

	bool HasPassed(const TCHAR* Key) const
	{
		bool bValue = false;
		return Checks->TryGetBoolField(Key, bValue) && bValue;
	}

	bool ResolveWorld()
	{
		const auto Matches = [this](const UWorld* Candidate)
		{
			return Candidate && Candidate->IsGameWorld() && Candidate->HasBegunPlay()
				&& ((Candidate->GetNetMode() != NM_Client) == bServer);
		};
		if (Matches(World.Get())) return true;
		if (!GEngine) return false;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Matches(Context.World())) { World = Context.World(); return true; }
		}
		return false;
	}

	AGuLiBattlePlayerState* FindCommander(EGuLiTeam Team) const
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			auto* PS = PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
			if (PS && PS->GetTeam() == Team && PS->IsCommander() && PS->IsBattleReady() && PS->IsSoldierStreamReady()) return PS;
		}
		return nullptr;
	}

	bool Execute(AGuLiBattlePlayerState& PS, const FGuLiArmySkillCommand& Command,
		const TCHAR* Label, bool bExpected = true)
	{
		FString Error;
		const bool bSucceeded = PS.ExecuteArmySkillCommand(Command, Error);
		Check(Label, bSucceeded == bExpected);
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("label"), Label);
		Row->SetNumberField(TEXT("team"), static_cast<int32>(PS.GetTeam()));
		Row->SetBoolField(TEXT("accepted"), bSucceeded);
		Row->SetBoolField(TEXT("expected"), bExpected);
		Row->SetStringField(TEXT("error"), Error);
		Commands.Add(MakeShared<FJsonValueObject>(Row));
		return bSucceeded == bExpected;
	}

	FGuLiArmySkillCommand SourceCommand(FGuid Id, float Percent) const
	{
		FGuLiArmySkillCommand Command;
		Command.Source.SourceInstanceId = Id;
		Command.Source.DebugLabel = TEXT("Native network QA damage source");
		auto& Modifier = Command.Source.Modifiers.AddDefaulted_GetRef();
		Modifier.Target.UnitTypeIds = {1};
		Modifier.Operation = EGuLiSkillModifierOperation::AddPercent;
		Modifier.Magnitude = Percent;
		return Command;
	}

	FGuLiArmySkillCommand ReplacementCommand(FGuid Id, FName SkillId) const
	{
		FGuLiArmySkillCommand Command;
		Command.Source.SourceInstanceId = Id;
		Command.Source.DebugLabel = TEXT("Native network QA slot replacement");
		auto& Replacement = Command.Source.Replacements.AddDefaulted_GetRef();
		Replacement.Target.UnitTypeIds = {1};
		Replacement.SkillId = SkillId;
		Replacement.Priority = 10;
		return Command;
	}

	FGuLiArmySkillCommand RemoveCommand(FGuid Id) const
	{
		FGuLiArmySkillCommand Command;
		Command.Command = EGuLiArmySkillCommand::RemoveSource;
		Command.SourceInstanceId = Id;
		return Command;
	}

	FGuLiArmySkillCommand OverrideCommand(uint16 UnitTypeId, float Damage, bool bClear = false) const
	{
		FGuLiArmySkillCommand Command;
		Command.Command = bClear ? EGuLiArmySkillCommand::ClearNumericOverride : EGuLiArmySkillCommand::SetNumericOverride;
		Command.NumericOverride.UnitTypeId = UnitTypeId;
		Command.NumericOverride.bOverrideDamage = true;
		Command.NumericOverride.Damage = Damage;
		return Command;
	}

	void CheckProfile(UGuLiArmySkillSubsystem& Skills, const TCHAR* Label, EGuLiTeam Team,
		uint16 UnitType, float Damage, FName SkillId = FName(TEXT("Strafe")))
	{
		const auto* Profile = Skills.FindResolvedSkill(Team, UnitType);
		Check(Label, Profile && Profile->SkillId == SkillId && FMath::IsNearlyEqual(Profile->Damage, Damage, 0.001f));
	}

	bool SpawnIsolatedPair(UGuLiBattleAuthoritySubsystem& Authority)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		ANavigationData* Nav = Navigation ? GuLiCommanderNavigationPolicy::ResolveRequiredNavigationData(*Navigation) : nullptr;
		if (!Navigation || !Nav) return false;
		TArray<FGuLiSoldierStateItem> States;
		Authority.BuildSoldierStateSnapshot(States);
		TArray<FVector> Occupied;
		for (const auto& State : States)
		{
			FGuLiSoldierCombatDebug Debug;
			if (State.IsAlive() && Authority.TryGetSoldierCombatDebug(State.SoldierId, Debug)) Occupied.Add(Debug.Location);
		}
		if (Occupied.IsEmpty()) return false;
		const FVector Origin = Occupied[0];
		for (float Distance : {50000.0f, 80000.0f, 120000.0f, 160000.0f})
		{
			for (int32 Direction = 0; Direction < 12; ++Direction)
			{
				const float Angle = Direction * (2.0f * PI / 12.0f);
				const FVector Candidate = Origin + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
				FNavLocation A, B;
				if (!Navigation->ProjectPointToNavigation(Candidate, A, FVector(750.0f, 750.0f, 5000.0f), Nav)
					|| !Navigation->ProjectPointToNavigation(Candidate + FVector(3000.0f, 0.0f, 0.0f), B,
						FVector(750.0f, 750.0f, 5000.0f), Nav)
					|| FVector::DistSquared2D(A.Location, B.Location) > FMath::Square(5000.0f)) continue;
				if (Occupied.ContainsByPredicate([&](const FVector& P)
					{ return FVector::DistSquared2D(P, A.Location) < FMath::Square(40000.0f)
						|| FVector::DistSquared2D(P, B.Location) < FMath::Square(40000.0f); })) continue;
				if (!Authority.SpawnDebugSoldier(EGuLiTeam::Red, 2, A.Location, PairRed)) continue;
				if (!Authority.SpawnDebugSoldier(EGuLiTeam::Blue, 2, B.Location, PairBlue))
				{
					Authority.ApplyDamage(PairRed, 1000000000.0f);
					return false;
				}
				FGuLiSoldierCombatDebug Debug;
				const bool bRead = Authority.TryGetSoldierCombatDebug(PairRed, Debug);
				Check(TEXT("server_spawn_inherits_current_profile"), bRead && FMath::IsNearlyEqual(Debug.Damage, 22.5f)
					&& Debug.SkillId == Strafe && FMath::IsNearlyEqual(Debug.MaxHealth, 300.5f)
					&& FMath::IsNearlyEqual(Debug.Health, 300.5f));
				Report->SetNumberField(TEXT("pair_red_id"), PairRed.Value);
				Report->SetNumberField(TEXT("pair_blue_id"), PairBlue.Value);
				return true;
			}
		}
		return false;
	}

	bool PreparePopulation(UGuLiBattleAuthoritySubsystem& Authority)
	{
		if (bPopulationPrepared) return true;
		if (PopulationSeeds.IsEmpty())
		{
			TArray<FGuLiSoldierStateItem> States;
			Authority.BuildSoldierStateSnapshot(States);
			if (States.IsEmpty()) return false;
			PopulationCount = States.Num();
			Report->SetNumberField(TEXT("initial_population"), PopulationCount);
			// The normal prototype stays unchanged. A requested larger run reserves
			// two entity slots for the isolated combat pair (the authority caps at 10000).
			if (PopulationTarget <= 500) { bPopulationPrepared = true; return true; }
			for (const auto& State : States)
			{
				FGuLiSoldierCombatDebug Seed;
				if (State.IsAlive() && Authority.TryGetSoldierCombatDebug(State.SoldierId, Seed)) PopulationSeeds.Add(Seed);
			}
			if (PopulationSeeds.IsEmpty()) return false;
		}
		const int32 DesiredBeforePair = PopulationTarget - 2;
		// Bound work per native ticker callback. Every entity still goes through the
		// normal data/profile initialization, dedicated NavMesh projection and Mass creation.
		for (int32 Budget = 0; Budget < 32 && PopulationCount < DesiredBeforePair; ++Budget)
		{
			const auto& Seed = PopulationSeeds[PopulationSpawnAttempts % PopulationSeeds.Num()];
			const int32 Layer = PopulationSpawnAttempts / PopulationSeeds.Num() + 1;
			const FVector Offset(((Layer % 7) - 3) * 1800.0f, (((Layer / 7) % 7) - 3) * 1800.0f, 0.0f);
			++PopulationSpawnAttempts;
			FGuLiSoldierId NewId;
			if (Authority.SpawnDebugSoldier(Seed.Team, 1, Seed.Location + Offset, NewId)) ++PopulationCount;
			if (PopulationSpawnAttempts >= 100000)
			{ Check(TEXT("server_population_spawn_budget"), false); Finish(TEXT("could not project the requested population to the dedicated NavMesh")); return false; }
		}
		if (PopulationCount < DesiredBeforePair) return false;
		bPopulationPrepared = true;
		Report->SetNumberField(TEXT("population_before_pair"), PopulationCount);
		Report->SetNumberField(TEXT("population_spawn_attempts"), PopulationSpawnAttempts);
		Check(TEXT("server_requested_population_prepared"), PopulationCount == DesiredBeforePair);
		WriteReport(false, TEXT("requested population prepared; starting skill stages"));
		return true;
	}

	void Advance(double Now) { ++Stage; NextActionAt = Now + StageSeconds; WriteReport(false, TEXT("running native server stages")); }

	void TickServer(double Now, UGuLiArmySkillSubsystem& Skills)
	{
		auto* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		auto* Red = FindCommander(EGuLiTeam::Red);
		auto* Blue = FindCommander(EGuLiTeam::Blue);
		const UNetDriver* Driver = World->GetNetDriver();
		if (!Authority || !Red || !Blue || !Driver || Driver->ClientConnections.Num() < 2
			|| !Skills.FindResolvedSkill(EGuLiTeam::Red, 1) || Now < NextActionAt) return;
		if (!PreparePopulation(*Authority)) return;
		Check(TEXT("server_two_real_ready_commanders"), true);
		if (Stage == 0)
		{
			FGuLiArmySkillCommand Clear; Clear.Command = EGuLiArmySkillCommand::ClearAll;
			Execute(*Red, Clear, TEXT("server_clear_red")); Execute(*Blue, Clear, TEXT("server_clear_blue"));
		}
		else if (Stage == 1)
		{
			CheckProfile(Skills, TEXT("server_baseline_A"), EGuLiTeam::Red, 1, 10.0f);
			CheckProfile(Skills, TEXT("server_baseline_B"), EGuLiTeam::Red, 2, 7.5f);
			Execute(*Red, SourceCommand(SourceA, 0.2f), TEXT("server_add_first_source_through_GA"));
			Execute(*Red, SourceCommand(SourceB, 0.2f), TEXT("server_add_second_source_through_GA"));
		}
		else if (Stage == 2)
		{
			CheckProfile(Skills, TEXT("server_two_twenty_percent_sources_14_4"), EGuLiTeam::Red, 1, 14.4f);
			CheckProfile(Skills, TEXT("server_other_team_unaffected"), EGuLiTeam::Blue, 1, 10.0f);
			CheckProfile(Skills, TEXT("server_other_type_unaffected"), EGuLiTeam::Red, 2, 7.5f);
			IdempotentRevision = Skills.FindResolvedSkill(EGuLiTeam::Red, 1)->Revision;
			Execute(*Red, SourceCommand(SourceA, 0.2f), TEXT("server_same_id_upsert_accepted"));
		}
		else if (Stage == 3)
		{
			const auto* Profile = Skills.FindResolvedSkill(EGuLiTeam::Red, 1);
			Check(TEXT("server_same_id_upsert_does_not_stack_or_revise"), Profile
				&& Profile->Revision == IdempotentRevision && FMath::IsNearlyEqual(Profile->Damage, 14.4f, 0.001f));
			Execute(*Red, SourceCommand(SourceA, 0.3f), TEXT("server_same_id_value_update"));
		}
		else if (Stage == 4)
		{
			CheckProfile(Skills, TEXT("server_upsert_updates_existing_source_15_6"), EGuLiTeam::Red, 1, 15.6f);
			Execute(*Red, RemoveCommand(SourceB), TEXT("server_remove_one_source"));
		}
		else if (Stage == 5)
		{
			CheckProfile(Skills, TEXT("server_remaining_source_13"), EGuLiTeam::Red, 1, 13.0f);
			Execute(*Red, ReplacementCommand(ReplacementSource, StrafeTest), TEXT("server_replace_slot"));
		}
		else if (Stage == 6)
		{
			CheckProfile(Skills, TEXT("server_replacement_uses_own_base_and_retains_modifier"), EGuLiTeam::Red, 1, 32.5f, StrafeTest);
			Execute(*Red, ReplacementCommand(ConflictSource, Strafe), TEXT("server_equal_priority_conflict_rejected"), false);
			CheckProfile(Skills, TEXT("server_conflict_rejection_keeps_committed_profile"), EGuLiTeam::Red, 1, 32.5f, StrafeTest);
			Execute(*Red, OverrideCommand(1, 123.25f), TEXT("server_set_final_override_A"));
			Execute(*Red, OverrideCommand(2, 22.5f), TEXT("server_set_spawn_profile_B"));
		}
		else if (Stage == 7)
		{
			CheckProfile(Skills, TEXT("server_final_override_after_sources"), EGuLiTeam::Red, 1, 123.25f, StrafeTest);
			CheckProfile(Skills, TEXT("server_B_override_ready"), EGuLiTeam::Red, 2, 22.5f);
			Check(TEXT("server_spawn_isolated_enemy_pair"), SpawnIsolatedPair(*Authority));
			TArray<FGuLiSoldierStateItem> CurrentStates;
			Authority->BuildSoldierStateSnapshot(CurrentStates);
			Report->SetNumberField(TEXT("measured_population"), CurrentStates.Num());
		}
		else if (Stage == 8)
		{
			Execute(*Red, OverrideCommand(1, 0.0f, true), TEXT("server_reset_only_numeric_override"));
		}
		else if (Stage == 9)
		{
			CheckProfile(Skills, TEXT("server_reset_preserves_modifier_and_replacement"), EGuLiTeam::Red, 1, 32.5f, StrafeTest);
			FGuLiSoldierCombatDebug A, B;
			const bool bReadA = Authority->TryGetSoldierCombatDebug(PairRed, A);
			const bool bReadB = Authority->TryGetSoldierCombatDebug(PairBlue, B);
			Check(TEXT("server_native_combat_fired_and_applied_damage"), bReadA && bReadB && A.ShotsFired > 0
				&& B.ShotsFired > 0 && A.Health < A.MaxHealth && B.Health < B.MaxHealth);
			Check(TEXT("server_native_combat_caused_death"), bReadB && B.Health <= 0.0f);
			Execute(*Red, OverrideCommand(2, 0.0f, true), TEXT("server_reset_B_override"));
			Execute(*Red, RemoveCommand(ReplacementSource), TEXT("server_remove_replacement"));
		}
		else if (Stage == 10)
		{
			CheckProfile(Skills, TEXT("server_remove_replacement_restores_modified_default"), EGuLiTeam::Red, 1, 13.0f);
			Execute(*Red, RemoveCommand(SourceA), TEXT("server_remove_last_modifier"));
			Check(TEXT("server_unified_float_damage_kills_large_health_survivor"), Authority->ApplyDamage(PairRed, 1000000000.0f));
			FGuLiSoldierCombatDebug B;
			if (Authority->TryGetSoldierCombatDebug(PairBlue, B) && B.Health > 0.0f) Authority->ApplyDamage(PairBlue, 1000000000.0f);
		}
		else if (Stage == 11)
		{
			CheckProfile(Skills, TEXT("server_final_A_baseline"), EGuLiTeam::Red, 1, 10.0f);
			CheckProfile(Skills, TEXT("server_final_B_baseline"), EGuLiTeam::Red, 2, 7.5f);
			CheckProfile(Skills, TEXT("server_final_blue_unchanged"), EGuLiTeam::Blue, 1, 10.0f);
			FGuLiSoldierCombatDebug A, B;
			Check(TEXT("server_both_debug_soldiers_dead"), Authority->TryGetSoldierCombatDebug(PairRed, A)
				&& Authority->TryGetSoldierCombatDebug(PairBlue, B) && A.Health == 0.0f && B.Health == 0.0f);
			Advance(Now); NextActionAt = Now + 8.0; return;
		}
		else { Finish(TEXT("native server stages completed; processes left running for caller cleanup")); return; }
		Advance(Now);
	}

	void ObserveClient(UGuLiArmySkillSubsystem& Skills)
	{
		AGuLiBattlePlayerState* LocalPS = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController()) LocalPS = PC->GetPlayerState<AGuLiBattlePlayerState>();
		}
		if (!LocalPS || !LocalPS->IsBattleReady() || !LocalPS->IsSoldierStreamReady()) return;
		if (!bTriedClientMutation)
		{
			FGuLiArmySkillCommand Command; Command.Command = EGuLiArmySkillCommand::ClearAll;
			Execute(*LocalPS, Command, TEXT("client_local_skill_command_rejected_without_RPC"), false);
			Check(TEXT("client_has_no_authority_simulation"), World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() == nullptr);
			bTriedClientMutation = true;
		}
		const auto* A = Skills.FindResolvedSkill(EGuLiTeam::Red, 1);
		const auto* B = Skills.FindResolvedSkill(EGuLiTeam::Red, 2);
		const auto* Blue = Skills.FindResolvedSkill(EGuLiTeam::Blue, 1);
		if (!A || !B || !Blue) return;
		Check(TEXT("client_other_team_remains_unmodified"), FMath::IsNearlyEqual(Blue->Damage, 10.0f) && Blue->SkillId == Strafe);
		if (FMath::IsNearlyEqual(A->Damage, 14.4f, 0.001f)) Check(TEXT("client_observed_two_sources_14_4"), true);
		if (FMath::IsNearlyEqual(A->Damage, 15.6f, 0.001f)) Check(TEXT("client_observed_upsert_15_6"), true);
		if (A->SkillId == StrafeTest && FMath::IsNearlyEqual(A->Damage, 32.5f))
		{
			Check(TEXT("client_observed_replacement_profile"), true);
			if (HasPassed(TEXT("client_observed_final_override"))) Check(TEXT("client_reset_preserved_source_and_replacement"), true);
		}
		if (FMath::IsNearlyEqual(A->Damage, 123.25f)) Check(TEXT("client_observed_final_override"), true);
		if (FMath::IsNearlyEqual(B->Damage, 22.5f)) Check(TEXT("client_observed_B_spawn_profile"), true);
		if (HasPassed(TEXT("client_reset_preserved_source_and_replacement")) && A->SkillId == Strafe
			&& FMath::IsNearlyEqual(A->Damage, 10.0f) && FMath::IsNearlyEqual(B->Damage, 7.5f)) Check(TEXT("client_final_profiles_restored"), true);

		const TCHAR* Required[] = {TEXT("client_local_skill_command_rejected_without_RPC"), TEXT("client_has_no_authority_simulation"),
			TEXT("client_other_team_remains_unmodified"), TEXT("client_observed_two_sources_14_4"), TEXT("client_observed_upsert_15_6"),
			TEXT("client_observed_replacement_profile"), TEXT("client_observed_final_override"), TEXT("client_reset_preserved_source_and_replacement"),
			TEXT("client_observed_B_spawn_profile"), TEXT("client_float_maximum_above_255"), TEXT("client_observed_damage_in_reliable_state"),
			TEXT("client_red_B_death"), TEXT("client_blue_B_death"), TEXT("client_final_profiles_restored")};
		for (const TCHAR* Key : Required) if (!HasPassed(Key)) return;
		Finish(TEXT("client observed authoritative profiles, float health, deaths, and rejected its local mutation"));
	}

	void CaptureSamples(double Now, UGuLiArmySkillSubsystem& Skills)
	{
		// Stress measures the existing 500-soldier battle. Sample the whole roster
		// once per second, so the QA observer does not add per-frame combat work.
		if (bStress && Now < NextNetSampleAt) return;
		for (const auto& Profile : Skills.GetResolvedSkills())
		{
			const uint32 Key = (static_cast<uint32>(Profile.Team) << 16) | Profile.UnitTypeId;
			if (LastProfileRevisions.FindRef(Key) == Profile.Revision) continue;
			LastProfileRevisions.Add(Key, Profile.Revision);
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("kind"), TEXT("profile")); Row->SetNumberField(TEXT("time_s"), Now - StartedAt);
			Row->SetNumberField(TEXT("team"), static_cast<int32>(Profile.Team)); Row->SetNumberField(TEXT("unit_type"), Profile.UnitTypeId);
			Row->SetStringField(TEXT("skill"), Profile.SkillId.ToString()); Row->SetNumberField(TEXT("damage"), Profile.Damage);
			Row->SetNumberField(TEXT("rate"), Profile.AttackRatePerSecond); Row->SetNumberField(TEXT("range_cm"), Profile.RangeCentimeters);
			Row->SetNumberField(TEXT("revision"), Profile.Revision); Samples.Add(MakeShared<FJsonValueObject>(Row));
		}
		if (bStress) CaptureStressRoster(Now);
		else for (TActorIterator<AGuLiSoldierStateReplicator> It(World.Get()); It; ++It)
		{
			for (const auto& State : It->GetItems())
			{
				if (State.UnitTypeId != 2) continue;
				const float* Previous = LastHealth.Find(State.SoldierId.Value);
				if (Previous && *Previous == State.Health) continue;
				if (!bServer)
				{
					if (FMath::IsNearlyEqual(State.MaxHealth, 300.5f)) Check(TEXT("client_float_maximum_above_255"), true);
					if (Previous && State.Health < *Previous) Check(TEXT("client_observed_damage_in_reliable_state"), true);
					if (!State.IsAlive()) Check(State.Team == EGuLiTeam::Red ? TEXT("client_red_B_death") : TEXT("client_blue_B_death"), true);
				}
				LastHealth.Add(State.SoldierId.Value, State.Health);
				auto Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("kind"), TEXT("soldier")); Row->SetNumberField(TEXT("time_s"), Now - StartedAt);
				Row->SetNumberField(TEXT("id"), State.SoldierId.Value); Row->SetNumberField(TEXT("team"), static_cast<int32>(State.Team));
				Row->SetNumberField(TEXT("unit_type"), State.UnitTypeId); Row->SetNumberField(TEXT("health"), State.Health);
				Row->SetNumberField(TEXT("max_health"), State.MaxHealth); Row->SetBoolField(TEXT("alive"), State.IsAlive());
				if (bServer)
				{
					FGuLiSoldierCombatDebug Debug;
					const auto* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
					if (Authority && Authority->TryGetSoldierCombatDebug(State.SoldierId, Debug))
					{ Row->SetNumberField(TEXT("shots_fired"), static_cast<double>(Debug.ShotsFired)); Row->SetNumberField(TEXT("target_id"), Debug.TargetId.Value); }
				}
				Samples.Add(MakeShared<FJsonValueObject>(Row));
			}
		}
		if (Now < NextNetSampleAt) return;
		NextNetSampleAt = Now + 1.0;
		auto Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("time_s"), Now - StartedAt); Row->SetNumberField(TEXT("net_mode"), static_cast<int32>(World->GetNetMode()));
		if (bServer)
		{
			if (const auto* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
			{
				const auto& Counters = Authority->GetPerformanceCounters();
				Row->SetNumberField(TEXT("member_count"), Authority->GetAuthoritativeMemberCount());
				Row->SetNumberField(TEXT("sim_tick"), Authority->GetServerSimTick());
				Row->SetNumberField(TEXT("authority_steps_total"), static_cast<double>(Counters.Steps));
				Row->SetNumberField(TEXT("simulation_ms_total"), Counters.SimulationMilliseconds);
				Row->SetNumberField(TEXT("combat_ms_total"), Counters.CombatMilliseconds);
				Row->SetNumberField(TEXT("max_simulation_ms"), Counters.MaxSimulationMilliseconds);
				Row->SetNumberField(TEXT("max_combat_ms"), Counters.MaxCombatMilliseconds);
				Row->SetNumberField(TEXT("shots_total"), static_cast<double>(Counters.Shots));
			}
		}
		if (const UNetDriver* Driver = World->GetNetDriver())
		{
			MaximumObservedClientConnections = FMath::Max(MaximumObservedClientConnections, Driver->ClientConnections.Num());
			double InRate = 0.0, OutRate = 0.0;
			const auto AddConnection = [&](const UNetConnection* Connection)
			{ if (Connection) { InRate += Connection->InBytesPerSecond; OutRate += Connection->OutBytesPerSecond; } };
			AddConnection(Driver->ServerConnection);
			for (const UNetConnection* Connection : Driver->ClientConnections) AddConnection(Connection);
			Row->SetNumberField(TEXT("client_connections"), Driver->ClientConnections.Num());
			Row->SetBoolField(TEXT("has_server_connection"), Driver->ServerConnection != nullptr);
			Row->SetNumberField(TEXT("in_bytes_per_second"), InRate); Row->SetNumberField(TEXT("out_bytes_per_second"), OutRate);
		}
		NetStats.Add(MakeShared<FJsonValueObject>(Row));
	}

	void CaptureStressRoster(double Now)
	{
		int32 Population = 0, Alive = 0, Injured = 0, Dead = 0, Invalid = 0, ChangedHealth = 0;
		int32 RedDead = 0, BlueDead = 0;
		double TotalHealth = 0.0, TotalMaxHealth = 0.0;
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World.Get()); It; ++It)
		{
			StressSnapshotEpoch = It->GetSnapshotMatchEpoch();
			for (const auto& State : It->GetItems())
			{
				++Population;
				if (!FMath::IsFinite(State.Health) || !FMath::IsFinite(State.MaxHealth)
					|| State.MaxHealth <= 0.0f || State.Health < 0.0f || State.Health > State.MaxHealth)
				{ ++Invalid; continue; }
				TotalHealth += State.Health; TotalMaxHealth += State.MaxHealth;
				if (const float* Previous = LastHealth.Find(State.SoldierId.Value); Previous && State.Health < *Previous) ++ChangedHealth;
				LastHealth.Add(State.SoldierId.Value, State.Health);
				if (State.IsAlive()) { ++Alive; Injured += State.Health < State.MaxHealth ? 1 : 0; }
				else { ++Dead; RedDead += State.Team == EGuLiTeam::Red ? 1 : 0; BlueDead += State.Team == EGuLiTeam::Blue ? 1 : 0; }
			}
		}
		StressPeakPopulation = FMath::Max(StressPeakPopulation, Population);
		StressPeakDeaths = FMath::Max(StressPeakDeaths, Dead);
		StressHealthChangeCount += ChangedHealth;
		bStressObservedHealthLoss |= Injured > 0 || Dead > 0;
		bStressAllHealthFinite &= Invalid == 0;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("kind"), TEXT("stress_roster")); Row->SetNumberField(TEXT("time_s"), Now - StartedAt);
		Row->SetNumberField(TEXT("snapshot_epoch"), StressSnapshotEpoch); Row->SetNumberField(TEXT("population"), Population);
		Row->SetNumberField(TEXT("alive"), Alive); Row->SetNumberField(TEXT("injured"), Injured); Row->SetNumberField(TEXT("dead"), Dead);
		Row->SetNumberField(TEXT("red_dead"), RedDead); Row->SetNumberField(TEXT("blue_dead"), BlueDead);
		Row->SetNumberField(TEXT("health_total"), TotalHealth); Row->SetNumberField(TEXT("max_health_total"), TotalMaxHealth);
		Row->SetNumberField(TEXT("health_decrease_observations"), ChangedHealth); Row->SetNumberField(TEXT("invalid_health_count"), Invalid);
		Samples.Add(MakeShared<FJsonValueObject>(Row));
	}

	void TickStress(double Now)
	{
		if (Now - StressStartedAt < StressDurationSeconds) return;
		Report->SetNumberField(TEXT("stress_elapsed_s"), Now - StressStartedAt);
		Report->SetNumberField(TEXT("observed_population_peak"), StressPeakPopulation);
		Report->SetNumberField(TEXT("observed_deaths_peak"), StressPeakDeaths);
		Report->SetNumberField(TEXT("health_decrease_observations"), StressHealthChangeCount);
		if (bServer)
		{
			const auto* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
			Check(TEXT("server_stress_population_is_500"), Authority
				&& Authority->GetAuthoritativeMemberCount() == 500 && StressPeakPopulation == 500);
			Check(TEXT("server_stress_production_shots_recorded"), Authority && Authority->GetPerformanceCounters().Shots > 0);
			Check(TEXT("server_stress_actual_health_loss"), bStressObservedHealthLoss);
			Check(TEXT("server_stress_actual_deaths"), StressPeakDeaths > 0);
			Check(TEXT("server_stress_two_clients_connected"), MaximumObservedClientConnections >= 2);
			if (Authority) Report->SetNumberField(TEXT("production_shots_total"), static_cast<double>(Authority->GetPerformanceCounters().Shots));
		}
		else
		{
			const UNetDriver* Driver = World->GetNetDriver();
			Check(TEXT("client_stress_has_server_connection"), Driver && Driver->ServerConnection);
			Check(TEXT("client_stress_received_500_soldier_roster"), StressPeakPopulation == 500 && StressSnapshotEpoch != 0);
			Check(TEXT("client_stress_received_finite_float_health"), StressPeakPopulation > 0 && bStressAllHealthFinite);
			Check(TEXT("client_stress_received_deaths"), StressPeakDeaths > 0);
			// Individual shots are not replicated; a client must not invent a shot count
			// from snapshot deltas or fail merely because it joined after the damage.
			Report->SetStringField(TEXT("shot_count_note"), TEXT("Not available on clients; only authoritative health and deaths replicate."));
		}
		Finish(TEXT("20-second 500-soldier stress observation completed; no GA commands or debug spawns were issued"));
	}

	bool Tick(float DeltaSeconds)
	{
		(void)DeltaSeconds;
		const double Now = FPlatformTime::Seconds();
		if (Now - StartedAt >= TimeoutSeconds)
		{ Check(TEXT("completed_before_90_second_timeout"), false); Finish(TEXT("timeout while waiting for readiness or required observations")); return false; }
		if (!ResolveWorld()) return true;
		if (bStress && StressStartedAt == 0.0)
		{
			StressStartedAt = Now;
			Report->SetNumberField(TEXT("stress_start_elapsed_s"), Now - StartedAt);
		}
		auto* Skills = World->GetSubsystem<UGuLiArmySkillSubsystem>();
		if (!Skills) return true;
		CaptureSamples(Now, *Skills);
		if (bStress) TickStress(Now);
		else if (bServer) TickServer(Now, *Skills); else ObserveClient(*Skills);
		return !bFinished;
	}

	void WriteReport(bool bCompleted, const FString& Reason)
	{
		bool bAllPassed = bCompleted && !Checks->Values.IsEmpty();
		for (const auto& Entry : Checks->Values) bAllPassed &= Entry.Value->AsBool();
		Report->SetBoolField(TEXT("completed"), bCompleted); Report->SetBoolField(TEXT("passed"), bAllPassed);
		Report->SetStringField(TEXT("reason"), Reason); Report->SetNumberField(TEXT("stage"), Stage);
		Report->SetNumberField(TEXT("elapsed_s"), FPlatformTime::Seconds() - StartedAt);
		Report->SetStringField(TEXT("world"), World.IsValid() ? World->GetPathName() : TEXT("unresolved"));
		Report->SetObjectField(TEXT("checks"), Checks); Report->SetArrayField(TEXT("commands"), Commands);
		Report->SetArrayField(TEXT("samples"), Samples); Report->SetArrayField(TEXT("netstats"), NetStats);
		Report->SetArrayField(TEXT("errors"), Errors);
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Report.ToSharedRef(), Writer);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
		if (!FFileHelper::SaveStringToFile(Json, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			UE_LOG(LogGuLiStrike, Error, TEXT("Skill native QA could not write %s"), *OutputPath);
	}

	void Finish(const FString& Reason)
	{
		bFinished = true;
		WriteReport(true, Reason);
		UE_LOG(LogGuLiStrike, Display, TEXT("Skill native QA completed passed=%s mode=%s report=%s reason=%s"),
			Report->GetBoolField(TEXT("passed")) ? TEXT("true") : TEXT("false"), bServer ? TEXT("Server") : TEXT("Client"), *OutputPath, *Reason);
	}

	const bool bServer;
	TWeakObjectPtr<UWorld> World;
	const double StartedAt;
	double NextActionAt = 0.0, NextNetSampleAt = 0.0;
	int32 Stage = 0;
	bool bFinished = false, bTriedClientMutation = false;
	bool bStress = false, bStressAllHealthFinite = true, bStressObservedHealthLoss = false;
	double StressStartedAt = 0.0;
	int32 StressPeakPopulation = 0, StressPeakDeaths = 0, StressHealthChangeCount = 0, MaximumObservedClientConnections = 0;
	uint32 StressSnapshotEpoch = 0;
	bool bPopulationPrepared = false;
	int32 PopulationTarget = 500, PopulationCount = 0, PopulationSpawnAttempts = 0;
	TArray<FGuLiSoldierCombatDebug> PopulationSeeds;
	uint32 IdempotentRevision = 0;
	FGuid SourceA = FGuid(0x71510001, 1, 1, 1), SourceB = FGuid(0x71510002, 2, 2, 2);
	FGuid ReplacementSource = FGuid(0x71510003, 3, 3, 3), ConflictSource = FGuid(0x71510004, 4, 4, 4);
	FGuLiSoldierId PairRed, PairBlue;
	TMap<uint32, uint32> LastProfileRevisions;
	TMap<uint32, float> LastHealth;
	TSharedPtr<FJsonObject> Report, Checks;
	TArray<TSharedPtr<FJsonValue>> Samples, NetStats, Commands, Errors;
	FString OutputPath;
};

TSharedPtr<FRunner> ActiveRunner;

void Start(bool bServer, UWorld* World)
{
	if (ActiveRunner.IsValid() && !ActiveRunner->IsFinished())
	{ UE_LOG(LogGuLiStrike, Warning, TEXT("Skill native QA is already running.")); return; }
	ActiveRunner = MakeShared<FRunner>(bServer, World);
	ActiveRunner->Start();
}

void StartCommand(const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() != 1 || !Args[0].Equals(TEXT("Start"), ESearchCase::IgnoreCase) || !World || !World->IsGameWorld())
	{ UE_LOG(LogGuLiStrike, Warning, TEXT("Usage in a disposable game World: gs.GM.Skill.QA Start")); return; }
	Start(World->GetNetMode() != NM_Client, World);
}

FAutoConsoleCommandWithWorldAndArgs QACommand(TEXT("gs.GM.Skill.QA"),
	TEXT("Native destructive-to-test-match skill bridge QA: gs.GM.Skill.QA Start; server requires two connected ready Commanders."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartCommand));

// The first engine tick reads opt-in flags after command-line and engine initialization.
const FTSTicker::FDelegateHandle AutoArmHandle = FTSTicker::GetCoreTicker().AddTicker(
	FTickerDelegate::CreateLambda([](float)
	{
		if (!GEngine) return true;
		FString Mode;
		if (FParse::Value(FCommandLine::Get(), TEXT("GuLiSkillQA="), Mode))
		{
			if (Mode.Equals(TEXT("Server"), ESearchCase::IgnoreCase)) Start(true, nullptr);
			else if (Mode.Equals(TEXT("Client"), ESearchCase::IgnoreCase)) Start(false, nullptr);
			else UE_LOG(LogGuLiStrike, Warning, TEXT("GuLiSkillQA must be Server or Client."));
		}
		return false;
	}), 0.1f);
}

#endif // !UE_BUILD_SHIPPING
