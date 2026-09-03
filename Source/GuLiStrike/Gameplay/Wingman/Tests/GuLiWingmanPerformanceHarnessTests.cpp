// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/Relay/GuLiWingmanRelayServer.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanGroupBehaviorRunner.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassProcessors.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "MassProcessingTypes.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanPerformanceHarness
{
	constexpr float FixedStepSeconds = 1.0f / 30.0f;
	constexpr int32 GroupSize = static_cast<int32>(GULI_WINGMAN_GROUP_SIZE);
	constexpr double H4000OwnerP95BudgetMilliseconds = 1000.0 / 30.0;
	constexpr double H4000OwnerThroughputFloor = 4000.0 * 30.0;
	constexpr double H4000ServerP95BudgetMilliseconds = 2.5;
	constexpr double H4000ServerMaximumBudgetMilliseconds = 8.0;
	constexpr double H4000ServerThroughputFloor = 4000.0 * 10.0;
	constexpr int32 H4000OwnerWarmupSteps = 30;
	constexpr int32 H4000OwnerMeasuredSteps = 300;
	constexpr int32 H4000ServerMinimumServiceSamples = 50;

	struct FScopedFlightNavSegmentValidator
	{
		explicit FScopedFlightNavSegmentValidator(UWorld* TransientTestWorld)
		{
			GuLiWingmanAvoidance::SetFlightNavSegmentValidatorForTests(
				TransientTestWorld,
				[](const FVector&, const FVector&, const float)
				{
					return true;
				});
		}

		~FScopedFlightNavSegmentValidator()
		{
			GuLiWingmanAvoidance::ResetFlightNavSegmentValidatorForTests();
		}
	};

	struct FTransientGameWorldFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FTransientGameWorldFixture()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for the performance harness"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Transient performance World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			return Test.TestTrue(TEXT("Owner harness uses a standalone client execution domain"),
				World->IsGameWorld() && World->GetNetMode() == NM_Standalone);
		}
	};

	struct FDistribution
	{
		double P50Milliseconds = 0.0;
		double P95Milliseconds = 0.0;
		double MaximumMilliseconds = 0.0;
	};

	FDistribution SummarizeMilliseconds(const TArray<double>& Samples)
	{
		FDistribution Result;
		if (Samples.IsEmpty())
		{
			return Result;
		}
		TArray<double> Sorted = Samples;
		Sorted.Sort();
		const auto NearestRank = [&Sorted](const double Quantile)
		{
			const int32 OneBasedRank = FMath::CeilToInt(Quantile * static_cast<double>(Sorted.Num()));
			return Sorted[FMath::Clamp(OneBasedRank - 1, 0, Sorted.Num() - 1)];
		};
		Result.P50Milliseconds = NearestRank(0.50);
		Result.P95Milliseconds = NearestRank(0.95);
		Result.MaximumMilliseconds = Sorted.Last();
		return Result;
	}

	FGuLiWingmanGroupHandle MakeGroup(const int32 GroupIndex, const uint32 NamespaceSeed)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(
			NamespaceSeed,
			static_cast<uint32>(GroupIndex + 1),
			0x51a70000u + static_cast<uint32>(GroupIndex),
			0x9e370000u ^ static_cast<uint32>(GroupIndex));
		Group.ShipGeneration = 1u;
		Group.GroupGeneration = 1u;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeAbilityConfig(
		const FGuLiWingmanGroupHandle& Group,
		const uint32 SnapshotRevision = 1u)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 1u;
		Config.SnapshotRevision = SnapshotRevision;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 1u;
		Config.FormationDefinitionChecksum = 0x1111222233334444ull;
		Config.BasicWeaponDefinitionRevision = 1u;
		Config.BasicWeaponDefinitionChecksum = 0x2222333344445555ull;
		Config.MissileDefinitionRevision = 1u;
		Config.MissileDefinitionChecksum = 0x3333444455556666ull;
		Config.FormationCommandRevision = 1u;
		Config.EffectiveClientSimTick = 100u;
		Config.RefreshHash();
		return Config;
	}

	struct FOwnerPipeline
	{
		UGuLiWingmanModeProcessor* Mode = nullptr;
		UGuLiWingmanFormationGuidanceProcessor* Guidance = nullptr;
		UGuLiWingmanAvoidanceProcessor* Avoidance = nullptr;
		UGuLiWingmanFlightIntegrationProcessor* Integration = nullptr;

		bool Initialize(UObject* Owner, FMassEntityManager& EntityManager)
		{
			Mode = NewObject<UGuLiWingmanModeProcessor>(Owner);
			Guidance = NewObject<UGuLiWingmanFormationGuidanceProcessor>(Owner);
			Avoidance = NewObject<UGuLiWingmanAvoidanceProcessor>(Owner);
			Integration = NewObject<UGuLiWingmanFlightIntegrationProcessor>(Owner);
			if (!Mode || !Guidance || !Avoidance || !Integration)
			{
				return false;
			}
			const TSharedRef<FMassEntityManager> SharedManager = EntityManager.AsShared();
			Mode->CallInitialize(Owner, SharedManager);
			Guidance->CallInitialize(Owner, SharedManager);
			Avoidance->CallInitialize(Owner, SharedManager);
			Integration->CallInitialize(Owner, SharedManager);
			return Mode->IsInitialized() && Guidance->IsInitialized()
				&& Avoidance->IsInitialized() && Integration->IsInitialized();
		}

		void Step(FMassEntityManager& EntityManager) const
		{
			UMassProcessor* Processors[] = {Mode, Guidance, Avoidance, Integration};
			UE::Mass::FProcessingContext ProcessingContext(EntityManager, FixedStepSeconds);
			UE::Mass::Executor::RunProcessorsView(MakeArrayView(Processors), ProcessingContext);
		}
	};

	uint64 ApproximateOwnerEntityPayloadBytes(const int32 EntityCount)
	{
		constexpr uint64 BytesPerEntity =
			sizeof(FTransformFragment)
			+ sizeof(FGuLiWingmanIdentityFragment)
			+ sizeof(FGuLiWingmanAbilityFragment)
			+ sizeof(FGuLiWingmanTuningFragment)
			+ sizeof(FGuLiWingmanCarrierFragment)
			+ sizeof(FGuLiWingmanFormationSlotFragment)
			+ sizeof(FGuLiWingmanGuidanceFragment)
			+ sizeof(FGuLiWingmanNavigationGuidanceFragment)
			+ sizeof(FGuLiWingmanAvoidanceFragment)
			+ sizeof(FGuLiWingmanFlightDynamicsFragment)
			+ sizeof(FGuLiWingmanWeaponStateFragment)
			+ sizeof(FGuLiWingmanOwnerMassTag);
		return static_cast<uint64>(EntityCount) * BytesPerEntity;
	}

	struct FOwnerRunResult
	{
		FDistribution StepTime;
		double SetupMilliseconds = 0.0;
		double TotalMeasuredMilliseconds = 0.0;
		double ThroughputEntitiesPerSecond = 0.0;
		double OldestQueueItemAgeMilliseconds = 0.0;
		uint64 ApproxEntityPayloadBytes = 0u;
		int64 ProcessResidentDeltaBytes = 0;
		int32 EntityCount = 0;
		int32 SampleCount = 0;
		int32 MaximumQueueDepth = 0;
		int32 FinalQueueDepth = 0;
		int32 DropCount = 0;
		int32 PresentationActorCount = 0;
		bool bTransformAdvanced = false;
	};

	bool RunOwnerHarness(
		FAutomationTestBase& Test,
		const int32 GroupCount,
		const int32 WarmupSteps,
		const int32 MeasuredSteps,
		FOwnerRunResult& OutResult)
	{
		if (GroupCount <= 0 || WarmupSteps < 0 || MeasuredSteps <= 0)
		{
			Test.AddError(TEXT("Owner harness received invalid workload dimensions"));
			return false;
		}
		FTransientGameWorldFixture Fixture;
		if (!Fixture.Initialize(Test))
		{
			return false;
		}
		FScopedFlightNavSegmentValidator AllowAllNavigationSegments(Fixture.World);
		UGuLiWingmanSimulationSubsystem* Simulation =
			Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
		UMassEntitySubsystem* MassSubsystem = Fixture.World->GetSubsystem<UMassEntitySubsystem>();
		if (!Test.TestNotNull(TEXT("Owner simulation subsystem exists"), Simulation)
			|| !Test.TestNotNull(TEXT("Mass entity subsystem exists"), MassSubsystem))
		{
			return false;
		}
		Simulation->SetNavigationRequirementBypassForTests(true);

		const uint64 ResidentBefore = FPlatformMemory::GetStats().UsedPhysical;
		const double SetupStart = FPlatformTime::Seconds();
		TArray<FGuLiWingmanGroupHandle> Groups;
		Groups.Reserve(GroupCount);
		FGuLiCarrierSourceRef CarrierSource;
		CarrierSource.CanonicalEpoch = 1u;
		CarrierSource.MoveRevision = 1u;
		for (int32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
		{
			const FGuLiWingmanGroupHandle Group = MakeGroup(GroupIndex, 0x0a110001u);
			const FGuLiGroupAbilityConfigSnapshot Config = MakeAbilityConfig(Group);
			if (!Simulation->CreateOrResetOwnedGroup(
				Group,
				Config,
				FTransform(FVector(static_cast<double>(GroupIndex) * 300000.0, 0.0, 0.0)),
				FVector::ZeroVector,
				CarrierSource))
			{
				Test.AddError(FString::Printf(TEXT("Failed to create owner group %d"), GroupIndex));
				return false;
			}
			Groups.Add(Group);
		}
		OutResult.SetupMilliseconds = (FPlatformTime::Seconds() - SetupStart) * 1000.0;
		OutResult.EntityCount = Simulation->GetTotalOwnedEntityCount();
		const uint64 ResidentAfterSetup = FPlatformMemory::GetStats().UsedPhysical;
		OutResult.ProcessResidentDeltaBytes = ResidentAfterSetup >= ResidentBefore
			? static_cast<int64>(ResidentAfterSetup - ResidentBefore)
			: -static_cast<int64>(ResidentBefore - ResidentAfterSetup);
		OutResult.ApproxEntityPayloadBytes = ApproximateOwnerEntityPayloadBytes(OutResult.EntityCount);

		for (TActorIterator<AGuLiWingmanPresentationActor> It(Fixture.World); It; ++It)
		{
			++OutResult.PresentationActorCount;
		}

		FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
		FOwnerPipeline Pipeline;
		if (!Test.TestTrue(TEXT("Owner Mass performance pipeline initializes"),
			Pipeline.Initialize(MassSubsystem, EntityManager)))
		{
			return false;
		}
		for (int32 Index = 0; Index < WarmupSteps; ++Index)
		{
			Pipeline.Step(EntityManager);
		}

		FGuLiWingmanCandidateBatch Before;
		if (!Simulation->BuildCandidate(Groups[0], 1u, 1u, 1u, 100u, Before))
		{
			Test.AddError(TEXT("Could not capture the pre-measurement owner transform"));
			return false;
		}

		struct FQueuedStep
		{
			double EnqueuedWallSeconds = 0.0;
			uint32 ClientSimTick = 0u;
		};
		TArray<FQueuedStep> Queue;
		Queue.Reserve(MeasuredSteps);
		const double QueueStart = FPlatformTime::Seconds();
		for (int32 Index = 0; Index < MeasuredSteps; ++Index)
		{
			Queue.Add({QueueStart, static_cast<uint32>(101 + Index)});
		}
		OutResult.MaximumQueueDepth = Queue.Num();
		TArray<double> StepMilliseconds;
		StepMilliseconds.Reserve(MeasuredSteps);
		int32 QueueHead = 0;
		const double MeasurementStart = FPlatformTime::Seconds();
		while (QueueHead < Queue.Num())
		{
			OutResult.OldestQueueItemAgeMilliseconds = FMath::Max(
				OutResult.OldestQueueItemAgeMilliseconds,
				(FPlatformTime::Seconds() - Queue[QueueHead].EnqueuedWallSeconds) * 1000.0);
			const double StepStart = FPlatformTime::Seconds();
			Pipeline.Step(EntityManager);
			StepMilliseconds.Add((FPlatformTime::Seconds() - StepStart) * 1000.0);
			++QueueHead;
		}
		const double TotalSeconds = FPlatformTime::Seconds() - MeasurementStart;
		OutResult.TotalMeasuredMilliseconds = TotalSeconds * 1000.0;
		OutResult.SampleCount = StepMilliseconds.Num();
		OutResult.FinalQueueDepth = Queue.Num() - QueueHead;
		OutResult.ThroughputEntitiesPerSecond = TotalSeconds > UE_SMALL_NUMBER
			? static_cast<double>(OutResult.EntityCount) * static_cast<double>(MeasuredSteps) / TotalSeconds
			: 0.0;
		OutResult.StepTime = SummarizeMilliseconds(StepMilliseconds);

		FGuLiWingmanCandidateBatch After;
		if (!Simulation->BuildCandidate(Groups[0], 1u, 1u, 2u,
			static_cast<uint32>(100 + WarmupSteps + MeasuredSteps), After))
		{
			Test.AddError(TEXT("Could not capture the post-measurement owner transform"));
			return false;
		}
		OutResult.bTransformAdvanced = Before.Samples.Num() == GroupSize
			&& After.Samples.Num() == GroupSize
			&& Before.Samples[0].PositionCentimeters != After.Samples[0].PositionCentimeters;

		Simulation->DestroyAllOwnedGroups();
		return true;
	}

	FString OwnerResultJson(
		const TCHAR* RunName,
		const FOwnerRunResult& Result,
		const bool bFormalGate,
		const double P95BudgetMilliseconds,
		const double ThroughputFloor)
	{
		return FString::Printf(
			TEXT("{\"schema\":\"guli.wingman.performance.v1\",\"run\":\"%s\",\"scope\":\"owner_mass_mode_guidance_avoidance_integration_no_render_no_authored_nav\",\"formal_gate\":%s,\"p95_budget_ms\":%.6f,\"throughput_floor_entity_steps_per_second\":%.3f,\"fixed_hz\":30,\"entity_count\":%d,\"sample_count\":%d,\"quantile_method\":\"nearest-rank\",\"step_p50_ms\":%.6f,\"step_p95_ms\":%.6f,\"step_max_ms\":%.6f,\"setup_ms\":%.6f,\"total_measured_ms\":%.6f,\"throughput_entities_per_second\":%.3f,\"queue_max_depth\":%d,\"queue_capacity\":%d,\"queue_oldest_age_ms\":%.6f,\"queue_final_depth\":%d,\"drop_count\":%d,\"approx_entity_payload_bytes\":%llu,\"process_resident_delta_bytes_approx\":%lld,\"presentation_actor_count\":%d,\"transform_advanced\":%s}"),
			RunName,
			bFormalGate ? TEXT("true") : TEXT("false"),
			P95BudgetMilliseconds,
			ThroughputFloor,
			Result.EntityCount,
			Result.SampleCount,
			Result.StepTime.P50Milliseconds,
			Result.StepTime.P95Milliseconds,
			Result.StepTime.MaximumMilliseconds,
			Result.SetupMilliseconds,
			Result.TotalMeasuredMilliseconds,
			Result.ThroughputEntitiesPerSecond,
			Result.MaximumQueueDepth,
			Result.MaximumQueueDepth,
			Result.OldestQueueItemAgeMilliseconds,
			Result.FinalQueueDepth,
			Result.DropCount,
			Result.ApproxEntityPayloadBytes,
			Result.ProcessResidentDeltaBytes,
			Result.PresentationActorCount,
			Result.bTransformAdvanced ? TEXT("true") : TEXT("false"));
	}

	struct FRelayRuntime
	{
		TUniquePtr<FGuLiWingmanRelayServer> Relay;
		FGuid Owner;
		bool bHighRateGrantIssued = false;
	};

	bool InitializeRelayCore(
		FRelayRuntime& Runtime,
		const FGuLiWingmanGroupHandle& Group,
		const int32 GroupIndex,
		const bool bStrictFlightContract)
	{
		Runtime.Relay = MakeUnique<FGuLiWingmanRelayServer>();
		Runtime.Owner = FGuid(0x0a220001u, static_cast<uint32>(GroupIndex + 1), 0x11111111u, 0x22222222u);
		const FGuid Backup(0x0a220002u, static_cast<uint32>(GroupIndex + 1), 0x33333333u, 0x44444444u);
		FGuLiWingmanRelayTuning Tuning;
		// Strict production packets are per Flight. Give the replay enough packet
		// budget to measure validation rather than intentionally exercise rate rejection.
		Tuning.CandidateBucketCapacity = bStrictFlightContract ? 128.0 : Tuning.CandidateBucketCapacity;
		Tuning.CandidateTokensPerSecond = bStrictFlightContract ? 128.0 : Tuning.CandidateTokensPerSecond;
		if (!Runtime.Relay->InitializeGroup(
			1u, Group, Runtime.Owner, Backup, MakeAbilityConfig(Group), 0.0, Tuning))
		{
			return false;
		}
		return !bStrictFlightContract || Runtime.Relay->ConfigureStrictFlightContract(
			1u, FGuLiWingmanRelayValidationRevisions{}, 0.0);
	}

	bool AcknowledgeRelayBootstrap(
		FRelayRuntime& Runtime,
		const FGuLiWingmanBootstrapBundle& Bootstrap,
		const double NowSeconds)
	{
		FGuLiGroupAbilityConfigAck AbilityAck;
		AbilityAck.Group = Runtime.Relay->GetLeaseState().Group;
		AbilityAck.LeaseEpoch = Runtime.Relay->GetLeaseState().LeaseEpoch;
		AbilityAck.SnapshotRevision = Runtime.Relay->GetAbilityConfig().SnapshotRevision;
		AbilityAck.SnapshotHash = Runtime.Relay->GetAbilityConfig().SnapshotHash;
		const FGuLiWingmanTransferBaseline* Baseline = Bootstrap.bHasTransferBaseline
			? &Bootstrap.TransferBaseline : nullptr;
		return Runtime.Relay->AcknowledgeAbilityConfig(Runtime.Owner, AbilityAck, NowSeconds)
			&& Runtime.Relay->AcknowledgeBootstrap(
				Runtime.Owner, Bootstrap.Commit, Baseline, NowSeconds + 0.001)
			&& Runtime.Relay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active;
	}

	FGuLiWingmanCandidateBatch MakeLegacyReplayCandidate(
		const FRelayRuntime& Runtime,
		const uint32 Sequence,
		const uint32 ClientSimTick)
	{
		const FGuLiWingmanRelayServer& Relay = *Runtime.Relay;
		const FGuLiGroupAbilityConfigSnapshot& Config = Relay.GetAbilityConfig();
		FGuLiWingmanCandidateBatch Candidate;
		Candidate.MatchEpoch = Relay.GetMatchEpoch();
		Candidate.Group = Relay.GetLeaseState().Group;
		Candidate.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Candidate.CandidateSequence = Sequence;
		Candidate.ClientSimTick = ClientSimTick;
		Candidate.CarrierSource.CanonicalEpoch = 1u;
		Candidate.CarrierSource.MoveRevision = Sequence;
		Candidate.AbilitySetRevision = Config.AbilitySetRevision;
		Candidate.FormationCommandRevision = Config.FormationCommandRevision;
		Candidate.FormationDefinitionChecksum = Config.FormationDefinitionChecksum;
		const int32 ElapsedTicks = static_cast<int32>(ClientSimTick) - 100;
		const int32 XDisplacement = FMath::RoundToInt(5000.0 * static_cast<double>(ElapsedTicks) / 30.0);
		for (const FGuLiWingmanRosterEntry& Roster : Relay.GetRoster())
		{
			FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
			Sample.Wingman = Roster.Wingman;
			const int32 MemberIndex = Roster.Wingman.GetGroupMemberIndex();
			Sample.PositionCentimeters = FIntVector(1000 + XDisplacement, MemberIndex * 100, 5000);
			Sample.VelocityCentimetersPerSecond = FIntVector(5000, 0, 0);
			Sample.RotationCentiDegrees = FIntVector::ZeroValue;
			Sample.FlightMode = 1u;
		}
		return Candidate;
	}

	FGuLiWingmanCandidateBatch MakeStrictFlightCandidate(
		const FRelayRuntime& Runtime,
		const uint8 FlightIndex,
		const uint32 CandidateSequence,
		const uint32 FrameSequence,
		const uint32 BaseAcceptedSequence,
		const uint32 ClientSimTick,
		const double CaptureEstimatedServerTimeSeconds,
		const EGuLiWingmanUploadRateClass RequestedRateClass,
		const uint32 ObservedGrantRevision)
	{
		const FGuLiWingmanRelayServer& Relay = *Runtime.Relay;
		const FGuLiGroupAbilityConfigSnapshot& Config = Relay.GetAbilityConfig();
		const FGuLiWingmanRelayValidationRevisions& Revisions = Relay.GetValidationRevisions();
		FGuLiWingmanCandidateBatch Candidate;
		Candidate.MatchEpoch = Relay.GetMatchEpoch();
		Candidate.ConnectionGeneration = Relay.GetConnectionGeneration();
		Candidate.Group = Relay.GetLeaseState().Group;
		Candidate.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Candidate.RosterRevision = Relay.GetRosterRevision();
		Candidate.FlightIndex = FlightIndex;
		Candidate.RequestedRateClass = RequestedRateClass;
		Candidate.ObservedGrantRevision = ObservedGrantRevision;
		Candidate.CandidateSequence = CandidateSequence;
		Candidate.FrameSequence = FrameSequence;
		Candidate.BaseAcceptedSequence = BaseAcceptedSequence;
		Candidate.ClientSimTick = ClientSimTick;
		Candidate.CaptureEstimatedServerTimeSeconds = CaptureEstimatedServerTimeSeconds;
		Candidate.NavSchemaRevision = Revisions.NavSchemaRevision;
		Candidate.NavDataChecksum = Revisions.NavDataChecksum;
		Candidate.TuningRevision = Revisions.TuningRevision;
		Candidate.ObstacleRevision = Revisions.ObstacleRevision;
		Candidate.CarrierSource.CanonicalEpoch = 1u;
		Candidate.CarrierSource.MoveRevision = CandidateSequence;
		Candidate.AbilitySetRevision = Config.AbilitySetRevision;
		Candidate.FormationCommandRevision = Config.FormationCommandRevision;
		Candidate.FormationDefinitionChecksum = Config.FormationDefinitionChecksum;
		const int32 ElapsedTicks = static_cast<int32>(ClientSimTick) - 100;
		const int32 XDisplacement = FMath::RoundToInt(
			5000.0 * static_cast<double>(ElapsedTicks) / 30.0);
		for (const FGuLiWingmanRosterEntry& Roster : Relay.GetRoster())
		{
			if (Roster.bDead || Roster.Wingman.Flight.FlightIndex != FlightIndex)
			{
				continue;
			}
			Candidate.RequiredMemberMask |= static_cast<uint8>(1u << Roster.Wingman.MemberIndex);
			FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
			Sample.Wingman = Roster.Wingman;
			const int32 MemberIndex = Roster.Wingman.GetGroupMemberIndex();
			Sample.PositionCentimeters = FIntVector(1000 + XDisplacement, MemberIndex * 100, 5000);
			Sample.VelocityCentimetersPerSecond = FIntVector(5000, 0, 0);
			Sample.RotationCentiDegrees = FIntVector::ZeroValue;
			Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Follow);
		}
		return Candidate;
	}

	FGuLiCarrierSourceResolver ReplayCarrier(const double ServerTimeSeconds)
	{
		return [ServerTimeSeconds](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& OutState)
		{
			OutState.Transform = FTransform::Identity;
			OutState.Velocity = FVector(5000.0, 0.0, 0.0);
			OutState.ServerWorldTimeSeconds = ServerTimeSeconds;
			return EGuLiRelayCarrierLookupResult::Found;
		};
	}

	FGuLiCandidateWorldValidator PermitReplayWorld()
	{
		return [](const FGuLiWingmanCandidateWorldValidationContext& Context)
		{
			return Context.IsWellFormed()
				? EGuLiWingmanRejectReason::None
				: EGuLiWingmanRejectReason::InvalidIdentity;
		};
	}

	bool ActivateLegacyRelay(
		FRelayRuntime& Runtime,
		const FGuLiWingmanGroupHandle& Group,
		const int32 GroupIndex)
	{
		if (!InitializeRelayCore(Runtime, Group, GroupIndex, false))
		{
			return false;
		}
		FGuLiWingmanBootstrapBundle Bootstrap;
		return Runtime.Relay->BuildBootstrap(Bootstrap)
			&& AcknowledgeRelayBootstrap(Runtime, Bootstrap, 0.01);
	}

	bool ActivateStrictRelay(
		FRelayRuntime& Runtime,
		const FGuLiWingmanGroupHandle& Group,
		const int32 GroupIndex)
	{
		if (!InitializeRelayCore(Runtime, Group, GroupIndex, true))
		{
			return false;
		}
		FGuLiWingmanBootstrapBundle Bootstrap;
		if (!Runtime.Relay->BuildBootstrap(Bootstrap) || !Bootstrap.bRequiresAtomicCandidateBatch)
		{
			return false;
		}

		FGuLiWingmanAtomicCandidateBatchFragment Fragment;
		Fragment.FragmentIndex = 0u;
		for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
		{
			Fragment.Flights.Add(MakeStrictFlightCandidate(
				Runtime,
				FlightIndex,
				static_cast<uint32>(FlightIndex) + 1u,
				1u,
				0u,
				100u,
				0.05,
				EGuLiWingmanUploadRateClass::Cruise5Hz,
				Runtime.Relay->GetUploadRateGrant().GrantRevision));
		}
		Fragment.Header.BatchId = static_cast<uint64>(GroupIndex) + 1u;
		Fragment.Header.BatchKind = Bootstrap.AtomicBatchKind;
		Fragment.Header.Group = Group;
		Fragment.Header.ConnectionGeneration = Bootstrap.ConnectionGeneration;
		Fragment.Header.LeaseEpoch = Runtime.Relay->GetLeaseState().LeaseEpoch;
		Fragment.Header.FrozenRosterRevision = Bootstrap.RosterRevision;
		Fragment.Header.FrozenRequiredFlightMask = Bootstrap.RequiredFlightMask;
		Fragment.Header.FrozenRequiredMemberMaskHash = Bootstrap.RequiredMemberMaskHash;
		Fragment.Header.BaselineRevision = Bootstrap.AtomicBaselineRevision;
		Fragment.Header.BaselineHash = Bootstrap.AtomicBaselineHash;
		Fragment.Header.IncludedFlightMask = Bootstrap.RequiredFlightMask;
		Fragment.Header.ClientBatchStartTick = 100u;
		Fragment.Header.FragmentCount = 1u;
		Fragment.Header.BatchPayloadBytes = Fragment.EstimatePayloadBytes();
		Fragment.Header.BatchPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(Fragment.Flights);
		Fragment.Header.bAtomicCommit = true;
		const FGuLiWingmanAtomicBatchAcceptance Atomic = Runtime.Relay->SubmitAtomicCandidateFragment(
			Runtime.Owner, Fragment, 0.05, ReplayCarrier(0.05), PermitReplayWorld());
		return Atomic.Disposition == EGuLiWingmanSubmissionDisposition::Accepted
			&& Atomic.CommittedFlightMask == Bootstrap.RequiredFlightMask
			&& AcknowledgeRelayBootstrap(Runtime, Bootstrap, 0.06);
	}

	struct FQueuedRelayCandidate
	{
		int32 RelayIndex = INDEX_NONE;
		FGuLiWingmanCandidateBatch Candidate;
		double ServerTimeSeconds = 0.0;
		double EnqueuedWallSeconds = 0.0;
	};

	struct FServerRunResult
	{
		FDistribution ServiceStepTime;
		double SetupMilliseconds = 0.0;
		double TotalMeasuredMilliseconds = 0.0;
		double ThroughputCandidatesPerSecond = 0.0;
		double ThroughputSamplesPerSecond = 0.0;
		double OldestQueueItemAgeMilliseconds = 0.0;
		uint64 ApproxReplayPayloadBytes = 0u;
		int64 ProcessResidentDeltaBytes = 0;
		uint64 ServerMovementWriteCount = 0u;
		int32 WingmanCount = 0;
		int32 RelayCount = 0;
		int32 CandidateCount = 0;
		int32 AcceptedCandidateCount = 0;
		int32 RelayedSampleCount = 0;
		int32 ServiceStepCount = 0;
		int32 CandidatesPerServiceStep = 0;
		int32 MaximumQueueDepth = 0;
		int32 FinalQueueDepth = 0;
		int32 DropCount = 0;
		int32 CruiseCaptureCountPerGroup = 0;
		int32 CombatCaptureCountPerGroup = 0;
	};

	bool RunServerHarness(
		FAutomationTestBase& Test,
		const int32 GroupCount,
		const int32 CandidatesPerServiceStep,
		FServerRunResult& OutResult)
	{
		if (GroupCount <= 0 || CandidatesPerServiceStep <= 0)
		{
			Test.AddError(TEXT("Server harness received invalid workload dimensions"));
			return false;
		}
		const uint64 ResidentBefore = FPlatformMemory::GetStats().UsedPhysical;
		const double SetupStart = FPlatformTime::Seconds();
		TArray<FRelayRuntime> Relays;
		Relays.Reserve(GroupCount);
		for (int32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
		{
			FRelayRuntime& Runtime = Relays.AddDefaulted_GetRef();
			if (!ActivateStrictRelay(Runtime, MakeGroup(GroupIndex, 0x0a220010u), GroupIndex))
			{
				Test.AddError(FString::Printf(TEXT("Failed to activate relay group %d"), GroupIndex));
				return false;
			}
		}

		// Four 5 Hz Cruise captures followed by four authority-granted 10 Hz Combat
		// captures. Each capture contains five production-shaped per-Flight packets.
		const uint32 ClientTicks[] = {106u, 112u, 118u, 124u, 127u, 130u, 133u, 136u};
		const double ServerTimes[] = {0.30, 0.50, 0.70, 0.90, 1.00, 1.10, 1.20, 1.30};
		OutResult.CruiseCaptureCountPerGroup = 4;
		OutResult.CombatCaptureCountPerGroup = 4;
		TArray<FQueuedRelayCandidate> Queue;
		Queue.Reserve(GroupCount * UE_ARRAY_COUNT(ClientTicks) * GULI_WINGMAN_FLIGHT_COUNT);
		const double QueueWallStart = FPlatformTime::Seconds();
		for (int32 CaptureIndex = 0; CaptureIndex < UE_ARRAY_COUNT(ClientTicks); ++CaptureIndex)
		{
			for (int32 RelayIndex = 0; RelayIndex < Relays.Num(); ++RelayIndex)
			{
				for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
				{
					FQueuedRelayCandidate& Work = Queue.AddDefaulted_GetRef();
					Work.RelayIndex = RelayIndex;
					const bool bHighRate = CaptureIndex >= OutResult.CruiseCaptureCountPerGroup;
					Work.Candidate = MakeStrictFlightCandidate(
						Relays[RelayIndex],
						FlightIndex,
						6u + static_cast<uint32>(CaptureIndex * GULI_WINGMAN_FLIGHT_COUNT) + FlightIndex,
						2u + static_cast<uint32>(CaptureIndex),
						1u + static_cast<uint32>(CaptureIndex),
						ClientTicks[CaptureIndex],
						ServerTimes[CaptureIndex],
						bHighRate ? EGuLiWingmanUploadRateClass::HighRate10Hz
							: EGuLiWingmanUploadRateClass::Cruise5Hz,
						bHighRate ? 2u : 1u);
					Work.ServerTimeSeconds = ServerTimes[CaptureIndex];
					Work.EnqueuedWallSeconds = QueueWallStart;
					OutResult.ApproxReplayPayloadBytes += sizeof(FQueuedRelayCandidate)
						+ Work.Candidate.Samples.GetAllocatedSize();
				}
			}
		}
		OutResult.SetupMilliseconds = (FPlatformTime::Seconds() - SetupStart) * 1000.0;
		OutResult.WingmanCount = GroupCount * GroupSize;
		OutResult.RelayCount = GroupCount;
		OutResult.CandidateCount = Queue.Num();
		OutResult.CandidatesPerServiceStep = CandidatesPerServiceStep;
		OutResult.MaximumQueueDepth = Queue.Num();
		const uint64 ResidentAfterSetup = FPlatformMemory::GetStats().UsedPhysical;
		OutResult.ProcessResidentDeltaBytes = ResidentAfterSetup >= ResidentBefore
			? static_cast<int64>(ResidentAfterSetup - ResidentBefore)
			: -static_cast<int64>(ResidentBefore - ResidentAfterSetup);

		const FGuLiCandidateWorldValidator PermitWorld = PermitReplayWorld();
		int32 QueueHead = 0;
		TArray<double> ServiceStepMilliseconds;
		const double MeasurementStart = FPlatformTime::Seconds();
		while (QueueHead < Queue.Num())
		{
			const int32 StepEnd = FMath::Min(QueueHead + CandidatesPerServiceStep, Queue.Num());
			OutResult.OldestQueueItemAgeMilliseconds = FMath::Max(
				OutResult.OldestQueueItemAgeMilliseconds,
				(FPlatformTime::Seconds() - Queue[QueueHead].EnqueuedWallSeconds) * 1000.0);
			const double StepStart = FPlatformTime::Seconds();
			for (; QueueHead < StepEnd; ++QueueHead)
			{
				const FQueuedRelayCandidate& Work = Queue[QueueHead];
				FRelayRuntime& Runtime = Relays[Work.RelayIndex];
				if (Work.Candidate.RequestedRateClass == EGuLiWingmanUploadRateClass::HighRate10Hz
					&& !Runtime.bHighRateGrantIssued)
				{
					FGuLiWingmanUploadRateGrant Grant;
					if (!Runtime.Relay->IssueHighRateGrant(
						127u,
						EGuLiWingmanUploadRateGrantReason::ServerObservedCombat,
						0.95,
						Grant)
						|| Grant.GrantRevision != 2u)
					{
						Test.AddError(FString::Printf(
							TEXT("Failed to issue strict high-rate grant: relay=%d"), Work.RelayIndex));
						return false;
					}
					Runtime.bHighRateGrantIssued = true;
				}
				const FGuLiWingmanSubmissionResult Result = Runtime.Relay->SubmitCandidate(
					Runtime.Owner,
					Work.Candidate,
					Work.ServerTimeSeconds,
					ReplayCarrier(Work.ServerTimeSeconds),
					PermitWorld);
				if (Result.Disposition != EGuLiWingmanSubmissionDisposition::Accepted)
				{
					Test.AddError(FString::Printf(
						TEXT("Replay Candidate rejected: relay=%d sequence=%u reason=%d"),
						Work.RelayIndex,
						Work.Candidate.CandidateSequence,
						static_cast<int32>(Result.RejectReason)));
					return false;
				}
				++OutResult.AcceptedCandidateCount;
				// Materialize the exact public-relay payload copy. No transform is generated here.
				const FGuLiWingmanAcceptedBatch Relayed = Result.AcceptedBatch;
				if (!Relayed.IsWellFormed() || Relayed.StableHash != Result.AcceptedBatch.StableHash)
				{
					Test.AddError(TEXT("Accepted Store produced an invalid relay payload"));
					return false;
				}
				OutResult.RelayedSampleCount += Relayed.Samples.Num();
			}
			ServiceStepMilliseconds.Add((FPlatformTime::Seconds() - StepStart) * 1000.0);
		}
		const double TotalSeconds = FPlatformTime::Seconds() - MeasurementStart;
		OutResult.TotalMeasuredMilliseconds = TotalSeconds * 1000.0;
		OutResult.ServiceStepCount = ServiceStepMilliseconds.Num();
		OutResult.ServiceStepTime = SummarizeMilliseconds(ServiceStepMilliseconds);
		OutResult.FinalQueueDepth = Queue.Num() - QueueHead;
		OutResult.ThroughputCandidatesPerSecond = TotalSeconds > UE_SMALL_NUMBER
			? static_cast<double>(OutResult.AcceptedCandidateCount) / TotalSeconds
			: 0.0;
		OutResult.ThroughputSamplesPerSecond = TotalSeconds > UE_SMALL_NUMBER
			? static_cast<double>(OutResult.RelayedSampleCount) / TotalSeconds
			: 0.0;
		for (const FRelayRuntime& Runtime : Relays)
		{
			OutResult.ServerMovementWriteCount += Runtime.Relay->GetServerWingmanMovementWriteCount();
		}
		return true;
	}

	FString ServerResultJson(
		const TCHAR* RunName,
		const FServerRunResult& Result,
		const bool bFormalGate,
		const double P95BudgetMilliseconds,
		const double ThroughputFloor)
	{
		return FString::Printf(
			TEXT("{\"schema\":\"guli.wingman.performance.v1\",\"run\":\"%s\",\"scope\":\"strict_flight_grant_atomic_relay_validate_store_payload_copy_only\",\"formal_gate\":%s,\"p95_budget_ms\":%.6f,\"max_budget_ms\":%.6f,\"throughput_floor_candidates_per_second\":%.3f,\"wingman_count\":%d,\"candidate_count\":%d,\"accepted_candidate_count\":%d,\"accepted_store_count\":%d,\"relay_count\":%d,\"relayed_sample_count\":%d,\"cruise_hz\":5,\"cruise_captures_per_group\":%d,\"combat_hz\":10,\"combat_captures_per_group\":%d,\"sample_count\":%d,\"candidates_per_service_step\":%d,\"quantile_method\":\"nearest-rank\",\"step_p50_ms\":%.6f,\"step_p95_ms\":%.6f,\"step_max_ms\":%.6f,\"setup_ms\":%.6f,\"total_measured_ms\":%.6f,\"throughput_candidates_per_second\":%.3f,\"throughput_samples_per_second\":%.3f,\"queue_max_depth\":%d,\"queue_capacity\":%d,\"queue_oldest_age_ms\":%.6f,\"queue_final_depth\":%d,\"drop_count\":%d,\"approx_replay_payload_bytes\":%llu,\"process_resident_delta_bytes_approx\":%lld,\"server_wingman_motion_step_count\":%llu}"),
			RunName,
			bFormalGate ? TEXT("true") : TEXT("false"),
			P95BudgetMilliseconds,
			H4000ServerMaximumBudgetMilliseconds,
			ThroughputFloor,
			Result.WingmanCount,
			Result.CandidateCount,
			Result.AcceptedCandidateCount,
			Result.AcceptedCandidateCount,
			Result.RelayCount,
			Result.RelayedSampleCount,
			Result.CruiseCaptureCountPerGroup,
			Result.CombatCaptureCountPerGroup,
			Result.ServiceStepCount,
			Result.CandidatesPerServiceStep,
			Result.ServiceStepTime.P50Milliseconds,
			Result.ServiceStepTime.P95Milliseconds,
			Result.ServiceStepTime.MaximumMilliseconds,
			Result.SetupMilliseconds,
			Result.TotalMeasuredMilliseconds,
			Result.ThroughputCandidatesPerSecond,
			Result.ThroughputSamplesPerSecond,
			Result.MaximumQueueDepth,
			Result.MaximumQueueDepth,
			Result.OldestQueueItemAgeMilliseconds,
			Result.FinalQueueDepth,
			Result.DropCount,
			Result.ApproxReplayPayloadBytes,
			Result.ProcessResidentDeltaBytes,
			Result.ServerMovementWriteCount);
	}

	bool IsFiniteDistribution(const FDistribution& Distribution)
	{
		return FMath::IsFinite(Distribution.P50Milliseconds)
			&& FMath::IsFinite(Distribution.P95Milliseconds)
			&& FMath::IsFinite(Distribution.MaximumMilliseconds)
			&& Distribution.P50Milliseconds >= 0.0
			&& Distribution.P95Milliseconds >= Distribution.P50Milliseconds
			&& Distribution.MaximumMilliseconds >= Distribution.P95Milliseconds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanH4000OwnerSimulationHarnessTest,
	"GuLiStrike.Wingman.Performance.H4000.OwnerSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanH4000OwnerSimulationHarnessTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPerformanceHarness;
	(void)Parameters;
	FOwnerRunResult Result;
	if (!RunOwnerHarness(
		*this, 160, H4000OwnerWarmupSteps, H4000OwnerMeasuredSteps, Result))
	{
		return false;
	}
	TestEqual(TEXT("H4000 creates exactly 4000 owner Mass entities"), Result.EntityCount, 4000);
	TestEqual(TEXT("H4000 creates no presentation actors"), Result.PresentationActorCount, 0);
	TestEqual(TEXT("H4000 measures every queued fixed step"),
		Result.SampleCount, H4000OwnerMeasuredSteps);
	TestEqual(TEXT("H4000 owner queue drains completely"), Result.FinalQueueDepth, 0);
	TestEqual(TEXT("H4000 owner harness drops no step"), Result.DropCount, 0);
	TestTrue(TEXT("H4000 executes the real fixed-wing Transform writer"), Result.bTransformAdvanced);
	TestTrue(TEXT("H4000 owner timing distribution is finite and ordered"), IsFiniteDistribution(Result.StepTime));
	TestTrue(TEXT("H4000 owner P95 stays within one 30 Hz fixed-step budget"),
		Result.StepTime.P95Milliseconds <= H4000OwnerP95BudgetMilliseconds);
	TestTrue(TEXT("H4000 owner throughput sustains 4000 entities at 30 Hz"),
		FMath::IsFinite(Result.ThroughputEntitiesPerSecond)
			&& Result.ThroughputEntitiesPerSecond >= H4000OwnerThroughputFloor);
	AddInfo(TEXT("GULI_PERF_METRICS ") + OwnerResultJson(
		TEXT("H4000-OwnerSimulation"), Result, true,
		H4000OwnerP95BudgetMilliseconds, H4000OwnerThroughputFloor));
	AddInfo(TEXT("Formal split-scale owner gate; this is not a complete 4000-Wingman network match."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanH4000ServerValidatorHarnessTest,
	"GuLiStrike.Wingman.Performance.H4000.ServerValidator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanH4000ServerValidatorHarnessTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPerformanceHarness;
	(void)Parameters;
	FServerRunResult Result;
	if (!RunServerHarness(*this, 160, 128, Result))
	{
		return false;
	}
	TestEqual(TEXT("Validator replay represents 4000 Wingmen"), Result.WingmanCount, 4000);
	TestEqual(TEXT("Every recorded-shape Candidate is accepted"),
		Result.AcceptedCandidateCount, Result.CandidateCount);
	TestEqual(TEXT("Every accepted per-Flight Candidate relays all 5 samples"),
		Result.RelayedSampleCount, Result.CandidateCount * GULI_WINGMAN_MEMBERS_PER_FLIGHT);
	TestEqual(TEXT("Server validator queue drains completely"), Result.FinalQueueDepth, 0);
	TestEqual(TEXT("Server validator harness drops no packet"), Result.DropCount, 0);
	TestEqual(TEXT("Validator/Store/Relay performs zero server movement writes"),
		Result.ServerMovementWriteCount, static_cast<uint64>(0u));
	TestTrue(TEXT("H4000 server timing distribution is finite and ordered"),
		IsFiniteDistribution(Result.ServiceStepTime));
	TestTrue(TEXT("H4000 server records enough nearest-rank service samples"),
		Result.ServiceStepCount >= H4000ServerMinimumServiceSamples);
	TestTrue(TEXT("H4000 server Validate/Store/Relay P95 stays within budget"),
		Result.ServiceStepTime.P95Milliseconds <= H4000ServerP95BudgetMilliseconds);
	TestTrue(TEXT("H4000 server Validate/Store/Relay maximum stays within budget"),
		Result.ServiceStepTime.MaximumMilliseconds <= H4000ServerMaximumBudgetMilliseconds);
	TestTrue(TEXT("H4000 server throughput sustains the 10 Hz combat capture ceiling"),
		FMath::IsFinite(Result.ThroughputCandidatesPerSecond)
			&& Result.ThroughputCandidatesPerSecond >= H4000ServerThroughputFloor);
	AddInfo(TEXT("GULI_PERF_METRICS ") + ServerResultJson(
		TEXT("H4000-ServerValidator"), Result, true,
		H4000ServerP95BudgetMilliseconds, H4000ServerThroughputFloor));
	AddInfo(TEXT("This replays protocol Candidates through Validate/Store/payload-copy only; it is not a 4000-unit server simulation."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanFormalScale100QuickSmokeTest,
	"GuLiStrike.Wingman.Performance.FormalScale100.QuickSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanFormalScale100QuickSmokeTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPerformanceHarness;
	(void)Parameters;
	FOwnerRunResult Owner;
	FServerRunResult Server;
	if (!RunOwnerHarness(*this, 4, 2, 30, Owner) || !RunServerHarness(*this, 4, 4, Server))
	{
		return false;
	}
	TestEqual(TEXT("Quick smoke creates the formal Wingman count on the owner"), Owner.EntityCount, 100);
	TestEqual(TEXT("Quick smoke represents the formal Wingman count on the server"), Server.WingmanCount, 100);
	TestEqual(TEXT("Quick smoke owner queue drains"), Owner.FinalQueueDepth, 0);
	TestEqual(TEXT("Quick smoke validator queue drains"), Server.FinalQueueDepth, 0);
	TestEqual(TEXT("Quick smoke still produces zero server movement writes"),
		Server.ServerMovementWriteCount, static_cast<uint64>(0u));
	TestTrue(TEXT("Quick smoke owner timings are finite"), IsFiniteDistribution(Owner.StepTime));
	TestTrue(TEXT("Quick smoke server timings are finite"), IsFiniteDistribution(Server.ServiceStepTime));
	AddInfo(TEXT("GULI_PERF_METRICS ") + OwnerResultJson(
		TEXT("S9-100-QuickSmoke-Owner"), Owner, false, 0.0, 0.0));
	AddInfo(TEXT("GULI_PERF_METRICS ") + ServerResultJson(
		TEXT("S9-100-QuickSmoke-Server"), Server, false, 0.0, 0.0));
	AddInfo(TEXT("QuickSmoke explicitly excludes 10-minute duration, real networking, 500 Commander control comparison, bandwidth, and Network Insights."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanDedicatedExecutionDomainStaticGatesTest,
	"GuLiStrike.Wingman.Performance.DedicatedExecutionDomain.StaticGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanDedicatedExecutionDomainStaticGatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const uint64 StateTreeDomainCount = AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(NM_DedicatedServer) ? 1u : 0u;
	const uint64 AStarDomainCount = UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(NM_DedicatedServer) ? 1u : 0u;
	const uint64 ModeDomainCount = GetDefault<UGuLiWingmanModeProcessor>()->ShouldExecute(
		EProcessorExecutionFlags::Server) ? 1u : 0u;
	const uint64 GuidanceDomainCount = GetDefault<UGuLiWingmanFormationGuidanceProcessor>()->ShouldExecute(
		EProcessorExecutionFlags::Server) ? 1u : 0u;
	const uint64 SteeringDomainCount = GetDefault<UGuLiWingmanAvoidanceProcessor>()->ShouldExecute(
		EProcessorExecutionFlags::Server) ? 1u : 0u;
	const uint64 IntegrationDomainCount = GetDefault<UGuLiWingmanFlightIntegrationProcessor>()->ShouldExecute(
		EProcessorExecutionFlags::Server) ? 1u : 0u;
	const uint64 GeneratedTransformWriterDomainCount = IntegrationDomainCount;
	TestEqual(TEXT("Dedicated Server cannot create the Wingman owner/A* simulation domain"),
		AStarDomainCount, static_cast<uint64>(0u));
	TestEqual(TEXT("Dedicated Server cannot run the Wingman StateTree/fallback runner"),
		StateTreeDomainCount, static_cast<uint64>(0u));
	TestEqual(TEXT("Dedicated Server cannot dispatch Wingman mode policy"),
		ModeDomainCount, static_cast<uint64>(0u));
	TestEqual(TEXT("Dedicated Server cannot dispatch formation/path Guidance"),
		GuidanceDomainCount, static_cast<uint64>(0u));
	TestEqual(TEXT("Dedicated Server cannot dispatch Wingman steering/avoidance"),
		SteeringDomainCount, static_cast<uint64>(0u));
	TestEqual(TEXT("Dedicated Server cannot dispatch fixed-wing Integration"),
		IntegrationDomainCount, static_cast<uint64>(0u));
	TestEqual(TEXT("Dedicated Server exposes no generated-Transform writer domain"),
		GeneratedTransformWriterDomainCount, static_cast<uint64>(0u));
	AddInfo(FString::Printf(
		TEXT("GULI_PERF_METRICS {\"schema\":\"guli.wingman.execution-domain.v1\",\"run\":\"Dedicated-Static-Gates\",\"formal_dedicated_process_gate\":false,\"server_wingman_state_tree_domain_count\":%llu,\"server_wingman_astar_domain_count\":%llu,\"server_wingman_mode_processor_domain_count\":%llu,\"server_wingman_guidance_processor_domain_count\":%llu,\"server_wingman_steering_processor_domain_count\":%llu,\"server_wingman_integration_processor_domain_count\":%llu,\"server_generated_transform_writer_domain_count\":%llu}"),
		StateTreeDomainCount,
		AStarDomainCount,
		ModeDomainCount,
		GuidanceDomainCount,
		SteeringDomainCount,
		IntegrationDomainCount,
		GeneratedTransformWriterDomainCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanDedicatedExecutionDomainInMemoryCountersTest,
	"GuLiStrike.Wingman.Performance.DedicatedExecutionDomain.InMemoryZeroCounters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanDedicatedExecutionDomainInMemoryCountersTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPerformanceHarness;
	(void)Parameters;
	struct FDedicatedCounters
	{
		uint64 StateTreeTicks = 0u;
		uint64 AStarRequests = 0u;
		uint64 SteeringSteps = 0u;
		uint64 IntegrationSteps = 0u;
		uint64 GeneratedTransforms = 0u;
		uint64 RelayMovementWrites = 0u;
	} Counters;

	// Exercise the same execution-domain decisions used by the runtime dispatcher,
	// retaining separate counters rather than collapsing them into one total.
	if (AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(NM_DedicatedServer))
	{
		++Counters.StateTreeTicks;
	}
	if (UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(NM_DedicatedServer))
	{
		++Counters.AStarRequests;
	}
	if (GetDefault<UGuLiWingmanAvoidanceProcessor>()->ShouldExecute(EProcessorExecutionFlags::Server))
	{
		++Counters.SteeringSteps;
	}
	if (GetDefault<UGuLiWingmanFlightIntegrationProcessor>()->ShouldExecute(EProcessorExecutionFlags::Server))
	{
		++Counters.IntegrationSteps;
	}

	FRelayRuntime Runtime;
	if (!ActivateLegacyRelay(Runtime, MakeGroup(0, 0x0a330010u), 0))
	{
		AddError(TEXT("Could not activate the in-memory Dedicated relay fixture"));
		return false;
	}
	const FGuLiWingmanCandidateBatch Candidate = MakeLegacyReplayCandidate(Runtime, 1u, 100u);
	const FGuLiCarrierSourceResolver Carrier = [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& OutState)
	{
		OutState.Transform = FTransform::Identity;
		OutState.Velocity = FVector(5000.0, 0.0, 0.0);
		OutState.ServerWorldTimeSeconds = 0.1;
		return EGuLiRelayCarrierLookupResult::Found;
	};
	const FGuLiCandidateWorldValidator PermitWorld = [](const FGuLiWingmanCandidateWorldValidationContext& Context)
	{
		return Context.IsWellFormed()
			? EGuLiWingmanRejectReason::None
			: EGuLiWingmanRejectReason::InvalidIdentity;
	};
	const FGuLiWingmanSubmissionResult Accepted = Runtime.Relay->SubmitCandidate(
		Runtime.Owner, Candidate, 0.1, Carrier, PermitWorld);
	if (!TestTrue(TEXT("In-memory Dedicated relay accepts the validated Candidate"),
		Accepted.Disposition == EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	Counters.RelayMovementWrites = Runtime.Relay->GetServerWingmanMovementWriteCount();
	if (Candidate.Samples.Num() != Accepted.AcceptedBatch.Samples.Num())
	{
		Counters.GeneratedTransforms += static_cast<uint64>(
			FMath::Abs(Candidate.Samples.Num() - Accepted.AcceptedBatch.Samples.Num()));
	}
	for (int32 Index = 0; Index < Candidate.Samples.Num() && Index < Accepted.AcceptedBatch.Samples.Num(); ++Index)
	{
		const FGuLiWingmanCandidateSample& Source = Candidate.Samples[Index];
		const FGuLiWingmanCandidateSample& Relayed = Accepted.AcceptedBatch.Samples[Index];
		if (Source.Wingman != Relayed.Wingman
			|| Source.PositionCentimeters != Relayed.PositionCentimeters
			|| Source.VelocityCentimetersPerSecond != Relayed.VelocityCentimetersPerSecond
			|| Source.RotationCentiDegrees != Relayed.RotationCentiDegrees)
		{
			++Counters.GeneratedTransforms;
		}
	}

	TestEqual(TEXT("server_wingman_state_tree_tick_count"), Counters.StateTreeTicks, static_cast<uint64>(0u));
	TestEqual(TEXT("server_wingman_path_request_count"), Counters.AStarRequests, static_cast<uint64>(0u));
	TestEqual(TEXT("server_wingman_steering_step_count"), Counters.SteeringSteps, static_cast<uint64>(0u));
	TestEqual(TEXT("server_wingman_flight_integration_step_count"), Counters.IntegrationSteps, static_cast<uint64>(0u));
	TestEqual(TEXT("server_generated_runtime_transform_count"), Counters.GeneratedTransforms, static_cast<uint64>(0u));
	TestEqual(TEXT("server_wingman_motion_step_count"), Counters.RelayMovementWrites, static_cast<uint64>(0u));
	AddInfo(FString::Printf(
		TEXT("GULI_PERF_METRICS {\"schema\":\"guli.wingman.execution-domain.v1\",\"run\":\"Dedicated-InMemory-ZeroCounters\",\"formal_dedicated_process_gate\":false,\"server_wingman_state_tree_tick_count\":%llu,\"server_wingman_astar_request_count\":%llu,\"server_wingman_path_request_count\":%llu,\"server_wingman_steering_step_count\":%llu,\"server_wingman_flight_integration_step_count\":%llu,\"server_generated_runtime_transform_count\":%llu,\"server_wingman_motion_step_count\":%llu}"),
		Counters.StateTreeTicks,
		Counters.AStarRequests,
		Counters.AStarRequests,
		Counters.SteeringSteps,
		Counters.IntegrationSteps,
		Counters.GeneratedTransforms,
		Counters.RelayMovementWrites));
	AddInfo(TEXT("These are static/in-memory dispatch counters; a source-engine Dedicated executable run remains a separate hard gate."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPresentationMultiFlightTimelineTest,
	"GuLiStrike.Wingman.Presentation.MultiFlightTimelines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPresentationMultiFlightTimelineTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPerformanceHarness;
	(void)Parameters;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AGuLiWingmanPresentationActor* Presentation =
		Fixture.World->SpawnActor<AGuLiWingmanPresentationActor>();
	if (!TestNotNull(TEXT("Multi-Flight presentation fixture actor exists"), Presentation))
	{
		return false;
	}

	FRelayRuntime OwnerRuntime;
	const FGuLiWingmanGroupHandle OwnerGroup = MakeGroup(0, 0x0a440010u);
	FGuLiWingmanBootstrapBundle OwnerBootstrap;
	if (!TestTrue(TEXT("Owner strict Relay initializes"),
		InitializeRelayCore(OwnerRuntime, OwnerGroup, 0, true))
		|| !TestTrue(TEXT("Owner Bootstrap builds"),
			OwnerRuntime.Relay->BuildBootstrap(OwnerBootstrap))
		|| !TestTrue(TEXT("Public Bootstrap can arrive first as a remote Cut"),
			Presentation->ApplyBootstrap(OwnerBootstrap, false, 0.0))
		|| !TestTrue(TEXT("The targeted copy promotes the same Cut to Owner"),
			Presentation->ApplyBootstrap(OwnerBootstrap, true, 0.0)))
	{
		return false;
	}
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		const FGuLiWingmanCandidateBatch Candidate = MakeStrictFlightCandidate(
			OwnerRuntime,
			FlightIndex,
			1u,
			1u,
			0u,
			100u,
			0.05,
			EGuLiWingmanUploadRateClass::Cruise5Hz,
			OwnerRuntime.Relay->GetUploadRateGrant().GrantRevision);
		if (!TestTrue(
			*FString::Printf(TEXT("Owner Flight %u accepts its independent sequence 1"), FlightIndex),
			Presentation->ApplyOwnerFrame(Candidate, 0.05)))
		{
			return false;
		}
	}

	FRelayRuntime RemoteRuntime;
	const FGuLiWingmanGroupHandle RemoteGroup = MakeGroup(1, 0x0a440010u);
	if (!TestTrue(TEXT("Remote strict Relay reaches Active with five Accepted Flights"),
		ActivateStrictRelay(RemoteRuntime, RemoteGroup, 1)))
	{
		return false;
	}
	FGuLiWingmanBootstrapBundle RetainedBootstrap;
	if (!TestTrue(TEXT("Active Relay returns its exact acknowledged Bootstrap"),
		RemoteRuntime.Relay->BuildBootstrap(RetainedBootstrap))
		|| !TestEqual(TEXT("The acknowledged Cut remains unchanged after activation"),
			RetainedBootstrap.AcceptedSnapshot.Num(),
			0))
	{
		return false;
	}
	if (!TestTrue(TEXT("Remote presentation applies the retained six-scope Cut"),
		Presentation->ApplyBootstrap(RetainedBootstrap, false, 0.10)))
	{
		return false;
	}
	const TArray<FGuLiWingmanAcceptedBatch>& AcceptedFlights =
		RemoteRuntime.Relay->GetAcceptedHistory();
	if (!TestEqual(TEXT("Atomic activation committed all five Flight snapshots"),
		AcceptedFlights.Num(), static_cast<int32>(GULI_WINGMAN_FLIGHT_COUNT)))
	{
		return false;
	}
	for (const FGuLiWingmanAcceptedBatch& Accepted : AcceptedFlights)
	{
		if (!TestEqual(TEXT("Atomic Flight snapshots retain the same server timestamp"),
			Accepted.ServerAcceptedTimeSeconds, 0.05)
			|| !TestTrue(
				*FString::Printf(TEXT("Remote Flight %u applies its independent timeline"),
					Accepted.FlightIndex),
				Presentation->ApplyAcceptedSnapshot(Accepted, 0.10)))
		{
			return false;
		}
	}
	TArray<FGuLiWingmanAcceptedTargetPose> Poses;
	Presentation->GetFreshAcceptedTargetPoses(0.05, 1.0, Poses);
	TestEqual(TEXT("All 25 Accepted poses are available after the five-Flight commit"),
		Poses.Num(), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));
	TestTrue(TEXT("The complete retained Cut is visible to observer QA"),
		Presentation->HasAppliedBootstrap(
			RemoteGroup, RetainedBootstrap.Commit.CutId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPresentationAppliesDeathCutWithRetainedPoseHistoryTest,
	"GuLiStrike.Wingman.Presentation.AppliesDeathCutWithRetainedPoseHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPresentationAppliesDeathCutWithRetainedPoseHistoryTest::RunTest(
	const FString& Parameters)
{
	using namespace GuLiWingmanPerformanceHarness;
	(void)Parameters;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FRelayRuntime Runtime;
	const FGuLiWingmanGroupHandle Group = MakeGroup(0, 0x0a440002u);
	if (!TestTrue(TEXT("Death-Cut presentation fixture relay initializes"),
		InitializeRelayCore(Runtime, Group, 0, false)))
	{
		return false;
	}
	FGuLiWingmanBootstrapBundle Bootstrap;
	if (!TestTrue(TEXT("Death-Cut fixture builds its initial six-scope Bootstrap"),
		Runtime.Relay->BuildBootstrap(Bootstrap)))
	{
		return false;
	}

	AGuLiWingmanPresentationActor* Presentation =
		Fixture.World->SpawnActor<AGuLiWingmanPresentationActor>();
	if (!TestNotNull(TEXT("Death-Cut presentation fixture actor exists"), Presentation)
		|| !TestTrue(TEXT("Presentation accepts the initial production Bootstrap"),
			Presentation->ApplyBootstrap(Bootstrap, false, 0.0))
		|| !TestTrue(TEXT("Death-Cut fixture activates the Relay"),
			AcknowledgeRelayBootstrap(Runtime, Bootstrap, 0.01)))
	{
		return false;
	}

	const FGuLiWingmanSubmissionResult Accepted = Runtime.Relay->SubmitCandidate(
		Runtime.Owner, MakeLegacyReplayCandidate(Runtime, 1u, 100u), 0.1,
		ReplayCarrier(0.1), PermitReplayWorld());
	if (!TestEqual(TEXT("The pre-death production Candidate is Accepted"),
		Accepted.Disposition, EGuLiWingmanSubmissionDisposition::Accepted)
		|| !TestTrue(TEXT("The pre-death pose enters presentation"),
			Presentation->ApplyAcceptedSnapshot(Accepted.AcceptedBatch, 0.1)))
	{
		return false;
	}
	const FGuLiWingmanHandle DeadWingman = Accepted.AcceptedBatch.Samples[0].Wingman;
	FTransform PreDeathTransform;
	if (!TestTrue(TEXT("The selected Wingman has a pre-death presented pose"),
		Presentation->TryGetPresentedTransform(DeadWingman, PreDeathTransform))
		|| !TestTrue(TEXT("The production Relay marks the Wingman dead"),
			Runtime.Relay->MarkWingmanDead(DeadWingman)))
	{
		return false;
	}

	FGuLiWingmanBootstrapBundle DeathBootstrap;
	if (!TestTrue(TEXT("The production Relay emits an Active roster/death Cut"),
		Runtime.Relay->RefreshActiveRosterCut(0.2, DeathBootstrap))
		|| !TestTrue(TEXT("The death Cut retains the authoritative Accepted high-water"),
			DeathBootstrap.AcceptedSnapshot.ContainsByPredicate(
				[&DeadWingman](const FGuLiWingmanAcceptedBatch& Batch)
				{
					return Batch.FindSample(DeadWingman) != nullptr;
				}))
		|| !TestTrue(TEXT("Presentation atomically applies a death Cut with retained history"),
			Presentation->ApplyBootstrap(DeathBootstrap, false, 0.2))
		|| !TestTrue(TEXT("Observer QA sees the exact death Cut as applied"),
			Presentation->HasAppliedBootstrap(Group, DeathBootstrap.Commit.CutId)))
	{
		return false;
	}

	FTransform DeadTransform;
	TestFalse(TEXT("The retained pre-death pose cannot resurrect the dead Wingman"),
		Presentation->TryGetPresentedTransform(DeadWingman, DeadTransform));
	TestFalse(TEXT("The dead Wingman remains non-interactable after the Cut"),
		Presentation->IsWingmanInteractable(DeadWingman));
	TArray<FGuLiWingmanAcceptedTargetPose> TargetPoses;
	Presentation->GetFreshAcceptedTargetPoses(0.2, 1.0, TargetPoses);
	TestFalse(TEXT("Target acquisition excludes the dead member from retained history"),
		TargetPoses.ContainsByPredicate([&DeadWingman](const FGuLiWingmanAcceptedTargetPose& Pose)
		{
			return Pose.Wingman == DeadWingman;
		}));
	TestEqual(TEXT("All other retained Accepted poses survive the atomic death Cut"),
		TargetPoses.Num(), GULI_WINGMAN_GROUP_SIZE - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPresentationRejectsPostDeathAcceptedPoseTest,
	"GuLiStrike.Wingman.Presentation.RejectsPostDeathAcceptedPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPresentationRejectsPostDeathAcceptedPoseTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPerformanceHarness;
	(void)Parameters;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FRelayRuntime Runtime;
	const FGuLiWingmanGroupHandle Group = MakeGroup(0, 0x0a440001u);
	if (!TestTrue(TEXT("Presentation fixture relay initializes"),
		InitializeRelayCore(Runtime, Group, 0, false)))
	{
		return false;
	}
	FGuLiWingmanBootstrapBundle Bootstrap;
	if (!TestTrue(TEXT("Presentation fixture builds its six-scope Bootstrap"),
		Runtime.Relay->BuildBootstrap(Bootstrap)))
	{
		return false;
	}

	AGuLiWingmanPresentationActor* Presentation =
		Fixture.World->SpawnActor<AGuLiWingmanPresentationActor>();
	if (!TestNotNull(TEXT("Presentation fixture actor exists"), Presentation)
		|| !TestTrue(TEXT("Remote presentation accepts the production Bootstrap"),
			Presentation->ApplyBootstrap(Bootstrap, false, 0.0))
		|| !TestTrue(TEXT("Presentation fixture activates the Relay"),
			AcknowledgeRelayBootstrap(Runtime, Bootstrap, 0.01)))
	{
		return false;
	}

	const FGuLiCarrierSourceResolver Carrier = ReplayCarrier(0.1);
	const FGuLiCandidateWorldValidator WorldValidator = PermitReplayWorld();
	const FGuLiWingmanSubmissionResult First = Runtime.Relay->SubmitCandidate(
		Runtime.Owner, MakeLegacyReplayCandidate(Runtime, 1u, 100u), 0.1,
		Carrier, WorldValidator);
	if (!TestEqual(TEXT("The first production Candidate is Accepted"), First.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted)
		|| !TestTrue(TEXT("The first Accepted pose enters presentation"),
			Presentation->ApplyAcceptedSnapshot(First.AcceptedBatch, 0.1)))
	{
		return false;
	}
	const FGuLiWingmanHandle DeadWingman = First.AcceptedBatch.Samples[0].Wingman;
	TestTrue(TEXT("Reliable death state hides the Wingman"),
		Presentation->SetWingmanAlive(DeadWingman, false));

	const FGuLiWingmanSubmissionResult Late = Runtime.Relay->SubmitCandidate(
		Runtime.Owner, MakeLegacyReplayCandidate(Runtime, 2u, 101u), 0.2,
		ReplayCarrier(0.2), WorldValidator);
	if (!TestEqual(TEXT("The old in-flight Candidate remains a valid server Accepted payload"),
		Late.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	TestFalse(TEXT("A late Accepted batch containing the dead entity is discarded"),
		Presentation->ApplyAcceptedSnapshot(Late.AcceptedBatch, 0.2));
	FTransform PresentedTransform;
	TestFalse(TEXT("The dead entity cannot regain a presented transform"),
		Presentation->TryGetPresentedTransform(DeadWingman, PresentedTransform));
	TestFalse(TEXT("The dead entity remains non-interactable"),
		Presentation->IsWingmanInteractable(DeadWingman));
	TArray<FGuLiWingmanAcceptedTargetPose> TargetPoses;
	Presentation->GetFreshAcceptedTargetPoses(0.2, 1.0, TargetPoses);
	TestFalse(TEXT("Target acquisition receives no post-death pose"),
		TargetPoses.ContainsByPredicate([&DeadWingman](const FGuLiWingmanAcceptedTargetPose& Pose)
		{
			return Pose.Wingman == DeadWingman;
		}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
