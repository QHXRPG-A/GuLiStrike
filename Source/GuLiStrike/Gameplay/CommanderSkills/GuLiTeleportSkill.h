#pragma once

#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "GuLiTeleportSkill.generated.h"

/** Optional authored profile for another skill using the relocation mechanism. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiTeleportSkillConfiguration : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Teleport",meta=(TitleProperty="Level")) TArray<FGuLiTeleportFieldConfig> Levels;
};

UCLASS()
class GULISTRIKE_API UGuLiTeleportSkillExecutor : public UGuLiCommanderSkillExecutor
{
	GENERATED_BODY()
public:
	static bool ResolveConfiguration(UWorld&,const FGuLiActiveSkillDefinition&,int32 Level,FGuLiTeleportFieldConfig& Out);
	static bool ResolveAuthoredConfiguration(const FGuLiActiveSkillDefinition&,int32 Level,FGuLiTeleportFieldConfig& Out);
	virtual bool ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const override;
	virtual bool ResolveGroundInput(UWorld& World,const FVector& Requested,FVector& Ground) const override;
	virtual FGuLiActiveSkillExecutionResult Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const override;
    virtual bool UsesHybridAsyncGlobal() const override {return true;}
    virtual bool ReadHybridRuntime(UWorld&,EGuLiTeam,FName,FGuLiActiveSkillRuntime&) const override;
    virtual FGuid SetHybridLevel(UWorld&,EGuLiTeam,FName,int32) const override;
};
