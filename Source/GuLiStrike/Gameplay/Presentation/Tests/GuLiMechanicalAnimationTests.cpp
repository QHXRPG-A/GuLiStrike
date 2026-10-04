#include "Gameplay/Presentation/GuLiMechanicalAnimation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMechanicalAnimationContractTest,
	"GuLiStrike.Presentation.MechanicalAnimation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiMechanicalAnimationContractTest::RunTest(const FString& Parameters)
{
	using namespace GuLiMechanicalAnimation;
	const auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid.SM_WarMachine_Rigid"));
	const auto C = FGuLiMechanicalAnimationConfig::FromStaticMesh(Mesh, .2f);
	if (!TestTrue(TEXT("WarMachine resolves from a static mesh and rigid sockets"), C.Model == EGuLiMechanicalModel::WarMachine)) return false;
	FGuLiMechanicalAnimationState S;
	const FVector Target(0, 100000, C.UpperPivot.Z);
	StepAim(C, FTransform::Identity, &Target, 720, .1f, S);
	TestTrue(TEXT("Base-speed stationary upper turn is 18 degrees per simulation step"), FMath::IsNearlyEqual(S.UpperYawDegrees, 18.0f));
	S = {};
	StepAim(C, FTransform::Identity, &Target, 1440, .1f, S);
	TestTrue(TEXT("Move-speed upgrade accelerates stationary upper turn"), FMath::IsNearlyEqual(S.UpperYawDegrees, 36.0f));
	S.UpperYawDegrees = 90; S.GunPitchDegrees[0] = 30;
	const FTransform Root(FRotator(0, 20, 0), FVector(700, -200, 40));
	FTransform Muzzle;
	TestTrue(TEXT("Full dynamic muzzle transform resolves"), ResolveMuzzle(C, S, Root, TEXT("BasicAttack"), 0, 10, Muzzle));
	TestTrue(TEXT("Bullet forward matches final gun yaw/pitch"), Muzzle.GetUnitAxis(EAxis::X).Equals(FRotator(30, 90, 0).Vector(), .0001));
	const FVector OriginalMuzzle = Muzzle.GetLocation();
	AcceptShot(C, S, 0, 10);
	TestTrue(TEXT("Recoil reaches 35 gameplay cm in 40 ms"), FMath::IsNearlyEqual(RecoilAt(C, S, 0, 10.04f), 35.0f, .001f));
	ResolveMuzzle(C, S, Root, TEXT("BasicAttack"), 0, 10.04f, Muzzle);
	TestTrue(TEXT("Model scale is applied once to recoil"), FMath::IsNearlyEqual(FVector::Distance(OriginalMuzzle, Muzzle.GetLocation()), 35.0, .01));
	const float BeforeRetrigger = RecoilAt(C, S, 0, 10.10f);
	AcceptShot(C, S, 0, 10.10f);
	TestTrue(TEXT("Retrigger continues current displacement"), FMath::IsNearlyEqual(RecoilAt(C, S, 0, 10.10f), BeforeRetrigger));
	TestTrue(TEXT("Opposite gun does not recoil"), FMath::IsNearlyZero(RecoilAt(C, S, 1, 10.12f)));
	TestTrue(TEXT("Recoil returns after kick plus return duration"), FMath::IsNearlyZero(RecoilAt(C, S, 0, 10.35f)));
	S = {};
	FTransform Move(FVector(144, 0, 0));
	StepLocomotion(C, FTransform::Identity, Move, .2f, false, S);
	TestTrue(TEXT("720 cm/s tilts discs five degrees forward"), FMath::IsNearlyEqual(float(S.DiscTiltRadians.Y), FMath::DegreesToRadians(5.0f), .0001f));
	StepLocomotion(C, Move, Move, .2f, false, S);
	TestTrue(TEXT("Stopped discs level within 0.20 seconds"), S.DiscTiltRadians.IsNearlyZero());
	Move.SetLocation(FVector(288, 0, 0));
	StepLocomotion(C, FTransform::Identity, Move, .2f, false, S);
	TestTrue(TEXT("1440 cm/s tilts discs fifteen degrees"), FMath::IsNearlyEqual(float(S.DiscTiltRadians.Y), FMath::DegreesToRadians(15.0f), .0001f));

	Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Commander/Units/Tactical/Cel/Sweeper/Meshes/SM_Sweeper_Rigid.SM_Sweeper_Rigid"));
	const auto Wheels = FGuLiMechanicalAnimationConfig::FromStaticMesh(Mesh, .2f);
	if (!TestTrue(TEXT("Sweeper resolves four real wheel radii"), Wheels.Model == EGuLiMechanicalModel::Sweeper)) return false;
	S = {};
	const FTransform Forward(FVector(100, 0, 0));
	StepLocomotion(Wheels, FTransform::Identity, Forward, .1f, false, S);
	TestTrue(TEXT("Wheel angle uses signed distance divided by actual radius"), FMath::IsNearlyEqual(S.WheelRadians[0], 100.0f/Wheels.WheelRadii[0]));
	StepLocomotion(Wheels, Forward, FTransform::Identity, .1f, false, S);
	// The stored phase is float; the LWC distance/radius quotient is double.
	TestTrue(TEXT("Reversing cancels roll"), FMath::IsNearlyZero(S.WheelRadians[0], 1.e-6f));
	StepLocomotion(Wheels, FTransform::Identity, FTransform(FRotator(0, 10, 0)), .1f, false, S);
	TestTrue(TEXT("Turning rolls opposite wheels in opposite directions"), S.WheelRadians[0]*S.WheelRadians[1] < 0);
	StepLocomotion(Wheels, FTransform::Identity, Forward, .1f, true, S);
	TestTrue(TEXT("Teleport resets distance sampling instead of spinning wheels"), FMath::IsNearlyZero(S.WheelRadians[0]));
	TestTrue(TEXT("Sweeper has no hover body transform"), HoverBodyTransform(Wheels, S).Equals(FTransform::Identity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWarMachineHoverContractTest,
	"GuLiStrike.Presentation.WarMachineHover", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWarMachineHoverContractTest::RunTest(const FString& Parameters)
{
	using namespace GuLiMechanicalAnimation;
	const auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid.SM_WarMachine_Rigid"));
	const auto C = FGuLiMechanicalAnimationConfig::FromStaticMesh(Mesh, .2f);
	if (!TestTrue(TEXT("Four static disc-bottom sockets are present"), C.bHasHoverNozzles)) return false;
	TestEqual(TEXT("Current and previous GPU pose contract has 51 floats"), FGuLiMechanicalAnimationFrame::CustomDataFloats, 51);
	FGuLiMechanicalAnimationState S;
	StepHover(C, 17, 0, .1f, 10.1, S);
	TestFalse(TEXT("Short stop does not enter Idle"), S.bHoverIdleTarget);
	StepHover(C, 17, 0, .1f, 10.2, S);
	TestTrue(TEXT("200 ms below 5 cm/s enters Idle"), S.bHoverIdleTarget);
	StepHover(C, 17, 10, .1f, 10.3, S);
	TestTrue(TEXT("Hysteresis band retains Idle"), S.bHoverIdleTarget);
	EvaluateHover(C, 17, 10.55, S);
	TestTrue(TEXT("Idle blend reaches full weight in .35 s"), FMath::IsNearlyEqual(HoverWeight(C, S, 10.55), 1.0f, .0001f));
	const auto BeforeMove = S;
	StepHover(C, 17, 16, .1f, 10.55, S);
	TestFalse(TEXT("Movement above 15 cm/s exits Idle"), S.bHoverIdleTarget);
	TestTrue(TEXT("Transition reverses from the current weight"), FMath::IsNearlyEqual(S.HoverBobCentimeters, BeforeMove.HoverBobCentimeters, .001f));
	EvaluateHover(C, 17, 10.9, S);
	TestTrue(TEXT("Moving removes wobble and keeps the 120 cm base lift"), HoverBodyTransform(C, S).GetLocation().Equals(FVector(0,0,120), .001));
	S = {}; S.bHoverIdleTarget = true; S.HoverBlendStartMilliseconds = 1000;
	EvaluateHover(C, 17, 20, S);
	FGuLiMechanicalAnimationState LateJoin;
	LateJoin.bHoverIdleTarget = S.bHoverIdleTarget; LateJoin.HoverBlendStartMilliseconds = S.HoverBlendStartMilliseconds; LateJoin.HoverBlendFromWeight = S.HoverBlendFromWeight;
	EvaluateHover(C, 17, 20, LateJoin);
	TestTrue(TEXT("Late join reconstructs the identical phase from the semantic transition"), HoverBodyTransform(C, S).Equals(HoverBodyTransform(C, LateJoin), .000001));
	EvaluateHover(C, 18, 20, LateJoin);
	TestFalse(TEXT("Neighboring stable IDs have different phases"), HoverBodyTransform(C, S).Equals(HoverBodyTransform(C, LateJoin), .001));
	S.DiscTiltRadians = FVector2D(0,FMath::DegreesToRadians(15.0));
	S.bInitialized = true; S.UpperYawDegrees = 35; S.GunPitchDegrees[0] = 25;
	const FTransform Root(FRotator(0,25,0),FVector(1000,2000,50));
	FTransform Nozzle, Muzzle;
	ResolveHoverNozzle(C,S,Root,0,Nozzle);
	const FQuat Tilt(FVector::RightVector,FMath::DegreesToRadians(15.0));
	const auto Body=HoverBodyTransform(C,S);
	const FVector ExpectedDown = Root.TransformVectorNoScale(Body.TransformVectorNoScale(Tilt.RotateVector(-FVector::UpVector)));
	TestTrue(TEXT("Exhaust follows tilted disc normal plus whole-body sway"), Nozzle.GetUnitAxis(EAxis::X).Equals(ExpectedDown,.00001));
	const auto Frame=BuildFrame(C,S,25,20);
	TestTrue(TEXT("Hover height is converted to mesh space exactly once"), FMath::IsNearlyEqual(Frame.Values[11]*C.ModelScale,C.HoverHeight+S.HoverBobCentimeters,.001f));
	ResolveMuzzle(C,S,Root,TEXT("BasicAttack"),0,20,Muzzle);
	const auto WithHover=Muzzle;
	auto WithoutConfig=C; WithoutConfig.HoverHeight=0;
	auto WithoutState=S; WithoutState.HoverBobCentimeters=WithoutState.HoverPitchRadians=WithoutState.HoverRollRadians=0;
	ResolveMuzzle(WithoutConfig,WithoutState,Root,TEXT("BasicAttack"),0,20,Muzzle);
	const FVector ExpectedPoint=Root.TransformPosition(Body.TransformPosition(Root.InverseTransformPosition(Muzzle.GetLocation())));
	TestTrue(TEXT("Muzzle applies the whole-body transform after mechanical articulation"), WithHover.GetLocation().Equals(ExpectedPoint,.0001));
	return true;
}
#endif
