#pragma once
#include "Gameplay/CommanderSkills/GuLiCommanderSkillDefinition.h"

/** Value-only view of the authoritative soldier. No Mass, selection, RPC or UI dependency. */
struct FGuLiUnitSkillCaster
{
	FGuLiActiveSkillExecutionContext Context;
	bool bEligible = false;
	bool bHasGroundPoint = false;
	/** Filled by the server adapter only when the definition names a source weapon slot. */
	float ResolvedSourceRange = 0.0f;
};

namespace GuLiUnitSkillExecution
{
	using FExecute = TFunctionRef<FGuLiActiveSkillExecutionResult(const FGuLiActiveSkillExecutionContext&, const UDataAsset*)>;
	GULISTRIKE_API FGuLiActiveSkillUnitResult Execute(const FGuLiActiveSkillDefinition* Definition,
		const FGuLiUnitSkillCaster& Caster, FGuLiActiveSkillRuntime& Runtime, double SimulationSeconds,
		double ServerSeconds, FExecute ExecuteEffect);
}
