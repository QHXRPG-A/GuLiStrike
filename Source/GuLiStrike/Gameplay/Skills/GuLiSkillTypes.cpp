#include "Gameplay/Skills/GuLiSkillTypes.h"

bool FGuLiResolvedSkillProfile::HasSameConfiguration(const FGuLiResolvedSkillProfile& Other) const
{
	return Team == Other.Team && UnitTypeId == Other.UnitTypeId && SlotId == Other.SlotId
		&& SkillId == Other.SkillId && ExecutorId == Other.ExecutorId && Tags == Other.Tags
		&& Damage == Other.Damage && AttackRatePerSecond == Other.AttackRatePerSecond
		&& RangeCentimeters == Other.RangeCentimeters
		&& bUnlocked == Other.bUnlocked && bEquipped == Other.bEquipped;
}

bool FGuLiSkillTargetSelector::MatchesUnitSlot(uint16 UnitTypeId, FName InSlotId) const
{
	return SlotId == InSlotId && (UnitTypeIds.IsEmpty() || UnitTypeIds.Contains(UnitTypeId));
}

bool FGuLiSkillTargetSelector::MatchesFinalSkill(const FGuLiSkillDefinition& Definition) const
{
	return (RequiredSkillId.IsNone() || RequiredSkillId == Definition.SkillId)
		&& Definition.Tags.HasAll(RequiredTags);
}
