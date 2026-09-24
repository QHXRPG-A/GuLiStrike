#pragma once
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"

/** Stateless adapters to existing business capabilities. Only called outside Mass iteration, on authority. */
namespace GuLiCommanderAbilities
{
	bool Validate(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, const FGuLiUnitTaskCommand& Command, FString& Error);
	bool BuildAutomatic(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand& Command);
	EGuLiTaskStatus Start(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task);
	EGuLiTaskStatus Poll(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task);
	bool Cancel(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task);
}
