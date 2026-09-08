// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Gameplay/Skills/GuLiSkillTypes.h"

/** Pure, deterministic and transactional: on failure OutProfiles is unchanged. */
class GULISTRIKE_API FGuLiSkillResolver
{
public:
	static bool Resolve(EGuLiTeam Team, const TArray<FGuLiSkillDefinition>& Definitions,
		const TArray<FGuLiUnitSkillConfig>& Configs, const TArray<FGuLiSkillSource>& Sources,
		const TArray<FGuLiSkillNumericOverride>& Overrides,
		TArray<FGuLiResolvedSkillProfile>& OutProfiles, FString& OutError,
		const TArray<FGuLiSkillLoadoutSelection>* Loadout = nullptr);
	static bool ValidateCatalog(const TArray<FGuLiSkillDefinition>& Definitions,
		const TArray<FGuLiUnitSkillConfig>& Configs, FString& OutError);
	/** Validates the full candidate input, computes only SelectedSlots; output is only these slots.
	 * The caller must include old and new targets of every changed source. Other cached slots
	 * remain validated because no source/override affecting them has changed. */
	static bool ResolveSelected(EGuLiTeam Team, const TArray<FGuLiSkillDefinition>& Definitions,
		const TArray<FGuLiUnitSkillConfig>& Configs, const TArray<FGuLiSkillSource>& Sources,
		const TArray<FGuLiSkillNumericOverride>& Overrides, const TArray<FGuLiSkillSlotKey>& SelectedSlots,
		TArray<FGuLiResolvedSkillProfile>& OutProfiles, FString& OutError,
		const TArray<FGuLiSkillLoadoutSelection>* Loadout = nullptr);
	/** Adds source targets without final-skill filtering: replacements may change that filter. */
	static void GatherAffectedSlots(const TArray<FGuLiUnitSkillConfig>& Configs,
		const FGuLiSkillSource& Source, TArray<FGuLiSkillSlotKey>& InOutSlots);
};
