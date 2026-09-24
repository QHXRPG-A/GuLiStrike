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
	double UsedSeconds = 0;
	double ScopeStartedAt = 0;
	int32 ScopeDepth = 0;
	uint64 QueryOverruns = 0;
	uint32 FrameQueryOverruns = 0;
	double MaximumQuerySeconds = 0;

	void Reset(double Milliseconds, int32 ProjectionCount, int32 PathCount, int32 MemberCount = MAX_int32)
	{
		check(ScopeDepth == 0);
		LimitSeconds = FMath::Max(.00001, Milliseconds * .001);
		UsedSeconds = 0;
		FrameQueryOverruns = 0;
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
		explicit FQueryScope(FGuLiNavigationWorkBudget& In) : Budget(In), Remaining(FMath::Max(0.,In.LimitSeconds-In.Elapsed())) {}
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
};
