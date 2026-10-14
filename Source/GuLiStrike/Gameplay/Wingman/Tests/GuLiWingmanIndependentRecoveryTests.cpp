#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanIndependentRecoveryTest,"GuLiStrike.Wingman.IndependentRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanIndependentRecoveryTest::RunTest(const FString&)
{
    struct FWorld {UWorld* Value=nullptr;~FWorld(){if (Value){Value->DestroyWorld(false);if(GEngine)GEngine->DestroyWorldContext(Value);}}} World;
    if (!TestNotNull(TEXT("Engine available"),GEngine)) return false;
    World.Value=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Value);
    World.Value->InitializeActorsForPlay(FURL());
    auto* Simulation=World.Value->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
    if (!TestNotNull(TEXT("Independent simulation subsystem available"),Simulation)) return false;
    Simulation->SetNavigationRequirementBypassForTests(true);
    auto* Carrier=World.Value->SpawnActor<AActor>();auto* Definition=NewObject<UGuLiShipHangarDefinition>(Carrier);
    Definition->AbilitySet=UGuLiShipAbilitySet::CreateNativeV3Transient(Definition);Definition->Loadout=FGuLiShipAbilityLoadoutState::MakeNativeV3();
    auto* Hangar=NewObject<UGuLiShipHangarCapabilityComponent>(Carrier);Hangar->Prepare(TEXT("Hangar"),TEXT("Hangar"),Definition);
    FGuLiShipAbilityProjectionContext Context;Context.ShipInstanceId=FGuid::NewGuid();Context.OwnerPlayerGuid=FGuid::NewGuid();
    Context.MatchEpoch=1;Context.Team=EGuLiTeam::Red;Context.WingmanTypeId=Definition->AbilitySet->WingmanTypeId;
    Context.ShipGeneration=Context.GroupGeneration=Context.FormationCommandRevision=Context.EffectiveClientSimTick=1;
    Hangar->SetProjectionContext(Context);Hangar->SetCapabilityEnabled(true);FGuLiGroupAbilityConfigSnapshot Configuration;
    if (!TestTrue(TEXT("Full native formation and weapon configuration builds"),Hangar->BuildGroupAbilityConfigSnapshot(Configuration))) return false;
    FGuLiGroupAbilityConfigSnapshot IndependentConfiguration;
    TestTrue(TEXT("Independent factory uses the same authored projection"),UGuLiShipHangarCapabilityComponent::BuildInitialConfiguration(*Definition,Context,IndependentConfiguration));
    TestEqual(TEXT("Independent projection matches the formal hangar"),IndependentConfiguration.SnapshotHash,Configuration.SnapshotHash);
    FGuLiWingmanHandle First;First.Flight.Group.ShipInstanceId=Context.ShipInstanceId;First.Flight.Group.ShipGeneration=First.Flight.Group.GroupGeneration=1;
    First.Flight.FlightIndex=First.MemberIndex=0;First.EntityGeneration=1;auto Second=First;Second.MemberIndex=1;
    FGuLiCarrierSourceRef Source;Source.CanonicalEpoch=Source.MoveRevision=1;
    auto One=Simulation->CreateIndependentMember(First,Configuration,FTransform::Identity,FVector::ZeroVector,Source,FVector(7000,0,1500),Carrier);
    auto Two=Simulation->CreateIndependentMember(Second,Configuration,FTransform::Identity,FVector::ZeroVector,Source,FVector(-7000,0,1500),Carrier);
    if (!TestTrue(TEXT("Two independent members in one group create"),One.IsValid() && Two.IsValid())) return false;
    TestEqual(TEXT("One runtime contains exactly one granted member"),One->Pawns.Num(),1);
    auto* Other=Two->Pawns[0].Get();const auto OtherPose=Other->GetActorTransform();const auto OtherTick=Other->GetRuntimeState().Dynamics.CaptureSimulationTick;
    TestTrue(TEXT("One granted member advances"),Simulation->AdvanceIndependentMember(*One,1.f/30.f,1./30.));
    TestTrue(TEXT("Advancing one member does not move its sibling"),Other->GetActorTransform().Equals(OtherPose));
    TestEqual(TEXT("Sibling logical clock is untouched"),Other->GetRuntimeState().Dynamics.CaptureSimulationTick,OtherTick);
    TArray<uint8> Captured;
    if (!TestTrue(TEXT("Complete movement, avoidance, attack and private navigation capture"),Simulation->CaptureIndependentMember(*One,Captured))) return false;
    TestFalse(TEXT("A sibling cannot restore another stable identity"),Simulation->ValidateIndependentMember(Captured,Second));
    for (const int32 Size:{0,1,Captured.Num()/2,Captured.Num()-1})
        TestFalse(TEXT("Truncated independent state rejects"),Simulation->ValidateIndependentMember(MakeArrayView(Captured.GetData(),Size),First));
    auto Changed=Captured;Changed[0]=99;TestFalse(TEXT("Unknown mechanism schema rejects"),Simulation->ValidateIndependentMember(Changed,First));
    Simulation->ReleaseIndependentMember(*One);
    auto Restored=Simulation->CreateIndependentMember(First,Configuration,FTransform::Identity,FVector::ZeroVector,Source,FVector(7000,0,1500),Carrier);
    if (!TestTrue(TEXT("Only original identity is rebuilt"),Restored.IsValid())) return false;
    TestTrue(TEXT("All private state restores without advancing time"),Simulation->RestoreIndependentMember(*Restored,Captured));
    TArray<uint8> Recaptured;TestTrue(TEXT("Restored state recaptures"),Simulation->CaptureIndependentMember(*Restored,Recaptured));
    if (Recaptured!=Captured)
    {
        int32 FirstDifference=0;while (FirstDifference<FMath::Min(Captured.Num(),Recaptured.Num()) && Captured[FirstDifference]==Recaptured[FirstDifference]) ++FirstDifference;
        AddInfo(FString::Printf(TEXT("Independent cut bytes: original=%d restored=%d first difference=%d original byte=%u restored byte=%u"),
            Captured.Num(),Recaptured.Num(),FirstDifference,Captured.IsValidIndex(FirstDifference)?Captured[FirstDifference]:0,
            Recaptured.IsValidIndex(FirstDifference)?Recaptured[FirstDifference]:0));
    }
    TestTrue(TEXT("Complete cut roundtrips exactly"),Recaptured==Captured);
    Simulation->ReleaseIndependentMember(*Restored);Simulation->ReleaseIndependentMember(*Two);
    return true;
}
#endif
