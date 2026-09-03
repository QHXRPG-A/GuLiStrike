#include "Battle/Combat/GuLiShipProjectileLedgerBridge.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace GuLiShipProjectileLedgerBridgeTests
{
	struct FVirtualWingmanState
	{
		FGuLiTargetHandle Handle;
		FVector Location = FVector::ZeroVector;
		float Radius = 0.0f;
		float Health = 100.0f;
		EGuLiTeam Team = EGuLiTeam::Blue;
	};

	FGuLiWingmanHandle MakeWingman(const uint8 MemberIndex)
	{
		FGuLiWingmanHandle Wingman;
		Wingman.Flight.Group.ShipInstanceId = FGuid(0x57494e47u, 0x4d414e00u, 0u, 99u);
		Wingman.Flight.Group.ShipGeneration = 1u;
		Wingman.Flight.Group.GroupGeneration = 1u;
		Wingman.Flight.FlightIndex = 0u;
		Wingman.MemberIndex = MemberIndex;
		Wingman.EntityGeneration = 1u;
		return Wingman;
	}

	bool RegisterVirtualWingman(
		UGuLiDamageLedgerSubsystem& Ledger,
		UObject& LifetimeOwner,
		const TSharedRef<FVirtualWingmanState>& State)
	{
		FGuLiCombatTargetAdapter Adapter;
		Adapter.LifetimeOwner = &LifetimeOwner;
		Adapter.ReadSnapshot = [State](FGuLiCombatTargetSnapshot& OutSnapshot)
		{
			OutSnapshot = FGuLiCombatTargetSnapshot{};
			OutSnapshot.Handle = State->Handle;
			OutSnapshot.Team = State->Team;
			OutSnapshot.Location = State->Location;
			OutSnapshot.CollisionRadius = State->Radius;
			OutSnapshot.Health = State->Health;
			OutSnapshot.bAlive = State->Health > 0.0f;
			return true;
		};
		Adapter.ApplyDamage = [State](
			const FGuLiDamageRequest& Request, FGuLiDamageCommitResult& OutResult)
		{
			if (Request.Target != State->Handle || State->Health <= 0.0f)
			{
				return false;
			}
			const float PreviousHealth = State->Health;
			State->Health = FMath::Max(0.0f, State->Health - Request.Damage);
			OutResult.AppliedDamage = PreviousHealth - State->Health;
			OutResult.RemainingHealth = State->Health;
			OutResult.bKilled = State->Health <= 0.0f;
			return true;
		};
		return Ledger.RegisterTarget(State->Handle, MoveTemp(Adapter));
	}

	struct FFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (bWorldContextRegistered && GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
		}

		bool Initialize(FAutomationTestBase& Test, const uint32 Epoch = 41u)
		{
			if (!Test.TestNotNull(TEXT("The engine exists"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("An isolated authority World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			UGuLiDamageLedgerSubsystem* Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
			return Test.TestNotNull(TEXT("The World owns a Damage Ledger"), Ledger)
				&& Test.TestTrue(TEXT("The authority epoch starts"), Ledger->BeginServerEpoch(Epoch));
		}

		AActor* SpawnCombatActor(
			FAutomationTestBase& Test,
			const uint32 StableId,
			const EGuLiTeam Team,
			UGuLiCombatHealthComponent*& OutHealth)
		{
			OutHealth = nullptr;
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("A combat Actor exists"), Actor))
			{
				return nullptr;
			}
			OutHealth = NewObject<UGuLiCombatHealthComponent>(Actor);
			Actor->AddInstanceComponent(OutHealth);
			OutHealth->RegisterComponent();
			FGuLiTargetHandle Handle;
			Handle.Kind = EGuLiTargetKind::Ship;
			Handle.AuthorityId = FGuid(0x53484950u, 0u, 0u, StableId);
			Handle.Generation = 1u;
			if (!Test.TestTrue(TEXT("The Actor registers a stable TargetHandle"),
				OutHealth->ConfigureServerTarget(Handle, Team))
				|| !Test.TestTrue(TEXT("The Actor initializes custom health"),
					OutHealth->InitializeServerHealth(100.0f, false)))
			{
				return nullptr;
			}
			return Actor;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipProjectileWingmanSweepTest,
	"GuLiStrike.Combat.ShipProjectile.AcceptedWingmanSegmentSphereSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipProjectileWingmanSweepTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipProjectileLedgerBridgeTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UGuLiCombatHealthComponent* SourceHealth = nullptr;
	AActor* Source = Fixture.SpawnCombatActor(*this, 10u, EGuLiTeam::Red, SourceHealth);
	UGuLiDamageLedgerSubsystem* Ledger =
		Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!Source || !SourceHealth || !Ledger)
	{
		return false;
	}

	const TSharedRef<FVirtualWingmanState> Far = MakeShared<FVirtualWingmanState>();
	Far->Handle = GuLiCombatTargets::MakeWingmanTargetHandle(MakeWingman(1u));
	Far->Location = FVector(800.0, 0.0, 0.0);
	Far->Radius = 50.0f;
	const TSharedRef<FVirtualWingmanState> Near = MakeShared<FVirtualWingmanState>();
	Near->Handle = GuLiCombatTargets::MakeWingmanTargetHandle(MakeWingman(0u));
	Near->Location = FVector(400.0, 0.0, 0.0);
	Near->Radius = 50.0f;
	if (!TestTrue(TEXT("The farther Accepted Wingman registers first"),
		RegisterVirtualWingman(*Ledger, *Source, Far))
		|| !TestTrue(TEXT("The nearer Accepted Wingman registers second"),
			RegisterVirtualWingman(*Ledger, *Source, Near)))
	{
		return false;
	}

	FGuLiShipProjectileLedgerContext Context;
	if (!TestTrue(TEXT("The physical Ship shot captures one immutable ledger identity"),
		GuLiShipProjectileLedger::BuildServerLaunchContext(*Source, 25.0f, Context)))
	{
		return false;
	}
	const FGuLiShipProjectileLedgerImpact Impact =
		GuLiShipProjectileLedger::CommitServerWingmanSweepImpact(
			*Fixture.World,
			Context,
			FVector::ZeroVector,
			FVector(1000.0, 0.0, 0.0),
			10.0f);
	TestTrue(TEXT("The earliest time-of-impact wins regardless of registration order"),
		Impact.Target == Near->Handle);
	TestEqual(TEXT("The Actor-less Wingman hit commits through the unified ledger"),
		Impact.Status, EGuLiShipProjectileLedgerImpactStatus::Committed);
	TestTrue(TEXT("The combined 10 cm projectile and 50 cm target radii yield t=0.34"),
		FMath::IsNearlyEqual(Impact.NormalizedSegmentTime, 0.34f, 0.0001f));
	TestTrue(TEXT("The impact point is reconstructed from the winning TOI"),
		Impact.HitLocation.Equals(FVector(340.0, 0.0, 0.0), 0.01));
	TestEqual(TEXT("Only the nearer Mass Wingman takes damage"), Near->Health, 75.0f);
	TestEqual(TEXT("The farther Mass Wingman is untouched"), Far->Health, 100.0f);
	TestEqual(TEXT("Exactly one Damage Event commits"), Ledger->GetCommitCount(), uint64(1u));

	const FGuLiShipProjectileLedgerImpact Duplicate =
		GuLiShipProjectileLedger::CommitServerWingmanSweepImpact(
			*Fixture.World,
			Context,
			FVector::ZeroVector,
			FVector(1000.0, 0.0, 0.0),
			10.0f);
	TestEqual(TEXT("A repeated component-tick segment reuses the same DamageEventId"),
		Duplicate.Status, EGuLiShipProjectileLedgerImpactStatus::Duplicate);
	TestEqual(TEXT("A repeated sweep cannot damage the Mass Wingman twice"), Near->Health, 75.0f);
	TestEqual(TEXT("A repeated sweep cannot advance the ledger"),
		Ledger->GetCommitCount(), uint64(1u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipProjectileLedgerCommitTest,
	"GuLiStrike.Combat.ShipProjectile.UnifiedTargetAndIdempotentLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipProjectileLedgerCommitTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipProjectileLedgerBridgeTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UGuLiCombatHealthComponent* SourceHealth = nullptr;
	UGuLiCombatHealthComponent* TargetHealth = nullptr;
	AActor* Source = Fixture.SpawnCombatActor(*this, 1u, EGuLiTeam::Red, SourceHealth);
	AActor* Target = Fixture.SpawnCombatActor(*this, 2u, EGuLiTeam::Blue, TargetHealth);
	if (!Source || !Target || !SourceHealth || !TargetHealth)
	{
		return false;
	}

	FGuLiShipProjectileLedgerContext Context;
	if (!TestTrue(TEXT("A Ship projectile captures its stable launch contract"),
		GuLiShipProjectileLedger::BuildServerLaunchContext(*Source, 17.5f, Context)))
	{
		return false;
	}
	TestTrue(TEXT("The launch source is the common Ship TargetHandle"),
		Context.Source == SourceHealth->GetTargetHandle());
	TestEqual(TEXT("The weapon part damage is retained"), Context.Damage, 17.5f);
	TestEqual(TEXT("The launch captures the current match epoch"), Context.MatchEpoch, 41u);
	TestTrue(TEXT("Shot and damage event identities are both stable and distinct"),
		Context.ShotId.IsValid() && Context.DamageEventId.IsValid()
			&& Context.ShotId != Context.DamageEventId);

	const FGuLiShipProjectileLedgerImpact First =
		GuLiShipProjectileLedger::CommitServerImpact(
			*Fixture.World, Context, *Target, FVector(10.0f, 20.0f, 30.0f));
	TestTrue(TEXT("The registered TargetHandle is resolved"),
		First.Target == TargetHealth->GetTargetHandle());
	TestTrue(TEXT("The first projectile hit commits through the common ledger"),
		First.Status == EGuLiShipProjectileLedgerImpactStatus::Committed);
	TestEqual(TEXT("Exactly the weapon damage is applied"), TargetHealth->GetHealthState().Health, 82.5f);

	UGuLiDamageLedgerSubsystem* Ledger = Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	TestEqual(TEXT("The ledger records exactly one commit"), Ledger->GetCommitCount(), static_cast<uint64>(1u));
	const FGuLiShipProjectileLedgerImpact Duplicate =
		GuLiShipProjectileLedger::CommitServerImpact(
			*Fixture.World, Context, *Target, FVector(11.0f, 21.0f, 31.0f));
	TestTrue(TEXT("A duplicate engine hit callback is recognized by event identity"),
		Duplicate.Status == EGuLiShipProjectileLedgerImpactStatus::Duplicate);
	TestEqual(TEXT("A duplicate callback cannot apply damage twice"),
		TargetHealth->GetHealthState().Health, 82.5f);
	TestEqual(TEXT("A duplicate callback cannot advance CommitCount"),
		Ledger->GetCommitCount(), static_cast<uint64>(1u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipProjectileLedgerRejectionTest,
	"GuLiStrike.Combat.ShipProjectile.AuthorityAndRuleRejections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipProjectileLedgerRejectionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipProjectileLedgerBridgeTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UGuLiCombatHealthComponent* SourceHealth = nullptr;
	UGuLiCombatHealthComponent* FriendlyHealth = nullptr;
	AActor* Source = Fixture.SpawnCombatActor(*this, 3u, EGuLiTeam::Red, SourceHealth);
	AActor* Friendly = Fixture.SpawnCombatActor(*this, 4u, EGuLiTeam::Red, FriendlyHealth);
	if (!Source || !Friendly || !SourceHealth || !FriendlyHealth)
	{
		return false;
	}

	FGuLiShipProjectileLedgerContext FriendlyContext;
	TestTrue(TEXT("The friendly-fire test shot captures a valid context"),
		GuLiShipProjectileLedger::BuildServerLaunchContext(*Source, 25.0f, FriendlyContext));
	const FGuLiShipProjectileLedgerImpact FriendlyHit =
		GuLiShipProjectileLedger::CommitServerImpact(
			*Fixture.World, FriendlyContext, *Friendly, FVector::ZeroVector);
	TestTrue(TEXT("A friendly Actor still resolves to its stable target"), FriendlyHit.HasResolvedTarget());
	TestTrue(TEXT("The common ledger rejects friendly fire"),
		FriendlyHit.Status == EGuLiShipProjectileLedgerImpactStatus::Rejected
			&& FriendlyHit.CommitResult.Status == EGuLiDamageCommitStatus::RejectedFriendlyFire);
	TestEqual(TEXT("Friendly fire cannot change custom health"),
		FriendlyHealth->GetHealthState().Health, 100.0f);

	AActor* Unregistered = Fixture.World->SpawnActor<AActor>();
	FGuLiShipProjectileLedgerContext MissingContext;
	TestTrue(TEXT("A second physical shot gets an independent event identity"),
		GuLiShipProjectileLedger::BuildServerLaunchContext(*Source, 25.0f, MissingContext));
	const FGuLiShipProjectileLedgerImpact MissingHit =
		GuLiShipProjectileLedger::CommitServerImpact(
			*Fixture.World, MissingContext, *Unregistered, FVector::ZeroVector);
	TestTrue(TEXT("An unregistered legacy Actor is left for the legacy hit path"),
		MissingHit.Status == EGuLiShipProjectileLedgerImpactStatus::MissingTarget
			&& !MissingHit.HasResolvedTarget());

	FGuLiShipProjectileLedgerContext OldEpochContext;
	TestTrue(TEXT("The old-epoch shot starts valid"),
		GuLiShipProjectileLedger::BuildServerLaunchContext(*Source, 25.0f, OldEpochContext));
	UGuLiDamageLedgerSubsystem* Ledger = Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	TestTrue(TEXT("The server advances to a new match epoch"), Ledger->BeginServerEpoch(42u));
	const FGuLiShipProjectileLedgerImpact OldEpochHit =
		GuLiShipProjectileLedger::CommitServerImpact(
			*Fixture.World, OldEpochContext, *Friendly, FVector::ZeroVector);
	TestTrue(TEXT("A stale physical projectile is rejected by the epoch gate"),
		OldEpochHit.Status == EGuLiShipProjectileLedgerImpactStatus::Rejected
			&& OldEpochHit.CommitResult.Status == EGuLiDamageCommitStatus::RejectedWrongEpoch);
	TestEqual(TEXT("No rejected projectile advances the commit ledger"),
		Ledger->GetCommitCount(), static_cast<uint64>(0u));
	return true;
}
#endif
