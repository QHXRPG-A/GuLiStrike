// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiSoldierCombat.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"

namespace
{
	bool IsEnemyInRange(const FGuLiCombatSample& Source, const FGuLiCombatSample& Target)
	{
		return Source.Profile && Target.SoldierId.IsValid() && Source.SoldierId != Target.SoldierId && Target.bAlive
			&& GuLiCommanderProtocol::IsPlayableTeam(Target.Team) && Source.Team != Target.Team
			&& !Target.Location.ContainsNaN()
			&& FVector::DistSquared(Source.Location, Target.Location)
				<= FMath::Square(static_cast<double>(Source.Profile->RangeCentimeters));
	}

	const FGuLiCombatSample* FindTarget(const FGuLiCombatSample& Source,
		const TConstArrayView<FGuLiCombatSample> Samples, const TMap<uint32, int32>& IndexById)
	{
		const int32* Index = IndexById.Find(Source.Attack->TargetId.Value);
		return Index && Samples.IsValidIndex(*Index) && IsEnemyInRange(Source, Samples[*Index])
			? &Samples[*Index] : nullptr;
	}
}

FGuLiCombatExecutorRegistry::FGuLiCombatExecutorRegistry()
{
	FExecutor EmitAttack = [](const FGuLiCombatSample& Source,
		const FGuLiCombatSample& Target, TArray<FGuLiCombatDamageEvent>& Events)
	{
		Events.Add({Source.SoldierId, Target.SoldierId, Source.Profile->SkillId, Source.Profile->Damage,
			Source.Profile->UnitTypeId, Source.Profile->SlotId, Source.Profile->Revision,
			Source.Profile->ExecutorId, Source.Attack ? Source.Attack->ShotsFired + 1 : 0});
	};
	RegisterExecutor(TEXT("DirectSingleTarget"), EmitAttack);
	RegisterExecutor(TEXT("LaunchProjectile"), MoveTemp(EmitAttack));
}

bool FGuLiCombatExecutorRegistry::RegisterExecutor(const FName Id, FExecutor Executor)
{
	if (Id.IsNone() || !Executor || Executors.Contains(Id)) return false;
	Executors.Add(Id, MoveTemp(Executor));
	return true;
}

bool FGuLiCombatExecutorRegistry::Contains(const FName Id) const { return Executors.Contains(Id); }

bool FGuLiCombatExecutorRegistry::Execute(const FGuLiCombatSample& Source,
	const FGuLiCombatSample& Target, TArray<FGuLiCombatDamageEvent>& OutEvents) const
{
	const FExecutor* Executor = Source.Profile ? Executors.Find(Source.Profile->ExecutorId) : nullptr;
	if (!Executor) return false;
	(*Executor)(Source, Target, OutEvents);
	return true;
}

const TCHAR* GuLiSoldierCombat::LexToString(const EGuLiCombatStopReason Reason)
{
	switch (Reason)
	{
	case EGuLiCombatStopReason::Dead: return TEXT("dead");
	case EGuLiCombatStopReason::Moving: return TEXT("move_order");
	case EGuLiCombatStopReason::Disabled: return TEXT("disabled_profile");
	case EGuLiCombatStopReason::UnknownExecutor: return TEXT("unknown_executor");
	case EGuLiCombatStopReason::NoTarget: return TEXT("no_target");
	case EGuLiCombatStopReason::Cooldown: return TEXT("cooldown");
	case EGuLiCombatStopReason::Fired: return TEXT("fired");
	default: return TEXT("unknown");
	}
}

FIntPoint GuLiSoldierCombat::MakeSpatialCell(const FVector& Location)
{
	return FIntPoint(FMath::FloorToInt(Location.X / SpatialCellSizeCentimeters),
		FMath::FloorToInt(Location.Y / SpatialCellSizeCentimeters));
}

bool GuLiSoldierCombat::IsProfileUsable(const FGuLiResolvedSkillProfile& Profile)
{
	return Profile.bUnlocked && Profile.bEquipped && !Profile.SkillId.IsNone() && !Profile.ExecutorId.IsNone()
		&& FMath::IsFinite(Profile.Damage) && Profile.Damage > 0.0f
		&& FMath::IsFinite(Profile.AttackRatePerSecond) && Profile.AttackRatePerSecond > 0.0f && Profile.AttackRatePerSecond <= 30.0f
		&& FMath::IsFinite(Profile.RangeCentimeters) && Profile.RangeCentimeters > 0.0f
		&& Profile.RangeCentimeters <= 1000000.0f;
}

void GuLiSoldierCombat::ReplaceProfile(const FGuLiResolvedSkillProfile& Previous,
	const FGuLiResolvedSkillProfile& Next, const double SimulationSeconds, FGuLiSoldierAttackState& State)
{
	if (Previous.SkillId != Next.SkillId || Previous.ExecutorId != Next.ExecutorId
		|| (!Previous.bEquipped && Next.bEquipped))
	{
		State.TargetId = FGuLiSoldierId();
		State.NextFireSeconds = FMath::Max(State.NextFireSeconds,
			SimulationSeconds + (Next.AttackRatePerSecond > 0.0f ? 1.0 / Next.AttackRatePerSecond : 0.0));
		State.PausedCooldownFraction = 1.0;
		State.bRatePaused = Next.AttackRatePerSecond <= 0.0f;
	}
	else if (Previous.AttackRatePerSecond != Next.AttackRatePerSecond)
	{
		const double RemainingFraction = Previous.AttackRatePerSecond > 0.0f
			? FMath::Clamp((State.NextFireSeconds - SimulationSeconds) * Previous.AttackRatePerSecond, 0.0, 1.0)
			: (State.bRatePaused ? State.PausedCooldownFraction : 1.0);
		State.bRatePaused = Next.AttackRatePerSecond <= 0.0f;
		State.PausedCooldownFraction = RemainingFraction;
		State.NextFireSeconds = SimulationSeconds + (State.bRatePaused ? 0.0 : RemainingFraction / Next.AttackRatePerSecond);
	}
}

void GuLiSoldierCombat::BuildSpatialGrid(const TConstArrayView<FGuLiCombatSample> Samples,
	TMap<FIntPoint, TArray<int32>>& OutGrid)
{
	OutGrid.Reset();
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		if (Samples[Index].bAlive && !Samples[Index].Location.ContainsNaN())
			OutGrid.FindOrAdd(MakeSpatialCell(Samples[Index].Location)).Add(Index);
	}
}

FGuLiCombatStepMetrics GuLiSoldierCombat::CollectAttacks(const TConstArrayView<FGuLiCombatSample> Samples,
	const TMap<uint32, int32>& IndexById, const TMap<FIntPoint, TArray<int32>>& Grid,
	const uint32 SimTick, const double SimulationSeconds, const FGuLiCombatExecutorRegistry& Executors,
	TArray<FGuLiCombatDamageEvent>& OutEvents)
{
	return CollectChannelAttacks(Samples, Samples, IndexById, Grid, SimTick, SimulationSeconds, Executors, OutEvents);
}

FGuLiCombatStepMetrics GuLiSoldierCombat::CollectChannelAttacks(const TConstArrayView<FGuLiCombatSample> Channels,
	const TConstArrayView<FGuLiCombatSample> Samples, const TMap<uint32, int32>& IndexById,
	const TMap<FIntPoint, TArray<int32>>& Grid, const uint32 SimTick, const double SimulationSeconds,
	const FGuLiCombatExecutorRegistry& Executors, TArray<FGuLiCombatDamageEvent>& OutEvents)
{
	OutEvents.Reset();
	FGuLiCombatStepMetrics Metrics;
	for (const FGuLiCombatSample& Source : Channels)
	{
		if (!Source.Attack) continue;
		FGuLiSoldierAttackState& State = *Source.Attack;
		if (!Source.bAlive) { State.TargetId = {}; State.StopReason = EGuLiCombatStopReason::Dead; continue; }
		if (!Source.SoldierId.IsValid() || !Source.Profile || !IsProfileUsable(*Source.Profile)
			|| !GuLiCommanderProtocol::IsPlayableTeam(Source.Team) || Source.Location.ContainsNaN())
		{ State.TargetId = {}; State.StopReason = EGuLiCombatStopReason::Disabled; continue; }
		if (!Executors.Contains(Source.Profile->ExecutorId))
		{ State.TargetId = {}; State.StopReason = EGuLiCombatStopReason::UnknownExecutor; continue; }
		const FGuLiCombatSample* Target = FindTarget(Source, Samples, IndexById);
		if (!Target)
		{
			State.TargetId = {};
			const uint32 SlotOffset = Source.Profile->SlotId == FName(TEXT("BasicAttack")) ? 0u
				: FCrc::StrCrc32(*Source.Profile->SlotId.ToString());
			if ((Source.SoldierId.Value + SlotOffset) % AcquisitionPeriodTicks == SimTick % AcquisitionPeriodTicks)
			{
				++Metrics.TargetQueries;
				const double Range = Source.Profile->RangeCentimeters;
				const FIntPoint Min = MakeSpatialCell(Source.Location - FVector(Range, Range, 0.0));
				const FIntPoint Max = MakeSpatialCell(Source.Location + FVector(Range, Range, 0.0));
				double BestDistance = TNumericLimits<double>::Max();
				for (int32 X = Min.X; X <= Max.X; ++X)
				for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
				{
					const TArray<int32>* Indices = Grid.Find(FIntPoint(X, Y));
					if (!Indices) continue;
					for (const int32 Index : *Indices)
					{
						if (!Samples.IsValidIndex(Index)) continue;
						++Metrics.CandidateChecks;
						const FGuLiCombatSample& Candidate = Samples[Index];
						if (!IsEnemyInRange(Source, Candidate)) continue;
						const double Distance = FVector::DistSquared(Source.Location, Candidate.Location);
						if (Distance < BestDistance || (Distance == BestDistance
							&& (!Target || Candidate.SoldierId < Target->SoldierId)))
						{ Target = &Candidate; BestDistance = Distance; }
					}
				}
				if (Target) State.TargetId = Target->SoldierId;
			}
		}
		if (!Target) { State.StopReason = EGuLiCombatStopReason::NoTarget; continue; }
		if (SimulationSeconds + 1.e-7 < State.NextFireSeconds)
		{ State.StopReason = EGuLiCombatStopReason::Cooldown; continue; }
		if (Executors.Execute(Source, *Target, OutEvents))
		{
			// Never replay shots accumulated while moving, without a target, or after a hitch.
			const double Interval = 1.0 / Source.Profile->AttackRatePerSecond;
			// Carry only fixed-step rounding error (e.g. 4/s needs alternating 2/3 ticks).
			// Any larger overdue period is discarded, so movement/hitches cannot bank a burst.
			const bool bOnlyStepRounding = State.NextFireSeconds > 0.0
				&& SimulationSeconds - State.NextFireSeconds <= GuLiCommanderSimulationTiming::StepSeconds + 1.e-7;
			State.NextFireSeconds = bOnlyStepRounding ? State.NextFireSeconds + Interval
				: SimulationSeconds + Interval;
			State.StopReason = EGuLiCombatStopReason::Fired;
			++State.ShotsFired;
			++Metrics.Shots;
		}
	}
	return Metrics;
}

bool GuLiSoldierCombat::RunBenchmark(const int32 PopulationCount, const int32 Steps,
	FGuLiCombatBenchmarkResult& OutResult)
{
	OutResult = {};
	if ((PopulationCount != 500 && PopulationCount != 10000) || Steps < 1 || Steps > 3600) return false;
	FGuLiResolvedSkillProfile Profile;
	Profile.SkillId = TEXT("Strafe"); Profile.ExecutorId = TEXT("DirectSingleTarget");
	Profile.Damage = 7.5f; Profile.AttackRatePerSecond = 4.0f; Profile.RangeCentimeters = 15000.0f;
	TArray<FGuLiSoldierAttackState> States; States.SetNum(PopulationCount);
	TArray<FGuLiCombatSample> Samples; Samples.SetNum(PopulationCount);
	TMap<uint32, int32> Indices;
	for (int32 Index = 0; Index < PopulationCount; ++Index)
	{
		FGuLiCombatSample& Sample = Samples[Index];
		Sample.SoldierId = FGuLiSoldierId(Index + 1);
		Sample.Team = Index % 2 == 0 ? EGuLiTeam::Red : EGuLiTeam::Blue;
		Sample.Location = FVector((Index % 100) * 1800.0, (Index / 100) * 1800.0, 0.0);
		Sample.bAlive = true; Sample.Profile = &Profile; Sample.Attack = &States[Index];
		Indices.Add(Sample.SoldierId.Value, Index);
	}
	TMap<FIntPoint, TArray<int32>> Grid;
	TArray<FGuLiCombatDamageEvent> Events;
	TArray<double> Times; Times.Reserve(Steps);
	const FGuLiCombatExecutorRegistry Executors;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const double Start = FPlatformTime::Seconds();
		BuildSpatialGrid(Samples, Grid);
		const FGuLiCombatStepMetrics Metrics = CollectAttacks(Samples, Indices, Grid,
			Step + 1, static_cast<double>(Step + 1) / GuLiCommanderSimulationTiming::RateHz, Executors, Events);
		const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
		Times.Add(Milliseconds); OutResult.MeanMilliseconds += Milliseconds;
		OutResult.Shots += Metrics.Shots; OutResult.TargetQueries += Metrics.TargetQueries;
		OutResult.CandidateChecks += Metrics.CandidateChecks;
	}
	Times.Sort();
	OutResult.PopulationCount = PopulationCount; OutResult.Steps = Steps;
	OutResult.MeanMilliseconds /= Steps;
	OutResult.MaximumMilliseconds = Times.Last();
	OutResult.P95Milliseconds = Times[FMath::Clamp(FMath::CeilToInt(Steps * 0.95) - 1, 0, Steps - 1)];
	return true;
}
