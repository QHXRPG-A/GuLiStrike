#pragma once

#include "Gameplay/Skills/GuLiSkillTypes.h"

class UGuLiCommanderDataSettings;
struct FGuLiSpellFieldConfig;

/** Shared authored-data resolver. It never creates a World or simulation subsystem. */
namespace GuLiCommanderSkillCatalog
{
GULISTRIKE_API bool Resolve(const UGuLiCommanderDataSettings* Settings,
    TFunctionRef<bool(uint16)> HasUnit,
    TFunctionRef<const FGuLiSpellFieldConfig*(FName)> FindField,
    TArray<FGuLiSkillDefinition>& SkillDefinitions,
    TArray<FGuLiUnitSkillConfig>& UnitSkillConfigs,FString& Error);
}
