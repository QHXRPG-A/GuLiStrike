#pragma once

#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "GuLiSummonSkill.generated.h"

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiSummonSkillConfiguration : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) int32 UnitTypeId = 5;
	UPROPERTY(EditAnywhere) int32 Count = 5;
	UPROPERTY(EditAnywhere) float ClearanceCentimeters = 50;
	UPROPERTY(EditAnywhere) int32 OuterRings = 3;
	UPROPERTY(EditAnywhere) int32 CandidatesPerRing = 36;
};

UCLASS()
class GULISTRIKE_API UGuLiSummonSkillExecutor : public UGuLiCommanderSkillExecutor
{
	GENERATED_BODY()
public:
	virtual bool ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const override;
	virtual bool IsAvailable(UWorld& World, EGuLiTeam Team, uint16 UnitTypeId, const UDataAsset* Configuration) const override;
	virtual FGuLiActiveSkillExecutionResult Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const override;
};
