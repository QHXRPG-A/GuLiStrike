// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/Presentation/GuLiCommanderPresentationPerformanceSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderPresentationPerformanceRegistryTest,
	"GuLiStrike.Commander.Presentation.PerformanceRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderPresentationPerformanceRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGuLiCommanderPresentationPerformanceSettings Defaults =
		FGuLiCommanderPresentationPerformanceSettings::CompiledDefaults();
	TestEqual(TEXT("Unit meshes hard-cull at 1000 metres by default"),
		Defaults.UnitCullDistanceCentimeters, 100000);
	TestEqual(TEXT("Rings do not distance-cull by default"),
		Defaults.RingCullDistanceCentimeters, 0);
	TestFalse(TEXT("Unit shadows default off"), Defaults.bUnitCastShadow);
	TestFalse(TEXT("Unit distance-field lighting defaults off"),
		Defaults.bUnitAffectDistanceFieldLighting);
	TestFalse(TEXT("Unit dynamic indirect lighting defaults off"),
		Defaults.bUnitAffectDynamicIndirectLighting);
	TestFalse(TEXT("Unit ray tracing visibility defaults off"),
		Defaults.bUnitVisibleInRayTracing);

	FGuLiCommanderPresentationPerformanceRegistry Registry;
	FGuLiCommanderPresentationRawConfigSettings MissingConfig;
	TArray<FString> ConfigErrors;
	Registry.InitializeFromRawConfig(MissingConfig, ConfigErrors);
	TestTrue(TEXT("Missing Config keys are not errors"), ConfigErrors.IsEmpty());
	TestEqual(TEXT("A missing Unit key retains the C++ source"),
		Registry.Get(TEXT("unit_cull_distance_cm")).Source,
		EGuLiCommanderPresentationSettingSource::CppDefault);
	TestEqual(TEXT("A missing Boolean key retains the C++ source"),
		Registry.Get(TEXT("unit.cast_shadow")).Source,
		EGuLiCommanderPresentationSettingSource::CppDefault);

	FGuLiCommanderPresentationRawConfigSettings MalformedConfig;
	MalformedConfig.UnitCullDistanceCentimeters = FString(TEXT("not_an_int"));
	MalformedConfig.RingCullDistanceCentimeters = FString(TEXT("-2"));
	MalformedConfig.bUnitCastShadow = FString(TEXT("true"));
	MalformedConfig.bUnitAffectDynamicIndirectLighting = FString(TEXT("maybe"));
	Registry.InitializeFromRawConfig(MalformedConfig, ConfigErrors);
	TestEqual(TEXT("Malformed and out-of-range Config values are diagnosed"),
		ConfigErrors.Num(), 3);
	TestEqual(TEXT("Invalid Unit Config falls back without clamping"),
		Registry.GetBaselineSettings().UnitCullDistanceCentimeters, 100000);
	TestEqual(TEXT("Invalid Ring Config falls back without clamping"),
		Registry.GetBaselineSettings().RingCullDistanceCentimeters, 0);
	TestTrue(TEXT("An independently valid Config flag remains effective"),
		Registry.GetBaselineSettings().bUnitCastShadow);
	TestEqual(TEXT("Invalid Config reports the C++ source"),
		Registry.Get(TEXT("unit_cull_distance_cm")).Source,
		EGuLiCommanderPresentationSettingSource::CppDefault);
	TestEqual(TEXT("Valid Config reports the Config source"),
		Registry.Get(TEXT("unit.cast_shadow")).Source,
		EGuLiCommanderPresentationSettingSource::Config);
	TestEqual(TEXT("Malformed Boolean Config reports the C++ source"),
		Registry.Get(TEXT("unit.affect_dynamic_indirect_lighting")).Source,
		EGuLiCommanderPresentationSettingSource::CppDefault);

	FGuLiCommanderPresentationRawConfigSettings ExplicitDefaultConfig;
	ExplicitDefaultConfig.UnitCullDistanceCentimeters = FString(TEXT("100000"));
	ExplicitDefaultConfig.RingCullDistanceCentimeters = FString(TEXT("0"));
	ExplicitDefaultConfig.bUnitCastShadow = FString(TEXT("false"));
	Registry.InitializeFromRawConfig(ExplicitDefaultConfig, ConfigErrors);
	TestTrue(TEXT("Explicit valid defaults produce no Config errors"), ConfigErrors.IsEmpty());
	TestEqual(TEXT("An explicitly configured default is still sourced from Config"),
		Registry.Get(TEXT("unit_cull_distance_cm")).Source,
		EGuLiCommanderPresentationSettingSource::Config);
	TestEqual(TEXT("An explicitly configured Boolean default is sourced from Config"),
		Registry.Get(TEXT("unit.cast_shadow")).Source,
		EGuLiCommanderPresentationSettingSource::Config);

	const TArray<FGuLiCommanderPresentationSettingView> AllEntries = Registry.List();
	TestEqual(TEXT("The local whitelist contains exactly six keys"), AllEntries.Num(), 6);
	for (int32 Index = 1; Index < AllEntries.Num(); ++Index)
	{
		TestTrue(
			TEXT("List output is sorted by canonical key"),
			AllEntries[Index - 1].Key.LexicalLess(AllEntries[Index].Key));
	}
	TestEqual(TEXT("Prefix filtering is case insensitive"),
		Registry.List(TEXT("UNIT.")).Num(), 4);

	const FGuLiCommanderPresentationSettingResult SetCull =
		Registry.Set(TEXT("UNIT_CULL_DISTANCE_CM"), TEXT("250000"));
	TestTrue(TEXT("A valid local cull override is accepted"), SetCull.bSuccess);
	TestEqual(TEXT("The prior effective value is preserved in the result"),
		SetCull.PreviousEffective, FString(TEXT("100000")));
	TestEqual(TEXT("The local cull override becomes effective"),
		SetCull.Effective, FString(TEXT("250000")));
	TestEqual(TEXT("The override source is explicit"), SetCull.Source,
		EGuLiCommanderPresentationSettingSource::LocalOverride);

	TestFalse(TEXT("Missing numeric text is rejected"),
		Registry.Set(TEXT("unit_cull_distance_cm"), TEXT("")).bSuccess);
	TestFalse(TEXT("NaN is rejected"),
		Registry.Set(TEXT("unit_cull_distance_cm"), TEXT("nan")).bSuccess);
	TestFalse(TEXT("Infinity is rejected"),
		Registry.Set(TEXT("unit_cull_distance_cm"), TEXT("inf")).bSuccess);
	TestFalse(TEXT("Fractional distances are rejected"),
		Registry.Set(TEXT("unit_cull_distance_cm"), TEXT("100.5")).bSuccess);
	TestFalse(TEXT("Trailing characters are rejected"),
		Registry.Set(TEXT("unit_cull_distance_cm"), TEXT("100cm")).bSuccess);
	TestFalse(TEXT("Whitespace is rejected rather than silently trimmed"),
		Registry.Set(TEXT("unit_cull_distance_cm"), TEXT(" 100")).bSuccess);
	TestFalse(TEXT("Zero is invalid for Unit culling"),
		Registry.Set(TEXT("unit_cull_distance_cm"), TEXT("0")).bSuccess);
	TestTrue(TEXT("Zero explicitly disables Ring culling"),
		Registry.Set(TEXT("ring_cull_distance_cm"), TEXT("0")).bSuccess);
	TestFalse(TEXT("Out-of-range Ring culling is rejected"),
		Registry.Set(TEXT("ring_cull_distance_cm"), TEXT("10000001")).bSuccess);
	TestFalse(TEXT("Loose Boolean aliases are rejected"),
		Registry.Set(TEXT("unit.cast_shadow"), TEXT("on")).bSuccess);
	TestTrue(TEXT("Strict Boolean text is accepted"),
		Registry.Set(TEXT("unit.cast_shadow"), TEXT("false")).bSuccess);
	TestFalse(TEXT("Unknown keys are rejected"),
		Registry.Set(TEXT("unit.unknown"), TEXT("false")).bSuccess);

	const TArray<FGuLiCommanderPresentationSettingResult> ResetCull =
		Registry.Reset(TEXT("unit_cull_distance_cm"));
	TestEqual(TEXT("One-key Reset returns one structured result"), ResetCull.Num(), 1);
	TestTrue(TEXT("One-key Reset succeeds"), ResetCull[0].bSuccess);
	TestEqual(TEXT("Reset restores the validated baseline"),
		ResetCull[0].Effective, FString(TEXT("100000")));
	TestEqual(TEXT("Reset restores the baseline source"), ResetCull[0].Source,
		EGuLiCommanderPresentationSettingSource::Config);
	TestEqual(TEXT("Reset all covers all six keys"), Registry.Reset(TEXT("all")).Num(), 6);

	FGuLiCommanderPresentationPerformanceRegistry OtherWorldRegistry;
	Registry.Set(TEXT("unit.visible_in_ray_tracing"), TEXT("true"));
	TestEqual(TEXT("A second World registry remains isolated"),
		OtherWorldRegistry.Get(TEXT("unit.visible_in_ray_tracing")).Effective,
		FString(TEXT("false")));

	const AGuLiCommanderPresentationActor* ActorCDO =
		GetDefault<AGuLiCommanderPresentationActor>();
	int32 StartCullDistance = INDEX_NONE;
	int32 EndCullDistance = INDEX_NONE;
	ActorCDO->GetUnitInstances()->GetCullDistances(StartCullDistance, EndCullDistance);
	TestEqual(TEXT("The native Unit ISM starts its hard cull at the default boundary"),
		StartCullDistance, 100000);
	TestEqual(TEXT("The native Unit ISM ends its hard cull at the same boundary"),
		EndCullDistance, 100000);
	ActorCDO->GetRingInstances()->GetCullDistances(StartCullDistance, EndCullDistance);
	TestEqual(TEXT("The native Ring ISM has no start cull"), StartCullDistance, 0);
	TestEqual(TEXT("The native Ring ISM has no end cull"), EndCullDistance, 0);
	TestFalse(TEXT("The native Unit ISM does not cast shadows"),
		ActorCDO->GetUnitInstances()->CastShadow != 0);
	TestFalse(TEXT("The native Ring ISM does not cast shadows"),
		ActorCDO->GetRingInstances()->CastShadow != 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderPresentationConsoleWorldIsolationTest,
	"GuLiStrike.Commander.Presentation.ConsoleWorldIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderPresentationConsoleWorldIsolationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* FirstWorld = UWorld::CreateWorld(EWorldType::Game, false);
	UWorld* SecondWorld = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("The first isolated command World exists"), FirstWorld);
	TestNotNull(TEXT("The second isolated command World exists"), SecondWorld);
	if (!FirstWorld || !SecondWorld)
	{
		if (FirstWorld)
		{
			FirstWorld->DestroyWorld(false);
		}
		if (SecondWorld)
		{
			SecondWorld->DestroyWorld(false);
		}
		return false;
	}

	AGuLiCommanderPresentationActor* FirstActor =
		FirstWorld->SpawnActor<AGuLiCommanderPresentationActor>();
	AGuLiCommanderPresentationActor* SecondActor =
		SecondWorld->SpawnActor<AGuLiCommanderPresentationActor>();
	TestNotNull(TEXT("The first World owns a presentation actor"), FirstActor);
	TestNotNull(TEXT("The second World owns a presentation actor"), SecondActor);

	FOutputDeviceNull OutputDevice;
	const auto Execute = [this, &OutputDevice](
		const TCHAR* CommandName,
		const TArray<FString>& Args,
		UWorld* World)
	{
		IConsoleObject* ConsoleObject =
			IConsoleManager::Get().FindConsoleObject(CommandName);
		IConsoleCommand* ConsoleCommand = ConsoleObject ? ConsoleObject->AsCommand() : nullptr;
		TestNotNull(*FString::Printf(TEXT("%s is registered"), CommandName), ConsoleCommand);
		return ConsoleCommand && ConsoleCommand->Execute(Args, World, OutputDevice);
	};

	AddExpectedMessagePlain(
		TEXT("Usage: gs.Commander.Presentation.List [prefix]"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		1);
	AddExpectedMessagePlain(
		TEXT("Usage: gs.Commander.Presentation.Get <key>"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		2);
	AddExpectedMessagePlain(
		TEXT("Usage: gs.Commander.Presentation.Set <key> <value>"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		2);
	AddExpectedMessagePlain(
		TEXT("Usage: gs.Commander.Presentation.Reset <key|all>"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		2);

	TestTrue(TEXT("List rejects extra arguments"), Execute(
		TEXT("gs.Commander.Presentation.List"),
		{TEXT("unit"), TEXT("extra")},
		FirstWorld));
	TestTrue(TEXT("Get rejects missing arguments"), Execute(
		TEXT("gs.Commander.Presentation.Get"), {}, FirstWorld));
	TestTrue(TEXT("Get rejects extra arguments"), Execute(
		TEXT("gs.Commander.Presentation.Get"),
		{TEXT("unit_cull_distance_cm"), TEXT("extra")},
		FirstWorld));
	TestTrue(TEXT("Set rejects missing arguments"), Execute(
		TEXT("gs.Commander.Presentation.Set"),
		{TEXT("unit_cull_distance_cm")},
		FirstWorld));
	TestTrue(TEXT("Set rejects extra arguments"), Execute(
		TEXT("gs.Commander.Presentation.Set"),
		{TEXT("unit_cull_distance_cm"), TEXT("250000"), TEXT("extra")},
		FirstWorld));
	TestTrue(TEXT("Reset rejects missing arguments"), Execute(
		TEXT("gs.Commander.Presentation.Reset"), {}, FirstWorld));
	TestTrue(TEXT("Reset rejects extra arguments"), Execute(
		TEXT("gs.Commander.Presentation.Reset"),
		{TEXT("all"), TEXT("extra")},
		FirstWorld));

	if (FirstActor && SecondActor)
	{
		TestTrue(TEXT("A valid Set command executes in the first World"), Execute(
			TEXT("gs.Commander.Presentation.Set"),
			{TEXT("unit_cull_distance_cm"), TEXT("250000")},
			FirstWorld));
		TestEqual(TEXT("The first World receives its local override"),
			FirstActor->GetEffectivePresentationPerformanceSettings()
				.UnitCullDistanceCentimeters,
			250000);
		TestEqual(TEXT("The second World does not receive the first World's override"),
			SecondActor->GetEffectivePresentationPerformanceSettings()
				.UnitCullDistanceCentimeters,
			100000);

		int32 StartCullDistance = INDEX_NONE;
		int32 EndCullDistance = INDEX_NONE;
		FirstActor->GetUnitInstances()->GetCullDistances(
			StartCullDistance,
			EndCullDistance);
		TestEqual(TEXT("The first World's component applies the override"),
			StartCullDistance, 250000);
		TestEqual(TEXT("The hard-cull boundary remains equal"),
			EndCullDistance, 250000);

		TestTrue(TEXT("Reset all executes in the first World"), Execute(
			TEXT("gs.Commander.Presentation.Reset"), {TEXT("all")}, FirstWorld));
		TestEqual(TEXT("Reset restores only the first World's baseline"),
			FirstActor->GetEffectivePresentationPerformanceSettings()
				.UnitCullDistanceCentimeters,
			100000);
		TestEqual(TEXT("The second World remains unchanged after the first resets"),
			SecondActor->GetEffectivePresentationPerformanceSettings()
				.UnitCullDistanceCentimeters,
			100000);
	}

	FirstWorld->DestroyWorld(false);
	SecondWorld->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
