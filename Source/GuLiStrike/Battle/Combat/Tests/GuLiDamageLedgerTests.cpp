#include "Battle/Combat/GuLiCombatDamageLedger.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace GuLiDamageLedgerTests
{
	struct FFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FFixture()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
		}

		bool Initialize(FAutomationTestBase& Test)
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
			return true;
		}

		UGuLiCombatHealthComponent* SpawnHealth(
			FAutomationTestBase& Test, const FGuLiTargetHandle& Handle, const EGuLiTeam Team)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("A target Actor exists"), Actor))
			{
				return nullptr;
			}
			UGuLiCombatHealthComponent* Health = NewObject<UGuLiCombatHealthComponent>(Actor);
			Actor->AddInstanceComponent(Health);
			Health->RegisterComponent();
			if (!Test.TestTrue(TEXT("The target gets a stable server identity"), Health->ConfigureServerTarget(Handle, Team)))
			{
				return nullptr;
			}
			if (!Test.TestTrue(TEXT("The target gets custom non-GAS health"), Health->InitializeServerHealth(100.0f, false)))
			{
				return nullptr;
			}
			return Health;
		}
	};

	FGuLiTargetHandle Target(const EGuLiTargetKind Kind, const uint32 Id, const uint32 LocalId = 0u)
	{
		FGuLiTargetHandle Result;
		Result.Kind = Kind;
		Result.AuthorityId = FGuid(0u, 0u, 0u, Id);
		Result.Generation = 1u;
		Result.LocalId = Kind == EGuLiTargetKind::Ship ? 0u : LocalId;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDamageLedgerIdempotencyTest,
	"GuLiStrike.Combat.DamageLedger.IdempotentCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDamageLedgerIdempotencyTest::RunTest(const FString& Parameters)
{
	using namespace GuLiDamageLedgerTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	UGuLiDamageLedgerSubsystem* Ledger = Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!TestNotNull(TEXT("The World owns one damage ledger"), Ledger)
		|| !TestTrue(TEXT("The server starts one match ledger epoch"), Ledger->BeginServerEpoch(9u)))
	{
		return false;
	}
	const FGuLiTargetHandle SourceHandle = Target(EGuLiTargetKind::Ship, 1u);
	const FGuLiTargetHandle TargetHandle = Target(EGuLiTargetKind::Ship, 2u);
	const FGuLiTargetHandle FriendlyHandle = Target(EGuLiTargetKind::Ship, 3u);
	UGuLiCombatHealthComponent* SourceHealth = Fixture.SpawnHealth(*this, SourceHandle, EGuLiTeam::Red);
	UGuLiCombatHealthComponent* TargetHealth = Fixture.SpawnHealth(*this, TargetHandle, EGuLiTeam::Blue);
	UGuLiCombatHealthComponent* FriendlyHealth = Fixture.SpawnHealth(*this, FriendlyHandle, EGuLiTeam::Red);
	if (!SourceHealth || !TargetHealth || !FriendlyHealth)
	{
		return false;
	}

	FGuLiDamageRequest Request;
	Request.MatchEpoch = 9u;
	Request.DamageEventId = FGuid(0u, 0u, 1u, 1u);
	Request.ShotId = FGuid(0u, 0u, 2u, 1u);
	Request.Source = SourceHandle;
	Request.Target = TargetHandle;
	Request.Damage = 25.5f;
	Request.HitLocation = TargetHealth->GetOwner()->GetActorLocation();
	const FGuLiDamageCommitResult First = Ledger->CommitDamage(Request);
	TestTrue(TEXT("The first valid event commits"), First.Status == EGuLiDamageCommitStatus::Committed);
	TestEqual(TEXT("Fractional damage is preserved"), First.AppliedDamage, 25.5f);
	TestEqual(TEXT("Custom health is the numerical truth"), TargetHealth->GetHealthState().Health, 74.5f);
	TestEqual(TEXT("Exactly one authoritative commit is counted"), Ledger->GetCommitCount(), static_cast<uint64>(1u));

	const FGuLiDamageCommitResult Duplicate = Ledger->CommitDamage(Request);
	TestTrue(TEXT("A repeated DamageEventId returns the cached duplicate result"),
		Duplicate.Status == EGuLiDamageCommitStatus::Duplicate);
	TestEqual(TEXT("A duplicate never applies damage twice"), TargetHealth->GetHealthState().Health, 74.5f);
	TestEqual(TEXT("A duplicate never increments CommitCount"), Ledger->GetCommitCount(), static_cast<uint64>(1u));

	FGuLiDamageRequest Friendly = Request;
	Friendly.DamageEventId = FGuid(0u, 0u, 1u, 2u);
	Friendly.Target = FriendlyHandle;
	TestTrue(TEXT("Friendly fire is rejected by the common ledger"),
		Ledger->CommitDamage(Friendly).Status == EGuLiDamageCommitStatus::RejectedFriendlyFire);
	TestEqual(TEXT("Rejected friendly fire does not change health"), FriendlyHealth->GetHealthState().Health, 100.0f);

	TArray<FGuLiCombatTargetSnapshot> Directory;
	Ledger->GetTargetSnapshots(Directory);
	TestEqual(TEXT("The common target directory exposes all three registered adapters"), Directory.Num(), 3);
	TestTrue(TEXT("The target directory has stable ordering"),
		Directory.Num() == 3 && Directory[0].Handle == SourceHandle
			&& Directory[1].Handle == TargetHandle && Directory[2].Handle == FriendlyHandle);

	FGuLiDamageRequest Kill = Request;
	Kill.DamageEventId = FGuid(0u, 0u, 1u, 3u);
	Kill.ShotId = FGuid(0u, 0u, 2u, 3u);
	Kill.Damage = 1000.0f;
	const FGuLiDamageCommitResult KillResult = Ledger->CommitDamage(Kill);
	TestTrue(TEXT("One lethal event reports one death"), KillResult.bKilled);
	TestEqual(TEXT("Lethal damage creates exactly one additional commit"),
		Ledger->GetCommitCount(), static_cast<uint64>(2u));
	const FGuLiDamageCommitResult DuplicateKill = Ledger->CommitDamage(Kill);
	TestTrue(TEXT("A repeated lethal event is a ledger duplicate"),
		DuplicateKill.Status == EGuLiDamageCommitStatus::Duplicate);
	TestEqual(TEXT("A repeated lethal event cannot commit death twice"),
		Ledger->GetCommitCount(), static_cast<uint64>(2u));

	FGuLiDamageRequest AfterDeath = Kill;
	AfterDeath.DamageEventId = FGuid(0u, 0u, 1u, 4u);
	AfterDeath.ShotId = FGuid(0u, 0u, 2u, 4u);
	TestTrue(TEXT("A new event cannot damage an already dead target"),
		Ledger->CommitDamage(AfterDeath).Status == EGuLiDamageCommitStatus::RejectedTargetDead);
	TestEqual(TEXT("Rejected post-death damage does not advance CommitCount"),
		Ledger->GetCommitCount(), static_cast<uint64>(2u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDamageLedgerDeathRewardIdempotencyTest,
	"GuLiStrike.Combat.DamageLedger.DeathReward.IdempotentPolicyUnavailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDamageLedgerDeathRewardIdempotencyTest::RunTest(const FString& Parameters)
{
	using namespace GuLiDamageLedgerTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	UGuLiDamageLedgerSubsystem* Ledger = Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!TestNotNull(TEXT("The World owns one damage ledger"), Ledger)
		|| !TestTrue(TEXT("The authority starts the death/reward epoch"), Ledger->BeginServerEpoch(21u)))
	{
		return false;
	}

	const FGuLiTargetHandle SourceHandle = Target(EGuLiTargetKind::Ship, 21u);
	const FGuLiTargetHandle TargetHandle = Target(EGuLiTargetKind::Ship, 22u);
	UGuLiCombatHealthComponent* SourceHealth = Fixture.SpawnHealth(*this, SourceHandle, EGuLiTeam::Red);
	UGuLiCombatHealthComponent* TargetHealth = Fixture.SpawnHealth(*this, TargetHandle, EGuLiTeam::Blue);
	if (!SourceHealth || !TargetHealth)
	{
		return false;
	}

	FGuLiDamageRequest NonLethal;
	NonLethal.MatchEpoch = 21u;
	NonLethal.DamageEventId = FGuid(0u, 0u, 21u, 1u);
	NonLethal.ShotId = FGuid(0u, 0u, 21u, 2u);
	NonLethal.Source = SourceHandle;
	NonLethal.Target = TargetHandle;
	NonLethal.Damage = 10.0f;
	NonLethal.HitLocation = TargetHealth->GetOwner()->GetActorLocation();
	const FGuLiDamageCommitResult NonLethalResult = Ledger->CommitDamage(NonLethal);
	TestFalse(TEXT("Non-lethal damage has no death"), NonLethalResult.bKilled);
	TestFalse(TEXT("Non-lethal damage has no DeathEventId"), NonLethalResult.DeathEventId.IsValid());
	TestFalse(TEXT("Non-lethal damage has no RewardEventId"), NonLethalResult.RewardEventId.IsValid());
	TestEqual(TEXT("Non-lethal damage creates no death record"), Ledger->GetDeathCommitCount(), uint64(0u));
	TestEqual(TEXT("Non-lethal damage creates no reward decision"), Ledger->GetRewardCommitCount(), uint64(0u));

	FGuLiDamageRequest Lethal = NonLethal;
	Lethal.DamageEventId = FGuid(0u, 0u, 21u, 3u);
	Lethal.ShotId = FGuid(0u, 0u, 21u, 4u);
	Lethal.Damage = 1000.0f;
	const FGuLiDamageCommitResult First = Ledger->CommitDamage(Lethal);
	TestTrue(TEXT("The first lethal damage event kills once"), First.bKilled);
	TestEqual(TEXT("DeathEventId is derived deterministically"), First.DeathEventId,
		UGuLiDamageLedgerSubsystem::DeriveDeathEventId(Lethal.DamageEventId));
	TestEqual(TEXT("RewardEventId is derived deterministically"), First.RewardEventId,
		UGuLiDamageLedgerSubsystem::DeriveRewardEventId(Lethal.DamageEventId));
	TestNotEqual(TEXT("Death and reward use independent event domains"),
		First.DeathEventId, First.RewardEventId);
	TestEqual(TEXT("Exactly one death record commits"), Ledger->GetDeathCommitCount(), uint64(1u));
	TestEqual(TEXT("Exactly one reward decision commits"), Ledger->GetRewardCommitCount(), uint64(1u));
	TestEqual(TEXT("No economy grant is invented without a policy"), Ledger->GetGrantedRewardCount(), uint64(0u));

	FGuLiDeathCommitRecord Death;
	FGuLiRewardCommitRecord Reward;
	TestTrue(TEXT("The death record is queryable by its stable id"),
		Ledger->TryGetDeathRecord(First.DeathEventId, Death));
	TestTrue(TEXT("The death record is well formed"), Death.IsWellFormed());
	TestEqual(TEXT("The death record points to the lethal damage"), Death.DamageEventId, Lethal.DamageEventId);
	TestTrue(TEXT("The reward decision is queryable by its stable id"),
		Ledger->TryGetRewardRecord(First.RewardEventId, Reward));
	TestTrue(TEXT("The reward decision is well formed"), Reward.IsWellFormed());
	TestEqual(TEXT("Missing policy is explicit and fail closed"),
		Reward.Status, EGuLiRewardGrantStatus::PolicyUnavailable);
	TestFalse(TEXT("PolicyUnavailable is not an economy grant"), Reward.WasGranted());

	const FGuLiDamageCommitResult Duplicate = Ledger->CommitDamage(Lethal);
	TestEqual(TEXT("Repeated lethal damage is a duplicate"),
		Duplicate.Status, EGuLiDamageCommitStatus::Duplicate);
	TestEqual(TEXT("Duplicate preserves the stable DeathEventId"), Duplicate.DeathEventId, First.DeathEventId);
	TestEqual(TEXT("Duplicate preserves the stable RewardEventId"), Duplicate.RewardEventId, First.RewardEventId);
	TestEqual(TEXT("Duplicate cannot commit death twice"), Ledger->GetDeathCommitCount(), uint64(1u));
	TestEqual(TEXT("Duplicate cannot commit a second reward decision"), Ledger->GetRewardCommitCount(), uint64(1u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiDamageLedgerDeathRewardEpochTest,
	"GuLiStrike.Combat.DamageLedger.DeathReward.EpochResetAndOptionalPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDamageLedgerDeathRewardEpochTest::RunTest(const FString& Parameters)
{
	using namespace GuLiDamageLedgerTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	UGuLiDamageLedgerSubsystem* Ledger = Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!TestNotNull(TEXT("The World owns one damage ledger"), Ledger)
		|| !TestTrue(TEXT("The first epoch starts"), Ledger->BeginServerEpoch(31u)))
	{
		return false;
	}

	int32 PolicyCallCount = 0;
	int32 SinkCallCount = 0;
	const FGuid Beneficiary(0u, 0u, 31u, 99u);
	TestTrue(TEXT("The authority can install an optional reward policy and sink"),
		Ledger->SetRewardPipeline(
			[&PolicyCallCount, Beneficiary](const FGuLiDeathCommitRecord&, FGuLiRewardGrant& OutGrant)
			{
				++PolicyCallCount;
				OutGrant.BeneficiaryId = Beneficiary;
				OutGrant.RewardDefinitionId = TEXT("Test.Reward.FromPolicy");
				OutGrant.Quantity = 7;
				return true;
			},
			[&SinkCallCount](const FGuLiRewardCommitRecord& Record)
			{
				++SinkCallCount;
				return Record.Grant.IsWellFormed();
			}));

	const FGuLiTargetHandle SourceHandle = Target(EGuLiTargetKind::Ship, 31u);
	const FGuLiTargetHandle FirstTargetHandle = Target(EGuLiTargetKind::Ship, 32u);
	UGuLiCombatHealthComponent* SourceHealth = Fixture.SpawnHealth(*this, SourceHandle, EGuLiTeam::Red);
	UGuLiCombatHealthComponent* FirstTarget = Fixture.SpawnHealth(*this, FirstTargetHandle, EGuLiTeam::Blue);
	if (!SourceHealth || !FirstTarget)
	{
		return false;
	}

	FGuLiDamageRequest Request;
	Request.MatchEpoch = 31u;
	Request.DamageEventId = FGuid(0u, 0u, 31u, 1u);
	Request.ShotId = FGuid(0u, 0u, 31u, 2u);
	Request.Source = SourceHandle;
	Request.Target = FirstTargetHandle;
	Request.Damage = 1000.0f;
	Request.HitLocation = FirstTarget->GetOwner()->GetActorLocation();
	const FGuLiDamageCommitResult First = Ledger->CommitDamage(Request);
	FGuLiRewardCommitRecord FirstReward;
	TestTrue(TEXT("The first epoch records its reward"),
		Ledger->TryGetRewardRecord(First.RewardEventId, FirstReward));
	TestEqual(TEXT("A policy-authored grant reaches the optional sink"),
		FirstReward.Status, EGuLiRewardGrantStatus::Granted);
	TestEqual(TEXT("The policy executes once"), PolicyCallCount, 1);
	TestEqual(TEXT("The sink executes once"), SinkCallCount, 1);
	TestEqual(TEXT("The granted reward counter advances once"), Ledger->GetGrantedRewardCount(), uint64(1u));

	TestTrue(TEXT("A new server epoch clears the idempotency domain"), Ledger->BeginServerEpoch(32u));
	FGuLiDeathCommitRecord ClearedDeath;
	FGuLiRewardCommitRecord ClearedReward;
	TestFalse(TEXT("The prior death id is absent after epoch reset"),
		Ledger->TryGetDeathRecord(First.DeathEventId, ClearedDeath));
	TestFalse(TEXT("The prior reward id is absent after epoch reset"),
		Ledger->TryGetRewardRecord(First.RewardEventId, ClearedReward));
	TestEqual(TEXT("Epoch reset clears damage count"), Ledger->GetCommitCount(), uint64(0u));
	TestEqual(TEXT("Epoch reset clears death count"), Ledger->GetDeathCommitCount(), uint64(0u));
	TestEqual(TEXT("Epoch reset clears reward count"), Ledger->GetRewardCommitCount(), uint64(0u));
	TestEqual(TEXT("Epoch reset clears granted reward count"), Ledger->GetGrantedRewardCount(), uint64(0u));

	const FGuLiTargetHandle SecondTargetHandle = Target(EGuLiTargetKind::Ship, 33u);
	UGuLiCombatHealthComponent* SecondTarget = Fixture.SpawnHealth(*this, SecondTargetHandle, EGuLiTeam::Blue);
	if (!SecondTarget)
	{
		return false;
	}
	Request.MatchEpoch = 32u;
	Request.Target = SecondTargetHandle;
	Request.HitLocation = SecondTarget->GetOwner()->GetActorLocation();
	const FGuLiDamageCommitResult Second = Ledger->CommitDamage(Request);
	FGuLiRewardCommitRecord SecondReward;
	TestTrue(TEXT("The same DamageEventId is valid in the new epoch domain"), Second.bKilled);
	TestTrue(TEXT("The new epoch creates a fresh reward decision"),
		Ledger->TryGetRewardRecord(Second.RewardEventId, SecondReward));
	TestEqual(TEXT("The reward pipeline remains configured across epoch resets"),
		SecondReward.Status, EGuLiRewardGrantStatus::Granted);
	TestEqual(TEXT("The policy runs once per epoch-domain event"), PolicyCallCount, 2);
	TestEqual(TEXT("The sink runs once per epoch-domain event"), SinkCallCount, 2);
	TestEqual(TEXT("New epoch counters restart from one"), Ledger->GetRewardCommitCount(), uint64(1u));
	return true;
}
#endif
