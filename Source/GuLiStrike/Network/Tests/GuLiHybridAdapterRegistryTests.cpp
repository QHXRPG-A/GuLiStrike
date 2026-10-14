#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridAdapterRegistryTests,
    "GuLiStrike.Network.AdapterRegistry",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiHybridAdapterRegistryTests::RunTest(const FString&)
{
    FGuLiHybridManifestData Data;FString Error;
    const FString Definition=TEXT("building/106");
    TestTrue(TEXT("A new definition reuses the existing factory capability"),
        FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,TEXT("ResourceFactory"),Error));
    FGuLiHybridStartupEntry Object;Object.Key.Kind=EGuLiHybridObjectKind::Building;Object.Definition=Definition;
    Data.Objects.Add(Object);
    auto& Spawn=Data.Rules.AddDefaulted_GetRef();Spawn.Kind=EGuLiHybridRuleKind::Spawn;
    Spawn.SpawnKind=EGuLiHybridObjectKind::Building;Spawn.TargetDefinition=Definition;
    TestTrue(TEXT("Both startup and dynamic spawn use the same declared capability"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    FGuLiHybridObjectState State;State.Key.Kind=EGuLiHybridObjectKind::Building;State.Definition=FName(*Definition);
    FGuLiHybridAdapterFamily Family;
    TestTrue(TEXT("A definition outside the old 1-6 range resolves"),FGuLiHybridAdapterRegistry::Resolve(Data,State,Family,Error));
    TestEqual(TEXT("Definition selects the declared family"),Family.Name,FName(TEXT("ResourceFactory")));
    TestFalse(TEXT("Conflicting family assignment is rejected"),
        FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,TEXT("PlacedBuilding"),Error));
    State.Key.Kind=EGuLiHybridObjectKind::Miner;
    TestFalse(TEXT("An object cannot borrow another kind's family"),FGuLiHybridAdapterRegistry::Resolve(Data,State,Family,Error));
    Data.Numbers.Add(TEXT("adapter/family/ResourceFactory/state_version"),999);
    TestFalse(TEXT("Incompatible recovery schema fails before gameplay"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data.Numbers.Add(TEXT("adapter/family/ResourceFactory/state_version"),Family.StateVersion);
    const auto GoodData=Data;
    Data.Strings.Remove(TEXT("adapter/actor_components"));
    TestFalse(TEXT("Family-only snapshots cannot start the common component bridge"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;Data.Strings.Add(TEXT("adapter/actor_components"),TEXT("999"));
    TestFalse(TEXT("An incompatible object envelope prevents start"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;
    Data.Numbers.Remove(TEXT("component/Construction/state_version"));
    TestFalse(TEXT("A required component capability cannot be omitted"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;Data.Numbers.Add(TEXT("component/Production/state_version"),999);
    TestFalse(TEXT("Production version incompatibility prevents start"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    Data=GoodData;Data.Numbers.Add(TEXT("component/Unknown/state_version"),1);
    TestFalse(TEXT("An unavailable capability cannot be silently ignored"),FGuLiHybridComponentState::ValidateContracts(Data,Error));
    Data=GoodData;Data.Numbers.Add(TEXT("component/Construction/class"),1);
    TestFalse(TEXT("Contract keys cannot change type"),FGuLiHybridComponentState::ValidateContracts(Data,Error));
    Data=GoodData;
    Data.Rules[0].TargetDefinition=TEXT("building/undeclared");
    TestFalse(TEXT("Dynamic definitions also require a declared contract"),FGuLiHybridAdapterRegistry::Validate(Data,Error));
    FGuLiBuildingDefinition Building;Building.DefinitionId=106;Building.Mesh=NewObject<UStaticMesh>();
    Building.DisplayName=FText::FromString(TEXT("Authored capability variant"));
    Building.Category=EGuLiBuildingCategory::Factory;
    TestEqual(TEXT("Factory capability does not depend on numeric building ID"),GetGuLiHybridBuildingFamily(Building),FName(TEXT("ResourceFactory")));
    Building.Category=EGuLiBuildingCategory::Tower;
    TestEqual(TEXT("A new ordinary building uses the shared family"),GetGuLiHybridBuildingFamily(Building),FName(TEXT("PlacedBuilding")));
    Building.Category=EGuLiBuildingCategory::Stronghold;
    TestTrue(TEXT("Territory capture cannot silently become ordinary placed-building state"),GetGuLiHybridBuildingFamily(Building).IsNone());
    Building.Category=EGuLiBuildingCategory::Tower;
    Building.CompletionUnitTypeId=17;
    TestTrue(TEXT("An unmigrated construction-to-unit capability is explicit"),GetGuLiHybridBuildingFamily(Building).IsNone());
    return true;
}
#endif
