// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"

#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/WorldSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderCameraComponentPolicyTest,
	"GuLiStrike.Commander.Camera.ExplicitTerrainSolverComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderCameraComponentPolicyTest::RunTest(const FString& Parameters)
{
	const AGuLiCommanderCameraPawn* CameraDefaults = GetDefault<AGuLiCommanderCameraPawn>();
	TestNotNull(TEXT("camera default object exists"), CameraDefaults);
	if (!CameraDefaults)
	{
		return false;
	}

	const USpringArmComponent* SpringArm = CameraDefaults->GetCommanderSpringArm();
	TestNotNull(TEXT("commander spring arm exists"), SpringArm);
	if (SpringArm)
	{
		TestFalse(TEXT("SpringArm collision cannot retract the terrain-solved camera"), SpringArm->bDoCollisionTest);
		TestFalse(TEXT("SpringArm lag cannot fight the explicit substep solver"), SpringArm->bEnableCameraLag);
		TestEqual(TEXT("default camera arm remains 800m"), SpringArm->TargetArmLength, 80000.0f);
	}

	TestTrue(
		TEXT("Landscape query has world lifetime"),
		UGuLiCommanderLandscapeQuerySubsystem::StaticClass()->IsChildOf(UWorldSubsystem::StaticClass()));
	return !HasAnyErrors();
}

#endif
