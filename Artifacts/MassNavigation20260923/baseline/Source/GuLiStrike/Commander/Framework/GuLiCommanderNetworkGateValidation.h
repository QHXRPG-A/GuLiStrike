// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FPacketSimulationSettings;

/** Runtime evidence that the client is actually exercising the required impaired link. */
// 本地运行证据：配置的延迟/抖动/丢包/乱序加实测 RTT；不是网络协议载荷。
struct FGuLiCommanderNetworkImpairmentEvidence
{
	int32 ConfiguredNominalRoundTripLagMilliseconds = 0;
	int32 ConfiguredJitterMilliseconds = 0;
	int32 ConfiguredLossPercent = 0;
	bool bPacketReorderingEnabled = false;
	float MeasuredRoundTripMilliseconds = 0.0f;
	bool bHasServerConnection = false;
};

/** Pure input contract for the final non-shipping network acceptance decision. */
// 最终验收输入：既要求业务 ACK，也要求真实连接、带宽样本、新姿态和足够的移动表现。
struct FGuLiCommanderNetworkGateEvidence
{
	bool bCommandsSucceeded = false;
	bool bRuntimeImpairmentValid = false;
	int32 ReceivedMoveAckSamples = 0;
	int32 DesiredMoveAckSamples = 0;
	double AckP95Milliseconds = 0.0;
	int32 BandwidthSampleCount = 0;
	double BandwidthAverageMegabits = 0.0;
	double BandwidthP95Megabits = 0.0;
	uint64 FreshPoseFrameCount = 0u;
	int32 PresentedStepSampleCount = 0;
	double PresentedTravelDistanceCentimeters = 0.0;
	double PresentedStepP95Centimeters = 0.0;
	double PresentationClockRoundTripMilliseconds = 0.0;
	double MaximumSeedPoseGapSeconds = 0.0;
	uint64 UntaggedHardSnapCount = 0u;
};

namespace GuLiCommanderNetworkGateValidation
{
	inline constexpr int32 RequiredRoundTripLagMilliseconds = 100;
	inline constexpr int32 RequiredJitterMilliseconds = 30;
	inline constexpr int32 RequiredLossPercent = 5;
	inline constexpr int32 RequiredPresentedStepSamples = 20;
	inline constexpr double RequiredPresentedTravelDistanceCentimeters = 60.0;
	inline constexpr double MaximumPresentedStepP95Centimeters = 20.0;
	inline constexpr double MaximumSeedPoseGapSeconds = 0.5;

	// 只读取当前 PacketSimulationSettings 生成证据，不会替调用者开启网络模拟。
	GULISTRIKE_API FGuLiCommanderNetworkImpairmentEvidence EvaluateImpairment(
		const FPacketSimulationSettings& Settings,
		bool bHasServerConnection,
		float MeasuredRoundTripMilliseconds);

	GULISTRIKE_API bool MeetsRequiredImpairment(
		const FGuLiCommanderNetworkImpairmentEvidence& Evidence);

	// 纯本地判定，所有门槛同时满足才返回 true；不能把静态单元测试通过当作双端实测通过。
	GULISTRIKE_API bool CanPass(const FGuLiCommanderNetworkGateEvidence& Evidence);
}
