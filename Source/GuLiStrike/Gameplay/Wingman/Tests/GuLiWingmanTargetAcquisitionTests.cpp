// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Combat/GuLiWingmanTargetAcquisition.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanTargetAcquisitionTests
{
	FGuLiTargetHandle MakeShipTarget(const uint32 StableId)
	{
		FGuLiTargetHandle Handle;
		Handle.Kind = EGuLiTargetKind::Ship;
		Handle.AuthorityId = FGuid(StableId, 0x11223344u, 0x55667788u, 0x99aabbccu);
		Handle.Generation = 1u;
		Handle.LocalId = StableId;
		return Handle;
	}

	FGuLiWingmanTargetObservation MakeObservation(
		const uint32 StableId,
		const EGuLiTeam Team,
		const FVector& Location,
		const bool bAlive = true,
		const bool bTrusted = true)
	{
		FGuLiWingmanTargetObservation Observation;
		Observation.Target = MakeShipTarget(StableId);
		Observation.Team = Team;
		Observation.Location = Location;
		Observation.bAlive = bAlive;
		Observation.bFromAcceptedOrReliableState = bTrusted;
		return Observation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanBasicTargetSelectionTest,
	"GuLiStrike.Wingman.Combat.BasicWeapon.TargetSelectionStableFilters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanBasicTargetSelectionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanTargetAcquisitionTests;
	(void)Parameters;

	FGuLiWingmanWeaponRuntimeConfig Weapon;
	Weapon.RangeCentimeters = 1000.0f;
	Weapon.TargetConeHalfAngleDegrees = 30.0f;
	Weapon.CooldownSeconds = 2.0f;
	Weapon.bRequiresLineOfSight = true;

	TArray<FGuLiWingmanTargetObservation> Observations;
	Observations.Add(MakeObservation(1u, EGuLiTeam::Red, FVector(100.0, 0.0, 0.0)));
	Observations.Add(MakeObservation(2u, EGuLiTeam::Blue, FVector(120.0, 0.0, 0.0), false));
	Observations.Add(MakeObservation(3u, EGuLiTeam::Blue, FVector(140.0, 0.0, 0.0), true, false));
	Observations.Add(MakeObservation(4u, EGuLiTeam::Blue, FVector(160.0, 0.0, 0.0)));
	Observations.Add(MakeObservation(5u, EGuLiTeam::Blue, FVector(-100.0, 0.0, 0.0)));
	Observations.Add(MakeObservation(6u, EGuLiTeam::Blue, FVector(1200.0, 0.0, 0.0)));
	Observations.Add(MakeObservation(9u, EGuLiTeam::Blue, FVector(300.0, 0.0, 0.0)));
	Observations.Add(MakeObservation(7u, EGuLiTeam::Blue, FVector(300.0, 0.0, 0.0)));

	FGuLiWingmanTargetObservation Selected;
	const bool bSelected = FGuLiWingmanTargetAcquisition::SelectBestTarget(
		FVector::ZeroVector,
		FVector::ForwardVector,
		EGuLiTeam::Red,
		Weapon,
		Observations,
		[](const FGuLiWingmanTargetObservation& Observation)
		{
			return Observation.Target.LocalId != 4u;
		},
		Selected);
	TestTrue(TEXT("An eligible enemy target is selected"), bSelected);
	TestEqual(TEXT("Blocked/friendly/dead/untrusted/out-of-cone/out-of-range targets are skipped; stable lower handle wins tie"),
		Selected.Target.LocalId,
		7u);

	Weapon.bRequiresLineOfSight = false;
	int32 UnexpectedLosCalls = 0;
	TestTrue(TEXT("Selection succeeds without LOS when projected definition disables it"),
		FGuLiWingmanTargetAcquisition::SelectBestTarget(
			FVector::ZeroVector,
			FVector::ForwardVector,
			EGuLiTeam::Red,
			Weapon,
			Observations,
			[&UnexpectedLosCalls](const FGuLiWingmanTargetObservation&)
			{
				++UnexpectedLosCalls;
				return false;
			},
			Selected));
	TestEqual(TEXT("LOS callback is not consulted when the projected weapon disables LOS"),
		UnexpectedLosCalls,
		0);
	TestEqual(TEXT("Nearest otherwise-eligible target wins without LOS"), Selected.Target.LocalId, 4u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanTargetScanCadenceTest,
	"GuLiStrike.Wingman.Combat.BasicWeapon.FiveHertzStaggeredCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanTargetScanCadenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Candidate publication cadence is exactly 5 Hz"),
		1.0 / FGuLiWingmanTargetAcquisition::CandidatePublishIntervalSeconds,
		5.0);

	TArray<double> Schedule;
	if (!TestTrue(TEXT("25-emitter schedule initializes"),
		FGuLiWingmanTargetAcquisition::InitializeStaggeredSchedule(100.0, 25, Schedule)))
	{
		return false;
	}
	TestEqual(TEXT("Every emitter owns an independent scan phase"), Schedule.Num(), 25);

	TSet<int32> FirstWindowEmitters;
	int32 MaximumOneTickBurst = 0;
	for (const double NowSeconds : {100.0, 100.05, 100.10, 100.15, 100.1999})
	{
		TArray<int32> Due;
		TestTrue(TEXT("Schedule consumption succeeds"),
			FGuLiWingmanTargetAcquisition::ConsumeDueScans(NowSeconds, Schedule, Due));
		MaximumOneTickBurst = FMath::Max(MaximumOneTickBurst, Due.Num());
		for (const int32 Index : Due)
		{
			TestFalse(TEXT("No emitter scans twice in the first 200 ms phase window"),
				FirstWindowEmitters.Contains(Index));
			FirstWindowEmitters.Add(Index);
		}
	}
	TestEqual(TEXT("All 25 emitters scan exactly once per 200 ms window"),
		FirstWindowEmitters.Num(),
		25);
	TestTrue(TEXT("Staggering avoids a 25-intent one-tick burst"), MaximumOneTickBurst <= 7);

	TArray<int32> DueAfterHitch;
	TestTrue(TEXT("A hitch is consumed without catch-up bursts"),
		FGuLiWingmanTargetAcquisition::ConsumeDueScans(101.0, Schedule, DueAfterHitch));
	TestEqual(TEXT("At most one scan is emitted per emitter after a hitch"), DueAfterHitch.Num(), 25);
	for (const double NextScan : Schedule)
	{
		TestTrue(TEXT("Each phase advances beyond the hitch time"), NextScan > 101.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanTargetSpatialHashTest,
	"GuLiStrike.Wingman.Combat.BasicWeapon.ClientSpatialHashStableThreeDimensionalCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanTargetSpatialHashTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanTargetAcquisitionTests;
	(void)Parameters;

	TArray<FGuLiWingmanTargetObservation> Input;
	Input.Add(MakeObservation(9u, EGuLiTeam::Blue, FVector(100.0, 0.0, 0.0)));
	Input.Add(MakeObservation(3u, EGuLiTeam::Blue, FVector(-100.0, 0.0, 0.0)));
	Input.Add(MakeObservation(7u, EGuLiTeam::Blue, FVector(0.0, 0.0, 250.0)));
	Input.Add(MakeObservation(4u, EGuLiTeam::Blue, FVector(0.0, 0.0, -250.0)));
	Input.Add(MakeObservation(12u, EGuLiTeam::Blue, FVector(5000.0, 0.0, 0.0)));

	FGuLiWingmanTargetSpatialHash Hash;
	TestFalse(TEXT("A non-positive cell size is rejected"), Hash.Build(Input, 0.0));
	TestTrue(TEXT("A finite client acquisition cut builds"), Hash.Build(Input, 200.0));
	TestEqual(TEXT("All valid observations are indexed"), Hash.Num(), Input.Num());

	TArray<FGuLiWingmanTargetObservation> Candidates;
	TestTrue(TEXT("A bounded 3D range query succeeds"),
		Hash.QuerySphere(FVector::ZeroVector, 300.0, Candidates));
	TestEqual(TEXT("The far target is excluded without scanning it per emitter"),
		Candidates.Num(),
		4);
	if (Candidates.Num() == 4)
	{
		TestEqual(TEXT("Results restore stable handle order (0)"), Candidates[0].Target.LocalId, 3u);
		TestEqual(TEXT("Results restore stable handle order (1)"), Candidates[1].Target.LocalId, 4u);
		TestEqual(TEXT("Results restore stable handle order (2)"), Candidates[2].Target.LocalId, 7u);
		TestEqual(TEXT("Results restore stable handle order (3)"), Candidates[3].Target.LocalId, 9u);
	}

	TArray<FGuLiWingmanTargetObservation> Reversed = Input;
	Algo::Reverse(Reversed);
	FGuLiWingmanTargetSpatialHash ReversedHash;
	TestTrue(TEXT("The reversed replicated cut builds"), ReversedHash.Build(Reversed, 200.0));
	TArray<FGuLiWingmanTargetObservation> ReversedCandidates;
	TestTrue(TEXT("The reversed cut query succeeds"),
		ReversedHash.QuerySphere(FVector::ZeroVector, 300.0, ReversedCandidates));
	TestEqual(TEXT("Replicated container order cannot change the candidate count"),
		ReversedCandidates.Num(),
		Candidates.Num());
	for (int32 Index = 0; Index < FMath::Min(Candidates.Num(), ReversedCandidates.Num()); ++Index)
	{
		TestTrue(TEXT("Replicated container order cannot change stable candidate order"),
			Candidates[Index].Target == ReversedCandidates[Index].Target);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanCommanderAuthoritativeTargetTest,
	"GuLiStrike.Wingman.Combat.BasicWeapon.CommanderUsesAuthoritativePoseReaderOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanCommanderAuthoritativeTargetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGuLiSoldierStateItem AliveEnemy;
	AliveEnemy.SoldierId = FGuLiSoldierId(77u);
	AliveEnemy.Team = EGuLiTeam::Blue;
	AliveEnemy.LifeState = EGuLiSoldierLifeState::Alive;
	AliveEnemy.Health = 100.0f;
	AliveEnemy.MaxHealth = 100.0f;
	AliveEnemy.StateRevision = 3u;

	FGuLiSoldierStateItem Destroyed = AliveEnemy;
	Destroyed.SoldierId = FGuLiSoldierId(88u);
	Destroyed.LifeState = EGuLiSoldierLifeState::Destroyed;
	Destroyed.Health = 0.0f;

	const TArray<FGuLiSoldierStateItem> ReliableStates = {AliveEnemy, Destroyed};
	int32 AuthoritativeReaderCalls = 0;
	TArray<FGuLiWingmanTargetObservation> Observations;
	const int32 AddedCount = FGuLiWingmanTargetAcquisition::AppendCommanderObservations(
		ReliableStates,
		42u,
		42u,
		[&AuthoritativeReaderCalls](const FGuLiSoldierId SoldierId, FTransform& OutTransform)
		{
			++AuthoritativeReaderCalls;
			if (SoldierId != FGuLiSoldierId(77u))
			{
				return false;
			}
			// This sentinel is the authoritative-pose result. The production call site
			// binds only TryGetAuthoritativeSoldierTransform; PresentedTransform is absent.
			OutTransform = FTransform(FQuat::Identity, FVector(1234.0, 50.0, 25.0));
			return true;
		},
		Observations);

	TestEqual(TEXT("Only the alive reliable roster entry requests an authoritative pose"),
		AuthoritativeReaderCalls,
		1);
	TestEqual(TEXT("One Commander target observation is produced"), AddedCount, 1);
	if (!TestEqual(TEXT("Observation array contains one Commander target"), Observations.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("The observation records authoritative-pose provenance"),
		static_cast<uint8>(Observations[0].Source),
		static_cast<uint8>(EGuLiWingmanTargetObservationSource::CommanderAuthoritativePose));
	TestEqual(TEXT("The authoritative sentinel, not a presented/predicted value, is used"),
		Observations[0].Location,
		FVector(1234.0, 50.0, 25.0));
	TestTrue(TEXT("Commander stable target handle matches the server adapter"),
		Observations[0].Target
			== GuLiCombatTargets::MakeCommanderSoldierTargetHandle(42u, 77u));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
