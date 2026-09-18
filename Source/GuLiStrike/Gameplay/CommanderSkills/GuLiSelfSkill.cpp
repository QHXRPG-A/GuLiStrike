#include "Gameplay/CommanderSkills/GuLiSelfSkill.h"

bool UGuLiSelfSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	if (Definition.TargetMode == EGuLiActiveSkillTargetMode::Self) return true;
	Error = TEXT("Self-effect executor requires Self targeting."); return false;
}
bool UGuLiSelfSkillExecutor::ApplySelfEffect_Implementation(const FGuLiActiveSkillExecutionContext&, const UDataAsset*) const { return false; }
FGuLiActiveSkillExecutionResult UGuLiSelfSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const
{
	FGuLiActiveSkillExecutionResult Result;
	Result.bSucceeded = ApplySelfEffect(Context, Configuration);
	if (!Result.bSucceeded) Result.Error = TEXT("Self effect did not apply.");
	return Result;
}
