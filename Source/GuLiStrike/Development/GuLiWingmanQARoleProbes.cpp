// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanQARoleProbes.h"

#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiShipProjectileLedgerBridge.h"
#include "Battle/Combat/GuLiWingmanReplenishmentController.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationPolicy.h"

namespace GuLiWingmanQARoleProbePrivate
{
	constexpr uint32 MatchEpoch = 0x51415250u;

	void SetError(FGuLiWingmanQARoleProbeResult& Out, const TCHAR* Message)
	{
		if (Out.Error.IsEmpty())
		{
			Out.Error = Message;
		}
	}

	void AddEvent(
		FGuLiWingmanQARoleProbeResult& Out,
		const EGuLiWingmanQALogStream Stream,
		const FName EventName,
		const FString& Message,
		const FString& RejectReason = FString())
	{
		FGuLiWingmanQAEvent& Event = Out.Events.AddDefaulted_GetRef();
		Event.Stream = Stream;
		Event.Event = EventName;
		Event.Fields.Add(TEXT("message"), Message);
		if (!RejectReason.IsEmpty())
		{
			Event.Fields.Add(TEXT("reject_reason"), RejectReason);
		}
	}

	void Pass(
		FGuLiWingmanQARoleProbeResult& Out,
		const FName Gate,
		const EGuLiWingmanQALogStream Stream,
		const TCHAR* Message)
	{
		Out.PassedGateIds.Add(Gate);
		AddEvent(Out, Stream, TEXT("QA_CHECK"),
			FString::Printf(TEXT("role_probe gate=%s %s"), *Gate.ToString(), Message));
	}

	FGuLiWingmanGroupHandle MakeGroup(const uint32 Seed, const uint32 GroupGeneration = 3u)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x51500000u + Seed, 0x51510000u, 0x51520000u, 0x51530000u);
		Group.ShipGeneration = 2u;
		Group.GroupGeneration = GroupGeneration;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 4u;
		Config.SnapshotRevision = 5u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 6u;
		Config.FormationDefinitionChecksum = 0x1111222233334444ull;
		Config.BasicWeaponDefinitionRevision = 7u;
		Config.BasicWeaponDefinitionChecksum = 0x2222333344445555ull;
		Config.MissileDefinitionRevision = 8u;
		Config.MissileDefinitionChecksum = 0x3333444455556666ull;
		Config.FormationCommandRevision = 9u;
		Config.EffectiveClientSimTick = 100u;
		Config.RefreshHash();
		return Config;
	}

	FGuLiWingmanRelayValidationRevisions MakeRevisions()
	{
		FGuLiWingmanRelayValidationRevisions Revisions;
		Revisions.NavSchemaRevision = 2u;
		Revisions.NavDataChecksum = 0xabcdef0123456789ull;
		Revisions.TuningRevision = 3u;
		Revisions.ObstacleRevision = 4u;
		return Revisions;
	}

	FGuLiCarrierSourceResolver CarrierAt(const double ServerSeconds)
	{
		return [ServerSeconds](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& Out)
		{
			Out.Transform = FTransform::Identity;
			Out.Velocity = FVector(100.0, 0.0, 0.0);
			Out.ServerWorldTimeSeconds = ServerSeconds;
			return EGuLiRelayCarrierLookupResult::Found;
		};
	}

	FGuLiCandidateWorldValidator PermitWorld()
	{
		return [](const FGuLiWingmanCandidateWorldValidationContext& Context)
		{
			return Context.IsWellFormed()
				? EGuLiWingmanRejectReason::None
				: EGuLiWingmanRejectReason::InvalidIdentity;
		};
	}

	FGuLiWingmanCandidateBatch MakeFlight(
		const FGuLiWingmanRelayServer& Relay,
		const uint8 FlightIndex,
		const uint32 CandidateSequence,
		const uint32 FrameSequence,
		const uint32 ClientTick,
		const double CaptureTime)
	{
		FGuLiWingmanCandidateBatch Candidate;
		Candidate.MatchEpoch = Relay.GetMatchEpoch();
		Candidate.ConnectionGeneration = Relay.GetConnectionGeneration();
		Candidate.Group = Relay.GetLeaseState().Group;
		Candidate.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Candidate.RosterRevision = Relay.GetRosterRevision();
		Candidate.FlightIndex = FlightIndex;
		Candidate.RequestedRateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;
		Candidate.ObservedGrantRevision = Relay.GetUploadRateGrant().GrantRevision;
		Candidate.CandidateSequence = CandidateSequence;
		Candidate.FrameSequence = FrameSequence;
		Candidate.BaseAcceptedSequence = Relay.GetAcceptedSequenceForFlight(FlightIndex);
		Candidate.ClientSimTick = ClientTick;
		Candidate.CaptureEstimatedServerTimeSeconds = CaptureTime;
		Candidate.NavSchemaRevision = Relay.GetValidationRevisions().NavSchemaRevision;
		Candidate.NavDataChecksum = Relay.GetValidationRevisions().NavDataChecksum;
		Candidate.TuningRevision = Relay.GetValidationRevisions().TuningRevision;
		Candidate.ObstacleRevision = Relay.GetValidationRevisions().ObstacleRevision;
		Candidate.CarrierSource.CanonicalEpoch = 1u;
		Candidate.CarrierSource.MoveRevision = CandidateSequence;
		Candidate.AbilitySetRevision = Relay.GetAbilityConfig().AbilitySetRevision;
		Candidate.FormationCommandRevision = Relay.GetAbilityConfig().FormationCommandRevision;
		Candidate.FormationDefinitionChecksum = Relay.GetAbilityConfig().FormationDefinitionChecksum;
		for (const FGuLiWingmanRosterEntry& Entry : Relay.GetRoster())
		{
			if (Entry.bDead || Entry.Wingman.Flight.FlightIndex != FlightIndex)
			{
				continue;
			}
			const FGuLiWingmanHealthEntry* Health = Relay.GetHealth().FindByPredicate(
				[&Entry](const FGuLiWingmanHealthEntry& Value)
				{
					return Value.Wingman == Entry.Wingman;
				});
			if (!Health || Health->CurrentHealthPermille == 0u)
			{
				continue;
			}
			FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
			Sample.Wingman = Entry.Wingman;
			Sample.PositionCentimeters = FIntVector(
				static_cast<int32>(FlightIndex) * 1000,
				static_cast<int32>(Entry.Wingman.MemberIndex) * 100,
				1000);
			Sample.VelocityCentimetersPerSecond = FIntVector(100, 0, 0);
			Sample.RotationCentiDegrees = FIntVector::ZeroValue;
			Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Follow);
			Candidate.RequiredMemberMask |= static_cast<uint8>(1u << Entry.Wingman.MemberIndex);
		}
		return Candidate;
	}

	TArray<FGuLiWingmanCandidateBatch> MakeAllFlights(
		const FGuLiWingmanRelayServer& Relay,
		const uint32 FirstSequence,
		const uint32 FrameSequence,
		const uint32 ClientTick,
		const double CaptureTime)
	{
		TArray<FGuLiWingmanCandidateBatch> Result;
		for (uint8 Flight = 0u; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
		{
			Result.Add(MakeFlight(
				Relay, Flight, FirstSequence + Flight, FrameSequence, ClientTick, CaptureTime));
		}
		return Result;
	}

	FGuLiWingmanAtomicCandidateBatchFragment MakeAtomicFragment(
		const FGuLiWingmanRelayServer& Relay,
		const FGuLiWingmanBootstrapBundle& Frozen,
		const TArray<FGuLiWingmanCandidateBatch>& AllFlights,
		const TArray<FGuLiWingmanCandidateBatch>& FragmentFlights,
		const uint8 FragmentIndex,
		const uint8 FragmentCount,
		const uint64 BatchId)
	{
		FGuLiWingmanAtomicCandidateBatchFragment Fragment;
		Fragment.Header.BatchId = BatchId;
		Fragment.Header.BatchKind = Frozen.AtomicBatchKind;
		Fragment.Header.Group = Relay.GetLeaseState().Group;
		Fragment.Header.ConnectionGeneration = Relay.GetConnectionGeneration();
		Fragment.Header.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Fragment.Header.FrozenRosterRevision = Frozen.RosterRevision;
		Fragment.Header.FrozenRequiredFlightMask = Frozen.RequiredFlightMask;
		Fragment.Header.FrozenRequiredMemberMaskHash = Frozen.RequiredMemberMaskHash;
		Fragment.Header.BaselineRevision = Frozen.AtomicBaselineRevision;
		Fragment.Header.BaselineHash = Frozen.AtomicBaselineHash;
		Fragment.Header.IncludedFlightMask = Frozen.RequiredFlightMask;
		Fragment.Header.ClientBatchStartTick = AllFlights.IsEmpty()
			? 0u : AllFlights[0].ClientSimTick;
		Fragment.Header.FragmentCount = FragmentCount;
		Fragment.Header.BatchPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(AllFlights);
		Fragment.Flights = FragmentFlights;
		Fragment.FragmentIndex = FragmentIndex;
		uint32 TotalBytes = 0u;
		for (const FGuLiWingmanCandidateBatch& Flight : AllFlights)
		{
			FGuLiWingmanAtomicCandidateBatchFragment Single;
			Single.Flights.Add(Flight);
			TotalBytes += Single.EstimatePayloadBytes();
		}
		Fragment.Header.BatchPayloadBytes = TotalBytes;
		return Fragment;
	}

	bool AcknowledgeCut(
		FGuLiWingmanRelayServer& Relay,
		const FGuid& Owner,
		const FGuLiWingmanBootstrapBundle& Bootstrap,
		const double NowSeconds)
	{
		FGuLiGroupAbilityConfigAck AbilityAck;
		AbilityAck.Group = Bootstrap.Commit.Group;
		AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		AbilityAck.SnapshotRevision = Bootstrap.AbilityConfig.SnapshotRevision;
		AbilityAck.SnapshotHash = Bootstrap.AbilityConfig.SnapshotHash;
		const FGuLiWingmanTransferBaseline* Transfer = Bootstrap.bHasTransferBaseline
			? &Bootstrap.TransferBaseline : nullptr;
		return Relay.AcknowledgeAbilityConfig(Owner, AbilityAck, NowSeconds)
			&& Relay.AcknowledgeBootstrap(Owner, Bootstrap.Commit, Transfer, NowSeconds + 0.001);
	}

	FGuLiWingmanAtomicBatchAcceptance SubmitWholeAtomic(
		FGuLiWingmanRelayServer& Relay,
		const FGuid& Owner,
		const FGuLiWingmanBootstrapBundle& Bootstrap,
		const TArray<FGuLiWingmanCandidateBatch>& Flights,
		const uint64 BatchId,
		const double NowSeconds)
	{
		return Relay.SubmitAtomicCandidateFragment(
			Owner,
			MakeAtomicFragment(Relay, Bootstrap, Flights, Flights, 0u, 1u, BatchId),
			NowSeconds,
			CarrierAt(NowSeconds),
			PermitWorld());
	}

	bool Activate(
		FGuLiWingmanRelayServer& Relay,
		const FGuid& Owner,
		FGuLiWingmanBootstrapBundle& InOutBootstrap)
	{
		const TArray<FGuLiWingmanCandidateBatch> Flights =
			MakeAllFlights(Relay, 1u, 1u, 100u, 0.1);
		return SubmitWholeAtomic(Relay, Owner, InOutBootstrap, Flights, 1u, 0.1).Disposition
				== EGuLiWingmanSubmissionDisposition::Accepted
			&& AcknowledgeCut(Relay, Owner, InOutBootstrap, 0.11)
			&& Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active;
	}

	struct FRelayFixture
	{
		FGuLiWingmanRelayServer Relay;
		FGuid Owner = FGuid(1u, 2u, 3u, 4u);
		FGuid Backup = FGuid(5u, 6u, 7u, 8u);
		FGuLiWingmanBootstrapBundle Bootstrap;

		bool Initialize(const uint32 Seed)
		{
			const FGuLiWingmanGroupHandle Group = MakeGroup(Seed);
			FGuLiWingmanRelayTuning Tuning;
			Tuning.CandidateBucketCapacity = 128.0;
			Tuning.CandidateTokensPerSecond = 128.0;
			return Relay.InitializeGroup(
				MatchEpoch, Group, Owner, Backup, MakeConfig(Group), 0.0, Tuning)
				&& Relay.ConfigureStrictFlightContract(9u, MakeRevisions(), 0.0)
				&& Relay.BuildBootstrap(Bootstrap);
		}

		bool InitializeAndActivate(const uint32 Seed)
		{
			return Initialize(Seed) && Activate(Relay, Owner, Bootstrap);
		}
	};

	const FGuLiWingmanAcceptedBatch* FindAcceptedFor(
		const FGuLiWingmanRelayServer& Relay,
		const FGuLiWingmanHandle& Wingman)
	{
		for (int32 Index = Relay.GetAcceptedHistory().Num() - 1; Index >= 0; --Index)
		{
			const FGuLiWingmanAcceptedBatch& Batch = Relay.GetAcceptedHistory()[Index];
			if (Batch.FindSample(Wingman))
			{
				return &Batch;
			}
		}
		return nullptr;
	}

	FGuLiWingmanFireIntent MakeFireIntent(
		const FGuLiWingmanRelayServer& Relay,
		const FGuLiWingmanHandle& Emitter,
		const FGuLiTargetHandle& Target,
		const uint32 FireSequence)
	{
		FGuLiWingmanFireIntent Intent;
		const FGuLiWingmanAcceptedBatch* Source = FindAcceptedFor(Relay, Emitter);
		if (!Source)
		{
			return Intent;
		}
		Intent.MatchEpoch = Relay.GetMatchEpoch();
		Intent.Group = Relay.GetLeaseState().Group;
		Intent.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Intent.DomainFireSequence = FireSequence;
		Intent.Emitter = Emitter;
		Intent.SourceAcceptedState = Source->StateRef;
		Intent.ClientFireTick = Source->StateRef.ClientSimTick + 1u;
		Intent.Target = Target;
		Intent.WeaponAbilityId = Relay.GetAbilityConfig().BasicWeaponAbilityId;
		Intent.WeaponDefinitionRevision = Relay.GetAbilityConfig().BasicWeaponDefinitionRevision;
		Intent.AbilitySetRevision = Relay.GetAbilityConfig().AbilitySetRevision;
		Intent.AimDirectionMilli = FIntVector(1000, 0, 0);
		Intent.bClientPredictedLineOfSight = true;
		return Intent;
	}

	struct FTransientWorld
	{
		UWorld* World = nullptr;
		bool bHasWorldContext = false;

		~FTransientWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (bHasWorldContext && GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
		}

		bool Initialize(FGuLiWingmanQARoleProbeResult& Out)
		{
			if (!GEngine)
			{
				SetError(Out, TEXT("Engine unavailable for isolated production probe."));
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				SetError(Out, TEXT("Could not create isolated authority World."));
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bHasWorldContext = true;
			return World->IsGameWorld() && World->GetNetMode() != NM_Client;
		}
	};

	struct FVirtualTargetState
	{
		FGuLiTargetHandle Handle;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		FVector Location = FVector::ZeroVector;
		float Radius = 50.0f;
		float Health = 100.0f;
	};

	bool RegisterVirtualTarget(
		UGuLiDamageLedgerSubsystem& Ledger,
		UObject& LifetimeOwner,
		const TSharedRef<FVirtualTargetState>& State)
	{
		FGuLiCombatTargetAdapter Adapter;
		Adapter.LifetimeOwner = &LifetimeOwner;
		Adapter.ReadSnapshot = [State](FGuLiCombatTargetSnapshot& OutSnapshot)
		{
			OutSnapshot = FGuLiCombatTargetSnapshot{};
			OutSnapshot.Handle = State->Handle;
			OutSnapshot.Team = State->Team;
			OutSnapshot.Location = State->Location;
			OutSnapshot.CollisionRadius = State->Radius;
			OutSnapshot.Health = State->Health;
			OutSnapshot.bAlive = State->Health > 0.0f;
			return true;
		};
		Adapter.ApplyDamage = [State](
			const FGuLiDamageRequest& Request,
			FGuLiDamageCommitResult& OutResult)
		{
			if (Request.Target != State->Handle || State->Health <= 0.0f)
			{
				return false;
			}
			const float Before = State->Health;
			State->Health = FMath::Max(0.0f, State->Health - Request.Damage);
			OutResult.AppliedDamage = Before - State->Health;
			OutResult.RemainingHealth = State->Health;
			OutResult.bKilled = State->Health <= 0.0f;
			return true;
		};
		return Ledger.RegisterTarget(State->Handle, MoveTemp(Adapter));
	}

	FGuLiDamageRequest MakeDamage(
		const uint32 Seed,
		const FGuLiTargetHandle& Source,
		const FGuLiWingmanHandle& Emitter,
		const FGuLiTargetHandle& Target,
		const float Damage)
	{
		FGuLiDamageRequest Request;
		Request.MatchEpoch = MatchEpoch;
		Request.DamageEventId = FGuid(0x44414d47u, Seed, 1u, 1u);
		Request.ShotId = FGuid(0x53484f54u, Seed, 2u, 2u);
		Request.Source = Source;
		Request.Emitter = Emitter;
		Request.Target = Target;
		Request.Damage = Damage;
		return Request;
	}

	bool InitializeLedger(
		FTransientWorld& Fixture,
		FGuLiWingmanQARoleProbeResult& Out,
		UGuLiDamageLedgerSubsystem*& OutLedger,
		UObject*& OutLifetime)
	{
		OutLedger = nullptr;
		OutLifetime = nullptr;
		if (!Fixture.Initialize(Out))
		{
			return false;
		}
		OutLedger = Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
		// The isolated UWorld is already a concrete UObject with exactly the
		// lifetime required by the virtual target adapters. UObject itself is
		// abstract in UE 5.7, so constructing a bare UObject here would fire an
		// object-allocation ensure before any production-path assertion runs.
		OutLifetime = Fixture.World;
		if (!OutLedger || !OutLifetime || !OutLedger->BeginServerEpoch(MatchEpoch))
		{
			SetError(Out, TEXT("Could not initialize isolated Damage Ledger epoch."));
			return false;
		}
		return true;
	}
}

namespace GuLiWingmanQARoleProbePrivate
{
	bool CompleteTakeover(
		FGuLiWingmanRelayServer& Relay,
		const FGuid& NewOwner,
		const uint32 FirstSequence,
		const uint32 ClientTick,
		const double NowSeconds,
		FGuLiWingmanBootstrapBundle& OutBootstrap)
	{
		if (!Relay.BuildBootstrap(OutBootstrap)
			|| !AcknowledgeCut(Relay, NewOwner, OutBootstrap, NowSeconds))
		{
			return false;
		}
		const TArray<FGuLiWingmanCandidateBatch> Flights = MakeAllFlights(
			Relay, FirstSequence, 1u, ClientTick, NowSeconds + 0.01);
		return SubmitWholeAtomic(
			Relay, NewOwner, OutBootstrap, Flights, 0x7000u + FirstSequence, NowSeconds + 0.01).Disposition
				== EGuLiWingmanSubmissionDisposition::Accepted
			&& Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active;
	}

	bool HasLeaseEvent(
		const FGuLiWingmanRelayServer& Relay,
		const EGuLiWingmanLeaseEventType Type)
	{
		return Relay.GetLeaseEvents().ContainsByPredicate([Type](const FGuLiWingmanLeaseEvent& Event)
		{
			return Event.Type == Type && Event.DetectionLagSeconds >= 0.0
				&& Event.DetectionLagSeconds <= 1.1;
		});
	}

	bool RunLeaseLoss(FGuLiWingmanQARoleProbeResult& Out)
	{
		FGuLiWingmanRelayAuthorityRegistry Registry;
		const FGuLiWingmanGroupHandle Group = MakeGroup(81u);
		const FGuid Owner(81u, 82u, 83u, 84u);
		const FGuid Backup(85u, 86u, 87u, 88u);
		FGuLiWingmanRelayTuning Tuning;
		Tuning.CandidateBucketCapacity = 128.0;
		Tuning.CandidateTokensPerSecond = 128.0;
		FGuLiWingmanRelayServer* Relay = Registry.CreateGroup(
			MatchEpoch, Group, Owner, Backup, MakeConfig(Group), 0.0, Tuning);
		FGuLiWingmanBootstrapBundle Initial;
		if (!Relay || !Relay->ConfigureStrictFlightContract(9u, MakeRevisions(), 0.0)
			|| !Relay->BuildBootstrap(Initial) || !Activate(*Relay, Owner, Initial))
		{
			SetError(Out, TEXT("Could not activate lease-loss registry fixture."));
			return false;
		}
		const FGuLiWingmanCandidateBatch CapturedOldCandidate =
			MakeFlight(*Relay, 0u, 6u, 2u, 106u, 0.2);
		const FGuLiWingmanHandle OldEmitter = Relay->GetRoster()[0].Wingman;
		FGuLiTargetHandle Target;
		Target.Kind = EGuLiTargetKind::CommanderSoldier;
		Target.AuthorityId = FGuid(81u, 1u, 2u, 3u);
		Target.Generation = 1u;
		Target.LocalId = 1u;
		const FGuLiWingmanFireIntent CapturedOldFire =
			MakeFireIntent(*Relay, OldEmitter, Target, 1u);

		Registry.RunLeaseMaintenance(1.0);
		Registry.RunLeaseMaintenance(2.0);
		const bool bStale = Relay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Stale;
		Registry.RunLeaseMaintenance(3.0);
		const bool bUnavailable = Relay->GetLeaseState().Lifecycle
			== EGuLiWingmanGroupLifecycle::Unavailable;
		Registry.RunLeaseMaintenance(4.0);
		const bool bRevoked = Relay->IsActiveLeaseRevoked()
			&& Relay->GetActiveLeaseTransaction().State
				== EGuLiWingmanActiveLeaseTransactionState::NoOwner;
		const bool bLifecycleEvents = HasLeaseEvent(*Relay, EGuLiWingmanLeaseEventType::BecameStale)
			&& HasLeaseEvent(*Relay, EGuLiWingmanLeaseEventType::BecameUnavailable)
			&& HasLeaseEvent(*Relay, EGuLiWingmanLeaseEventType::ActiveLeaseRevoked);
		if (!bStale || !bUnavailable || !bRevoked || !bLifecycleEvents)
		{
			SetError(Out, TEXT("Lease watchdog did not traverse Active/Stale/Unavailable/Revoke at 1 Hz."));
			return false;
		}
		Pass(Out, TEXT("LEASE_ACTIVE_STALE_UNAVAILABLE_REVOKE"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("silent owner traversed all 1 Hz freshness states with bounded detection lag"));

		FGuLiWingmanOwnerLossAssignment Offer;
		FGuLiWingmanOwnerLossAssignment Commit;
		FGuLiWingmanBootstrapBundle Takeover;
		if (!Registry.BeginLeaseLossRecovery(Group, {Backup}, 4.01, Offer)
			|| Offer.Disposition != EGuLiWingmanOwnerLossDisposition::OfferStarted
			|| !Registry.AcknowledgeLeaseOfferReady(
				Group, Backup, Offer.OfferRevision, 4.02, Commit)
			|| !CompleteTakeover(*Relay, Backup, 6u, 106u, 4.03, Takeover))
		{
			SetError(Out, TEXT("Backup did not complete Offer/Ready/Commit/ACK/Takeover after lease loss."));
			return false;
		}
		Registry.RunLeaseMaintenance(5.0);
		if (Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Active)
		{
			SetError(Out, TEXT("Recovered lease became stale on its first maintenance boundary."));
			return false;
		}
		Pass(Out, TEXT("TAKEOVER_RECOVERS_ACTIVE"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("connected backup completed frozen six-scope takeover and remained Active"));

		const FGuLiWingmanSubmissionResult OldCandidateResult = Relay->SubmitCandidate(
			Owner, CapturedOldCandidate, 5.01, CarrierAt(5.01), PermitWorld());
		const FGuLiWingmanSubmissionResult OldFireResult = Relay->SubmitFireIntent(
			Owner, CapturedOldFire, 5.02);
		if (OldCandidateResult.RejectReason != EGuLiWingmanRejectReason::WrongLease
			|| OldFireResult.RejectReason != EGuLiWingmanRejectReason::WrongLease)
		{
			SetError(Out, TEXT("Old owner Candidate or Fire survived the committed recovery epoch."));
			return false;
		}
		Pass(Out, TEXT("OLD_OWNER_PERMANENTLY_REJECTED"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("captured old Candidate and Fire both rejected WrongLease after takeover"));
		return true;
	}

	bool RunGracefulFireBeforeCommit(FGuLiWingmanQARoleProbeResult& Out)
	{
		FRelayFixture Fixture;
		if (!Fixture.InitializeAndActivate(82u))
		{
			SetError(Out, TEXT("Could not activate graceful transfer fixture."));
			return false;
		}
		const FGuid OldOwner = Fixture.Owner;
		const FGuid NewOwner = Fixture.Backup;
		FGuLiWingmanPendingLeaseOffer Offer;
		if (!Fixture.Relay.BeginLeaseOffer(NewOwner, OldOwner, 0.2, Offer))
		{
			SetError(Out, TEXT("Could not begin graceful Offer preview."));
			return false;
		}
		const uint64 PreviewHash = Offer.PreviewAcceptedSnapshotHash;
		const FGuLiWingmanSubmissionResult LateOldCandidate = Fixture.Relay.SubmitCandidate(
			OldOwner,
			MakeFlight(Fixture.Relay, 0u, 6u, 2u, 106u, 0.3),
			0.3,
			CarrierAt(0.3),
			PermitWorld());
		if (LateOldCandidate.Disposition != EGuLiWingmanSubmissionDisposition::Accepted)
		{
			SetError(Out, TEXT("Old Active owner could not update the pending Offer baseline."));
			return false;
		}
		const FGuLiWingmanHandle OldEmitter = LateOldCandidate.AcceptedBatch.Samples[0].Wingman;
		FGuLiTargetHandle Target;
		Target.Kind = EGuLiTargetKind::CommanderSoldier;
		Target.AuthorityId = FGuid(82u, 1u, 2u, 3u);
		Target.Generation = 1u;
		Target.LocalId = 2u;
		const FGuLiWingmanFireIntent CapturedInFlight =
			MakeFireIntent(Fixture.Relay, OldEmitter, Target, 1u);
		Fixture.Relay.RunLeaseMaintenance(1.0);
		Fixture.Relay.RunLeaseMaintenance(2.0);
		if (Fixture.Relay.GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Stale
			|| !Fixture.Relay.GetPendingLeaseOffer().IsPending()
			|| !Fixture.Relay.AcknowledgeLeaseOfferReady(NewOwner, Offer.OfferRevision, 2.1))
		{
			SetError(Out, TEXT("Pending Offer did not remain independent while old lease became Stale."));
			return false;
		}
		FGuLiWingmanBootstrapBundle Takeover;
		if (!Fixture.Relay.BuildBootstrap(Takeover)
			|| Fixture.Relay.GetActiveLeaseTransaction().FrozenAcceptedSnapshotHash == PreviewHash
			|| !AcknowledgeCut(Fixture.Relay, NewOwner, Takeover, 2.11)
			|| SubmitWholeAtomic(
				Fixture.Relay,
				NewOwner,
				Takeover,
				MakeAllFlights(Fixture.Relay, 7u, 1u, 112u, 2.12),
				8201u,
				2.12).Disposition != EGuLiWingmanSubmissionDisposition::Accepted)
		{
			SetError(Out, TEXT("Graceful transfer did not freeze the post-preview baseline and activate atomically."));
			return false;
		}
		Pass(Out, TEXT("TRANSFER_OFFER_READY_COMMIT_ACK_TAKEOVER"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("Offer stayed a preview; Ready froze the updated baseline; ACK and full batch activated"));

		const FGuLiWingmanSubmissionResult OldRejected = Fixture.Relay.SubmitFireIntent(
			OldOwner, CapturedInFlight, 2.13);
		const FGuLiWingmanHandle NewEmitter = Fixture.Relay.GetRoster()[0].Wingman;
		const FGuLiWingmanFireIntent NewFire = MakeFireIntent(
			Fixture.Relay, NewEmitter, Target, 1u);
		const FGuLiWingmanSubmissionResult NewAccepted = Fixture.Relay.SubmitFireIntent(
			NewOwner, NewFire, 2.14);
		const FGuLiWingmanSubmissionResult NewDuplicate = Fixture.Relay.SubmitFireIntent(
			NewOwner, NewFire, 2.15);
		if (OldRejected.RejectReason != EGuLiWingmanRejectReason::WrongLease
			|| NewAccepted.Disposition != EGuLiWingmanSubmissionDisposition::Accepted
			|| NewDuplicate.RejectReason != EGuLiWingmanRejectReason::Duplicate)
		{
			SetError(Out, TEXT("In-flight old Fire or first new-lease Fire high-water behavior was incorrect."));
			return false;
		}
		Pass(Out, TEXT("INFLIGHT_FIRE_SINGLE_COMMIT"),
			EGuLiWingmanQALogStream::BattleCombat,
			TEXT("old in-flight Fire rejected after commit; new epoch reserved its first sequence exactly once"));
		return true;
	}

	bool RunDamageAckRace(FGuLiWingmanQARoleProbeResult& Out)
	{
		FRelayFixture Fixture;
		if (!Fixture.InitializeAndActivate(83u))
		{
			SetError(Out, TEXT("Could not activate damage/ACK race fixture."));
			return false;
		}
		const FGuid NewOwner = Fixture.Backup;
		FGuLiWingmanPendingLeaseOffer Offer;
		if (!Fixture.Relay.BeginLeaseOffer(NewOwner, Fixture.Owner, 0.2, Offer)
			|| !Fixture.Relay.AcknowledgeLeaseOfferReady(NewOwner, Offer.OfferRevision, 0.21))
		{
			SetError(Out, TEXT("Could not commit transfer for damage/ACK race."));
			return false;
		}
		FGuLiWingmanBootstrapBundle Frozen;
		if (!Fixture.Relay.BuildBootstrap(Frozen)
			|| !AcknowledgeCut(Fixture.Relay, NewOwner, Frozen, 0.22))
		{
			SetError(Out, TEXT("Could not ACK initial takeover baseline."));
			return false;
		}
		const TArray<FGuLiWingmanCandidateBatch> Flights =
			MakeAllFlights(Fixture.Relay, 6u, 1u, 106u, 0.23);
		TArray<FGuLiWingmanCandidateBatch> FirstTwo;
		FirstTwo.Append(Flights.GetData(), 2);
		const FGuLiWingmanAtomicBatchAcceptance Pending = Fixture.Relay.SubmitAtomicCandidateFragment(
			NewOwner,
			MakeAtomicFragment(Fixture.Relay, Frozen, Flights, FirstTwo, 0u, 2u, 8301u),
			0.23,
			CarrierAt(0.23),
			PermitWorld());
		if (Pending.Disposition != EGuLiWingmanSubmissionDisposition::Pending)
		{
			SetError(Out, TEXT("Takeover batch was not held pending before damage race."));
			return false;
		}

		const FGuLiWingmanHandle Victim = Fixture.Relay.GetRoster()[0].Wingman;
		const FGuLiTargetHandle VictimTarget = GuLiCombatTargets::MakeWingmanTargetHandle(Victim);
		FGuLiTargetHandle Source;
		Source.Kind = EGuLiTargetKind::Ship;
		Source.AuthorityId = FGuid(83u, 10u, 20u, 30u);
		Source.Generation = 1u;
		FTransientWorld WorldFixture;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UObject* Lifetime = nullptr;
		if (!InitializeLedger(WorldFixture, Out, Ledger, Lifetime))
		{
			return false;
		}
		const TSharedRef<FVirtualTargetState> SourceState = MakeShared<FVirtualTargetState>();
		SourceState->Handle = Source;
		SourceState->Team = EGuLiTeam::Red;
		const TSharedRef<FVirtualTargetState> VictimState = MakeShared<FVirtualTargetState>();
		VictimState->Handle = VictimTarget;
		VictimState->Team = EGuLiTeam::Blue;
		if (!RegisterVirtualTarget(*Ledger, *Lifetime, SourceState)
			|| !RegisterVirtualTarget(*Ledger, *Lifetime, VictimState))
		{
			SetError(Out, TEXT("Could not register damage-race ledger targets."));
			return false;
		}
		const FGuLiDamageRequest Damage = MakeDamage(83u, Source, FGuLiWingmanHandle{},
			VictimTarget, 100.0f);
		const FGuLiDamageCommitResult First = Ledger->CommitDamage(Damage);
		const FGuLiDamageCommitResult Replay = Ledger->CommitDamage(Damage);
		const uint32 RosterBefore = Fixture.Relay.GetRosterRevision();
		const bool bMarkedDead = Fixture.Relay.MarkWingmanDead(Victim);
		const FGuLiWingmanAtomicBatchAcceptance OldCompletion = Fixture.Relay.SubmitAtomicCandidateFragment(
			NewOwner,
			MakeAtomicFragment(Fixture.Relay, Frozen, Flights, Flights, 0u, 1u, 8302u),
			0.25,
			CarrierAt(0.25),
			PermitWorld());
		FGuLiWingmanBootstrapBundle Revised;
		if (!bMarkedDead || Fixture.Relay.GetRosterRevision() != RosterBefore + 1u
			|| OldCompletion.Disposition != EGuLiWingmanSubmissionDisposition::Rejected
			|| !Fixture.Relay.BuildBootstrap(Revised)
			|| Revised.AtomicBaselineHash == Frozen.AtomicBaselineHash
			|| !AcknowledgeCut(Fixture.Relay, NewOwner, Revised, 0.26)
			|| SubmitWholeAtomic(
				Fixture.Relay,
				NewOwner,
				Revised,
				MakeAllFlights(Fixture.Relay, 6u, 1u, 106u, 0.27),
				8303u,
				0.27).Disposition != EGuLiWingmanSubmissionDisposition::Accepted
			|| First.Status != EGuLiDamageCommitStatus::Committed || !First.bKilled
			|| Replay.Status != EGuLiDamageCommitStatus::Duplicate
			|| Ledger->GetCommitCount() != 1u || Ledger->GetDeathCommitCount() != 1u
			|| Ledger->GetRewardCommitCount() != 1u)
		{
			SetError(Out, TEXT("Damage/ACK race did not cancel the old batch, revise baseline, and dedupe damage."));
			return false;
		}
		Pass(Out, TEXT("DAMAGE_ACK_TRANSFER_RACE_SINGLE_COMMIT"),
			EGuLiWingmanQALogStream::BattleCombat,
			TEXT("lethal event committed once; roster revision canceled old batch; revised cut activated without resurrection"));
		return true;
	}

	bool RunActualDisconnect(FGuLiWingmanQARoleProbeResult& Out)
	{
		FGuLiWingmanRelayAuthorityRegistry Registry;
		const FGuLiWingmanGroupHandle Group = MakeGroup(84u);
		const FGuid Owner(840u, 841u, 842u, 843u);
		const FGuid FirstBackup(850u, 851u, 852u, 853u);
		const FGuid SecondBackup(860u, 861u, 862u, 863u);
		FGuLiWingmanRelayTuning Tuning;
		Tuning.CandidateBucketCapacity = 128.0;
		Tuning.CandidateTokensPerSecond = 128.0;
		FGuLiWingmanRelayServer* Relay = Registry.CreateGroup(
			MatchEpoch, Group, Owner, FirstBackup, MakeConfig(Group), 0.0, Tuning);
		FGuLiWingmanBootstrapBundle Initial;
		if (!Relay || !Relay->ConfigureStrictFlightContract(9u, MakeRevisions(), 0.0)
			|| !Relay->BuildBootstrap(Initial) || !Activate(*Relay, Owner, Initial))
		{
			SetError(Out, TEXT("Could not activate disconnect registry fixture."));
			return false;
		}

		FGuLiWingmanReplenishmentController Replenishment;
		TArray<FGuLiWingmanReplenishmentResult> Replenished;
		bool bRosterChanged = false;
		const FGuLiWingmanHandle Dead = Relay->GetRoster()[0].Wingman;
		if (!Relay->MarkWingmanDead(Dead)
			|| Replenishment.Advance(*Relay, 0.15, Replenished, bRosterChanged, 0.5) != 0)
		{
			SetError(Out, TEXT("Could not schedule pre-disconnect replenishment."));
			return false;
		}
		FGuLiWingmanBootstrapBundle ActiveRosterCut;
		if (!Relay->RefreshActiveRosterCut(0.16, ActiveRosterCut)
			|| !AcknowledgeCut(*Relay, Owner, ActiveRosterCut, 0.17))
		{
			SetError(Out, TEXT("Could not acknowledge pre-disconnect death roster cut."));
			return false;
		}

		const TArray<FGuLiWingmanOwnerLossAssignment> Disconnected =
			Registry.HandleOwnerDisconnected(Owner, {SecondBackup, FirstBackup}, 0.2);
		if (Disconnected.Num() != 1
			|| Disconnected[0].Disposition != EGuLiWingmanOwnerLossDisposition::OfferStarted
			|| Disconnected[0].NewOwnerPlayerGuid != FirstBackup)
		{
			SetError(Out, TEXT("Socket disconnect did not retain the group and offer the configured backup."));
			return false;
		}
		Pass(Out, TEXT("OWNER_SOCKET_DISCONNECT_DETECTED"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("owner disconnect retained all scopes and issued reliable offer to BackupA"));

		Registry.RunLeaseMaintenance(1.0);
		Registry.RunLeaseMaintenance(2.0);
		const int32 SpawnWhileUnavailable = Replenishment.Advance(
			*Relay, 2.0, Replenished, bRosterChanged, 0.5);
		const int32 DeferredCount = Replenishment.GetQueuedDueSlotCount();
		Registry.RunLeaseMaintenance(3.0);
		const TArray<FGuLiWingmanOwnerLossAssignment> Rotated = Registry.RunLeaseMaintenance(4.0);
		if (Rotated.Num() != 1 || Rotated[0].NewOwnerPlayerGuid != SecondBackup)
		{
			SetError(Out, TEXT("Missed BackupA Ready deadline did not rotate exactly once to BackupB."));
			return false;
		}
		FGuLiWingmanOwnerLossAssignment Commit;
		FGuLiWingmanBootstrapBundle Takeover;
		if (!Registry.AcknowledgeLeaseOfferReady(
			Group, SecondBackup, Rotated[0].OfferRevision, 4.01, Commit)
			|| !CompleteTakeover(*Relay, SecondBackup, 6u, 106u, 4.02, Takeover))
		{
			SetError(Out, TEXT("BackupB did not complete the retained disconnect takeover."));
			return false;
		}
		Pass(Out, TEXT("BACKUP_TAKEOVER_AFTER_DISCONNECT"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("BackupA missed Ready; 1 Hz rotation selected BackupB; frozen takeover returned Active"));

		const int32 Released = Replenishment.Advance(
			*Relay, 4.1, Replenished, bRosterChanged, 0.5);
		const uint64 ScheduleId = Replenished.IsEmpty() ? 0u : Replenished[0].ScheduleId;
		const int32 ReplayedRelease = Replenishment.Advance(
			*Relay, 4.2, Replenished, bRosterChanged, 0.5);
		if (SpawnWhileUnavailable != 0 || DeferredCount != 1 || Released != 1
			|| ScheduleId == 0u || ReplayedRelease != 0)
		{
			SetError(Out, TEXT("Due replenishment was not deferred while unavailable and released exactly once after Active."));
			return false;
		}
		Pass(Out, TEXT("DEFERRED_REPLENISH_SINGLE_SPAWN"),
			EGuLiWingmanQALogStream::BattleCombat,
			TEXT("due schedule spawned zero while unavailable, once after Active, and never replayed"));
		return true;
	}
}

namespace GuLiWingmanQARoleProbePrivate
{
	bool RunMirroredCombat(bool bAB, FGuLiWingmanQARoleProbeResult& Out);
	bool RunFireReplay(FGuLiWingmanQARoleProbeResult& Out);
	bool RunProjectileReplay(FGuLiWingmanQARoleProbeResult& Out);
	bool RunConcurrentDuplicate(FGuLiWingmanQARoleProbeResult& Out);
	bool RunCorrectionReverse(FGuLiWingmanQARoleProbeResult& Out);
	bool RunDeathBeforePose(FGuLiWingmanQARoleProbeResult& Out);
	bool RunLeaseBeforeOldProposal(FGuLiWingmanQARoleProbeResult& Out);
	bool RunRespawnBeforeOldPose(FGuLiWingmanQARoleProbeResult& Out);
}

bool GuLiWingmanQARoleProbes::Supports(const FName RoleId)
{
	static const TSet<FName> Supported = {
		TEXT("S3C-AB"),
		TEXT("S3C-BA"),
		TEXT("S4-Idempotency-FireProposalReplay"),
		TEXT("S4-Idempotency-ProjectileOverlapReplay"),
		TEXT("S4-Idempotency-Concurrent"),
		TEXT("S6-CorrectionReverse"),
		TEXT("S6-DeathBeforePose"),
		TEXT("S6-LeaseBeforeOldProposal"),
		TEXT("S6-RespawnBeforeOldPose"),
		TEXT("S7-LeaseLoss"),
		TEXT("S7-Graceful-FireBeforeCommit"),
		TEXT("S7-Graceful-DamageAckRace"),
		TEXT("S7-ActualDisconnect")
	};
	return Supported.Contains(RoleId);
}

bool GuLiWingmanQARoleProbes::RequiresClientExecutionDomain(const FName RoleId)
{
	// These probes intentionally exercise the production Presentation actor. A
	// Dedicated executable must never create its Mass/ISM rendering resources,
	// so the formal endpoint router runs them inside a real connected client.
	return RoleId == TEXT("S6-DeathBeforePose")
		|| RoleId == TEXT("S6-RespawnBeforeOldPose");
}

FGuLiWingmanQARoleProbeResult GuLiWingmanQARoleProbes::Run(const FName RoleId)
{
	using namespace GuLiWingmanQARoleProbePrivate;
	FGuLiWingmanQARoleProbeResult Out;
	Out.bRecognizedRole = Supports(RoleId);
	if (!Out.bRecognizedRole)
	{
		return Out;
	}

	if (RoleId == TEXT("S3C-AB"))
	{
		RunMirroredCombat(true, Out);
	}
	else if (RoleId == TEXT("S3C-BA"))
	{
		RunMirroredCombat(false, Out);
	}
	else if (RoleId == TEXT("S4-Idempotency-FireProposalReplay"))
	{
		RunFireReplay(Out);
	}
	else if (RoleId == TEXT("S4-Idempotency-ProjectileOverlapReplay"))
	{
		RunProjectileReplay(Out);
	}
	else if (RoleId == TEXT("S4-Idempotency-Concurrent"))
	{
		RunConcurrentDuplicate(Out);
	}
	else if (RoleId == TEXT("S6-CorrectionReverse"))
	{
		RunCorrectionReverse(Out);
	}
	else if (RoleId == TEXT("S6-DeathBeforePose"))
	{
		RunDeathBeforePose(Out);
	}
	else if (RoleId == TEXT("S6-LeaseBeforeOldProposal"))
	{
		RunLeaseBeforeOldProposal(Out);
	}
	else if (RoleId == TEXT("S6-RespawnBeforeOldPose"))
	{
		RunRespawnBeforeOldPose(Out);
	}
	else if (RoleId == TEXT("S7-LeaseLoss"))
	{
		RunLeaseLoss(Out);
	}
	else if (RoleId == TEXT("S7-Graceful-FireBeforeCommit"))
	{
		RunGracefulFireBeforeCommit(Out);
	}
	else if (RoleId == TEXT("S7-Graceful-DamageAckRace"))
	{
		RunDamageAckRace(Out);
	}
	else if (RoleId == TEXT("S7-ActualDisconnect"))
	{
		RunActualDisconnect(Out);
	}
	return Out;
}

namespace GuLiWingmanQARoleProbePrivate
{
	bool RunCorrectionReverse(FGuLiWingmanQARoleProbeResult& Out)
	{
		TArray<FGuLiWingmanPresentationPose> Samples;
		FGuLiWingmanPresentationPose Newer;
		Newer.SourceTimeSeconds = 2.0;
		Newer.Sequence = 2u;
		Newer.Location = FVector(200.0, 0.0, 0.0);
		Newer.Velocity = FVector(100.0, 0.0, 0.0);
		FGuLiWingmanPresentationPose Older = Newer;
		Older.SourceTimeSeconds = 1.0;
		Older.Sequence = 1u;
		Older.Location = FVector(100.0, 0.0, 0.0);
		const bool bAppliedNew = GuLiWingmanPresentationPolicy::AppendPose(Samples, Newer);
		const bool bRejectedOld = !GuLiWingmanPresentationPolicy::AppendPose(Samples, Older);
		if (!bAppliedNew || !bRejectedOld || Samples.Num() != 1
			|| Samples[0].Sequence != Newer.Sequence || Samples[0].Location != Newer.Location)
		{
			SetError(Out, TEXT("Presentation baseline sequence regression was not rejected without mutation."));
			return false;
		}
		Pass(Out, TEXT("BASELINE_SEQUENCE_REGRESSION_REJECTED"),
			EGuLiWingmanQALogStream::WingmanNet,
			TEXT("newer rebase applied first; captured older baseline discarded without pose rollback"));
		return true;
	}

	bool RunDeathBeforePose(FGuLiWingmanQARoleProbeResult& Out)
	{
		FTransientWorld WorldFixture;
		if (!WorldFixture.Initialize(Out))
		{
			return false;
		}
		FRelayFixture Fixture;
		if (!Fixture.Initialize(72u))
		{
			SetError(Out, TEXT("Could not initialize Death-before-pose Relay fixture."));
			return false;
		}
		AGuLiWingmanPresentationActor* Presentation =
			WorldFixture.World->SpawnActor<AGuLiWingmanPresentationActor>();
		if (!Presentation || !Presentation->ApplyBootstrap(Fixture.Bootstrap, false, 0.0)
			|| !Activate(Fixture.Relay, Fixture.Owner, Fixture.Bootstrap))
		{
			SetError(Out, TEXT("Could not apply the production Bootstrap to remote presentation."));
			return false;
		}
		const FGuLiWingmanAcceptedBatch First = Fixture.Relay.GetAcceptedHistory()[0];
		if (!Presentation->ApplyAcceptedSnapshot(First, 0.1))
		{
			SetError(Out, TEXT("Could not apply initial accepted presentation pose."));
			return false;
		}
		const FGuLiWingmanHandle Dead = First.Samples[0].Wingman;
		const FGuLiWingmanSubmissionResult CapturedLatePose = Fixture.Relay.SubmitCandidate(
			Fixture.Owner,
			MakeFlight(Fixture.Relay, Dead.Flight.FlightIndex, 6u, 2u, 106u, 0.2),
			0.2,
			CarrierAt(0.2),
			PermitWorld());
		const bool bDeathApplied = Presentation->SetWingmanAlive(Dead, false);
		const bool bLatePoseRejected = CapturedLatePose.Disposition
				== EGuLiWingmanSubmissionDisposition::Accepted
			&& !Presentation->ApplyAcceptedSnapshot(CapturedLatePose.AcceptedBatch, 0.21);
		FTransform Presented;
		TArray<FGuLiWingmanAcceptedTargetPose> TargetPoses;
		Presentation->GetFreshAcceptedTargetPoses(0.21, 1.0, TargetPoses);
		const bool bRemainsAbsent = !Presentation->TryGetPresentedTransform(Dead, Presented)
			&& !Presentation->IsWingmanInteractable(Dead)
			&& !TargetPoses.ContainsByPredicate([&Dead](const FGuLiWingmanAcceptedTargetPose& Pose)
			{
				return Pose.Wingman == Dead;
			});
		if (!bDeathApplied || !bLatePoseRejected || !bRemainsAbsent)
		{
			SetError(Out, TEXT("Reliable Death did not dominate a captured late Accepted pose."));
			return false;
		}
		Pass(Out, TEXT("POST_DEATH_POSE_REJECTED"),
			EGuLiWingmanQALogStream::WingmanNet,
			TEXT("reliable death applied before captured Accepted pose; dead track stayed hidden and untargetable"));
		return true;
	}

	bool RunLeaseBeforeOldProposal(FGuLiWingmanQARoleProbeResult& Out)
	{
		FRelayFixture Fixture;
		if (!Fixture.InitializeAndActivate(73u))
		{
			SetError(Out, TEXT("Could not activate old-lease proposal fixture."));
			return false;
		}
		const FGuid OldOwner = Fixture.Owner;
		const uint32 OldLeaseEpoch = Fixture.Relay.GetLeaseState().LeaseEpoch;
		const FGuLiWingmanCandidateBatch CapturedOld =
			MakeFlight(Fixture.Relay, 0u, 6u, 2u, 106u, 0.2);
		FGuLiWingmanPendingLeaseOffer Offer;
		if (!Fixture.Relay.BeginLeaseOffer(Fixture.Backup, OldOwner, 0.2, Offer)
			|| !Fixture.Relay.AcknowledgeLeaseOfferReady(Fixture.Backup, Offer.OfferRevision, 0.21))
		{
			SetError(Out, TEXT("Could not commit the new lease before releasing old proposal."));
			return false;
		}
		const FGuLiWingmanSubmissionResult Rejected = Fixture.Relay.SubmitCandidate(
			OldOwner, CapturedOld, 0.22, CarrierAt(0.22), PermitWorld());
		if (Fixture.Relay.GetLeaseState().LeaseEpoch <= OldLeaseEpoch
			|| Rejected.RejectReason != EGuLiWingmanRejectReason::WrongLease)
		{
			SetError(Out, TEXT("Old-owner Candidate survived TransferCommit epoch replacement."));
			return false;
		}
		Pass(Out, TEXT("OLD_LEASE_PROPOSAL_REJECTED"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("new TransferCommit applied first; captured old-owner Candidate rejected WrongLease"));
		return true;
	}

	bool RunRespawnBeforeOldPose(FGuLiWingmanQARoleProbeResult& Out)
	{
		FTransientWorld WorldFixture;
		if (!WorldFixture.Initialize(Out))
		{
			return false;
		}
		FRelayFixture OldFixture;
		if (!OldFixture.Initialize(74u))
		{
			SetError(Out, TEXT("Could not initialize old group-generation fixture."));
			return false;
		}
		AGuLiWingmanPresentationActor* Presentation =
			WorldFixture.World->SpawnActor<AGuLiWingmanPresentationActor>();
		if (!Presentation || !Presentation->ApplyBootstrap(OldFixture.Bootstrap, false, 0.0)
			|| !Activate(OldFixture.Relay, OldFixture.Owner, OldFixture.Bootstrap))
		{
			SetError(Out, TEXT("Could not establish old group presentation."));
			return false;
		}
		const FGuLiWingmanAcceptedBatch CapturedOldPose = OldFixture.Relay.GetAcceptedHistory()[0];
		const FGuLiWingmanHandle OldWingman = CapturedOldPose.Samples[0].Wingman;
		if (!Presentation->ApplyAcceptedSnapshot(CapturedOldPose, 0.1))
		{
			SetError(Out, TEXT("Could not capture old group Accepted pose."));
			return false;
		}

		const FGuLiWingmanGroupHandle NewGroup = MakeGroup(
			74u, OldFixture.Relay.GetLeaseState().Group.GroupGeneration + 1u);
		FGuLiWingmanRelayServer NewRelay;
		FGuLiWingmanBootstrapBundle NewBootstrap;
		if (!NewRelay.InitializeGroup(
			MatchEpoch, NewGroup, OldFixture.Owner, OldFixture.Backup, MakeConfig(NewGroup), 0.2)
			|| !NewRelay.ConfigureStrictFlightContract(10u, MakeRevisions(), 0.2)
			|| !NewRelay.BuildBootstrap(NewBootstrap)
			|| !Presentation->RemoveGroup(OldFixture.Relay.GetLeaseState().Group)
			|| !Presentation->ApplyBootstrap(NewBootstrap, false, 0.2))
		{
			SetError(Out, TEXT("Could not atomically replace old presentation generation."));
			return false;
		}
		const bool bOldRejected = !Presentation->ApplyAcceptedSnapshot(CapturedOldPose, 0.21);
		FTransform OldTransform;
		if (!bOldRejected || Presentation->TryGetPresentedTransform(OldWingman, OldTransform))
		{
			SetError(Out, TEXT("Old group-generation pose survived retained roster replacement."));
			return false;
		}
		Pass(Out, TEXT("OLD_GROUP_POSE_REJECTED"),
			EGuLiWingmanQALogStream::WingmanNet,
			TEXT("new GroupGeneration roster applied first; captured old-generation pose had no destination track"));
		return true;
	}
}

namespace GuLiWingmanQARoleProbePrivate
{
	bool RunMirroredCombat(
		const bool bAB,
		FGuLiWingmanQARoleProbeResult& Out)
	{
		FRelayFixture RelayFixture;
		if (!RelayFixture.InitializeAndActivate(bAB ? 31u : 32u))
		{
			SetError(Out, TEXT("Could not activate mirrored-combat Relay fixture."));
			return false;
		}

		const FGuLiWingmanHandle A = RelayFixture.Relay.GetRoster()[0].Wingman;
		const FGuLiWingmanHandle B = RelayFixture.Relay.GetRoster()[1].Wingman;
		const FGuLiWingmanHandle Winner = bAB ? A : B;
		const FGuLiWingmanHandle Loser = bAB ? B : A;
		const FGuLiTargetHandle WinnerTarget = GuLiCombatTargets::MakeWingmanTargetHandle(Winner);
		const FGuLiTargetHandle LoserTarget = GuLiCombatTargets::MakeWingmanTargetHandle(Loser);

		FTransientWorld WorldFixture;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UObject* Lifetime = nullptr;
		if (!InitializeLedger(WorldFixture, Out, Ledger, Lifetime))
		{
			return false;
		}
		const TSharedRef<FVirtualTargetState> WinnerState = MakeShared<FVirtualTargetState>();
		WinnerState->Handle = WinnerTarget;
		WinnerState->Team = bAB ? EGuLiTeam::Red : EGuLiTeam::Blue;
		const TSharedRef<FVirtualTargetState> LoserState = MakeShared<FVirtualTargetState>();
		LoserState->Handle = LoserTarget;
		LoserState->Team = bAB ? EGuLiTeam::Blue : EGuLiTeam::Red;
		if (!RegisterVirtualTarget(*Ledger, *Lifetime, WinnerState)
			|| !RegisterVirtualTarget(*Ledger, *Lifetime, LoserState))
		{
			SetError(Out, TEXT("Could not register mirrored Wingman ledger targets."));
			return false;
		}

		// Both directions are captured before this deterministic release point. The
		// requested role decides which proposal reaches the single-threaded authority first.
		const FGuLiWingmanFireIntent WinnerFire = MakeFireIntent(
			RelayFixture.Relay, Winner, LoserTarget, 1u);
		const FGuLiWingmanSubmissionResult WinnerAccepted = RelayFixture.Relay.SubmitFireIntent(
			RelayFixture.Owner, WinnerFire, 0.15);
		const FGuLiDamageRequest Lethal = MakeDamage(
			bAB ? 31u : 32u, WinnerTarget, Winner, LoserTarget, 100.0f);
		const FGuLiDamageCommitResult First = Ledger->CommitDamage(Lethal);
		const bool bDeathApplied = RelayFixture.Relay.MarkWingmanDead(Loser);
		const FGuLiWingmanFireIntent LoserFire = MakeFireIntent(
			RelayFixture.Relay, Loser, WinnerTarget, 1u);
		const FGuLiWingmanSubmissionResult LoserRejected = RelayFixture.Relay.SubmitFireIntent(
			RelayFixture.Owner, LoserFire, 0.16);
		const FGuLiDamageCommitResult Duplicate = Ledger->CommitDamage(Lethal);

		const bool bBarrierResult = WinnerAccepted.Disposition
				== EGuLiWingmanSubmissionDisposition::Accepted
			&& First.Status == EGuLiDamageCommitStatus::Committed && First.bKilled
			&& bDeathApplied
			&& LoserRejected.RejectReason == EGuLiWingmanRejectReason::EmitterDead;
		const bool bSingleCommit = Duplicate.Status == EGuLiDamageCommitStatus::Duplicate
			&& Ledger->GetCommitCount() == 1u
			&& Ledger->GetDeathCommitCount() == 1u
			&& Ledger->GetRewardCommitCount() == 1u
			&& Ledger->GetGrantedRewardCount() == 0u;
		if (!bBarrierResult || !bSingleCommit)
		{
			SetError(Out, TEXT("Mirrored lethal barrier did not produce one winner, SourceDead loser, and one ledger commit."));
			return false;
		}

		Pass(Out,
			bAB ? FName(TEXT("COMBAT_BARRIER_RELEASE_AB"))
				: FName(TEXT("COMBAT_BARRIER_RELEASE_BA")),
			EGuLiWingmanQALogStream::BattleCombat,
			TEXT("captured pair released in requested order; losing emitter rejected after death"));
		Pass(Out, TEXT("MIRRORED_LEDGER_SINGLE_COMMIT"),
			EGuLiWingmanQALogStream::BattleCombat,
			TEXT("damage/death/reward decision committed once; Reward remains PolicyUnavailable"));
		return true;
	}

	bool RunFireReplay(FGuLiWingmanQARoleProbeResult& Out)
	{
		FRelayFixture Fixture;
		if (!Fixture.InitializeAndActivate(41u))
		{
			SetError(Out, TEXT("Could not activate Fire replay Relay fixture."));
			return false;
		}
		const FGuLiWingmanHandle Emitter = Fixture.Relay.GetRoster()[0].Wingman;
		FGuLiTargetHandle Target;
		Target.Kind = EGuLiTargetKind::CommanderSoldier;
		Target.AuthorityId = FGuid(41u, 42u, 43u, 44u);
		Target.Generation = 1u;
		Target.LocalId = 7u;
		const FGuLiWingmanFireIntent Intent = MakeFireIntent(Fixture.Relay, Emitter, Target, 1u);
		const FGuLiWingmanSubmissionResult First = Fixture.Relay.SubmitFireIntent(
			Fixture.Owner, Intent, 0.15);
		const FGuLiWingmanSubmissionResult Replay = Fixture.Relay.SubmitFireIntent(
			Fixture.Owner, Intent, 0.16);
		if (First.Disposition != EGuLiWingmanSubmissionDisposition::Accepted
			|| Replay.RejectReason != EGuLiWingmanRejectReason::Duplicate)
		{
			SetError(Out, TEXT("Fire proposal replay did not reserve exactly one DomainFireSequence."));
			return false;
		}
		Pass(Out, TEXT("FIRE_PROPOSAL_REPLAY_SINGLE_COMMIT"),
			EGuLiWingmanQALogStream::WingmanRelay,
			TEXT("first FireIntent accepted; same emitter/sequence replay rejected Duplicate"));
		return true;
	}

	bool RunProjectileReplay(FGuLiWingmanQARoleProbeResult& Out)
	{
		FTransientWorld WorldFixture;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UObject* Lifetime = nullptr;
		if (!InitializeLedger(WorldFixture, Out, Ledger, Lifetime))
		{
			return false;
		}
		FGuLiTargetHandle Source;
		Source.Kind = EGuLiTargetKind::Ship;
		Source.AuthorityId = FGuid(51u, 52u, 53u, 54u);
		Source.Generation = 1u;
		FGuLiWingmanHandle Wingman;
		Wingman.Flight.Group = MakeGroup(51u);
		Wingman.Flight.FlightIndex = 0u;
		Wingman.MemberIndex = 0u;
		Wingman.EntityGeneration = 1u;
		const TSharedRef<FVirtualTargetState> SourceState = MakeShared<FVirtualTargetState>();
		SourceState->Handle = Source;
		SourceState->Team = EGuLiTeam::Red;
		const TSharedRef<FVirtualTargetState> TargetState = MakeShared<FVirtualTargetState>();
		TargetState->Handle = GuLiCombatTargets::MakeWingmanTargetHandle(Wingman);
		TargetState->Team = EGuLiTeam::Blue;
		TargetState->Location = FVector(400.0, 0.0, 0.0);
		if (!RegisterVirtualTarget(*Ledger, *Lifetime, SourceState)
			|| !RegisterVirtualTarget(*Ledger, *Lifetime, TargetState))
		{
			SetError(Out, TEXT("Could not register projectile replay targets."));
			return false;
		}

		FGuLiShipProjectileLedgerContext Context;
		Context.MatchEpoch = MatchEpoch;
		Context.ShotId = FGuid(0x50524f4au, 51u, 1u, 1u);
		Context.DamageEventId = FGuid(0x50524f4au, 51u, 2u, 2u);
		Context.Source = Source;
		Context.Damage = 25.0f;
		const FGuLiShipProjectileLedgerImpact First =
			GuLiShipProjectileLedger::CommitServerWingmanSweepImpact(
				*WorldFixture.World, Context, FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), 10.0f);
		const FGuLiShipProjectileLedgerImpact Replay =
			GuLiShipProjectileLedger::CommitServerWingmanSweepImpact(
				*WorldFixture.World, Context, FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), 10.0f);
		if (First.Status != EGuLiShipProjectileLedgerImpactStatus::Committed
			|| Replay.Status != EGuLiShipProjectileLedgerImpactStatus::Duplicate
			|| Ledger->GetCommitCount() != 1u || !FMath::IsNearlyEqual(TargetState->Health, 75.0f))
		{
			SetError(Out, TEXT("Repeated physical overlap did not reuse one immutable projectile event."));
			return false;
		}
		Pass(Out, TEXT("PROJECTILE_OVERLAP_REPLAY_SINGLE_COMMIT"),
			EGuLiWingmanQALogStream::BattleCombat,
			TEXT("two segment-overlap callbacks reused one DamageEventId and one health mutation"));
		return true;
	}

	bool RunConcurrentDuplicate(FGuLiWingmanQARoleProbeResult& Out)
	{
		FTransientWorld WorldFixture;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UObject* Lifetime = nullptr;
		if (!InitializeLedger(WorldFixture, Out, Ledger, Lifetime))
		{
			return false;
		}
		FGuLiTargetHandle Source;
		Source.Kind = EGuLiTargetKind::Ship;
		Source.AuthorityId = FGuid(61u, 62u, 63u, 64u);
		Source.Generation = 1u;
		FGuLiWingmanHandle Wingman;
		Wingman.Flight.Group = MakeGroup(61u);
		Wingman.Flight.FlightIndex = 0u;
		Wingman.MemberIndex = 0u;
		Wingman.EntityGeneration = 1u;
		const TSharedRef<FVirtualTargetState> SourceState = MakeShared<FVirtualTargetState>();
		SourceState->Handle = Source;
		SourceState->Team = EGuLiTeam::Red;
		const TSharedRef<FVirtualTargetState> TargetState = MakeShared<FVirtualTargetState>();
		TargetState->Handle = GuLiCombatTargets::MakeWingmanTargetHandle(Wingman);
		TargetState->Team = EGuLiTeam::Blue;
		if (!RegisterVirtualTarget(*Ledger, *Lifetime, SourceState)
			|| !RegisterVirtualTarget(*Ledger, *Lifetime, TargetState))
		{
			SetError(Out, TEXT("Could not register concurrent duplicate targets."));
			return false;
		}
		const FGuLiDamageRequest Request = MakeDamage(61u, Source, FGuLiWingmanHandle{},
			TargetState->Handle, 100.0f);
		// Network callbacks are serialized onto the authoritative game thread. These
		// two captured arrivals share one barrier release and therefore exercise the
		// production idempotency domain without introducing an unsafe second writer.
		const FGuLiDamageCommitResult First = Ledger->CommitDamage(Request);
		const FGuLiDamageCommitResult Second = Ledger->CommitDamage(Request);
		if (First.Status != EGuLiDamageCommitStatus::Committed
			|| Second.Status != EGuLiDamageCommitStatus::Duplicate
			|| Ledger->GetCommitCount() != 1u || Ledger->GetDeathCommitCount() != 1u
			|| Ledger->GetRewardCommitCount() != 1u)
		{
			SetError(Out, TEXT("Barrier-released duplicate arrivals did not serialize to one ledger commit."));
			return false;
		}
		Pass(Out, TEXT("CONCURRENT_DUPLICATE_SINGLE_COMMIT"),
			EGuLiWingmanQALogStream::BattleCombat,
			TEXT("two barrier-released arrivals serialized through one authoritative ledger event"));
		return true;
	}
}
