#pragma once
#include "Commander/Orders/GuLiUnitTaskTypes.h"

struct FStateTreeExecutionContext;

// Identity resolved by the production processor immediately before this tree
// executes. Node callbacks only queue operations; business commits follow the
// Mass query. Exact context identity and a stack lease cover nested execution.
class FGuLiCommanderBehaviorExecution final
{
    inline static thread_local const FGuLiCommanderBehaviorExecution* Active=nullptr;
    const FGuLiCommanderBehaviorExecution* Previous;
    const FStateTreeExecutionContext* Context;
    FGuLiTaskUnitId Unit;
public:
    FGuLiCommanderBehaviorExecution(const FStateTreeExecutionContext& InContext,FGuLiTaskUnitId InUnit);
    ~FGuLiCommanderBehaviorExecution();
    FGuLiCommanderBehaviorExecution(const FGuLiCommanderBehaviorExecution&)=delete;
    FGuLiCommanderBehaviorExecution& operator=(const FGuLiCommanderBehaviorExecution&)=delete;
    static bool Resolve(const FStateTreeExecutionContext& InContext,FGuLiTaskUnitId& Out)
    {
        if (!Active || Active->Context!=&InContext) return false;
        Out=Active->Unit;return true;
    }
};
