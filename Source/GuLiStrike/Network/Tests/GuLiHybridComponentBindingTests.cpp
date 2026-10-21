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
    const int32 Previous=Option->GetInt();Option->SetWithCurrentPriority(8);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);Option->SetWithCurrentPriority(Previous);};
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
    Capability.AdvanceLogical=[](UActorComponent& C,uint64,double){C.ComponentTags.AddUnique(TEXT("logical"));};
    Capability.NeedsLogicalAdvance=[](const UActorComponent& C,uint64){return !C.ComponentTags.Contains(TEXT("sleep"));};
    auto Registration=MakeUnique<FGuLiHybridComponentCapabilityRegistration>(Capability);
    auto* First=NewObject<USceneComponent>(Actor,TEXT("BoundComponent"));Actor->AddInstanceComponent(First);First->ComponentTags.Add(TEXT("first"));
    TestTrue(TEXT("Registration/addition invalidates negative cache"),FGuLiHybridComponentState::Capture(*Actor,Bytes,&Context));
    const auto Snapshot=Bytes;const uint64 Builds=Metrics->BindingRebuilds;
    TestTrue(TEXT("Captured data validates through the same lease"),FGuLiHybridComponentState::Validate(*Actor,Snapshot,&Context));
    TestEqual(TEXT("Repeated operation reuses binding"),Metrics->BindingRebuilds,Builds);
    TestTrue(TEXT("Logical-only lease advances the live component"),FGuLiHybridComponentState::Advance(*Actor,1,1./60.,&Context));
    TestTrue(TEXT("Logical callback ran"),First->ComponentTags.Contains(TEXT("logical")));
    First->ComponentTags.Remove(TEXT("logical"));First->ComponentTags.Add(TEXT("sleep"));
    TestTrue(TEXT("Sleeping capability keeps a valid binding"),FGuLiHybridComponentState::Advance(*Actor,2,1./60.,&Context));
    TestFalse(TEXT("Sleeping callback does no work"),First->ComponentTags.Contains(TEXT("logical")));
    First->ComponentTags.Remove(TEXT("sleep"));
    TestTrue(TEXT("Eligibility is reconsidered on the next logical step"),FGuLiHybridComponentState::Advance(*Actor,3,1./60.,&Context));
    TestTrue(TEXT("A state change wakes immediately"),First->ComponentTags.Contains(TEXT("logical")));
    Actor->RemoveInstanceComponent(First);First->DestroyComponent();First->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors);
    auto* Second=NewObject<USceneComponent>(Actor,TEXT("BoundComponent"));Actor->AddInstanceComponent(Second);Second->ComponentTags={TEXT("second"),TEXT("new")};
    TestTrue(TEXT("Same-count same-name replacement captures"),FGuLiHybridComponentState::Capture(*Actor,Bytes,&Context));
    TestTrue(TEXT("Replacement cannot reuse old component identity"),Metrics->BindingRebuilds>Builds && Bytes!=Snapshot);
    TestTrue(TEXT("Restoring compatible data targets the new component"),FGuLiHybridComponentState::Restore(*Actor,Snapshot,EGuLiHybridRestoreStage::Activate,&Context));
    TestEqual(TEXT("Restored value was applied"),Second->ComponentTags.Num(),1);
    TestTrue(TEXT("Replacement also invalidates the logical lease"),FGuLiHybridComponentState::Advance(*Actor,4,1./60.,&Context));
    TestTrue(TEXT("The new component receives logical work"),Second->ComponentTags.Contains(TEXT("logical")));
    auto Malformed=Snapshot;Malformed.Pop();
    TestFalse(TEXT("Every payload remains validated despite a binding hit"),FGuLiHybridComponentState::Validate(*Actor,Malformed,&Context));
    Second->ComponentTags.SetNum(3);
    TestTrue(TEXT("Invalid public state is not mistaken for no state"),FGuLiHybridComponentState::TryCapturePublic(*Actor,Bytes,&Context)==EGuLiHybridPublicCapture::Invalid);
    Registration.Reset();
    Second->ComponentTags.Reset();
    TestTrue(TEXT("Removed logical registration leaves an empty valid lease"),FGuLiHybridComponentState::Advance(*Actor,5,1./60.,&Context));
    TestTrue(TEXT("Removed callback is not invoked"),Second->ComponentTags.IsEmpty());
    TestTrue(TEXT("Unregistration invalidates existing lease"),FGuLiHybridComponentState::TryCapturePublic(*Actor,Bytes,&Context)==EGuLiHybridPublicCapture::Absent);
    Context.Reset();
    auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (TestNotNull(TEXT("World binding registry"),Hybrid))
    {
        FGuLiHybridObjectKey Key;Key.Kind=EGuLiHybridObjectKind::Building;Key.StableId=FGuid::NewGuid();
        const auto Before=Hybrid->GetActorBindingRevision();
        TestNull(TEXT("Unbound lookup"),Hybrid->FindBoundActor(Key));
        TestTrue(TEXT("Bind invalidates negative actor caches"),Hybrid->BindActor(Key,*Actor));
        TestTrue(TEXT("Actor binding revision advanced"),Hybrid->GetActorBindingRevision()>Before);
        auto* Replacement=World->SpawnActor<AActor>();
        const auto Bound=Hybrid->GetActorBindingRevision();
        TestTrue(TEXT("Reinstalling the same actor is valid"),Hybrid->BindActor(Key,*Actor));
        TestEqual(TEXT("Same binding keeps the version"),Hybrid->GetActorBindingRevision(),Bound);
        Hybrid->UnbindActor(Key,*Replacement);
        TestEqual(TEXT("Wrong actor cannot invalidate a live binding"),Hybrid->GetActorBindingRevision(),Bound);
        Hybrid->UnbindActor(Key,*Actor);
        TestTrue(TEXT("Unbind invalidates actor caches"),Hybrid->GetActorBindingRevision()>Bound);
        TestTrue(TEXT("Replacement can bind the same key"),Hybrid->BindActor(Key,*Replacement));
        TestTrue(TEXT("Current lookup returns the replacement"),Hybrid->FindBoundActor(Key)==Replacement);
        Replacement->Destroy();TestNull(TEXT("Destroyed actor is never returned"),Hybrid->FindBoundActor(Key));
    }
    Actor->Destroy();
    return true;
}
#endif
