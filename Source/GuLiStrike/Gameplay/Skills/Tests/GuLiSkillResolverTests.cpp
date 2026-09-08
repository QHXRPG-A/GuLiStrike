#include "Gameplay/Skills/GuLiSkillResolver.h"
#include "Gameplay/Skills/GuLiSkillTags.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace GuLiSkillTests
{
	void MakeCatalog(TArray<FGuLiSkillDefinition>& Definitions, TArray<FGuLiUnitSkillConfig>& Configs)
	{
		for (const TCHAR* Id : {TEXT("Strafe"), TEXT("StrafeTest"), TEXT("OtherTest")})
		{
			auto& Definition = Definitions.AddDefaulted_GetRef();
			Definition.SkillId = Id; Definition.ExecutorId = TEXT("DirectSingleTarget");
			Definition.Tags.AddTag(TAG_GuLi_BasicAttack);
			Definition.Tags.AddTag(Definition.SkillId == TEXT("Strafe") ? TAG_GuLi_MachineGun : TAG_GuLi_TestWeapon);
			for (uint16 Unit = 1; Unit <= 2; ++Unit)
			{
				auto& Config = Configs.AddDefaulted_GetRef();
				Config.UnitTypeId = Unit; Config.SkillId = Id; Config.bDefault = Definition.SkillId == TEXT("Strafe");
				Config.Damage = Unit * (Config.bDefault ? 100.0f : 300.0f);
				Config.AttackRatePerSecond = Config.bDefault ? 5.0f : 2.0f;
				Config.RangeCentimeters = 1000.0f;
			}
		}
	}
	FGuLiSkillSource PercentSource(uint32 Id, float Percent, bool bMachineGunOnly = false)
	{
		FGuLiSkillSource Source; Source.SourceInstanceId = FGuid(0, 0, 0, Id);
		auto& Modifier = Source.Modifiers.AddDefaulted_GetRef();
		Modifier.Target.UnitTypeIds = {1}; Modifier.Operation = EGuLiSkillModifierOperation::AddPercent;
		Modifier.Magnitude = Percent;
		if (bMachineGunOnly) Modifier.Target.RequiredTags.AddTag(TAG_GuLi_MachineGun);
		return Source;
	}
	FGuLiSkillSource Replacement(uint32 Id, FName Skill, int32 Priority)
	{
		FGuLiSkillSource Source; Source.SourceInstanceId = FGuid(0, 0, 0, Id);
		auto& Change = Source.Replacements.AddDefaulted_GetRef();
		Change.Target.UnitTypeIds = {1}; Change.SkillId = Skill; Change.Priority = Priority;
		return Source;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillMultiplicativeSourcesTest,
	"GuLiStrike.Skills.Resolver.MultiplicativeSourcesAndFinalFilters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillMultiplicativeSourcesTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillTests;
	TArray<FGuLiSkillDefinition> Definitions; TArray<FGuLiUnitSkillConfig> Configs;
	MakeCatalog(Definitions, Configs);
	TArray<FGuLiSkillSource> Sources = {PercentSource(1, .2f), PercentSource(2, .2f, true)};
	TArray<FGuLiResolvedSkillProfile> Profiles; FString Error;
	if (!TestTrue(TEXT("Valid sources resolve"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error))) return false;
	TestTrue(TEXT("Two independent +20% sources produce +44%, not +40%"), FMath::IsNearlyEqual(Profiles[0].Damage, 144.0f, .001f));
	TestEqual(TEXT("Untargeted unit type retains its own table damage"), Profiles[1].Damage, 200.0f);
	Sources.Swap(0, 1);
	TArray<FGuLiResolvedSkillProfile> Reordered;
	if (!TestTrue(TEXT("Source order is immaterial"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Reordered, Error))) return false;
	TestEqual(TEXT("GUID-ordered reduction is stable"), Reordered[0].Damage, Profiles[0].Damage);
	Sources.Add(Replacement(3, TEXT("StrafeTest"), 10));
	if (!TestTrue(TEXT("Replacing the slot is valid"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error))) return false;
	TestEqual(TEXT("Replacement selects its own per-unit base"), Profiles[0].SkillId, FName(TEXT("StrafeTest")));
	TestTrue(TEXT("Only the generic +20% survives the final skill tag filter"), FMath::IsNearlyEqual(Profiles[0].Damage, 360.0f, .001f));
	Sources.RemoveAt(2);
	if (!TestTrue(TEXT("Removing replacement re-evaluates retained sources"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error))) return false;
	TestTrue(TEXT("Machine-gun-specific modifier returns without being re-added"), FMath::IsNearlyEqual(Profiles[0].Damage, 144.0f, .001f));
	Sources.RemoveAll([](const auto& Source) { return Source.SourceInstanceId == FGuid(0, 0, 0, 1); });
	if (!TestTrue(TEXT("Removing one source preserves the other"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error))) return false;
	TestTrue(TEXT("Remaining independent source produces +20%"), FMath::IsNearlyEqual(Profiles[0].Damage, 120.0f, .001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillReplacementTransactionTest,
	"GuLiStrike.Skills.Resolver.ReplacementPriorityAndAtomicFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillReplacementTransactionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillTests;
	TArray<FGuLiSkillDefinition> Definitions; TArray<FGuLiUnitSkillConfig> Configs;
	MakeCatalog(Definitions, Configs);
	TArray<FGuLiSkillSource> Sources = {Replacement(1, TEXT("StrafeTest"), 10), Replacement(2, TEXT("OtherTest"), 20)};
	TArray<FGuLiResolvedSkillProfile> Profiles; FString Error;
	if (!TestTrue(TEXT("Layered replacements resolve"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error))) return false;
	TestEqual(TEXT("Highest priority wins"), Profiles[0].SkillId, FName(TEXT("OtherTest")));
	const auto Prior = Profiles[0];
	Sources.Add(Replacement(3, TEXT("Strafe"), 10));
	TestFalse(TEXT("Conflicting lower priority is rejected even while hidden"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error));
	if (!TestEqual(TEXT("Rejected transaction preserves output count"), Profiles.Num(), 2)) return false;
	TestTrue(TEXT("Failed transaction does not overwrite caller output"), Profiles[0].HasSameConfiguration(Prior));
	Sources.RemoveAt(2);
	Sources.RemoveAt(1);
	if (!TestTrue(TEXT("Removing high-priority source restores the retained lower source"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error))) return false;
	TestEqual(TEXT("Lower replacement restored"), Profiles[0].SkillId, FName(TEXT("StrafeTest")));
	Sources.Add(Replacement(4, TEXT("StrafeTest"), 10));
	if (!TestTrue(TEXT("Same priority and same target is not a conflict"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error))) return false;
	Sources[1].Replacements[0].Target.RequiredTags.AddTag(TAG_GuLi_MachineGun);
	TestFalse(TEXT("No replacement chains based on mutable current-skill tags"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, Sources, {}, Profiles, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillValidationAndOverridesTest,
	"GuLiStrike.Skills.Resolver.ValidationAndFinalOverrides", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillValidationAndOverridesTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillTests;
	TArray<FGuLiSkillDefinition> Definitions; TArray<FGuLiUnitSkillConfig> Configs;
	MakeCatalog(Definitions, Configs);
	TArray<FGuLiResolvedSkillProfile> Profiles; FString Error;
	FGuLiSkillNumericOverride Override; Override.UnitTypeId = 1;
	Override.bOverrideDamage = true; Override.Damage = 7.25f;
	Override.bOverrideAttackRate = true; Override.AttackRatePerSecond = 0.0f;
	if (!TestTrue(TEXT("Final GM override permits fractional damage and zero-rate stop"), FGuLiSkillResolver::Resolve(EGuLiTeam::Blue, Definitions, Configs, {PercentSource(1, .2f)}, {Override}, Profiles, Error))) return false;
	TestEqual(TEXT("GM override applies after source multiplication"), Profiles[0].Damage, 7.25f);
	TestEqual(TEXT("Zero rate preserved without silent clamp"), Profiles[0].AttackRatePerSecond, 0.0f);
	TestEqual(TEXT("Team comes from the resolver's authority context"), Profiles[0].Team, EGuLiTeam::Blue);
	Override.AttackRatePerSecond = 30.01f;
	TestFalse(TEXT("GM cannot silently exceed the fixed-step fire-rate contract"), FGuLiSkillResolver::Resolve(EGuLiTeam::Blue, Definitions, Configs, {}, {Override}, Profiles, Error));
	Override.AttackRatePerSecond = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN GM values are rejected"), FGuLiSkillResolver::Resolve(EGuLiTeam::Blue, Definitions, Configs, {}, {Override}, Profiles, Error));
	auto InvalidSource = PercentSource(1, std::numeric_limits<float>::infinity());
	TestFalse(TEXT("Infinite modifiers are rejected"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {InvalidSource}, {}, Profiles, Error));
	InvalidSource = PercentSource(1, .2f); InvalidSource.Modifiers[0].Target.UnitTypeIds = {999};
	TestFalse(TEXT("Unknown unit cannot silently match nothing"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {InvalidSource}, {}, Profiles, Error));
	const auto Source = PercentSource(1, .2f);
	TestFalse(TEXT("Duplicate source IDs cannot accidentally double-apply a source"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {Source, Source}, {}, Profiles, Error));
	Configs[0].AttackRatePerSecond = 31;
	TestFalse(TEXT("Table rates have the same bound as GM and resolved rates"), FGuLiSkillResolver::ValidateCatalog(Definitions, Configs, Error));
	Configs[0].AttackRatePerSecond = 0; Configs[0].RangeCentimeters = 0;
	TestTrue(TEXT("Authored zero rate and range are accepted as stop-fire values"), FGuLiSkillResolver::ValidateCatalog(Definitions, Configs, Error));
	const FGuLiUnitSkillConfig Duplicate = Configs[0];
	Configs.Add(Duplicate);
	TestFalse(TEXT("Duplicate unit/slot/skill row is rejected"), FGuLiSkillResolver::ValidateCatalog(Definitions, Configs, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillDirtyTargetUnionTest,
	"GuLiStrike.Skills.Resolver.DirtyTargetUnionAndSelectedResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillDirtyTargetUnionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillTests;
	TArray<FGuLiSkillDefinition> Definitions; TArray<FGuLiUnitSkillConfig> Configs;
	MakeCatalog(Definitions, Configs);
	FGuLiUnitSkillConfig Secondary = Configs[0]; Secondary.SlotId = TEXT("Secondary");
	Configs.Add(Secondary);
	const auto OldSource = PercentSource(1, .2f);
	auto UpdatedSource = OldSource; UpdatedSource.Modifiers[0].Target.UnitTypeIds = {2};
	TArray<FGuLiSkillSlotKey> Dirty;
	FGuLiSkillResolver::GatherAffectedSlots(Configs, OldSource, Dirty);
	TestEqual(TEXT("One targeted source produces one dirty slot"), Dirty.Num(), 1);
	TArray<FGuLiResolvedSkillProfile> Profiles; FString Error;
	if (!TestTrue(TEXT("Only the requested slot is resolved"), FGuLiSkillResolver::ResolveSelected(EGuLiTeam::Red, Definitions, Configs, {OldSource}, {}, Dirty, Profiles, Error))) return false;
	TestEqual(TEXT("Neither other unit nor second slot was numerically recomputed"), Profiles.Num(), 1);
	TestEqual(TEXT("Correct selected slot"), Profiles[0].SlotId, FName(TEXT("BasicAttack")));
	FGuLiSkillResolver::GatherAffectedSlots(Configs, UpdatedSource, Dirty);
	TestEqual(TEXT("Retargeting keeps old and new unit keys in the dirty union"), Dirty.Num(), 2);
	if (!TestTrue(TEXT("Updated source resolves both ends of retargeting"), FGuLiSkillResolver::ResolveSelected(EGuLiTeam::Red, Definitions, Configs, {UpdatedSource}, {}, Dirty, Profiles, Error))) return false;
	TestEqual(TEXT("Old target returns to its base after source retargeting"), Profiles[0].Damage, 100.0f);
	TestTrue(TEXT("New target receives the source"), FMath::IsNearlyEqual(Profiles[1].Damage, 240.0f, .001f));
	UpdatedSource.Modifiers[0].Target.UnitTypeIds.Reset();
	UpdatedSource.Modifiers[0].Target.RequiredTags.AddTag(TAG_GuLi_TestWeapon);
	Dirty.Reset();
	FGuLiSkillResolver::GatherAffectedSlots(Configs, UpdatedSource, Dirty);
	TestEqual(TEXT("Wildcard includes both units but not another slot, regardless of currently inactive final-skill filter"), Dirty.Num(), 2);
	FGuLiSkillNumericOverride Override; Override.UnitTypeId = 2; Override.bOverrideDamage = true; Override.Damage = 42.25f;
	if (!TestTrue(TEXT("Override can resolve just its exact target"), FGuLiSkillResolver::ResolveSelected(EGuLiTeam::Red, Definitions, Configs, {}, {Override}, {{2, FName(TEXT("BasicAttack"))}}, Profiles, Error))) return false;
	TestEqual(TEXT("One override computes one slot"), Profiles.Num(), 1);
	TestEqual(TEXT("Override target is unit two"), Profiles[0].UnitTypeId, static_cast<uint16>(2));
	TestEqual(TEXT("Override value is exact"), Profiles[0].Damage, 42.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillDirtyTransactionTest,
	"GuLiStrike.Skills.Resolver.DirtyMultiTargetAtomicFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillDirtyTransactionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillTests;
	TArray<FGuLiSkillDefinition> Definitions; TArray<FGuLiUnitSkillConfig> Configs;
	MakeCatalog(Definitions, Configs);
	TArray<FGuLiResolvedSkillProfile> Profiles; FString Error;
	if (!TestTrue(TEXT("Baseline is valid"), FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {}, {}, Profiles, Error))) return false;
	const auto Baseline = Profiles;
	const auto ValidFirstUnitChange = PercentSource(1, .2f);
	auto FirstReplacement = Replacement(2, TEXT("StrafeTest"), 10); FirstReplacement.Replacements[0].Target.UnitTypeIds = {2};
	auto ConflictingReplacement = Replacement(3, TEXT("Strafe"), 10); ConflictingReplacement.Replacements[0].Target.UnitTypeIds = {2};
	auto CoveringReplacement = Replacement(4, TEXT("OtherTest"), 20); CoveringReplacement.Replacements[0].Target.UnitTypeIds = {2};
	const TArray<FGuLiSkillSource> ConflictingSources = {ValidFirstUnitChange, FirstReplacement, ConflictingReplacement, CoveringReplacement};
	TArray<FGuLiSkillSlotKey> Dirty;
	for (const auto& Source : ConflictingSources) FGuLiSkillResolver::GatherAffectedSlots(Configs, Source, Dirty);
	TestFalse(TEXT("A hidden conflict in the second dirty slot rejects the complete candidate"),
		FGuLiSkillResolver::ResolveSelected(EGuLiTeam::Red, Definitions, Configs, ConflictingSources, {}, Dirty, Profiles, Error));
	if (!TestEqual(TEXT("Atomic conflict preserves all output entries"), Profiles.Num(), Baseline.Num())) return false;
	TestTrue(TEXT("Successful first-slot work was not partly committed"), Profiles[0].HasSameConfiguration(Baseline[0]));
	TestTrue(TEXT("Second slot remains unchanged too"), Profiles[1].HasSameConfiguration(Baseline[1]));
	auto InvalidRate = PercentSource(5, 6.0f); InvalidRate.Modifiers[0].Target.UnitTypeIds = {2};
	InvalidRate.Modifiers[0].Attribute = EGuLiSkillAttribute::AttackRate;
	TestFalse(TEXT("Numeric failure in a later dirty slot also rejects all earlier computed profiles"),
		FGuLiSkillResolver::ResolveSelected(EGuLiTeam::Red, Definitions, Configs, {ValidFirstUnitChange, InvalidRate}, {}, Dirty, Profiles, Error));
	if (!TestEqual(TEXT("Atomic numeric rejection preserves all output entries"), Profiles.Num(), Baseline.Num())) return false;
	TestTrue(TEXT("No partial numerical commit"), Profiles[0].HasSameConfiguration(Baseline[0]) && Profiles[1].HasSameConfiguration(Baseline[1]));
	TestTrue(TEXT("Rejection identifies the attribute and confirms no clamp"), Error.Contains(TEXT("AttackRatePerSecond")) && Error.Contains(TEXT("No clamp")));
	InvalidRate.Modifiers[0].Target.UnitTypeIds = {999};
	TestFalse(TEXT("Full candidate structure is checked even outside selected calculation keys"),
		FGuLiSkillResolver::ResolveSelected(EGuLiTeam::Red, Definitions, Configs, {ValidFirstUnitChange, InvalidRate}, {}, {{1, FName(TEXT("BasicAttack"))}}, Profiles, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWeaponSlotEquipmentIsolationTest,
	"GuLiStrike.Skills.Resolver.WeaponSlotEquipmentIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWeaponSlotEquipmentIsolationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiSkillTests;
	TArray<FGuLiSkillDefinition> Definitions;
	TArray<FGuLiUnitSkillConfig> Configs;
	MakeCatalog(Definitions, Configs);

	// Reuse the same registered behaviors in a second WM01 slot. This is an in-memory
	// catalog only; production data and assets are not changed by the test.
	const TArray<FGuLiUnitSkillConfig> OriginalConfigs = Configs;
	for (const FGuLiUnitSkillConfig& Config : OriginalConfigs)
	{
		if (Config.UnitTypeId != 2u) continue;
		FGuLiUnitSkillConfig Secondary = Config;
		Secondary.SlotId = TEXT("SecondaryWeapon");
		Secondary.bInitiallyUnlocked = false;
		Configs.Add(MoveTemp(Secondary));
	}

	FGuLiSkillSource Reward;
	Reward.SourceInstanceId = FGuid(0, 0, 0, 50);
	Reward.DebugLabel = TEXT("UnlockWM01Secondary");
	auto& Unlock = Reward.Unlocks.AddDefaulted_GetRef();
	Unlock.Target.UnitTypeIds = {2};
	Unlock.Target.SlotId = TEXT("SecondaryWeapon");
	auto& WeaponOnlyModifier = Reward.Modifiers.AddDefaulted_GetRef();
	WeaponOnlyModifier.Target.UnitTypeIds = {2};
	WeaponOnlyModifier.Target.SlotId = TEXT("SecondaryWeapon");
	WeaponOnlyModifier.Target.RequiredSkillId = TEXT("StrafeTest");
	WeaponOnlyModifier.Attribute = EGuLiSkillAttribute::Damage;
	WeaponOnlyModifier.Operation = EGuLiSkillModifierOperation::AddFlat;
	WeaponOnlyModifier.Magnitude = 5.0f;

	TArray<FGuLiSkillLoadoutSelection> Loadout;
	Loadout.Add({1u, TEXT("BasicAttack"), TEXT("StrafeTest")});
	Loadout.Add({2u, TEXT("SecondaryWeapon"), TEXT("StrafeTest")});
	TArray<FGuLiResolvedSkillProfile> Profiles;
	FString Error;
	if (!TestTrue(TEXT("A reward can unlock and equip WM01's second slot"),
		FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {Reward}, {}, Profiles, Error, &Loadout)))
	{
		AddError(Error);
		return false;
	}
	const auto FindProfile = [&Profiles](const uint16 UnitTypeId, const FName SlotId)
	{
		return Profiles.FindByPredicate([&](const FGuLiResolvedSkillProfile& Profile)
			{ return Profile.UnitTypeId == UnitTypeId && Profile.SlotId == SlotId; });
	};
	const FGuLiResolvedSkillProfile* SoldierA = FindProfile(1u, TEXT("BasicAttack"));
	const FGuLiResolvedSkillProfile* WM01Secondary = FindProfile(2u, TEXT("SecondaryWeapon"));
	if (!TestNotNull(TEXT("Soldier A's selected binding resolves"), SoldierA)
		|| !TestNotNull(TEXT("WM01's secondary binding resolves"), WM01Secondary)) return false;
	TestTrue(TEXT("The unlocked WM01 slot is equipped"), WM01Secondary->bUnlocked && WM01Secondary->bEquipped);
	TestEqual(TEXT("The weapon-limited reward modifies only its exact binding"), WM01Secondary->Damage, 605.0f);
	TestEqual(TEXT("Sharing one SkillId across bindings does not modify Soldier A"), SoldierA->Damage, 300.0f);

	if (!TestTrue(TEXT("Removing the reward recomputes the retained equipment choice"),
		FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {}, {}, Profiles, Error, &Loadout))) return false;
	WM01Secondary = FindProfile(2u, TEXT("SecondaryWeapon"));
	TestTrue(TEXT("Removing the unlock locks and disables only WM01's second slot"),
		WM01Secondary && !WM01Secondary->bUnlocked && !WM01Secondary->bEquipped);
	SoldierA = FindProfile(1u, TEXT("BasicAttack"));
	TestTrue(TEXT("The other unit binding remains equipped"), SoldierA && SoldierA->bEquipped);

	Loadout[1].SkillId = NAME_None;
	if (!TestTrue(TEXT("An unlocked slot may be intentionally unequipped"),
		FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {Reward}, {}, Profiles, Error, &Loadout))) return false;
	WM01Secondary = FindProfile(2u, TEXT("SecondaryWeapon"));
	TestTrue(TEXT("Unequipping preserves the unlocked stable slot"),
		WM01Secondary && WM01Secondary->bUnlocked && !WM01Secondary->bEquipped);

	const TArray<FGuLiResolvedSkillProfile> PriorProfiles = Profiles;
	Loadout.Add({2u, TEXT("SecondaryWeapon"), TEXT("OtherTest")});
	TestFalse(TEXT("Duplicate equipment for one binding is rejected"),
		FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {Reward}, {}, Profiles, Error, &Loadout));
	TestEqual(TEXT("Rejected equipment preserves the caller's prior snapshot"), Profiles.Num(), PriorProfiles.Num());
	Loadout.Pop();
	Loadout[1].SkillId = TEXT("NotCompatible");
	TestFalse(TEXT("A candidate absent from the unit/slot catalog is rejected"),
		FGuLiSkillResolver::Resolve(EGuLiTeam::Red, Definitions, Configs, {Reward}, {}, Profiles, Error, &Loadout));

	TArray<FGuLiUnitSkillConfig> TooManySlots;
	for (int32 SlotIndex = 0; SlotIndex < 33; ++SlotIndex)
	{
		FGuLiUnitSkillConfig& Config = TooManySlots.AddDefaulted_GetRef();
		Config.UnitTypeId = 2u;
		Config.SlotId = FName(*FString::Printf(TEXT("Slot%02d"), SlotIndex));
		Config.SkillId = TEXT("Strafe");
		Config.bDefault = true;
		Config.Damage = 1.0f;
		Config.AttackRatePerSecond = 1.0f;
		Config.RangeCentimeters = 1000.0f;
	}
	TestFalse(TEXT("A unit type cannot exceed the runtime channel safety bound"),
		FGuLiSkillResolver::ValidateCatalog(Definitions, TooManySlots, Error));
	return true;
}
#endif
