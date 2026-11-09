#include "Commander/Behavior/Private/GuLiCommanderBehaviorExecution.h"

FGuLiCommanderBehaviorExecution::FGuLiCommanderBehaviorExecution(
    const FStateTreeExecutionContext& InContext,FGuLiTaskUnitId InUnit)
    :Previous(Active),Context(&InContext),Unit(InUnit)
{Active=this;}

FGuLiCommanderBehaviorExecution::~FGuLiCommanderBehaviorExecution()
{Active=Previous;}
