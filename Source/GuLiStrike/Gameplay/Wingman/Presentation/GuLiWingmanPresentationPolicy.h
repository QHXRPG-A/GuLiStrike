// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"

/** One immutable pose on either the local-owner or accepted-server timeline. */
struct GULISTRIKE_API FGuLiWingmanPresentationPose
{
	double SourceTimeSeconds = 0.0;
	/** Authority receipt time is for freshness/target queries, never trajectory spacing. */
	double ServerAcceptedTimeSeconds = 0.0;
	uint32 Sequence = 0u;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
};

/** Pure presentation result. It never feeds authority, collision or damage. */
struct GULISTRIKE_API FGuLiWingmanPresentationEvaluation
{
	FTransform Transform = FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	float Opacity = 0.0f;
	bool bVisible = false;
	bool bInteractable = false;
	bool bExtrapolating = false;
};

namespace GuLiWingmanPresentationPolicy
{
	enum class EStalePolicy : uint8
	{
		FadeThenHide = 0,
		RetainLastPose
	};

	inline constexpr int32 MaximumBufferedPoseSamples = 4;
	inline constexpr double MaximumExtrapolationSeconds = 0.5;
	inline constexpr double StaleFadeSeconds = 0.15;

	/** RFC-1982 style comparison used by candidate and accepted sequence numbers. */
	GULISTRIKE_API bool IsNewerSequence(uint32 Candidate, uint32 Baseline);

	/** Converts protocol quantization to a presentation-only pose. */
	GULISTRIKE_API bool BuildPose(
		const FGuLiWingmanCandidateSample& Sample,
		double SourceTimeSeconds,
		uint32 Sequence,
		FGuLiWingmanPresentationPose& OutPose);

	/** Appends a strictly newer pose and keeps a bounded history. */
	GULISTRIKE_API bool AppendPose(
		TArray<FGuLiWingmanPresentationPose>& InOutSamples,
		const FGuLiWingmanPresentationPose& Pose,
		int32 MaximumSamples = MaximumBufferedPoseSamples);

	/**
	 * Interpolates at RenderTimeSeconds while applying freshness against ServerNowSeconds.
	 * Motion extrapolation is capped at 0.5 s; the following 0.15 s is visual-only fade.
	 */
	GULISTRIKE_API FGuLiWingmanPresentationEvaluation Evaluate(
		TConstArrayView<FGuLiWingmanPresentationPose> Samples,
		double RenderTimeSeconds,
		double ServerNowSeconds,
		EStalePolicy StalePolicy = EStalePolicy::FadeThenHide);

	/** Deterministic 0..24 member slot; INDEX_NONE for malformed identities. */
	GULISTRIKE_API int32 GetStableMemberSlot(const FGuLiWingmanHandle& Wingman);

	/** Dedicated Server must not create rendering state or remote presentation mirrors. */
	GULISTRIKE_API bool ShouldCreateClientPresentation(ENetMode NetMode);
}
