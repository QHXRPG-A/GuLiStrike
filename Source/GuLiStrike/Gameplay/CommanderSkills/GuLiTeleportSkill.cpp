#include "Gameplay/CommanderSkills/GuLiTeleportSkill.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Gameplay/Navigation/GuLiLandingGround.h"
#include "Gameplay/Teleport/GuLiHybridTeleport.h"
#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Engine/World.h"

bool UGuLiTeleportSkillExecutor::ResolveGroundInput(UWorld& World,const FVector& Requested,FVector& Ground) const
{return GuLiLandingGround::Resolve(World,Requested,Ground);}

bool UGuLiTeleportSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	if (Definition.Scope != EGuLiActiveSkillScope::Global || Definition.TargetMode != EGuLiActiveSkillTargetMode::TwoPoint || Definition.MaximumLevel<1)
    {Error=TEXT("Relocation requires a global two-point skill");return false;}
    if (const auto* Config=Cast<UGuLiTeleportSkillConfiguration>(Definition.Configuration))
    {
        TSet<int32> Levels;
        for (const auto& C:Config->Levels) if (!C.IsValid()||C.Level>Definition.MaximumLevel||Levels.Contains(C.Level))
        {Error=TEXT("Invalid or duplicate authored relocation level");return false;} else Levels.Add(C.Level);
        if (Levels.Num()==Definition.MaximumLevel) return true;
        Error=TEXT("Missing authored relocation level");return false;
    }
    if (!Definition.Configuration && Definition.MaximumLevel<=4) return true;
    Error=TEXT("Relocation requires an authored configuration or the existing four-tier table");return false;
}
bool UGuLiTeleportSkillExecutor::ResolveConfiguration(UWorld& W,const FGuLiActiveSkillDefinition& D,int32 Level,FGuLiTeleportFieldConfig& Out)
{
    const FGuLiTeleportFieldConfig* C=nullptr;
    if (const auto* Config=Cast<UGuLiTeleportSkillConfiguration>(D.Configuration)) C=Config->Levels.FindByPredicate([Level](const auto& V){return V.Level==Level;});
    else if (!D.Configuration) if (const auto* Data=W.GetSubsystem<UGuLiCommanderDataSubsystem>()) C=Data->FindTeleportFieldConfig(Level);
    if (!C || !C->IsValid()) return false;Out=*C;return true;
}
bool UGuLiTeleportSkillExecutor::ResolveAuthoredConfiguration(const FGuLiActiveSkillDefinition& D,int32 Level,FGuLiTeleportFieldConfig& Out)
{
    FString Error;if (Level<1 || Level>D.MaximumLevel || !GetDefault<UGuLiTeleportSkillExecutor>()->ValidateDefinition(D,Error)) return false;
    if (const auto* Config=Cast<UGuLiTeleportSkillConfiguration>(D.Configuration))
    {
        const auto* C=Config->Levels.FindByPredicate([Level](const auto& V){return V.Level==Level;});
        if (!C || !C->IsValid()) return false;Out=*C;return true;
    }
    return !D.Configuration && UGuLiSpellFieldDataSubsystem::ResolveAuthoredTeleportConfig(Level,Out);
}
FGuLiActiveSkillExecutionResult UGuLiTeleportSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset*) const
{
    FGuLiActiveSkillExecutionResult Result;
        Result.EffectId=GuLiHybridTeleport::Request(Context);Result.bPendingCommit=Result.EffectId.IsValid();
        Result.CastId=Context.Command==EGuLiActiveSkillCommand::Activate?Context.RequestId:Context.CastId;
        if (!Result.bPendingCommit) Result.Error=TEXT("Relocation request could not enter the controller's command stream");return Result;
}
bool UGuLiTeleportSkillExecutor::ReadHybridRuntime(UWorld& W,EGuLiTeam Team,FName Skill,FGuLiActiveSkillRuntime& Out) const
{return GuLiHybridTeleport::ReadRuntime(W,Team,Skill,Out);}
FGuid UGuLiTeleportSkillExecutor::SetHybridLevel(UWorld& W,EGuLiTeam Team,FName Skill,int32 Level) const
{return GuLiHybridTeleport::SetLevel(W,Team,Skill,Level);}
