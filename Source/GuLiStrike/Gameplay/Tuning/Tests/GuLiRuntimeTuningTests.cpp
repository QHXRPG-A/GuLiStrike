// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiRuntimeTuningRegistryTest,
	"GuLiStrike.RuntimeTuning.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiRuntimeTuningRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FGuLiRuntimeTuningRegistry Registry;
	const TArray<FGuLiRuntimeTuningEntryView> AllEntries = Registry.List();
	TestEqual(TEXT("The global GM whitelist contains five keys; skill tuning has its own scoped commands"), AllEntries.Num(), 5);
	for (int32 Index = 1; Index < AllEntries.Num(); ++Index)
	{
		TestTrue(
			TEXT("List is sorted by canonical key"),
			AllEntries[Index - 1].Key.LexicalLess(AllEntries[Index].Key));
	}

	FString BaselineError;
	TestTrue(
		TEXT("A valid DataTable baseline can replace the C++ fallback"),
		Registry.SetBaseline(
			FName(TEXT("soldier.move_speed_cm_s")),
			4200.0,
			EGuLiRuntimeTuningValueSource::DataTable,
			&BaselineError));
	const FGuLiRuntimeTuningResult Baseline = Registry.Get(TEXT("SOLDIER.MOVE_SPEED_CM_S"));
	TestTrue(TEXT("Get is case insensitive"), Baseline.bSuccess);
	TestEqual(TEXT("DataTable baseline is visible"), Baseline.Baseline, 4200.0);
	TestEqual(
		TEXT("Baseline source is preserved"),
		Baseline.Source,
		EGuLiRuntimeTuningValueSource::DataTable);

	const FGuLiRuntimeTuningResult Set = Registry.Set(
		TEXT("soldier.move_speed_cm_s"),
		TEXT("7200"));
	TestTrue(TEXT("A valid finite override is accepted"), Set.bSuccess);
	TestTrue(TEXT("A changed value is reported"), Set.bChanged);
	TestEqual(TEXT("Previous effective value is reported"), Set.PreviousEffective, 4200.0);
	TestEqual(TEXT("GM override becomes effective"), Set.Effective, 7200.0);
	TestEqual(
		TEXT("GM override source wins"),
		Set.Source,
		EGuLiRuntimeTuningValueSource::GMOverride);

	const FGuLiRuntimeTuningResult Reset = Registry.Reset(TEXT("soldier.move_speed_cm_s"));
	TestTrue(TEXT("Reset succeeds"), Reset.bSuccess);
	TestEqual(TEXT("Reset restores the DataTable baseline"), Reset.Effective, 4200.0);
	TestEqual(
		TEXT("Reset restores the baseline source"),
		Reset.Source,
		EGuLiRuntimeTuningValueSource::DataTable);

	TestFalse(
		TEXT("Unknown keys are rejected"),
		Registry.Set(TEXT("soldier.unknown"), TEXT("1")).bSuccess);
	TestFalse(
		TEXT("NaN is rejected"),
		Registry.Set(TEXT("soldier.max_health"), TEXT("nan")).bSuccess);
	TestFalse(
		TEXT("Infinity is rejected"),
		Registry.Set(TEXT("soldier.max_health"), TEXT("inf")).bSuccess);
	TestFalse(
		TEXT("Trailing junk is rejected"),
		Registry.Set(TEXT("soldier.defense"), TEXT("12junk")).bSuccess);
	TestFalse(
		TEXT("Out-of-range health is rejected instead of clamped"),
		Registry.Set(TEXT("soldier.max_health"), TEXT("1000000001")).bSuccess);
	TestFalse(
		TEXT("Movement speed beyond the Mass wire representation is rejected"),
		Registry.Set(TEXT("soldier.move_speed_cm_s"), TEXT("32768")).bSuccess);
	TestTrue(
		TEXT("Fractional health is accepted"),
		Registry.Set(TEXT("soldier.max_health"), TEXT("99.5")).bSuccess);
	TestTrue(TEXT("Health above the removed uint8 ceiling is accepted"),
		Registry.Set(TEXT("soldier.max_health"), TEXT("300.5")).bSuccess);
	TestTrue(TEXT("Positive maximum health below one is accepted"),
		Registry.Set(TEXT("soldier.max_health"), TEXT("0.5")).bSuccess);
	TestFalse(TEXT("Zero maximum health is rejected"),
		Registry.Set(TEXT("soldier.max_health"), TEXT("0")).bSuccess);
	for (const TCHAR* LegacyKey : {TEXT("soldier.attack_power"), TEXT("soldier.attack_range_cm")})
	{
		const FGuLiRuntimeTuningResult Legacy = Registry.Set(LegacyKey, TEXT("10"));
		TestFalse(TEXT("Legacy global skill overrides cannot overwrite per-type profiles"), Legacy.bSuccess);
		TestTrue(TEXT("Legacy command explains the replacement scope and command"),
			Legacy.Error.Contains(TEXT("gs.GM.Skill.Set <Red|Blue> <UnitTypeId>")));
	}
	FGuLiRuntimeTuningRegistry LayerRegistry;
	TestTrue(TEXT("An override equal to the default type still affects other types"),
		LayerRegistry.Set(TEXT("soldier.max_health"), TEXT("100")).bChanged);
	TestFalse(TEXT("Repeating the identical active override is a no-op"),
		LayerRegistry.Set(TEXT("soldier.max_health"), TEXT("100")).bChanged);
	TestTrue(TEXT("Reset restores each type's baseline even if default type's number is unchanged"),
		LayerRegistry.Reset(TEXT("soldier.max_health")).bChanged);
	TestFalse(TEXT("Reset without an override is a no-op"),
		LayerRegistry.Reset(TEXT("soldier.max_health")).bChanged);

	FGuLiRuntimeTuningRegistry OtherWorldRegistry;
	Registry.Set(TEXT("ship.max_speed_multiplier"), TEXT("2"));
	TestEqual(
		TEXT("A separate World registry retains its own value"),
		OtherWorldRegistry.Get(TEXT("ship.max_speed_multiplier")).Effective,
		1.0);

	TestFalse(
		TEXT("A pure client cannot mutate runtime tuning"),
		UGuLiRuntimeTuningSubsystem::IsMutationAllowedForNetMode(NM_Client));
	TestTrue(
		TEXT("Standalone can mutate runtime tuning"),
		UGuLiRuntimeTuningSubsystem::IsMutationAllowedForNetMode(NM_Standalone));
	TestTrue(
		TEXT("Listen server can mutate runtime tuning"),
		UGuLiRuntimeTuningSubsystem::IsMutationAllowedForNetMode(NM_ListenServer));
	TestTrue(
		TEXT("Dedicated server can mutate runtime tuning"),
		UGuLiRuntimeTuningSubsystem::IsMutationAllowedForNetMode(NM_DedicatedServer));

	const FProperty* ShipGMStateProperty = FindFProperty<FProperty>(
		AGuLiStrikeShip::StaticClass(),
		TEXT("GMRuntimeState"));
	TestNotNull(
		TEXT("Ship exposes one atomic GM runtime replication property"),
		ShipGMStateProperty);
	if (ShipGMStateProperty)
	{
		TestTrue(
			TEXT("Ship GM runtime state is replicated"),
			ShipGMStateProperty->HasAnyPropertyFlags(CPF_Net));
		TestTrue(
			TEXT("Ship GM runtime state has a RepNotify"),
			ShipGMStateProperty->HasAnyPropertyFlags(CPF_RepNotify));
		const FStructProperty* ShipGMStructProperty = CastField<FStructProperty>(ShipGMStateProperty);
		TestNotNull(
			TEXT("Ship GM runtime values and revision travel as one struct"),
			ShipGMStructProperty);
		if (ShipGMStructProperty)
		{
			TestTrue(
				TEXT("The replicated property uses the dedicated atomic GM state struct"),
				ShipGMStructProperty->Struct.Get()
					== FGuLiShipGMRuntimeReplicatedState::StaticStruct());
		}
	}
	TestTrue(
		TEXT("The native Ship class enables actor replication"),
		GetDefault<AGuLiStrikeShip>()->GetIsReplicated());

	TestEqual(
		TEXT("Living health keeps its ratio when maximum health doubles"),
		GuLiRuntimeTuning::ScaleHealthPreservingRatio(50u, 100u, 200u),
		100.0f);
	TestEqual(
		TEXT("Small surviving health retains its fractional ratio instead of rounding to one"),
		GuLiRuntimeTuning::ScaleHealthPreservingRatio(1u, 255u, 1u),
		1.0f / 255.0f);
	TestEqual(
		TEXT("A dead Soldier remains dead when maximum health changes"),
		GuLiRuntimeTuning::ScaleHealthPreservingRatio(0u, 100u, 200u),
		0.0f);
	TestEqual(TEXT("Fractional health above 255 preserves its ratio exactly"),
		GuLiRuntimeTuning::ScaleHealthPreservingRatio(150.25f, 300.5f, 601.0f), 300.5f);
	TestEqual(TEXT("Over-healed data cannot scale beyond the new maximum"),
		GuLiRuntimeTuning::ScaleHealthPreservingRatio(250.0f, 100.0f, 300.5f), 300.5f);
	TestEqual(TEXT("Non-finite health does not revive or propagate NaN"),
		GuLiRuntimeTuning::ScaleHealthPreservingRatio(std::numeric_limits<float>::quiet_NaN(), 100.0f, 300.5f), 0.0f);
	TestEqual(TEXT("Invalid new maximum does not propagate infinity"),
		GuLiRuntimeTuning::ScaleHealthPreservingRatio(50.0f, 100.0f, std::numeric_limits<float>::infinity()), 0.0f);
	TestEqual(
		TEXT("The 3600 cm/s baseline predicts exactly one quarter second"),
		GuLiRuntimeTuning::CalculatePredictionDistance(3600.0f, 0.25f, 450.0f),
		900.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
