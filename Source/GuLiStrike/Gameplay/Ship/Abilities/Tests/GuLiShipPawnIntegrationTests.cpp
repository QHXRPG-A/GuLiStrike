#include "Gameplay/Ship/GuLiStrikeShip.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipComponentOwnershipTest,
	"GuLiStrike.Ship.Abilities.ComponentOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiShipComponentOwnershipTest::RunTest(const FString&)
{
	auto* Ship = GetMutableDefault<AGuLiStrikeShip>();
	auto* State = GetMutableDefault<AGuLiBattlePlayerState>();
	TestNotNull(TEXT("Ship owns a fixed assembly component"), Ship->GetShipAssembly());
	TestTrue(TEXT("Assembly belongs directly to Ship"), Ship->GetShipAssembly()->GetOwner() == Ship);
	TestNull(TEXT("Spawn does not grant a hangar"), Ship->GetHangarCapability());
	TestNotNull(TEXT("PlayerState owns match build facts"), State->GetShipBuild());
	TestTrue(TEXT("Build facts belong directly to PlayerState"), State->GetShipBuild()->GetOwner() == State);
	TestNotNull(TEXT("Common health remains independent of capability ownership"), Ship->GetCombatHealthComponent());
	for (UObject* Owner : {static_cast<UObject*>(Ship), static_cast<UObject*>(State)})
	{
		TArray<UObject*> Subobjects; Owner->GetDefaultSubobjects(Subobjects);
		for (const UObject* Subobject : Subobjects)
			TestFalse(TEXT("No GAS default subobject survives"), Subobject->GetClass()->GetName().Contains(TEXT("AbilitySystem")));
	}
	return true;
}
#endif
