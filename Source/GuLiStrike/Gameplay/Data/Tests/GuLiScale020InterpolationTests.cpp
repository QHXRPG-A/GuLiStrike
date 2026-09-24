// Scale the spatial contract, not the packet cadence or playback clock.
#if WITH_DEV_AUTOMATION_TESTS
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationPolicy.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020InterpolationContract,
	"GuLiStrike.Scale020.ClientInterpolation.Mass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiScale020InterpolationContract::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	auto* Actor = World->SpawnActor<AGuLiCommanderPresentationActor>();
	if (!TestNotNull(TEXT("Presentation actor"), Actor))
	{
		GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false;
	}
	TestEqual(TEXT("10 Hz interpolation baseline remains 120 ms"), Actor->InterpolationBackTimeSeconds, .12f);
	TestEqual(TEXT("Jitter allowance remains 350 ms"), Actor->MaximumAdaptiveInterpolationBackTimeSeconds, .35f);
	TestEqual(TEXT("Extrapolation remains bounded to 100 ms"), Actor->MaximumExtrapolationSeconds, .1f);
	TestEqual(TEXT("Ordinary correction is capped at three times standard speed"), Actor->MaximumCorrectionSpeedMultiplier, 3.f);
	TestEqual(TEXT("Prediction cap is now 1.8 m, not 9 m"), Actor->MaximumPredictionDistanceCentimeters, 180.f);
	TestEqual(TEXT("Prediction duration unchanged"), Actor->PredictionDurationSeconds, .25f);
	TestEqual(TEXT("Correction duration unchanged"), Actor->PredictionResolveSeconds, .15f);

	const FVector Anchor(224000, -112000, -6300);
	FGuLiCommanderPresentedSoldier Before, After;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FGuLiCommanderBufferedSoldierPose Pose;
		Pose.ServerTimeSeconds = 10.0 + Index * .1;
		Pose.FrameSequence = Index + 1;
		Pose.Location = Anchor + FVector(360, 40, 10) * Index;
		Pose.Velocity = FVector(3600, 400, 100);
		Pose.FacingYawDegrees = Index == 0 ? 350.f : 10.f;
		Before.Samples.Add(Pose);
		Pose.Location = Anchor + (Pose.Location - Anchor) * .2;
		Pose.Velocity *= .2;
		After.Samples.Add(Pose);
	}
	for (const double Time : {10.0, 10.025, 10.05, 10.1, 10.15, 10.2, 10.7})
	{
		FTransform A, B;
		TestTrue(TEXT("Baseline timeline evaluates"), Actor->EvaluateAuthoritativeTransform(Before, Time, A));
		TestTrue(TEXT("Scaled timeline evaluates"), Actor->EvaluateAuthoritativeTransform(After, Time, B));
		TestTrue(TEXT("Cubic interpolation/extrapolation scales relative displacement exactly once"),
			B.GetLocation().Equals(Anchor + (A.GetLocation() - Anchor) * .2, 1.e-4));
		TestTrue(TEXT("Rotation/shortest-angle timing is unchanged"), B.GetRotation().Equals(A.GetRotation(), 1.e-6));
	}
	FTransform Capped;
	Actor->EvaluateAuthoritativeTransform(After, 100.0, Capped);
	TestTrue(TEXT("Long outage stops after 72 cm X extrapolation at 720 cm/s"),
		Capped.GetLocation().Equals(After.Samples.Last().Location + After.Samples.Last().Velocity * .1, 1.e-4));

	const FGuLiSoldierId Id(90001);
	auto& Soldier = Actor->PresentedSoldiers.FindOrAdd(Id);
	Soldier.PresentedTransform.SetLocation(Anchor);
	Soldier.bHasPresentedTransform = true;
	FGuLiCommanderBufferedSoldierPose Pose = After.Samples[0];
	Actor->InsertPoseSample(Id, Pose, 20.0);
	Pose.FrameSequence = 2; Pose.ServerTimeSeconds += .1; Pose.Location = Anchor + FVector(200, 0, 0);
	Actor->InsertPoseSample(Id, Pose, 20.1);
	TestEqual(TEXT("Exactly 2 m does not hard-snap"), Soldier.UntaggedHardSnapCount, uint64(0));
	Pose.FrameSequence = 3; Pose.ServerTimeSeconds += .1; Pose.Location = Anchor + FVector(901, 0, 0);
	Actor->InsertPoseSample(Id, Pose, 20.2);
	TestEqual(TEXT("Large ordinary correction does not hard-snap"), Soldier.UntaggedHardSnapCount, uint64(0));
	TestEqual(TEXT("Ordinary correction retains interpolation history"), Soldier.Samples.Num(), 3);

	auto& Prediction = Actor->PredictedMoves.FindOrAdd(Id);
	Prediction.StartTimeSeconds = 30.0;
	Prediction.Direction = FVector::ForwardVector;
	Prediction.MaximumDistance = Actor->MaximumPredictionDistanceCentimeters;
	for (const TPair<double, double>& Sample : {TPair<double, double>(30.125, 90), {30.25, 180}, {30.325, 90}, {30.401, 0}})
	{
		FTransform Result(FQuat::Identity, Anchor);
		Actor->ApplyPrediction(Id, Sample.Key, Result);
		TestTrue(TEXT("Scaled anticipation resolves with unchanged time curve"),
			Result.GetLocation().Equals(Anchor + FVector(Sample.Value, 0, 0), .001));
	}
	TestFalse(TEXT("Finished correction releases prediction"), Actor->PredictedMoves.Contains(Id));
	Actor->PredictedMoves.FindOrAdd(Id);
	Soldier.bRenderClockInitialized = true;
	Pose.FrameSequence = 4; Pose.ServerTimeSeconds += .1;
	Pose.Location = Anchor + FVector(50000, -65000, 100); Pose.bTeleport = true;
	Actor->InsertPoseSample(Id, Pose, 30.5);
	TestEqual(TEXT("Teleport is counted separately"), Soldier.TeleportSnapCount, uint64(1));
	TestEqual(TEXT("Teleport keeps exactly one new sample"), Soldier.Samples.Num(), 1);
	TestTrue(TEXT("Absolute teleport destination is not multiplied by scale"), Soldier.PresentedTransform.GetLocation().Equals(Pose.Location));
	TestFalse(TEXT("Teleport clears prediction"), Actor->PredictedMoves.Contains(Id));
	TestFalse(TEXT("Teleport resets playback clock"), Soldier.bRenderClockInitialized);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020CharacterSmoothingContract,
	"GuLiStrike.Scale020.ClientInterpolation.CharacterMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiScale020CharacterSmoothingContract::RunTest(const FString& Parameters)
{
	const UCharacterMovementComponent* Native = GetDefault<UCharacterMovementComponent>();
	const UCharacterMovementComponent* Components[] = {
		GetDefault<UGuLiShipMovementComponent>(), GetDefault<UGuLiExternalCharacterMovementComponent>()};
	for (const auto* Movement : Components)
	{
		TestEqual(TEXT("Smooth correction radius scales from 256 to 51.2 cm"), Movement->NetworkMaxSmoothUpdateDistance, 51.2f);
		TestEqual(TEXT("Hard correction radius scales from 384 to 76.8 cm"), Movement->NetworkNoSmoothUpdateDistance, 76.8f);
		TestEqual(TEXT("Remote location smoothing time unchanged"), Movement->NetworkSimulatedSmoothLocationTime, Native->NetworkSimulatedSmoothLocationTime);
		TestEqual(TEXT("Remote rotation smoothing time unchanged"), Movement->NetworkSimulatedSmoothRotationTime, Native->NetworkSimulatedSmoothRotationTime);
		TestEqual(TEXT("Listen-server smoothing time unchanged"), Movement->ListenServerNetworkSimulatedSmoothLocationTime, Native->ListenServerNetworkSimulatedSmoothLocationTime);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020WingmanInterpolationContract,
	"GuLiStrike.Scale020.ClientInterpolation.Wingman",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiScale020WingmanInterpolationContract::RunTest(const FString& Parameters)
{
	const FVector Anchor(224000, -112000, 12000);
	TArray<FGuLiWingmanPresentationPose> Before, After;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FGuLiWingmanPresentationPose Pose;
		Pose.SourceTimeSeconds = 10.0 + Index * .2;
		Pose.Sequence = Index + 1;
		Pose.Location = Anchor + FVector(1600, 200, 100) * Index;
		Pose.Velocity = FVector(8000, 1000, 500);
		Pose.Rotation = FRotator(0, Index * 40, 0).Quaternion();
		Before.Add(Pose);
		Pose.Location = Anchor + (Pose.Location - Anchor) * .2;
		Pose.Velocity *= .2;
		After.Add(Pose);
	}
	for (const double Time : {10.0, 10.1, 10.2, 10.45, 10.7, 10.775, 10.86})
	{
		const auto A = GuLiWingmanPresentationPolicy::Evaluate(Before, Time, Time);
		const auto B = GuLiWingmanPresentationPolicy::Evaluate(After, Time, Time);
		TestEqual(TEXT("Visibility timing is unchanged"), B.bVisible, A.bVisible);
		TestEqual(TEXT("Extrapolation timing is unchanged"), B.bExtrapolating, A.bExtrapolating);
		TestEqual(TEXT("Stale interaction cutoff unchanged"), B.bInteractable, A.bInteractable);
		TestEqual(TEXT("Fade timing unchanged"), B.Opacity, A.Opacity);
		if (B.bVisible)
		{
			TestTrue(TEXT("Wingman interpolated/extrapolated world position scales only relative motion"),
				B.Transform.GetLocation().Equals(Anchor + (A.Transform.GetLocation() - Anchor) * .2, 1.e-4));
			TestTrue(TEXT("Wingman angle unchanged"), B.Transform.GetRotation().Equals(A.Transform.GetRotation(), 1.e-6));
		}
	}
	TestEqual(TEXT("Wingman extrapolation bound remains half a second"), GuLiWingmanPresentationPolicy::MaximumExtrapolationSeconds, .5);
	const auto Capped = GuLiWingmanPresentationPolicy::Evaluate(After, 99, 99,
		GuLiWingmanPresentationPolicy::EStalePolicy::RetainLastPose);
	TestTrue(TEXT("Stale retained pose caps scaled velocity at 0.5 seconds"),
		Capped.Transform.GetLocation().Equals(After.Last().Location + After.Last().Velocity * .5, 1.e-4));
	return true;
}
#endif
