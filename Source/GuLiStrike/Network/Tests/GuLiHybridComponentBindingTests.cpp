#if WITH_DEV_AUTOMATION_TESTS
#include "Network/Private/GuLiHybridComponentState.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHybridComponentBindingTest,
    "GuLiStrike.Network.ComponentBindingLifetimes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiHybridComponentBindingTest::RunTest(const FString&)
{
    auto* Option=IConsoleManager::Get().FindConsoleVariable(TEXT("gs.Perf.HotspotMask"));
    const int32 Previous=Option->GetInt();Option->Set(8,ECVF_SetByCode);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);Option->Set(Previous,ECVF_SetByCode);};
    auto* Actor=World->SpawnActor<AActor>();
    auto* Metrics=World->GetSubsystem<UGuLiHotspotOptions>();
    if (!TestNotNull(TEXT("World cache settings"),Metrics) || !TestTrue(TEXT("Cache enabled in isolated world"),Metrics->Has(EGuLiHotspot::ComponentCache))) return false;
    FGuLiHybridComponentBindingContext Context;TArray<uint8> Bytes;
    TestTrue(TEXT("No public state is a distinct result"),FGuLiHybridComponentState::TryCapturePublic(*Actor,Bytes,&Context)==EGuLiHybridPublicCapture::Absent);
    const uint64 EmptyBuilds=Metrics->BindingRebuilds;
    FGuLiHybridComponentState::TryCapturePublic(*Actor,Bytes,&Context);
    TestEqual(TEXT("Empty public bindings are cached"),Metrics->BindingRebuilds,EmptyBuilds);

    FGuLiHybridComponentCapability Capability;Capability.Name=TEXT("test/component_binding");Capability.Version=1;
    Capability.ComponentClass=[]()->const UClass* {return USceneComponent::StaticClass();};
    Capability.Capture=[](const UActorComponent& C,TArray<uint8>& Out){Out={uint8(C.ComponentTags.Num())};return true;};
    Capability.Validate=[](const UActorComponent&,TConstArrayView<uint8> In){return In.Num()==1 && In[0]<=2;};
    Capability.Restore=[](UActorComponent& C,TConstArrayView<uint8> In,EGuLiHybridRestoreStage){C.ComponentTags.SetNum(In[0]);return true;};
    Capability.CapturePublic=Capability.Capture;Capability.ValidatePublic=Capability.Validate;
    Capability.ApplyPublic=[](UActorComponent& C,TConstArrayView<uint8> In){C.ComponentTags.SetNum(In[0]);return true;};
    auto Registration=MakeUnique<FGuLiHybridComponentCapabilityRegistration>(Capability);
    auto* First=NewObject<USceneComponent>(Actor,TEXT("BoundComponent"));Actor->AddInstanceComponent(First);First->ComponentTags.Add(TEXT("first"));
    TestTrue(TEXT("Registration/addition invalidates negative cache"),FGuLiHybridComponentState::Capture(*Actor,Bytes,&Context));
    const auto Snapshot=Bytes;const uint64 Builds=Metrics->BindingRebuilds;
    TestTrue(TEXT("Captured data validates through the same lease"),FGuLiHybridComponentState::Validate(*Actor,Snapshot,&Context));
    TestEqual(TEXT("Repeated operation reuses binding"),Metrics->BindingRebuilds,Builds);
    Actor->RemoveInstanceComponent(First);First->DestroyComponent();First->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors);
    auto* Second=NewObject<USceneComponent>(Actor,TEXT("BoundComponent"));Actor->AddInstanceComponent(Second);Second->ComponentTags={TEXT("second"),TEXT("new")};
    TestTrue(TEXT("Same-count same-name replacement captures"),FGuLiHybridComponentState::Capture(*Actor,Bytes,&Context));
    TestTrue(TEXT("Replacement cannot reuse old component identity"),Metrics->BindingRebuilds>Builds && Bytes!=Snapshot);
    TestTrue(TEXT("Restoring compatible data targets the new component"),FGuLiHybridComponentState::Restore(*Actor,Snapshot,EGuLiHybridRestoreStage::Activate,&Context));
    TestEqual(TEXT("Restored value was applied"),Second->ComponentTags.Num(),1);
    auto Malformed=Snapshot;Malformed.Pop();
    TestFalse(TEXT("Every payload remains validated despite a binding hit"),FGuLiHybridComponentState::Validate(*Actor,Malformed,&Context));
    Second->ComponentTags.SetNum(3);
    TestTrue(TEXT("Invalid public state is not mistaken for no state"),FGuLiHybridComponentState::TryCapturePublic(*Actor,Bytes,&Context)==EGuLiHybridPublicCapture::Invalid);
    Registration.Reset();
    TestTrue(TEXT("Unregistration invalidates existing lease"),FGuLiHybridComponentState::TryCapturePublic(*Actor,Bytes,&Context)==EGuLiHybridPublicCapture::Absent);
    Context.Reset();Actor->Destroy();
    return true;
}
#endif
