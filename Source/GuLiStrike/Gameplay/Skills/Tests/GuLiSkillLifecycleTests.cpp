#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Skills/GuLiArmySkillAbility.h"
#include "Misc/AutomationTest.h"

namespace GuLiSkillLifecycleTests
{
	/** Real game-world subsystems and public server APIs; no private-state or catalog injection. */
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		AGuLiBattleGameState* State = nullptr;
		UGuLiArmySkillSubsystem* Bridge = nullptr;
		float BaseDamage = 0.0f;
		bool bWorldContextRegistered = false;

		~FWorldFixture()
		{
			if (!World) return;
			// Actor/subsystem teardown still needs its engine context. Remove only this
			// fixture's context after World cleanup, leaving the editor's context intact.
			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine) GEngine->DestroyWorldContext(World);
			World = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("An engine exists to own the fixture's World context"), GEngine)) return false;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("An isolated authority game World exists"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			State = World->SpawnActor<AGuLiBattleGameState>();
			if (!Test.TestNotNull(TEXT("A real BattleGameState exists"), State)) return false;
			World->SetGameState(State);
			State->InitializeServerMatchState();
			Bridge = World->GetSubsystem<UGuLiArmySkillSubsystem>();
			if (!Test.TestNotNull(TEXT("The World creates its real army skill bridge"), Bridge)) return false;
			Bridge->CommitPendingChanges();
			const auto* Profile = Bridge->FindResolvedSkill(EGuLiTeam::Red, 1);
			if (!Test.TestNotNull(TEXT("Imported unit-one BasicAttack resolves in the test World"), Profile)) return false;
			BaseDamage = Profile->Damage;
			return Test.TestTrue(TEXT("The imported basic attack has positive base damage"), BaseDamage > 0.0f);
		}

		bool ClaimReadySeat(FAutomationTestBase& Test, AGuLiBattlePlayerState& PlayerState, uint8 PreferredSlot = 0)
		{
			PlayerState.EnsureServerPlayerGuid();
			uint8 Slot = MAX_uint8;
			EGuLiTeam Team = EGuLiTeam::Unassigned;
			EGuLiCommanderRole Role = EGuLiCommanderRole::Observer;
			if (!Test.TestTrue(TEXT("Public GameState API grants the requested free commander seat"),
				State->ClaimRoleSlot(PlayerState.GetPlayerGuid(), PreferredSlot, Slot, Team, Role))) return false;
			if (!Test.TestEqual(TEXT("The requested commander seat was actually granted"), Slot, PreferredSlot)) return false;
			if (!Test.TestTrue(TEXT("The granted seat is a commander seat"), Role == EGuLiCommanderRole::Commander)) return false;
			PlayerState.SetServerRoleAssignment(Team, Role, Slot);
			PlayerState.SetServerBattleReady(true);
			PlayerState.SetServerSoldierStreamReady(true);
			State->SetRoleSlotBattleReady(Slot, PlayerState.GetPlayerGuid(), true);
			State->SetRoleSlotSyncReady(Slot, PlayerState.GetPlayerGuid(), true);
			return true;
		}

		AGuLiBattlePlayerState* SpawnCommander(FAutomationTestBase& Test, APlayerController*& OutController)
		{
			OutController = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("A real authority PlayerController exists"), OutController)) return nullptr;
			FActorSpawnParameters Parameters;
			Parameters.Owner = OutController;
			Parameters.ObjectFlags |= RF_Transient;
			auto* PlayerState = World->SpawnActor<AGuLiBattlePlayerState>(Parameters);
			if (!Test.TestNotNull(TEXT("A real BattlePlayerState and ASC exist"), PlayerState)) return nullptr;
			OutController->SetPlayerState(PlayerState);
			if (!ClaimReadySeat(Test, *PlayerState)) return nullptr;
			return PlayerState;
		}

		bool DamageEquals(FAutomationTestBase& Test, const TCHAR* Description, float Multiplier) const
		{
			const auto* Profile = Bridge->FindResolvedSkill(EGuLiTeam::Red, 1);
			return Test.TestNotNull(Description, Profile)
				&& Test.TestTrue(Description, FMath::IsNearlyEqual(Profile->Damage, BaseDamage * Multiplier, .001f));
		}
	};

	FGuLiArmySkillCommand SourceCommand(uint32 Id, float Percent = .2f)
	{
		FGuLiArmySkillCommand Command;
		Command.Command = EGuLiArmySkillCommand::UpsertSource;
		Command.Source.SourceInstanceId = FGuid(0, 0, 0, Id);
		Command.Source.DebugLabel = FString::Printf(TEXT("LifecycleSource%u"), Id);
		auto& Modifier = Command.Source.Modifiers.AddDefaulted_GetRef();
		Modifier.Target.UnitTypeIds = {1};
		Modifier.Operation = EGuLiSkillModifierOperation::AddPercent;
		Modifier.Magnitude = Percent;
		return Command;
	}

	bool Execute(FAutomationTestBase& Test, AGuLiBattlePlayerState& PlayerState,
		const FGuLiArmySkillCommand& Command, const TCHAR* Description)
	{
		FString Error;
		const bool bSucceeded = PlayerState.ExecuteArmySkillCommand(Command, Error);
		return Test.TestTrue(FString::Printf(TEXT("%s (%s)"), Description, *Error), bSucceeded);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillPawnLifecycleTest,
	"GuLiStrike.Skills.Lifecycle.PawnReplacementAndASCRebinding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillPawnLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillLifecycleTests;
	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	APlayerController* Controller = nullptr;
	auto* PlayerState = Fixture.SpawnCommander(*this, Controller);
	if (!PlayerState) return false;
	auto* FirstPawn = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("The first Pawn exists"), FirstPawn)) return false;
	Controller->Possess(FirstPawn);
	if (!TestTrue(TEXT("The real controller possesses its first Pawn"), Controller->GetPawn() == FirstPawn)) return false;
	const auto Source = SourceCommand(1);
	if (!Execute(*this, *PlayerState, Source, TEXT("The actual ServerOnly GA submits the source"))) return false;
	Fixture.DamageEquals(*this, TEXT("Submission waits for the authority boundary"), 1.0f);
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("The committed source adds exactly twenty percent"), 1.2f);
	const auto* Profile = Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1);
	if (!Profile) return false;
	const uint32 Revision = Profile->Revision;
	UAbilitySystemComponent* AbilitySystem = PlayerState->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("The Commander owns an ASC"), AbilitySystem)) return false;

	TestTrue(TEXT("Destroying the occupied Pawn succeeds"), FirstPawn->Destroy());
	auto* SecondPawn = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("A replacement Pawn exists"), SecondPawn)) return false;
	Controller->Possess(SecondPawn);
	TestTrue(TEXT("The real controller possesses the replacement Pawn"), Controller->GetPawn() == SecondPawn);
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("Pawn destruction and replacement preserve the World source"), 1.2f);
	TestTrue(TEXT("Pawn replacement retains the same PlayerState ASC"), AbilitySystem == PlayerState->GetAbilitySystemComponent());
	TestTrue(TEXT("Army ability owner remains the PlayerState"), AbilitySystem->GetOwnerActor() == PlayerState);
	TestTrue(TEXT("Army ability avatar remains the PlayerState, not the destroyed Pawn"), AbilitySystem->GetAvatarActor() == PlayerState);

	// ExecuteArmySkillCommand rebinds the actual ASC each time; the same source ID must stay idempotent.
	for (int32 Index = 0; Index < 3; ++Index)
		if (!Execute(*this, *PlayerState, Source, TEXT("Repeated ASC initialization can update the same source"))) return false;
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("Repeated ASC binding and source upsert never multiply the same source"), 1.2f);
	Profile = Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1);
	if (TestNotNull(TEXT("The rebound profile remains visible"), Profile))
		TestEqual(TEXT("Unchanged final configuration retains its revision"), Profile->Revision, Revision);
	int32 ArmyAbilityCount = 0;
	for (const auto& Spec : AbilitySystem->GetActivatableAbilities())
		if (Spec.Ability && Spec.Ability->IsA<UGuLiArmySkillAbility>()) ++ArmyAbilityCount;
	TestEqual(TEXT("ASC reinitialization grants the command ability only once"), ArmyAbilityCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillWM01OwnershipModelTest,
	"GuLiStrike.Skills.Lifecycle.WM01UsesProfilesNotExtraAbilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillWM01OwnershipModelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiSkillLifecycleTests;
	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	APlayerController* Controller = nullptr;
	AGuLiBattlePlayerState* Commander = Fixture.SpawnCommander(*this, Controller);
	if (!Commander) return false;
	if (!Commander->HasActorBegunPlay())
	{
		Commander->DispatchBeginPlay();
	}

	const FGuLiResolvedSkillProfile* SoldierA =
		Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1u, TEXT("BasicAttack"));
	const FGuLiResolvedSkillProfile* WM01 =
		Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 2u, TEXT("BasicAttack"));
	if (!TestNotNull(TEXT("Soldier A resolves one active BasicAttack profile"), SoldierA)
		|| !TestNotNull(TEXT("WM01 resolves one active BasicAttack profile"), WM01))
	{
		return false;
	}
	TestEqual(TEXT("WM01 reuses the Strafe behavior definition"),
		WM01->SkillId, FName(TEXT("Strafe")));
	TestEqual(TEXT("WM01 integration damage remains 7.5"), WM01->Damage, 7.5f);
	TestEqual(TEXT("WM01 integration cadence remains four shots per second"),
		WM01->AttackRatePerSecond, 4.0f);
	TestEqual(TEXT("WM01 integration range remains 150 metres"),
		WM01->RangeCentimeters, 15000.0f);

	int32 RedBasicAttackProfiles = 0;
	for (const FGuLiResolvedSkillProfile& Profile : Fixture.Bridge->GetResolvedSkills())
	{
		if (Profile.Team == EGuLiTeam::Red
			&& Profile.SlotId == FName(TEXT("BasicAttack")))
		{
			++RedBasicAttackProfiles;
		}
	}
	TestEqual(TEXT("Two Soldier types produce two active Red-team BasicAttack profiles"),
		RedBasicAttackProfiles, 2);

	UAbilitySystemComponent* AbilitySystem = Commander->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("The Commander owns its army ASC"), AbilitySystem)) return false;
	int32 ArmyAbilityCount = 0;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->IsA<UGuLiArmySkillAbility>())
		{
			++ArmyAbilityCount;
		}
	}
	TestEqual(TEXT("Adding WM01 profiles does not grant a second ArmySkill GA"),
		ArmyAbilityCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWeaponEquipmentTransactionTest,
	"GuLiStrike.Skills.Lifecycle.WeaponEquipmentTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWeaponEquipmentTransactionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiSkillLifecycleTests;
	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	APlayerController* Controller = nullptr;
	AGuLiBattlePlayerState* Commander = Fixture.SpawnCommander(*this, Controller);
	if (!Commander) return false;
	if (!Commander->HasActorBegunPlay()) Commander->DispatchBeginPlay();

	TArray<FGuLiWeaponChannelView> Views = Commander->GetWeaponChannels(EGuLiWeaponDomain::Army);
	const auto FindBasicAttack = [&Views]()
	{
		return Views.FindByPredicate([](const FGuLiWeaponChannelView& View)
			{ return View.Binding.SubjectId == FName(TEXT("1")) && View.Binding.SlotId == FName(TEXT("BasicAttack")); });
	};
	const FGuLiWeaponChannelView* InitialView = FindBasicAttack();
	if (!TestNotNull(TEXT("The Commander receives its committed Army weapon view"), InitialView)) return false;
	const FGuLiWeaponBindingKey Binding = InitialView->Binding;
	const FName OriginalSkill = InitialView->SkillId;
	const int64 InitialRevision = InitialView->LoadoutRevision;
	TestTrue(TEXT("The initial binding is valid and equipped"),
		Binding.IsWellFormed() && InitialView->bUnlocked && InitialView->bEquipped);

	const FGuid UnequipRequest(0, 0, 0, 100);
	FGuLiWeaponChangeResult Result = Commander->ProcessEquipWeaponRequest(
		UnequipRequest, Binding, NAME_None, InitialRevision);
	TestEqual(TEXT("A valid request waits for the fixed-step commit"),
		Result.Status, EGuLiWeaponChangeStatus::AwaitingCommit);
	TestEqual(TEXT("The request reports the still-committed version while pending"),
		Result.LoadoutRevision, InitialRevision);

	Result = Commander->ProcessEquipWeaponRequest(UnequipRequest, Binding, NAME_None, InitialRevision);
	TestEqual(TEXT("An identical request ID replays the cached pending result"),
		Result.Status, EGuLiWeaponChangeStatus::AwaitingCommit);
	Result = Commander->ProcessEquipWeaponRequest(UnequipRequest, Binding, OriginalSkill, InitialRevision);
	TestEqual(TEXT("The same request ID cannot be reused for different equipment"),
		Result.Status, EGuLiWeaponChangeStatus::Rejected);

	Fixture.Bridge->CommitPendingChanges();
	const AGuLiBattlePlayerState::FWeaponRequestRecord* CommittedRequest =
		Commander->WeaponRequests.Find(UnequipRequest);
	if (!TestNotNull(TEXT("The accepted request remains in the bounded result cache"), CommittedRequest)) return false;
	Result = CommittedRequest->Result;
	TestEqual(TEXT("The original accepted request receives its committed result"),
		Result.Status, EGuLiWeaponChangeStatus::Committed);
	TestTrue(TEXT("A changed equipment snapshot advances the complete loadout version"),
		Result.LoadoutRevision > InitialRevision);
	Views = Commander->GetWeaponChannels(EGuLiWeaponDomain::Army);
	const FGuLiWeaponChannelView* UnequippedView = FindBasicAttack();
	if (!TestNotNull(TEXT("The unequipped stable slot remains visible"), UnequippedView)) return false;
	TestTrue(TEXT("The committed view distinguishes unlocked from equipped"),
		UnequippedView->bUnlocked && !UnequippedView->bEquipped);
	TestEqual(TEXT("The committed view and result expose the same loadout version"),
		UnequippedView->LoadoutRevision, Result.LoadoutRevision);

	FString Error;
	TestFalse(TEXT("An old version cannot overwrite the newer equipment snapshot"),
		Fixture.Bridge->EquipWeapon(*Commander, 1u, TEXT("BasicAttack"), OriginalSkill,
			static_cast<uint32>(InitialRevision), Error));
	TestTrue(TEXT("The stale rejection is diagnosable"), Error.Contains(TEXT("stale")));
	const uint32 BeforeReequipRevision = Fixture.Bridge->GetLoadoutRevision();
	if (!TestTrue(TEXT("The current Commander may re-equip a compatible weapon"),
		Fixture.Bridge->EquipWeapon(*Commander, 1u, TEXT("BasicAttack"), OriginalSkill,
			BeforeReequipRevision, Error)))
	{
		AddError(Error);
		return false;
	}
	const FGuLiResolvedSkillProfile* PendingProfile = Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1u);
	TestTrue(TEXT("The previous view remains committed until the authority step"),
		PendingProfile && !PendingProfile->bEquipped);
	Fixture.Bridge->CommitPendingChanges();
	const FGuLiResolvedSkillProfile* Reequipped = Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1u);
	TestTrue(TEXT("The compatible weapon is equipped after commit"), Reequipped && Reequipped->bEquipped);
	TestTrue(TEXT("Re-equipping advances the loadout version again"),
		Fixture.Bridge->GetLoadoutRevision() > BeforeReequipRevision);

	APlayerController* ObserverController = Fixture.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("An authority controller exists for the unassigned PlayerState"), ObserverController)) return false;
	FActorSpawnParameters ObserverParameters;
	ObserverParameters.Owner = ObserverController;
	ObserverParameters.ObjectFlags |= RF_Transient;
	AGuLiBattlePlayerState* Observer = Fixture.World->SpawnActor<AGuLiBattlePlayerState>(ObserverParameters);
	if (!TestNotNull(TEXT("An unassigned PlayerState exists for the authority check"), Observer)) return false;
	ObserverController->SetPlayerState(Observer);
	const FGuLiWeaponChangeResult ObserverResult = Observer->ProcessEquipWeaponRequest(
		FGuid(0, 0, 0, 101), Binding, NAME_None, Fixture.Bridge->GetLoadoutRevision());
	TestEqual(TEXT("A PlayerState that does not own the team binding is rejected"),
		ObserverResult.Status, EGuLiWeaponChangeStatus::Rejected);
	FGuLiWeaponBindingKey ForeignEpoch = Binding;
	++ForeignEpoch.MatchEpoch;
	const FGuLiWeaponChangeResult ForeignEpochResult = Commander->ProcessEquipWeaponRequest(
		FGuid(0, 0, 0, 102), ForeignEpoch, NAME_None, Fixture.Bridge->GetLoadoutRevision());
	TestEqual(TEXT("A binding from another match epoch is rejected"),
		ForeignEpochResult.Status, EGuLiWeaponChangeStatus::Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillCommanderLifecycleTest,
	"GuLiStrike.Skills.Lifecycle.CommanderReplacementAndStaleHost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillCommanderLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillLifecycleTests;
	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	APlayerController* OldController = nullptr;
	auto* OldCommander = Fixture.SpawnCommander(*this, OldController);
	if (!OldCommander || !Execute(*this, *OldCommander, SourceCommand(1), TEXT("The original commander submits a team source"))) return false;
	Fixture.Bridge->CommitPendingChanges();
	const uint8 Seat = OldCommander->GetBattleSlotIndex();
	const FGuid OldGuid = OldCommander->GetPlayerGuid();
	Fixture.State->ReleaseRoleSlot(Seat, OldGuid);
	APlayerController* NewController = nullptr;
	auto* NewCommander = Fixture.SpawnCommander(*this, NewController);
	if (!NewCommander) return false;
	TestTrue(TEXT("The new occupant has a distinct player identity"), NewCommander->GetPlayerGuid() != OldGuid);
	Fixture.DamageEquals(*this, TEXT("Changing the seat occupant does not remove the former commander's team source"), 1.2f);

	FString Error;
	TestFalse(TEXT("The old ready PlayerState cannot change sources after its seat is reassigned"),
		OldCommander->ExecuteArmySkillCommand(SourceCommand(3, .9f), Error));
	TestFalse(TEXT("The rejected stale commander receives a diagnostic"), Error.IsEmpty());
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("A stale-host attempt leaves the committed team profile intact"), 1.2f);
	if (!Execute(*this, *NewCommander, SourceCommand(2), TEXT("The newly authorized commander can add a second source"))) return false;
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("New and retained team sources compose across a commander change"), 1.44f);

	// Exercise the stale logout guard as well as actual host/ASC component destruction.
	Fixture.State->ReleaseRoleSlot(Seat, OldGuid);
	OldController->SetPlayerState(nullptr);
	TestTrue(TEXT("Destroying the old PlayerState and ASC succeeds"), OldCommander->Destroy());
	TestTrue(TEXT("Destroying the old controller succeeds"), OldController->Destroy());
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("Old host destruction cannot revoke sources now owned by the World ledger"), 1.44f);
	FGuLiArmySkillCommand Remove;
	Remove.Command = EGuLiArmySkillCommand::RemoveSource;
	Remove.SourceInstanceId = FGuid(0, 0, 0, 1);
	if (!Execute(*this, *NewCommander, Remove, TEXT("The new commander can revoke the old commander's retained source"))) return false;
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("Revoking the retained source leaves only the new commander's source"), 1.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillMatchLifecycleTest,
	"GuLiStrike.Skills.Lifecycle.NewMatchEpochClearsLedger", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillMatchLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiSkillLifecycleTests;
	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	APlayerController* Controller = nullptr;
	auto* Commander = Fixture.SpawnCommander(*this, Controller);
	if (!Commander || !Execute(*this, *Commander, SourceCommand(1), TEXT("The old match has a source"))) return false;
	FGuLiArmySkillCommand Override;
	Override.Command = EGuLiArmySkillCommand::SetNumericOverride;
	Override.NumericOverride.bOverrideDamage = true;
	Override.NumericOverride.Damage = Fixture.BaseDamage * 3.0f;
	if (!Execute(*this, *Commander, Override, TEXT("The old match also has a final GM override"))) return false;
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("The old match's override is committed"), 3.0f);
	FString EquipmentError;
	if (!TestTrue(TEXT("The old match can hold an explicit unequipped slot"),
		Fixture.Bridge->EquipWeapon(*Commander, 1u, TEXT("BasicAttack"), NAME_None,
			Fixture.Bridge->GetLoadoutRevision(), EquipmentError)))
	{
		AddError(EquipmentError);
		return false;
	}
	Fixture.Bridge->CommitPendingChanges();
	const FGuLiResolvedSkillProfile* UnequippedProfile =
		Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1u, TEXT("BasicAttack"));
	TestTrue(TEXT("The old match commits its explicit unequipped choice"),
		UnequippedProfile && !UnequippedProfile->bEquipped);
	if (!Execute(*this, *Commander, SourceCommand(2), TEXT("The old match can also have an uncommitted source"))) return false;

	// Publicly install a newly initialized BattleGameState. No private epoch field or ledger is mutated.
	const uint32 OldEpoch = Fixture.State->GetMatchEpoch();
	auto* OldState = Fixture.State;
	auto* NewState = Fixture.World->SpawnActor<AGuLiBattleGameState>();
	if (!TestNotNull(TEXT("The next match has a new authoritative BattleGameState"), NewState)) return false;
	NewState->InitializeServerMatchState();
	if (!TestTrue(TEXT("The new GameState supplies a distinct nonzero epoch"),
		NewState->GetMatchEpoch() != 0 && NewState->GetMatchEpoch() != OldEpoch)) return false;
	Fixture.World->SetGameState(NewState);
	Fixture.State = NewState;
	TestTrue(TEXT("The World retains the same bridge across an in-world match epoch change"),
		Fixture.World->GetSubsystem<UGuLiArmySkillSubsystem>() == Fixture.Bridge);
	TestNull(TEXT("An old match's committed profile is hidden before the next authority commit"),
		Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1));
	TestTrue(TEXT("Bulk readers also cannot observe old-epoch profiles"), Fixture.Bridge->GetResolvedSkills().IsEmpty());
	FString Error;
	TestFalse(TEXT("Old readiness without a new match seat does not authorize the commander"),
		Commander->ExecuteArmySkillCommand(SourceCommand(3), Error));
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("A new match clears committed and pending sources plus GM overrides"), 1.0f);
	const FGuLiResolvedSkillProfile* NewMatchProfile =
		Fixture.Bridge->FindResolvedSkill(EGuLiTeam::Red, 1u, TEXT("BasicAttack"));
	TestTrue(TEXT("A new match also clears the old explicit equipment choice"),
		NewMatchProfile && NewMatchProfile->bEquipped);
	TestFalse(TEXT("The old match's source detail is absent"),
		Fixture.Bridge->ExplainResolvedSkill(EGuLiTeam::Red, 1).Contains(TEXT("LifecycleSource")));
	if (!Fixture.ClaimReadySeat(*this, *Commander)) return false;
	if (!Execute(*this, *Commander, SourceCommand(1), TEXT("The reauthorized commander may reuse a source ID in the new match"))) return false;
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("Reusing an old source ID creates exactly one new-match contribution"), 1.2f);
	TestTrue(TEXT("Destroying the previous GameState succeeds"), OldState->Destroy());
	Fixture.Bridge->CommitPendingChanges();
	Fixture.DamageEquals(*this, TEXT("Destroying the old GameState cannot erase new-match state"), 1.2f);
	return true;
}
#endif
