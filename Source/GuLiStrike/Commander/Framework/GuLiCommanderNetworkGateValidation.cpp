// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderNetworkGateValidation.h"

#include "Engine/NetDriver.h"

namespace
{
	int32 GetNominalLagMilliseconds(
		const int32 ConstantLag,
		const int32 MinimumLag,
		const int32 MaximumLag)
	{
		if (ConstantLag > 0)
		{
			return ConstantLag;
		}
		const int32 SanitizedMinimum = FMath::Max(0, MinimumLag);
		const int32 SanitizedMaximum = FMath::Max(SanitizedMinimum, MaximumLag);
		return SanitizedMinimum + (SanitizedMaximum - SanitizedMinimum) / 2;
	}
}

FGuLiCommanderNetworkImpairmentEvidence GuLiCommanderNetworkGateValidation::EvaluateImpairment(
	const FPacketSimulationSettings& Settings,
	const bool bHasServerConnection,
	const float MeasuredRoundTripMilliseconds)
{
	FGuLiCommanderNetworkImpairmentEvidence Evidence;
	const int32 OutgoingNominalLag = GetNominalLagMilliseconds(
		Settings.PktLag,
		Settings.PktLagMin,
		Settings.PktLagMax);
	const int32 IncomingNominalLag = GetNominalLagMilliseconds(
		0,
		Settings.PktIncomingLagMin,
		Settings.PktIncomingLagMax);
	Evidence.ConfiguredNominalRoundTripLagMilliseconds =
		OutgoingNominalLag + IncomingNominalLag;
	Evidence.ConfiguredJitterMilliseconds = FMath::Max3(
		FMath::Max(0, Settings.PktJitter),
		FMath::Max(0, Settings.PktLagVariance) * 2,
		FMath::Max(
			FMath::Max(0, Settings.PktLagMax - Settings.PktLagMin),
			FMath::Max(0, Settings.PktIncomingLagMax - Settings.PktIncomingLagMin)));
	Evidence.ConfiguredLossPercent = FMath::Max(
		FMath::Clamp(Settings.PktLoss, 0, 100),
		FMath::Clamp(Settings.PktIncomingLoss, 0, 100));
	Evidence.bPacketReorderingEnabled = Settings.PktOrder > 0;
	Evidence.MeasuredRoundTripMilliseconds = FMath::IsFinite(MeasuredRoundTripMilliseconds)
		? FMath::Max(0.0f, MeasuredRoundTripMilliseconds)
		: 0.0f;
	Evidence.bHasServerConnection = bHasServerConnection;
	return Evidence;
}

bool GuLiCommanderNetworkGateValidation::MeetsRequiredImpairment(
	const FGuLiCommanderNetworkImpairmentEvidence& Evidence)
{
	const bool bLagIsConfigured = Evidence.ConfiguredNominalRoundTripLagMilliseconds > 0;
	const bool bRequiredLagObserved =
		Evidence.ConfiguredNominalRoundTripLagMilliseconds >= RequiredRoundTripLagMilliseconds
		|| Evidence.MeasuredRoundTripMilliseconds >= static_cast<float>(RequiredRoundTripLagMilliseconds);
	return Evidence.bHasServerConnection
		&& bLagIsConfigured
		&& bRequiredLagObserved
		&& Evidence.ConfiguredJitterMilliseconds >= RequiredJitterMilliseconds
		&& Evidence.ConfiguredLossPercent >= RequiredLossPercent
		&& Evidence.bPacketReorderingEnabled;
}

bool GuLiCommanderNetworkGateValidation::CanPass(
	const FGuLiCommanderNetworkGateEvidence& Evidence)
{
	return Evidence.bCommandsSucceeded
		&& Evidence.bRuntimeImpairmentValid
		&& Evidence.DesiredMoveAckSamples > 0
		&& Evidence.ReceivedMoveAckSamples == Evidence.DesiredMoveAckSamples
		&& Evidence.AckP95Milliseconds <= 150.0
		&& Evidence.BandwidthSampleCount > 0
		&& Evidence.BandwidthAverageMegabits <= 1.5
		&& Evidence.BandwidthP95Megabits <= 2.0
		&& Evidence.FreshPoseFrameCount > 0u
		&& Evidence.PresentedStepSampleCount >= RequiredPresentedStepSamples
		&& Evidence.PresentedTravelDistanceCentimeters
			>= RequiredPresentedTravelDistanceCentimeters
		&& Evidence.PresentedStepP95Centimeters
			<= MaximumPresentedStepP95Centimeters
		&& Evidence.PresentationClockRoundTripMilliseconds > 0.0
		&& Evidence.MaximumSeedPoseGapSeconds <= MaximumSeedPoseGapSeconds
		&& Evidence.UntaggedHardSnapCount == 0u;
}
