#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
namespace GuLiHybridTeleport
{
    FGuid Request(const FGuLiActiveSkillExecutionContext& Context);
    FGuid SetLevel(UWorld& World,EGuLiTeam Team,FName Skill,int32 Level);
    bool ReadRuntime(UWorld& World,EGuLiTeam Team,FName Skill,FGuLiActiveSkillRuntime& Out);
    FGuLiTeleportCastState Query(UWorld& World,EGuLiTeam Team);
}
