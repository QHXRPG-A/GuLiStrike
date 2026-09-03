// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
	FGuLiWingmanHandle MakeWingman(const uint8 FlightIndex = 0u, const uint8 MemberIndex = 0u)
	{
		FGuLiWingmanHandle Handle;
		Handle.Flight.Group.ShipInstanceId = FGuid(1u, 2u, 3u, 4u);
		Handle.Flight.Group.ShipGeneration = 1u;
		Handle.Flight.Group.GroupGeneration = 1u;
		Handle.Flight.FlightIndex = FlightIndex;
		Handle.MemberIndex = MemberIndex;
		Handle.EntityGeneration = 1u;
		return Handle;
	}

	FGuLiWingmanPresentationPose MakePose(
		const double TimeSeconds,
		const uint32 Sequence,
		const FVector& Location,
		const FVector& Velocity = FVector::ZeroVector,
		const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FGuLiWingmanPresentationPose Pose;
		Pose.SourceTimeSeconds = TimeSeconds;
		Pose.Sequence = Sequence;
		Pose.Location = Location;
		Pose.Velocity = Velocity;
		Pose.Rotation = Rotation.Quaternion();
		return Pose;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPresentationInterpolationTest,
	"GuLiStrike.Wingman.Presentation.Interpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPresentationInterpolationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FGuLiWingmanPresentationPose> Samples;
	TestTrue(TEXT("First sample is accepted"),
		GuLiWingmanPresentationPolicy::AppendPose(
			Samples,
			MakePose(10.0, 1u, FVector::ZeroVector, FVector(100.0, 0.0, 0.0))));
	TestTrue(TEXT("Strictly newer sample is accepted"),
		GuLiWingmanPresentationPolicy::AppendPose(
			Samples,
			MakePose(10.2, 2u, FVector(20.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0),
				FRotator(0.0, 90.0, 0.0))));

	const FGuLiWingmanPresentationEvaluation Evaluation =
		GuLiWingmanPresentationPolicy::Evaluate(Samples, 10.1, 10.2);
	TestTrue(TEXT("An in-range pose is visible"), Evaluation.bVisible);
	TestTrue(TEXT("A fresh pose remains interactable"), Evaluation.bInteractable);
	TestFalse(TEXT("Interpolation is not reported as extrapolation"), Evaluation.bExtrapolating);
	TestTrue(TEXT("Location interpolates halfway"),
		Evaluation.Transform.GetLocation().Equals(FVector(10.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("Rotation follows the shortest quaternion arc"),
		FMath::IsNearlyEqual(Evaluation.Transform.Rotator().Yaw, 45.0, 0.01));
	TestEqual(TEXT("Fresh interpolation is fully opaque"), Evaluation.Opacity, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPresentationStalePolicyTest,
	"GuLiStrike.Wingman.Presentation.ExtrapolationFadeHide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPresentationStalePolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FGuLiWingmanPresentationPose> Samples = {
		MakePose(20.0, 1u, FVector(100.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0))
	};

	const FGuLiWingmanPresentationEvaluation Extrapolated =
		GuLiWingmanPresentationPolicy::Evaluate(Samples, 20.25, 20.25);
	TestTrue(TEXT("A sample extrapolates during the fresh 0.5 second window"),
		Extrapolated.bExtrapolating);
	TestTrue(TEXT("Fresh extrapolation remains interactable"), Extrapolated.bInteractable);
	TestTrue(TEXT("Velocity drives bounded extrapolation"),
		Extrapolated.Transform.GetLocation().Equals(FVector(125.0, 0.0, 0.0), 0.001));

	const FGuLiWingmanPresentationEvaluation Fading =
		GuLiWingmanPresentationPolicy::Evaluate(Samples, 20.575, 20.575);
	TestTrue(TEXT("The following 0.15 second window remains visually present"), Fading.bVisible);
	TestFalse(TEXT("Fading presentation is immediately non-interactable"), Fading.bInteractable);
	TestFalse(TEXT("Motion stops when the fade starts"), Fading.bExtrapolating);
	TestTrue(TEXT("Fading pose is frozen at the 0.5 second bound"),
		Fading.Transform.GetLocation().Equals(FVector(150.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("Halfway through fade has half opacity"),
		FMath::IsNearlyEqual(Fading.Opacity, 0.5f, 0.001f));

	const FGuLiWingmanPresentationEvaluation Hidden =
		GuLiWingmanPresentationPolicy::Evaluate(Samples, 20.65, 20.65);
	TestFalse(TEXT("Presentation hides after 0.5 + 0.15 seconds"), Hidden.bVisible);
	TestFalse(TEXT("Hidden presentation is not interactable"), Hidden.bInteractable);
	TestEqual(TEXT("Hidden presentation has zero opacity"), Hidden.Opacity, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPresentationSequenceTest,
	"GuLiStrike.Wingman.Presentation.SequenceAndBoundedHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPresentationSequenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FGuLiWingmanPresentationPose> Samples;
	for (uint32 Sequence = 1u; Sequence <= 6u; ++Sequence)
	{
		TestTrue(TEXT("Monotonic samples append"),
			GuLiWingmanPresentationPolicy::AppendPose(
				Samples,
				MakePose(static_cast<double>(Sequence), Sequence, FVector::ZeroVector)));
	}
	TestEqual(TEXT("History remains bounded"),
		Samples.Num(), GuLiWingmanPresentationPolicy::MaximumBufferedPoseSamples);
	TestEqual(TEXT("The oldest retained sequence advances"), Samples[0].Sequence, 3u);
	TestFalse(TEXT("Duplicate sequence is rejected"),
		GuLiWingmanPresentationPolicy::AppendPose(
			Samples,
			MakePose(7.0, 6u, FVector::ZeroVector)));
	TestFalse(TEXT("A newer sequence with non-increasing time is rejected"),
		GuLiWingmanPresentationPolicy::AppendPose(
			Samples,
			MakePose(6.0, 7u, FVector::ZeroVector)));
	TestTrue(TEXT("Serial comparison accepts uint32 wrap"),
		GuLiWingmanPresentationPolicy::IsNewerSequence(1u, MAX_uint32));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPresentationPartitionTest,
	"GuLiStrike.Wingman.Presentation.StableSlotAndDedicatedServerGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPresentationPartitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("First member owns stable slot zero"),
		GuLiWingmanPresentationPolicy::GetStableMemberSlot(MakeWingman()), 0);
	TestEqual(TEXT("Last member owns stable slot twenty-four"),
		GuLiWingmanPresentationPolicy::GetStableMemberSlot(MakeWingman(4u, 4u)), 24);
	TestFalse(TEXT("Dedicated Server cannot create presentation"),
		GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(NM_DedicatedServer));
	TestTrue(TEXT("Network clients may create presentation"),
		GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(NM_Client));
	TestTrue(TEXT("Listen hosts may create client presentation"),
		GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(NM_ListenServer));
	return true;
}

#endif
