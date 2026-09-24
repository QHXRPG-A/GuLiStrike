#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

/** World-frame budget. Only time inside a Scope is charged; movement integration is not planning. */
struct FGuLiNavigationWorkBudget
{
	int32 Projections = 64;
	int32 Paths = 4;
	int32 Members = MAX_int32;
	double LimitSeconds = .002;
	double WorldLimitSeconds = .002;
	double UsedSeconds = 0;
	double ScopeStartedAt = 0;
	int32 ScopeDepth = 0;
	uint64 QueryOverruns = 0;
	uint32 FrameQueryOverruns = 0;
	uint32 FrameSliceOverruns = 0;
	double MaximumQuerySeconds = 0;

	void Reset(double Milliseconds, int32 ProjectionCount, int32 PathCount, int32 MemberCount = MAX_int32)
	{
		check(ScopeDepth == 0);
		LimitSeconds = FMath::Max(.00001, Milliseconds * .001);
		WorldLimitSeconds = LimitSeconds;
		UsedSeconds = 0;
		FrameQueryOverruns = 0;
		FrameSliceOverruns = 0;
		Projections = FMath::Max(0, ProjectionCount);
		Paths = FMath::Max(0, PathCount);
		Members = FMath::Max(0, MemberCount);
	}
	double Elapsed() const
	{
		return UsedSeconds + (ScopeDepth ? FPlatformTime::Seconds() - ScopeStartedAt : 0);
	}
	bool CanWork() const { return Elapsed() < LimitSeconds; }
	bool TakeProjection()
	{
		if (!CanWork() || Projections <= 0) return false;
		--Projections; return true;
	}
	bool TakePath()
	{
		if (!CanWork() || Paths <= 0) return false;
		--Paths; return true;
	}
	void RecordQuery(double StartedAt, double RemainingAtStart)
	{
		const double Duration = FPlatformTime::Seconds() - StartedAt;
		MaximumQuerySeconds = FMath::Max(MaximumQuerySeconds, Duration);
		QueryOverruns += Duration > RemainingAtStart ? 1 : 0;
		FrameQueryOverruns += Duration > RemainingAtStart ? 1 : 0;
	}
	struct FQueryScope
	{
		FGuLiNavigationWorkBudget& Budget;
		double Started=FPlatformTime::Seconds();
		double Remaining;
		explicit FQueryScope(FGuLiNavigationWorkBudget& In) : Budget(In), Remaining(FMath::Max(0.,In.WorldLimitSeconds-In.Elapsed())) {}
		~FQueryScope() { Budget.RecordQuery(Started,Remaining); }
	};
	struct FScope
	{
		FGuLiNavigationWorkBudget& Budget;
		explicit FScope(FGuLiNavigationWorkBudget& InBudget) : Budget(InBudget)
		{
			if (Budget.ScopeDepth++ == 0) Budget.ScopeStartedAt = FPlatformTime::Seconds();
		}
		~FScope()
		{
			if (--Budget.ScopeDepth == 0) Budget.UsedSeconds += FPlatformTime::Seconds() - Budget.ScopeStartedAt;
		}
	};

	/** One category's guaranteed share. Unused shares may be borrowed in a second pass. */
	struct FAllowance
	{
		double Seconds = 0;
		int32 Projections = 0;
		int32 Paths = 0;
		double UsedSeconds = 0;
		int32 UsedProjections = 0;
		int32 UsedPaths = 0;
	};

	/** Restrict the SAME ledger, including legacy callers holding references to its counters. */
	struct FSlice
	{
		FGuLiNavigationWorkBudget& Budget;
		FAllowance& Share;
		int32 WorldProjections, WorldPaths, StartProjections, StartPaths;
		double WorldLimit, Started, Granted;
		FSlice(FGuLiNavigationWorkBudget& In, FAllowance& InShare)
			: Budget(In), Share(InShare), WorldProjections(In.Projections), WorldPaths(In.Paths),
			  WorldLimit(In.LimitSeconds), Started(In.Elapsed())
		{
			StartProjections = In.Projections = FMath::Min3(In.Projections, FMath::Max(0, Share.Projections), 8);
			StartPaths = In.Paths = FMath::Min3(In.Paths, FMath::Max(0, Share.Paths), 1);
			Granted = FMath::Min(.0002, FMath::Max(0., Share.Seconds));
			In.LimitSeconds = FMath::Min(WorldLimit, Started + Granted);
		}
		~FSlice()
		{
			const double Used = FMath::Max(0., Budget.Elapsed() - Started);
			const int32 PositionsUsed = StartProjections - Budget.Projections;
			const int32 PathsUsed = StartPaths - Budget.Paths;
			check(PositionsUsed >= 0 && PathsUsed >= 0);
			Share.Seconds = FMath::Max(0., Share.Seconds - Used);
			Share.Projections -= PositionsUsed; Share.Paths -= PathsUsed;
			Share.UsedSeconds += Used; Share.UsedProjections += PositionsUsed; Share.UsedPaths += PathsUsed;
			Budget.Projections = WorldProjections - PositionsUsed; Budget.Paths = WorldPaths - PathsUsed;
			Budget.LimitSeconds = WorldLimit;
			Budget.FrameSliceOverruns += Used > Granted ? 1u : 0u;
		}
	};
};
