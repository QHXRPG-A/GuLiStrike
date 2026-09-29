#pragma once

#include "Gameplay/CommanderSkills/GuLiPointSkill.h"
#include "GuLiWarMachineMissileSkill.generated.h"

/** WM01 Q: resolved missile weapon damage and alternating calibrated launch mounts. */
UCLASS()
class GULISTRIKE_API UGuLiWarMachineMissileSkillExecutor : public UGuLiPointSkillExecutor
{
	GENERATED_BODY()
public:
	virtual bool ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const override;
	virtual bool IsAvailable(UWorld& World, EGuLiTeam Team, uint16 UnitTypeId, const UDataAsset* Configuration) const override;
	virtual FGuLiActiveSkillExecutionResult Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const override;
protected:
	virtual bool ConfigurePayload(const FGuLiActiveSkillExecutionContext& Context, const UGuLiPointSkillConfiguration& Configuration,
		FGuLiCombatAttackRequest& Request, FString& Error) const override;
};
