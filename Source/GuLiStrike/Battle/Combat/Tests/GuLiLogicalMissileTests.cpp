#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace GuLiLogicalMissileTests
{
	struct FFixture
	{
		UWorld* World = nullptr;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UGuLiLogicalMissileSubsystem* Missiles = nullptr;
		bool bWorldContextRegistered = false;

		~FFixture()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine) GEngine->DestroyWorldContext(World);
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("The engine exists"), GEngine)) return false;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("An authority game World exists"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
			Missiles = World->GetSubsystem<UGuLiLogicalMissileSubsystem>();
			return Test.TestNotNull(TEXT("The World creates the damage ledger"), Ledger)
				&& Test.TestNotNull(TEXT("The World creates the logical missile service"), Missiles)
				&& Test.TestTrue(TEXT("The damage ledger starts the match epoch"), Ledger->BeginServerEpoch(3u));
		}

		UGuLiCombatHealthComponent* SpawnTarget(FAutomationTestBase& Test, const FGuLiTargetHandle& Handle,
			const EGuLiTeam Team, const FVector& Location)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("A logical target Actor exists"), Actor)) return nullptr;
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Actor->AddInstanceComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorLocation(Location);
			UGuLiCombatHealthComponent* Health = NewObject<UGuLiCombatHealthComponent>(Actor);
			Actor->AddInstanceComponent(Health);
			Health->RegisterComponent();
			return Health->ConfigureServerTarget(Handle, Team)
				&& Health->InitializeServerHealth(100.0f, false) ? Health : nullptr;
		}
	};

	FGuLiTargetHandle MakeShipTarget(const uint32 Id)
	{
		FGuLiTargetHandle Handle;
		Handle.Kind = EGuLiTargetKind::Ship;
		Handle.AuthorityId = FGuid(0u, 0u, 0u, Id);
		Handle.Generation = 1u;
		return Handle;
	}

	FGuLiWingmanHandle MakeEmitter(const FGuLiTargetHandle& Source)
	{
		FGuLiWingmanHandle Emitter;
		Emitter.Flight.Group.ShipInstanceId = Source.AuthorityId;
		Emitter.Flight.Group.ShipGeneration = Source.Generation;
		Emitter.Flight.Group.GroupGeneration = 1u;
		Emitter.Flight.FlightIndex = 0u;
		Emitter.MemberIndex = 0u;
		Emitter.EntityGeneration = 1u;
		return Emitter;
	}

	FGuLiLogicalMissileLaunchRequest MakeLaunch(
		const FGuLiTargetHandle& Source, const FGuLiTargetHandle& Target, const uint32 Id,
		const FVector& Position, const float Speed = 6000.0f)
	{
		FGuLiLogicalMissileLaunchRequest Request;
		Request.MatchEpoch = 3u;
		Request.MissileId = FGuid(0u, 0u, 10u, Id);
		Request.ShotId = FGuid(0u, 0u, 11u, Id);
		Request.RootEventId = Request.ShotId;
		Request.WeaponBinding = FGuLiWeaponBindingKey::Wingman(Request.MatchEpoch,
			EGuLiTeam::Red, Source.AuthorityId, TEXT("MissileFixture"), TEXT("BasicAttack"));
		Request.SkillId = TEXT("MissileFixture");
		Request.LoadoutRevision = 1u;
		Request.ProfileRevision = 1u;
		Request.Source = Source;
		Request.Emitter = MakeEmitter(Source);
		Request.Target = Target;
		Request.LaunchPosition = Position;
		Request.LaunchDirection = FVector::ForwardVector;
		Request.SpeedCentimetersPerSecond = Speed;
		Request.TurnRateDegreesPerSecond = 180.0f;
		Request.SweepRadiusCentimeters = 50.0f;
		Request.MaximumLifetimeSeconds = 10.0f;
		Request.Damage = 40.0f;
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiLogicalMissileImpactTest,
	"GuLiStrike.Combat.Missile.LogicalImpactWithoutReplicatedActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiLogicalMissileImpactTest::RunTest(const FString& Parameters)
{
	using namespace GuLiLogicalMissileTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const FGuLiTargetHandle Source = MakeShipTarget(1u);
	const FGuLiTargetHandle Target = MakeShipTarget(2u);
	if (!Fixture.SpawnTarget(*this, Source, EGuLiTeam::Red, FVector::ZeroVector)
		|| !Fixture.SpawnTarget(*this, Target, EGuLiTeam::Blue, FVector(1000.0, 0.0, 0.0))) return false;

	EGuLiLogicalMissileTerminalReason FinishedReason = EGuLiLogicalMissileTerminalReason::Invalid;
	Fixture.Missiles->OnFinished.AddLambda([&FinishedReason](const FGuLiLogicalMissileTerminalEvent& Event)
	{
		FinishedReason = Event.Reason;
	});
	const FGuLiLogicalMissileLaunchRequest Launch = MakeLaunch(Source, Target, 1u, FVector::ZeroVector);
	TestTrue(TEXT("A valid server logical missile launches"), Fixture.Missiles->LaunchMissile(Launch));
	TestFalse(TEXT("The missile service is not an Actor class"),
		UGuLiLogicalMissileSubsystem::StaticClass()->IsChildOf(AActor::StaticClass()));
	for (int32 Step = 0; Step < 30 && Fixture.Missiles->GetActiveMissileCount() > 0; ++Step)
	{
		Fixture.Missiles->Tick(1.0f / 30.0f);
	}
	TestEqual(TEXT("The impact terminates the logical record"), Fixture.Missiles->GetActiveMissileCount(), 0);
	TestTrue(TEXT("The terminal event is an impact"), FinishedReason == EGuLiLogicalMissileTerminalReason::Impact);
	FGuLiCombatTargetSnapshot Snapshot;
	TestTrue(TEXT("The target remains registered after impact"), Fixture.Ledger->TryGetTargetSnapshot(Target, Snapshot));
	TestEqual(TEXT("The common ledger applies missile damage once"), Snapshot.Health, 60.0f);
	TestEqual(TEXT("Missile impact creates one damage commit"), Fixture.Ledger->GetCommitCount(), static_cast<uint64>(1u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiLogicalMissileTargetLostTest,
	"GuLiStrike.Combat.Missile.TargetDeathDoesNotRetarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiLogicalMissileTargetLostTest::RunTest(const FString& Parameters)
{
	using namespace GuLiLogicalMissileTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const FGuLiTargetHandle Source = MakeShipTarget(3u);
	const FGuLiTargetHandle Target = MakeShipTarget(4u);
	if (!Fixture.SpawnTarget(*this, Source, EGuLiTeam::Red, FVector::ZeroVector)
		|| !Fixture.SpawnTarget(*this, Target, EGuLiTeam::Blue, FVector(100000.0, 0.0, 0.0))) return false;
	EGuLiLogicalMissileTerminalReason FinishedReason = EGuLiLogicalMissileTerminalReason::Invalid;
	Fixture.Missiles->OnFinished.AddLambda([&FinishedReason](const FGuLiLogicalMissileTerminalEvent& Event)
	{
		FinishedReason = Event.Reason;
	});
	TestTrue(TEXT("A distant target can be locked at launch"),
		Fixture.Missiles->LaunchMissile(MakeLaunch(Source, Target, 2u, FVector::ZeroVector, 1000.0f)));
	Fixture.Missiles->Tick(1.0f / 30.0f);

	FGuLiDamageRequest Kill;
	Kill.MatchEpoch = 3u;
	Kill.DamageEventId = FGuid(0u, 0u, 20u, 1u);
	Kill.ShotId = FGuid(0u, 0u, 21u, 1u);
	Kill.Source = Source;
	Kill.Target = Target;
	Kill.Damage = 1000.0f;
	TestTrue(TEXT("Another accepted hit kills the target in flight"), Fixture.Ledger->CommitDamage(Kill).bKilled);
	Fixture.Missiles->Tick(1.0f / 30.0f);
	TestEqual(TEXT("A target loss terminates the original missile"), Fixture.Missiles->GetActiveMissileCount(), 0);
	TestTrue(TEXT("The missile self-destructs without selecting a replacement"),
		FinishedReason == EGuLiLogicalMissileTerminalReason::TargetLost);
	TestEqual(TEXT("Target loss adds no second damage commit"), Fixture.Ledger->GetCommitCount(), static_cast<uint64>(1u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiLogicalMissileAtomicSalvoTest,
	"GuLiStrike.Combat.Missile.AtomicSalvoRejectsWithoutPartialCreation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiLogicalMissileAtomicSalvoTest::RunTest(const FString& Parameters)
{
	using namespace GuLiLogicalMissileTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const FGuLiTargetHandle Source = MakeShipTarget(30u);
	const FGuLiTargetHandle Target = MakeShipTarget(31u);
	if (!Fixture.SpawnTarget(*this, Source, EGuLiTeam::Red, FVector::ZeroVector)
		|| !Fixture.SpawnTarget(*this, Target, EGuLiTeam::Blue, FVector(1000.0, 0.0, 0.0))) return false;

	TArray<FGuLiLogicalMissileLaunchRequest> Batch;
	Batch.Add(MakeLaunch(Source, Target, 30u, FVector::ZeroVector));
	Batch.Add(MakeLaunch(Source, Target, 31u, FVector(0.0, 100.0, 0.0)));
	Batch[1].Emitter.MemberIndex = 1u;
	Batch[1].MatchEpoch = 4u;
	int32 LaunchCount = INDEX_NONE;
	TestFalse(TEXT("One malformed member rejects the complete Flight preflight"),
		Fixture.Missiles->CanLaunchFlightSalvo(Batch));
	TestFalse(TEXT("The complete Flight transaction fails"),
		Fixture.Missiles->LaunchFlightSalvo(Batch, LaunchCount));
	TestEqual(TEXT("A rejected batch reports zero launches"), LaunchCount, 0);
	TestEqual(TEXT("A rejected batch leaves no logical missile"),
		Fixture.Missiles->GetActiveMissileCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiLogicalMissileEpochIsolationTest,
	"GuLiStrike.Combat.Missile.LaunchEpochCannotDamageNextMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiLogicalMissileEpochIsolationTest::RunTest(const FString& Parameters)
{
	using namespace GuLiLogicalMissileTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const FGuLiTargetHandle Source = MakeShipTarget(40u);
	const FGuLiTargetHandle Target = MakeShipTarget(41u);
	if (!Fixture.SpawnTarget(*this, Source, EGuLiTeam::Red, FVector::ZeroVector)
		|| !Fixture.SpawnTarget(*this, Target, EGuLiTeam::Blue, FVector(100000.0, 0.0, 0.0))) return false;

	EGuLiLogicalMissileTerminalReason FinishedReason = EGuLiLogicalMissileTerminalReason::Invalid;
	Fixture.Missiles->OnFinished.AddLambda([&FinishedReason](const FGuLiLogicalMissileTerminalEvent& Event)
	{
		FinishedReason = Event.Reason;
	});
	TestTrue(TEXT("The old-epoch missile launches"),
		Fixture.Missiles->LaunchMissile(MakeLaunch(Source, Target, 40u, FVector::ZeroVector, 1000.0f)));
	TestTrue(TEXT("Authority advances to a new match epoch"), Fixture.Ledger->BeginServerEpoch(4u));
	Fixture.Missiles->Tick(1.0f / 30.0f);
	TestEqual(TEXT("The old-epoch missile self-destructs"), Fixture.Missiles->GetActiveMissileCount(), 0);
	TestTrue(TEXT("The terminal reason records epoch isolation"),
		FinishedReason == EGuLiLogicalMissileTerminalReason::MatchEpochEnded);
	TestEqual(TEXT("No damage is committed into the new match"),
		Fixture.Ledger->GetCommitCount(), static_cast<uint64>(0u));
	return true;
}
#endif
