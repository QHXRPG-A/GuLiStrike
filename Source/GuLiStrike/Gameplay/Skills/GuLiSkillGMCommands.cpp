// Copyright Epic Games, Inc. All Rights Reserved.
// Server-console diagnostics. Mutations traverse the commander's real GAS ability.
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"

#if !UE_BUILD_SHIPPING
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/GuLiSoldierCombat.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Misc/SecureHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiSkillGM, Log, All);

namespace GuLiSkillGM
{
bool ParseTeam(const FString& Text, EGuLiTeam& Out)
{
	if (Text.Equals(TEXT("Red"), ESearchCase::IgnoreCase)) { Out = EGuLiTeam::Red; return true; }
	if (Text.Equals(TEXT("Blue"), ESearchCase::IgnoreCase)) { Out = EGuLiTeam::Blue; return true; }
	return false;
}

bool ParseUnit(const FString& Text, uint16& Out)
{
	int32 Value = 0;
	if (!LexTryParseString(Value, *Text) || Value <= 0 || Value > MAX_uint16) { return false; }
	Out = static_cast<uint16>(Value);
	return true;
}

bool ParseValue(const FString& Text, float& Out)
{
	return LexTryParseString(Out, *Text) && FMath::IsFinite(Out);
}

bool ParseAttribute(const FString& Text, EGuLiSkillAttribute& Out)
{
	if (Text.Equals(TEXT("damage"), ESearchCase::IgnoreCase)) { Out = EGuLiSkillAttribute::Damage; return true; }
	if (Text.Equals(TEXT("rate"), ESearchCase::IgnoreCase)) { Out = EGuLiSkillAttribute::AttackRate; return true; }
	if (Text.Equals(TEXT("range"), ESearchCase::IgnoreCase)) { Out = EGuLiSkillAttribute::Range; return true; }
	return false;
}

FGuid SourceId(const EGuLiTeam Team, const FString& Label)
{
	FGuid Result;
	FGuid::Parse(FMD5::HashAnsiString(*FString::Printf(TEXT("GuLiSkillGM/%d/%s"), int32(Team), *Label)), Result);
	return Result;
}

AGuLiBattlePlayerState* Commander(UWorld* World, const EGuLiTeam Team)
{
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Rejected: a server game World is required; client console commands do not send RPCs."));
		return nullptr;
	}
	if (const AGameStateBase* State = World->GetGameState())
	{
		for (APlayerState* Player : State->PlayerArray)
		{
			AGuLiBattlePlayerState* Candidate = Cast<AGuLiBattlePlayerState>(Player);
			if (Candidate && Candidate->IsCommander() && Candidate->GetTeam() == Team) { return Candidate; }
		}
	}
	UE_LOG(LogGuLiSkillGM, Warning, TEXT("Rejected: no active commander for team %d. Join the match before using GAS mutations."), int32(Team));
	return nullptr;
}

void Execute(UWorld* World, const EGuLiTeam Team, const FGuLiArmySkillCommand& Command)
{
	if (AGuLiBattlePlayerState* Player = Commander(World, Team))
	{
		FString Error;
		const bool bSuccess = Player->ExecuteArmySkillCommand(Command, Error);
		UE_LOG(LogGuLiSkillGM, Display, TEXT("GAS command team=%d accepted=%d source=%s error=%s (accepted changes publish on next authority step)"),
			int32(Team), bSuccess, *Command.Source.SourceInstanceId.ToString(), *Error);
	}
}

void List(const TArray<FString>& Args, UWorld* World)
{
	if (UGuLiArmySkillSubsystem* Skills = World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr)
	{
		for (const FGuLiResolvedSkillProfile& Profile : Skills->GetResolvedSkills())
		{
			UE_LOG(LogGuLiSkillGM, Display, TEXT("%s"), *Skills->ExplainResolvedSkill(Profile.Team, Profile.UnitTypeId, Profile.SlotId));
		}
	}
}

void Get(const TArray<FString>& Args, UWorld* World)
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 Unit = 0;
	if (Args.Num() < 2 || Args.Num() > 3 || !ParseTeam(Args[0], Team) || !ParseUnit(Args[1], Unit))
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Get <Red|Blue> <UnitTypeId> [SlotId=BasicAttack]")); return;
	}
	if (UGuLiArmySkillSubsystem* Skills = World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr)
	{
		UE_LOG(LogGuLiSkillGM, Display, TEXT("%s"), *Skills->ExplainResolvedSkill(Team, Unit, Args.Num() == 3 ? FName(*Args[2]) : FName(TEXT("BasicAttack"))));
	}
}

void Set(const TArray<FString>& Args, UWorld* World)
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	EGuLiSkillAttribute Attribute = EGuLiSkillAttribute::Damage;
	uint16 Unit = 0;
	float Value = 0.0f;
	if (Args.Num() < 4 || Args.Num() > 5 || !ParseTeam(Args[0], Team) || !ParseUnit(Args[1], Unit)
		|| !ParseAttribute(Args[2], Attribute) || !ParseValue(Args[3], Value) || Value < 0.0f)
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Set <Red|Blue> <UnitTypeId> <damage|rate|range> <value> [SlotId]; range is cm.")); return;
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::SetNumericOverride;
	Command.NumericOverride.UnitTypeId = Unit;
	if (Args.Num() == 5) { Command.NumericOverride.SlotId = FName(*Args[4]); }
	if (Attribute == EGuLiSkillAttribute::Damage) { Command.NumericOverride.bOverrideDamage = true; Command.NumericOverride.Damage = Value; }
	if (Attribute == EGuLiSkillAttribute::AttackRate) { Command.NumericOverride.bOverrideAttackRate = true; Command.NumericOverride.AttackRatePerSecond = Value; }
	if (Attribute == EGuLiSkillAttribute::Range) { Command.NumericOverride.bOverrideRange = true; Command.NumericOverride.RangeCentimeters = Value; }
	Execute(World, Team, Command);
}

void Reset(const TArray<FString>& Args, UWorld* World)
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 Unit = 0;
	if (Args.Num() < 2 || Args.Num() > 3 || !ParseTeam(Args[0], Team) || !ParseUnit(Args[1], Unit))
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Reset <Red|Blue> <UnitTypeId> [SlotId]; clears numeric overrides only.")); return;
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::ClearNumericOverride;
	Command.NumericOverride.UnitTypeId = Unit;
	if (Args.Num() == 3) { Command.NumericOverride.SlotId = FName(*Args[2]); }
	Execute(World, Team, Command);
}

void Source(const TArray<FString>& Args, UWorld* World)
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	EGuLiSkillAttribute Attribute = EGuLiSkillAttribute::Damage;
	uint16 Unit = 0;
	float Value = 0.0f;
	if (Args.Num() < 6 || Args.Num() > 9 || !ParseTeam(Args[0], Team) || !ParseUnit(Args[1], Unit)
		|| !ParseAttribute(Args[3], Attribute) || !ParseValue(Args[5], Value)
		|| !(Args[4].Equals(TEXT("flat"), ESearchCase::IgnoreCase) || Args[4].Equals(TEXT("percent"), ESearchCase::IgnoreCase)))
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Source <Red|Blue> <UnitTypeId> <label> <damage|rate|range> <flat|percent> <value> [SkillId|*] [Tag|*] [SlotId]. percent 0.2 = +20%%; same label replaces the whole source.")); return;
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::UpsertSource;
	Command.Source.SourceInstanceId = SourceId(Team, Args[2]);
	Command.Source.DebugLabel = Args[2];
	FGuLiSkillModifier& Modifier = Command.Source.Modifiers.AddDefaulted_GetRef();
	Modifier.Target.UnitTypeIds.Add(Unit);
	Modifier.Attribute = Attribute;
	Modifier.Operation = Args[4].Equals(TEXT("flat"), ESearchCase::IgnoreCase) ? EGuLiSkillModifierOperation::AddFlat : EGuLiSkillModifierOperation::AddPercent;
	Modifier.Magnitude = Value;
	if (Args.Num() > 6 && Args[6] != TEXT("*")) { Modifier.Target.RequiredSkillId = FName(*Args[6]); }
	if (Args.Num() > 7 && Args[7] != TEXT("*"))
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[7]), false);
		if (!Tag.IsValid()) { UE_LOG(LogGuLiSkillGM, Warning, TEXT("Rejected unknown gameplay tag: %s"), *Args[7]); return; }
		Modifier.Target.RequiredTags.AddTag(Tag);
	}
	if (Args.Num() > 8) { Modifier.Target.SlotId = FName(*Args[8]); }
	Execute(World, Team, Command);
}

void Replace(const TArray<FString>& Args, UWorld* World)
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 Unit = 0;
	int32 Priority = 0;
	if (Args.Num() < 5 || Args.Num() > 6 || !ParseTeam(Args[0], Team) || !ParseUnit(Args[1], Unit) || !LexTryParseString(Priority, *Args[4]))
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Replace <Red|Blue> <UnitTypeId> <label> <SkillId> <priority> [SlotId]")); return;
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::UpsertSource;
	Command.Source.SourceInstanceId = SourceId(Team, Args[2]);
	Command.Source.DebugLabel = Args[2];
	FGuLiSkillSlotReplacement& Replacement = Command.Source.Replacements.AddDefaulted_GetRef();
	Replacement.Target.UnitTypeIds.Add(Unit);
	Replacement.SkillId = FName(*Args[3]);
	Replacement.Priority = Priority;
	if (Args.Num() == 6) { Replacement.Target.SlotId = FName(*Args[5]); }
	Execute(World, Team, Command);
}

void Remove(const TArray<FString>& Args, UWorld* World)
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	if (Args.Num() != 2 || !ParseTeam(Args[0], Team))
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Remove <Red|Blue> <label>")); return;
	}
	FGuLiArmySkillCommand Command;
	Command.Command = EGuLiArmySkillCommand::RemoveSource;
	Command.SourceInstanceId = SourceId(Team, Args[1]);
	Execute(World, Team, Command);
}

void Soldier(const TArray<FString>& Args, UWorld* World)
{
	uint32 Id;
	if (Args.Num() != 1 || !LexTryParseString(Id, *Args[0]) || Id == 0)
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Soldier <SoldierId> (server authority only)")); return;
	}
	const UGuLiBattleAuthoritySubsystem* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	FGuLiSoldierCombatDebug Debug;
	FGuLiSoldierNavigationDebug Navigation;
	if (!Authority || !Authority->TryGetSoldierCombatDebug(FGuLiSoldierId(Id), Debug))
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Soldier %u unavailable in this authority World."), Id); return;
	}
	const bool bHasNavigation = Authority->TryGetSoldierNavigationDebug(FGuLiSoldierId(Id), Navigation);
	UE_LOG(LogGuLiSkillGM, Display, TEXT("Soldier=%u team=%d type=%u skill=%s executor=%s target=%u hp=%.6g/%.6g damage=%.6g rate=%.6g range_cm=%.6g cooldown=%.6f combat_reason=%s moving=%d nav_state=%d shots=%llu revision=%u position=%s"),
		Id, int32(Debug.Team), Debug.UnitTypeId, *Debug.SkillId.ToString(), *Debug.ExecutorId.ToString(), Debug.TargetId.Value,
		Debug.Health, Debug.MaxHealth, Debug.Damage, Debug.AttackRate, Debug.RangeCentimeters, Debug.CooldownRemaining,
		GuLiSoldierCombat::LexToString(Debug.StopReason), bHasNavigation && Navigation.bMoving ? 1 : 0,
		bHasNavigation ? static_cast<int32>(Navigation.State) : -1,
		Debug.ShotsFired, Debug.ProfileRevision, *Debug.Location.ToString());
}

void Spawn(const TArray<FString>& Args, UWorld* World)
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 Unit = 0;
	float X, Y, Z;
	if (Args.Num() != 5 || !ParseTeam(Args[0], Team) || !ParseUnit(Args[1], Unit)
		|| !ParseValue(Args[2], X) || !ParseValue(Args[3], Y) || !ParseValue(Args[4], Z))
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Spawn <Red|Blue> <UnitTypeId> <X> <Y> <Z> (cm, server only)")); return;
	}
	UGuLiBattleAuthoritySubsystem* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	FGuLiSoldierId Id;
	const bool bSpawned = Authority && World->GetNetMode() != NM_Client && Authority->SpawnDebugSoldier(Team, Unit, FVector(X, Y, Z), Id);
	UE_LOG(LogGuLiSkillGM, Display, TEXT("Spawn accepted=%d soldier=%u"), bSpawned, Id.Value);
}

void Bench(const TArray<FString>& Args, UWorld* World)
{
	int32 Count = 500;
	int32 Steps = 300;
	if (Args.Num() > 2 || (Args.Num() > 0 && !LexTryParseString(Count, *Args[0]))
		|| (Args.Num() > 1 && !LexTryParseString(Steps, *Args[1])) || (Count != 500 && Count != 10000) || Steps < 1 || Steps > 1800)
	{
		UE_LOG(LogGuLiSkillGM, Warning, TEXT("Usage: gs.GM.Skill.Bench [500|10000 soldiers] [1..1800 steps]; measures combat helper only.")); return;
	}
	FGuLiCombatBenchmarkResult Result;
	if (GuLiSoldierCombat::RunBenchmark(Count, Steps, Result))
	{
		UE_LOG(LogGuLiSkillGM, Display, TEXT("CombatBench population=%d steps=%d mean_ms=%.6f p95_ms=%.6f max_ms=%.6f shots=%lld queries=%lld candidates=%lld excludes=rendering,navigation,network,world"),
			Result.PopulationCount, Result.Steps, Result.MeanMilliseconds, Result.P95Milliseconds, Result.MaximumMilliseconds,
			Result.Shots, Result.TargetQueries, Result.CandidateChecks);
	}
}

FAutoConsoleCommandWithWorldAndArgs ListCommand(TEXT("gs.GM.Skill.List"), TEXT("List resolved army skills."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&List));
FAutoConsoleCommandWithWorldAndArgs GetCommand(TEXT("gs.GM.Skill.Get"), TEXT("Explain <team> <unit> [slot]."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Get));
FAutoConsoleCommandWithWorldAndArgs SetCommand(TEXT("gs.GM.Skill.Set"), TEXT("Override <team> <unit> <damage|rate|range> <value> [slot]."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Set));
FAutoConsoleCommandWithWorldAndArgs ResetCommand(TEXT("gs.GM.Skill.Reset"), TEXT("Clear numeric override <team> <unit> [slot], preserving sources."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Reset));
FAutoConsoleCommandWithWorldAndArgs SourceCommand(TEXT("gs.GM.Skill.Source"), TEXT("Upsert neutral GAS modifier; invoke without arguments for usage."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Source));
FAutoConsoleCommandWithWorldAndArgs ReplaceCommand(TEXT("gs.GM.Skill.Replace"), TEXT("Upsert GAS slot replacement; invoke without arguments for usage."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Replace));
FAutoConsoleCommandWithWorldAndArgs RemoveCommand(TEXT("gs.GM.Skill.Remove"), TEXT("Remove one neutral GAS source <team> <label>."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Remove));
FAutoConsoleCommandWithWorldAndArgs SoldierCommand(TEXT("gs.GM.Skill.Soldier"), TEXT("Inspect one authority soldier by ID."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Soldier));
FAutoConsoleCommandWithWorldAndArgs SpawnCommand(TEXT("gs.GM.Skill.Spawn"), TEXT("Spawn debug soldier <team> <unit> <x> <y> <z>."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Spawn));
FAutoConsoleCommandWithWorldAndArgs BenchCommand(TEXT("gs.GM.Skill.Bench"), TEXT("Benchmark production combat loop without rendering/navigation/network."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Bench));
}
#endif
