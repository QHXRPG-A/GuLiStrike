#pragma once

#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "GuLiTeleportSkill.generated.h"

UCLASS()
class GULISTRIKE_API UGuLiTeleportSkillExecutor : public UGuLiCommanderSkillExecutor
{
	GENERATED_BODY()
public:
	virtual bool ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const override;
	virtual FGuLiActiveSkillExecutionResult Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const override;
};
