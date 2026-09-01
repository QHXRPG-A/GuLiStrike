// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"

enum class EGuLiCombatStopReason : uint8
{
	Dead, Moving, Disabled, UnknownExecutor, NoTarget, Cooldown, Fired
};

struct GULISTRIKE_API FGuLiSoldierAttackState
{
	FGuLiSoldierId TargetId;
	double NextFireSeconds = 0.0;
	double PausedCooldownFraction = 1.0;
	bool bRatePaused = false;
	uint64 ShotsFired = 0;
	EGuLiCombatStopReason StopReason = EGuLiCombatStopReason::NoTarget;
};

/** A stable, non-owning view for one simulation step. Indices match the spatial grid. */
struct GULISTRIKE_API FGuLiCombatSample
{
	FGuLiSoldierId SoldierId;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	FVector Location = FVector::ZeroVector;
	bool bAlive = false;
	bool bHasMoveOrder = false;
	const FGuLiResolvedSkillProfile* Profile = nullptr;
	FGuLiSoldierAttackState* Attack = nullptr;
};

struct GULISTRIKE_API FGuLiCombatDamageEvent
{
	FGuLiSoldierId SourceId;
	FGuLiSoldierId TargetId;
	FName SkillId = NAME_None;
	float Damage = 0.0f;
};

/** Extend with another executor or hit behavior without adding skill switches to Mass. */
class GULISTRIKE_API FGuLiCombatExecutorRegistry
{
public:
	using FExecutor = TFunction<void(const FGuLiCombatSample&, const FGuLiCombatSample&,
		TArray<FGuLiCombatDamageEvent>&)>;
	FGuLiCombatExecutorRegistry();
	bool RegisterExecutor(FName Id, FExecutor Executor);
	bool Contains(FName Id) const;
	bool Execute(const FGuLiCombatSample& Source, const FGuLiCombatSample& Target,
		TArray<FGuLiCombatDamageEvent>& OutEvents) const;

private:
	TMap<FName, FExecutor> Executors;
};

struct GULISTRIKE_API FGuLiCombatStepMetrics
{
	int32 TargetQueries = 0;
	int32 CandidateChecks = 0;
	int32 Shots = 0;
};

struct GULISTRIKE_API FGuLiSoldierCombatDebug
{
	FGuLiSoldierId SoldierId;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 UnitTypeId = 0;
	FName SkillId = NAME_None;
	FName ExecutorId = NAME_None;
	FGuLiSoldierId TargetId;
	FVector Location = FVector::ZeroVector;
	float Health = 0.0f;
	float MaxHealth = 0.0f;
	float Damage = 0.0f;
	float AttackRate = 0.0f;
	float RangeCentimeters = 0.0f;
	double CooldownRemaining = 0.0;
	uint32 ProfileRevision = 0;
	uint64 ShotsFired = 0;
	EGuLiCombatStopReason StopReason = EGuLiCombatStopReason::NoTarget;
};

struct GULISTRIKE_API FGuLiCombatBenchmarkResult
{
	int32 PopulationCount = 0;
	int32 Steps = 0;
	double MeanMilliseconds = 0.0;
	double P95Milliseconds = 0.0;
	double MaximumMilliseconds = 0.0;
	int64 Shots = 0;
	int64 TargetQueries = 0;
	int64 CandidateChecks = 0;
};

namespace GuLiSoldierCombat
{
	inline constexpr float SpatialCellSizeCentimeters = 10000.0f;
	inline constexpr uint32 AcquisitionPeriodTicks = 6; // 200 ms at 30 Hz.
	GULISTRIKE_API const TCHAR* LexToString(EGuLiCombatStopReason Reason);
	GULISTRIKE_API FIntPoint MakeSpatialCell(const FVector& Location);
	GULISTRIKE_API bool IsProfileUsable(const FGuLiResolvedSkillProfile& Profile);
	/** Rate changes preserve remaining normalized cooldown; skill changes start a full interval. */
	GULISTRIKE_API void ReplaceProfile(const FGuLiResolvedSkillProfile& Previous,
		const FGuLiResolvedSkillProfile& Next, double SimulationSeconds, FGuLiSoldierAttackState& State);
	GULISTRIKE_API void BuildSpatialGrid(TConstArrayView<FGuLiCombatSample> Samples,
		TMap<FIntPoint, TArray<int32>>& OutGrid);
	/** Collect first, apply health changes afterwards: soldiers alive this step can trade kills. */
	GULISTRIKE_API FGuLiCombatStepMetrics CollectAttacks(TConstArrayView<FGuLiCombatSample> Samples,
		const TMap<uint32, int32>& IndexById, const TMap<FIntPoint, TArray<int32>>& Grid,
		uint32 SimTick, double SimulationSeconds, const FGuLiCombatExecutorRegistry& Executors,
		TArray<FGuLiCombatDamageEvent>& OutEvents);
	/** No World, actors, rendering, network or navigation. Measures grid + the production attack loop. */
	GULISTRIKE_API bool RunBenchmark(int32 PopulationCount, int32 Steps, FGuLiCombatBenchmarkResult& OutResult);
}
