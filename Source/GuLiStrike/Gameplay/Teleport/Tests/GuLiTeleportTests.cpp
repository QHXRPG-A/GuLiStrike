#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "Gameplay/WarMachine/GuLiWarMachinePlaceholderPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Network/Private/GuLiHybridEntityState.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridActorComponent.h"
#include "GuLiNetRelocation.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiTeleportConfigurationTest,"GuLiStrike.Teleport.ConfigurationAndBoundaries",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiTeleportConfigurationTest::RunTest(const FString& Parameters)
{
	for (int32 Level=1; Level<=4; ++Level)
	{
		constexpr float Radii[] = {800.f, 2000.f, 4000.f, 10000.f};
		FGuLiTeleportFieldConfig Config; Config.Level=Level; Config.RadiusCentimeters=Radii[Level-1]; Config.bAllowPlayerVehicles=Level==4;
		TestTrue(TEXT("Tier configuration is valid"),Config.IsValid());
		TestTrue(TEXT("Ground circle boundary is inclusive"),GuLiTeleport::IsInsideDisc(FVector(Config.RadiusCentimeters,0,10000),FVector::ZeroVector,Config.RadiusCentimeters));
		TestFalse(TEXT("One centimeter beyond the circle is rejected"),GuLiTeleport::IsInsideDisc(FVector(Config.RadiusCentimeters+1,0,0),FVector::ZeroVector,Config.RadiusCentimeters));
		TestEqual(TEXT("Only tier four accepts player WM"),GuLiTeleport::CanCollectVehicle(Config,false,90),Level==4);
		TestEqual(TEXT("Only tier four accepts player Ship at exactly 20m"),GuLiTeleport::CanCollectVehicle(Config,true,2000),Level==4);
		TestFalse(TEXT("Ship above 20m is excluded"),GuLiTeleport::CanCollectVehicle(Config,true,2000.1));
		TestFalse(TEXT("Underground Ship is excluded"),GuLiTeleport::CanCollectVehicle(Config,true,-1));
		FGuLiTeleportCastState State; State.Config=Config; State.PhaseStartTime=20;
		TestEqual(TEXT("Progress starts at twelve o'clock"),GuLiTeleport::WindupProgress(State,20),0.f);
		TestEqual(TEXT("Half circle at 1.5 seconds"),GuLiTeleport::WindupProgress(State,21.5),.5f);
		TestTrue(TEXT("Windup is unfinished immediately before 3 seconds"),GuLiTeleport::WindupProgress(State,22.999)<1);
		TestEqual(TEXT("Full circle exactly at 3 seconds"),GuLiTeleport::WindupProgress(State,23),1.f);
		TestEqual(TEXT("Beam height is 100m at every tier"),Config.BeamHeightCentimeters,10000.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiTeleportPawnIdentityCameraTest,"GuLiStrike.Teleport.PawnIdentityMaterialCameraAndRecovery",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiTeleportPawnIdentityCameraTest::RunTest(const FString& Parameters)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	if (!TestNotNull(TEXT("Isolated game world"),World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Pawn=World->SpawnActor<AGuLiWarMachinePlaceholderPawn>();
	auto* PC=World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Original WM Pawn"),Pawn)||!TestNotNull(TEXT("Original controller"),PC)) return false;
	PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
	PC->PlayerCameraManager->InitializeFor(PC);
	PC->Possess(Pawn); PC->SetViewTarget(Pawn); PC->SetControlRotation(FRotator(-20,35,0));
	auto* Control=Pawn->FindComponentByClass<UGuLiExternalUnitControlComponent>();
	auto* Movement=Cast<UGuLiExternalCharacterMovementComponent>(Pawn->GetCharacterMovement());
	auto* Mesh=Pawn->FindComponentByClass<UStaticMeshComponent>();
	auto* Blue=GuLiVfx::Load<UMaterialInterface>(nullptr,GuLiVfxIds::TeleportBody);
	if (!TestNotNull(TEXT("Generic control"),Control)||!TestNotNull(TEXT("Versioned movement"),Movement)||!TestNotNull(TEXT("Blue translucent material"),Blue)||!Mesh) return false;
	const auto* OriginalMaterial=Mesh->GetMaterial(0); const FGuid Token=FGuid::NewGuid();
	const FTransform Source=Pawn->GetActorTransform();
	TestFalse(TEXT("An isolated Pawn cannot submit authority-only control"),Control->ApplyOwnedState(Token,true,true,Source,true,Blue));
    // This isolated test covers committed-state presentation. Authority admission
    // and the relocation transaction are exercised by the multiplayer review.
    if (!UGuLiHybridActorComponent::Find(Pawn))
    {
        auto* Identity=NewObject<UGuLiHybridActorComponent>(Pawn);Pawn->AddInstanceComponent(Identity);Identity->RegisterComponent();
    }
    FGuLiExternalUnitControlState Applied;Applied.OwnerToken=Token;Applied.bPhased=true;Applied.bActionsLocked=true;
    Applied.Baseline=Source;Applied.DisplacementRevision=1;Applied.Revision=1;Applied.PhaseMaterial=Blue;
    Applied.RestoreMovementMode=Movement->MovementMode;Control->ApplyHybridState(Applied);
	TestFalse(TEXT("Phased pawn remains visible"),Pawn->IsHidden());
	TestTrue(TEXT("Phased body uses blue material"),Mesh->GetMaterial(0)==Blue);
	TestFalse(TEXT("Phased body has no collision"),Pawn->GetActorEnableCollision());
	TestFalse(TEXT("Phased body cannot be damaged"),Pawn->CanBeDamaged());
	TestTrue(TEXT("Camera still follows the same Pawn"),PC->GetViewTarget()==Pawn && PC->GetPawn()==Pawn);
	TestFalse(TEXT("Another field cannot take a locked participant"),Control->ApplyOwnedState(FGuid::NewGuid(),true,true,Source,true,Blue));
	const FTransform Target(FRotator(0,70,0),FVector(40000,10000,100));
    Applied.bPhased=false;Applied.Baseline=Target;++Applied.DisplacementRevision;++Applied.Revision;Control->ApplyHybridState(Applied);
	TestTrue(TEXT("Original Pawn moved in place"),Pawn->GetActorTransform().Equals(Target));
	TestTrue(TEXT("Original material is restored"),Mesh->GetMaterial(0)==OriginalMaterial);
	TestTrue(TEXT("Landing restores the original Pawn collision"),Pawn->GetActorEnableCollision());
	TestTrue(TEXT("Recovery allows incoming damage"),Pawn->CanBeDamaged());
	TestTrue(TEXT("Recovery keeps actions locked"),Control->AreActionsLocked());
	TestTrue(TEXT("Camera retains its target and look rotation"),PC->GetViewTarget()==Pawn && PC->GetControlRotation().Equals(FRotator(-20,35,0)));
	TestEqual(TEXT("Capture and landing each invalidate prior movement"),Movement->GetDisplacementRevision(),2u);
    Applied.bActionsLocked=false;++Applied.Revision;Control->ApplyHybridState(Applied);
	TestFalse(TEXT("Action lock removed"),Control->AreActionsLocked());
	TestEqual(TEXT("Recovery release does not reset displacement history"),Movement->GetDisplacementRevision(),2u);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiTeleportPeriodicEligibilityTest,"GuLiStrike.Teleport.PeriodicEligibilityAndRestore",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiTeleportPeriodicEligibilityTest::RunTest(const FString&)
{
    auto* Option=IConsoleManager::Get().FindConsoleVariable(TEXT("gs.Perf.HotspotMask"));
    if (!TestNotNull(TEXT("Development options"),Option)) return false;
    const int32 Previous=Option->GetInt();
    ON_SCOPE_EXIT {Option->SetWithCurrentPriority(Previous);};
    for (const int32 Mask:{0,31})
    {
        Option->SetWithCurrentPriority(Mask);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false);
        if (!TestNotNull(TEXT("Isolated capability world"),World)) return false;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};

        FGuLiHybridObjectState Object;
        Object.Key.Kind=EGuLiHybridObjectKind::Soldier;Object.Key.StableId=FGuid::NewGuid();
        Object.Definition=TEXT("unit/2");Object.LifeState=1;
        GuLiNet::Relocation::Body Body;
        const auto SetBody=[&]
        {
            auto& Capability=Object.Capabilities.FindOrAdd(TEXT("relocation_body"));
            Capability.Version=GuLiNet::Relocation::Version;
            const auto Bytes=GuLiNet::Relocation::Encode(Body);
            Capability.Data.Reset();Capability.Data.Append(Bytes.data(),int32(Bytes.size()));
        };
        SetBody();
        FGuLiHybridManifestData Manifest;FString Error;
        if (!TestTrue(TEXT("Production relocation capability binds"),
            FGuLiHybridEntityState::Bind(Manifest,Object.Definition.ToString(),TEXT("RelocationBody"),Error))) return false;
        FGuLiHybridEntityState Entity;
        if (!TestTrue(TEXT("Production body is prepared"),Entity.Prepare(*World,Object,Manifest))) return false;
        TestFalse(TEXT("Idle body sleeps independently of remaining diagnostic options"),Entity.HasPeriodicWork());
        TArray<uint8> IdleSnapshot;
        TestTrue(TEXT("Idle private state remains capturable"),Entity.Capture(IdleSnapshot));

        Body.Root=GuLiNetBridge::ToCore(Object.Key);Body.Cast=GuLiNetBridge::ToCore(FGuid::NewGuid());
        Body.Step=1;Body.Locked=true;SetBody();Entity.ApplyLedger(Object);
        TestTrue(TEXT("A new ledger step wakes immediately"),Entity.HasPeriodicWork());
        TestFalse(TEXT("Motion stays blocked until the actual step is acknowledged"),Entity.AcceptMotion(100));
        for (const auto Stage:{EGuLiHybridRestoreStage::Detach,EGuLiHybridRestoreStage::Create,
             EGuLiHybridRestoreStage::Link,EGuLiHybridRestoreStage::Activate})
            TestTrue(TEXT("Existing handoff format restores across all stages"),Entity.Restore(IdleSnapshot,Stage));
        TestTrue(TEXT("An older private snapshot cannot sleep an outstanding public step"),Entity.HasPeriodicWork());

        Body.Acknowledged=Body.Step;Body.AppliedAt=100;SetBody();Entity.ApplyLedger(Object);
        TestFalse(TEXT("Acknowledged work can sleep again"),Entity.HasPeriodicWork());
        TestFalse(TEXT("The acknowledged application tick remains protected"),Entity.AcceptMotion(100));
        TestTrue(TEXT("Newer motion is accepted after acknowledgement"),Entity.AcceptMotion(101));
        ++Body.Step;SetBody();Entity.ApplyLedger(Object);
        TestTrue(TEXT("A later return/release step wakes the same body"),Entity.HasPeriodicWork());
        Object.Capabilities.FindChecked(TEXT("relocation_body")).Data.Pop();Entity.ApplyLedger(Object);
        TestTrue(TEXT("Invalid data cannot be hidden by idle eligibility"),Entity.HasPeriodicWork());

        ++Object.Key.LifeGeneration;Body={};SetBody();
        TestTrue(TEXT("A reused identity prepares a new body"),Entity.Prepare(*World,Object,Manifest));
        TestFalse(TEXT("Old pending work does not leak into the new life"),Entity.HasPeriodicWork());
        Entity.Reset();
        TestFalse(TEXT("Released capabilities leave no periodic work"),Entity.HasPeriodicWork());
    }
    return true;
}
#endif
