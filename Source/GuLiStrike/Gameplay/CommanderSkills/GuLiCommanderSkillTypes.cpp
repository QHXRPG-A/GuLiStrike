#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"

bool FGuLiActiveSkillRequest::HasSameContent(const FGuLiActiveSkillRequest& Other) const
{
	return MatchEpoch == Other.MatchEpoch && SelectionRevision == Other.SelectionRevision && GlobalSkillId == Other.GlobalSkillId
		&& Command == Other.Command && CastId == Other.CastId && bHasGroundPoint == Other.bHasGroundPoint && GroundPoint == Other.GroundPoint;
}
const FGuLiActiveSkillDefinition* UGuLiCommanderSkillCatalog::FindSkill(FName Id) const
{ return Skills.FindByPredicate([Id](const auto& Entry) { return Entry.SkillId == Id; }); }
const FGuLiActiveSkillDefinition* UGuLiCommanderSkillCatalog::FindUnitSkill(uint16 UnitTypeId) const
{
	const FName* Id = UnitSkills.Find(UnitTypeId); return Id ? FindSkill(*Id) : nullptr;
}
bool UGuLiCommanderSkillCatalog::Validate(FString& Error) const
{
	TSet<FName> Seen;
	for (const auto& Definition : Skills)
	{
		if (Definition.SkillId.IsNone() || Seen.Contains(Definition.SkillId) || !Definition.ExecutorClass
			|| Definition.ExecutorClass->HasAnyClassFlags(CLASS_Abstract) || !FMath::IsFinite(Definition.CooldownSeconds)
			|| Definition.CooldownSeconds < 0 || !FMath::IsFinite(Definition.RangeCentimeters) || Definition.RangeCentimeters < 0 || Definition.MaximumLevel < 1)
		{ Error = TEXT("Skill requires a unique ID, concrete executor and valid cooldown/range/level."); return false; }
		if (!Definition.RangeSourceSlot.IsNone() && (Definition.Scope != EGuLiActiveSkillScope::Unit
			|| Definition.TargetMode != EGuLiActiveSkillTargetMode::GroundPoint
			|| !FMath::IsFinite(Definition.RangeMultiplier) || Definition.RangeMultiplier <= 0))
		{ Error = TEXT("A weapon-derived range requires a point unit skill and a positive finite multiplier."); return false; }
		if (!Definition.ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->ValidateDefinition(Definition, Error)) return false;
		Seen.Add(Definition.SkillId);
	}
	for (const auto& Pair : UnitSkills)
	{
		if (Pair.Key < 1 || Pair.Key > MAX_uint16) { Error = TEXT("Unit skill mapping has an invalid UnitTypeId."); return false; }
		if (Pair.Value.IsNone()) continue;
		const auto* Definition = FindSkill(Pair.Value);
		if (!Definition || Definition->Scope != EGuLiActiveSkillScope::Unit || Definition->TargetMode == EGuLiActiveSkillTargetMode::TwoPoint)
		{ Error = TEXT("Unit skill mapping requires a unit-scoped, single-command skill."); return false; }
		if (Definition->TargetMode == EGuLiActiveSkillTargetMode::GroundPoint
			&& Definition->RangeSourceSlot.IsNone() && Definition->RangeCentimeters <= 0)
		{ Error = TEXT("Ground-targeted unit skills require positive range."); return false; }
	}
	Seen.Reset();
	for (FName Id : GlobalSkills)
	{
		const auto* Definition = FindSkill(Id);
		if (!Definition || Definition->Scope != EGuLiActiveSkillScope::Global || Seen.Contains(Id))
		{ Error = TEXT("Global skills require unique global-scoped definitions."); return false; }
		Seen.Add(Id);
	}
	return true;
}
bool UGuLiCommanderSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition&, FString&) const { return true; }
FGuLiActiveSkillExecutionResult UGuLiCommanderSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext&, const UDataAsset*) const
{
	FGuLiActiveSkillExecutionResult Result; Result.Error = TEXT("Executor has no implementation."); return Result;
}
