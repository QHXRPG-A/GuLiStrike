#pragma once

#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "GuLiSelfSkill.generated.h"

/** Extension point for caster buffs. A concrete executor must successfully apply its effect before cooldown starts. */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiSelfSkillExecutor : public UGuLiCommanderSkillExecutor
{
	GENERATED_BODY()
public:
	virtual bool ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const override;
	virtual FGuLiActiveSkillExecutionResult Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const override;
	UFUNCTION(BlueprintNativeEvent, Category="Commander Skills") bool ApplySelfEffect(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const;
};

