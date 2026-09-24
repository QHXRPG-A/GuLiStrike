#include "Gameplay/Navigation/GuLiEngineeringPathSubsystem.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	TAutoConsoleVariable<int32> QueriesPerFrame(TEXT("guli.engineering.PathQueriesPerFrame"),4,TEXT("Shared authority engineering path query limit."));
	TAutoConsoleVariable<float> BudgetMs(TEXT("guli.engineering.PathBudgetMs"),2.f,TEXT("Soft main-thread query budget; individual synchronous queries cannot be preempted."));
}
bool UGuLiEngineeringPathSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode()!=NM_Client;
}
void UGuLiEngineeringPathSubsystem::Enqueue(UGuLiEngineeringTravelComponent& Travel) { Queue.AddUnique(&Travel); }
void UGuLiEngineeringPathSubsystem::Cancel(UGuLiEngineeringTravelComponent& Travel) { Queue.Remove(&Travel); }
void UGuLiEngineeringPathSubsystem::RecordPathQuery(EGuLiEngineeringPathOrigin Origin, double Milliseconds)
{
	++ActualQueries[int32(Origin)]; ActualQueryMilliseconds[int32(Origin)]+=Milliseconds;
	MaxActualQueryMilliseconds=FMath::Max(MaxActualQueryMilliseconds,Milliseconds);
	OverBudgetSingleQueries+=Milliseconds>FMath::Max(.1f,BudgetMs.GetValueOnGameThread());
}
void UGuLiEngineeringPathSubsystem::Tick(float)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiEngineering_PathBudget);
	const double Start = FPlatformTime::Seconds(); LastFrameQueries = 0;
	int32 Remaining = Queue.Num();
	while (Remaining-- > 0 && !Queue.IsEmpty() && LastFrameQueries < FMath::Max(1,QueriesPerFrame.GetValueOnGameThread())
		&& (FPlatformTime::Seconds()-Start)*1000 < FMath::Max(.1f,BudgetMs.GetValueOnGameThread()))
	{
		auto Item = Queue[0]; Queue.RemoveAt(0,1,EAllowShrinking::No);
		auto* Travel = Item.Get(); if (!Travel) continue;
		const double Before = FPlatformTime::Seconds();
		const bool bQueried = Travel->ProcessQueuedPath();
		if (bQueried)
		{
			const double Elapsed = (FPlatformTime::Seconds()-Before)*1000;
			++LastFrameQueries; ++QueryCount; TotalMilliseconds += Elapsed;
			MaxQueryMilliseconds = FMath::Max(MaxQueryMilliseconds,Elapsed);
			MaxQueueMilliseconds = FMath::Max(MaxQueueMilliseconds,Travel->GetLastQueueWaitMilliseconds());
			if (QueueWaitSamples.Num()<2048) QueueWaitSamples.Add(Travel->GetLastQueueWaitMilliseconds());
			else { QueueWaitSamples[QueueWaitCursor]=Travel->GetLastQueueWaitMilliseconds(); QueueWaitCursor=(QueueWaitCursor+1)%2048; }
		}
		if (Travel->HasQueuedPath()) Queue.AddUnique(Item);
	}
}
TStatId UGuLiEngineeringPathSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiEngineeringPathSubsystem,STATGROUP_Tickables); }
void UGuLiEngineeringPathSubsystem::Deinitialize() { Queue.Reset(); Super::Deinitialize(); }
FString UGuLiEngineeringPathSubsystem::GetBudgetDebug() const
{
	return FString::Printf(TEXT("Queued=%d FrameRequests=%d Requests=%llu ServiceMs=%.3f MaxServiceMs=%.3f MaxWaitMs=%.3f ManualQueries=%llu WorkQueries=%llu RepathQueries=%llu ManualMs=%.3f WorkMs=%.3f RepathMs=%.3f MaxQueryMs=%.3f OverBudgetQueries=%llu QueueCapacityBytes=%llu WaitSamples=%d WaitCapacityBytes=%llu"),
		Queue.Num(),LastFrameQueries,QueryCount,TotalMilliseconds,MaxQueryMilliseconds,MaxQueueMilliseconds,
		ActualQueries[0],ActualQueries[1],ActualQueries[2],ActualQueryMilliseconds[0],ActualQueryMilliseconds[1],ActualQueryMilliseconds[2],
		MaxActualQueryMilliseconds,OverBudgetSingleQueries,uint64(Queue.GetAllocatedSize()),QueueWaitSamples.Num(),uint64(QueueWaitSamples.GetAllocatedSize()));
}
