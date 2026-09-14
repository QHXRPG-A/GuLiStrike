// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanTargetAcquisition.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	constexpr uint32 PrivateTrajectoryCaptureIntervalTicks = 6u;
	// Normal 5 Hz uploads span six 30 Hz simulation ticks. Capturing at 15 Hz
	// gives the authority two interior points, so it validates the flown arc
	// instead of a straight chord that may cut across a FlightNav portal corner.
	constexpr uint32 ActiveTrajectoryCaptureIntervalTicks = 1u;
	constexpr int32 RetainedTrajectoryHistoryCapacityPerFlight = 16;
	/**
	 * Resume/Takeover trails are validated against the authority's 3.5 second capture window
	 * after crossing the network. Keeping at most the newest two seconds relative to the
	 * endpoint leaves bounded room for one-way latency and a reliable-RPC retry.
	 */
	constexpr double RetainedTrajectoryMaximumWireAgeSeconds = 2.0;

	EGuLiRelayCarrierLookupResult ResolveCarrierFromMovement(
		const TWeakObjectPtr<UGuLiShipMovementComponent>& WeakMovement,
		const FGuLiCarrierSourceRef& Source,
		FGuLiRelayCarrierState& OutState)
	{
		OutState = FGuLiRelayCarrierState{};
		const UGuLiShipMovementComponent* Movement = WeakMovement.Get();
		if (!Movement || !Source.IsValid())
		{
			return EGuLiRelayCarrierLookupResult::Rejected;
		}

		FGuLiShipCanonicalMoveState CanonicalState;
		switch (Movement->LookupCanonicalMove(Source.CanonicalEpoch, Source.MoveRevision, CanonicalState))
		{
		case EGuLiShipCanonicalMoveLookupResult::Found:
			OutState.Transform = CanonicalState.Transform;
			OutState.Velocity = CanonicalState.Velocity;
			OutState.ServerWorldTimeSeconds = CanonicalState.ServerWorldTimeSeconds;
			return OutState.IsWellFormed()
				? EGuLiRelayCarrierLookupResult::Found : EGuLiRelayCarrierLookupResult::Rejected;
		case EGuLiShipCanonicalMoveLookupResult::Pending:
			return EGuLiRelayCarrierLookupResult::Pending;
		case EGuLiShipCanonicalMoveLookupResult::Unavailable:
			return Movement->GetCanonicalEpoch() == Source.CanonicalEpoch
				? EGuLiRelayCarrierLookupResult::Pending : EGuLiRelayCarrierLookupResult::Rejected;
		case EGuLiShipCanonicalMoveLookupResult::Expired:
			return EGuLiRelayCarrierLookupResult::Expired;
		default:
			return EGuLiRelayCarrierLookupResult::Rejected;
		}
	}

	bool GetLatestCarrierFromMovement(
		const TWeakObjectPtr<UGuLiShipMovementComponent>& WeakMovement,
		FGuLiCarrierSourceRef& OutSource)
	{
		OutSource = FGuLiCarrierSourceRef{};
		const UGuLiShipMovementComponent* Movement = WeakMovement.Get();
		FGuLiShipCanonicalMoveState State;
		if (!Movement || !Movement->GetLatestCanonicalMove(State) || !State.IsValid())
		{
			return false;
		}
		OutSource.CanonicalEpoch = State.CanonicalEpoch;
		OutSource.MoveRevision = State.MoveRevision;
		return OutSource.IsValid();
	}

	void ConstrainPublishedFlightModesToAcceptedBaseline(
		FGuLiWingmanCandidateBatch& Candidate,
		const FGuLiWingmanAcceptedBatch& AcceptedBaseline)
	{
		if (!Candidate.UsesStrictFlightContract() || !AcceptedBaseline.IsWellFormed()
			|| Candidate.Group != AcceptedBaseline.Group
			|| Candidate.FlightIndex != AcceptedBaseline.FlightIndex)
		{
			return;
		}

		constexpr int32 MaximumPublishedModeStep = 2;
		const int32 MaximumLiveMode = static_cast<int32>(EGuLiWingmanFlightMode::Recover);
		TMap<FGuLiWingmanHandle, uint8> PreviousModes;
		for (const FGuLiWingmanCandidateSample& Sample : AcceptedBaseline.Samples)
		{
			if (Sample.FlightMode <= MaximumLiveMode)
			{
				PreviousModes.Add(Sample.Wingman, Sample.FlightMode);
			}
		}

		auto ConstrainFrame = [&PreviousModes, MaximumLiveMode, MaximumPublishedModeStep](
			TArray<FGuLiWingmanCandidateSample>& FrameSamples)
		{
			for (FGuLiWingmanCandidateSample& Sample : FrameSamples)
			{
				uint8* PreviousValue = PreviousModes.Find(Sample.Wingman);
				const int32 DesiredMode = static_cast<int32>(Sample.FlightMode);
				if (DesiredMode < 0 || DesiredMode > MaximumLiveMode)
				{
					continue;
				}
				if (!PreviousValue)
				{
					// A replenished EntityGeneration must not inherit the retired identity's
					// Accepted mode. Its first live frame establishes only this candidate's
					// local chain so later trails/endpoint obey the authority's transition gate.
					PreviousModes.Add(Sample.Wingman, Sample.FlightMode);
					continue;
				}
				const int32 PreviousMode = static_cast<int32>(*PreviousValue);
				if (PreviousMode < 0 || PreviousMode > MaximumLiveMode)
				{
					continue;
				}
				const int32 Delta = DesiredMode - PreviousMode;
				if (FMath::Abs(Delta) > MaximumPublishedModeStep)
				{
					Sample.FlightMode = static_cast<uint8>(PreviousMode
						+ FMath::Clamp(Delta, -MaximumPublishedModeStep, MaximumPublishedModeStep));
				}
				*PreviousValue = Sample.FlightMode;
			}
		};
		for (FGuLiWingmanCandidateTrailSample& Trail : Candidate.TrailSamples)
		{
			ConstrainFrame(Trail.Samples);
		}
		ConstrainFrame(Candidate.Samples);
	}

	uint32 GetBootstrapLeaseEpoch(const FGuLiWingmanBootstrapBundle& Bootstrap)
	{
		return Bootstrap.AuthorityMap.IsEmpty() ? 0u : Bootstrap.AuthorityMap[0].LeaseEpoch;
	}

	uint32 AdvanceNonZeroSequence(const uint32 Current, const uint32 Delta)
	{
		if (Current == 0u || Delta == 0u)
		{
			return Current;
		}
		const uint64 ZeroBased = static_cast<uint64>(Current - 1u);
		return static_cast<uint32>((ZeroBased + Delta) % static_cast<uint64>(MAX_uint32)) + 1u;
	}

	uint32 ElapsedNonZeroSequenceTicks(const uint32 Previous, const uint32 Current)
	{
		if (Current == 0u)
		{
			return 0u;
		}
		if (Previous == 0u)
		{
			return Current;
		}
		return Current >= Previous
			? Current - Previous
			: (MAX_uint32 - Previous) + Current;
	}

	bool IsStrictlyNewerNonZeroSequence(const uint32 Candidate, const uint32 Previous)
	{
		return Candidate != 0u
			&& (Previous == 0u || static_cast<int32>(Candidate - Previous) > 0);
	}

	uint32 SelectBootstrapClientSimulationTick(
		const uint32 CurrentTick,
		const uint32 EffectiveTick,
		const uint32 RebasedTick,
		const bool bPreserveCurrentTick)
	{
		return FMath::Max3(
			bPreserveCurrentTick ? FMath::Max(1u, CurrentTick) : 1u,
			EffectiveTick,
			RebasedTick);
	}

	bool IsRetainedOwnerPrivateLifecycle(const EGuLiWingmanGroupLifecycle Lifecycle)
	{
		return Lifecycle == EGuLiWingmanGroupLifecycle::Initializing
			|| Lifecycle == EGuLiWingmanGroupLifecycle::Stale
			|| Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable;
	}

	bool StoreRetainedTrajectoryEndpoint(
		TArray<FGuLiWingmanCandidateTrailSample>& History,
		const FGuLiWingmanCandidateBatch& Candidate)
	{
		if (!Candidate.UsesStrictFlightContract() || !Candidate.IsWellFormed())
		{
			return false;
		}

		FGuLiWingmanCandidateTrailSample Sample;
		Sample.ClientSimTick = Candidate.ClientSimTick;
		Sample.CaptureEstimatedServerTimeSeconds = Candidate.CaptureEstimatedServerTimeSeconds;
		Sample.CarrierSource = Candidate.CarrierSource;
		Sample.Samples = Candidate.Samples;
		if (!Sample.IsWellFormed(Candidate.Group, Candidate.FlightIndex, Candidate.RequiredMemberMask))
		{
			return false;
		}

		if (!History.IsEmpty())
		{
			const FGuLiWingmanCandidateTrailSample& Last = History.Last();
			if (Sample.ClientSimTick <= Last.ClientSimTick)
			{
				if (!IsStrictlyNewerNonZeroSequence(Sample.ClientSimTick, Last.ClientSimTick))
				{
					return false;
				}
				// Trail wire ordering is numerically monotonic. Start a new local window at wrap.
				History.Reset();
			}
			else if (Sample.CaptureEstimatedServerTimeSeconds
				<= Last.CaptureEstimatedServerTimeSeconds)
			{
				return false;
			}
		}

		History.Add(MoveTemp(Sample));
		if (History.Num() > RetainedTrajectoryHistoryCapacityPerFlight)
		{
			History.RemoveAt(
				0,
				History.Num() - RetainedTrajectoryHistoryCapacityPerFlight,
				EAllowShrinking::No);
		}
		return true;
	}

	bool AppendSelectedRetainedTrajectory(
		FGuLiWingmanCandidateBatch& Candidate,
		const FGuLiWingmanAcceptedBatch& AcceptedBaseline,
		const TArray<FGuLiWingmanCandidateTrailSample>& History,
		const TArray<uint32>& RequiredFireTicks = {})
	{
		Candidate.TrailSamples.Reset();
		if (!Candidate.UsesStrictFlightContract() || !Candidate.IsWellFormed()
			|| !AcceptedBaseline.IsWellFormed()
			|| AcceptedBaseline.Group != Candidate.Group
			|| AcceptedBaseline.FlightIndex != Candidate.FlightIndex
			|| AcceptedBaseline.StateRef.AcceptedSequence != Candidate.BaseAcceptedSequence)
		{
			return false;
		}

		TArray<const FGuLiWingmanCandidateTrailSample*,
			TInlineAllocator<RetainedTrajectoryHistoryCapacityPerFlight>> Eligible;
		const double EarliestWireCaptureTime =
			Candidate.CaptureEstimatedServerTimeSeconds
			- RetainedTrajectoryMaximumWireAgeSeconds;
		for (const FGuLiWingmanCandidateTrailSample& Trail : History)
		{
			if (Trail.ClientSimTick <= AcceptedBaseline.StateRef.ClientSimTick
				|| Trail.ClientSimTick >= Candidate.ClientSimTick
				|| Trail.CaptureEstimatedServerTimeSeconds
					<= AcceptedBaseline.CaptureEstimatedServerTimeSeconds
				|| Trail.CaptureEstimatedServerTimeSeconds < EarliestWireCaptureTime
				|| Trail.CaptureEstimatedServerTimeSeconds
					>= Candidate.CaptureEstimatedServerTimeSeconds)
			{
				continue;
			}
			if (Trail.Samples.ContainsByPredicate([](const FGuLiWingmanCandidateSample& Sample)
			{
				return Sample.FlightMode
					> static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
			}))
			{
				// Protocol well-formedness reserves Stale as a wire value, but an owner
				// may never use Stale/unknown private history as Resume continuity proof.
				continue;
			}

			FGuLiWingmanCandidateBatch Probe = Candidate;
			Probe.TrailSamples.Add(Trail);
			if (Probe.IsWellFormed())
			{
				Eligible.Add(&Trail);
			}
		}
		if (Eligible.IsEmpty())
		{
			return false;
		}

		const int32 WireSampleCount = FMath::Min(
			Eligible.Num(), static_cast<int32>(GULI_WINGMAN_MAX_TRAIL_SAMPLES));
		if (!RequiredFireTicks.IsEmpty() && Eligible.Num() > WireSampleCount)
		{
			// Every emitted shot needs its actual captured pose. Preserve those ticks
			// first, then distribute the remaining continuity evidence across the arc.
			TArray<int32> Selected;
			for (int32 Index = 0; Index < Eligible.Num() && Selected.Num() < WireSampleCount; ++Index)
				if (RequiredFireTicks.Contains(Eligible[Index]->ClientSimTick)) Selected.Add(Index);
			for (int32 Index = 0; Index < WireSampleCount && Selected.Num() < WireSampleCount; ++Index)
				Selected.AddUnique(FMath::Min(Eligible.Num() - 1, (Index + 1) * Eligible.Num() / (WireSampleCount + 1)));
			for (int32 Index = 0; Index < Eligible.Num() && Selected.Num() < WireSampleCount; ++Index) Selected.AddUnique(Index);
			Selected.Sort();
			for (int32 Index : Selected) Candidate.TrailSamples.Add(*Eligible[Index]);
		}
		else if (Eligible.Num() <= WireSampleCount)
		{
			for (const FGuLiWingmanCandidateTrailSample* Trail : Eligible)
			{
				Candidate.TrailSamples.Add(*Trail);
			}
		}
		else
		{
			const double StartTime = FMath::Max(
				AcceptedBaseline.CaptureEstimatedServerTimeSeconds,
				EarliestWireCaptureTime);
			const double Duration = Candidate.CaptureEstimatedServerTimeSeconds - StartTime;
			int32 PreviousIndex = INDEX_NONE;
			for (int32 SelectionIndex = 0; SelectionIndex < WireSampleCount; ++SelectionIndex)
			{
				const double TargetTime = StartTime + Duration
					* static_cast<double>(SelectionIndex + 1)
					/ static_cast<double>(WireSampleCount + 1);
				const int32 MinimumIndex = PreviousIndex + 1;
				const int32 RemainingSelections = WireSampleCount - SelectionIndex - 1;
				const int32 MaximumIndex = Eligible.Num() - RemainingSelections - 1;
				int32 BestIndex = MinimumIndex;
				double BestDistance = FMath::Abs(
					Eligible[BestIndex]->CaptureEstimatedServerTimeSeconds - TargetTime);
				for (int32 Index = MinimumIndex + 1; Index <= MaximumIndex; ++Index)
				{
					const double Distance = FMath::Abs(
						Eligible[Index]->CaptureEstimatedServerTimeSeconds - TargetTime);
					if (Distance < BestDistance)
					{
						BestDistance = Distance;
						BestIndex = Index;
					}
				}
				Candidate.TrailSamples.Add(*Eligible[BestIndex]);
				PreviousIndex = BestIndex;
			}
		}

		if (!Candidate.IsWellFormed())
		{
			Candidate.TrailSamples.Reset();
			return false;
		}
		return !Candidate.TrailSamples.IsEmpty();
	}

	void PruneRetainedTrajectoryAtAcceptedBaseline(
		TArray<FGuLiWingmanCandidateTrailSample>& History,
		const FGuLiWingmanAcceptedBatch& Accepted)
	{
		History.RemoveAll([&Accepted](const FGuLiWingmanCandidateTrailSample& Trail)
		{
			return Trail.ClientSimTick <= Accepted.StateRef.ClientSimTick
				|| Trail.CaptureEstimatedServerTimeSeconds
					<= Accepted.CaptureEstimatedServerTimeSeconds;
		});
	}

	bool TryAdvanceAcceptedSequenceFromRebase(
		const FGuLiWingmanSimulationAcceptance& Acceptance,
		const FGuLiWingmanGroupHandle& ExpectedGroup,
		const uint32 ExpectedLeaseEpoch,
		const uint32 ExpectedRosterRevision,
		const uint32 ExpectedMatchEpoch,
		uint32& InOutAcceptedSequence)
	{
		if (Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Accepted
			|| !Acceptance.IsWellFormed()
			|| !ExpectedGroup.IsValid()
			|| Acceptance.Group != ExpectedGroup
			|| Acceptance.LeaseEpoch != ExpectedLeaseEpoch
			|| Acceptance.RosterRevision != ExpectedRosterRevision
			|| Acceptance.FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
			|| Acceptance.AcceptedSnapshotSequence == 0u
			|| !Acceptance.RebaseBaseline.IsValid()
			|| Acceptance.RebaseBaseline.MatchEpoch != ExpectedMatchEpoch
			|| Acceptance.RebaseBaseline.GroupGeneration != ExpectedGroup.GroupGeneration
			|| Acceptance.RebaseBaseline.AcceptedSequence
				!= Acceptance.AcceptedSnapshotSequence
			|| !IsStrictlyNewerNonZeroSequence(
				Acceptance.AcceptedSnapshotSequence, InOutAcceptedSequence))
		{
			return false;
		}

		InOutAcceptedSequence = Acceptance.AcceptedSnapshotSequence;
		return true;
	}

	struct FActiveRosterCutAcceptedPlan
	{
		TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> AcceptedSequences{};
		TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> NextFrameSequences{};
		TStaticArray<FGuLiWingmanAcceptedBatch, GULI_WINGMAN_FLIGHT_COUNT> SafeBaselines{};
		TArray<FGuLiWingmanAcceptedBatch> SourceRefBatches;
		FGuLiWingmanAcceptedBatch LatestSafeBaseline;
	};

	bool BuildActiveRosterCutAcceptedPlan(
		const FGuLiWingmanBootstrapBundle& Bootstrap,
		const uint32 ExpectedMatchEpoch,
		const TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT>& CurrentAcceptedSequences,
		const TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT>& CurrentNextFrameSequences,
		const TStaticArray<FGuLiWingmanAcceptedBatch, GULI_WINGMAN_FLIGHT_COUNT>& CurrentBaselines,
		FActiveRosterCutAcceptedPlan& OutPlan)
	{
		OutPlan = FActiveRosterCutAcceptedPlan{};
		if (!Bootstrap.bActiveRosterRefresh || !Bootstrap.Commit.Group.IsValid()
			|| ExpectedMatchEpoch == 0u || Bootstrap.ConnectionGeneration == 0u)
		{
			return false;
		}

		OutPlan.AcceptedSequences = CurrentAcceptedSequences;
		OutPlan.NextFrameSequences = CurrentNextFrameSequences;
		OutPlan.SafeBaselines = CurrentBaselines;
		uint8 SeenFlightMask = 0u;
		for (const FGuLiWingmanAcceptedBatch& Baseline : Bootstrap.AcceptedSnapshot)
		{
			if (!Baseline.IsWellFormed() || !Baseline.UsesStrictFlightContract()
				|| Baseline.Group != Bootstrap.Commit.Group
				|| Baseline.StateRef.MatchEpoch != ExpectedMatchEpoch
				|| Baseline.ConnectionGeneration != Bootstrap.ConnectionGeneration
				|| Baseline.FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
				|| Baseline.AbilitySetRevision != Bootstrap.AbilityConfig.AbilitySetRevision
				|| Baseline.FormationCommandRevision
					!= Bootstrap.AbilityConfig.FormationCommandRevision
				|| Baseline.FormationDefinitionChecksum
					!= Bootstrap.AbilityConfig.FormationDefinitionChecksum)
			{
				return false;
			}
			const uint8 FlightBit = static_cast<uint8>(1u << Baseline.FlightIndex);
			if ((SeenFlightMask & FlightBit) != 0u)
			{
				return false;
			}
			SeenFlightMask |= FlightBit;

			uint32& AcceptedHighWater = OutPlan.AcceptedSequences[Baseline.FlightIndex];
			const bool bBaselineIsCurrent = AcceptedHighWater == Baseline.StateRef.AcceptedSequence;
			const bool bBaselineIsNewer = IsStrictlyNewerNonZeroSequence(
				Baseline.StateRef.AcceptedSequence, AcceptedHighWater);
			if (!bBaselineIsCurrent && !bBaselineIsNewer)
			{
				// A delayed retry of an older Cut cannot roll a newer rejection/acceptance rebase back.
				continue;
			}
			AcceptedHighWater = Baseline.StateRef.AcceptedSequence;

			uint32& NextFrameSequence = OutPlan.NextFrameSequences[Baseline.FlightIndex];
			if (!IsStrictlyNewerNonZeroSequence(NextFrameSequence, Baseline.FrameSequence))
			{
				NextFrameSequence = Baseline.FrameSequence == MAX_uint32
					? 1u : Baseline.FrameSequence + 1u;
			}

			FGuLiWingmanAcceptedBatch SafeBaseline = Baseline;
			SafeBaseline.Samples.RemoveAll([&Bootstrap](const FGuLiWingmanCandidateSample& Sample)
			{
				const FGuLiWingmanRosterEntry* Current = Bootstrap.Roster.FindByPredicate(
					[&Sample](const FGuLiWingmanRosterEntry& Entry)
					{
						return Entry.Wingman == Sample.Wingman;
					});
				return !Current || Current->bDead;
			});
			if (SafeBaseline.Samples.IsEmpty())
			{
				// A stable slot may now contain a replacement generation. Never leave the old
				// generation's Accepted source cached as a fallback for that slot.
				OutPlan.SafeBaselines[Baseline.FlightIndex] = FGuLiWingmanAcceptedBatch{};
				continue;
			}
			SafeBaseline.RefreshHash();
			if (!SafeBaseline.IsWellFormed())
			{
				return false;
			}
			OutPlan.SafeBaselines[Baseline.FlightIndex] = SafeBaseline;
			OutPlan.SourceRefBatches.Add(SafeBaseline);
		}
		for (const FGuLiWingmanAcceptedBatch& SafeBaseline : OutPlan.SafeBaselines)
		{
			if (SafeBaseline.IsWellFormed()
				&& (!OutPlan.LatestSafeBaseline.IsWellFormed()
					|| SafeBaseline.ServerAcceptedTimeSeconds
						> OutPlan.LatestSafeBaseline.ServerAcceptedTimeSeconds))
			{
				OutPlan.LatestSafeBaseline = SafeBaseline;
			}
		}
		return true;
	}

	uint32 SelectPublishedRosterRevision(
		const FGuLiWingmanGroupHandle& ReplicatedGroup,
		const uint32 ReplicatedLeaseEpoch,
		const uint32 ReplicatedRosterRevision,
		const FGuLiWingmanBootstrapBundle& LastAppliedBootstrap)
	{
		return LastAppliedBootstrap.bActiveRosterRefresh
			&& LastAppliedBootstrap.Commit.Group == ReplicatedGroup
			&& GetBootstrapLeaseEpoch(LastAppliedBootstrap) == ReplicatedLeaseEpoch
			&& LastAppliedBootstrap.RosterRevision != 0u
			? LastAppliedBootstrap.RosterRevision : ReplicatedRosterRevision;
	}

	bool BootstrapCommitMatches(
		const FGuLiWingmanBootstrapCommit& Lhs,
		const FGuLiWingmanBootstrapCommit& Rhs)
	{
		if (Lhs.ProtocolVersion != Rhs.ProtocolVersion || Lhs.CutId != Rhs.CutId
			|| Lhs.Group != Rhs.Group || Lhs.Scopes.Num() != Rhs.Scopes.Num())
		{
			return false;
		}
		for (const FGuLiWingmanBootstrapScopeState& LhsState : Lhs.Scopes)
		{
			const FGuLiWingmanBootstrapScopeState* RhsState = Rhs.FindScope(LhsState.Scope);
			if (!RhsState || LhsState.Revision != RhsState->Revision
				|| LhsState.Hash != RhsState->Hash || LhsState.ChunkCount != RhsState->ChunkCount)
			{
				return false;
			}
		}
		return true;
	}

	bool BootstrapRetryContractMatches(
		const FGuLiWingmanBootstrapBundle& Lhs,
		const FGuLiWingmanBootstrapBundle& Rhs)
	{
		const bool bValidationRevisionsMatch =
			Lhs.ValidationRevisions.NavSchemaRevision == Rhs.ValidationRevisions.NavSchemaRevision
			&& Lhs.ValidationRevisions.NavDataChecksum == Rhs.ValidationRevisions.NavDataChecksum
			&& Lhs.ValidationRevisions.TuningRevision == Rhs.ValidationRevisions.TuningRevision
			&& Lhs.ValidationRevisions.ObstacleRevision == Rhs.ValidationRevisions.ObstacleRevision;
		const bool bUploadRateGrantMatches =
			Lhs.UploadRateGrant.ProtocolVersion == Rhs.UploadRateGrant.ProtocolVersion
			&& Lhs.UploadRateGrant.Group == Rhs.UploadRateGrant.Group
			&& Lhs.UploadRateGrant.ConnectionGeneration == Rhs.UploadRateGrant.ConnectionGeneration
			&& Lhs.UploadRateGrant.LeaseEpoch == Rhs.UploadRateGrant.LeaseEpoch
			&& Lhs.UploadRateGrant.RateClass == Rhs.UploadRateGrant.RateClass
			&& Lhs.UploadRateGrant.GrantRevision == Rhs.UploadRateGrant.GrantRevision
			&& Lhs.UploadRateGrant.EffectiveClientSimTick
				== Rhs.UploadRateGrant.EffectiveClientSimTick
			&& Lhs.UploadRateGrant.ExpiryServerTimeSeconds
				== Rhs.UploadRateGrant.ExpiryServerTimeSeconds
			&& Lhs.UploadRateGrant.Reason == Rhs.UploadRateGrant.Reason;
		const bool bTransferBaselineMatches = !Lhs.bHasTransferBaseline
			|| (Lhs.TransferBaseline.ProtocolVersion == Rhs.TransferBaseline.ProtocolVersion
				&& Lhs.TransferBaseline.CutId == Rhs.TransferBaseline.CutId
				&& Lhs.TransferBaseline.Group == Rhs.TransferBaseline.Group
				&& Lhs.TransferBaseline.LeaseEpoch == Rhs.TransferBaseline.LeaseEpoch
				&& Lhs.TransferBaseline.AbilityConfigRevision
					== Rhs.TransferBaseline.AbilityConfigRevision
				&& Lhs.TransferBaseline.AbilityConfigHash == Rhs.TransferBaseline.AbilityConfigHash
				&& Lhs.TransferBaseline.AcceptedSnapshotRevision
					== Rhs.TransferBaseline.AcceptedSnapshotRevision
				&& Lhs.TransferBaseline.AcceptedSnapshotHash
					== Rhs.TransferBaseline.AcceptedSnapshotHash);
		return BootstrapCommitMatches(Lhs.Commit, Rhs.Commit)
			&& GetBootstrapLeaseEpoch(Lhs) == GetBootstrapLeaseEpoch(Rhs)
			&& Lhs.ConnectionGeneration == Rhs.ConnectionGeneration
			&& Lhs.RosterRevision == Rhs.RosterRevision
			&& bValidationRevisionsMatch
			&& bUploadRateGrantMatches
			&& Lhs.bRequiresAtomicCandidateBatch == Rhs.bRequiresAtomicCandidateBatch
			&& Lhs.bActiveRosterRefresh == Rhs.bActiveRosterRefresh
			&& Lhs.AtomicBatchKind == Rhs.AtomicBatchKind
			&& Lhs.AtomicBaselineRevision == Rhs.AtomicBaselineRevision
			&& Lhs.AtomicBaselineHash == Rhs.AtomicBaselineHash
			&& Lhs.RequiredFlightMask == Rhs.RequiredFlightMask
			&& Lhs.RequiredMemberMaskHash == Rhs.RequiredMemberMaskHash
			&& Lhs.bHasTransferBaseline == Rhs.bHasTransferBaseline
			&& bTransferBaselineMatches;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPublishedFlightModeConstraintTest,
	"GuLiStrike.Wingman.Network.ClientPublishFlightModeConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiPublishedFlightModeConstraintTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGuLiWingmanGroupHandle Group;
	Group.ShipInstanceId = FGuid(1u, 2u, 3u, 4u);
	Group.ShipGeneration = 1u;
	Group.GroupGeneration = 1u;

	FGuLiWingmanHandle Wingman;
	Wingman.Flight.Group = Group;
	Wingman.Flight.FlightIndex = 0u;
	Wingman.MemberIndex = 0u;
	Wingman.EntityGeneration = 1u;

	FGuLiWingmanAcceptedBatch Baseline;
	Baseline.Group = Group;
	Baseline.StateRef.MatchEpoch = 1u;
	Baseline.StateRef.GroupGeneration = Group.GroupGeneration;
	Baseline.StateRef.AcceptedSequence = 1u;
	Baseline.StateRef.ClientSimTick = 1u;
	Baseline.CarrierSource.CanonicalEpoch = 1u;
	Baseline.CarrierSource.MoveRevision = 1u;
	Baseline.ConnectionGeneration = 1u;
	Baseline.RosterRevision = 1u;
	Baseline.FlightIndex = Wingman.Flight.FlightIndex;
	Baseline.FrameSequence = 1u;
	Baseline.CaptureEstimatedServerTimeSeconds = 0.0;
	Baseline.AbilitySetRevision = 1u;
	Baseline.FormationCommandRevision = 1u;
	Baseline.FormationDefinitionChecksum = 1u;
	Baseline.ServerAcceptedTimeSeconds = 0.0;
	FGuLiWingmanCandidateSample& AcceptedSample = Baseline.Samples.AddDefaulted_GetRef();
	AcceptedSample.Wingman = Wingman;
	Baseline.RefreshHash();
	if (!TestTrue(TEXT("The fixture is a valid strict Accepted baseline"), Baseline.IsWellFormed()))
	{
		return false;
	}

	constexpr int32 MaximumPublishedModeStep = 2;
	const int32 MaximumLiveMode = static_cast<int32>(EGuLiWingmanFlightMode::Recover);
	for (int32 PreviousMode = static_cast<int32>(EGuLiWingmanFlightMode::Orbit);
		PreviousMode <= MaximumLiveMode; ++PreviousMode)
	{
		Baseline.Samples[0].FlightMode = static_cast<uint8>(PreviousMode);
		Baseline.RefreshHash();
		for (int32 DesiredMode = static_cast<int32>(EGuLiWingmanFlightMode::Orbit);
			DesiredMode <= MaximumLiveMode; ++DesiredMode)
		{
			FGuLiWingmanCandidateBatch Candidate;
			Candidate.Group = Group;
			Candidate.FlightIndex = Wingman.Flight.FlightIndex;
			FGuLiWingmanCandidateSample& Published = Candidate.Samples.AddDefaulted_GetRef();
			Published.Wingman = Wingman;
			Published.FlightMode = static_cast<uint8>(DesiredMode);
			ConstrainPublishedFlightModesToAcceptedBaseline(Candidate, Baseline);

			const int32 ExpectedMode = PreviousMode + FMath::Clamp(
				DesiredMode - PreviousMode, -MaximumPublishedModeStep, MaximumPublishedModeStep);
			TestEqual(
				FString::Printf(TEXT("Mode %d -> %d publishes the server-legal boundary"),
					PreviousMode, DesiredMode),
				static_cast<int32>(Published.FlightMode), ExpectedMode);
		}
	}

	FGuLiWingmanCandidateBatch NewGenerationCandidate;
	NewGenerationCandidate.Group = Group;
	NewGenerationCandidate.FlightIndex = Wingman.Flight.FlightIndex;
	FGuLiWingmanCandidateSample& NewGeneration = NewGenerationCandidate.Samples.AddDefaulted_GetRef();
	NewGeneration.Wingman = Wingman;
	++NewGeneration.Wingman.EntityGeneration;
	NewGeneration.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
	Baseline.Samples[0].FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
	Baseline.RefreshHash();
	ConstrainPublishedFlightModesToAcceptedBaseline(NewGenerationCandidate, Baseline);
	TestEqual(TEXT("A replacement identity does not inherit the prior generation's mode"),
		NewGeneration.FlightMode, static_cast<uint8>(EGuLiWingmanFlightMode::Recover));
	FGuLiWingmanCandidateBatch NewGenerationSequence;
	NewGenerationSequence.Group = Group;
	NewGenerationSequence.FlightIndex = Wingman.Flight.FlightIndex;
	FGuLiWingmanCandidateTrailSample& NewGenerationTrail =
		NewGenerationSequence.TrailSamples.AddDefaulted_GetRef();
	FGuLiWingmanCandidateSample& NewGenerationTrailMode =
		NewGenerationTrail.Samples.AddDefaulted_GetRef();
	NewGenerationTrailMode.Wingman = NewGeneration.Wingman;
	NewGenerationTrailMode.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
	FGuLiWingmanCandidateSample& NewGenerationEndpointMode =
		NewGenerationSequence.Samples.AddDefaulted_GetRef();
	NewGenerationEndpointMode.Wingman = NewGeneration.Wingman;
	NewGenerationEndpointMode.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
	ConstrainPublishedFlightModesToAcceptedBaseline(NewGenerationSequence, Baseline);
	TestEqual(TEXT("A replacement's first retained frame establishes its own mode chain"),
		NewGenerationTrailMode.FlightMode,
		static_cast<uint8>(EGuLiWingmanFlightMode::Recover));
	TestEqual(TEXT("A replacement endpoint is clamped from its retained frame"),
		NewGenerationEndpointMode.FlightMode,
		static_cast<uint8>(EGuLiWingmanFlightMode::Follow));

	FGuLiWingmanCandidateBatch FailClosedCandidate;
	FailClosedCandidate.Group = Group;
	FailClosedCandidate.FlightIndex = Wingman.Flight.FlightIndex;
	FGuLiWingmanCandidateSample& FailClosed = FailClosedCandidate.Samples.AddDefaulted_GetRef();
	FailClosed.Wingman = Wingman;
	FailClosed.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Stale);
	ConstrainPublishedFlightModesToAcceptedBaseline(FailClosedCandidate, Baseline);
	TestEqual(TEXT("Stale remains invalid for the authority to reject rather than wrapping into a live mode"),
		FailClosed.FlightMode, static_cast<uint8>(EGuLiWingmanFlightMode::Stale));
	FailClosed.FlightMode = MAX_uint8;
	ConstrainPublishedFlightModesToAcceptedBaseline(FailClosedCandidate, Baseline);
	TestEqual(TEXT("An unknown wire value remains fail-closed and never wraps into a live mode"),
		FailClosed.FlightMode, MAX_uint8);
	TestFalse(TEXT("The protocol rejects that unknown sample before authority transition validation"),
		FailClosed.IsWellFormed(Group));

	FGuLiWingmanCandidateBatch RetainedSequence;
	RetainedSequence.Group = Group;
	RetainedSequence.FlightIndex = Wingman.Flight.FlightIndex;
	FGuLiWingmanCandidateTrailSample& RetainedTrail =
		RetainedSequence.TrailSamples.AddDefaulted_GetRef();
	FGuLiWingmanCandidateSample& TrailMode = RetainedTrail.Samples.AddDefaulted_GetRef();
	TrailMode.Wingman = Wingman;
	TrailMode.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
	FGuLiWingmanCandidateSample& EndpointMode =
		RetainedSequence.Samples.AddDefaulted_GetRef();
	EndpointMode.Wingman = Wingman;
	EndpointMode.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
	Baseline.Samples[0].FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
	Baseline.RefreshHash();
	ConstrainPublishedFlightModesToAcceptedBaseline(RetainedSequence, Baseline);
	TestEqual(TEXT("The first retained frame is clamped from Accepted"),
		TrailMode.FlightMode, static_cast<uint8>(EGuLiWingmanFlightMode::CatchUp));
	TestEqual(TEXT("The endpoint is then clamped from the retained frame, not Accepted again"),
		EndpointMode.FlightMode, static_cast<uint8>(EGuLiWingmanFlightMode::Recover));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiRelayClientSequenceBoundaryTest,
	"GuLiStrike.Wingman.Network.ClientUploadSequenceBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiRelayClientSequenceBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("The non-zero producer sequence advances across wrap without losing residual ticks"),
		AdvanceNonZeroSequence(MAX_uint32 - 1u, 3u), 2u);
	TestEqual(TEXT("One tick across the non-zero wrap is measured as one"),
		ElapsedNonZeroSequenceTicks(MAX_uint32, 1u), 1u);
	TestEqual(TEXT("Two ticks across the non-zero wrap are not over-counted"),
		ElapsedNonZeroSequenceTicks(MAX_uint32 - 1u, 1u), 2u);
	TestEqual(TEXT("The never-submitted sentinel becomes due from the producer origin"),
		ElapsedNonZeroSequenceTicks(0u, 6u), 6u);
	TestEqual(TEXT("A same-group Bootstrap cannot roll a retained producer tick backward"),
		SelectBootstrapClientSimulationTick(500u, 30u, 101u, true), 500u);
	TestEqual(TEXT("A same-group Bootstrap may advance to a newer effective tick"),
		SelectBootstrapClientSimulationTick(20u, 40u, 30u, true), 40u);
	TestEqual(TEXT("A fresh group does not inherit an unrelated producer tick"),
		SelectBootstrapClientSimulationTick(500u, 30u, 101u, false), 101u);
	TestTrue(TEXT("Initializing keeps the retained owner's private clock alive"),
		IsRetainedOwnerPrivateLifecycle(EGuLiWingmanGroupLifecycle::Initializing));
	TestTrue(TEXT("Stale keeps the retained owner's private clock alive"),
		IsRetainedOwnerPrivateLifecycle(EGuLiWingmanGroupLifecycle::Stale));
	TestTrue(TEXT("Unavailable keeps the retained owner's private clock alive"),
		IsRetainedOwnerPrivateLifecycle(EGuLiWingmanGroupLifecycle::Unavailable));
	TestFalse(TEXT("Active uses the public publish path"),
		IsRetainedOwnerPrivateLifecycle(EGuLiWingmanGroupLifecycle::Active));
	TestFalse(TEXT("Revoked cannot retain a private producer clock"),
		IsRetainedOwnerPrivateLifecycle(EGuLiWingmanGroupLifecycle::Revoked));

	FGuLiWingmanBootstrapBundle First;
	FGuLiWingmanBootstrapBundle Retry = First;
	TestTrue(TEXT("An unchanged retry contract matches"),
		BootstrapRetryContractMatches(First, Retry));
	++Retry.ValidationRevisions.TuningRevision;
	TestFalse(TEXT("A duplicate Cut cannot change validation revisions"),
		BootstrapRetryContractMatches(First, Retry));
	Retry = First;
	++Retry.UploadRateGrant.GrantRevision;
	TestFalse(TEXT("A duplicate Cut cannot change the upload grant"),
		BootstrapRetryContractMatches(First, Retry));
	Retry = First;
	Retry.bActiveRosterRefresh = !First.bActiveRosterRefresh;
	TestFalse(TEXT("A duplicate Cut cannot change transaction kind"),
		BootstrapRetryContractMatches(First, Retry));

	FGuLiWingmanGroupHandle Group;
	Group.ShipInstanceId = FGuid(1u, 2u, 3u, 4u);
	Group.ShipGeneration = 3u;
	Group.GroupGeneration = 5u;
	FGuLiWingmanSimulationAcceptance Rebase;
	Rebase.Group = Group;
	Rebase.FlightIndex = 2u;
	Rebase.LeaseEpoch = 7u;
	Rebase.RosterRevision = 11u;
	Rebase.FrameSequence = 13u;
	Rebase.Disposition = EGuLiWingmanSubmissionDisposition::Rejected;
	Rebase.RejectReason = EGuLiWingmanRejectReason::StaleAcceptedBaseline;
	Rebase.AcceptedSnapshotSequence = 41u;
	Rebase.AcceptedServerTimeSeconds = 1.0;
	Rebase.RebaseBaseline.MatchEpoch = 17u;
	Rebase.RebaseBaseline.GroupGeneration = Group.GroupGeneration;
	Rebase.RebaseBaseline.AcceptedSequence = Rebase.AcceptedSnapshotSequence;
	Rebase.RebaseBaseline.ClientSimTick = 101u;
	uint32 AcceptedHighWater = 40u;
	TestTrue(TEXT("A typed rejection advances the exact Flight Accepted high-water mark"),
		TryAdvanceAcceptedSequenceFromRebase(
			Rebase, Group, 7u, 11u, 17u, AcceptedHighWater));
	TestEqual(TEXT("The next Candidate rebases to the authority sequence"), AcceptedHighWater, 41u);
	TestFalse(TEXT("A duplicate rebase cannot report progress"),
		TryAdvanceAcceptedSequenceFromRebase(
			Rebase, Group, 7u, 11u, 17u, AcceptedHighWater));
	Rebase.AcceptedSnapshotSequence = 40u;
	Rebase.RebaseBaseline.AcceptedSequence = 40u;
	TestFalse(TEXT("An out-of-order rejection cannot roll the high-water mark backward"),
		TryAdvanceAcceptedSequenceFromRebase(
			Rebase, Group, 7u, 11u, 17u, AcceptedHighWater));
	TestEqual(TEXT("Out-of-order results preserve the authority high-water mark"),
		AcceptedHighWater, 41u);
	Rebase.AcceptedSnapshotSequence = 1u;
	Rebase.RebaseBaseline.AcceptedSequence = 1u;
	AcceptedHighWater = MAX_uint32;
	TestTrue(TEXT("Accepted rebase advances across the non-zero sequence wrap"),
		TryAdvanceAcceptedSequenceFromRebase(
			Rebase, Group, 7u, 11u, 17u, AcceptedHighWater));
	TestEqual(TEXT("Accepted rebase preserves the non-zero wrapped value"), AcceptedHighWater, 1u);
	AcceptedHighWater = 0u;
	TestFalse(TEXT("A delayed result from another Lease cannot mutate the current Flight"),
		TryAdvanceAcceptedSequenceFromRebase(
			Rebase, Group, 8u, 11u, 17u, AcceptedHighWater));
	TestEqual(TEXT("A mismatched Lease leaves the local sequence untouched"), AcceptedHighWater, 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiActiveRosterCutAcceptedBaselineRebaseTest,
	"GuLiStrike.Wingman.Network.ActiveRosterCutAcceptedBaselineRebase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiActiveRosterCutAcceptedBaselineRebaseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGuLiWingmanGroupHandle Group;
	Group.ShipInstanceId = FGuid(21u, 22u, 23u, 24u);
	Group.ShipGeneration = 2u;
	Group.GroupGeneration = 4u;

	FGuLiWingmanHandle Survivor;
	Survivor.Flight.Group = Group;
	Survivor.Flight.FlightIndex = 0u;
	Survivor.MemberIndex = 0u;
	Survivor.EntityGeneration = 1u;
	FGuLiWingmanHandle Replaced = Survivor;
	Replaced.MemberIndex = 1u;
	FGuLiWingmanHandle Replacement = Replaced;
	Replacement.EntityGeneration = 2u;

	FGuLiWingmanBootstrapBundle Cut;
	Cut.bActiveRosterRefresh = true;
	Cut.Commit.Group = Group;
	Cut.ConnectionGeneration = 7u;
	Cut.RosterRevision = 11u;
	Cut.AbilityConfig.AbilitySetRevision = 3u;
	Cut.AbilityConfig.FormationCommandRevision = 5u;
	Cut.AbilityConfig.FormationDefinitionChecksum = 13u;
	FGuLiWingmanRosterEntry& SurvivorEntry = Cut.Roster.AddDefaulted_GetRef();
	SurvivorEntry.Wingman = Survivor;
	FGuLiWingmanRosterEntry& ReplacementEntry = Cut.Roster.AddDefaulted_GetRef();
	ReplacementEntry.Wingman = Replacement;

	FGuLiWingmanAcceptedBatch& ReliableBaseline = Cut.AcceptedSnapshot.AddDefaulted_GetRef();
	ReliableBaseline.Group = Group;
	ReliableBaseline.StateRef.MatchEpoch = 17u;
	ReliableBaseline.StateRef.GroupGeneration = Group.GroupGeneration;
	ReliableBaseline.StateRef.AcceptedSequence = 41u;
	ReliableBaseline.StateRef.ClientSimTick = 100u;
	ReliableBaseline.CarrierSource.CanonicalEpoch = 2u;
	ReliableBaseline.CarrierSource.MoveRevision = 90u;
	ReliableBaseline.ConnectionGeneration = Cut.ConnectionGeneration;
	ReliableBaseline.RosterRevision = 10u;
	ReliableBaseline.FlightIndex = 0u;
	ReliableBaseline.FrameSequence = 90u;
	ReliableBaseline.CaptureEstimatedServerTimeSeconds = 10.0;
	ReliableBaseline.ValidationRevisions.NavSchemaRevision = 1u;
	ReliableBaseline.ValidationRevisions.NavDataChecksum = 2u;
	ReliableBaseline.ValidationRevisions.TuningRevision = 3u;
	ReliableBaseline.ValidationRevisions.ObstacleRevision = 4u;
	ReliableBaseline.AbilitySetRevision = Cut.AbilityConfig.AbilitySetRevision;
	ReliableBaseline.FormationCommandRevision = Cut.AbilityConfig.FormationCommandRevision;
	ReliableBaseline.FormationDefinitionChecksum = Cut.AbilityConfig.FormationDefinitionChecksum;
	ReliableBaseline.ServerAcceptedTimeSeconds = 10.1;
	FGuLiWingmanCandidateSample& SurvivorSample = ReliableBaseline.Samples.AddDefaulted_GetRef();
	SurvivorSample.Wingman = Survivor;
	SurvivorSample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
	FGuLiWingmanCandidateSample& ReplacedSample = ReliableBaseline.Samples.AddDefaulted_GetRef();
	ReplacedSample.Wingman = Replaced;
	ReplacedSample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
	ReliableBaseline.RefreshHash();
	if (!TestTrue(TEXT("The reliable pre-replacement Accepted snapshot is well formed"),
		ReliableBaseline.IsWellFormed()))
	{
		return false;
	}

	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> AcceptedSequences{};
	AcceptedSequences[0] = 40u; // The Unreliable Accepted result for sequence 41 was lost.
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> NextFrameSequences{};
	NextFrameSequences[0] = 80u;
	TStaticArray<FGuLiWingmanAcceptedBatch, GULI_WINGMAN_FLIGHT_COUNT> CurrentBaselines{};
	FActiveRosterCutAcceptedPlan Plan;
	if (!TestTrue(TEXT("The reliable Active Cut produces a generation-safe rebase plan"),
		BuildActiveRosterCutAcceptedPlan(
			Cut, 17u, AcceptedSequences, NextFrameSequences, CurrentBaselines, Plan)))
	{
		return false;
	}
	TestEqual(TEXT("The reliable Cut repairs the lost per-Flight Accepted high-water"),
		Plan.AcceptedSequences[0], 41u);
	TestEqual(TEXT("The next normal frame starts strictly after the reliable server baseline"),
		Plan.NextFrameSequences[0], 91u);
	TestEqual(TEXT("Only the still-current generation receives the old Accepted source"),
		Plan.SourceRefBatches.Num(), 1);
	if (!Plan.SourceRefBatches.IsEmpty())
	{
		TestEqual(TEXT("The filtered source batch contains only the survivor"),
			Plan.SourceRefBatches[0].Samples.Num(), 1);
		TestTrue(TEXT("The survivor keeps its exact source identity"),
			Plan.SourceRefBatches[0].FindSample(Survivor) != nullptr);
		TestTrue(TEXT("The replacement does not inherit the prior generation source"),
			Plan.SourceRefBatches[0].FindSample(Replacement) == nullptr
				&& Plan.SourceRefBatches[0].FindSample(Replaced) == nullptr);
	}

	Cut.AuthorityMap.AddDefaulted_GetRef().LeaseEpoch = 9u;
	TestEqual(TEXT("A locally applied same-Lease Active Cut owns the next Candidate roster revision"),
		SelectPublishedRosterRevision(Group, 9u, 10u, Cut), 11u);
	TestEqual(TEXT("A Cut from another Lease cannot override the replicated roster revision"),
		SelectPublishedRosterRevision(Group, 10u, 10u, Cut), 10u);
	FGuLiWingmanBootstrapBundle NonActiveCut = Cut;
	NonActiveCut.bActiveRosterRefresh = false;
	TestEqual(TEXT("A non-Active bootstrap cannot override normal Candidate roster revision"),
		SelectPublishedRosterRevision(Group, 9u, 10u, NonActiveCut), 10u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiRetainedResumeTrajectorySelectionTest,
	"GuLiStrike.Wingman.Network.RetainedResumeTrajectorySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiRetainedResumeTrajectorySelectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGuLiWingmanGroupHandle Group;
	Group.ShipInstanceId = FGuid(11u, 12u, 13u, 14u);
	Group.ShipGeneration = 2u;
	Group.GroupGeneration = 3u;
	FGuLiWingmanHandle Wingman;
	Wingman.Flight.Group = Group;
	Wingman.Flight.FlightIndex = 0u;
	Wingman.MemberIndex = 0u;
	Wingman.EntityGeneration = 1u;

	FGuLiWingmanAcceptedBatch Baseline;
	Baseline.Group = Group;
	Baseline.StateRef.MatchEpoch = 1u;
	Baseline.StateRef.GroupGeneration = Group.GroupGeneration;
	Baseline.StateRef.AcceptedSequence = 1u;
	Baseline.StateRef.ClientSimTick = 100u;
	Baseline.CarrierSource.CanonicalEpoch = 1u;
	Baseline.CarrierSource.MoveRevision = 1u;
	Baseline.ConnectionGeneration = 1u;
	Baseline.RosterRevision = 1u;
	Baseline.FlightIndex = 0u;
	Baseline.FrameSequence = 1u;
	Baseline.CaptureEstimatedServerTimeSeconds = 10.0;
	Baseline.AbilitySetRevision = 1u;
	Baseline.FormationCommandRevision = 1u;
	Baseline.FormationDefinitionChecksum = 1u;
	Baseline.ServerAcceptedTimeSeconds = 10.0;
	FGuLiWingmanCandidateSample& BaselineSample = Baseline.Samples.AddDefaulted_GetRef();
	BaselineSample.Wingman = Wingman;
	BaselineSample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
	Baseline.RefreshHash();
	if (!TestTrue(TEXT("The retained-history baseline is well formed"), Baseline.IsWellFormed()))
	{
		return false;
	}

	auto MakeCandidate = [&Group, &Wingman](const uint32 Tick, const double CaptureTime)
	{
		FGuLiWingmanCandidateBatch Candidate;
		Candidate.MatchEpoch = 1u;
		Candidate.ConnectionGeneration = 1u;
		Candidate.Group = Group;
		Candidate.LeaseEpoch = 1u;
		Candidate.RosterRevision = 1u;
		Candidate.FlightIndex = 0u;
		Candidate.RequiredMemberMask = 1u;
		Candidate.ObservedGrantRevision = 1u;
		Candidate.CandidateSequence = 1u;
		Candidate.FrameSequence = 1u;
		Candidate.BaseAcceptedSequence = 1u;
		Candidate.ClientSimTick = Tick;
		Candidate.CaptureEstimatedServerTimeSeconds = CaptureTime;
		Candidate.NavSchemaRevision = 1u;
		Candidate.NavDataChecksum = 1u;
		Candidate.TuningRevision = 1u;
		Candidate.ObstacleRevision = 1u;
		Candidate.CarrierSource.CanonicalEpoch = 1u;
		Candidate.CarrierSource.MoveRevision = 1u;
		Candidate.AbilitySetRevision = 1u;
		Candidate.FormationCommandRevision = 1u;
		Candidate.FormationDefinitionChecksum = 1u;
		FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
		Sample.Wingman = Wingman;
		Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
		return Candidate;
	};

	TArray<FGuLiWingmanCandidateTrailSample> History;
	for (uint32 Index = 0u; Index < 20u; ++Index)
	{
		const FGuLiWingmanCandidateBatch Candidate =
			MakeCandidate(106u + Index * 6u, 10.2 + static_cast<double>(Index) * 0.2);
		TestTrue(TEXT("A monotonic private endpoint enters retained history"),
			StoreRetainedTrajectoryEndpoint(History, Candidate));
	}
	TestEqual(TEXT("Local retained history keeps sixteen 5 Hz endpoints"),
		History.Num(), RetainedTrajectoryHistoryCapacityPerFlight);
	TestEqual(TEXT("The bounded history drops its oldest endpoint"),
		History[0].ClientSimTick, 130u);
	TestEqual(TEXT("The newest private endpoint remains retained"),
		History.Last().ClientSimTick, 220u);

	FGuLiWingmanCandidateBatch ResumeEndpoint = MakeCandidate(226u, 14.2);
	TestTrue(TEXT("Resume selects an eligible retained trail"),
		AppendSelectedRetainedTrajectory(ResumeEndpoint, Baseline, History));
	TestEqual(TEXT("The wire trail remains capped at four samples"),
		ResumeEndpoint.TrailSamples.Num(), static_cast<int32>(GULI_WINGMAN_MAX_TRAIL_SAMPLES));
	for (int32 Index = 1; Index < ResumeEndpoint.TrailSamples.Num(); ++Index)
	{
		TestTrue(TEXT("Selected trail ticks are strictly increasing"),
			ResumeEndpoint.TrailSamples[Index - 1].ClientSimTick
				< ResumeEndpoint.TrailSamples[Index].ClientSimTick);
		TestTrue(TEXT("Selected trail times are strictly increasing"),
			ResumeEndpoint.TrailSamples[Index - 1].CaptureEstimatedServerTimeSeconds
				< ResumeEndpoint.TrailSamples[Index].CaptureEstimatedServerTimeSeconds);
	}
	TestTrue(TEXT("Selection spans the retained interval instead of taking only the tail"),
		ResumeEndpoint.TrailSamples.Last().ClientSimTick
			- ResumeEndpoint.TrailSamples[0].ClientSimTick >= 30u);
	for (const FGuLiWingmanCandidateTrailSample& Trail : ResumeEndpoint.TrailSamples)
	{
		TestTrue(TEXT("Every selected trail leaves authority-side transport headroom"),
			Trail.CaptureEstimatedServerTimeSeconds
				>= ResumeEndpoint.CaptureEstimatedServerTimeSeconds
					- RetainedTrajectoryMaximumWireAgeSeconds);
	}
	ConstrainPublishedFlightModesToAcceptedBaseline(ResumeEndpoint, Baseline);
	TestEqual(TEXT("Accepted constrains the first retained mode step"),
		ResumeEndpoint.TrailSamples[0].Samples[0].FlightMode,
		static_cast<uint8>(EGuLiWingmanFlightMode::CatchUp));
	TestEqual(TEXT("Later retained frames may reach the requested live mode"),
		ResumeEndpoint.TrailSamples[1].Samples[0].FlightMode,
		static_cast<uint8>(EGuLiWingmanFlightMode::Recover));
	TestTrue(TEXT("The selected and constrained Resume candidate remains well formed"),
		ResumeEndpoint.IsWellFormed());

	TArray<FGuLiWingmanCandidateTrailSample> StaleHistory;
	FGuLiWingmanCandidateTrailSample StaleTrail = History.Last();
	StaleTrail.Samples[0].FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Stale);
	TestTrue(TEXT("Stale remains a structurally valid reserved trail mode"),
		StaleTrail.IsWellFormed(Group, Wingman.Flight.FlightIndex, 1u));
	StaleHistory.Add(StaleTrail);
	FGuLiWingmanCandidateBatch StaleResumeEndpoint = MakeCandidate(226u, 14.2);
	TestFalse(TEXT("Stale private history is not selected as Resume continuity proof"),
		AppendSelectedRetainedTrajectory(StaleResumeEndpoint, Baseline, StaleHistory));
	TestTrue(TEXT("Rejecting stale private history leaves the wire trail empty"),
		StaleResumeEndpoint.TrailSamples.IsEmpty());

	TArray<FGuLiWingmanCandidateTrailSample> TooOldHistory;
	for (const FGuLiWingmanCandidateTrailSample& Trail : History)
	{
		if (Trail.CaptureEstimatedServerTimeSeconds
			< ResumeEndpoint.CaptureEstimatedServerTimeSeconds
				- RetainedTrajectoryMaximumWireAgeSeconds)
		{
			TooOldHistory.Add(Trail);
		}
	}
	FGuLiWingmanCandidateBatch TooOldResumeEndpoint = MakeCandidate(226u, 14.2);
	TestFalse(TEXT("History outside the bounded wire-age window is not selected"),
		AppendSelectedRetainedTrajectory(TooOldResumeEndpoint, Baseline, TooOldHistory));
	TestTrue(TEXT("Rejecting over-age private history leaves the wire trail empty"),
		TooOldResumeEndpoint.TrailSamples.IsEmpty());

	FGuLiWingmanAcceptedBatch PruneBaseline = Baseline;
	PruneBaseline.StateRef.ClientSimTick = 190u;
	PruneBaseline.CaptureEstimatedServerTimeSeconds = 12.8;
	PruneRetainedTrajectoryAtAcceptedBaseline(History, PruneBaseline);
	TestEqual(TEXT("Accepted pruning retains only newer private endpoints"), History.Num(), 5);
	TestEqual(TEXT("The first retained endpoint follows the Accepted tick"),
		History[0].ClientSimTick, 196u);
	return true;
}
#endif

UGuLiWingmanRelayComponent::UGuLiWingmanRelayComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Keep the producer clock aligned with the client-only Pawn fixed step. Upload capture then
	// naturally lands on every sixth (5 Hz) or third (10 Hz) 30 Hz simulation tick.
	PrimaryComponentTick.TickInterval = 0.0f;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

UGuLiWingmanRelayComponent::~UGuLiWingmanRelayComponent() = default;

void UGuLiWingmanRelayComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		AcknowledgementBucket.Reset(GetAuthorityTimeSeconds(), 8.0, 4.0);
	}
}

void UGuLiWingmanRelayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyClientOwnedGroup();
	if (FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
	{
		Registry->ClearFireHooks(BoundServerGroup);
	}
	ServerFireIntentAccepted.Clear();
	ServerFireIntentValidator = FGuLiFireIntentServerValidator{};
	ServerRelay = nullptr;
	BoundServerGroup = FGuLiWingmanGroupHandle{};
	Super::EndPlay(EndPlayReason);
}

void UGuLiWingmanRelayComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner())
	{
		return;
	}

	if (GetOwner()->HasAuthority() && ServerRelay
		&& ServerRelay->GetLeaseState().OwnerPlayerGuid == GetOwningPlayerGuid())
	{
		const EGuLiWingmanGroupLifecycle PreviousLifecycle = ReplicatedState.Lease.Lifecycle;
		const uint32 PreviousRosterRevision = ReplicatedState.RosterRevision;
		const FGuLiCarrierSourceRef PreviousCarrierSource = ReplicatedState.LatestCarrierSource;
		const FGuLiCarrierSourceResolver Resolver = [this](const FGuLiCarrierSourceRef& Source,
			FGuLiRelayCarrierState& OutState)
		{
			return ResolveCarrierSource(Source, OutState);
		};
		ServerRelay->AdvancePacketDeadlines(GetAuthorityTimeSeconds(), Resolver);
		TArray<FGuLiWingmanSubmissionResult> DeferredResults;
		ServerRelay->DrainDeferredCandidateResults(DeferredResults);
		for (const FGuLiWingmanSubmissionResult& Result : DeferredResults)
		{
			HandleCandidateResult(Result);
		}
		TArray<FGuLiWingmanAtomicBatchAcceptance> DeferredAtomicResults;
		ServerRelay->DrainDeferredAtomicBatchResults(DeferredAtomicResults);
		for (const FGuLiWingmanAtomicBatchAcceptance& Result : DeferredAtomicResults)
		{
			ClientReceiveAtomicBatchResult(Result, TArray<FGuLiWingmanAcceptedBatch>{});
		}
		FGuLiCarrierSourceRef CurrentCarrierSource;
		GetLatestCarrierSource(CurrentCarrierSource);
		const bool bCarrierSourceChanged = PreviousCarrierSource.CanonicalEpoch != CurrentCarrierSource.CanonicalEpoch
			|| PreviousCarrierSource.MoveRevision != CurrentCarrierSource.MoveRevision;
		const bool bRosterRevisionChanged = PreviousRosterRevision != ServerRelay->GetRosterRevision();
		if (PreviousLifecycle != ServerRelay->GetLeaseState().Lifecycle
			|| bCarrierSourceChanged || bRosterRevisionChanged)
		{
			RefreshReplicatedState();
		}
		if (PreviousLifecycle != ServerRelay->GetLeaseState().Lifecycle
			&& (ServerRelay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Stale
				|| ServerRelay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable)
			&& !ServerRelay->IsTransferInProgress())
		{
			// Listen-host authority follows the same explicit Resume transaction; it
			// does not let an ordinary Flight Candidate reactivate the group.
			ServerBeginResume();
		}
		if (bRosterRevisionChanged
			&& (ServerRelay->IsActiveRosterCutPending()
				|| ServerRelay->IsTransferInProgress()
				|| ServerRelay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Initializing))
		{
			// Roster/Death mutation invalidates any partially assembled cut in the core.
			// Rebuild and reliably publish the replacement cut immediately; this is
			// group-level work and never writes a Wingman transform.
			BuildAndSendBootstrap();
		}
	}
	const APlayerController* LocalController = Cast<APlayerController>(GetOwner());
	if (LocalController && LocalController->IsLocalController()
		&& ReplicatedState.Lease.Group.IsValid()
		&& IsLocalLeaseOwner(ReplicatedState.Lease.Group)
		&& ReplicatedState.Lease.Lifecycle != EGuLiWingmanGroupLifecycle::Revoked)
	{
		if (ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Stale
			|| ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable)
		{
			// The first request normally starts at Stale/Unavailable. Keep a bounded
			// retry alive because a rejected/expired atomic Resume can leave the same
			// retained lease Unavailable without another RepNotify transition.
			RequestResume();
		}
	}

	TickOwnerClientSimulation(DeltaTime);
}

void UGuLiWingmanRelayComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGuLiWingmanRelayComponent, ReplicatedState, COND_OwnerOnly);
}

bool UGuLiWingmanRelayComponent::ServerInitializeGroup(
	const uint32 MatchEpoch,
	const FGuLiWingmanGroupHandle& Group,
	const FGuid& LeaseOwnerPlayerGuid,
	const FGuid& BackupPlayerGuid,
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
	FGuLiCandidateWorldValidator CandidateWorldValidator,
	const FGuLiWingmanRelayTuning& Tuning)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	if (!Registry)
	{
		return false;
	}
	const double NowSeconds = GetAuthorityTimeSeconds();
	FGuLiWingmanRelayServer* NewRelay = Registry->CreateGroup(
		MatchEpoch, Group, LeaseOwnerPlayerGuid, BackupPlayerGuid, AbilityConfig, NowSeconds, Tuning);
	if (!NewRelay)
	{
		NewRelay = Registry->FindGroup(Group);
		if (!NewRelay || NewRelay->GetMatchEpoch() != MatchEpoch
			|| NewRelay->GetLeaseState().OwnerPlayerGuid != LeaseOwnerPlayerGuid)
		{
			return false;
		}
	}
	ServerRelay = NewRelay;
	BoundServerGroup = Group;
	FGuLiWingmanRelayValidationRevisions Revisions;
	Revisions.NavSchemaRevision = 1u;
	Revisions.NavDataChecksum = AbilityConfig.FormationDefinitionChecksum;
	Revisions.TuningRevision = FMath::Max(1u, AbilityConfig.AbilitySetRevision);
	Revisions.ObstacleRevision = 1u;
	if (!ServerRelay->ConfigureStrictFlightContract(
		GetCurrentConnectionGeneration(), Revisions, NowSeconds))
	{
		return false;
	}
	if (const APlayerController* Controller = Cast<APlayerController>(GetOwner()))
	{
		if (const AGuLiBattlePlayerState* PlayerState =
			Controller->GetPlayerState<AGuLiBattlePlayerState>())
		{
			Registry->SetGroupOwnerCohort(Group, static_cast<uint8>(PlayerState->GetTeam()));
		}
	}
	// A listen host executes its Client RPC locally and may submit the initial atomic batch
	// synchronously from BuildAndSendBootstrap.  Install this mandatory gate first; publishing
	// the cut before the hook exists would make that valid first batch fail closed with no retry.
	if (!CandidateWorldValidator
		|| !Registry->SetCandidateWorldValidator(Group, MoveTemp(CandidateWorldValidator)))
	{
		return false;
	}
	InstallCarrierResolvers(Group);
	AcknowledgementBucket.Reset(NowSeconds, 8.0, 4.0);
	RefreshReplicatedState();
	return BuildAndSendBootstrap();
}

bool UGuLiWingmanRelayComponent::ServerAttachPersistentGroup(
	const FGuLiWingmanGroupHandle& Group)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Group.IsValid())
	{
		return false;
	}
	FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	FGuLiWingmanRelayServer* Relay = Registry ? Registry->FindGroup(Group) : nullptr;
	if (!Relay || Relay->GetLeaseState().OwnerPlayerGuid != GetOwningPlayerGuid()
		|| !Relay->IsTransferInProgress()
		|| (Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Initializing
			&& Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Unavailable
			&& Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Stale))
	{
		return false;
	}
	if (ServerRelay && ServerRelay != Relay
		&& ServerRelay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Revoked)
	{
		return false;
	}

	ServerRelay = Relay;
	BoundServerGroup = Group;
	if (Relay->GetConnectionGeneration() != GetCurrentConnectionGeneration())
	{
		if (!Relay->ConfigureStrictFlightContract(GetCurrentConnectionGeneration(),
			Relay->GetValidationRevisions(), GetAuthorityTimeSeconds()))
		{
			ServerRelay = nullptr;
			BoundServerGroup = FGuLiWingmanGroupHandle{};
			return false;
		}
	}
	AcknowledgementBucket.Reset(GetAuthorityTimeSeconds(), 8.0, 4.0);
	RefreshReplicatedState();
	return BuildAndSendBootstrap();
}

bool UGuLiWingmanRelayComponent::ServerDeliverLeaseOffer(
	const FGuLiWingmanGroupHandle& Group)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Group.IsValid())
	{
		return false;
	}
	const FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	const FGuLiWingmanRelayServer* Relay = Registry ? Registry->FindGroup(Group) : nullptr;
	const FGuLiWingmanPendingLeaseOffer* Offer = Relay ? &Relay->GetPendingLeaseOffer() : nullptr;
	if (!Offer || !Offer->IsWellFormed()
		|| Offer->ProposedOwnerPlayerGuid != GetOwningPlayerGuid())
	{
		return false;
	}
	ClientReceiveLeaseOffer(Group, Offer->OfferRevision,
		Offer->ReadyDeadlineSeconds, Offer->OverallDeadlineSeconds);
	return true;
}

void UGuLiWingmanRelayComponent::ServerDetachPersistentGroup(
	const FGuLiWingmanGroupHandle& Group)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Group != BoundServerGroup)
	{
		return;
	}
	if (FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
	{
		Registry->ClearFireHooks(Group);
	}
	ServerRelay = nullptr;
	BoundServerGroup = FGuLiWingmanGroupHandle{};
	ReplicatedState = FGuLiWingmanRelayReplicatedState{};
	++ReplicatedState.StateRevision;
	GetOwner()->ForceNetUpdate();
}

bool UGuLiWingmanRelayComponent::CanServerAttachPersistentGroup() const
{
	return GetOwner() && GetOwner()->HasAuthority()
		&& (!ServerRelay
			|| ServerRelay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Revoked);
}

bool UGuLiWingmanRelayComponent::ServerPublishAbilityConfig(
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ServerRelay
		|| !ServerRelay->PublishAbilityConfig(AbilityConfig, GetAuthorityTimeSeconds()))
	{
		return false;
	}
	RefreshReplicatedState();
	return BuildAndSendBootstrap();
}

bool UGuLiWingmanRelayComponent::ServerBeginTakeover(
	const FGuid& NewOwnerPlayerGuid, const FGuid& NewBackupPlayerGuid)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ServerRelay
		|| !ServerRelay->BeginTakeover(NewOwnerPlayerGuid, NewBackupPlayerGuid, GetAuthorityTimeSeconds()))
	{
		return false;
	}
	RefreshReplicatedState();
	return BuildAndSendBootstrap();
}

void UGuLiWingmanRelayComponent::PublishServerExternalControl(const TArray<FGuLiWingmanAcceptedBatch>& Baselines)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ServerRelay) { return; }
	ReplicatedState.ExternalDisplacementBaselines = Baselines;
	ReplicatedState.ExternalDisplacementEpoch = ServerRelay->GetExternalDisplacementRevision();
	RefreshReplicatedState();
	if (auto* GS = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
	{
		TArray<FGuLiWingmanAcceptedBatch> PublicBaselines = Baselines;
		if (ServerRelay->IsPhased())
		{
			for (uint8 Flight=0; Flight<GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
			{
				const auto& History = ServerRelay->GetAcceptedHistory();
				const int32 Index = History.FindLastByPredicate([Flight](const auto& B) { return B.FlightIndex == Flight; });
				if (Index != INDEX_NONE) { PublicBaselines.Add(History[Index]); }
			}
		}
		GS->ServerPublishWingmanExternalControl(ServerRelay->GetLeaseState().Group,
			ServerRelay->IsPhased(),ReplicatedState.bExternalActionsLocked,PublicBaselines);
	}
}

void UGuLiWingmanRelayComponent::ServerAcknowledgeExternalDisplacement_Implementation(const uint32 Epoch)
{
	if (ServerRelay && ServerRelay->AcknowledgeExternalDisplacement(GetOwningPlayerGuid(), Epoch, GetAuthorityTimeSeconds()))
	{
		PublishServerExternalControl(ReplicatedState.ExternalDisplacementBaselines);
	}
}

bool UGuLiWingmanRelayComponent::ServerBeginResume()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ServerRelay
		|| !ServerRelay->BeginResume(GetAuthorityTimeSeconds()))
	{
		return false;
	}
	RefreshReplicatedState();
	return BuildAndSendBootstrap();
}

bool UGuLiWingmanRelayComponent::ServerRefreshActiveRosterCut()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ServerRelay)
	{
		return false;
	}
	FGuLiWingmanBootstrapBundle Bootstrap;
	if (!ServerRelay->RefreshActiveRosterCut(GetAuthorityTimeSeconds(), Bootstrap))
	{
		return false;
	}
	RefreshReplicatedState();
	ClientReceiveBootstrap(Bootstrap);
	if (Bootstrap.UploadRateGrant.IsWellFormed())
	{
		ClientReceiveUploadRateGrant(Bootstrap.UploadRateGrant);
	}
	if (AGuLiBattleGameState* BattleGameState =
		GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
	{
		BattleGameState->ServerPublishWingmanBootstrap(
			Bootstrap, ServerRelay->GetLeaseState().Lifecycle);
	}
	return true;
}

bool UGuLiWingmanRelayComponent::ServerIssueHighRateUploadGrant(
	const uint32 EffectiveClientSimTick,
	const EGuLiWingmanUploadRateGrantReason Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ServerRelay)
	{
		return false;
	}
	FGuLiWingmanUploadRateGrant Grant;
	if (!ServerRelay->IssueHighRateGrant(
		EffectiveClientSimTick, Reason, GetAuthorityTimeSeconds(), Grant))
	{
		return false;
	}
	RefreshReplicatedState();
	ClientReceiveUploadRateGrant(Grant);
	return true;
}

void UGuLiWingmanRelayComponent::ServerRevokeGroup()
{
	if (GetOwner() && GetOwner()->HasAuthority() && ServerRelay)
	{
		const FGuLiWingmanGroupHandle RevokedGroup = ServerRelay->GetLeaseState().Group;
		if (FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
		{
			Registry->RevokeGroup(RevokedGroup, GetAuthorityTimeSeconds());
		}
		else
		{
			ServerRelay->Revoke(GetAuthorityTimeSeconds());
		}
		RefreshReplicatedState();
		if (AGuLiBattleGameState* BattleGameState =
			GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
		{
			BattleGameState->ServerRevokeWingmanGroup(RevokedGroup);
		}
	}
}

void UGuLiWingmanRelayComponent::SubmitCandidate(const FGuLiWingmanCandidateBatch& Candidate)
{
	if (Candidate.AttackFireRecords.IsEmpty()) ServerSubmitCandidate(Candidate);
	else ServerSubmitAttackCandidate(Candidate);
}

void UGuLiWingmanRelayComponent::SubmitFireIntent(const FGuLiWingmanFireIntent& Intent)
{
	ServerSubmitFireIntent(Intent);
}

void UGuLiWingmanRelayComponent::RequestResume()
{
	if (!ReplicatedState.Lease.Group.IsValid()
		|| !IsLocalLeaseOwner(ReplicatedState.Lease.Group)
		|| (ReplicatedState.Lease.Lifecycle != EGuLiWingmanGroupLifecycle::Stale
			&& ReplicatedState.Lease.Lifecycle != EGuLiWingmanGroupLifecycle::Unavailable))
	{
		return;
	}
	const double LocalNowSeconds = GetWorld()
		? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	if (LastRequestedResumeLeaseEpoch == ReplicatedState.Lease.LeaseEpoch
		&& LocalNowSeconds + UE_DOUBLE_SMALL_NUMBER < NextClientResumeRequestTimeSeconds)
	{
		return;
	}
	LastRequestedResumeLeaseEpoch = ReplicatedState.Lease.LeaseEpoch;
	NextClientResumeRequestTimeSeconds = LocalNowSeconds + 1.0;
	ServerRequestResume(ReplicatedState.Lease.Group, ReplicatedState.Lease.LeaseEpoch);
}

bool UGuLiWingmanRelayComponent::SetServerCandidateWorldValidator(
	FGuLiCandidateWorldValidator InValidator)
{
	if (FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
	{
		return Registry->SetCandidateWorldValidator(BoundServerGroup, MoveTemp(InValidator));
	}
	return false;
}

void UGuLiWingmanRelayComponent::SetServerFireIntentValidator(
	FGuLiFireIntentServerValidator InValidator)
{
	ServerFireIntentValidator = MoveTemp(InValidator);
	if (FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
	{
		Registry->SetFireIntentValidator(BoundServerGroup, MoveTemp(ServerFireIntentValidator));
	}
}

FGuLiServerFireIntentAcceptedSignature&
UGuLiWingmanRelayComponent::OnServerFireIntentAccepted()
{
	if (FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
	{
		if (FGuLiServerFireIntentAcceptedSignature* Delegate =
			Registry->FindFireIntentAcceptedDelegate(BoundServerGroup))
		{
			return *Delegate;
		}
	}
	return ServerFireIntentAccepted;
}

void UGuLiWingmanRelayComponent::ServerSubmitCandidate_Implementation(
	const FGuLiWingmanCandidateBatch& Candidate)
{
	if (!ServerRelay)
	{
		FGuLiWingmanSimulationAcceptance Acceptance;
		Acceptance.Group = Candidate.Group.IsValid() ? Candidate.Group : ReplicatedState.Lease.Group;
		Acceptance.FlightIndex = Candidate.FlightIndex;
		Acceptance.LeaseEpoch = Candidate.LeaseEpoch != 0u
			? Candidate.LeaseEpoch : ReplicatedState.Lease.LeaseEpoch;
		Acceptance.RosterRevision = Candidate.RosterRevision;
		Acceptance.FrameSequence = Candidate.FrameSequence;
		Acceptance.Disposition = EGuLiWingmanSubmissionDisposition::Rejected;
		Acceptance.RejectReason = EGuLiWingmanRejectReason::InactiveGroup;
		FGuLiWingmanCandidateResultWire WireResult;
		WireResult.CandidateSequence = Candidate.CandidateSequence;
		WireResult.Acceptance = Acceptance;
		ClientReceiveCandidateResult(WireResult);
		return;
	}
	const FGuLiCarrierSourceResolver Resolver = [this](const FGuLiCarrierSourceRef& Source,
		FGuLiRelayCarrierState& OutState)
	{
		return ResolveCarrierSource(Source, OutState);
	};
	const FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	const FGuLiCandidateWorldValidator* WorldValidator = Registry
		? Registry->FindCandidateWorldValidator(BoundServerGroup) : nullptr;
	HandleCandidateResult(ServerRelay->SubmitCandidate(
		GetOwningPlayerGuid(), Candidate, GetAuthorityTimeSeconds(), Resolver,
		WorldValidator ? *WorldValidator : FGuLiCandidateWorldValidator{}));
}

void UGuLiWingmanRelayComponent::ServerSubmitPoseFrame_Implementation(
	const TArray<FGuLiWingmanCandidateBatch>& Candidates)
{
	if (Candidates.Num() > GULI_WINGMAN_FLIGHT_COUNT) return;
	for (const FGuLiWingmanCandidateBatch& Candidate : Candidates)
		ServerSubmitCandidate_Implementation(Candidate);
}

void UGuLiWingmanRelayComponent::ServerSubmitAttackCandidate_Implementation(const FGuLiWingmanCandidateBatch& Candidate)
{
	if (!ServerRelay || Candidate.Group != BoundServerGroup || Candidate.AttackFireRecords.IsEmpty()
		|| !Candidate.IsWellFormed()) return;
	const double Now = GetAuthorityTimeSeconds();
	for (auto It = ReliableAttackCandidateSequences.CreateIterator(); It; ++It)
		if (Now - It.Value() > 2.0) It.RemoveCurrent();
	if (ReliableAttackCandidateSequences.Num() >= 64) return;
	ReliableAttackCandidateSequences.Add(Candidate.CandidateSequence, Now);
	// Reliable burst starts still use the same lease, identity and attack authority checks.
	ServerSubmitCandidate_Implementation(Candidate);
}

void UGuLiWingmanRelayComponent::ServerRequestEmergencyRebase_Implementation(
	const FGuLiWingmanEmergencyRebaseRequest& Request)
{
	FGuLiWingmanEmergencyRebaseResponse Response;
	Response.Wingman = Request.Wingman;
	Response.LeaseEpoch = Request.LeaseEpoch;
	Response.RequestSequence = Request.RequestSequence;
	Response.Result = EGuLiWingmanEmergencyRebaseResult::WrongLease;
	Response.RetryAfterServerTimeSeconds = FMath::Max(0.0, GetAuthorityTimeSeconds());
	FGuLiWingmanAcceptedBatch AcceptedBatch;
	if (ServerRelay)
	{
		const FGuLiCarrierSourceResolver Resolver = [this](
			const FGuLiCarrierSourceRef& Source, FGuLiRelayCarrierState& OutState)
		{
			return ResolveCarrierSource(Source, OutState);
		};
		const FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
		const FGuLiCandidateWorldValidator* WorldValidator = Registry
			? Registry->FindCandidateWorldValidator(BoundServerGroup) : nullptr;
		Response = ServerRelay->SubmitEmergencyRebase(
			GetOwningPlayerGuid(), Request, GetAuthorityTimeSeconds(), Resolver,
			WorldValidator ? *WorldValidator : FGuLiCandidateWorldValidator{}, AcceptedBatch);
	}
	const bool bAccepted = Response.Result == EGuLiWingmanEmergencyRebaseResult::Accepted
		&& AcceptedBatch.IsWellFormed();
	if (bAccepted)
	{
		RefreshReplicatedState();
		if (AGuLiBattleGameState* BattleGameState =
			GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
		{
			BattleGameState->ServerPublishWingmanAcceptedBatch(AcceptedBatch);
		}
	}
	ClientReceiveEmergencyRebase(Response, AcceptedBatch, bAccepted);
}

void UGuLiWingmanRelayComponent::ClientReceiveAttackCandidateResult_Implementation(const FGuLiWingmanCandidateResultWire& Result)
{
	ClientReceiveCandidateResult_Implementation(Result);
}

void UGuLiWingmanRelayComponent::ClientReceiveEmergencyRebase_Implementation(
	const FGuLiWingmanEmergencyRebaseResponse& Response,
	const FGuLiWingmanAcceptedBatch& AcceptedBatch,
	const bool bHasAcceptedBatch)
{
	if (!Response.IsWellFormed() || !GetWorld()
		|| Response.Wingman.Flight.Group != ReplicatedState.Lease.Group
		|| Response.LeaseEpoch != ReplicatedState.Lease.LeaseEpoch)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	const bool bListenSmoke = FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke"));
	if (bListenSmoke)
	{
		++ListenSmokeEmergencyRebaseResultCount;
	}
#endif
	UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!Simulation)
	{
		return;
	}
	if (Response.Result == EGuLiWingmanEmergencyRebaseResult::Accepted)
	{
		if (!bHasAcceptedBatch || !AcceptedBatch.IsWellFormed()
			|| AcceptedBatch.StateRef.AcceptedSequence != Response.AcceptedSequence
			|| AcceptedBatch.FlightIndex != Response.Wingman.Flight.FlightIndex
			|| (AcceptedBatch.RebasedMemberMask
				& static_cast<uint8>(1u << Response.Wingman.MemberIndex)) == 0u)
		{
			return;
		}
		ObserveAuthoritativeServerTime(AcceptedBatch.ServerAcceptedTimeSeconds);
		RetainedTrajectoryByFlight[AcceptedBatch.FlightIndex].Reset();
		LastRetainedTrajectoryTickByFlight[AcceptedBatch.FlightIndex] = 0u;
		const bool bApplied = ApplyAcceptedBatchToLocalOwner(AcceptedBatch);
#if !UE_BUILD_SHIPPING
		if (bListenSmoke)
		{
			++ListenSmokeEmergencyRebaseAcceptedCount;
			ListenSmokeEmergencyRebaseAppliedCount += bApplied ? 1u : 0u;
		}
#endif
		return;
	}
	Simulation->ApplyEmergencyRebaseResponse(Response, nullptr);
}

void UGuLiWingmanRelayComponent::ServerSubmitAtomicCandidateFragment_Implementation(
	const FGuLiWingmanAtomicCandidateBatchFragment& Fragment)
{
	FGuLiWingmanAtomicBatchAcceptance Result;
	TArray<FGuLiWingmanAcceptedBatch> AcceptedFlights;
	if (!ServerRelay)
	{
		Result.BatchId = Fragment.Header.BatchId;
		Result.BatchKind = Fragment.Header.BatchKind;
		Result.Group = Fragment.Header.Group;
		Result.LeaseEpoch = Fragment.Header.LeaseEpoch;
		Result.FrozenRosterRevision = Fragment.Header.FrozenRosterRevision;
		Result.BaselineRevision = Fragment.Header.BaselineRevision;
		Result.BaselineHash = Fragment.Header.BaselineHash;
		Result.RejectReason = EGuLiWingmanRejectReason::InactiveGroup;
		ClientReceiveAtomicBatchResult(Result, AcceptedFlights);
		return;
	}
	const FGuLiCarrierSourceResolver Resolver = [this](const FGuLiCarrierSourceRef& Source,
		FGuLiRelayCarrierState& OutState)
	{
		return ResolveCarrierSource(Source, OutState);
	};
	const FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	const FGuLiCandidateWorldValidator* WorldValidator = Registry
		? Registry->FindCandidateWorldValidator(BoundServerGroup) : nullptr;
	Result = ServerRelay->SubmitAtomicCandidateFragment(
		GetOwningPlayerGuid(), Fragment, GetAuthorityTimeSeconds(), Resolver,
		WorldValidator ? *WorldValidator : FGuLiCandidateWorldValidator{});
	if (Result.Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
	{
		for (const FGuLiWingmanAcceptedBatch& Accepted : ServerRelay->GetAcceptedHistory())
		{
			if (Accepted.ConnectionGeneration == Fragment.Header.ConnectionGeneration
				&& Accepted.RosterRevision == Fragment.Header.FrozenRosterRevision
				&& Result.AcceptedSnapshotSequences.IsValidIndex(Accepted.FlightIndex)
				&& Accepted.StateRef.AcceptedSequence
					== Result.AcceptedSnapshotSequences[Accepted.FlightIndex]
				&& (Fragment.Header.IncludedFlightMask & (1u << Accepted.FlightIndex)) != 0u)
			{
				AcceptedFlights.Add(Accepted);
			}
		}
		AcceptedFlights.Sort([](const FGuLiWingmanAcceptedBatch& Lhs,
			const FGuLiWingmanAcceptedBatch& Rhs)
		{
			return Lhs.FlightIndex < Rhs.FlightIndex;
		});
		RefreshReplicatedState();
		if (AGuLiBattleGameState* BattleGameState =
			GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
		{
			// The accepted all-Flight batch is the activation point. Publish the exact
			// frozen six-scope cut again with its now-Active lifecycle, followed by the
			// verbatim accepted Flights, so existing observers and late joiners never
			// retain an Initializing/old-owner projection after activation or takeover.
			FGuLiWingmanBootstrapBundle ActiveBootstrap;
			if (ServerRelay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active
				&& ServerRelay->BuildBootstrap(ActiveBootstrap))
			{
				BattleGameState->ServerPublishWingmanBootstrap(
					ActiveBootstrap, EGuLiWingmanGroupLifecycle::Active);
			}
			BattleGameState->ServerPublishWingmanAcceptedAtomicBatch(AcceptedFlights);
		}
	}
	ClientReceiveAtomicBatchResult(Result, AcceptedFlights);
}

void UGuLiWingmanRelayComponent::ServerSubmitFireIntent_Implementation(
	const FGuLiWingmanFireIntent& Intent)
{
	FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	const FGuLiFireIntentServerValidator* Validator = Registry
		? Registry->FindFireIntentValidator(BoundServerGroup) : nullptr;
	// A retained projection authorizes simulation, not active combat by itself. If its original
	// server-side Ship validator is gone, a backup may take over movement but cannot forge a weapon request.
	const FGuLiWingmanSubmissionResult Result = ServerRelay && Validator
		? ServerRelay->SubmitFireIntent(GetOwningPlayerGuid(), Intent, GetAuthorityTimeSeconds(), *Validator)
		: FGuLiWingmanSubmissionResult::Rejected(EGuLiWingmanRejectReason::InactiveGroup,
			Intent.DomainFireSequence);
	if (Result.Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
	{
		if (FGuLiServerFireIntentAcceptedSignature* Accepted = Registry
			? Registry->FindFireIntentAcceptedDelegate(BoundServerGroup) : nullptr)
		{
			Accepted->Broadcast(Intent);
		}
	}
	ClientReceiveFireIntentResult(Result.Sequence, Result.Disposition, Result.RejectReason);
}

void UGuLiWingmanRelayComponent::ServerAcknowledgeAbilityConfig_Implementation(
	const FGuLiGroupAbilityConfigAck& Ack)
{
	const double NowSeconds = GetAuthorityTimeSeconds();
	if (!ServerRelay || !AcknowledgementBucket.Consume(NowSeconds)
		|| !ServerRelay->AcknowledgeAbilityConfig(GetOwningPlayerGuid(), Ack, NowSeconds))
	{
		return;
	}
	RefreshReplicatedState();
}

void UGuLiWingmanRelayComponent::ServerAcknowledgeBootstrap_Implementation(
	const FGuLiWingmanBootstrapCommit& Commit)
{
	ProcessServerBootstrapAcknowledgement(Commit, nullptr);
}

void UGuLiWingmanRelayComponent::ServerAcknowledgeTransferBootstrap_Implementation(
	const FGuLiWingmanBootstrapCommit& Commit,
	const FGuLiWingmanTransferBaseline& TransferBaseline)
{
	ProcessServerBootstrapAcknowledgement(Commit, &TransferBaseline);
}

void UGuLiWingmanRelayComponent::ProcessServerBootstrapAcknowledgement(
	const FGuLiWingmanBootstrapCommit& Commit,
	const FGuLiWingmanTransferBaseline* TransferBaseline)
{
	const double NowSeconds = GetAuthorityTimeSeconds();
	if (!ServerRelay || !AcknowledgementBucket.Consume(NowSeconds))
	{
		return;
	}
	if (ServerRelay->AcknowledgeBootstrap(GetOwningPlayerGuid(), Commit, TransferBaseline, NowSeconds))
	{
		RefreshReplicatedState();
	}
}

void UGuLiWingmanRelayComponent::ServerAcknowledgeLeaseOfferReady_Implementation(
	const FGuLiWingmanGroupHandle& Group, const uint32 OfferRevision)
{
	FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	FGuLiWingmanOwnerLossAssignment Assignment;
	if (!Registry || !Registry->AcknowledgeLeaseOfferReady(
		Group, GetOwningPlayerGuid(), OfferRevision, GetAuthorityTimeSeconds(), Assignment))
	{
		return;
	}
	if (!ServerAttachPersistentGroup(Group))
	{
		UE_LOG(LogTemp, Error,
			TEXT("Wingman Relay committed lease offer but transport attach failed: Group=%s Owner=%s"),
			*Group.ShipInstanceId.ToString(), *GetOwningPlayerGuid().ToString());
	}
}

void UGuLiWingmanRelayComponent::ServerRequestResume_Implementation(
	const FGuLiWingmanGroupHandle& Group, const uint32 LeaseEpoch)
{
	if (!ServerRelay || Group != BoundServerGroup || Group != ServerRelay->GetLeaseState().Group
		|| LeaseEpoch == 0u || LeaseEpoch != ServerRelay->GetLeaseState().LeaseEpoch
		|| ServerRelay->GetLeaseState().OwnerPlayerGuid != GetOwningPlayerGuid())
	{
		return;
	}
	ServerBeginResume();
}

void UGuLiWingmanRelayComponent::ClientReceiveBootstrap_Implementation(
	const FGuLiWingmanBootstrapBundle& Bootstrap)
{
	if (!Bootstrap.IsWellFormed())
	{
		return;
	}
	const uint32 PreviousLeaseEpoch = GetBootstrapLeaseEpoch(LastClientBootstrap);
	const uint32 IncomingLeaseEpoch = GetBootstrapLeaseEpoch(Bootstrap);
	const bool bSameGroup = LastClientBootstrap.Commit.Group.IsValid()
		&& LastClientBootstrap.Commit.Group == Bootstrap.Commit.Group;
	const bool bSameLeaseContext = bSameGroup && PreviousLeaseEpoch != 0u
		&& PreviousLeaseEpoch == IncomingLeaseEpoch;
	const bool bDuplicateCutId = bSameGroup
		&& LastClientBootstrap.Commit.CutId == Bootstrap.Commit.CutId;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("GuLiBootstrapTrace")))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[GULI_BOOTSTRAP_TRACE] receive cut=%llu previous=%llu same_group=%d duplicate=%d active_refresh=%d requires_atomic=%d"),
			Bootstrap.Commit.CutId,
			LastClientBootstrap.Commit.CutId,
			bSameGroup ? 1 : 0,
			bDuplicateCutId ? 1 : 0,
			Bootstrap.bActiveRosterRefresh ? 1 : 0,
			Bootstrap.bRequiresAtomicCandidateBatch ? 1 : 0);
	}
#endif
	if (bDuplicateCutId && !BootstrapRetryContractMatches(LastClientBootstrap, Bootstrap))
	{
		// A Cut id is immutable, including fields outside the six hashed scopes. Never let
		// a conflicting duplicate mutate upload cadence or the locally applied transaction.
		return;
	}
	if (Bootstrap.bActiveRosterRefresh)
	{
		const uint32 LeaseEpoch = Bootstrap.AuthorityMap.IsEmpty()
			? 0u : Bootstrap.AuthorityMap[0].LeaseEpoch;
		const double LocalNowSeconds = GetWorld()
			? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
		if (bDuplicateCutId)
		{
			// The server keeps the exact reliable Cut outstanding until it accepts the
			// acknowledgement. Retry at the acknowledgement bucket's 4 Hz refill rate,
			// but never re-apply or rebroadcast an immutable Cut every render frame.
			if (LocalNowSeconds + UE_DOUBLE_SMALL_NUMBER
				>= NextClientActiveRosterAckRetryTimeSeconds)
			{
				ServerAcknowledgeBootstrap(Bootstrap.Commit);
				NextClientActiveRosterAckRetryTimeSeconds = LocalNowSeconds + 0.25;
			}
			return;
		}
		APlayerController* Controller = Cast<APlayerController>(GetOwner());
		UGuLiWingmanSimulationSubsystem* Simulation = GetWorld()
			? GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>() : nullptr;
		if (!Controller || !Controller->IsLocalController() || LeaseEpoch == 0u
			|| !Simulation || !Simulation->ApplyRosterCut(Bootstrap.Commit.Group, Bootstrap.Roster))
		{
			return;
		}
		FActiveRosterCutAcceptedPlan AcceptedPlan;
		if (!BuildActiveRosterCutAcceptedPlan(
			Bootstrap,
			ReplicatedState.MatchEpoch,
			ClientAcceptedSequenceByFlight,
			NextClientFrameSequenceByFlight,
			LastClientAcceptedByFlight,
			AcceptedPlan))
		{
			return;
		}
		for (const FGuLiWingmanAcceptedBatch& SourceRefBatch : AcceptedPlan.SourceRefBatches)
		{
			if (!Simulation->ApplyAcceptedBatch(SourceRefBatch))
			{
				return;
			}
		}
		ClientAcceptedSequenceByFlight = AcceptedPlan.AcceptedSequences;
		NextClientFrameSequenceByFlight = AcceptedPlan.NextFrameSequences;
		LastClientAcceptedByFlight = AcceptedPlan.SafeBaselines;
		LastClientAcceptedBatch = AcceptedPlan.LatestSafeBaseline;
		LastAppliedAcceptedState = AcceptedPlan.LatestSafeBaseline.IsWellFormed()
			? AcceptedPlan.LatestSafeBaseline.StateRef : FGuLiAcceptedStateRef{};
		for (const FGuLiWingmanAcceptedBatch& Baseline : Bootstrap.AcceptedSnapshot)
		{
			if (Baseline.IsWellFormed() && Baseline.FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
			{
				PruneRetainedTrajectoryAtAcceptedBaseline(
					RetainedTrajectoryByFlight[Baseline.FlightIndex], Baseline);
			}
		}
		LastClientBootstrap = Bootstrap;
		OnBootstrapReceived.Broadcast(LastClientBootstrap);
		if (AGuLiWingmanPresentationActor* Presentation = GetWorld()
			? AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld()) : nullptr)
		{
			Presentation->ApplyBootstrap(Bootstrap, true, GetWorld()->GetTimeSeconds());
		}
		ApplyClientUploadRateGrant(Bootstrap.UploadRateGrant);
		// Active roster/health/death refreshes do not change AbilityConfig and the
		// group could not be Active unless that immutable snapshot was already
		// acknowledged. Spending the shared token on a redundant Ability ACK can
		// starve the required six-scope Bootstrap ACK indefinitely.
		ServerAcknowledgeBootstrap(Bootstrap.Commit);
		NextClientActiveRosterAckRetryTimeSeconds = LocalNowSeconds + 0.25;
		LastAcknowledgedGroup = Bootstrap.Commit.Group;
		LastAcknowledgedLeaseEpoch = LeaseEpoch;
		LastAcknowledgedCutId = Bootstrap.Commit.CutId;
		return;
	}
	if (!bSameGroup && LastClientBootstrap.Commit.Group.IsValid())
	{
		// Cut ids are group-local. Remove a retained predecessor before accepting another
		// group on the same controller component, and do not inherit its acknowledgement.
		DestroyClientOwnedGroup();
	}
	const double PreservedAtomicRetryTimeSeconds = NextClientAtomicBaselineRetryTimeSeconds;
	LastClientBootstrap = Bootstrap;
	OnBootstrapReceived.Broadcast(LastClientBootstrap);
	if (AGuLiWingmanPresentationActor* Presentation = GetWorld()
		? AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld()) : nullptr)
	{
		const bool bLocallyOwned = IsLocalLeaseOwner(Bootstrap.Commit.Group);
		Presentation->ApplyBootstrap(Bootstrap, bLocallyOwned, GetWorld()->GetTimeSeconds());
	}
	const bool bAlreadyAcknowledged = LastAcknowledgedGroup == Bootstrap.Commit.Group
		&& LastAcknowledgedLeaseEpoch == IncomingLeaseEpoch
		&& LastAcknowledgedCutId == Bootstrap.Commit.CutId;
	bClientBootstrapPending = !bAlreadyAcknowledged;
	if (!bDuplicateCutId)
	{
		if (bSameGroup && Bootstrap.bRequiresAtomicCandidateBatch && GetWorld())
		{
			// The reliable cut can overtake the replicated inactive lease. Cancel the
			// old attack before any synchronous atomic retry consumes its pose tick.
			if (auto* Simulation = GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>())
				Simulation->TickAttackRuns(Bootstrap.Commit.Group, Bootstrap.AttackState,
					IncomingLeaseEpoch, ClientSimulationTick, GetEstimatedServerTimeSeconds(), false);
		}
		bClientBootstrapLocallyApplied = false;
		bClientAtomicBaselineSubmitted = false;
		bClientAtomicBaselineAccepted = !Bootstrap.bRequiresAtomicCandidateBatch;
		bClientAtomicBootstrapAcksSubmitted = false;
		// Resume can publish a replacement Cut for the same retained Lease while an
		// atomic rejection is cooling down.  That reliable Bootstrap must not turn the
		// 0.25 s backoff into a per-replication-tick retry loop.
		NextClientAtomicBaselineRetryTimeSeconds = bSameLeaseContext
			? PreservedAtomicRetryTimeSeconds : 0.0;
	}
	TryCommitClientBootstrap();
}

void UGuLiWingmanRelayComponent::ClientReceiveLeaseOffer_Implementation(
	const FGuLiWingmanGroupHandle& Group,
	const uint32 OfferRevision,
	const double ReadyDeadlineSeconds,
	const double OverallDeadlineSeconds)
{
	const double EstimatedServerNow = GetEstimatedServerTimeSeconds();
	if (!Group.IsValid() || OfferRevision == 0u
		|| !FMath::IsFinite(ReadyDeadlineSeconds) || !FMath::IsFinite(OverallDeadlineSeconds)
		|| EstimatedServerNow >= ReadyDeadlineSeconds || EstimatedServerNow >= OverallDeadlineSeconds)
	{
		return;
	}
	ServerAcknowledgeLeaseOfferReady(Group, OfferRevision);
}

void UGuLiWingmanRelayComponent::ClientReceiveCandidateResult_Implementation(
	const FGuLiWingmanCandidateResultWire& Result)
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke")))
	{
		++ListenSmokeNormalResultCount;
		ListenSmokeLastNormalResultDisposition = Result.Acceptance.Disposition;
		ListenSmokeLastNormalResultRejectReason = Result.Acceptance.RejectReason;
		if (Result.Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
		{
			++ListenSmokeNormalAcceptedCount;
		}
		else if (Result.Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Rejected)
		{
			++ListenSmokeNormalRejectedCount;
			if (Result.Acceptance.FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
			{
				++ListenSmokeNormalRejectedCountByFlight[Result.Acceptance.FlightIndex];
				ListenSmokeLastNormalRejectReasonByFlight[Result.Acceptance.FlightIndex] =
					Result.Acceptance.RejectReason;
			}
		}
	}
#endif
	ConsumeValidatedCandidateResultWire(Result, true);
}

void UGuLiWingmanRelayComponent::ClientReceivePublicPoseFrame_Implementation(
	const TArray<FGuLiWingmanAcceptedBatch>& AcceptedBatches)
{
	UWorld* World = GetWorld();
	AGuLiBattleGameState* BattleGameState = World
		? World->GetGameState<AGuLiBattleGameState>() : nullptr;
	if (!BattleGameState)
	{
		// A client RPC can arrive before GameState; the next 10 Hz frame repeats the latest poses.
		return;
	}
	BattleGameState->ReceivePublicWingmanAcceptedBatches(AcceptedBatches);
}

void UGuLiWingmanRelayComponent::ClientReceiveAtomicBatchResult_Implementation(
	const FGuLiWingmanAtomicBatchAcceptance& Acceptance,
	const TArray<FGuLiWingmanAcceptedBatch>& AcceptedFlights)
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke")))
	{
		++ListenSmokeAtomicResultCount;
		ListenSmokeLastAtomicResultDisposition = Acceptance.Disposition;
		ListenSmokeLastAtomicResultRejectReason = Acceptance.RejectReason;
		if (Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
		{
			++ListenSmokeAtomicAcceptedCount;
		}
		else if (Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Rejected)
		{
			++ListenSmokeAtomicRejectedCount;
		}
	}
#endif
	OnAtomicBatchResult.Broadcast(Acceptance);
	if (Acceptance.Disposition != EGuLiWingmanSubmissionDisposition::Accepted)
	{
		bClientAtomicBaselineSubmitted = false;
		NextClientAtomicBaselineRetryTimeSeconds = GetWorld()
			? static_cast<double>(GetWorld()->GetTimeSeconds()) + 0.25 : 0.0;
		return;
	}
	// The reliable atomic result is an authoritative lifecycle observation. A remote
	// owner must not wait for a later owner-only property replication before starting
	// ordinary per-Flight uploads: the one-second freshness gate can otherwise make a
	// successfully activated group Stale before its first normal Candidate is sent.
	if (Acceptance.AvailabilityAfter == EGuLiWingmanGroupLifecycle::Active
		&& Acceptance.Group == ReplicatedState.Lease.Group
		&& Acceptance.LeaseEpoch == ReplicatedState.Lease.LeaseEpoch)
	{
		ReplicatedState.Lease.Lifecycle = EGuLiWingmanGroupLifecycle::Active;
	}
	bClientAtomicBaselineAccepted = true;
	NextClientAtomicBaselineRetryTimeSeconds = 0.0;
	for (const FGuLiWingmanAcceptedBatch& Accepted : AcceptedFlights)
	{
		ApplyAcceptedBatchToLocalOwner(Accepted);
	}
	TryCommitClientBootstrap();
}

void UGuLiWingmanRelayComponent::ClientReceiveUploadRateGrant_Implementation(
	const FGuLiWingmanUploadRateGrant& Grant)
{
	ApplyClientUploadRateGrant(Grant);
}

void UGuLiWingmanRelayComponent::ClientReceiveFireIntentResult_Implementation(
	const uint32 Sequence,
	const EGuLiWingmanSubmissionDisposition Disposition,
	const EGuLiWingmanRejectReason RejectReason)
{
	OnFireIntentResult.Broadcast(static_cast<int64>(Sequence), Disposition, RejectReason);
}

void UGuLiWingmanRelayComponent::OnRep_RelayState()
{
	ObserveAuthoritativeServerTime(ReplicatedState.Lease.LifecycleChangedTimeSeconds);
	TryCommitClientBootstrap();
	if (ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Active)
	{
		LastRequestedResumeLeaseEpoch = 0u;
		NextClientResumeRequestTimeSeconds = 0.0;
	}
	else if ((ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Stale
			|| ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable)
		&& IsLocalLeaseOwner(ReplicatedState.Lease.Group)
		&& LastRequestedResumeLeaseEpoch != ReplicatedState.Lease.LeaseEpoch)
	{
		RequestResume();
	}
	if (ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked
		|| (!ReplicatedState.Lease.Group.IsValid()
			&& LastClientBootstrap.Commit.Group.IsValid()))
	{
		// A valid Unavailable lease retains its private simulation for Resume. An empty
		// detached replicated state has no resumable lease and must take the normal cleanup
		// path, just like Revoked and EndPlay.
		DestroyClientOwnedGroup();
	}
}

FGuid UGuLiWingmanRelayComponent::GetOwningPlayerGuid() const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const AGuLiBattlePlayerState* PlayerState = Controller
		? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	return PlayerState ? PlayerState->GetPlayerGuid() : FGuid{};
}

bool UGuLiWingmanRelayComponent::IsLocalLeaseOwner(
	const FGuLiWingmanGroupHandle& Group) const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Group.IsValid())
	{
		return false;
	}
	const FGuid PlayerGuid = GetOwningPlayerGuid();
	if (PlayerGuid.IsValid() && ReplicatedState.Lease.Group == Group
		&& ReplicatedState.Lease.OwnerPlayerGuid == PlayerGuid)
	{
		return true;
	}
	if (LastClientBootstrap.Commit.Group != Group
		|| LastClientBootstrap.AuthorityMap.IsEmpty())
	{
		return false;
	}
	const FGuLiWingmanAuthorityEntry& BootstrapAuthority =
		LastClientBootstrap.AuthorityMap[0];
	if (!BootstrapAuthority.LeaseOwnerPlayerGuid.IsValid()
		|| BootstrapAuthority.LeaseEpoch == 0u)
	{
		return false;
	}
	// ClientReceiveBootstrap is a targeted Client RPC on this controller. If a newer
	// replicated lease is already present, it must still match the targeted Cut so a
	// former owner cannot keep producing after transfer.
	return !ReplicatedState.Lease.Group.IsValid()
		|| (ReplicatedState.Lease.Group == Group
			&& ReplicatedState.Lease.OwnerPlayerGuid
				== BootstrapAuthority.LeaseOwnerPlayerGuid
			&& ReplicatedState.Lease.LeaseEpoch == BootstrapAuthority.LeaseEpoch);
}

double UGuLiWingmanRelayComponent::GetAuthorityTimeSeconds() const
{
	return GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
}

EGuLiRelayCarrierLookupResult UGuLiWingmanRelayComponent::ResolveCarrierSource(
	const FGuLiCarrierSourceRef& Source, FGuLiRelayCarrierState& OutState) const
{
	if (const FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
	{
		return Registry->ResolveCarrierSource(BoundServerGroup, Source, OutState);
	}
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return ResolveCarrierFromMovement(
		TWeakObjectPtr<UGuLiShipMovementComponent>(
			Pawn ? Pawn->FindComponentByClass<UGuLiShipMovementComponent>() : nullptr),
		Source,
		OutState);
}

bool UGuLiWingmanRelayComponent::GetLatestCarrierSource(FGuLiCarrierSourceRef& OutSource) const
{
	if (const FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry())
	{
		return Registry->GetLatestCarrierSource(BoundServerGroup, OutSource);
	}
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return GetLatestCarrierFromMovement(
		TWeakObjectPtr<UGuLiShipMovementComponent>(
			Pawn ? Pawn->FindComponentByClass<UGuLiShipMovementComponent>() : nullptr),
		OutSource);
}

FGuLiWingmanRelayAuthorityRegistry* UGuLiWingmanRelayComponent::GetAuthorityRegistry() const
{
	AGuLiBattleGameState* BattleGameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	return BattleGameState ? BattleGameState->GetWingmanRelayAuthorityRegistry() : nullptr;
}

void UGuLiWingmanRelayComponent::SendPublicWingmanAcceptedBatches(
	const TArray<FGuLiWingmanAcceptedBatch>& AcceptedBatches)
{
	TArray<FGuLiWingmanAcceptedBatch> PublicPoseFrame;
	const FGuid Viewer = GetOwningPlayerGuid();
	for (const FGuLiWingmanAcceptedBatch& Batch : AcceptedBatches)
	{
		if (ServerRelay && Batch.Group == ServerRelay->GetLeaseState().Group
			&& Viewer == ServerRelay->GetLeaseState().OwnerPlayerGuid) continue;
		PublicPoseFrame.Add(Batch);
	}
	// One compact frame at 10 Hz, ordered with lifecycle cuts. UE must not discard
	// the current pose when a busy frame temporarily saturates the connection.
	ClientReceivePublicPoseFrame(PublicPoseFrame);
}

void UGuLiWingmanRelayComponent::InstallCarrierResolvers(
	const FGuLiWingmanGroupHandle& Group)
{
	FGuLiWingmanRelayAuthorityRegistry* Registry = GetAuthorityRegistry();
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UGuLiShipMovementComponent* Movement = Pawn
		? Pawn->FindComponentByClass<UGuLiShipMovementComponent>() : nullptr;
	if (!Registry || !Movement)
	{
		return;
	}
	const TWeakObjectPtr<UGuLiShipMovementComponent> WeakMovement(Movement);
	Registry->SetCarrierResolvers(
		Group,
		[WeakMovement](const FGuLiCarrierSourceRef& Source, FGuLiRelayCarrierState& OutState)
		{
			return ResolveCarrierFromMovement(WeakMovement, Source, OutState);
		},
		[WeakMovement](FGuLiCarrierSourceRef& OutSource)
		{
			return GetLatestCarrierFromMovement(WeakMovement, OutSource);
		});
}

void UGuLiWingmanRelayComponent::RefreshReplicatedState()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ServerRelay)
	{
		return;
	}
	ReplicatedState.Lease = ServerRelay->GetLeaseState();
	ReplicatedState.bPhased = ServerRelay->IsPhased();
	ReplicatedState.bExternalActionsLocked = ServerRelay->IsExternallyControlled();
	ReplicatedState.AbilityConfig = ServerRelay->GetAbilityConfig();
	ReplicatedState.AttackState = ServerRelay->AttackState;
	ReplicatedState.MatchEpoch = ServerRelay->GetMatchEpoch();
	ReplicatedState.ConnectionGeneration = ServerRelay->GetConnectionGeneration();
	ReplicatedState.RosterRevision = ServerRelay->GetRosterRevision();
	ReplicatedState.ValidationRevisions = ServerRelay->GetValidationRevisions();
	ReplicatedState.UploadRateGrant = ServerRelay->GetUploadRateGrant();
	GetLatestCarrierSource(ReplicatedState.LatestCarrierSource);
	ReplicatedState.LastAcceptedCandidateSequence = ServerRelay->GetLastAcceptedCandidateSequence();
	++ReplicatedState.StateRevision;
	if (ReplicatedState.StateRevision == 0u)
	{
		++ReplicatedState.StateRevision;
	}
	GetOwner()->ForceNetUpdate();
}

bool UGuLiWingmanRelayComponent::TryCommitClientBootstrap()
{
	if (!bClientBootstrapPending || !LastClientBootstrap.IsWellFormed() || !GetWorld())
	{
		return false;
	}
	if (ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked
		|| (ReplicatedState.Lease.Group.IsValid()
			&& ReplicatedState.Lease.Group != LastClientBootstrap.Commit.Group))
	{
		// A reliable Bootstrap may overtake its owner-only replicated state, so an empty
		// state is allowed to wait for OnRep. A known conflicting or revoked lease is a
		// hard local creation gate and cannot resurrect the retained/terminated group.
		return false;
	}
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	const uint32 LeaseEpoch = LastClientBootstrap.AuthorityMap.IsEmpty()
		? 0u : LastClientBootstrap.AuthorityMap[0].LeaseEpoch;
	if (!Controller || !Controller->IsLocalController() || !Pawn || !Simulation
		|| LeaseEpoch == 0u || !ReplicatedState.LatestCarrierSource.IsValid())
	{
		return false;
	}

	if (!bClientBootstrapLocallyApplied)
	{
		const bool bRetainingSameGroup =
			Simulation->HasOwnedGroup(LastClientBootstrap.Commit.Group);
		const bool bRetainingTransferTrajectory = bRetainingSameGroup
			&& LastClientBootstrap.AtomicBatchKind != EGuLiWingmanAtomicBatchKind::Bootstrap;
		const uint32 PreviousClientSimulationTick = ClientSimulationTick;
		const FVector CarrierVelocity = Pawn->GetVelocity();
		if (!Simulation->CreateOrResetOwnedGroup(
			LastClientBootstrap.Commit.Group,
			LastClientBootstrap.AbilityConfig,
			Pawn->GetActorTransform(),
			CarrierVelocity,
			ReplicatedState.LatestCarrierSource,
			Pawn))
		{
			return false;
		}
		LastAppliedAcceptedState = FGuLiAcceptedStateRef{};
		LastClientAcceptedBatch = FGuLiWingmanAcceptedBatch{};
		for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
		{
			LastClientAcceptedByFlight[FlightIndex] = FGuLiWingmanAcceptedBatch{};
			ClientAcceptedSequenceByFlight[FlightIndex] = 0u;
			NextClientFrameSequenceByFlight[FlightIndex] = 1u;
			LastSubmittedClientTickByFlight[FlightIndex] = 0u;
			if (!bRetainingTransferTrajectory)
			{
				RetainedTrajectoryByFlight[FlightIndex].Reset();
				LastRetainedTrajectoryTickByFlight[FlightIndex] = 0u;
			}
		}
		NextBasicTargetScanSeconds.Reset();
		uint32 LatestBaselineClientTick = 0u;
		for (const FGuLiWingmanAcceptedBatch& Baseline : LastClientBootstrap.AcceptedSnapshot)
		{
			const bool bAppliedBaseline = ApplyAcceptedBatchToLocalOwner(Baseline);
			if (bAppliedBaseline && Baseline.UsesStrictFlightContract())
			{
				// Resume/Takeover preserves the server's per-Flight frame high-water mark.
				// Rebase the next producer frame from the frozen Accepted scope instead of
				// restarting at one, which the authority must reject as stale.
				NextClientFrameSequenceByFlight[Baseline.FlightIndex] =
					Baseline.FrameSequence == MAX_uint32 ? 1u : Baseline.FrameSequence + 1u;
			}
			LatestBaselineClientTick = FMath::Max(
				LatestBaselineClientTick, Baseline.StateRef.ClientSimTick);
		}
		ApplyClientUploadRateGrant(LastClientBootstrap.UploadRateGrant);
		const uint32 RebasedClientTick = LatestBaselineClientTick == MAX_uint32
			? 1u : LatestBaselineClientTick + 1u;
		// Retained poses already have a physical integration timestamp. Advancing it
		// here labels the same pose as a new step and corrupts the recovery trail.
		ClientSimulationTick = bRetainingSameGroup ? PreviousClientSimulationTick : SelectBootstrapClientSimulationTick(
			PreviousClientSimulationTick,
			LastClientBootstrap.AbilityConfig.EffectiveClientSimTick,
			RebasedClientTick,
			bRetainingSameGroup);
		NextClientCandidateSequence = ReplicatedState.LastAcceptedCandidateSequence == MAX_uint32
			? 1u : ReplicatedState.LastAcceptedCandidateSequence + 1u;
		NextClientEmergencyRebaseSequence = 1u;
		bClientBootstrapLocallyApplied = true;
	}
	// Keep the Pawn carrier reference aligned with the newest replicated canonical move
	// before every atomic retry;
	// otherwise a delayed remote Bootstrap can remain pinned to a revision that has already
	// fallen out of the server's bounded movement history and can never become Active.
	if (!Simulation->UpdateOwnedGroupCarrier(
		LastClientBootstrap.Commit.Group,
		Pawn->GetActorTransform(),
		Pawn->GetVelocity(),
		ReplicatedState.LatestCarrierSource,
		Pawn))
	{
		return false;
	}
	const bool bAtomicBootstrapAckFirst =
		LastClientBootstrap.bRequiresAtomicCandidateBatch;
	auto SubmitExactAcks = [this, LeaseEpoch]()
	{
		FGuLiGroupAbilityConfigAck AbilityAck;
		AbilityAck.Group = LastClientBootstrap.Commit.Group;
		AbilityAck.LeaseEpoch = LeaseEpoch;
		AbilityAck.SnapshotRevision = LastClientBootstrap.AbilityConfig.SnapshotRevision;
		AbilityAck.SnapshotHash = LastClientBootstrap.AbilityConfig.SnapshotHash;
		ServerAcknowledgeAbilityConfig(AbilityAck);
		if (LastClientBootstrap.bHasTransferBaseline)
		{
			ServerAcknowledgeTransferBootstrap(
				LastClientBootstrap.Commit, LastClientBootstrap.TransferBaseline);
		}
		else
		{
			ServerAcknowledgeBootstrap(LastClientBootstrap.Commit);
		}
	};
	if (bAtomicBootstrapAckFirst && !bClientAtomicBootstrapAcksSubmitted)
	{
		// Submit the exact AbilityConfig and six-scope ACK before the first atomic baseline
		// attempt. Reliable actor-channel ordering queues these fixed-deadline prerequisites
		// in the same outbound window and removes any dependency on an atomic-result round trip.
		// Authority still keeps the group Initializing until both ACKs and the complete
		// all-or-nothing candidate batch have committed.
		SubmitExactAcks();
		bClientAtomicBootstrapAcksSubmitted = true;
	}
	if (LastClientBootstrap.bRequiresAtomicCandidateBatch && !bClientAtomicBaselineAccepted)
	{
		const double LocalNowSeconds = static_cast<double>(GetWorld()->GetTimeSeconds());
		if (!bClientAtomicBaselineSubmitted
			&& LocalNowSeconds + UE_DOUBLE_SMALL_NUMBER >= NextClientAtomicBaselineRetryTimeSeconds)
		{
			// Listen-server RPCs can synchronously deliver a rejection result. Mark the
			// request in-flight first so that callback can authoritatively clear it;
			// assigning the call's return value afterwards would overwrite that clear.
			bClientAtomicBaselineSubmitted = true;
			if (!BuildAndSubmitAtomicClientBaseline())
			{
				bClientAtomicBaselineSubmitted = false;
			}
		}
		return false;
	}

	if (!bAtomicBootstrapAckFirst)
	{
		SubmitExactAcks();
	}
	LastAcknowledgedGroup = LastClientBootstrap.Commit.Group;
	LastAcknowledgedLeaseEpoch = LeaseEpoch;
	LastAcknowledgedCutId = LastClientBootstrap.Commit.CutId;
	bClientBootstrapPending = false;
	return true;
}

bool UGuLiWingmanRelayComponent::ApplyAcceptedBatchToLocalOwner(
	const FGuLiWingmanAcceptedBatch& AcceptedBatch)
{
	if (!AcceptedBatch.IsWellFormed() || !GetWorld())
	{
		return false;
	}
	if (AcceptedBatch.UsesStrictFlightContract())
	{
		const uint32 AcceptedHighWater =
			ClientAcceptedSequenceByFlight[AcceptedBatch.FlightIndex];
		if (AcceptedHighWater != 0u
			&& AcceptedBatch.StateRef.AcceptedSequence != AcceptedHighWater
			&& !IsStrictlyNewerNonZeroSequence(
				AcceptedBatch.StateRef.AcceptedSequence, AcceptedHighWater))
		{
			// A rejection/pending echo may already have advanced the logical baseline
			// after the corresponding Accepted DTO was lost. Never let a later,
			// out-of-order Accepted DTO roll that authority high-water mark backward.
			return false;
		}
		const FGuLiWingmanAcceptedBatch& Previous = LastClientAcceptedByFlight[AcceptedBatch.FlightIndex];
		if (Previous.IsWellFormed()
			&& !IsStrictlyNewerNonZeroSequence(
				AcceptedBatch.StateRef.AcceptedSequence, Previous.StateRef.AcceptedSequence))
		{
			return false;
		}
	}
	else if (LastAppliedAcceptedState.MatchEpoch == AcceptedBatch.StateRef.MatchEpoch
		&& LastAppliedAcceptedState.GroupGeneration == AcceptedBatch.StateRef.GroupGeneration
		&& LastAppliedAcceptedState.AcceptedSequence == AcceptedBatch.StateRef.AcceptedSequence
		&& LastAppliedAcceptedState.ClientSimTick == AcceptedBatch.StateRef.ClientSimTick)
	{
		return false;
	}
	UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!Simulation || !Simulation->ApplyAcceptedBatch(AcceptedBatch))
	{
		return false;
	}
	LastAppliedAcceptedState = AcceptedBatch.StateRef;
	LastClientAcceptedBatch = AcceptedBatch;
	if (AcceptedBatch.UsesStrictFlightContract())
	{
		LastClientAcceptedByFlight[AcceptedBatch.FlightIndex] = AcceptedBatch;
		ClientAcceptedSequenceByFlight[AcceptedBatch.FlightIndex] = AcceptedBatch.StateRef.AcceptedSequence;
		PruneRetainedTrajectoryAtAcceptedBaseline(
			RetainedTrajectoryByFlight[AcceptedBatch.FlightIndex], AcceptedBatch);
	}
	OnAcceptedBatch.Broadcast(AcceptedBatch);
	return true;
}

uint8 UGuLiWingmanRelayComponent::BuildClientRequiredMemberMask(const uint8 FlightIndex) const
{
	if (FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT)
	{
		return 0u;
	}
	uint8 Mask = 0u;
	for (const FGuLiWingmanRosterEntry& Entry : LastClientBootstrap.Roster)
	{
		if (Entry.bDead || Entry.Wingman.Flight.FlightIndex != FlightIndex)
		{
			continue;
		}
		const FGuLiWingmanHealthEntry* Health = LastClientBootstrap.Health.FindByPredicate(
			[&Entry](const FGuLiWingmanHealthEntry& Candidate)
			{
				return Candidate.Wingman == Entry.Wingman;
			});
		if (Health && Health->CurrentHealthPermille > 0u)
		{
			Mask |= static_cast<uint8>(1u << Entry.Wingman.MemberIndex);
		}
	}
	return Mask;
}

bool UGuLiWingmanRelayComponent::BuildAndSubmitAtomicClientBaseline()
{
	// A resumed owner must actually integrate beyond the frozen baseline before
	// publishing an atomic endpoint; a larger sequence number is not movement.
	if (ClientSimulationTick < LastClientBootstrap.AbilityConfig.EffectiveClientSimTick
		|| LastClientBootstrap.AcceptedSnapshot.ContainsByPredicate([this](const auto& Baseline)
		{
			return !IsStrictlyNewerNonZeroSequence(ClientSimulationTick, Baseline.StateRef.ClientSimTick);
		})) return false;
#if !UE_BUILD_SHIPPING
	const bool bListenSmokeDiagnostics = FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke"));
	if (bListenSmokeDiagnostics)
	{
		++ListenSmokeAtomicBuildAttemptCount;
	}
#endif
	if (!LastClientBootstrap.bRequiresAtomicCandidateBatch)
	{
		return true;
	}
	if (bListenSmokeDiagnostics)
	{
		ListenSmokeLastAtomicFlightModeMask = 0u;
	}
	if (!GetWorld() || LastClientBootstrap.ConnectionGeneration == 0u
		|| LastClientBootstrap.RosterRevision == 0u
		|| !LastClientBootstrap.ValidationRevisions.IsWellFormed()
		|| !ClientUploadRateGrant.IsWellFormed())
	{
#if !UE_BUILD_SHIPPING
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeAtomicBuildPreconditionFailureCount;
		}
#endif
		return false;
	}
	UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!Simulation)
	{
#if !UE_BUILD_SHIPPING
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeAtomicBuildMissingSimulationFailureCount;
		}
#endif
		return false;
	}

	FGuLiWingmanAtomicCandidateBatchFragment Fragment;
	const double CaptureTime = GetEstimatedServerTimeSeconds();
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		if ((LastClientBootstrap.RequiredFlightMask & (1u << FlightIndex)) == 0u)
		{
			continue;
		}
		FGuLiWingmanCandidateBatch Candidate;
		if (!Simulation->BuildFlightCandidate(
			LastClientBootstrap.Commit.Group,
			ReplicatedState.MatchEpoch,
			LastClientBootstrap.AuthorityMap[0].LeaseEpoch,
			LastClientBootstrap.ConnectionGeneration,
			LastClientBootstrap.RosterRevision,
			FlightIndex,
			BuildClientRequiredMemberMask(FlightIndex),
			EGuLiWingmanUploadRateClass::Cruise5Hz,
			ClientUploadRateGrant.GrantRevision,
			NextClientCandidateSequence,
			NextClientFrameSequenceByFlight[FlightIndex],
			ClientAcceptedSequenceByFlight[FlightIndex],
			ClientSimulationTick,
			CaptureTime,
			LastClientBootstrap.ValidationRevisions.NavSchemaRevision,
			LastClientBootstrap.ValidationRevisions.NavDataChecksum,
			LastClientBootstrap.ValidationRevisions.TuningRevision,
			LastClientBootstrap.ValidationRevisions.ObstacleRevision,
			Candidate))
		{
#if !UE_BUILD_SHIPPING
			if (bListenSmokeDiagnostics)
			{
				++ListenSmokeAtomicBuildFlightCandidateFailureCount;
				ListenSmokeAtomicBuildLastFailedFlightIndex = FlightIndex;
			}
#endif
			return false;
		}
		if (LastClientBootstrap.AtomicBatchKind == EGuLiWingmanAtomicBatchKind::Resume
			|| LastClientBootstrap.AtomicBatchKind == EGuLiWingmanAtomicBatchKind::Takeover)
		{
			AppendSelectedRetainedTrajectory(
				Candidate,
				LastClientAcceptedByFlight[FlightIndex],
				RetainedTrajectoryByFlight[FlightIndex]);
		}
		// Authority validates Accepted -> trails -> endpoint. Apply the same ordered
		// two-mode-step clamp to every retained frame, not just the endpoint.
		ConstrainPublishedFlightModesToAcceptedBaseline(
			Candidate, LastClientAcceptedByFlight[FlightIndex]);
		if (bListenSmokeDiagnostics)
		{
			for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
			{
				if (Sample.FlightMode < 8u)
				{
					ListenSmokeLastAtomicFlightModeMask |= static_cast<uint8>(1u << Sample.FlightMode);
				}
			}
		}
		Fragment.Flights.Add(MoveTemp(Candidate));
		LastSubmittedClientTickByFlight[FlightIndex] = ClientSimulationTick;
		NextClientFrameSequenceByFlight[FlightIndex] =
			NextClientFrameSequenceByFlight[FlightIndex] == MAX_uint32
			? 1u : NextClientFrameSequenceByFlight[FlightIndex] + 1u;
		NextClientCandidateSequence = NextClientCandidateSequence == MAX_uint32
			? 1u : NextClientCandidateSequence + 1u;
	}
	if (Fragment.Flights.IsEmpty())
	{
#if !UE_BUILD_SHIPPING
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeAtomicBuildEmptyFailureCount;
		}
#endif
		return false;
	}

	Fragment.Header.BatchId = NextClientAtomicBatchId++;
	if (NextClientAtomicBatchId == 0u) NextClientAtomicBatchId = 1u;
	Fragment.Header.BatchKind = LastClientBootstrap.AtomicBatchKind;
	Fragment.Header.Group = LastClientBootstrap.Commit.Group;
	Fragment.Header.ConnectionGeneration = LastClientBootstrap.ConnectionGeneration;
	Fragment.Header.LeaseEpoch = LastClientBootstrap.AuthorityMap[0].LeaseEpoch;
	Fragment.Header.FrozenRosterRevision = LastClientBootstrap.RosterRevision;
	Fragment.Header.FrozenRequiredFlightMask = LastClientBootstrap.RequiredFlightMask;
	Fragment.Header.FrozenRequiredMemberMaskHash = LastClientBootstrap.RequiredMemberMaskHash;
	Fragment.Header.BaselineRevision = LastClientBootstrap.AtomicBaselineRevision;
	Fragment.Header.BaselineHash = LastClientBootstrap.AtomicBaselineHash;
	Fragment.Header.IncludedFlightMask = LastClientBootstrap.RequiredFlightMask;
	Fragment.Header.ClientBatchStartTick = ClientSimulationTick;
	Fragment.Header.FragmentCount = 1u;
	Fragment.Header.BatchPayloadBytes = Fragment.EstimatePayloadBytes();
	Fragment.Header.BatchPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(Fragment.Flights);
	Fragment.Header.bAtomicCommit = true;
	Fragment.FragmentIndex = 0u;
	if (!Fragment.IsWellFormed()
		|| Fragment.Header.BatchPayloadBytes > GULI_WINGMAN_ATOMIC_BATCH_MAX_BYTES)
	{
#if !UE_BUILD_SHIPPING
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeAtomicBuildFragmentFailureCount;
		}
#endif
		return false;
	}
	ServerSubmitAtomicCandidateFragment(Fragment);
#if !UE_BUILD_SHIPPING
	if (bListenSmokeDiagnostics)
	{
		++ListenSmokeAtomicBuildSubmittedCount;
	}
#endif
	return true;
}

void UGuLiWingmanRelayComponent::ApplyClientUploadRateGrant(
	const FGuLiWingmanUploadRateGrant& Grant)
{
	const FGuLiWingmanGroupHandle ExpectedGroup = LastClientBootstrap.Commit.Group.IsValid()
		? LastClientBootstrap.Commit.Group : ReplicatedState.Lease.Group;
	const uint32 ExpectedConnectionGeneration = LastClientBootstrap.ConnectionGeneration != 0u
		? LastClientBootstrap.ConnectionGeneration : ReplicatedState.ConnectionGeneration;
	const uint32 ExpectedLeaseEpoch = !LastClientBootstrap.AuthorityMap.IsEmpty()
		? LastClientBootstrap.AuthorityMap[0].LeaseEpoch : ReplicatedState.Lease.LeaseEpoch;
	if (!Grant.IsWellFormed() || Grant.Group != ExpectedGroup
		|| Grant.ConnectionGeneration != ExpectedConnectionGeneration
		|| Grant.LeaseEpoch != ExpectedLeaseEpoch)
	{
		return;
	}
	if (ClientUploadRateGrant.IsWellFormed()
		&& static_cast<int32>(Grant.GrantRevision - ClientUploadRateGrant.GrantRevision) <= 0)
	{
		return;
	}
	ClientUploadRateGrant = Grant;
}

uint32 UGuLiWingmanRelayComponent::GetCurrentConnectionGeneration() const
{
	const AGuLiBattlePlayerController* Controller = Cast<AGuLiBattlePlayerController>(GetOwner());
	const UGuLiPlayerNetSyncComponent* NetSync = Controller
		? Controller->GetPlayerNetSyncComponent() : nullptr;
	return FMath::Max(1u, NetSync ? NetSync->GetConnectionGeneration() : 0u);
}

uint32 UGuLiWingmanRelayComponent::AdvanceClientSimulationClock(const float DeltaTime)
{
	const FGuLiWingmanGroupHandle Group=(bClientBootstrapPending || bClientBootstrapLocallyApplied)
		&& LastClientBootstrap.Commit.Group.IsValid() ? LastClientBootstrap.Commit.Group : ReplicatedState.Lease.Group;
	uint32 CompletedTick=0;
	if (const auto* Simulation=GetWorld() ? GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>() : nullptr;
		Simulation && Simulation->GetCompletedSimulationTick(Group,CompletedTick))
	{
		if(CaptureClockGroup != Group || LastCompletedPawnTick==0)
		{
			CaptureClockGroup=Group;
			LastCompletedPawnTick=CompletedTick;
			// Pawn clocks and the Relay producer clock share the same 30 Hz epoch.
			// The first observation may already follow one or more completed fixed
			// steps (Bootstrap is committed earlier in this component tick). Account
			// for those steps instead of silently anchoring past them; otherwise the
			// next Candidate labels two integrations as one protocol tick.
			if (IsStrictlyNewerNonZeroSequence(CompletedTick, ClientSimulationTick))
			{
				const uint32 Elapsed = ElapsedNonZeroSequenceTicks(
					ClientSimulationTick, CompletedTick);
				ClientSimulationTick = CompletedTick;
				return Elapsed;
			}
			return 0;
		}
		const uint32 Elapsed=ElapsedNonZeroSequenceTicks(LastCompletedPawnTick,CompletedTick);
		LastCompletedPawnTick=CompletedTick;
		if(Elapsed>0) ClientSimulationTick=AdvanceNonZeroSequence(ClientSimulationTick,Elapsed);
		return Elapsed;
	}
	// The owner Pawn integrator caps one frame to four 30 Hz steps. The protocol
	// clock must drop the same hitch remainder; otherwise a Candidate claims more
	// elapsed simulation time than its transforms actually integrated and the
	// authority correctly rejects the resulting constant-velocity envelope.
	constexpr double ClientSimulationHz = 30.0;
	constexpr double MaximumAccumulatedSimulationTicks = 4.0;
	ClientCandidateAccumulator = FMath::Min(
		ClientCandidateAccumulator
			+ static_cast<double>(FMath::Max(0.0f, DeltaTime)) * ClientSimulationHz,
		MaximumAccumulatedSimulationTicks);
	const uint32 ElapsedSimulationTicks = static_cast<uint32>(FMath::FloorToInt(
		FMath::Min(ClientCandidateAccumulator, static_cast<double>(MAX_int32))));
	ClientCandidateAccumulator -= static_cast<double>(ElapsedSimulationTicks);
	if (ElapsedSimulationTicks > 0u)
	{
		ClientSimulationTick = AdvanceNonZeroSequence(
			ClientSimulationTick, ElapsedSimulationTicks);
	}
	return ElapsedSimulationTicks;
}

void UGuLiWingmanRelayComponent::CaptureOwnerTrajectory(
	UGuLiWingmanSimulationSubsystem& Simulation,
	const uint32 CaptureIntervalTicks)
{
	const FGuLiWingmanGroupHandle Group = LastClientBootstrap.Commit.Group.IsValid()
		? LastClientBootstrap.Commit.Group : ReplicatedState.Lease.Group;
	const uint32 LeaseEpoch = GetBootstrapLeaseEpoch(LastClientBootstrap);
	if (CaptureIntervalTicks == 0u || !Group.IsValid() || !Simulation.HasOwnedGroup(Group)
		|| ReplicatedState.MatchEpoch == 0u || LeaseEpoch == 0u
		|| LastClientBootstrap.ConnectionGeneration == 0u
		|| LastClientBootstrap.RosterRevision == 0u
		|| !LastClientBootstrap.ValidationRevisions.IsWellFormed()
		|| !ClientUploadRateGrant.IsWellFormed())
	{
		return;
	}

	const double CaptureTime = GetEstimatedServerTimeSeconds();
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		if (ElapsedNonZeroSequenceTicks(
			LastRetainedTrajectoryTickByFlight[FlightIndex], ClientSimulationTick)
			< CaptureIntervalTicks)
		{
			continue;
		}
		// A failed capture must not turn this owner-private path into a 30 Hz retry loop.
		LastRetainedTrajectoryTickByFlight[FlightIndex] = ClientSimulationTick;
		const uint8 RequiredMemberMask = BuildClientRequiredMemberMask(FlightIndex);
		if (RequiredMemberMask == 0u)
		{
			continue;
		}

		FGuLiWingmanCandidateBatch Candidate;
		if (Simulation.BuildFlightCandidate(
			Group,
			ReplicatedState.MatchEpoch,
			LeaseEpoch,
			LastClientBootstrap.ConnectionGeneration,
			LastClientBootstrap.RosterRevision,
			FlightIndex,
			RequiredMemberMask,
			EGuLiWingmanUploadRateClass::Cruise5Hz,
			ClientUploadRateGrant.GrantRevision,
			NextClientCandidateSequence,
			NextClientFrameSequenceByFlight[FlightIndex],
			ClientAcceptedSequenceByFlight[FlightIndex],
			ClientSimulationTick,
			CaptureTime,
			LastClientBootstrap.ValidationRevisions.NavSchemaRevision,
			LastClientBootstrap.ValidationRevisions.NavDataChecksum,
			LastClientBootstrap.ValidationRevisions.TuningRevision,
			LastClientBootstrap.ValidationRevisions.ObstacleRevision,
			Candidate))
		{
			// This is a private continuity sample. Sequence numbers are intentionally not
			// consumed until an atomic Resume/Takeover endpoint is actually submitted.
			StoreRetainedTrajectoryEndpoint(
				RetainedTrajectoryByFlight[FlightIndex], Candidate);
		}
	}
}

void UGuLiWingmanRelayComponent::TickOwnerClientSimulation(const float DeltaTime)
{
	const bool bListenSmokeDiagnostics =
#if !UE_BUILD_SHIPPING
		FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke"));
#else
		false;
#endif
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !GetWorld())
	{
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeOwnerMissingRuntimeGateCount;
		}
		return;
	}
	UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	auto ResolveClientOwnedGroup = [this]()
	{
		return (bClientBootstrapPending || bClientBootstrapLocallyApplied)
			&& LastClientBootstrap.Commit.Group.IsValid()
			? LastClientBootstrap.Commit.Group : ReplicatedState.Lease.Group;
	};

	// Bootstrap/lease state is committed before the explicit Pawn simulation step.
	TryCommitClientBootstrap();

	const FGuLiWingmanGroupHandle Group = ResolveClientOwnedGroup();
	const bool bActive =
		ReplicatedState.Lease.Lifecycle == EGuLiWingmanGroupLifecycle::Active;
	const bool bPrivateRetainedLifecycle =
		IsRetainedOwnerPrivateLifecycle(ReplicatedState.Lease.Lifecycle);
	if ((!bActive && !bPrivateRetainedLifecycle) || !ReplicatedState.MatchEpoch
		|| !Group.IsValid() || !IsLocalLeaseOwner(Group))
	{
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeOwnerInactiveGateCount;
		}
		return;
	}
	APawn* Pawn = Controller->GetPawn();
	if (!Pawn || !Simulation || !Simulation->HasOwnedGroup(Group))
	{
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeOwnerMissingRuntimeGateCount;
		}
		return;
	}
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig = bActive
		? ReplicatedState.AbilityConfig : LastClientBootstrap.AbilityConfig;
	if (ReplicatedState.ExternalDisplacementEpoch != 0 && !ReplicatedState.ExternalDisplacementBaselines.IsEmpty()
		&& LastAppliedExternalDisplacementEpoch != ReplicatedState.ExternalDisplacementEpoch)
	{
		bool bAppliedAll = true;
		for (const auto& Baseline : ReplicatedState.ExternalDisplacementBaselines)
		{
			bAppliedAll &= Simulation->ApplyAcceptedBatch(Baseline);
		}
		if (!bAppliedAll) { return; }
		for (const auto& Baseline : ReplicatedState.ExternalDisplacementBaselines)
		{
			const uint8 Flight = Baseline.FlightIndex;
			LastClientAcceptedByFlight[Flight] = Baseline;
			ClientAcceptedSequenceByFlight[Flight] = Baseline.StateRef.AcceptedSequence;
			NextClientFrameSequenceByFlight[Flight] = FMath::Max(NextClientFrameSequenceByFlight[Flight], Baseline.FrameSequence + 1u);
			RetainedTrajectoryByFlight[Flight].Reset();
			LastRetainedTrajectoryTickByFlight[Flight] = 0;
		}
		LastAppliedExternalDisplacementEpoch = ReplicatedState.ExternalDisplacementEpoch;
		ServerAcknowledgeExternalDisplacement(LastAppliedExternalDisplacementEpoch);
	}
	Simulation->SetGroupExternalControlState(Group, ReplicatedState.bPhased, ReplicatedState.bExternalActionsLocked);
	if (ReplicatedState.bExternalActionsLocked) { return; }
	Simulation->ApplyCommittedAbilityConfig(Group, AbilityConfig);
	// A Ship prediction barrier starts a new canonical-move epoch and deliberately
	// discards the server's earlier move history. Never attach those now-unresolvable
	// carrier references to a later Wingman Candidate; start a fresh local trail at
	// the first source in the new epoch. This is the same discontinuity boundary used
	// by the Ship movement protocol, not a position correction for any Wingman.
	if (ReplicatedState.LatestCarrierSource.IsValid()
		&& (RetainedTrajectoryCarrierGroup != Group
			|| RetainedTrajectoryCarrierEpoch
				!= ReplicatedState.LatestCarrierSource.CanonicalEpoch))
	{
		for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
		{
			RetainedTrajectoryByFlight[FlightIndex].Reset();
			LastRetainedTrajectoryTickByFlight[FlightIndex] = 0u;
		}
		RetainedTrajectoryCarrierGroup = Group;
		RetainedTrajectoryCarrierEpoch =
			ReplicatedState.LatestCarrierSource.CanonicalEpoch;
	}
	if (!Simulation->UpdateOwnedGroupCarrier(
			Group,
			Pawn->GetActorTransform(),
			Pawn->GetVelocity(),
			ReplicatedState.LatestCarrierSource,
			Pawn))
	{
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeOwnerMissingRuntimeGateCount;
		}
		return;
	}
    Simulation->TickAttackRuns(Group, ReplicatedState.AttackState,
        ReplicatedState.Lease.LeaseEpoch, ClientSimulationTick, GetEstimatedServerTimeSeconds(), bActive && !bClientBootstrapPending);
	if (!Simulation->AdvanceOwnedGroup(Group, DeltaTime))
	{
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeOwnerMissingRuntimeGateCount;
		}
		return;
	}
	AdvanceClientSimulationClock(DeltaTime);
	// Normal flight recovery is fully local. The v13 emergency-rebase RPC remains
	// decodable for an in-flight compatibility packet, but the owner simulation
	// never waits for or automatically requests a server movement decision.
	if (!bActive)
	{
		CaptureOwnerTrajectory(*Simulation, PrivateTrajectoryCaptureIntervalTicks);
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeOwnerInactiveGateCount;
		}
		return;
	}
	if (bListenSmokeDiagnostics)
	{
		++ListenSmokeOwnerEligibleTickCount;
	}
	ApplyClientUploadRateGrant(ReplicatedState.UploadRateGrant);
	CaptureOwnerTrajectory(*Simulation, ActiveTrajectoryCaptureIntervalTicks);
	if (LastOwnerUploadSimulationTick == ClientSimulationTick) return;
	LastOwnerUploadSimulationTick = ClientSimulationTick;
	const double EstimatedServerTime = GetEstimatedServerTimeSeconds();
	constexpr auto RequestedRate = EGuLiWingmanUploadRateClass::HighRate10Hz;
	constexpr uint32 CaptureIntervalTicks = 3u;
	TArray<FGuLiWingmanCandidateBatch> PoseFrame;
	// Every flight publishes its current endpoint at 10 Hz, independent of combat
	// grants or a per-render-frame flight budget. Simulation remains fixed at 30 Hz.
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		const uint32 ElapsedTicks = ElapsedNonZeroSequenceTicks(
			LastSubmittedClientTickByFlight[FlightIndex], ClientSimulationTick);
		if (ElapsedTicks < CaptureIntervalTicks)
		{
			continue;
		}
		const uint8 RequiredMemberMask = BuildClientRequiredMemberMask(FlightIndex);
		if (RequiredMemberMask == 0u)
		{
			continue;
		}
		FGuLiWingmanCandidateBatch Candidate;
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeNormalBuildAttemptCount;
		}
		if (!Simulation->BuildFlightCandidate(
			ReplicatedState.Lease.Group,
			ReplicatedState.MatchEpoch,
			ReplicatedState.Lease.LeaseEpoch,
			ReplicatedState.ConnectionGeneration,
			SelectPublishedRosterRevision(
				ReplicatedState.Lease.Group,
				ReplicatedState.Lease.LeaseEpoch,
				ReplicatedState.RosterRevision,
				LastClientBootstrap),
			FlightIndex,
			RequiredMemberMask,
			RequestedRate,
			ClientUploadRateGrant.GrantRevision,
			NextClientCandidateSequence,
			NextClientFrameSequenceByFlight[FlightIndex],
			ClientAcceptedSequenceByFlight[FlightIndex],
			ClientSimulationTick,
			EstimatedServerTime,
			ReplicatedState.ValidationRevisions.NavSchemaRevision,
			ReplicatedState.ValidationRevisions.NavDataChecksum,
			ReplicatedState.ValidationRevisions.TuningRevision,
			ReplicatedState.ValidationRevisions.ObstacleRevision,
			Candidate))
		{
			if (bListenSmokeDiagnostics)
			{
				++ListenSmokeNormalBuildFailureCount;
				ListenSmokeNormalBuildLastFailedFlightIndex = FlightIndex;
			}
			continue;
		}
		// Motion is owner-authored. Only a shot-start record needs its exact capture
		// pose; ordinary 10 Hz endpoints no longer upload unused navigation trails.
		TArray<uint32> RequiredFireTicks;
		Simulation->GetPendingAttackCaptureTicks(Candidate.Group, FlightIndex, RequiredFireTicks);
		if (!RequiredFireTicks.IsEmpty())
		{
			AppendSelectedRetainedTrajectory(Candidate, LastClientAcceptedByFlight[FlightIndex],
				RetainedTrajectoryByFlight[FlightIndex], RequiredFireTicks);
		}
		Simulation->AppendAttackFireRecords(Candidate);
		if (ElapsedNonZeroSequenceTicks(
			LastRetainedTrajectoryTickByFlight[FlightIndex], ClientSimulationTick)
			>= ActiveTrajectoryCaptureIntervalTicks)
		{
			LastRetainedTrajectoryTickByFlight[FlightIndex] = ClientSimulationTick;
			StoreRetainedTrajectoryEndpoint(
				RetainedTrajectoryByFlight[FlightIndex], Candidate);
		}
		if (bListenSmokeDiagnostics)
		{
			ListenSmokeLastNormalFlightModeMask = 0u;
			for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
			{
				if (Sample.FlightMode < 8u)
				{
					ListenSmokeLastNormalFlightModeMask |= static_cast<uint8>(1u << Sample.FlightMode);
				}
			}
		}
		if (AGuLiWingmanPresentationActor* Presentation =
			AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld()))
		{
			Presentation->ApplyOwnerFrame(Candidate, GetWorld()->GetTimeSeconds());
		}
		if (Candidate.AttackFireRecords.IsEmpty()) PoseFrame.Add(Candidate);
		else SubmitCandidate(Candidate);
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeNormalSubmittedCount;
		}
		LastSubmittedClientTickByFlight[FlightIndex] = ClientSimulationTick;
		NextClientFrameSequenceByFlight[FlightIndex] =
			NextClientFrameSequenceByFlight[FlightIndex] == MAX_uint32
			? 1u : NextClientFrameSequenceByFlight[FlightIndex] + 1u;
		NextClientCandidateSequence = NextClientCandidateSequence == MAX_uint32
			? 1u : NextClientCandidateSequence + 1u;
	}

	if (!PoseFrame.IsEmpty()) ServerSubmitPoseFrame(PoseFrame);
	TickOwnerBasicWeapon(GetWorld()->GetTimeSeconds());
}

void UGuLiWingmanRelayComponent::DrainAndSubmitEmergencyRebaseRequests(
	UGuLiWingmanSimulationSubsystem& Simulation,
	const FGuLiWingmanGroupHandle& Group)
{
	TArray<FGuLiWingmanLocalRebaseRequest> LocalRequests;
	Simulation.DrainEmergencyRebaseRequests(Group, LocalRequests);
	for (const FGuLiWingmanLocalRebaseRequest& Local : LocalRequests)
	{
		const uint8 FlightIndex = Local.Wingman.Flight.FlightIndex;
		const uint32 RequestSequence = NextClientEmergencyRebaseSequence;
		NextClientEmergencyRebaseSequence = AdvanceNonZeroSequence(
			NextClientEmergencyRebaseSequence, 1u);
		if (FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
			|| ClientAcceptedSequenceByFlight[FlightIndex] == 0u)
		{
			FGuLiWingmanEmergencyRebaseResponse LocalRejection;
			LocalRejection.Wingman = Local.Wingman;
			LocalRejection.LeaseEpoch = ReplicatedState.Lease.LeaseEpoch;
			LocalRejection.RequestSequence = RequestSequence;
			LocalRejection.Result = EGuLiWingmanEmergencyRebaseResult::StaleBaseline;
			LocalRejection.RetryAfterServerTimeSeconds =
				GetEstimatedServerTimeSeconds() + 1.0;
			Simulation.ApplyEmergencyRebaseResponse(LocalRejection, nullptr);
			continue;
		}
		FGuLiWingmanEmergencyRebaseRequest Request;
		Request.MatchEpoch = ReplicatedState.MatchEpoch;
		Request.ConnectionGeneration = ReplicatedState.ConnectionGeneration;
		Request.Wingman = Local.Wingman;
		Request.LeaseEpoch = ReplicatedState.Lease.LeaseEpoch;
		Request.RosterRevision = ReplicatedState.RosterRevision;
		Request.RequestSequence = RequestSequence;
		Request.BaselineAcceptedSequence =
			ClientAcceptedSequenceByFlight[FlightIndex];
		Request.Reason = Local.Reason;
		if (Request.IsWellFormed())
		{
#if !UE_BUILD_SHIPPING
			if (FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke")))
			{
				++ListenSmokeEmergencyRebaseRequestCount;
			}
#endif
			ServerRequestEmergencyRebase(Request);
		}
		else
		{
			FGuLiWingmanEmergencyRebaseResponse LocalRejection;
			LocalRejection.Wingman = Local.Wingman;
			LocalRejection.LeaseEpoch = ReplicatedState.Lease.LeaseEpoch;
			LocalRejection.RequestSequence = RequestSequence;
			LocalRejection.Result = EGuLiWingmanEmergencyRebaseResult::InvalidRequest;
			LocalRejection.RetryAfterServerTimeSeconds =
				GetEstimatedServerTimeSeconds() + 1.0;
			Simulation.ApplyEmergencyRebaseResponse(LocalRejection, nullptr);
		}
	}
}

void UGuLiWingmanRelayComponent::TickOwnerBasicWeapon(const double NowSeconds)
{
	if (!GetWorld() || !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| !ReplicatedState.AbilityConfig.IsUsableByLeaseOwner())
	{
		return;
	}
	TArray<const FGuLiWingmanWeaponChannelConfig*> AutomaticChannels;
	for (const FGuLiWingmanWeaponChannelConfig& Channel :
		ReplicatedState.AbilityConfig.WeaponChannels)
	{
		if (Channel.bEnabled && Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic
            && Channel.Runtime.Attack.Pattern == EGuLiWingmanAttackPattern::Legacy)
		{
			AutomaticChannels.Add(&Channel);
		}
	}
	if (AutomaticChannels.IsEmpty())
	{
		return;
	}

	const double EstimatedServerNowSeconds = GetEstimatedServerTimeSeconds();
	if (!FMath::IsFinite(EstimatedServerNowSeconds))
	{
		return;
	}

	if (NextBasicTargetScanSeconds.Num() != GULI_WINGMAN_GROUP_SIZE
		&& !FGuLiWingmanTargetAcquisition::InitializeStaggeredSchedule(
			NowSeconds,
			GULI_WINGMAN_GROUP_SIZE,
			NextBasicTargetScanSeconds))
	{
		return;
	}
	TArray<int32> DueEmitterIndices;
	if (!FGuLiWingmanTargetAcquisition::ConsumeDueScans(
		NowSeconds,
		NextBasicTargetScanSeconds,
		DueEmitterIndices)
		|| DueEmitterIndices.IsEmpty())
	{
		return;
	}

	UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!Simulation)
	{
		return;
	}

	for (const int32 GroupMemberIndex : DueEmitterIndices)
	{
		const uint8 FlightIndex = static_cast<uint8>(
			GroupMemberIndex / GULI_WINGMAN_MEMBERS_PER_FLIGHT);
		const FGuLiWingmanAcceptedBatch& FlightAccepted =
			LastClientAcceptedByFlight[FlightIndex].IsWellFormed()
			? LastClientAcceptedByFlight[FlightIndex] : LastClientAcceptedBatch;
		const double AcceptedAgeSeconds = EstimatedServerNowSeconds
			- FlightAccepted.ServerAcceptedTimeSeconds;
		if (!FlightAccepted.IsWellFormed()
			|| FlightAccepted.Group != ReplicatedState.Lease.Group
			|| FlightAccepted.StateRef.MatchEpoch != ReplicatedState.MatchEpoch
			|| FlightAccepted.AbilitySetRevision != ReplicatedState.AbilityConfig.AbilitySetRevision
			|| FlightAccepted.FormationCommandRevision
				!= ReplicatedState.AbilityConfig.FormationCommandRevision
			|| FlightAccepted.FormationDefinitionChecksum
				!= ReplicatedState.AbilityConfig.FormationDefinitionChecksum
			|| AcceptedAgeSeconds < -0.05
			|| AcceptedAgeSeconds > FGuLiWingmanTargetAcquisition::MaximumAcceptedPoseAgeSeconds)
		{
			continue;
		}
		const FGuLiWingmanCandidateSample* EmitterSample =
			FlightAccepted.Samples.FindByPredicate(
				[GroupMemberIndex](const FGuLiWingmanCandidateSample& Sample)
				{
					return Sample.Wingman.GetGroupMemberIndex() == GroupMemberIndex;
				});
		if (!EmitterSample)
		{
			continue;
		}
		const FGuLiWingmanAttackTarget* AssignedTarget =
			GuLiWingmanTargeting::ResolveTargetForEmitter(
				ReplicatedState.AttackState, EmitterSample->Wingman);
		if (!AssignedTarget || !AssignedTarget->IsValid()
			|| EstimatedServerNowSeconds + 0.05 < AssignedTarget->ServerTime
			|| EstimatedServerNowSeconds - AssignedTarget->ServerTime
				> FGuLiWingmanTargetAcquisition::MaximumAcceptedPoseAgeSeconds)
		{
			continue;
		}
		const FVector EmitterLocation(
			static_cast<double>(EmitterSample->PositionCentimeters.X),
			static_cast<double>(EmitterSample->PositionCentimeters.Y),
			static_cast<double>(EmitterSample->PositionCentimeters.Z));
		const FRotator EmitterRotation(
			static_cast<double>(EmitterSample->RotationCentiDegrees.X) * 0.01,
			static_cast<double>(EmitterSample->RotationCentiDegrees.Y) * 0.01,
			static_cast<double>(EmitterSample->RotationCentiDegrees.Z) * 0.01);
		for (const FGuLiWingmanWeaponChannelConfig* Channel : AutomaticChannels)
		{
			if (!Channel || !GuLiWingmanAttack::IsInsideForwardArc(
				EmitterLocation, EmitterRotation.Vector(), AssignedTarget->Location,
				AssignedTarget->Radius, Channel->Runtime.RangeCentimeters,
				Channel->Runtime.TargetConeHalfAngleDegrees))
			{
				continue;
			}
			const bool bHasLineOfSight = !Channel->Runtime.bRequiresLineOfSight
				|| HasClientLineOfSight(EmitterLocation, *AssignedTarget);
			if (!bHasLineOfSight)
			{
				continue;
			}

			FGuLiWingmanFireIntent Intent;
			if (Simulation->TryBuildWeaponFireIntent(
				ReplicatedState.Lease.Group,
				EmitterSample->Wingman,
				ReplicatedState.MatchEpoch,
				ReplicatedState.Lease.LeaseEpoch,
				*Channel,
				*AssignedTarget,
				NowSeconds,
				ClientSimulationTick,
				bHasLineOfSight,
				Intent))
			{
				SubmitFireIntent(Intent);
			}
		}
	}
}

bool UGuLiWingmanRelayComponent::HasClientLineOfSight(
	const FVector& SourceLocation,
	const FGuLiWingmanAttackTarget& Target) const
{
	UWorld* World = GetWorld();
	if (!World || SourceLocation.ContainsNaN() || Target.Location.ContainsNaN())
	{
		return false;
	}
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiWingmanClientWeaponLos), false,
		Controller ? Controller->GetPawn() : nullptr);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(
		Hit,
		SourceLocation,
		Target.Location,
		ECC_Visibility,
		QueryParams))
	{
		return true;
	}
	if (const AActor* HitActor = Hit.GetActor())
	{
		if (const UGuLiCombatHealthComponent* Health =
			HitActor->FindComponentByClass<UGuLiCombatHealthComponent>())
		{
			return Health->GetTargetHandle() == Target.Target;
		}
	}
	return FVector::Distance(Hit.ImpactPoint, Target.Location)
		<= FMath::Max(100.0f, Target.Radius);
}

void UGuLiWingmanRelayComponent::ObserveAuthoritativeServerTime(
	const double ServerTimeSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !FMath::IsFinite(ServerTimeSeconds) || ServerTimeSeconds < 0.0)
	{
		return;
	}
	if (bHasClientServerTimeAnchor
		&& ServerTimeSeconds + UE_DOUBLE_SMALL_NUMBER
			< LastObservedAuthoritativeServerTimeSeconds)
	{
		return;
	}
	const double LocalWorldSeconds = static_cast<double>(World->GetTimeSeconds());
	// Result RPCs can arrive out of order, so reject an older authoritative sample.
	// Do allow a newer sample to correct the locally projected clock backwards: a
	// slightly faster client clock must not accumulate permanent future skew and turn
	// every later Candidate into CaptureTimeInvalid.
	LastObservedAuthoritativeServerTimeSeconds = ServerTimeSeconds;
	ClientServerTimeAnchorSeconds = ServerTimeSeconds;
	ClientServerTimeAnchorLocalWorldSeconds = LocalWorldSeconds;
	bHasClientServerTimeAnchor = true;
}

double UGuLiWingmanRelayComponent::GetEstimatedServerTimeSeconds() const
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return GetAuthorityTimeSeconds();
	}
	if (bHasClientServerTimeAnchor && GetWorld())
	{
		return ClientServerTimeAnchorSeconds + FMath::Max(
			0.0,
			static_cast<double>(GetWorld()->GetTimeSeconds())
				- ClientServerTimeAnchorLocalWorldSeconds);
	}
	const AGuLiBattleGameState* BattleGameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	return BattleGameState
		? static_cast<double>(BattleGameState->GetServerWorldTimeSeconds())
		: GetAuthorityTimeSeconds();
}

void UGuLiWingmanRelayComponent::DestroyClientOwnedGroup()
{
	const FGuLiWingmanGroupHandle PreviousBootstrapGroup = LastClientBootstrap.Commit.Group;
	LastClientAcceptedBatch = FGuLiWingmanAcceptedBatch{};
	LastAppliedAcceptedState = FGuLiAcceptedStateRef{};
	ClientUploadRateGrant = FGuLiWingmanUploadRateGrant{};
	bClientBootstrapLocallyApplied = false;
	bClientAtomicBaselineSubmitted = false;
	bClientAtomicBaselineAccepted = false;
	bClientAtomicBootstrapAcksSubmitted = false;
	NextClientAtomicBaselineRetryTimeSeconds = 0.0;
	ClientCandidateAccumulator = 0.0;
	CaptureClockGroup = {};
	LastCompletedPawnTick = 0u;
	LastOwnerUploadSimulationTick = 0u;
	NextClientActiveRosterAckRetryTimeSeconds = 0.0;
	LastRequestedResumeLeaseEpoch = 0u;
	NextClientResumeRequestTimeSeconds = 0.0;
	NextClientEmergencyRebaseSequence = 1u;
	RetainedTrajectoryCarrierGroup = FGuLiWingmanGroupHandle{};
	RetainedTrajectoryCarrierEpoch = 0u;
	bClientBootstrapPending = false;
	LastAcknowledgedGroup = FGuLiWingmanGroupHandle{};
	LastAcknowledgedLeaseEpoch = 0u;
	LastAcknowledgedCutId = 0u;
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		LastClientAcceptedByFlight[FlightIndex] = FGuLiWingmanAcceptedBatch{};
		NextClientFrameSequenceByFlight[FlightIndex] = 0u;
		ClientAcceptedSequenceByFlight[FlightIndex] = 0u;
		LastSubmittedClientTickByFlight[FlightIndex] = 0u;
		RetainedTrajectoryByFlight[FlightIndex].Reset();
		LastRetainedTrajectoryTickByFlight[FlightIndex] = 0u;
	}
	NextBasicTargetScanSeconds.Reset();
	LastClientBootstrap = FGuLiWingmanBootstrapBundle{};
	if (!GetWorld())
	{
		return;
	}
	if (UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>())
	{
		if (ReplicatedState.Lease.Group.IsValid())
		{
			Simulation->DestroyOwnedGroup(ReplicatedState.Lease.Group);
		}
		if (PreviousBootstrapGroup.IsValid()
			&& PreviousBootstrapGroup != ReplicatedState.Lease.Group)
		{
			Simulation->DestroyOwnedGroup(PreviousBootstrapGroup);
		}
	}
	if (AGuLiWingmanPresentationActor* Presentation =
		AGuLiWingmanPresentationActor::FindOrSpawn(GetWorld()))
	{
		if (ReplicatedState.Lease.Group.IsValid())
		{
			Presentation->RemoveGroup(ReplicatedState.Lease.Group);
		}
		if (PreviousBootstrapGroup.IsValid()
			&& PreviousBootstrapGroup != ReplicatedState.Lease.Group)
		{
			Presentation->RemoveGroup(PreviousBootstrapGroup);
		}
	}
}

bool UGuLiWingmanRelayComponent::BuildAndSendBootstrap()
{
	FGuLiWingmanBootstrapBundle Bootstrap;
	if (!ServerRelay || !ServerRelay->BuildBootstrap(Bootstrap))
	{
		return false;
	}
	ClientReceiveBootstrap(Bootstrap);
	if (Bootstrap.UploadRateGrant.IsWellFormed())
	{
		ClientReceiveUploadRateGrant(Bootstrap.UploadRateGrant);
	}
	if (AGuLiBattleGameState* BattleGameState =
		GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
	{
		BattleGameState->ServerPublishWingmanBootstrap(
			Bootstrap,
			ServerRelay->GetLeaseState().Lifecycle);
	}
	return true;
}

void UGuLiWingmanRelayComponent::HandleCandidateResult(const FGuLiWingmanSubmissionResult& Result)
{
	FGuLiWingmanSimulationAcceptance Acceptance = Result.Acceptance;
	Acceptance.Disposition = Result.Disposition;
	Acceptance.RejectReason = Result.RejectReason;
	if (!Acceptance.Group.IsValid() && ServerRelay)
	{
		Acceptance.Group = ServerRelay->GetLeaseState().Group;
		Acceptance.LeaseEpoch = ServerRelay->GetLeaseState().LeaseEpoch;
		Acceptance.RosterRevision = ServerRelay->GetRosterRevision();
		Acceptance.AvailabilityAfter = ServerRelay->GetLeaseState().Lifecycle;
		Acceptance.AllowedUploadRateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;
		Acceptance.GrantRevision = ServerRelay->GetUploadRateGrant().GrantRevision;
		Acceptance.GrantExpiryServerTimeSeconds =
			ServerRelay->GetUploadRateGrant().ExpiryServerTimeSeconds;
	}
	FGuLiWingmanCandidateResultWire WireResult;
	WireResult.CandidateSequence = Result.Sequence;
	WireResult.Acceptance = Acceptance;
	WireResult.AcceptedBatch = Result.AcceptedBatch;
	const bool bAttackLoadout = ServerRelay && ServerRelay->GetAbilityConfig().WeaponChannels.ContainsByPredicate([](const auto& Channel)
		{ return Channel.bEnabled && Channel.Runtime.Attack.Pattern != EGuLiWingmanAttackPattern::Legacy; });
	if (ReliableAttackCandidateSequences.Contains(Result.Sequence) || bAttackLoadout)
	{
		ClientReceiveAttackCandidateResult(WireResult);
		if (Result.Disposition != EGuLiWingmanSubmissionDisposition::Pending)
			ReliableAttackCandidateSequences.Remove(Result.Sequence);
	}
	else ClientReceiveCandidateResult(WireResult);
	if (Result.Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
	{
		RefreshReplicatedState();
		const APlayerController* Controller = Cast<APlayerController>(GetOwner());
		if (Controller && Controller->IsLocalController())
		{
			// A listen host has no remote network hop. It must still consume only a NetSerialize
			// round-tripped DTO; the apply helper remains StateRef-idempotent if the local Client RPC
			// implementation is also dispatched by the engine.
			ConsumeValidatedCandidateResultWire(WireResult, false);
		}
		if (AGuLiBattleGameState* BattleGameState =
			GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr)
		{
			BattleGameState->ServerPublishWingmanAcceptedBatch(Result.AcceptedBatch);
		}
	}
}

bool UGuLiWingmanRelayComponent::ConsumeValidatedCandidateResultWire(
	const FGuLiWingmanCandidateResultWire& Result,
	const bool bBroadcastResult)
{
	FGuLiWingmanCandidateResultWire WireCopy;
	if (!GuLiWingmanRelayWire::MakeValidatedCandidateResultCopy(Result, WireCopy))
	{
		return false;
	}
	ObserveAuthoritativeServerTime(WireCopy.Acceptance.AcceptedServerTimeSeconds);
	if (bBroadcastResult)
	{
		OnCandidateResult.Broadcast(
			static_cast<int64>(WireCopy.CandidateSequence),
			WireCopy.Acceptance.Disposition,
			WireCopy.Acceptance.RejectReason);
	}
	if (WireCopy.Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
	{
		return ApplyAcceptedBatchToLocalOwner(WireCopy.AcceptedBatch);
	}

	// Candidate results are deliberately Unreliable. If one Accepted DTO is lost,
	// subsequent requests carry the old BaseAcceptedSequence and the authority replies
	// with a typed Pending/Rejected result that echoes the exact current Flight baseline.
	// Advance only that logical high-water mark after validating the full current
	// Group/Lease/Roster/Match context. The missing pose is never fabricated: Pawn weapon
	// SourceRefs and the cached Accepted batch advance only when a complete Accepted DTO arrives.
	const uint8 FlightIndex = WireCopy.Acceptance.FlightIndex;
	if (FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
	{
		TryAdvanceAcceptedSequenceFromRebase(
			WireCopy.Acceptance,
			ReplicatedState.Lease.Group,
			ReplicatedState.Lease.LeaseEpoch,
			ReplicatedState.RosterRevision,
			ReplicatedState.MatchEpoch,
			ClientAcceptedSequenceByFlight[FlightIndex]);
	}
	return true;
}
