// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/GuLiStrikeShip.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"
#include "Misc/AutomationTest.h"

namespace GuLiShipPawnIntegrationTests
{
	struct FDefaultSubobjectCounts
	{
		int32 AbilitySystemComponents = 0;
		int32 ShipAbilitySystemComponents = 0;
		int32 CombatHealthComponents = 0;
		int32 AttributeSets = 0;
	};

	FDefaultSubobjectCounts CountRelevantDefaultSubobjects(UObject& Owner)
	{
		TArray<UObject*> DefaultSubobjects;
		Owner.GetDefaultSubobjects(DefaultSubobjects);
		FDefaultSubobjectCounts Counts;
		for (const UObject* Subobject : DefaultSubobjects)
		{
			if (Subobject && Subobject->IsA<UAbilitySystemComponent>())
			{
				++Counts.AbilitySystemComponents;
			}
			if (Subobject && Subobject->IsA<UGuLiShipAbilitySystemComponent>())
			{
				++Counts.ShipAbilitySystemComponents;
			}
			if (Subobject && Subobject->IsA<UGuLiCombatHealthComponent>())
			{
				++Counts.CombatHealthComponents;
			}
			if (Subobject && Subobject->IsA<UAttributeSet>())
			{
				++Counts.AttributeSets;
			}
		}
		return Counts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipPawnAbilitySystemOwnershipTest,
	"GuLiStrike.Ship.Abilities.PawnOwnedASCIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipPawnAbilitySystemOwnershipTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipPawnIntegrationTests;

	UClass* ShipClass = AGuLiStrikeShip::StaticClass();
	if (!TestNotNull(TEXT("Ship class is reflected even though it is abstract"), ShipClass))
	{
		return false;
	}
	TestTrue(TEXT("Ship class implements IAbilitySystemInterface"),
		ShipClass->ImplementsInterface(UAbilitySystemInterface::StaticClass()));

	AGuLiStrikeShip* ShipCDO = ShipClass->GetDefaultObject<AGuLiStrikeShip>();
	if (!TestNotNull(TEXT("Abstract Ship has a testable CDO"), ShipCDO))
	{
		return false;
	}

	UGuLiShipAbilitySystemComponent* ShipASC = ShipCDO->GetShipAbilitySystemComponent();
	if (!TestNotNull(TEXT("Ship CDO owns its custom ASC default subobject"), ShipASC))
	{
		return false;
	}
	TestTrue(TEXT("IAbilitySystemInterface returns the Pawn-owned custom Ship ASC"),
		ShipCDO->GetAbilitySystemComponent() == ShipASC);
	TestTrue(TEXT("Ship ASC is directly owned by the Ship CDO"),
		ShipASC->GetOuter() == ShipCDO && ShipASC->GetOwner() == ShipCDO);

	UGuLiCombatHealthComponent* CombatHealth = ShipCDO->GetCombatHealthComponent();
	if (!TestNotNull(TEXT("Ship CDO owns its custom authoritative Health component"), CombatHealth))
	{
		return false;
	}
	TestTrue(TEXT("Custom Health is directly owned by the Ship CDO"),
		CombatHealth->GetOuter() == ShipCDO && CombatHealth->GetOwner() == ShipCDO);

	const FDefaultSubobjectCounts ShipCounts = CountRelevantDefaultSubobjects(*ShipCDO);
	TestEqual(TEXT("Ship CDO has exactly one AbilitySystemComponent"),
		ShipCounts.AbilitySystemComponents, 1);
	TestEqual(TEXT("That ASC is exactly one UGuLiShipAbilitySystemComponent"),
		ShipCounts.ShipAbilitySystemComponents, 1);
	TestEqual(TEXT("Ship CDO has exactly one custom CombatHealth component"),
		ShipCounts.CombatHealthComponents, 1);
	TestEqual(TEXT("Ship CDO has no AttributeSet default subobject"), ShipCounts.AttributeSets, 0);
	TestTrue(TEXT("Ship ASC has no spawned AttributeSet"), ShipASC->GetSpawnedAttributes().IsEmpty());

	AGuLiBattlePlayerState* PlayerStateCDO = GetMutableDefault<AGuLiBattlePlayerState>();
	if (!TestNotNull(TEXT("Battle PlayerState CDO exists"), PlayerStateCDO))
	{
		return false;
	}
	UAbilitySystemComponent* ArmyASC = PlayerStateCDO->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("PlayerState retains its Army ASC default subobject"), ArmyASC))
	{
		return false;
	}
	TestTrue(TEXT("Army ASC is a different component from the Pawn-owned Ship ASC"), ArmyASC != ShipASC);
	TestFalse(TEXT("Army ASC is not the custom Ship ASC type"),
		ArmyASC->IsA<UGuLiShipAbilitySystemComponent>());
	TestTrue(TEXT("Army ASC remains directly owned by the PlayerState CDO"),
		ArmyASC->GetOuter() == PlayerStateCDO && ArmyASC->GetOwner() == PlayerStateCDO);
	TestEqual(TEXT("Army ASC keeps its distinct default-subobject name"),
		ArmyASC->GetFName(), FName(TEXT("ArmyAbilitySystem")));

	const FDefaultSubobjectCounts PlayerStateCounts = CountRelevantDefaultSubobjects(*PlayerStateCDO);
	TestEqual(TEXT("PlayerState CDO has exactly one independent Army AbilitySystemComponent"),
		PlayerStateCounts.AbilitySystemComponents, 1);
	TestEqual(TEXT("PlayerState CDO does not contain a Pawn-owned Ship ASC"),
		PlayerStateCounts.ShipAbilitySystemComponents, 0);
	return true;
}

#endif
