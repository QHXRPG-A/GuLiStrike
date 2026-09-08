// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilities.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"

namespace GuLiShipAbilityTests
{
	struct FShipASCFixture
	{
		UWorld* World = nullptr;
		AActor* Ship = nullptr;
		UGuLiShipAbilitySystemComponent* ASC = nullptr;
		UGuLiShipAbilitySet* AbilitySet = nullptr;
		bool bWorldContextRegistered = false;

		~FShipASCFixture()
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
			World = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for Ship ASC test World"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Isolated authority World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			Ship = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Authority Ship surrogate exists"), Ship))
			{
				return false;
			}
			ASC = NewObject<UGuLiShipAbilitySystemComponent>(Ship, TEXT("ShipAbilitySystem"));
			if (!Test.TestNotNull(TEXT("Pawn-owned Ship ASC exists"), ASC))
			{
				return false;
			}
			Ship->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitializeShipActorInfo(Ship);
			AbilitySet = UGuLiShipAbilitySet::CreateNativeV1Transient(ASC);
			return Test.TestNotNull(TEXT("Native v1 ability set exists"), AbilitySet);
		}

		static int32 CountShipAbilities(const UGuLiShipAbilitySystemComponent& InASC)
		{
			int32 Count = 0;
			for (const FGameplayAbilitySpec& Spec : InASC.GetActivatableAbilities())
			{
				if (Spec.Ability && Spec.Ability->IsA<UGuLiShipGameplayAbility>())
				{
					++Count;
				}
			}
			return Count;
		}

		static int32 CountActivePersistentAbilities(const UGuLiShipAbilitySystemComponent& InASC)
		{
			int32 Count = 0;
			for (const FGameplayAbilitySpec& Spec : InASC.GetActivatableAbilities())
			{
				const UGuLiShipGameplayAbility* Ability = Spec.Ability
					? Cast<UGuLiShipGameplayAbility>(Spec.Ability)
					: nullptr;
				if (Ability
					&& Ability->GetShipActivationPolicy() == EGuLiShipAbilityActivationPolicy::WhileGranted
					&& Spec.IsActive())
				{
					++Count;
				}
			}
			return Count;
		}
	};

	FGuLiShipAbilityProjectionContext ProjectionContext()
	{
		FGuLiShipAbilityProjectionContext Context;
		Context.ShipInstanceId = FGuid(1, 2, 3, 4);
		Context.MatchEpoch = 17u;
		Context.Team = EGuLiTeam::Red;
		Context.OwnerPlayerGuid = FGuid(5, 6, 7, 8);
		Context.WingmanTypeId = GuLiGetDefaultWingmanTypeId();
		Context.ShipGeneration = 11u;
		Context.GroupGeneration = 7u;
		Context.FormationCommandRevision = 1u;
		Context.EffectiveClientSimTick = 90u;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipAbilitySetContractTest,
	"GuLiStrike.Ship.Abilities.AbilitySetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipAbilitySetContractTest::RunTest(const FString& Parameters)
{
	UGuLiShipAbilitySet* Set = UGuLiShipAbilitySet::CreateNativeV1Transient(GetTransientPackage());
	if (!TestNotNull(TEXT("Native v1 set can be created without an asset package"), Set))
	{
		return false;
	}
	FString Error;
	TestTrue(FString::Printf(TEXT("Native v1 set validates: %s"), *Error), Set->IsWellFormed(&Error));

	FGuLiShipAbilityLoadoutState Loadout = FGuLiShipAbilityLoadoutState::MakeNativeV1();
	TArray<FGuLiShipAbilityGrant> Resolved;
	if (!TestTrue(TEXT("Native v1 loadout resolves exactly one entry per slot"),
		Set->ResolveLoadout(Loadout, Resolved, &Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("V1 loadout contains exactly three group-level abilities"), Resolved.Num(), 3);
	if (Resolved.Num() == 3)
	{
		TestEqual(TEXT("Formation is first in canonical slot order"), Resolved[0].Slot, EGuLiShipAbilitySlot::Formation);
		TestEqual(TEXT("Basic weapon is second in canonical slot order"), Resolved[1].Slot, EGuLiShipAbilitySlot::BasicWeapon);
		TestEqual(TEXT("Missile is third in canonical slot order"), Resolved[2].Slot, EGuLiShipAbilitySlot::Missile);
	}
	TestTrue(TEXT("Ability-set checksum is nonzero"), Set->ComputeLoadoutChecksum(Loadout) != 0u);

	FGuLiShipAbilityLoadoutState Duplicate = Loadout;
	Duplicate.AbilityIds.Add(TAG_GuLi_ShipAbility_Formation_DoubleRing);
	Error.Reset();
	TestFalse(TEXT("Duplicate stable IDs are rejected before any GAS grant"),
		Set->ResolveLoadout(Duplicate, Resolved, &Error));
	TestTrue(TEXT("Duplicate rejection is diagnosable"), Error.Contains(TEXT("duplicate")));

	FGuLiShipAbilityLoadoutState MissingFormation = Loadout;
	MissingFormation.AbilityIds.Remove(TAG_GuLi_ShipAbility_Formation_DoubleRing);
	Error.Reset();
	TestFalse(TEXT("A loadout without a formation slot cannot become Active"),
		Set->ResolveLoadout(MissingFormation, Resolved, &Error));
	TestTrue(TEXT("Missing formation rejection names a required slot"), Error.Contains(TEXT("missing required slot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGroupAbilityConfigHashTest,
	"GuLiStrike.Ship.Abilities.GroupConfigHashAndInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGroupAbilityConfigHashTest::RunTest(const FString& Parameters)
{
	UGuLiShipAbilitySet* Set = UGuLiShipAbilitySet::CreateNativeV1Transient(GetTransientPackage());
	if (!TestNotNull(TEXT("Native definitions exist for snapshot checksum test"), Set))
	{
		return false;
	}
	const FGuLiShipAbilityLoadoutState Loadout = FGuLiShipAbilityLoadoutState::MakeNativeV1();
	TArray<FGuLiShipAbilityGrant> Grants;
	if (!Set->ResolveLoadout(Loadout, Grants) || Grants.Num() != 3)
	{
		AddError(TEXT("Native grants did not resolve."));
		return false;
	}

	FGuLiGroupAbilityConfigSnapshot Snapshot;
	const FGuLiShipAbilityProjectionContext Context = GuLiShipAbilityTests::ProjectionContext();
	Snapshot.ShipInstanceId = Context.ShipInstanceId;
	Snapshot.ShipGeneration = Context.ShipGeneration;
	Snapshot.GroupGeneration = Context.GroupGeneration;
	Snapshot.AbilitySetRevision = 3u;
	Snapshot.SnapshotRevision = 5u;
	Snapshot.bGroupAbilitiesValid = true;
	Snapshot.FormationAbilityId = Grants[0].AbilityId;
	Snapshot.BasicWeaponAbilityId = Grants[1].AbilityId;
	Snapshot.MissileAbilityId = Grants[2].AbilityId;
	Snapshot.FormationDefinitionRevision = Grants[0].GetDefinitionRevision();
	Snapshot.FormationDefinitionChecksum = Grants[0].GetDefinitionChecksum();
	Snapshot.BasicWeaponDefinitionRevision = Grants[1].GetDefinitionRevision();
	Snapshot.BasicWeaponDefinitionChecksum = Grants[1].GetDefinitionChecksum();
	Snapshot.MissileDefinitionRevision = Grants[2].GetDefinitionRevision();
	Snapshot.MissileDefinitionChecksum = Grants[2].GetDefinitionChecksum();
	Snapshot.FormationCommandRevision = Context.FormationCommandRevision;
	Snapshot.EffectiveClientSimTick = Context.EffectiveClientSimTick;
	Snapshot.RefreshHash();
	TestTrue(TEXT("Complete v7 group config validates its deterministic hash"), Snapshot.IsWellFormed());
	TestTrue(TEXT("Complete v7 group config is usable by a Lease Owner"), Snapshot.IsUsableByLeaseOwner());

	const uint64 OriginalHash = Snapshot.SnapshotHash;
	FGuLiGroupAbilityConfigSnapshot Copy = Snapshot;
	Copy.RefreshHash();
	TestEqual(TEXT("Identical config produces the same hash"), Copy.SnapshotHash, OriginalHash);
	Copy.EffectiveClientSimTick++;
	TestFalse(TEXT("Mutation without hash refresh is rejected"), Copy.IsWellFormed());
	Copy.RefreshHash();
	TestTrue(TEXT("Refreshed mutation is structurally valid"), Copy.IsWellFormed());
	TestTrue(TEXT("Effective-tick mutation changes hash"), Copy.SnapshotHash != OriginalHash);
	Copy = Snapshot;
	Copy.FormationRuntime.CruiseSpeedCentimetersPerSecond += 1.0f;
	TestFalse(TEXT("Projected runtime tuning is protected by the snapshot hash"), Copy.IsWellFormed());
	Copy.RefreshHash();
	TestTrue(TEXT("A valid tuning change can be published with a new hash"), Copy.IsWellFormed());
	TestTrue(TEXT("Runtime tuning mutation changes hash"), Copy.SnapshotHash != OriginalHash);

	FGuLiGroupAbilityConfigSnapshot Tombstone;
	Tombstone.ShipInstanceId = Context.ShipInstanceId;
	Tombstone.ShipGeneration = Context.ShipGeneration;
	Tombstone.GroupGeneration = Context.GroupGeneration;
	Tombstone.AbilitySetRevision = Snapshot.AbilitySetRevision;
	Tombstone.SnapshotRevision = Snapshot.SnapshotRevision + 1u;
	Tombstone.bGroupAbilitiesValid = false;
	Tombstone.FormationCommandRevision = Context.FormationCommandRevision;
	Tombstone.EffectiveClientSimTick = Context.EffectiveClientSimTick;
	Tombstone.RefreshHash();
	TestTrue(TEXT("An explicit field-cleared invalidation tombstone is structurally valid"), Tombstone.IsWellFormed());
	TestFalse(TEXT("An invalidation tombstone cannot activate a group"), Tombstone.IsUsableByLeaseOwner());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipASCGrantProjectionTest,
	"GuLiStrike.Ship.Abilities.ASCGrantProjectionAndInputLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipASCGrantProjectionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipAbilityTests;
	FShipASCFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AActor* ClientProxyShip = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("A simulated client-proxy Ship surrogate exists"), ClientProxyShip))
	{
		return false;
	}
	ClientProxyShip->SetRole(ROLE_SimulatedProxy);
	auto* NeverAuthorityInitializedASC = NewObject<UGuLiShipAbilitySystemComponent>(
		ClientProxyShip, TEXT("NeverAuthorityInitializedShipAbilitySystem"));
	if (!TestNotNull(TEXT("An uninitialized Ship ASC can be constructed for the teardown guard test"),
		NeverAuthorityInitializedASC))
	{
		return false;
	}
	ClientProxyShip->AddInstanceComponent(NeverAuthorityInitializedASC);
	NeverAuthorityInitializedASC->RegisterComponent();
	NeverAuthorityInitializedASC->InitializeShipActorInfo(ClientProxyShip);
	NeverAuthorityInitializedASC->SetLooseGameplayTagCount(
		TAG_GuLi_ShipAbility_State_MissileCooldown,
		1,
		EGameplayTagReplicationState::TagOnly);
	NeverAuthorityInitializedASC->ServerClearShipAbilities();
	TestTrue(TEXT("A client-proxy ASC cannot use the authority-only EndPlay teardown capability"),
		NeverAuthorityInitializedASC->IsMissileCooldownActive());
	ClientProxyShip->SetRole(ROLE_Authority);

	TestTrue(TEXT("Ship is both ASC owner and avatar"),
		Fixture.ASC->GetOwnerActor() == Fixture.Ship && Fixture.ASC->GetAvatarActor() == Fixture.Ship);
	TestTrue(TEXT("Ship ASC starts with no AttributeSet"), Fixture.ASC->GetSpawnedAttributes().IsEmpty());
	TestTrue(TEXT("Projection context changes once"), Fixture.ASC->SetProjectionContext(ProjectionContext()));
	const uint32 ContextRevision = Fixture.ASC->GetProjectionSnapshotRevision();
	TestFalse(TEXT("Repeating identical projection context is a no-op"),
		Fixture.ASC->SetProjectionContext(ProjectionContext()));
	TestEqual(TEXT("No-op context does not advance SnapshotRevision"),
		Fixture.ASC->GetProjectionSnapshotRevision(), ContextRevision);

	FString Error;
	FGuLiShipAbilityLoadoutState Loadout = FGuLiShipAbilityLoadoutState::MakeNativeV1();
	if (!TestTrue(FString::Printf(TEXT("Authority grants v1 loadout: %s"), *Error),
		Fixture.ASC->ServerApplyAbilitySet(Fixture.AbilitySet, Loadout, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Exactly three Ship abilities are granted"), FShipASCFixture::CountShipAbilities(*Fixture.ASC), 3);
	TestEqual(TEXT("Exactly two persistent abilities auto-activate"),
		FShipASCFixture::CountActivePersistentAbilities(*Fixture.ASC), 2);
	TestTrue(TEXT("Granting Ship abilities still creates no AttributeSet"), Fixture.ASC->GetSpawnedAttributes().IsEmpty());

	FGuLiGroupAbilityConfigSnapshot Snapshot;
	TestTrue(TEXT("ASC builds a structurally valid projection"),
		Fixture.ASC->BuildGroupAbilityConfigSnapshot(Snapshot));
	TestTrue(TEXT("Formation/basic/missile projection is Active-ready"), Snapshot.IsUsableByLeaseOwner());
	TestEqual(TEXT("Projected ability set revision matches ASC"),
		Snapshot.AbilitySetRevision, Fixture.ASC->GetAbilitySetRevision());
	TestEqual(TEXT("Projected snapshot revision matches ASC"),
		Snapshot.SnapshotRevision, Fixture.ASC->GetProjectionSnapshotRevision());
	TestEqual(TEXT("Formation DataAsset projects the required 20 degree turn limit"),
		Snapshot.FormationRuntime.MaximumTurnRateDegreesPerSecond, 20.0f);
	TestEqual(TEXT("Basic weapon DataAsset projects the 1500m range"),
		Snapshot.BasicWeaponRuntime.RangeCentimeters, 150000.0f);
	TestEqual(TEXT("Basic weapon DataAsset projects independent two-second cadence"),
		Snapshot.BasicWeaponRuntime.CooldownSeconds, 2.0f);
	TestEqual(TEXT("Missile targeting parameters are present without ASC access"),
		Snapshot.MissileRuntime.RangeCentimeters, 250000.0f);

	const uint32 AbilityRevision = Fixture.ASC->GetAbilitySetRevision();
	const uint32 ProjectionRevision = Fixture.ASC->GetProjectionSnapshotRevision();
	Error.Reset();
	TestFalse(TEXT("Applying the identical set/loadout is an idempotent no-op"),
		Fixture.ASC->ServerApplyAbilitySet(Fixture.AbilitySet, Loadout, Error));
	TestTrue(TEXT("Idempotent no-op is not reported as an error"), Error.IsEmpty());
	TestEqual(TEXT("Idempotent grant retains exactly three specs"), FShipASCFixture::CountShipAbilities(*Fixture.ASC), 3);
	TestEqual(TEXT("Idempotent grant does not advance AbilitySetRevision"),
		Fixture.ASC->GetAbilitySetRevision(), AbilityRevision);
	TestEqual(TEXT("Idempotent grant does not advance SnapshotRevision"),
		Fixture.ASC->GetProjectionSnapshotRevision(), ProjectionRevision);

	for (int32 Index = 0; Index < 3; ++Index)
	{
		Fixture.ASC->InitializeShipActorInfo(Fixture.Ship);
	}
	TestEqual(TEXT("BeginPlay/possession-style ActorInfo refresh never duplicates grants"),
		FShipASCFixture::CountShipAbilities(*Fixture.ASC), 3);

	int32 MissileAuthorizations = 0;
	Fixture.ASC->OnTriggeredAbilityAuthorized().AddLambda(
		[&MissileAuthorizations](FGameplayAbilitySpecHandle, FGameplayTag, uint32, bool)
		{
			++MissileAuthorizations;
		});
	Fixture.ASC->AbilityInputTagPressed(TAG_GuLi_Input_Ship_Wingman_Missile);
	Fixture.ASC->AbilityInputTagReleased(TAG_GuLi_Input_Ship_Wingman_Missile);
	TestEqual(TEXT("Missile input produces one group-level authorization, not per-wingman GA activations"),
		MissileAuthorizations, 1);
	const FGuid CooldownOwner(0x10u, 0x20u, 0x30u, 0x40u);
	const FGuid WrongCooldownOwner(0x11u, 0x21u, 0x31u, 0x41u);
	TestTrue(TEXT("A valid salvo identity reserves the shared cooldown atomically"),
		Fixture.ASC->ServerTryReserveMissileCooldown(8.0f, CooldownOwner));
	TestFalse(TEXT("A second activation cannot steal the shared cooldown"),
		Fixture.ASC->ServerTryReserveMissileCooldown(8.0f, WrongCooldownOwner));
	TestFalse(TEXT("A different activation cannot roll back the reservation"),
		Fixture.ASC->ServerRollbackMissileCooldown(WrongCooldownOwner));
	TestTrue(TEXT("The original reservation remains active"), Fixture.ASC->IsMissileCooldownActive());
	TestTrue(TEXT("Only the owning activation may roll back a failed batch"),
		Fixture.ASC->ServerRollbackMissileCooldown(CooldownOwner));
	TestFalse(TEXT("Rollback leaves no shared cooldown side effect"), Fixture.ASC->IsMissileCooldownActive());
	Fixture.ASC->SetActiveAbilityInputEnabled(false);
	Fixture.ASC->AbilityInputTagPressed(TAG_GuLi_Input_Ship_Wingman_Missile);
	TestEqual(TEXT("UnPossess-style input disable blocks active missile without cancelling passives"),
		MissileAuthorizations, 1);
	TestEqual(TEXT("Persistent formation/basic abilities survive input disable"),
		FShipASCFixture::CountActivePersistentAbilities(*Fixture.ASC), 2);
	const FGuid TeardownCooldownOwner(0x12u, 0x22u, 0x32u, 0x42u);
	TestTrue(TEXT("A live cooldown may exist when Ship teardown begins"),
		Fixture.ASC->ServerTryReserveMissileCooldown(8.0f, TeardownCooldownOwner));

	Fixture.ASC->ServerClearShipAbilities();
	TestFalse(TEXT("Death teardown clears the loose cooldown tag and timer"),
		Fixture.ASC->IsMissileCooldownActive());
	TestEqual(TEXT("Death teardown clears every Ship ability spec"), FShipASCFixture::CountShipAbilities(*Fixture.ASC), 0);
	FGuLiGroupAbilityConfigSnapshot Tombstone;
	TestTrue(TEXT("Death teardown still builds a reliable invalidation payload"),
		Fixture.ASC->BuildGroupAbilityConfigSnapshot(Tombstone));
	TestTrue(TEXT("Invalidation payload is structurally valid"), Tombstone.IsWellFormed());
	TestFalse(TEXT("Invalidation payload cannot activate the old group"), Tombstone.IsUsableByLeaseOwner());
	TestTrue(TEXT("Death teardown advances SnapshotRevision"), Tombstone.SnapshotRevision > Snapshot.SnapshotRevision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanWeaponBindingProjectionTest,
	"GuLiStrike.Ship.Abilities.WingmanWeaponBindingProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanWeaponBindingProjectionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipAbilityTests;
	FShipASCFixture Fixture;
	if (!Fixture.Initialize(*this)
		|| !TestTrue(TEXT("The authoritative projection context is accepted"),
			Fixture.ASC->SetProjectionContext(ProjectionContext())))
	{
		return false;
	}

	FString Error;
	FGuLiShipAbilityLoadoutState FormationOnly;
	FormationOnly.AbilityIds.Add(TAG_GuLi_ShipAbility_Formation_DoubleRing);
	FormationOnly.Revision = 1u;
	if (!TestTrue(TEXT("A formation-only loadout is a legal committed configuration"),
		Fixture.ASC->ServerApplyAbilitySet(Fixture.AbilitySet, FormationOnly, Error)))
	{
		AddError(Error);
		return false;
	}
	FGuLiGroupAbilityConfigSnapshot FormationOnlySnapshot;
	TestTrue(TEXT("Formation-only projection builds"),
		Fixture.ASC->BuildGroupAbilityConfigSnapshot(FormationOnlySnapshot));
	TestTrue(TEXT("Formation-only projection is lease-owner usable"),
		FormationOnlySnapshot.IsUsableByLeaseOwner());
	TestEqual(TEXT("Formation-only projection contains zero weapon channels"),
		FormationOnlySnapshot.WeaponChannels.Num(), 0);

	const FGuLiShipAbilityGrant* NativeBasic =
		Fixture.AbilitySet->FindGrant(TAG_GuLi_ShipAbility_Weapon_Basic_Auto);
	const FGuLiShipAbilityGrant* NativeMissile =
		Fixture.AbilitySet->FindGrant(TAG_GuLi_ShipAbility_Weapon_Missile_Salvo);
	if (!TestNotNull(TEXT("Native basic grant exists"), NativeBasic)
		|| !TestNotNull(TEXT("Native missile grant exists"), NativeMissile))
	{
		return false;
	}
	const FGuLiShipAbilityGrant NativeBasicCopy = *NativeBasic;
	const FGuLiShipAbilityGrant NativeMissileCopy = *NativeMissile;

	FGuLiShipAbilityGrant SecondaryBasic = NativeBasicCopy;
	SecondaryBasic.AbilityId = TAG_GuLi_ShipWingman_Weapon_Basic;
	SecondaryBasic.WeaponSlotId = TEXT("SecondaryWeapon");
	SecondaryBasic.SkillId = TEXT("Test.Secondary.Auto");
	SecondaryBasic.ProfileRevision = 2u;
	Fixture.AbilitySet->Grants.Add(SecondaryBasic);

	FGuLiShipAbilityGrant SecondaryMissile = NativeMissileCopy;
	SecondaryMissile.AbilityId = TAG_GuLi_ShipWingman_Weapon_Missile;
	SecondaryMissile.WeaponSlotId = TEXT("SecondaryMissile");
	SecondaryMissile.SkillId = TEXT("Test.Secondary.Missile");
	SecondaryMissile.ProfileRevision = 2u;
	SecondaryMissile.CooldownGroupId = TEXT("TestSecondaryMissile");
	Fixture.AbilitySet->Grants.Add(SecondaryMissile);

	FGuLiShipAbilityLoadoutState MultiWeaponLoadout = FGuLiShipAbilityLoadoutState::MakeNativeV1();
	MultiWeaponLoadout.AbilityIds.Add(SecondaryBasic.AbilityId);
	MultiWeaponLoadout.AbilityIds.Add(SecondaryMissile.AbilityId);
	MultiWeaponLoadout.Revision = 2u;
	MultiWeaponLoadout.Normalize();
	Error.Reset();
	if (!TestTrue(TEXT("One formation and four weapon entries apply as one loadout"),
		Fixture.ASC->ServerApplyAbilitySet(Fixture.AbilitySet, MultiWeaponLoadout, Error)))
	{
		AddError(Error);
		return false;
	}

	FGuLiGroupAbilityConfigSnapshot Snapshot;
	if (!TestTrue(TEXT("Multi-channel projection builds"),
		Fixture.ASC->BuildGroupAbilityConfigSnapshot(Snapshot)))
	{
		return false;
	}
	TestTrue(TEXT("Multi-channel projection is structurally valid"), Snapshot.IsUsableByLeaseOwner());
	TestEqual(TEXT("Every selected weapon becomes exactly one channel"), Snapshot.WeaponChannels.Num(), 4);
	TestEqual(TEXT("Projection carries the complete loadout revision"),
		Snapshot.LoadoutRevision, Fixture.ASC->GetWeaponLoadoutRevision());

	const FGuLiWeaponBindingKey PrimaryBasicBinding = FGuLiWeaponBindingKey::Wingman(
		Snapshot.MatchEpoch, Snapshot.Team, Snapshot.OwnerPlayerGuid, Snapshot.WingmanTypeId, TEXT("BasicWeapon"));
	const FGuLiWeaponBindingKey SecondaryBasicBinding = FGuLiWeaponBindingKey::Wingman(
		Snapshot.MatchEpoch, Snapshot.Team, Snapshot.OwnerPlayerGuid, Snapshot.WingmanTypeId, TEXT("SecondaryWeapon"));
	const FGuLiWeaponBindingKey PrimaryMissileBinding = FGuLiWeaponBindingKey::Wingman(
		Snapshot.MatchEpoch, Snapshot.Team, Snapshot.OwnerPlayerGuid, Snapshot.WingmanTypeId, TEXT("Missile"));
	const FGuLiWeaponBindingKey SecondaryMissileBinding = FGuLiWeaponBindingKey::Wingman(
		Snapshot.MatchEpoch, Snapshot.Team, Snapshot.OwnerPlayerGuid, Snapshot.WingmanTypeId, TEXT("SecondaryMissile"));

	const FGuLiShipAbilityGrant* ProjectedPrimaryBasic = Fixture.ASC->FindConfiguredGrant(PrimaryBasicBinding);
	const FGuLiShipAbilityGrant* ProjectedSecondaryBasic = Fixture.ASC->FindConfiguredGrant(SecondaryBasicBinding);
	const FGuLiShipAbilityGrant* ProjectedPrimaryMissile = Fixture.ASC->FindConfiguredGrant(PrimaryMissileBinding);
	const FGuLiShipAbilityGrant* ProjectedSecondaryMissile = Fixture.ASC->FindConfiguredGrant(SecondaryMissileBinding);
	if (!TestNotNull(TEXT("Primary automatic binding resolves"), ProjectedPrimaryBasic)
		|| !TestNotNull(TEXT("Secondary automatic binding resolves"), ProjectedSecondaryBasic)
		|| !TestNotNull(TEXT("Primary missile binding resolves"), ProjectedPrimaryMissile)
		|| !TestNotNull(TEXT("Secondary missile binding resolves"), ProjectedSecondaryMissile))
	{
		return false;
	}
	TestTrue(TEXT("Two automatic channels may reuse the same GA class"),
		ProjectedPrimaryBasic->AbilityClass == ProjectedSecondaryBasic->AbilityClass);
	TestTrue(TEXT("Two missile channels may reuse the same GA class"),
		ProjectedPrimaryMissile->AbilityClass == ProjectedSecondaryMissile->AbilityClass);
	TestTrue(TEXT("Reused GA classes retain distinct stable catalog identities"),
		ProjectedPrimaryBasic->AbilityId != ProjectedSecondaryBasic->AbilityId
		&& ProjectedPrimaryMissile->AbilityId != ProjectedSecondaryMissile->AbilityId);

	int32 BindingAuthorizations = 0;
	FGuLiWeaponBindingKey AuthorizedBinding;
	Fixture.ASC->OnWeaponAbilityAuthorized().AddLambda(
		[&BindingAuthorizations, &AuthorizedBinding](FGameplayAbilitySpecHandle,
			FGuLiWeaponBindingKey Binding, FName, FGameplayTag, uint32, bool)
		{
			++BindingAuthorizations;
			AuthorizedBinding = Binding;
		});
	Fixture.ASC->AbilityInputTagPressed(TAG_GuLi_Input_Ship_Wingman_Missile);
	TestEqual(TEXT("A shared InputTag never implicitly fans out to multiple weapon channels"),
		BindingAuthorizations, 0);
	TestTrue(TEXT("Binding-exact input activates only the requested missile channel"),
		Fixture.ASC->AbilityWeaponBindingPressed(PrimaryMissileBinding));
	TestEqual(TEXT("Binding-exact input emits one authorization"), BindingAuthorizations, 1);
	TestTrue(TEXT("Authorization carries the exact requested binding"),
		AuthorizedBinding == PrimaryMissileBinding);
	TestTrue(TEXT("Binding-exact release resolves the same channel"),
		Fixture.ASC->AbilityWeaponBindingReleased(PrimaryMissileBinding));

	FGuLiShipAbilityGrant* MutableSecondaryBasic = Fixture.AbilitySet->Grants.FindByPredicate(
		[](const FGuLiShipAbilityGrant& Grant)
		{
			return Grant.AbilityId == TAG_GuLi_ShipWingman_Weapon_Basic;
		});
	if (!TestNotNull(TEXT("Secondary automatic catalog entry remains addressable"), MutableSecondaryBasic))
	{
		return false;
	}
	const FName OriginalSecondarySlot = MutableSecondaryBasic->WeaponSlotId;
	MutableSecondaryBasic->WeaponSlotId = TEXT("BasicWeapon");
	TArray<FGuLiShipAbilityGrant> Resolved;
	Error.Reset();
	TestFalse(TEXT("A loadout cannot select two weapons for the same binding slot"),
		Fixture.AbilitySet->ResolveLoadout(MultiWeaponLoadout, Resolved, &Error));
	MutableSecondaryBasic->WeaponSlotId = OriginalSecondarySlot;

	FGuLiGroupAbilityConfigSnapshot TooManyChannels = Snapshot;
	for (int32 Index = TooManyChannels.WeaponChannels.Num(); Index < GULI_MAX_WINGMAN_WEAPON_CHANNELS + 1; ++Index)
	{
		FGuLiWingmanWeaponChannelConfig Extra = TooManyChannels.WeaponChannels[0];
		Extra.Binding.SlotId = FName(*FString::Printf(TEXT("OverflowSlot%d"), Index));
		TooManyChannels.WeaponChannels.Add(Extra);
	}
	TooManyChannels.RefreshHash();
	TestFalse(TEXT("A projection with more than eight weapon channels is rejected"),
		TooManyChannels.IsWellFormed());

	FGuLiGroupAbilityConfigSnapshot ExcessiveAutomaticRate = Snapshot;
	for (FGuLiWingmanWeaponChannelConfig& Channel : ExcessiveAutomaticRate.WeaponChannels)
	{
		if (Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic)
		{
			Channel.Runtime.CooldownSeconds = 0.5f;
		}
	}
	ExcessiveAutomaticRate.RefreshHash();
	TestFalse(TEXT("The projected 40-intent-per-second automatic-fire budget is enforced"),
		ExcessiveAutomaticRate.IsWellFormed());
	return true;
}

#endif
