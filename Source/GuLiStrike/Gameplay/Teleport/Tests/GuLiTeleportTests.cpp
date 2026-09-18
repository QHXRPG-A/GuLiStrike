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
	auto* Blue=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBody.M_TeleportBody"));
	if (!TestNotNull(TEXT("Generic control"),Control)||!TestNotNull(TEXT("Versioned movement"),Movement)||!TestNotNull(TEXT("Blue translucent material"),Blue)||!Mesh) return false;
	const auto* OriginalMaterial=Mesh->GetMaterial(0); const FGuid Token=FGuid::NewGuid();
	const FTransform Source=Pawn->GetActorTransform();
	TestTrue(TEXT("Capture commits"),Control->ApplyServerState(Token,true,true,Source,true,Blue));
	TestFalse(TEXT("Phased pawn remains visible"),Pawn->IsHidden());
	TestTrue(TEXT("Phased body uses blue material"),Mesh->GetMaterial(0)==Blue);
	TestFalse(TEXT("Phased body has no collision"),Pawn->GetActorEnableCollision());
	TestFalse(TEXT("Phased body cannot be damaged"),Pawn->CanBeDamaged());
	TestTrue(TEXT("Camera still follows the same Pawn"),PC->GetViewTarget()==Pawn && PC->GetPawn()==Pawn);
	TestFalse(TEXT("Another field cannot take a locked participant"),Control->ApplyServerState(FGuid::NewGuid(),true,true,Source,true,Blue));
	const FTransform Target(FRotator(0,70,0),FVector(40000,10000,100));
	TestTrue(TEXT("Landing commits"),Control->ApplyServerState(Token,false,true,Target,true));
	TestTrue(TEXT("Original Pawn moved in place"),Pawn->GetActorTransform().Equals(Target));
	TestTrue(TEXT("Original material is restored"),Mesh->GetMaterial(0)==OriginalMaterial);
	TestTrue(TEXT("Recovery allows incoming damage"),Pawn->CanBeDamaged());
	TestTrue(TEXT("Recovery keeps actions locked"),Control->AreActionsLocked());
	TestTrue(TEXT("Camera retains its target and look rotation"),PC->GetViewTarget()==Pawn && PC->GetControlRotation().Equals(FRotator(-20,35,0)));
	TestEqual(TEXT("Capture and landing each invalidate prior movement"),Movement->GetDisplacementRevision(),2u);
	TestTrue(TEXT("Recovery release succeeds"),Control->ApplyServerState(Token,false,false,Target,false));
	TestFalse(TEXT("Action lock removed"),Control->AreActionsLocked());
	TestEqual(TEXT("Recovery release does not reset displacement history"),Movement->GetDisplacementRevision(),2u);
	return true;
}
#endif
