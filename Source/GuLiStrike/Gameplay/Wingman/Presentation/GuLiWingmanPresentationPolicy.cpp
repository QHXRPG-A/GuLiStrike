// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationPolicy.h"

namespace
{
	FTransform MakeTransform(const FVector& Location, const FQuat& Rotation)
	{
		return FTransform(Rotation.GetNormalized(), Location, FVector::OneVector);
	}

	FGuLiWingmanPresentationEvaluation MakeVisibleEvaluation(
		const FTransform& Transform,
		const float Opacity,
		const bool bInteractable,
		const bool bExtrapolating)
	{
		FGuLiWingmanPresentationEvaluation Result;
		Result.Transform = Transform;
		Result.Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
		Result.bVisible = Result.Opacity > 0.0f;
		Result.bInteractable = bInteractable;
		Result.bExtrapolating = bExtrapolating;
		return Result;
	}
}

bool GuLiWingmanPresentationPolicy::IsNewerSequence(
	const uint32 Candidate,
	const uint32 Baseline)
{
	return Candidate != 0u && (Baseline == 0u || static_cast<int32>(Candidate - Baseline) > 0);
}

bool GuLiWingmanPresentationPolicy::BuildPose(
	const FGuLiWingmanCandidateSample& Sample,
	const double SourceTimeSeconds,
	const uint32 Sequence,
	FGuLiWingmanPresentationPose& OutPose)
{
	if (!Sample.Wingman.IsValid() || !FMath::IsFinite(SourceTimeSeconds)
		|| SourceTimeSeconds < 0.0 || Sequence == 0u)
	{
		return false;
	}

	const FVector Location(
		static_cast<double>(Sample.PositionCentimeters.X),
		static_cast<double>(Sample.PositionCentimeters.Y),
		static_cast<double>(Sample.PositionCentimeters.Z));
	const FVector Velocity(
		static_cast<double>(Sample.VelocityCentimetersPerSecond.X),
		static_cast<double>(Sample.VelocityCentimetersPerSecond.Y),
		static_cast<double>(Sample.VelocityCentimetersPerSecond.Z));
	const FRotator Rotation(
		static_cast<double>(Sample.RotationCentiDegrees.X) * 0.01,
		static_cast<double>(Sample.RotationCentiDegrees.Y) * 0.01,
		static_cast<double>(Sample.RotationCentiDegrees.Z) * 0.01);
	if (Location.ContainsNaN() || Velocity.ContainsNaN() || Rotation.ContainsNaN())
	{
		return false;
	}

	OutPose.SourceTimeSeconds = SourceTimeSeconds;
	OutPose.ServerAcceptedTimeSeconds = SourceTimeSeconds;
	OutPose.Sequence = Sequence;
	OutPose.Location = Location;
	OutPose.Velocity = Velocity;
	OutPose.Rotation = Rotation.Quaternion().GetNormalized();
	return !OutPose.Rotation.ContainsNaN();
}

bool GuLiWingmanPresentationPolicy::AppendPose(
	TArray<FGuLiWingmanPresentationPose>& InOutSamples,
	const FGuLiWingmanPresentationPose& Pose,
	const int32 MaximumSamples)
{
	if (!FMath::IsFinite(Pose.SourceTimeSeconds) || Pose.SourceTimeSeconds < 0.0
		|| Pose.Sequence == 0u || Pose.Location.ContainsNaN() || Pose.Velocity.ContainsNaN()
		|| Pose.Rotation.ContainsNaN())
	{
		return false;
	}
	if (!InOutSamples.IsEmpty())
	{
		const FGuLiWingmanPresentationPose& Latest = InOutSamples.Last();
		if (!IsNewerSequence(Pose.Sequence, Latest.Sequence)
			|| Pose.SourceTimeSeconds <= Latest.SourceTimeSeconds)
		{
			return false;
		}
	}

	InOutSamples.Add(Pose);
	while (InOutSamples.Num() > MaximumSamples)
	{
		InOutSamples.RemoveAt(0, 1, EAllowShrinking::No);
	}
	return true;
}

FGuLiWingmanPresentationEvaluation GuLiWingmanPresentationPolicy::Evaluate(
	const TConstArrayView<FGuLiWingmanPresentationPose> Samples,
	const double RenderTimeSeconds,
	const double ServerNowSeconds,
	const EStalePolicy StalePolicy)
{
	FGuLiWingmanPresentationEvaluation Hidden;
	if (Samples.IsEmpty() || !FMath::IsFinite(RenderTimeSeconds)
		|| !FMath::IsFinite(ServerNowSeconds))
	{
		return Hidden;
	}

	const FGuLiWingmanPresentationPose& Latest = Samples.Last();
	const double FreshnessAgeSeconds = FMath::Max(0.0, ServerNowSeconds - Latest.SourceTimeSeconds);
	const double HideAtSeconds = MaximumExtrapolationSeconds + StaleFadeSeconds;
	if (StalePolicy == EStalePolicy::FadeThenHide
		&& FreshnessAgeSeconds + UE_DOUBLE_SMALL_NUMBER >= HideAtSeconds)
	{
		return Hidden;
	}

	const bool bRetainingStalePose = StalePolicy == EStalePolicy::RetainLastPose
		&& FreshnessAgeSeconds > MaximumExtrapolationSeconds;
	const bool bInFade = StalePolicy == EStalePolicy::FadeThenHide
		&& FreshnessAgeSeconds > MaximumExtrapolationSeconds;
	const float Opacity = bInFade
		? static_cast<float>(1.0 - (FreshnessAgeSeconds - MaximumExtrapolationSeconds) / StaleFadeSeconds)
		: 1.0f;
	const bool bInteractable = !bInFade && !bRetainingStalePose;
	const double EvaluationTimeSeconds = FMath::Min(
		RenderTimeSeconds,
		Latest.SourceTimeSeconds + MaximumExtrapolationSeconds);

	const FGuLiWingmanPresentationPose& First = Samples[0];
	if (EvaluationTimeSeconds <= First.SourceTimeSeconds)
	{
		return MakeVisibleEvaluation(
			MakeTransform(First.Location, First.Rotation),
			Opacity,
			bInteractable,
			false);
	}

	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const FGuLiWingmanPresentationPose& Next = Samples[Index];
		if (EvaluationTimeSeconds > Next.SourceTimeSeconds)
		{
			continue;
		}

		const FGuLiWingmanPresentationPose& Previous = Samples[Index - 1];
		const double IntervalSeconds = Next.SourceTimeSeconds - Previous.SourceTimeSeconds;
		if (IntervalSeconds <= UE_DOUBLE_SMALL_NUMBER)
		{
			return MakeVisibleEvaluation(
				MakeTransform(Next.Location, Next.Rotation),
				Opacity,
				bInteractable,
				false);
		}
		const float Alpha = static_cast<float>(FMath::Clamp(
			(EvaluationTimeSeconds - Previous.SourceTimeSeconds) / IntervalSeconds,
			0.0,
			1.0));
		const FVector Location = FMath::Lerp(Previous.Location, Next.Location, Alpha);
		const FQuat Rotation = FQuat::Slerp(Previous.Rotation, Next.Rotation, Alpha).GetNormalized();
		return MakeVisibleEvaluation(
			MakeTransform(Location, Rotation),
			Opacity,
			bInteractable,
			false);
	}

	const double ExtrapolationSeconds = FMath::Clamp(
		EvaluationTimeSeconds - Latest.SourceTimeSeconds,
		0.0,
		MaximumExtrapolationSeconds);
	return MakeVisibleEvaluation(
		MakeTransform(Latest.Location + Latest.Velocity * ExtrapolationSeconds, Latest.Rotation),
		Opacity,
		bInteractable,
		!bInFade && !bRetainingStalePose
			&& ExtrapolationSeconds > UE_DOUBLE_SMALL_NUMBER);
}

int32 GuLiWingmanPresentationPolicy::GetStableMemberSlot(const FGuLiWingmanHandle& Wingman)
{
	const uint8 GroupMemberIndex = Wingman.GetGroupMemberIndex();
	return GroupMemberIndex == MAX_uint8 ? INDEX_NONE : static_cast<int32>(GroupMemberIndex);
}

bool GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(const ENetMode NetMode)
{
	return NetMode != NM_DedicatedServer;
}
