// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiSoldierCombat.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace GuLiSoldierCombatTests
{
	struct FFixture
	{
		TArray<FGuLiResolvedSkillProfile> Profiles;
		TArray<FGuLiSoldierAttackState> States;
		TArray<FGuLiCombatSample> Samples;
		TMap<uint32, int32> Indices;
		TMap<FIntPoint, TArray<int32>> Grid;
		TArray<FGuLiCombatDamageEvent> Events;
		FGuLiCombatExecutorRegistry Executors;
		explicit FFixture(const int32 Count)
		{
			Profiles.SetNum(Count); States.SetNum(Count); Samples.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Profiles[Index].SkillId = TEXT("Strafe");
				Profiles[Index].ExecutorId = TEXT("DirectSingleTarget");
				Profiles[Index].Damage = Index == 0 ? 7.5f : 0.0f;
				Profiles[Index].AttackRatePerSecond = 4.0f;
				Profiles[Index].RangeCentimeters = 15000.0f;
				Samples[Index] = {FGuLiSoldierId(Index + 1), Index == 0 ? EGuLiTeam::Red : EGuLiTeam::Blue,
					FVector(Index * 1000.0, 0.0, 0.0), true, false, &Profiles[Index], &States[Index]};
				Indices.Add(Index + 1, Index);
			}
		}
		FGuLiCombatStepMetrics Tick(const uint32 Tick, const double Seconds)
		{
			GuLiSoldierCombat::BuildSpatialGrid(Samples, Grid);
			return GuLiSoldierCombat::CollectAttacks(Samples, Indices, Grid, Tick, Seconds, Executors, Events);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiStrafeTargetingTest,
	"GuLiStrike.Commander.Combat.StrafeTargeting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiStrafeTargetingTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSoldierCombatTests;
	FFixture F(5);
	F.Samples[1].Location = FVector(12000.0, 0.0, 0.0);
	F.Samples[2].Location = FVector(-12000.0, 0.0, 0.0); // tie: lower SoldierId wins.
	F.Samples[3].Location = FVector(0.0, 0.0, 16000.0); // planar overlap but outside 3D range.
	F.Samples[4].Location = FVector(100.0, 0.0, 0.0); F.Samples[4].Team = EGuLiTeam::Red;
	F.Tick(1, 1.0 / 30.0);
	TestEqual(TEXT("one attack"), F.Events.Num(), 1);
	TestEqual(TEXT("nearest valid enemy and stable ID tie break"), F.States[0].TargetId.Value, 2u);
	if (!F.Events.IsEmpty()) TestEqual(TEXT("fractional damage is preserved"), F.Events[0].Damage, 7.5f);
	F.Samples[2].Location = FVector(500.0, 0.0, 0.0);
	F.Tick(7, 7.0 / 30.0);
	TestEqual(TEXT("valid target remains locked even if a nearer enemy appears"), F.States[0].TargetId.Value, 2u);
	F.Samples[1].bAlive = false;
	F.Tick(8, 8.0 / 30.0);
	TestFalse(TEXT("invalid target is cleared immediately"), F.States[0].TargetId.IsValid());
	F.Tick(13, 13.0 / 30.0);
	TestEqual(TEXT("next 200ms bucket selects a living target"), F.States[0].TargetId.Value, 3u);
	F.Profiles[0].RangeCentimeters = 35000.0f;
	F.States[0].TargetId = {};
	F.Samples[2].Location = FVector(30000.0, 0.0, 0.0);
	F.Samples[3].bAlive = false;
	F.Tick(19, 19.0 / 30.0);
	TestEqual(TEXT("range query searches beyond the old avoidance nine cells"), F.States[0].TargetId.Value, 3u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiStrafeSharedSkillDifferentTypesTest,
	"GuLiStrike.Commander.Combat.SharedSkillDifferentTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiStrafeSharedSkillDifferentTypesTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSoldierCombatTests;
	FFixture F(3);
	F.Samples[1].Team = EGuLiTeam::Red;
	F.Profiles[0].Damage = 10.0f; F.Profiles[0].AttackRatePerSecond = 2.0f;
	F.Profiles[0].RangeCentimeters = 10000.0f;
	F.Profiles[1].Damage = 7.5f; F.Profiles[1].AttackRatePerSecond = 4.0f;
	F.States[0].TargetId = FGuLiSoldierId(3); F.States[1].TargetId = FGuLiSoldierId(3);
	float TypeADamage = 0.0f, TypeBDamage = 0.0f;
	for (uint32 Tick = 1; Tick <= 300; ++Tick)
	{
		F.Tick(Tick, static_cast<double>(Tick) / 30.0);
		for (const FGuLiCombatDamageEvent& Event : F.Events)
		{
			if (Event.SourceId.Value == 1) TypeADamage += Event.Damage;
			if (Event.SourceId.Value == 2) TypeBDamage += Event.Damage;
		}
	}
	TestEqual(TEXT("same skill uses A's independent rate"), F.States[0].ShotsFired, uint64(20));
	TestEqual(TEXT("same skill uses B's independent rate"), F.States[1].ShotsFired, uint64(40));
	TestEqual(TEXT("A's configured damage"), TypeADamage, 200.0f);
	TestEqual(TEXT("B's fractional configured damage"), TypeBDamage, 300.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiStrafeOrdersAndCadenceTest,
	"GuLiStrike.Commander.Combat.OrdersAndCadence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiStrafeOrdersAndCadenceTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSoldierCombatTests;
	FFixture F(2);
	F.Samples[0].bHasMoveOrder = true;
	F.Tick(1, 1.0 / 30.0);
	TestEqual(TEXT("move order still fires with the configured damage"), F.Events.Num(), 1);
	TestEqual(TEXT("moving fire keeps a valid target"), F.States[0].TargetId.Value, 2u);
	TestEqual(TEXT("moving fire reports the actual combat state"), F.States[0].StopReason, EGuLiCombatStopReason::Fired);
	for (uint32 Tick = 2; Tick <= 300; ++Tick) F.Tick(Tick, static_cast<double>(Tick) / 30.0);
	TestEqual(TEXT("4 attacks per second over ten seconds despite 30Hz quantization"), F.States[0].ShotsFired, uint64(40));
	const uint64 BeforeHitch = F.States[0].ShotsFired;
	F.Tick(307, 100.0);
	TestEqual(TEXT("a hitch never replays missed shots"), F.States[0].ShotsFired, BeforeHitch + 1);
	F.Tick(308, 100.0 + 1.0 / 30.0);
	TestEqual(TEXT("hitch cannot leave a queued burst"), F.States[0].ShotsFired, BeforeHitch + 1);
	TestEqual(TEXT("moving through a hitch retains target"), F.States[0].TargetId.Value, 2u);
	F.Samples[0].bHasMoveOrder = false;
	F.Tick(309, 100.1);
	TestEqual(TEXT("changing movement state does not reset cooldown"), F.States[0].ShotsFired, BeforeHitch + 1);
	F.Samples[0].bHasMoveOrder = true;
	F.Tick(313, 100.25);
	TestEqual(TEXT("moving resumes on the original cooldown boundary"), F.States[0].ShotsFired, BeforeHitch + 2);
	TestEqual(TEXT("movement state never becomes a stop reason"), F.States[0].StopReason, EGuLiCombatStopReason::Fired);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiStrafeProfileChangeTest,
	"GuLiStrike.Commander.Combat.ProfileChange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiStrafeProfileChangeTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSoldierCombatTests;
	FFixture F(2);
	F.Profiles[0].AttackRatePerSecond = 2.0f;
	F.States[0].NextFireSeconds = 1.5;
	F.States[0].TargetId = FGuLiSoldierId(2);
	FGuLiResolvedSkillProfile Next = F.Profiles[0]; Next.AttackRatePerSecond = 4.0f;
	GuLiSoldierCombat::ReplaceProfile(F.Profiles[0], Next, 1.25, F.States[0]);
	TestEqual(TEXT("half remaining interval stays half after doubling rate"), F.States[0].NextFireSeconds, 1.375);
	TestEqual(TEXT("numeric change retains target"), F.States[0].TargetId.Value, 2u);
	FGuLiResolvedSkillProfile Paused = Next; Paused.AttackRatePerSecond = 0.0f;
	GuLiSoldierCombat::ReplaceProfile(Next, Paused, 1.25, F.States[0]);
	GuLiSoldierCombat::ReplaceProfile(Paused, Next, 10.0, F.States[0]);
	TestEqual(TEXT("zero-rate pause preserves cooldown progress when resumed"), F.States[0].NextFireSeconds, 10.125);
	F.Profiles[0] = Next;
	Next.SkillId = TEXT("OtherBasicAttack");
	GuLiSoldierCombat::ReplaceProfile(F.Profiles[0], Next, 1.3, F.States[0]);
	TestEqual(TEXT("skill replacement starts a complete new cooldown"), F.States[0].NextFireSeconds, 1.55);
	TestFalse(TEXT("skill replacement clears old target"), F.States[0].TargetId.IsValid());
	F.Profiles[0] = Next; F.Profiles[0].AttackRatePerSecond = 0;
	F.Tick(13, 2.0);
	TestEqual(TEXT("zero rate stops firing"), F.States[0].StopReason, EGuLiCombatStopReason::Disabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiStrafeSimultaneousAndExtensibleTest,
	"GuLiStrike.Commander.Combat.SimultaneousAndExtensible", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiStrafeSimultaneousAndExtensibleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSoldierCombatTests;
	FFixture F(2);
	F.Profiles[0].Damage = 100.0f; F.Profiles[1].Damage = 300.5f;
	F.States[0].TargetId = FGuLiSoldierId(2); F.States[1].TargetId = FGuLiSoldierId(1);
	F.Tick(1, 1.0 / 30.0);
	TestEqual(TEXT("both attacks collected before damage allows mutual kills"), F.Events.Num(), 2);
	F.Profiles[0].ExecutorId = TEXT("Unknown");
	F.Tick(7, 1.0);
	TestEqual(TEXT("unknown executor safely stops"), F.States[0].StopReason, EGuLiCombatStopReason::UnknownExecutor);
	TestTrue(TEXT("register another executor without modifying attack scheduling"), F.Executors.RegisterExecutor(TEXT("Unknown"),
		[](const FGuLiCombatSample& Source, const FGuLiCombatSample& Target, TArray<FGuLiCombatDamageEvent>& Events)
		{ Events.Add({Source.SoldierId, Target.SoldierId, Source.Profile->SkillId, 1.25f}); }));
	F.Tick(13, 2.0);
	TestTrue(TEXT("registered executor is executed"), F.Events.ContainsByPredicate(
		[](const FGuLiCombatDamageEvent& Event) { return Event.SourceId.Value == 1 && Event.Damage == 1.25f; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCombatBenchmarkHarnessTest,
	"GuLiStrike.Commander.Combat.BenchmarkHarness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCombatBenchmarkHarnessTest::RunTest(const FString& Parameters)
{
	FGuLiCombatBenchmarkResult Result;
	TestTrue(TEXT("production helper runs without World/rendering"), GuLiSoldierCombat::RunBenchmark(500, 12, Result));
	TestEqual(TEXT("population reported"), Result.PopulationCount, 500);
	TestTrue(TEXT("all acquisition buckets are exercised"), Result.TargetQueries >= 500);
	TestTrue(TEXT("attacks execute in benchmark"), Result.Shots >= 500);
	TestTrue(TEXT("timing distribution is well formed"), Result.MaximumMilliseconds >= Result.P95Milliseconds);
	return true;
}
#endif
