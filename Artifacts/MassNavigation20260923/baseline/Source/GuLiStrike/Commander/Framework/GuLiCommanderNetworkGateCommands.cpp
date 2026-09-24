// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if !UE_BUILD_SHIPPING

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderNetworkGateValidation.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"

namespace GuLiCommanderNetworkGate
{
	// 仅非 Shipping 的主动验收状态机；会实际提交选兵和移动，不能当作无副作用查询。
	enum class EStage : uint8
	{
		WaitingForBootstrap,
		WaitingForSelectionAck,
		WaitingForSelectionState,
		WaitingForMoveAck,
		WaitingToIssueMove,
		WaitingForPresentationEvidence,
		Finished,
	};

	class FRunner : public TSharedFromThis<FRunner>
	{
	public:
		explicit FRunner(const int32 InDesiredMoveSamples)
			: DesiredMoveSamples(FMath::Clamp(InDesiredMoveSamples, 5, 100))
			, StartTimeSeconds(FPlatformTime::Seconds())
		{
			uint32 CandidateId = FPlatformTime::Cycles();
			FirstCommandId = CandidateId == 0u ? 1u : CandidateId;
		}

		void Start()
		{
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateSP(AsShared(), &FRunner::Tick));
			UE_LOG(
				LogGuLiStrike,
				Display,
				TEXT("Commander network gate armed: moves=%d commandline=%s"),
				DesiredMoveSamples,
				FCommandLine::Get());
		}

		bool IsFinished() const
		{
			return Stage == EStage::Finished;
		}

	private:
		bool Tick(float DeltaSeconds)
		{
			(void)DeltaSeconds;
			const double NowSeconds = FPlatformTime::Seconds();
			if (NowSeconds - StartTimeSeconds > 60.0)
			{
				Finish(false, TEXT("timed out after 60 seconds"));
				return false;
			}

			if (!ResolveClientContext())
			{
				return true;
			}

			CaptureBandwidthSample(NowSeconds);
			CapturePresentationStep();

			switch (Stage)
			{
			case EStage::WaitingForBootstrap:
				if (PrepareSeed())
				{
					if (!CaptureRuntimeImpairment())
					{
						if (FirstImpairmentCheckTimeSeconds <= 0.0)
						{
							FirstImpairmentCheckTimeSeconds = NowSeconds;
						}
						if (NowSeconds - FirstImpairmentCheckTimeSeconds > 15.0)
						{
							Finish(false, TEXT("required 100ms RTT / 30ms jitter / 5% loss / reordering impairment was not observed within 15 seconds"));
							return false;
						}
						break;
					}
					BindAckDelegate();
					IssueSelection(NowSeconds);
				}
				break;

			case EStage::WaitingForSelectionState:
				if (!NetSync.IsValid())
				{
					break;
				}
				if (!NetSync->GetSelectionState().Cohorts.IsEmpty()
					&& NetSync->GetSelectionState().SelectionRevision != SelectionRevisionBeforeRequest)
				{
					Stage = EStage::WaitingToIssueMove;
					NextMoveIssueTimeSeconds = NowSeconds;
				}
				break;

			case EStage::WaitingToIssueMove:
				if (NowSeconds >= NextMoveIssueTimeSeconds)
				{
					IssueMove(NowSeconds);
				}
				break;

			case EStage::WaitingForSelectionAck:
			case EStage::WaitingForMoveAck:
				if (NowSeconds - CommandSentTimeSeconds > 5.0)
				{
					Finish(false, TEXT("reliable command ACK exceeded 5 seconds"));
					return false;
				}
				break;

			case EStage::WaitingForPresentationEvidence:
			{
				const double EvidenceDurationSeconds = NowSeconds - PresentationEvidenceStartTimeSeconds;
				const bool bHasEnoughSamples = PresentedStepCentimeters.Num()
					>= GuLiCommanderNetworkGateValidation::RequiredPresentedStepSamples;
				const bool bHasMovedFarEnough = PresentedTravelDistanceCentimeters
					>= GuLiCommanderNetworkGateValidation::RequiredPresentedTravelDistanceCentimeters;
				const bool bHasClockRoundTrip = Presentation.IsValid()
					&& Presentation->GetLatestClockRoundTripMilliseconds() > 0.0f;
				if (EvidenceDurationSeconds >= 2.0 && bHasEnoughSamples
					&& bHasMovedFarEnough && bHasClockRoundTrip)
				{
					Finish(true, TEXT("typed ACKs and moving presentation evidence collected"));
					return false;
				}
				if (EvidenceDurationSeconds > 8.0)
				{
					Finish(false, TEXT("presentation did not move far enough within 8 seconds"));
					return false;
				}
				break;
			}

			case EStage::Finished:
				return false;
			}

			return true;
		}

		// 只寻找 NM_Client World；Listen Server 的本地主机不能替代真实远端连接验收。
		bool ResolveClientContext()
		{
			if (World.IsValid() && Controller.IsValid() && NetSync.IsValid())
			{
				return true;
			}

			if (!GEngine)
			{
				return false;
			}

			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* CandidateWorld = Context.World();
				if (!CandidateWorld || CandidateWorld->GetNetMode() != NM_Client)
				{
					continue;
				}

				for (FConstPlayerControllerIterator It = CandidateWorld->GetPlayerControllerIterator(); It; ++It)
				{
					AGuLiCommanderPlayerController* CandidateController = Cast<AGuLiCommanderPlayerController>(It->Get());
					if (!CandidateController || !CandidateController->IsLocalController())
					{
						continue;
					}

					World = CandidateWorld;
					Controller = CandidateController;
					NetSync = CandidateController->GetCommanderNetSyncComponent();
					return NetSync.IsValid();
				}
			}

			return false;
		}

		bool PrepareSeed()
		{
			if (!World.IsValid() || !Controller.IsValid() || !NetSync.IsValid())
			{
				return false;
			}

			const AGuLiCommanderPlayerState* PlayerState = Controller->GetPlayerState<AGuLiCommanderPlayerState>();
			if (!PlayerState || !PlayerState->IsCommander() || !PlayerState->IsSyncReady())
			{
				return false;
			}

			if (!StateReplicator.IsValid())
			{
				for (TActorIterator<AGuLiSoldierStateReplicator> It(World.Get()); It; ++It)
				{
					StateReplicator = *It;
					break;
				}
			}
			if (!Presentation.IsValid())
			{
				for (TActorIterator<AGuLiCommanderPresentationActor> It(World.Get()); It; ++It)
				{
					Presentation = *It;
					break;
				}
			}
			if (!StateReplicator.IsValid() || !Presentation.IsValid())
			{
				return false;
			}

			for (const FGuLiSoldierStateItem& State : StateReplicator->GetItems())
			{
				FTransform PresentedTransform;
				if (State.Team == PlayerState->GetTeam() && State.IsAlive()
					&& Presentation->TryGetPresentedSoldierTransform(State.SoldierId, PresentedTransform))
				{
					SeedSoldierId = State.SoldierId;
					SeedLocation = PresentedTransform.GetLocation();
					LastPresentedLocation = SeedLocation;
					bHasLastPresentedLocation = true;
					return true;
				}
			}

			return false;
		}

		bool CaptureRuntimeImpairment()
		{
			if (!World.IsValid() || !Controller.IsValid())
			{
				return false;
			}
			const UNetDriver* NetDriver = World->GetNetDriver();
			const UNetConnection* Connection = NetDriver
				? NetDriver->ServerConnection
				: nullptr;
			const AGuLiCommanderPlayerState* PlayerState = Controller->GetPlayerState<AGuLiCommanderPlayerState>();
			const float MeasuredRoundTripMilliseconds = PlayerState
				? PlayerState->GetPingInMilliseconds()
				: 0.0f;
			if (!NetDriver)
			{
				return false;
			}
#if DO_ENABLE_NET_TEST
			RuntimeImpairment = GuLiCommanderNetworkGateValidation::EvaluateImpairment(
				NetDriver->PacketSimulationSettings,
				Connection != nullptr,
				MeasuredRoundTripMilliseconds);
			bRuntimeImpairmentValid = GuLiCommanderNetworkGateValidation::MeetsRequiredImpairment(RuntimeImpairment);
			return bRuntimeImpairmentValid;
#else
			return false;
#endif
		}

		void BindAckDelegate()
		{
			if (!NetSync.IsValid() || AckDelegateHandle.IsValid())
			{
				return;
			}
			AckDelegateHandle = NetSync->OnCommandAckChanged.AddSP(AsShared(), &FRunner::HandleAck);
		}

		void IssueSelection(const double NowSeconds)
		{
			// Measure steady-state command/pose traffic after the reliable roster bootstrap.
			IncomingBytesPerSecond.Reset();
			ConnectionBudgetBytesPerSecond = 0;
			PoseFrameCountAtStart = NetSync->GetAcceptedPoseFrameCount();
			MeasurementStartTimeSeconds = NowSeconds;
			Presentation->ResetSoldierPresentationDiagnostics(SeedSoldierId);
			NextBandwidthSampleTimeSeconds = NowSeconds + 1.0;
			FGuLiSelectionRequest Request;
			Request.Center = SeedLocation;
			Request.RadiusPreset = EGuLiSelectionRadiusPreset::Small;
			Request.Modifier = EGuLiSelectionModifier::Replace;
			Request.ClientRequestId = FirstCommandId;
			SelectionRevisionBeforeRequest = NetSync->GetSelectionState().SelectionRevision;
			Request.KnownSelectionRevision = SelectionRevisionBeforeRequest;
			ExpectedCommandKind = EGuLiCommandKind::Selection;
			ExpectedCommandId = FirstCommandId;
			CommandSentTimeSeconds = NowSeconds;
			Stage = EStage::WaitingForSelectionAck;
			NetSync->SubmitSelectionRequest(Request);
		}

		void IssueMove(const double NowSeconds)
		{
			if (!NetSync.IsValid())
			{
				return;
			}
			FVector Direction = -SeedLocation.GetSafeNormal2D();
			if (Direction.IsNearlyZero())
			{
				Direction = FVector::ForwardVector;
			}

			FGuLiMoveRequest Request;
			Request.Target = SeedLocation + Direction * (MoveSamplesIssued % 2 == 0 ? 20000.0f : 10000.0f);
			Request.SelectionRevision = NetSync->GetSelectionState().SelectionRevision;
			// The first move deliberately reuses the Selection numeric ID. CommandKind must disambiguate it.
			Request.ClientCommandId = FirstCommandId + static_cast<uint32>(MoveSamplesIssued);
			if (Request.ClientCommandId == 0u)
			{
				Request.ClientCommandId = 1u;
			}
			ExpectedCommandKind = EGuLiCommandKind::Move;
			ExpectedCommandId = Request.ClientCommandId;
			CommandSentTimeSeconds = NowSeconds;
			Stage = EStage::WaitingForMoveAck;
			++MoveSamplesIssued;
			NetSync->SubmitMoveRequest(Request);
		}

		// 按种类+ID 匹配回执；ACK 延迟包含发送调度、网络、服务器处理和回程，并非单纯 ping。
		void HandleAck(const FGuLiCommandAck& Ack)
		{
			if (Stage == EStage::Finished
				|| Ack.CommandKind != ExpectedCommandKind
				|| Ack.ClientCommandId != ExpectedCommandId)
			{
				return;
			}

			const double NowSeconds = FPlatformTime::Seconds();
			const double RoundTripMilliseconds = (NowSeconds - CommandSentTimeSeconds) * 1000.0;
			if (!Ack.IsAccepted())
			{
				Finish(
					false,
					*FString::Printf(
						TEXT("command rejected kind=%u id=%u result=%u"),
						static_cast<uint8>(Ack.CommandKind),
						Ack.ClientCommandId,
						static_cast<uint8>(Ack.Result)));
				return;
			}

			if (Ack.CommandKind == EGuLiCommandKind::Selection)
			{
				SelectionAckRoundTripMilliseconds = RoundTripMilliseconds;
				Stage = EStage::WaitingForSelectionState;
				return;
			}

			MoveAckRoundTripMilliseconds.Add(RoundTripMilliseconds);
			if (MoveAckRoundTripMilliseconds.Num() >= DesiredMoveSamples)
			{
				Stage = EStage::WaitingForPresentationEvidence;
				PresentationEvidenceStartTimeSeconds = NowSeconds;
				PresentedStepCentimeters.Reset();
				PresentedTravelDistanceCentimeters = 0.0;
				if (bHasLastPresentedLocation)
				{
					PresentationEvidenceOrigin = LastPresentedLocation;
					bHasPresentationEvidenceOrigin = true;
				}
				return;
			}

			Stage = EStage::WaitingToIssueMove;
			NextMoveIssueTimeSeconds = NowSeconds + 0.2;
		}

		// 每秒直接采集连接入站 B/s；包含该连接其他复制流量，不把传输/RPC 开销藏在 Mbps 换算后面。
		void CaptureBandwidthSample(const double NowSeconds)
		{
			if (!World.IsValid() || NowSeconds < NextBandwidthSampleTimeSeconds)
			{
				return;
			}
			NextBandwidthSampleTimeSeconds = NowSeconds + 1.0;
			if (const UNetDriver* NetDriver = World->GetNetDriver())
			{
				if (const UNetConnection* Connection = NetDriver->ServerConnection)
				{
					IncomingBytesPerSecond.Add(static_cast<double>(Connection->InBytesPerSecond));
					ConnectionBudgetBytesPerSecond = Connection->CurrentNetSpeed;
				}
			}
		}

		// 测量最终显示位置的帧间位移；移动距离记录相对观察起点的最大二维偏移，不是累计路程。
		void CapturePresentationStep()
		{
			if (!Presentation.IsValid() || !SeedSoldierId.IsValid())
			{
				return;
			}
			FTransform Transform;
			if (!Presentation->TryGetPresentedSoldierTransform(SeedSoldierId, Transform))
			{
				return;
			}
			const FVector Location = Transform.GetLocation();
			if (bHasLastPresentedLocation)
			{
				const double PresentedStepCentimetersValue = FVector::Dist(Location, LastPresentedLocation);
				MaximumPresentedStepCentimeters = FMath::Max(
					MaximumPresentedStepCentimeters,
					static_cast<float>(PresentedStepCentimetersValue));
				if (Stage == EStage::WaitingForPresentationEvidence)
				{
					PresentedStepCentimeters.Add(PresentedStepCentimetersValue);
				}
			}
			if (Stage == EStage::WaitingForPresentationEvidence
				&& bHasPresentationEvidenceOrigin)
			{
				PresentedTravelDistanceCentimeters = FMath::Max(
					PresentedTravelDistanceCentimeters,
					static_cast<double>((Location - PresentationEvidenceOrigin).Size2D()));
			}
			LastPresentedLocation = Location;
			bHasLastPresentedLocation = true;
			++PresentedFrameSamples;
		}

		// 对有限样本排序，取 ceil(N*0.95)-1 下标；必须结合样本数量解释 P95。
		static double Percentile95(TArray<double> Values)
		{
			if (Values.IsEmpty())
			{
				return 0.0;
			}
			Values.Sort();
			const int32 Index = FMath::Clamp(
				FMath::CeilToInt(static_cast<double>(Values.Num()) * 0.95) - 1,
				0,
				Values.Num() - 1);
			return Values[Index];
		}

		static double Average(const TArray<double>& Values)
		{
			if (Values.IsEmpty())
			{
				return 0.0;
			}
			double Sum = 0.0;
			for (const double Value : Values)
			{
				Sum += Value;
			}
			return Sum / static_cast<double>(Values.Num());
		}

		// 收集证据交给 CanPass 统一判定；命令都成功只是必要条件，仍可能因链路/带宽/平滑指标失败。
		void Finish(const bool bCommandsSucceeded, const TCHAR* Reason)
		{
			if (Stage == EStage::Finished)
			{
				return;
			}
			if (NetSync.IsValid() && AckDelegateHandle.IsValid())
			{
				NetSync->OnCommandAckChanged.Remove(AckDelegateHandle);
				AckDelegateHandle.Reset();
			}
			Stage = EStage::Finished;

			const double AckP95Milliseconds = Percentile95(MoveAckRoundTripMilliseconds);
			const double AckAverageMilliseconds = Average(MoveAckRoundTripMilliseconds);
			const double BandwidthP95BytesPerSecond = Percentile95(IncomingBytesPerSecond);
			const double BandwidthAverageBytesPerSecond = Average(IncomingBytesPerSecond);
			const double BandwidthP95Megabits = BandwidthP95BytesPerSecond * 8.0 / 1000000.0;
			const double BandwidthAverageMegabits = BandwidthAverageBytesPerSecond * 8.0 / 1000000.0;
			const double PresentedStepP95Centimeters = Percentile95(PresentedStepCentimeters);
			FGuLiCommanderSoldierPresentationDiagnostics PresentationDiagnostics;
			if (Presentation.IsValid())
			{
				Presentation->TryGetSoldierPresentationDiagnostics(
					SeedSoldierId,
					PresentationDiagnostics);
			}
			const double PresentationClockRoundTripMilliseconds = Presentation.IsValid()
				? static_cast<double>(Presentation->GetLatestClockRoundTripMilliseconds())
				: 0.0;
			const TCHAR* PresentationClockSource = PresentationClockRoundTripMilliseconds > 0.0
				? (Presentation->IsLatestClockRoundTripFromConnectionStats()
					? TEXT("connection")
					: TEXT("player_state"))
				: TEXT("none");
			uint64 FreshPoseFrameCount = 0u;
			if (NetSync.IsValid()
				&& NetSync->GetLastAcceptedPoseReceiveTimeSeconds() >= MeasurementStartTimeSeconds)
			{
				const uint64 CurrentPoseFrameCount = NetSync->GetAcceptedPoseFrameCount();
				FreshPoseFrameCount = CurrentPoseFrameCount >= PoseFrameCountAtStart
					? CurrentPoseFrameCount - PoseFrameCountAtStart
					: CurrentPoseFrameCount;
			}
			FGuLiCommanderNetworkGateEvidence GateEvidence;
			GateEvidence.bCommandsSucceeded = bCommandsSucceeded;
			GateEvidence.bRuntimeImpairmentValid = bRuntimeImpairmentValid;
			GateEvidence.ReceivedMoveAckSamples = MoveAckRoundTripMilliseconds.Num();
			GateEvidence.DesiredMoveAckSamples = DesiredMoveSamples;
			GateEvidence.AckP95Milliseconds = AckP95Milliseconds;
			GateEvidence.BandwidthSampleCount = IncomingBytesPerSecond.Num();
			GateEvidence.BandwidthAverageMegabits = BandwidthAverageMegabits;
			GateEvidence.BandwidthP95Megabits = BandwidthP95Megabits;
			GateEvidence.FreshPoseFrameCount = FreshPoseFrameCount;
			GateEvidence.PresentedStepSampleCount = PresentedStepCentimeters.Num();
			GateEvidence.PresentedTravelDistanceCentimeters = PresentedTravelDistanceCentimeters;
			GateEvidence.PresentedStepP95Centimeters = PresentedStepP95Centimeters;
			GateEvidence.PresentationClockRoundTripMilliseconds = PresentationClockRoundTripMilliseconds;
			GateEvidence.MaximumSeedPoseGapSeconds = PresentationDiagnostics.MaximumPoseReceiptGapSeconds;
			GateEvidence.UntaggedHardSnapCount = PresentationDiagnostics.UntaggedHardSnapCount;
			const bool bGatePassed = GuLiCommanderNetworkGateValidation::CanPass(GateEvidence);

			const FString Summary = FString::Printf(
				TEXT("reason=%s impairment_valid=%d configured_rtt_ms=%d measured_rtt_ms=%.1f jitter_ms=%d loss_pct=%d reordering=%d selection_ack_ms=%.1f move_samples=%d ack_avg_ms=%.1f ack_p95_ms=%.1f bandwidth_samples=%d connection_budget_Bps=%d inbound_avg_Bps=%.0f inbound_p95_Bps=%.0f inbound_avg_mbps=%.3f inbound_p95_mbps=%.3f fresh_pose_frames=%llu presentation_clock_rtt_ms=%.1f clock_rtt_source=%s presentation_step_samples=%d presentation_travel_cm=%.1f presentation_step_p95_cm=%.1f seed_pose_gap_max_ms=%.1f untagged_hard_snaps=%llu last_hard_snap_delta_cm=(%.1f,%.1f,%.1f) prior_sample_delta_cm=(%.1f,%.1f,%.1f) hard_snap_velocity_cmps=(%.1f,%.1f,%.1f) hard_snap_frame_gap=%u hard_snap_server_gap_ms=%.1f hard_snap_chunk_sample=%u:%u->%u:%u current_world_cm=(%.1f,%.1f,%.1f) previous_world_cm=(%.1f,%.1f,%.1f) explicit_teleport_snaps=%llu presented_frames=%d max_presented_step_cm=%.1f."),
				Reason,
				bRuntimeImpairmentValid ? 1 : 0,
				RuntimeImpairment.ConfiguredNominalRoundTripLagMilliseconds,
				RuntimeImpairment.MeasuredRoundTripMilliseconds,
				RuntimeImpairment.ConfiguredJitterMilliseconds,
				RuntimeImpairment.ConfiguredLossPercent,
				RuntimeImpairment.bPacketReorderingEnabled ? 1 : 0,
				SelectionAckRoundTripMilliseconds,
				MoveAckRoundTripMilliseconds.Num(),
				AckAverageMilliseconds,
				AckP95Milliseconds,
				IncomingBytesPerSecond.Num(),
				ConnectionBudgetBytesPerSecond,
				BandwidthAverageBytesPerSecond,
				BandwidthP95BytesPerSecond,
				BandwidthAverageMegabits,
				BandwidthP95Megabits,
				static_cast<unsigned long long>(FreshPoseFrameCount),
				PresentationClockRoundTripMilliseconds,
				PresentationClockSource,
				PresentedStepCentimeters.Num(),
				PresentedTravelDistanceCentimeters,
				PresentedStepP95Centimeters,
				PresentationDiagnostics.MaximumPoseReceiptGapSeconds * 1000.0,
				static_cast<unsigned long long>(PresentationDiagnostics.UntaggedHardSnapCount),
				PresentationDiagnostics.LastUntaggedHardSnapDelta.X,
				PresentationDiagnostics.LastUntaggedHardSnapDelta.Y,
				PresentationDiagnostics.LastUntaggedHardSnapDelta.Z,
				PresentationDiagnostics.LastHardSnapPriorSampleDelta.X,
				PresentationDiagnostics.LastHardSnapPriorSampleDelta.Y,
				PresentationDiagnostics.LastHardSnapPriorSampleDelta.Z,
				PresentationDiagnostics.LastHardSnapSampleVelocity.X,
				PresentationDiagnostics.LastHardSnapSampleVelocity.Y,
				PresentationDiagnostics.LastHardSnapSampleVelocity.Z,
				PresentationDiagnostics.LastHardSnapFrameGap,
				PresentationDiagnostics.LastHardSnapServerTimeGapSeconds * 1000.0,
				PresentationDiagnostics.LastHardSnapPreviousChunkIndex,
				PresentationDiagnostics.LastHardSnapPreviousSampleIndex,
				PresentationDiagnostics.LastHardSnapCurrentChunkIndex,
				PresentationDiagnostics.LastHardSnapCurrentSampleIndex,
				PresentationDiagnostics.LastHardSnapCurrentLocation.X,
				PresentationDiagnostics.LastHardSnapCurrentLocation.Y,
				PresentationDiagnostics.LastHardSnapCurrentLocation.Z,
				PresentationDiagnostics.LastHardSnapPreviousLocation.X,
				PresentationDiagnostics.LastHardSnapPreviousLocation.Y,
				PresentationDiagnostics.LastHardSnapPreviousLocation.Z,
				static_cast<unsigned long long>(PresentationDiagnostics.TeleportSnapCount),
				PresentedFrameSamples,
				MaximumPresentedStepCentimeters);
			if (bGatePassed)
			{
				UE_LOG(LogGuLiStrike, Display, TEXT("Commander network gate PASS: %s"), *Summary);
			}
			else
			{
				UE_LOG(LogGuLiStrike, Error, TEXT("Commander network gate FAIL: %s"), *Summary);
			}
		}

		const int32 DesiredMoveSamples;
		const double StartTimeSeconds;
		uint32 FirstCommandId = 1u;
		uint32 ExpectedCommandId = 0u;
		uint32 SelectionRevisionBeforeRequest = 0u;
		int32 MoveSamplesIssued = 0;
		EGuLiCommandKind ExpectedCommandKind = EGuLiCommandKind::None;
		EStage Stage = EStage::WaitingForBootstrap;
		double CommandSentTimeSeconds = 0.0;
		double FirstImpairmentCheckTimeSeconds = 0.0;
		double NextMoveIssueTimeSeconds = 0.0;
		double NextBandwidthSampleTimeSeconds = 0.0;
		double SelectionAckRoundTripMilliseconds = 0.0;
		double PresentationEvidenceStartTimeSeconds = 0.0;
		double PresentedTravelDistanceCentimeters = 0.0;
		TArray<double> MoveAckRoundTripMilliseconds;
		TArray<double> IncomingBytesPerSecond;
		int32 ConnectionBudgetBytesPerSecond = 0;
		TArray<double> PresentedStepCentimeters;
		FGuLiCommanderNetworkImpairmentEvidence RuntimeImpairment;
		FGuLiSoldierId SeedSoldierId;
		FVector SeedLocation = FVector::ZeroVector;
		FVector LastPresentedLocation = FVector::ZeroVector;
		FVector PresentationEvidenceOrigin = FVector::ZeroVector;
		float MaximumPresentedStepCentimeters = 0.0f;
		int32 PresentedFrameSamples = 0;
		uint64 PoseFrameCountAtStart = 0u;
		double MeasurementStartTimeSeconds = 0.0;
		bool bHasLastPresentedLocation = false;
		bool bHasPresentationEvidenceOrigin = false;
		bool bRuntimeImpairmentValid = false;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AGuLiCommanderPlayerController> Controller;
		TWeakObjectPtr<UGuLiCommanderNetSyncComponent> NetSync;
		TWeakObjectPtr<AGuLiSoldierStateReplicator> StateReplicator;
		TWeakObjectPtr<AGuLiCommanderPresentationActor> Presentation;
		FDelegateHandle AckDelegateHandle;
		FTSTicker::FDelegateHandle TickerHandle;
	};

	TSharedPtr<FRunner> ActiveRunner;

	// 控制台 gs.Commander.NetworkGate [move_ack_samples]，默认 20、内部限制 5..100。
	// 使用独立测试会话：运行器自行分配较大的命令序号，可能使同连接后续普通输入序号被视为旧请求。
	void StartNetworkGate(const TArray<FString>& Args, UWorld* CommandWorld)
	{
		(void)CommandWorld;
		if (ActiveRunner.IsValid() && !ActiveRunner->IsFinished())
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander network gate is already running."));
			return;
		}
		const int32 DesiredSamples = Args.IsEmpty() ? 20 : FCString::Atoi(*Args[0]);
		ActiveRunner = MakeShared<FRunner>(DesiredSamples);
		ActiveRunner->Start();
	}

	FAutoConsoleCommandWithWorldAndArgs NetworkGateCommand(
		TEXT("gs.Commander.NetworkGate"),
		TEXT("Client network impairment gate: gs.Commander.NetworkGate [move_ack_samples]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartNetworkGate));
}

#endif // !UE_BUILD_SHIPPING
