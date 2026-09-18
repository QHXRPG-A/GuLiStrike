#include "Gameplay/Skills/GuLiSkillResolver.h"

namespace GuLiSkillResolverPrivate
{
	bool ValidValue(double Value) { return FMath::IsFinite(Value) && Value >= 0.0 && Value <= 1000000000.0; }
	bool ValidAttribute(double Value, int32 Index)
	{
		return ValidValue(Value) && (Index != 1 || Value <= 30.0) && (Index != 2 || Value <= 1000000.0);
	}
	FString Key(uint16 UnitTypeId, FName SlotId) { return FString::Printf(TEXT("%u/%s"), UnitTypeId, *SlotId.ToString()); }
	const FGuLiSkillDefinition* FindDefinition(const TArray<FGuLiSkillDefinition>& Definitions, FName Id)
	{
		return Definitions.FindByPredicate([Id](const auto& Entry) { return Entry.SkillId == Id; });
	}
	bool ValidTarget(const FGuLiSkillTargetSelector& Target, const TArray<FGuLiSkillDefinition>& Definitions,
		const TArray<FGuLiUnitSkillConfig>& Configs, FString& Error)
	{
		if (Target.SlotId.IsNone() || (!Target.RequiredSkillId.IsNone() && !FindDefinition(Definitions, Target.RequiredSkillId)))
		{
			Error = TEXT("Target has an empty slot or unknown required skill."); return false;
		}
		for (uint16 Unit : Target.UnitTypeIds)
		{
			if (!Configs.ContainsByPredicate([&](const auto& Row) { return Row.bDefault && Row.UnitTypeId == Unit && Row.SlotId == Target.SlotId; }))
			{
				Error = FString::Printf(TEXT("Target references unknown unit/slot %s."), *Key(Unit, Target.SlotId)); return false;
			}
		}
		if (!Configs.ContainsByPredicate([&](const auto& Row) { return Row.bDefault && Target.MatchesUnitSlot(Row.UnitTypeId, Row.SlotId); }))
		{
			Error = TEXT("Target matches no authored unit/slot."); return false;
		}
		return true;
	}
}

bool FGuLiSkillResolver::ValidateCatalog(const TArray<FGuLiSkillDefinition>& Definitions,
	const TArray<FGuLiUnitSkillConfig>& Configs, FString& OutError)
{
	using namespace GuLiSkillResolverPrivate;
	OutError.Reset();
	if (Definitions.IsEmpty() || Configs.IsEmpty()) { OutError = TEXT("Skills and UnitSkills must both contain rows."); return false; }
	TSet<FName> SkillIds;
	for (const auto& Def : Definitions)
	{
		if (Def.SkillId.IsNone() || Def.ExecutorId.IsNone() || SkillIds.Contains(Def.SkillId))
		{ OutError = TEXT("Skill IDs must be unique and skill/executor IDs nonempty."); return false; }
		SkillIds.Add(Def.SkillId);
	}
	TSet<FString> UniqueConfigs;
	TMap<FString, int32> DefaultCounts;
	TMap<uint16, TSet<FName>> SlotsByUnit;
	for (const auto& Row : Configs)
	{
		const FString SlotKey = Key(Row.UnitTypeId, Row.SlotId);
		const FString ConfigKey = SlotKey + TEXT("/") + Row.SkillId.ToString();
		if (Row.UnitTypeId == 0 || Row.SlotId.IsNone() || !SkillIds.Contains(Row.SkillId) || UniqueConfigs.Contains(ConfigKey)
			|| !ValidAttribute(Row.Damage, 0) || !ValidAttribute(Row.AttackRatePerSecond, 1) || !ValidAttribute(Row.RangeCentimeters, 2))
		{ OutError = FString::Printf(TEXT("Invalid/duplicate UnitSkills row %s (finite nonnegative; damage<=1e9, rate<=30, range<=1e6)."), *ConfigKey); return false; }
		const auto* Definition = FindDefinition(Definitions, Row.SkillId);
		if (Definition->ExecutorId == TEXT("GroundMachineGun")
			&& (!FMath::IsFinite(Row.ProjectileSpeedCentimetersPerSecond) || Row.ProjectileSpeedCentimetersPerSecond <= 0 || Row.ProjectileSpeedCentimetersPerSecond > 1000000
				|| !FMath::IsFinite(Row.ProjectileLifetimeSeconds) || Row.ProjectileLifetimeSeconds < 0.01f || Row.ProjectileLifetimeSeconds > 120
				|| !FMath::IsFinite(Row.ProjectileSweepRadiusCentimeters) || Row.ProjectileSweepRadiusCentimeters < 0 || Row.ProjectileSweepRadiusCentimeters > 10000))
		{ OutError = FString::Printf(TEXT("Invalid ground projectile motion in UnitSkills row %s."), *ConfigKey); return false; }
		UniqueConfigs.Add(ConfigKey);
		SlotsByUnit.FindOrAdd(Row.UnitTypeId).Add(Row.SlotId);
		if (SlotsByUnit.FindChecked(Row.UnitTypeId).Num() > 32)
		{ OutError = TEXT("A unit type exceeds the 32-channel runtime safety limit."); return false; }
		DefaultCounts.FindOrAdd(SlotKey) += Row.bDefault ? 1 : 0;
	}
	for (const auto& Entry : DefaultCounts)
	{
		if (Entry.Value != 1) { OutError = FString::Printf(TEXT("Unit/slot %s needs exactly one default skill."), *Entry.Key); return false; }
	}
	return true;
}

bool FGuLiSkillResolver::Resolve(EGuLiTeam Team, const TArray<FGuLiSkillDefinition>& Definitions,
	const TArray<FGuLiUnitSkillConfig>& Configs, const TArray<FGuLiSkillSource>& Sources,
	const TArray<FGuLiSkillNumericOverride>& Overrides, TArray<FGuLiResolvedSkillProfile>& OutProfiles, FString& OutError,
	const TArray<FGuLiSkillLoadoutSelection>* Loadout)
{
	TArray<FGuLiSkillSlotKey> AllSlots;
	for (const auto& Config : Configs)
		if (Config.bDefault) AllSlots.AddUnique({Config.UnitTypeId, Config.SlotId});
	return ResolveSelected(Team, Definitions, Configs, Sources, Overrides, AllSlots, OutProfiles, OutError, Loadout);
}

void FGuLiSkillResolver::GatherAffectedSlots(const TArray<FGuLiUnitSkillConfig>& Configs,
	const FGuLiSkillSource& Source, TArray<FGuLiSkillSlotKey>& InOutSlots)
{
	for (const auto& Config : Configs)
	{
		if (!Config.bDefault) continue;
		const bool bTargeted = Source.Modifiers.ContainsByPredicate([&](const auto& Modifier)
			{ return Modifier.Target.MatchesUnitSlot(Config.UnitTypeId, Config.SlotId); })
			|| Source.Replacements.ContainsByPredicate([&](const auto& Replacement)
			{ return Replacement.Target.MatchesUnitSlot(Config.UnitTypeId, Config.SlotId); })
			|| Source.Unlocks.ContainsByPredicate([&](const auto& Unlock)
			{ return Unlock.Target.MatchesUnitSlot(Config.UnitTypeId, Config.SlotId); });
		if (bTargeted) InOutSlots.AddUnique({Config.UnitTypeId, Config.SlotId});
	}
}

bool FGuLiSkillResolver::ResolveSelected(EGuLiTeam Team, const TArray<FGuLiSkillDefinition>& Definitions,
	const TArray<FGuLiUnitSkillConfig>& Configs, const TArray<FGuLiSkillSource>& Sources,
	const TArray<FGuLiSkillNumericOverride>& Overrides, const TArray<FGuLiSkillSlotKey>& SelectedSlots,
	TArray<FGuLiResolvedSkillProfile>& OutProfiles, FString& OutError,
	const TArray<FGuLiSkillLoadoutSelection>* Loadout)
{
	using namespace GuLiSkillResolverPrivate;
	if (Team != EGuLiTeam::Red && Team != EGuLiTeam::Blue) { OutError = TEXT("A real team is required."); return false; }
	if (!ValidateCatalog(Definitions, Configs, OutError)) return false;
	TSet<FString> EquipmentKeys;
	if (Loadout) for (const auto& Selection : *Loadout)
	{
		const FString SelectionKey = Key(Selection.UnitTypeId, Selection.SlotId);
		if (EquipmentKeys.Contains(SelectionKey) || !Configs.ContainsByPredicate([&](const auto& Row)
			{ return Row.UnitTypeId == Selection.UnitTypeId && Row.SlotId == Selection.SlotId
				&& (Selection.SkillId.IsNone() ? Row.bDefault : Row.SkillId == Selection.SkillId); }))
		{ OutError = TEXT("Equipment has a duplicate/unknown slot or incompatible weapon."); return false; }
		EquipmentKeys.Add(SelectionKey);
	}
	for (const auto& Slot : SelectedSlots)
	{
		if (!Configs.ContainsByPredicate([&](const auto& Config) { return Config.bDefault && Config.UnitTypeId == Slot.UnitTypeId && Config.SlotId == Slot.SlotId; }))
		{ OutError = TEXT("Selected resolution key is not an authored unit/slot."); return false; }
	}
	TSet<FGuid> SourceIds;
	for (const auto& Source : Sources)
	{
		if (!Source.SourceInstanceId.IsValid() || SourceIds.Contains(Source.SourceInstanceId))
		{ OutError = TEXT("Source instance IDs must be valid and unique."); return false; }
		SourceIds.Add(Source.SourceInstanceId);
		for (const auto& Unlock : Source.Unlocks)
		{
			if (!ValidTarget(Unlock.Target, Definitions, Configs, OutError)) return false;
			if (!Unlock.Target.RequiredSkillId.IsNone() || !Unlock.Target.RequiredTags.IsEmpty())
			{ OutError = TEXT("Slot unlocks must not depend on the currently equipped skill."); return false; }
		}
		for (const auto& Modifier : Source.Modifiers)
		{
			if (!ValidTarget(Modifier.Target, Definitions, Configs, OutError)) return false;
			if (!FMath::IsFinite(Modifier.Magnitude) || static_cast<uint8>(Modifier.Attribute) > 2
				|| static_cast<uint8>(Modifier.Operation) > 1
				|| (Modifier.Operation == EGuLiSkillModifierOperation::AddPercent && Modifier.Magnitude < -1.0f))
			{ OutError = TEXT("Invalid modifier attribute/operation/magnitude; percentage cannot be below -100%."); return false; }
		}
		for (const auto& Replacement : Source.Replacements)
		{
			if (!ValidTarget(Replacement.Target, Definitions, Configs, OutError)) return false;
			if (!Replacement.Target.RequiredSkillId.IsNone() || !Replacement.Target.RequiredTags.IsEmpty()
				|| !FindDefinition(Definitions, Replacement.SkillId))
			{ OutError = TEXT("Replacement must reference a known skill and may not filter current/final skill or tags."); return false; }
		}
	}
	TSet<FString> OverrideKeys;
	for (const auto& Override : Overrides)
	{
		const FString OverrideKey = Key(Override.UnitTypeId, Override.SlotId);
		if (OverrideKeys.Contains(OverrideKey)
			|| !Configs.ContainsByPredicate([&](const auto& Row) { return Row.bDefault && Row.UnitTypeId == Override.UnitTypeId && Row.SlotId == Override.SlotId; }))
		{ OutError = FString::Printf(TEXT("Duplicate numeric override or unknown unit/slot %s."), *OverrideKey); return false; }
		const bool Active[] = {Override.bOverrideDamage, Override.bOverrideAttackRate, Override.bOverrideRange};
		const double Values[] = {Override.Damage, Override.AttackRatePerSecond, Override.RangeCentimeters};
		static const TCHAR* AttributeNames[] = {TEXT("Damage"), TEXT("AttackRatePerSecond"), TEXT("RangeCentimeters")};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (Active[Index] && !ValidAttribute(Values[Index], Index))
			{
				OutError = FString::Printf(TEXT("Rejected GM override %s %s=%.9g: finite nonnegative; damage<=1e9, rate<=30, range<=1e6. No clamp applied."),
					*OverrideKey, AttributeNames[Index], Values[Index]);
				return false;
			}
		}
		OverrideKeys.Add(OverrideKey);
	}
	// GUID ordering makes floating-point reduction independent of insertion/upsert order.
	TArray<const FGuLiSkillSource*> OrderedSources;
	for (const auto& Source : Sources) OrderedSources.Add(&Source);
	OrderedSources.Sort([](const FGuLiSkillSource& A, const FGuLiSkillSource& B)
	{ return A.SourceInstanceId.ToString() < B.SourceInstanceId.ToString(); });
	TArray<FGuLiResolvedSkillProfile> Result;
	for (const auto& Default : Configs)
	{
		if (!Default.bDefault || !SelectedSlots.Contains(FGuLiSkillSlotKey{Default.UnitTypeId, Default.SlotId})) continue;
		FName SelectedSkill = Default.SkillId;
		const auto* Selection = Loadout ? Loadout->FindByPredicate([&](const auto& Row)
			{ return Row.UnitTypeId == Default.UnitTypeId && Row.SlotId == Default.SlotId; }) : nullptr;
		if (Selection && !Selection->SkillId.IsNone()) SelectedSkill = Selection->SkillId;
		bool bUnlocked = Default.bInitiallyUnlocked;
		int32 HighestPriority = MIN_int32;
		bool bHasReplacement = false;
		TMap<int32, FName> ReplacementByPriority;
		for (const auto* Source : OrderedSources)
		{
			bUnlocked |= Source->Unlocks.ContainsByPredicate([&](const auto& Unlock)
				{ return Unlock.Target.MatchesUnitSlot(Default.UnitTypeId, Default.SlotId); });
			for (const auto& Replacement : Source->Replacements)
			{
				if (!Replacement.Target.MatchesUnitSlot(Default.UnitTypeId, Default.SlotId)) continue;
				if (const FName* Prior = ReplacementByPriority.Find(Replacement.Priority); Prior && *Prior != Replacement.SkillId)
				{ OutError = FString::Printf(TEXT("Conflicting replacements at priority %d for %s."), Replacement.Priority, *Key(Default.UnitTypeId, Default.SlotId)); return false; }
				ReplacementByPriority.Add(Replacement.Priority, Replacement.SkillId);
				// Validate hidden lower-priority alternatives too, before atomically accepting a source.
				if (!Configs.ContainsByPredicate([&](const auto& Row) { return Row.UnitTypeId == Default.UnitTypeId && Row.SlotId == Default.SlotId && Row.SkillId == Replacement.SkillId; }))
				{ OutError = TEXT("Replacement has no UnitSkills configuration for a targeted unit/slot."); return false; }
				if (!bHasReplacement || Replacement.Priority > HighestPriority)
				{ SelectedSkill = Replacement.SkillId; HighestPriority = Replacement.Priority; bHasReplacement = true; }
			}
		}
		const auto* Config = Configs.FindByPredicate([&](const auto& Row)
		{ return Row.UnitTypeId == Default.UnitTypeId && Row.SlotId == Default.SlotId && Row.SkillId == SelectedSkill; });
		const auto* Definition = FindDefinition(Definitions, SelectedSkill);
		check(Config && Definition);
		double Flat[3] = {0, 0, 0};
		double Factor[3] = {1, 1, 1};
		for (const auto* Source : OrderedSources)
		{
			double SourcePercent[3] = {0, 0, 0};
			for (const auto& Modifier : Source->Modifiers)
			{
				if (!Modifier.Target.MatchesUnitSlot(Default.UnitTypeId, Default.SlotId) || !Modifier.Target.MatchesFinalSkill(*Definition)) continue;
				const int32 Index = static_cast<int32>(Modifier.Attribute);
				if (Modifier.Operation == EGuLiSkillModifierOperation::AddFlat) Flat[Index] += Modifier.Magnitude;
				else SourcePercent[Index] += Modifier.Magnitude;
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (SourcePercent[Index] < -1.0) { OutError = TEXT("Combined percentage within one source is below -100%."); return false; }
				Factor[Index] *= 1.0 + SourcePercent[Index];
			}
		}
		double Values[3] = {(Config->Damage + Flat[0]) * Factor[0], (Config->AttackRatePerSecond + Flat[1]) * Factor[1], (Config->RangeCentimeters + Flat[2]) * Factor[2]};
		// Invalid normal state cannot be concealed behind a temporary GM override.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!ValidAttribute(Values[Index], Index))
			{
				static const TCHAR* AttributeNames[] = {TEXT("Damage"), TEXT("AttackRatePerSecond"), TEXT("RangeCentimeters")};
				OutError = FString::Printf(TEXT("Rejected %s skill=%s %s=%.9g: finite nonnegative; damage<=1e9, rate<=30, range<=1e6. No clamp applied."),
					*Key(Default.UnitTypeId, Default.SlotId), *SelectedSkill.ToString(), AttributeNames[Index], Values[Index]);
				return false;
			}
		}
		if (const auto* Override = Overrides.FindByPredicate([&](const auto& Row) { return Row.UnitTypeId == Default.UnitTypeId && Row.SlotId == Default.SlotId; }))
		{
			if (Override->bOverrideDamage) Values[0] = Override->Damage;
			if (Override->bOverrideAttackRate) Values[1] = Override->AttackRatePerSecond;
			if (Override->bOverrideRange) Values[2] = Override->RangeCentimeters;
		}
		auto& Profile = Result.AddDefaulted_GetRef();
		Profile.Team = Team; Profile.UnitTypeId = Default.UnitTypeId; Profile.SlotId = Default.SlotId;
		Profile.SkillId = SelectedSkill; Profile.ExecutorId = Definition->ExecutorId; Profile.Tags = Definition->Tags;
		Profile.bUnlocked = bUnlocked;
		Profile.TriggerMode = Config->TriggerMode;
		Profile.ProjectileSpeedCentimetersPerSecond = Config->ProjectileSpeedCentimetersPerSecond;
		Profile.ProjectileLifetimeSeconds = Config->ProjectileLifetimeSeconds;
		Profile.ProjectileSweepRadiusCentimeters = Config->ProjectileSweepRadiusCentimeters;
		Profile.bEquipped = bUnlocked && (!Selection || !Selection->SkillId.IsNone());
		Profile.Damage = static_cast<float>(Values[0]); Profile.AttackRatePerSecond = static_cast<float>(Values[1]); Profile.RangeCentimeters = static_cast<float>(Values[2]);
	}
	Result.Sort([](const auto& A, const auto& B) { return A.UnitTypeId != B.UnitTypeId ? A.UnitTypeId < B.UnitTypeId : A.SlotId.LexicalLess(B.SlotId); });
	OutProfiles = MoveTemp(Result);
	OutError.Reset();
	return true;
}
