#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Capabilities/GuLiLogicalCooldownComponent.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Network/GuLiHybridManifest.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipCapabilityRecoveryTests,
    "GuLiStrike.Ship.Abilities.HangarPrivateRecovery",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiShipCapabilityRecoveryTests::RunTest(const FString&)
{
    struct FWorld
    {
        UWorld* Value=nullptr;
        ~FWorld() {if (Value) {Value->DestroyWorld(false);if (GEngine) GEngine->DestroyWorldContext(Value);}}
    } World;
    if (!TestNotNull(TEXT("Engine available"),GEngine)) return false;
    World.Value=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Value);World.Value->InitializeActorsForPlay(FURL());
    auto* Battle=World.Value->SpawnActor<AGuLiBattleGameState>();World.Value->SetGameState(Battle);Battle->ApplyHybridMatchIdentity(FGuid(0,0,0,9),9);
    auto* Actor=World.Value->SpawnActor<AActor>();
    auto* Clock=NewObject<UGuLiLogicalCooldownComponent>(Actor);Actor->AddInstanceComponent(Clock);Clock->RegisterComponent();
    auto* Definition=NewObject<UGuLiShipHangarDefinition>(Actor);Definition->AbilitySet=UGuLiShipAbilitySet::CreateNativeV3Transient(Definition);
    Definition->Loadout=FGuLiShipAbilityLoadoutState::MakeNativeV3();
    auto* Assembly=NewObject<UGuLiShipAssemblyComponent>(Actor);
    Assembly->Catalog=NewObject<UGuLiShipBuildCatalog>(Assembly);
    auto& Draft=Assembly->Catalog->Groups.AddDefaulted_GetRef();Draft.ConfigurationId=TEXT("DraftTurret");
    Draft.Capabilities.AddDefaulted_GetRef().CapabilityId=TEXT("Turret");
    auto& Executable=Assembly->Catalog->Groups.AddDefaulted_GetRef();Executable.ConfigurationId=TEXT("HangarGroup");Executable.bExecutable=true;
    auto& Capability=Executable.Capabilities.AddDefaulted_GetRef();Capability.CapabilityId=TEXT("Hangar");
    Capability.ComponentClass=UGuLiShipHangarCapabilityComponent::StaticClass();Capability.Configuration=Definition;
    FGuLiHybridManifestData Contracts;FString ContractError;
    TestTrue(TEXT("Only executable mechanisms require a private contract"),Assembly->ExportHybridCapabilityContracts(Contracts,ContractError));
    TestEqual(TEXT("Draft has no executable contract"),Contracts.Numbers.Num(),1);
    TestTrue(TEXT("Local executable contract matches manifest"),Assembly->ValidateHybridCapabilityContracts(Contracts));
    Assembly->Catalog->Groups[0].bExecutable=true;
    FGuLiHybridManifestData InvalidContracts;
    TestFalse(TEXT("Executable mechanism without implementation rejects export"),Assembly->ExportHybridCapabilityContracts(InvalidContracts,ContractError));
    Assembly->Catalog->Groups[0].bExecutable=false;
    ++Definition->AbilitySet->Revision;
    TestFalse(TEXT("Immutable capability configuration mismatch rejects startup"),Assembly->ValidateHybridCapabilityContracts(Contracts));
    --Definition->AbilitySet->Revision;
    auto Make=[&]()
    {
        auto* C=NewObject<UGuLiShipHangarCapabilityComponent>(Actor);Actor->AddInstanceComponent(C);
        C->Prepare(TEXT("HangarGroup"),TEXT("Hangar"),Definition);C->RegisterComponent();return C;
    };
    auto* Original=Make();Original->InitializeShipActorInfo(Actor);
    FGuLiShipAbilityProjectionContext Context;Context.ShipInstanceId=FGuid::NewGuid();Context.MatchEpoch=Battle->GetMatchEpoch();
    Context.Team=EGuLiTeam::Red;Context.OwnerPlayerGuid=FGuid::NewGuid();Context.WingmanTypeId=Definition->AbilitySet->WingmanTypeId;
    Context.ShipGeneration=Context.GroupGeneration=Context.FormationCommandRevision=Context.EffectiveClientSimTick=1;
    Original->SetProjectionContext(Context);Original->SetCapabilityEnabled(true);
    const FName Channel=TEXT("RecoveryWeapon");const auto Token=FGuid::NewGuid();
    if (!TestTrue(TEXT("One cooldown is reserved"),Original->ServerTryReserveWeaponCooldown(Channel,5,Token))) return false;
    auto* Prepared=Make();Prepared->DestroyComponent();
    TestTrue(TEXT("Discarding an uncommitted group preserves existing cooldown"),Original->IsWeaponCooldownActive(Channel));
    TArray<uint8> Private,Cooldown;
    if (!TestTrue(TEXT("Complete independent hangar state captures"),Original->CapturePrivateState(Private))
        || !TestTrue(TEXT("Common cooldown state captures"),Clock->CaptureState(Cooldown))) return false;
    const auto Checksum=Original->GetConfigurationChecksum(Definition);const auto Revision=Original->GetAbilitySetRevision();
    Original->DestroyComponent();
    auto* Restored=Make();
    TestTrue(TEXT("Immutable authored configuration accepts state"),Restored->ValidatePrivateState(Private,Definition));
    ++Definition->AbilitySet->Revision;
    TestFalse(TEXT("Changed configuration cannot restore old capability state"),Restored->ValidatePrivateState(Private,Definition));
    --Definition->AbilitySet->Revision;
    TestTrue(TEXT("Cooldown cut is restored before equipment links"),Clock->RestoreState(Cooldown));
    TestTrue(TEXT("Private capability state restores"),Restored->RestorePrivateState(Private));
    TestTrue(TEXT("Committed state is reconstructed without firing input"),Restored->RestoreCapabilityState(EGuLiShipCapabilityState::Enabled));
    TestEqual(TEXT("Ability revision is preserved"),Restored->GetAbilitySetRevision(),Revision);
    TestEqual(TEXT("Configuration checksum is preserved"),Restored->GetConfigurationChecksum(Definition),Checksum);
    TestTrue(TEXT("Projection identity is preserved"),Restored->GetProjectionContext()==Context);
    TestTrue(TEXT("Reserved cooldown still applies"),Restored->IsWeaponCooldownActive(Channel));
    TestTrue(TEXT("Original reservation token can roll back once"),Restored->ServerRollbackWeaponCooldown(Channel,Token));
    TestFalse(TEXT("Reservation rollback is not repeated"),Restored->ServerRollbackWeaponCooldown(Channel,Token));
    for (const int32 Size:{0,1,Private.Num()/2,Private.Num()-1})
        TestFalse(TEXT("Incomplete private state fails"),Restored->ValidatePrivateState(MakeArrayView(Private.GetData(),Size),Definition));
    auto Changed=Private;Changed[0]=99;
    TestFalse(TEXT("Unknown capability state version fails"),Restored->ValidatePrivateState(Changed,Definition));
    return true;
}
#endif
