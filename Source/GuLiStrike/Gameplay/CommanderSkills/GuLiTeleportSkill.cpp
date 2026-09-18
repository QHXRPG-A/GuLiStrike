#include "Gameplay/CommanderSkills/GuLiTeleportSkill.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"

bool UGuLiTeleportSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	if (Definition.Scope == EGuLiActiveSkillScope::Global && Definition.TargetMode == EGuLiActiveSkillTargetMode::TwoPoint
		&& Definition.MaximumLevel <= 4) return true;
	Error = TEXT("Teleport requires a global two-point skill with at most four authored levels."); return false;
}
FGuLiActiveSkillExecutionResult UGuLiTeleportSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset*) const
{
	FGuLiActiveSkillExecutionResult Result;
	auto& Commander = *Context.Commander;
	if (Context.Command == EGuLiActiveSkillCommand::Activate)
	{
		if (auto* Field = AGuLiTeleportFieldActor::StartCast(Commander, Context.Level, Context.GroundPoint, Result.Error))
		{ Result.CastId = Field->GetCastState().CastId; Result.bSucceeded = true; }
	}
	else if (auto* Field = AGuLiTeleportFieldActor::FindCast(*Commander.GetWorld(), Commander.GetPlayerGuid());
		Field && Field->GetCastState().CastId == Context.CastId)
	{
		Result.CastId = Context.CastId;
		if (Context.Command == EGuLiActiveSkillCommand::Continue) Result.bSucceeded = Field->SubmitDestination(Commander, Context.GroundPoint, Result.Error);
		else { Field->Cancel(Commander); Result.bSucceeded = true; }
	}
	else Result.Error = TEXT("Teleport cast is no longer owned by this commander.");
	return Result;
}
