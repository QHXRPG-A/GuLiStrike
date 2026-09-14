// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanPresentationActor)

namespace
{
	EGuLiTeam ResolveGroupTeam(UWorld* World, const FGuLiWingmanGroupHandle& Group)
	{
		for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
		{
			const FGuLiGroupAbilityConfigSnapshot& Config = It->GetGroupAbilityConfig();
			if (Config.ShipInstanceId == Group.ShipInstanceId
				&& Config.ShipGeneration == Group.ShipGeneration)
			{
				return It->GetCombatHealthComponent()->GetCombatTeam();
			}
		}
		return EGuLiTeam::Unassigned;
	}

	bool MatchesConfigGroup(
		const FGuLiGroupAbilityConfigSnapshot& Config,
		const FGuLiWingmanGroupHandle& Group)
	{
		return Config.ShipInstanceId == Group.ShipInstanceId
			&& Config.ShipGeneration == Group.ShipGeneration
			&& Config.GroupGeneration == Group.GroupGeneration;
	}

	uint32 GetSourceSequence(
		const FGuLiWingmanPresentationGroupRuntime& Runtime,
		const uint8 FlightIndex)
	{
		return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT
			? Runtime.LastSourceSequenceByFlight[FlightIndex]
			: Runtime.LastSourceSequence;
	}

	double GetSourceTimeSeconds(
		const FGuLiWingmanPresentationGroupRuntime& Runtime,
		const uint8 FlightIndex)
	{
		return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT
			? Runtime.LastSourceTimeSecondsByFlight[FlightIndex]
			: Runtime.LastSourceTimeSeconds;
	}

	void ResetSourceTimeline(FGuLiWingmanPresentationGroupRuntime& Runtime)
	{
		for (uint32& Sequence : Runtime.LastSourceSequenceByFlight)
		{
			Sequence = 0u;
		}
		for (double& TimeSeconds : Runtime.LastSourceTimeSecondsByFlight)
		{
			TimeSeconds = 0.0;
		}
		Runtime.LastSourceSequence = 0u;
		Runtime.LastSourceTimeSeconds = 0.0;
	}

	void RecordSourceTimeline(
		FGuLiWingmanPresentationGroupRuntime& Runtime,
		const uint8 FlightIndex,
		const uint32 Sequence,
		const double TimeSeconds)
	{
		if (FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
		{
			Runtime.LastSourceSequenceByFlight[FlightIndex] = Sequence;
			Runtime.LastSourceTimeSecondsByFlight[FlightIndex] = TimeSeconds;
		}
		if (Runtime.LastSourceSequence == 0u
			|| TimeSeconds >= Runtime.LastSourceTimeSeconds)
		{
			Runtime.LastSourceSequence = Sequence;
			Runtime.LastSourceTimeSeconds = TimeSeconds;
		}
	}

}

AGuLiWingmanPresentationActor::AGuLiWingmanPresentationActor()
{
	bReplicates = false;
	bNetLoadOnClient = false;
	SetReplicateMovement(false);
	SetActorEnableCollision(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	DefaultPresentationMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Wingman/SM_Wingman_Mass.SM_Wingman_Mass")));
}

void AGuLiWingmanPresentationActor::BeginPlay()
{
	Super::BeginPlay();
	if (!EnsureClientResources())
	{
		SetActorTickEnabled(false);
		return;
	}
	if (UGuLiCombatEffectPresentationSubsystem* Effects =
		GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
	{
		Effects->RegisterPoseResolver(EGuLiTargetKind::Wingman, this,
			[this](const FGuLiTargetHandle& Target, FTransform& Pose, int32& Type)
			{
				Type = 0;
				for (const auto& Pair : Groups)
				{
					for (const FGuLiWingmanPresentationTrack& Track : Pair.Value.Tracks)
					{
						if (GuLiCombatTargets::MakeWingmanTargetHandle(Track.Handle) == Target)
						{
							return TryGetPresentedTransform(Track.Handle, Pose);
						}
					}
				}
				return false;
			});
	}
}

void AGuLiWingmanPresentationActor::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		if (UGuLiCombatEffectPresentationSubsystem* Effects =
			GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		{
			Effects->UnregisterPoseResolver(EGuLiTargetKind::Wingman, this);
		}
	}
	ResetAllGroups();
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : RemotePawnPool)
	{
		if (AGuLiWingmanPawn* Pawn = Entry.Get())
		{
			Pawn->Destroy();
		}
	}
	RemotePawnPool.Reset();
	Super::EndPlay(EndPlayReason);
}

void AGuLiWingmanPresentationActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld()
		|| !GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(GetNetMode()))
	{
		return;
	}
	const double LocalNowSeconds = GetWorld()->GetTimeSeconds();
	for (TPair<FGuLiWingmanGroupHandle, FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		TickGroup(Pair.Value, LocalNowSeconds);
	}
	RefreshActorAllocation();
}

AGuLiWingmanPresentationActor* AGuLiWingmanPresentationActor::FindOrSpawn(UWorld* World)
{
	if (!World || !World->IsGameWorld()
		|| !GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(World->GetNetMode()))
	{
		return nullptr;
	}
	for (TActorIterator<AGuLiWingmanPresentationActor> It(World); It; ++It)
	{
		return *It;
	}
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AGuLiWingmanPresentationActor>(
		AGuLiWingmanPresentationActor::StaticClass(), FTransform::Identity, Parameters);
}

bool AGuLiWingmanPresentationActor::EnsureClientResources()
{
	if (!GetWorld() || !GetWorld()->IsGameWorld()
		|| !GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(GetNetMode()))
	{
		return false;
	}
	if (!OwnerMesh && !DefaultPresentationMesh.IsNull())
	{
		OwnerMesh = DefaultPresentationMesh.LoadSynchronous();
	}
	if (!RemoteMesh)
	{
		RemoteMesh = OwnerMesh;
	}
	return OwnerMesh != nullptr;
}

bool AGuLiWingmanPresentationActor::ApplyBootstrap(
	const FGuLiWingmanBootstrapBundle& Bundle,
	const bool bLocallyOwned,
	const double LocalReceiptTimeSeconds)
{
	if (!FMath::IsFinite(LocalReceiptTimeSeconds) || LocalReceiptTimeSeconds < 0.0
		|| !EnsureClientResources() || !Bundle.IsWellFormed())
	{
		return false;
	}
	const EGuLiWingmanPresentationRole PresentationRole = bLocallyOwned
		? EGuLiWingmanPresentationRole::Owner
		: EGuLiWingmanPresentationRole::Remote;
	if (const FGuLiWingmanPresentationGroupRuntime* Existing =
		Groups.Find(Bundle.Commit.Group))
	{
		if (Bundle.Commit.CutId < Existing->LastBootstrapCutId)
		{
			return false;
		}
		if (Bundle.Commit.CutId == Existing->LastBootstrapCutId)
		{
			const bool bSameConfig = Existing->AbilityConfig.SnapshotRevision
					== Bundle.AbilityConfig.SnapshotRevision
				&& Existing->AbilityConfig.SnapshotHash
					== Bundle.AbilityConfig.SnapshotHash;
			if (!bSameConfig)
			{
				return false;
			}
			return Existing->Role == PresentationRole
				|| (Existing->Role == EGuLiWingmanPresentationRole::Remote
					&& PresentationRole == EGuLiWingmanPresentationRole::Owner
					&& SetGroupRole(Bundle.Commit.Group, PresentationRole));
		}
	}
	FGuLiWingmanPresentationGroupRuntime Staged;
	if (!BuildRuntimeFromBootstrap(Bundle, PresentationRole,
		LocalReceiptTimeSeconds, Staged))
	{
		return false;
	}
	if (FGuLiWingmanPresentationGroupRuntime* Existing =
		Groups.Find(Bundle.Commit.Group))
	{
		// Reliable membership changes recover a lost cosmetic death cue, but initial snapshots never replay deaths.
		if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
		{
			for (int32 Slot = 0; Slot < Existing->Tracks.Num() && Slot < Staged.Tracks.Num(); ++Slot)
			{
				const auto& Before = Existing->Tracks[Slot];
				const auto& After = Staged.Tracks[Slot];
				if (Before.bAlive && Before.bHasPresentedTransform
					&& ((!After.bAlive && Before.Handle == After.Handle)
						|| (Bundle.bActiveRosterRefresh && Before.Handle != After.Handle)))
					Feedback->ApplyWingmanFeedback(Before.Handle, Before.PresentedTransform.GetLocation(), true);
			}
		}
		if (Existing->Role == Staged.Role)
		{
			Staged.PlaybackTimeByFlight = Existing->PlaybackTimeByFlight;
			Staged.PlaybackLocalTimeByFlight = Existing->PlaybackLocalTimeByFlight;
			Staged.PlaybackStartedByFlight = Existing->PlaybackStartedByFlight;
			for (int32 Slot = 0; Slot < Staged.Tracks.Num(); ++Slot)
			{
				FGuLiWingmanPresentationTrack& ExistingTrack = Existing->Tracks[Slot];
				FGuLiWingmanPresentationTrack& StagedTrack = Staged.Tracks[Slot];
				if (!ExistingTrack.bAlive || !StagedTrack.bAlive
					|| ExistingTrack.Handle != StagedTrack.Handle)
				{
					continue;
				}

				const bool bNewDisplacement = StagedTrack.LastRebasedSequence != 0u
					&& !ExistingTrack.Samples.IsEmpty()
					&& GuLiWingmanPresentationPolicy::IsNewerSequence(
						StagedTrack.LastRebasedSequence, ExistingTrack.Samples.Last().Sequence);
				if (!bNewDisplacement)
				{
					// A roster cut updates membership, not the trajectory of surviving
					// members. Keep the interval currently being rendered and merge new
					// snapshot poses instead of replacing it with a newer first sample.
					TArray<FGuLiWingmanPresentationPose> SnapshotSamples = MoveTemp(StagedTrack.Samples);
					StagedTrack.Samples = MoveTemp(ExistingTrack.Samples);
					StagedTrack.LastRebasedSequence = ExistingTrack.LastRebasedSequence;
					for (const FGuLiWingmanPresentationPose& Pose : SnapshotSamples)
					{
						if (StagedTrack.Samples.IsEmpty()
							|| GuLiWingmanPresentationPolicy::IsNewerSequence(
								Pose.Sequence, StagedTrack.Samples.Last().Sequence))
						{
							GuLiWingmanPresentationPolicy::AppendPose(StagedTrack.Samples, Pose, 32);
						}
					}
				}
				else
				{
					Staged.PlaybackStartedByFlight[StagedTrack.Handle.Flight.FlightIndex] = false;
				}
				StagedTrack.PresentedTransform = ExistingTrack.PresentedTransform;
				StagedTrack.PresentedPawn = ExistingTrack.PresentedPawn;
				StagedTrack.Opacity = ExistingTrack.Opacity;
				StagedTrack.bHasPresentedTransform = ExistingTrack.bHasPresentedTransform;
				StagedTrack.bInteractable = ExistingTrack.bInteractable;
				ExistingTrack.PresentedPawn.Reset();
			}

			for (uint8 FlightIndex = 0; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT;
				++FlightIndex)
			{
				if (Existing->LastSourceTimeSecondsByFlight[FlightIndex]
					>= Staged.LastSourceTimeSecondsByFlight[FlightIndex])
				{
					Staged.PoseReceiptTimeByFlight[FlightIndex] =
						Existing->PoseReceiptTimeByFlight[FlightIndex];
					Staged.LastSourceTimeSecondsByFlight[FlightIndex] =
						Existing->LastSourceTimeSecondsByFlight[FlightIndex];
					Staged.LastSourceSequenceByFlight[FlightIndex] =
						Existing->LastSourceSequenceByFlight[FlightIndex];
				}
			}
			if (Existing->LastSourceTimeSeconds >= Staged.LastSourceTimeSeconds)
			{
				Staged.LastSourceTimeSeconds = Existing->LastSourceTimeSeconds;
				Staged.LastSourceSequence = Existing->LastSourceSequence;
			}

			const double ExistingServerNow = Existing->bHasClock
				? Existing->ClockServerSeconds + FMath::Max(
					0.0, LocalReceiptTimeSeconds - Existing->ClockLocalReceiptSeconds)
				: 0.0;
			const double StagedServerNow = Staged.bHasClock
				? Staged.ClockServerSeconds + FMath::Max(
					0.0, LocalReceiptTimeSeconds - Staged.ClockLocalReceiptSeconds)
				: 0.0;
			if (Existing->bHasClock || Staged.bHasClock)
			{
				Staged.ClockServerSeconds = FMath::Max(
					ExistingServerNow, StagedServerNow);
				Staged.ClockLocalReceiptSeconds = LocalReceiptTimeSeconds;
				Staged.bHasClock = true;
			}
			Staged.bUsingServerTimeline = Existing->bUsingServerTimeline
				|| Staged.bUsingServerTimeline;
			Staged.bPhased = Existing->bPhased;
			Staged.bExternalActionsLocked = Existing->bExternalActionsLocked;
		}
		for (FGuLiWingmanPresentationTrack& Track : Existing->Tracks)
		{
			ReleaseRemotePawn(Track);
		}
		*Existing = MoveTemp(Staged);
	}
	else
	{
		Groups.Add(Bundle.Commit.Group, MoveTemp(Staged));
	}
	return true;
}

bool AGuLiWingmanPresentationActor::BuildRuntimeFromBootstrap(
	const FGuLiWingmanBootstrapBundle& Bundle,
	const EGuLiWingmanPresentationRole PresentationRole,
	const double LocalReceiptTimeSeconds,
	FGuLiWingmanPresentationGroupRuntime& OutRuntime) const
{
	if (!Bundle.IsWellFormed() || !FMath::IsFinite(LocalReceiptTimeSeconds)
		|| !MatchesConfigGroup(Bundle.AbilityConfig, Bundle.Commit.Group))
	{
		return false;
	}
	FGuLiWingmanPresentationGroupRuntime Runtime;
	Runtime.Group = Bundle.Commit.Group;
	Runtime.LeaseOwnerPlayerGuid = Bundle.AuthorityMap[0].LeaseOwnerPlayerGuid;
	Runtime.AbilityConfig = Bundle.AbilityConfig;
	Runtime.Role = PresentationRole;
	Runtime.LastBootstrapCutId = Bundle.Commit.CutId;
	Runtime.Tracks.SetNum(GULI_WINGMAN_GROUP_SIZE);
	TSet<FGuLiWingmanHandle> DeadHandles;
	DeadHandles.Append(Bundle.Dead);
	for (const FGuLiWingmanRosterEntry& Entry : Bundle.Roster)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Entry.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot)
			|| Runtime.Tracks[Slot].Handle.IsValid())
		{
			return false;
		}
		Runtime.Tracks[Slot].Handle = Entry.Wingman;
		Runtime.Tracks[Slot].bAlive =
			!Entry.bDead && !DeadHandles.Contains(Entry.Wingman);
	}
	for (const FGuLiWingmanHealthEntry& Health : Bundle.Health)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Health.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot)
			|| Runtime.Tracks[Slot].Handle != Health.Wingman)
		{
			return false;
		}
		Runtime.Tracks[Slot].bAlive &= Health.CurrentHealthPermille > 0u;
	}
	for (const FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
	{
		if (!Track.Handle.IsValid())
		{
			return false;
		}
	}
	TArray<FGuLiWingmanAcceptedBatch> Accepted = Bundle.AcceptedSnapshot;
	Accepted.Sort([](const FGuLiWingmanAcceptedBatch& A,
		const FGuLiWingmanAcceptedBatch& B)
	{
		if (A.ServerAcceptedTimeSeconds != B.ServerAcceptedTimeSeconds)
		{
			return A.ServerAcceptedTimeSeconds < B.ServerAcceptedTimeSeconds;
		}
		return A.StateRef.AcceptedSequence < B.StateRef.AcceptedSequence;
	});
	for (FGuLiWingmanAcceptedBatch Batch : Accepted)
	{
		Batch.Samples.RemoveAll([&Runtime](const FGuLiWingmanCandidateSample& Sample)
		{
			const int32 Slot =
				GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
			return !Runtime.Tracks.IsValidIndex(Slot)
				|| Runtime.Tracks[Slot].Handle != Sample.Wingman
				|| !Runtime.Tracks[Slot].bAlive;
		});
		if (Batch.Samples.IsEmpty())
		{
			continue;
		}
		uint8 LivingMask = 0;
		for (const auto& Sample : Batch.Samples) { LivingMask |= uint8(1u << Sample.Wingman.MemberIndex); }
		Batch.RebasedMemberMask &= LivingMask;
		Batch.RefreshHash();
		if (!AppendAcceptedBatch(Runtime, Batch))
		{
			return false;
		}
		Runtime.PoseReceiptTimeByFlight[Batch.FlightIndex] = LocalReceiptTimeSeconds;
	}
	if (Runtime.LastSourceSequence != 0u)
	{
		Runtime.bHasClock = true;
		Runtime.ClockServerSeconds = Runtime.LastSourceTimeSeconds;
		Runtime.ClockLocalReceiptSeconds = LocalReceiptTimeSeconds;
	}
	Runtime.bUsingServerTimeline =
		PresentationRole == EGuLiWingmanPresentationRole::Remote
		|| Runtime.LastSourceSequence != 0u;
	OutRuntime = MoveTemp(Runtime);
	return true;
}

bool AGuLiWingmanPresentationActor::ValidateAcceptedBatch(
	const FGuLiWingmanPresentationGroupRuntime& Runtime,
	const FGuLiWingmanAcceptedBatch& AcceptedBatch) const
{
	const uint32 LastSequence = GetSourceSequence(Runtime, AcceptedBatch.FlightIndex);
	const double LastTime = GetSourceTimeSeconds(Runtime, AcceptedBatch.FlightIndex);
	if (!AcceptedBatch.IsWellFormed() || AcceptedBatch.Group != Runtime.Group
		|| AcceptedBatch.AbilitySetRevision != Runtime.AbilityConfig.AbilitySetRevision
		|| AcceptedBatch.FormationCommandRevision
			!= Runtime.AbilityConfig.FormationCommandRevision
		|| AcceptedBatch.FormationDefinitionChecksum
			!= Runtime.AbilityConfig.FormationDefinitionChecksum
		|| !GuLiWingmanPresentationPolicy::IsNewerSequence(
			AcceptedBatch.StateRef.AcceptedSequence, LastSequence)
		|| (LastSequence != 0u && AcceptedBatch.ServerAcceptedTimeSeconds <= LastTime))
	{
		return false;
	}
	for (const FGuLiWingmanCandidateSample& Sample : AcceptedBatch.Samples)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot)
			|| Runtime.Tracks[Slot].Handle != Sample.Wingman
			|| !Runtime.Tracks[Slot].bAlive)
		{
			return false;
		}
	}
	return true;
}

bool AGuLiWingmanPresentationActor::AppendAcceptedBatch(
	FGuLiWingmanPresentationGroupRuntime& Runtime,
	const FGuLiWingmanAcceptedBatch& AcceptedBatch) const
{
	if (!ValidateAcceptedBatch(Runtime, AcceptedBatch))
	{
		return false;
	}
	for (const FGuLiWingmanCandidateSample& Sample : AcceptedBatch.Samples)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
		FGuLiWingmanPresentationTrack& Track = Runtime.Tracks[Slot];
		const bool bRebased = (AcceptedBatch.RebasedMemberMask
			& (1u << Sample.Wingman.MemberIndex)) != 0u;
		if (bRebased)
		{
			// Explicit gameplay displacement starts a new trajectory.
			Track.Samples.Reset();
			Track.LastRebasedSequence = AcceptedBatch.StateRef.AcceptedSequence;
		}
		FGuLiWingmanPresentationPose Pose;
		if (!GuLiWingmanPresentationPolicy::BuildPose(
			Sample, static_cast<double>(AcceptedBatch.StateRef.ClientSimTick)
				* GuLiWingmanSteering::FixedStepSeconds,
			AcceptedBatch.StateRef.AcceptedSequence, Pose))
		{
			return false;
		}
		Pose.ServerAcceptedTimeSeconds = AcceptedBatch.ServerAcceptedTimeSeconds;
		if (!GuLiWingmanPresentationPolicy::AppendPose(Track.Samples, Pose, 32))
		{
			return false;
		}
	}
	RecordSourceTimeline(Runtime, AcceptedBatch.FlightIndex,
		AcceptedBatch.StateRef.AcceptedSequence,
		AcceptedBatch.ServerAcceptedTimeSeconds);
	return true;
}

bool AGuLiWingmanPresentationActor::ApplyAcceptedSnapshot(
	const FGuLiWingmanAcceptedBatch& AcceptedBatch,
	const double LocalReceiptTimeSeconds)
{
	FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(AcceptedBatch.Group);
	if (!Runtime || Runtime->Role != EGuLiWingmanPresentationRole::Remote
		|| !FMath::IsFinite(LocalReceiptTimeSeconds) || LocalReceiptTimeSeconds < 0.0
		|| !ValidateAcceptedBatch(*Runtime, AcceptedBatch))
	{
		return false;
	}
	FGuLiWingmanPresentationGroupRuntime Staged = *Runtime;
	if (!AppendAcceptedBatch(Staged, AcceptedBatch))
	{
		return false;
	}
	const double Progressed = Staged.bHasClock
		? Staged.ClockServerSeconds
			+ FMath::Max(0.0, LocalReceiptTimeSeconds - Staged.ClockLocalReceiptSeconds)
		: AcceptedBatch.ServerAcceptedTimeSeconds;
	Staged.ClockServerSeconds = FMath::Max(
		AcceptedBatch.ServerAcceptedTimeSeconds, Progressed);
	Staged.ClockLocalReceiptSeconds = LocalReceiptTimeSeconds;
	Staged.bHasClock = true;
	Staged.bUsingServerTimeline = true;
	Staged.PoseReceiptTimeByFlight[AcceptedBatch.FlightIndex] = LocalReceiptTimeSeconds;
	if (AcceptedBatch.RebasedMemberMask != 0u)
	{
		Staged.PlaybackStartedByFlight[AcceptedBatch.FlightIndex] = false;
	}
	*Runtime = MoveTemp(Staged);
	TickGroup(*Runtime, LocalReceiptTimeSeconds);
	RefreshActorAllocation();
	return true;
}

bool AGuLiWingmanPresentationActor::ValidateCandidate(
	const FGuLiWingmanPresentationGroupRuntime& Runtime,
	const FGuLiWingmanCandidateBatch& Candidate) const
{
	const uint32 LastSequence = GetSourceSequence(Runtime, Candidate.FlightIndex);
	if (!Candidate.IsWellFormed() || Candidate.Group != Runtime.Group
		|| Candidate.AbilitySetRevision != Runtime.AbilityConfig.AbilitySetRevision
		|| Candidate.FormationCommandRevision
			!= Runtime.AbilityConfig.FormationCommandRevision
		|| Candidate.FormationDefinitionChecksum
			!= Runtime.AbilityConfig.FormationDefinitionChecksum
		|| (!Runtime.bUsingServerTimeline
			&& !GuLiWingmanPresentationPolicy::IsNewerSequence(
				Candidate.CandidateSequence, LastSequence)))
	{
		return false;
	}
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot)
			|| Runtime.Tracks[Slot].Handle != Sample.Wingman)
		{
			return false;
		}
	}
	return true;
}

bool AGuLiWingmanPresentationActor::ApplyOwnerFrame(
	const FGuLiWingmanCandidateBatch& Candidate,
	const double LocalTimeSeconds)
{
	FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(Candidate.Group);
	if (!Runtime || Runtime->Role != EGuLiWingmanPresentationRole::Owner
		|| !FMath::IsFinite(LocalTimeSeconds) || LocalTimeSeconds < 0.0
		|| !ValidateCandidate(*Runtime, Candidate))
	{
		return false;
	}
	if (Runtime->bUsingServerTimeline)
	{
		for (FGuLiWingmanPresentationTrack& Track : Runtime->Tracks)
		{
			Track.Samples.Reset();
			Track.bHasPresentedTransform = false;
			Track.bInteractable = false;
			Track.Opacity = 0.0f;
		}
		ResetSourceTimeline(*Runtime);
		Runtime->bHasClock = false;
		Runtime->bUsingServerTimeline = false;
	}
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
		FGuLiWingmanPresentationPose Pose;
		if (!GuLiWingmanPresentationPolicy::BuildPose(
			Sample, LocalTimeSeconds, Candidate.CandidateSequence, Pose)
			|| !GuLiWingmanPresentationPolicy::AppendPose(
				Runtime->Tracks[Slot].Samples, Pose))
		{
			return false;
		}
	}
	RecordSourceTimeline(*Runtime, Candidate.FlightIndex,
		Candidate.CandidateSequence, LocalTimeSeconds);
	TickGroup(*Runtime, LocalTimeSeconds);
	return true;
}

bool AGuLiWingmanPresentationActor::ObserveServerClock(
	const FGuLiWingmanGroupHandle& Group,
	const double EstimatedServerNowSeconds,
	const double LocalReceiptTimeSeconds)
{
	FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(Group);
	if (!Runtime || !Runtime->bUsingServerTimeline
		|| !FMath::IsFinite(EstimatedServerNowSeconds)
		|| EstimatedServerNowSeconds < 0.0
		|| !FMath::IsFinite(LocalReceiptTimeSeconds)
		|| LocalReceiptTimeSeconds < 0.0)
	{
		return false;
	}
	const double CurrentEstimate = Runtime->bHasClock
		? Runtime->ClockServerSeconds
			+ FMath::Max(0.0,
				LocalReceiptTimeSeconds - Runtime->ClockLocalReceiptSeconds)
		: 0.0;
	if (Runtime->bHasClock && EstimatedServerNowSeconds < CurrentEstimate)
	{
		return false;
	}
	Runtime->ClockServerSeconds = EstimatedServerNowSeconds;
	Runtime->ClockLocalReceiptSeconds = LocalReceiptTimeSeconds;
	Runtime->bHasClock = true;
	return true;
}

bool AGuLiWingmanPresentationActor::SetGroupRole(
	const FGuLiWingmanGroupHandle& Group,
	const EGuLiWingmanPresentationRole NewRole)
{
	FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(Group);
	if (!Runtime)
	{
		return false;
	}
	if (Runtime->Role == NewRole)
	{
		return true;
	}
	for (FGuLiWingmanPresentationTrack& Track : Runtime->Tracks)
	{
		ReleaseRemotePawn(Track);
		Track.Samples.Reset();
		Track.bInteractable = false;
		Track.Opacity = 0.0f;
	}
	Runtime->Role = NewRole;
	Runtime->PlaybackStartedByFlight = {};
	ResetSourceTimeline(*Runtime);
	Runtime->ClockServerSeconds = 0.0;
	Runtime->ClockLocalReceiptSeconds = 0.0;
	Runtime->bHasClock = false;
	Runtime->bUsingServerTimeline =
		NewRole == EGuLiWingmanPresentationRole::Remote;
	return true;
}

bool AGuLiWingmanPresentationActor::SetWingmanAlive(
	const FGuLiWingmanHandle& Wingman,
	const bool bAlive)
{
	FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	if (!Track)
	{
		return false;
	}
	if (Track->bAlive && !bAlive && Track->bHasPresentedTransform)
		if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
			Feedback->ApplyWingmanFeedback(Wingman, Track->PresentedTransform.GetLocation(), true);
	Track->bAlive = bAlive;
	if (!bAlive)
	{
		Track->Samples.Reset();
		Track->bHasPresentedTransform = false;
		Track->bInteractable = false;
		Track->Opacity = 0.0f;
		ReleaseRemotePawn(*Track);
	}
	return true;
}

bool AGuLiWingmanPresentationActor::RemoveGroup(
	const FGuLiWingmanGroupHandle& Group)
{
	FGuLiWingmanPresentationGroupRuntime Runtime;
	if (!Groups.RemoveAndCopyValue(Group, Runtime))
	{
		return false;
	}
	for (FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
	{
		ReleaseRemotePawn(Track);
	}
	return true;
}

void AGuLiWingmanPresentationActor::ResetAllGroups()
{
	for (TPair<FGuLiWingmanGroupHandle, FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		for (FGuLiWingmanPresentationTrack& Track : Pair.Value.Tracks)
		{
			ReleaseRemotePawn(Track);
		}
	}
	Groups.Reset();
}

bool AGuLiWingmanPresentationActor::HasAppliedBootstrap(
	const FGuLiWingmanGroupHandle& Group,
	const uint64 CutId) const
{
	const FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(Group);
	return Runtime && Group.IsValid() && CutId != 0u
		&& Runtime->LastBootstrapCutId == CutId
		&& Runtime->Tracks.Num() == GULI_WINGMAN_GROUP_SIZE;
}

bool AGuLiWingmanPresentationActor::TryGetPresentedTransform(
	const FGuLiWingmanHandle& Wingman,
	FTransform& OutTransform) const
{
	const FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	if (!Track || !Track->bAlive || !Track->bHasPresentedTransform
		|| Track->Opacity <= 0.0f)
	{
		return false;
	}
	if (const AGuLiWingmanPawn* Pawn = Track->PresentedPawn.Get())
	{
		OutTransform = Pawn->GetPresentationTransform();
		return !OutTransform.ContainsNaN();
	}
	OutTransform = Track->PresentedTransform;
	return !OutTransform.ContainsNaN();
}

bool AGuLiWingmanPresentationActor::IsWingmanInteractable(
	const FGuLiWingmanHandle& Wingman) const
{
	const FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	return Track && Track->bAlive && Track->bHasPresentedTransform
		&& Track->bInteractable;
}

AGuLiWingmanPawn* AGuLiWingmanPresentationActor::FindPresentedPawn(
	const FGuLiWingmanHandle& Wingman) const
{
	const FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	AGuLiWingmanPawn* Pawn = Track ? Track->PresentedPawn.Get() : nullptr;
	return Pawn && Pawn->GetWingmanHandle() == Wingman ? Pawn : nullptr;
}

bool AGuLiWingmanPresentationActor::TryGetDestructionMotion(const FGuLiWingmanHandle& Wingman,
	FTransform& OutPose, FVector& OutVelocity) const
{
	const FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	if (!Track || !Track->bHasPresentedTransform) return false;
	OutPose = Track->PresentedTransform;
	OutVelocity = FVector::ZeroVector;
	if (const AGuLiWingmanPawn* Pawn = FindPresentedPawn(Wingman))
	{
		OutPose = Pawn->GetPresentationTransform();
		if (Pawn->IsOwnerSimulationPawn())
		{
			OutVelocity = Pawn->GetRuntimeState().Dynamics.Velocity;
			return !OutPose.ContainsNaN() && !OutVelocity.ContainsNaN();
		}
	}
	if (!Track->Samples.IsEmpty())
	{
		OutVelocity = Track->Samples.Last().Velocity;
		const auto* Group = Groups.Find(Wingman.Flight.Group);
		const double Time = Group ? Group->PlaybackTimeByFlight[Wingman.Flight.FlightIndex] : Track->Samples.Last().SourceTimeSeconds;
		for (int32 Index = 1; Index < Track->Samples.Num(); ++Index)
		{
			const auto& A = Track->Samples[Index - 1];
			const auto& B = Track->Samples[Index];
			if (Time > B.SourceTimeSeconds) continue;
			const double Alpha = FMath::Clamp((Time - A.SourceTimeSeconds) / FMath::Max(static_cast<double>(UE_SMALL_NUMBER), B.SourceTimeSeconds - A.SourceTimeSeconds), 0.0, 1.0);
			OutVelocity = FMath::Lerp(A.Velocity, B.Velocity, Alpha);
			break;
		}
	}
	return !OutPose.ContainsNaN() && !OutVelocity.ContainsNaN();
}

int32 AGuLiWingmanPresentationActor::GetActiveActorCount() const
{
	int32 Count = 0;
	for (const TPair<FGuLiWingmanGroupHandle,
		FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		for (const FGuLiWingmanPresentationTrack& Track : Pair.Value.Tracks)
		{
			Count += Track.PresentedPawn.IsValid() ? 1 : 0;
		}
	}
	return Count;
}

int32 AGuLiWingmanPresentationActor::GetActiveRemoteActorCount() const
{
	int32 Count = 0;
	for (const TPair<FGuLiWingmanGroupHandle,
		FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		for (const FGuLiWingmanPresentationTrack& Track : Pair.Value.Tracks)
		{
			const AGuLiWingmanPawn* Pawn = Track.PresentedPawn.Get();
			Count += Pawn && !Pawn->IsOwnerSimulationPawn() ? 1 : 0;
		}
	}
	return Count;
}

bool AGuLiWingmanPresentationActor::GetPresentationTiming(
	const FGuLiWingmanHandle& Wingman, double& OldestSample, double& LatestSample,
	double& Playback, int32& SampleCount, int64& BootstrapCut) const
{
	const FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	if (!Track || Track->Samples.IsEmpty()) return false;
	const FGuLiWingmanPresentationGroupRuntime& Runtime = Groups.FindChecked(Wingman.Flight.Group);
	OldestSample = Track->Samples[0].SourceTimeSeconds;
	LatestSample = Track->Samples.Last().SourceTimeSeconds;
	Playback = Runtime.PlaybackTimeByFlight[Wingman.Flight.FlightIndex];
	SampleCount = Track->Samples.Num();
	BootstrapCut = static_cast<int64>(Runtime.LastBootstrapCutId);
	return true;
}

void AGuLiWingmanPresentationActor::GetFreshAcceptedTargetPoses(
	const double EstimatedServerNowSeconds,
	const double MaximumAcceptedAgeSeconds,
	TArray<FGuLiWingmanAcceptedTargetPose>& OutPoses) const
{
	OutPoses.Reset();
	if (!FMath::IsFinite(EstimatedServerNowSeconds)
		|| EstimatedServerNowSeconds < 0.0
		|| !FMath::IsFinite(MaximumAcceptedAgeSeconds)
		|| MaximumAcceptedAgeSeconds < 0.0)
	{
		return;
	}
	for (const TPair<FGuLiWingmanGroupHandle,
		FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		const FGuLiWingmanPresentationGroupRuntime& Runtime = Pair.Value;
		if (Runtime.Role != EGuLiWingmanPresentationRole::Remote
			|| !Runtime.bUsingServerTimeline
			|| !Runtime.LeaseOwnerPlayerGuid.IsValid())
		{
			continue;
		}
		for (const FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
		{
			if (!Track.bAlive || !Track.Handle.IsValid() || Track.Samples.IsEmpty())
			{
				continue;
			}
			const FGuLiWingmanPresentationPose& AcceptedPose = Track.Samples.Last();
			const double AgeSeconds =
				EstimatedServerNowSeconds - AcceptedPose.ServerAcceptedTimeSeconds;
			if (!FMath::IsFinite(AgeSeconds) || AgeSeconds < -0.05
				|| AgeSeconds > MaximumAcceptedAgeSeconds
				|| AcceptedPose.Location.ContainsNaN()
				|| !AcceptedPose.Rotation.IsNormalized())
			{
				continue;
			}
			FGuLiWingmanAcceptedTargetPose& TargetPose =
				OutPoses.AddDefaulted_GetRef();
			TargetPose.Wingman = Track.Handle;
			TargetPose.LeaseOwnerPlayerGuid = Runtime.LeaseOwnerPlayerGuid;
			TargetPose.Transform = FTransform(
				AcceptedPose.Rotation, AcceptedPose.Location);
			TargetPose.ServerAcceptedTimeSeconds = AcceptedPose.ServerAcceptedTimeSeconds;
			TargetPose.AcceptedSequence = AcceptedPose.Sequence;
		}
	}
}

void AGuLiWingmanPresentationActor::ConfigurePresentationMeshes(
	UStaticMesh* InOwnerMesh,
	UStaticMesh* InRemoteMesh)
{
	OwnerMesh = InOwnerMesh;
	RemoteMesh = InRemoteMesh ? InRemoteMesh : InOwnerMesh;
	for (TPair<FGuLiWingmanGroupHandle,
		FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		for (FGuLiWingmanPresentationTrack& Track : Pair.Value.Tracks)
		{
			if (AGuLiWingmanPawn* Pawn = Track.PresentedPawn.Get())
			{
				Pawn->ConfigureMesh(Pawn->IsOwnerSimulationPawn()
					? OwnerMesh.Get() : RemoteMesh.Get());
			}
		}
	}
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : RemotePawnPool)
	{
		if (AGuLiWingmanPawn* Pawn = Entry.Get())
		{
			Pawn->ConfigureMesh(RemoteMesh.Get());
		}
	}
}

void AGuLiWingmanPresentationActor::SetGroupExternalControlState(const FGuLiWingmanGroupHandle& Group, bool bPhased, bool bLocked)
{
	if (auto* Runtime = Groups.Find(Group)) { Runtime->bPhased = bPhased; Runtime->bExternalActionsLocked = bLocked; }
}

void AGuLiWingmanPresentationActor::TickGroup(
	FGuLiWingmanPresentationGroupRuntime& Runtime,
	const double LocalNowSeconds)
{
	if (Runtime.Role == EGuLiWingmanPresentationRole::Owner)
	{
		const EGuLiTeam Team = ResolveGroupTeam(GetWorld(), Runtime.Group);
		UGuLiWingmanSimulationSubsystem* Simulation = GetWorld()
			? GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>() : nullptr;
		for (FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
		{
			AGuLiWingmanPawn* Pawn = Track.bAlive && Simulation
				? Simulation->FindOwnedPawn(Track.Handle) : nullptr;
			if (Track.PresentedPawn.Get() != Pawn)
			{
				ReleaseRemotePawn(Track);
				Track.PresentedPawn = Pawn;
			}
			const bool bVisible = Pawn && !Pawn->IsHidden();
			Track.bHasPresentedTransform = bVisible;
			Track.bInteractable = bVisible && Pawn->IsPresentationInteractable();
			Track.Opacity = bVisible ? 1.0f : 0.0f;
			if (bVisible)
			{
				Pawn->GetTeamOutline()->SetOutlineTeam(Team);
				Track.PresentedTransform = Pawn->GetActorTransform();
			}
		}
		return;
	}

	// Public packets may arrive together. Their receipt times do not describe flight
	// motion: preserve the producer's 30 Hz spacing and advance a continuous cursor.
	TStaticArray<double, GULI_WINGMAN_FLIGHT_COUNT> SourceNowByFlight{};
	for (uint8 Flight = 0; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
	{
		const FGuLiWingmanPresentationTrack* ClockTrack = Runtime.Tracks.FindByPredicate(
			[Flight](const FGuLiWingmanPresentationTrack& Track)
			{
				return Track.bAlive && Track.Handle.Flight.FlightIndex == Flight && !Track.Samples.IsEmpty();
			});
		if (!ClockTrack) continue;
		const double SourceNow = ClockTrack->Samples.Last().SourceTimeSeconds
			+ LocalNowSeconds - Runtime.PoseReceiptTimeByFlight[Flight];
		SourceNowByFlight[Flight] = SourceNow;
		const double TargetPlayback = SourceNow - InterpolationBackTimeSeconds;
		double& Playback = Runtime.PlaybackTimeByFlight[Flight];
		if (!Runtime.PlaybackStartedByFlight[Flight])
		{
			Playback = TargetPlayback;
			Runtime.PlaybackStartedByFlight[Flight] = true;
		}
		else
		{
			const double Delta = LocalNowSeconds - Runtime.PlaybackLocalTimeByFlight[Flight];
			Playback += Delta;
			// Small clock-rate adjustments absorb jitter without jumping the playhead.
			Playback += FMath::Clamp(TargetPlayback - Playback, -0.1 * Delta, 0.1 * Delta);
		}
		Runtime.PlaybackLocalTimeByFlight[Flight] = LocalNowSeconds;
	}
	for (FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
	{
		FGuLiWingmanPresentationEvaluation Evaluation;
		if (Track.bAlive && Runtime.bExternalActionsLocked && !Track.Samples.IsEmpty())
		{
			const auto& Pose = Track.Samples.Last();
			Evaluation.Transform = FTransform(Pose.Rotation,Pose.Location);
			Evaluation.Opacity = 1; Evaluation.bVisible = true; Evaluation.bInteractable = !Runtime.bPhased;
		}
		else if (Track.bAlive && !Track.Samples.IsEmpty())
		{
			const uint8 Flight = Track.Handle.Flight.FlightIndex;
			Evaluation = GuLiWingmanPresentationPolicy::Evaluate(
				Track.Samples, Runtime.PlaybackTimeByFlight[Flight], SourceNowByFlight[Flight],
				GuLiWingmanPresentationPolicy::EStalePolicy::RetainLastPose);
		}
		Track.Opacity = Evaluation.Opacity;
		Track.bInteractable = Evaluation.bInteractable;
		Track.bHasPresentedTransform = Evaluation.bVisible;
		if (Evaluation.bVisible)
		{
			Track.PresentedTransform = Evaluation.Transform;
		}
	}
}

void AGuLiWingmanPresentationActor::RefreshActorAllocation()
{
	for (TPair<FGuLiWingmanGroupHandle,
		FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		if (Pair.Value.Role != EGuLiWingmanPresentationRole::Remote)
		{
			continue;
		}
		const EGuLiTeam Team = ResolveGroupTeam(GetWorld(), Pair.Key);
		for (FGuLiWingmanPresentationTrack& Track : Pair.Value.Tracks)
		{
			if (!Track.bAlive || !Track.bHasPresentedTransform || Track.Opacity <= 0.0f)
			{
				ReleaseRemotePawn(Track);
				continue;
			}
			AGuLiWingmanPawn* Pawn = Track.PresentedPawn.Get();
			if (!Pawn)
			{
				Pawn = AcquireRemotePawn(Track.Handle);
				Track.PresentedPawn = Pawn;
			}
			const bool bRebased = !Track.Samples.IsEmpty()
				&& Track.LastRebasedSequence != 0u
				&& Track.Samples.Last().Sequence == Track.LastRebasedSequence;
			Pawn->GetTeamOutline()->SetOutlineTeam(Team);
			Pawn->ApplyRemotePresentation(Track.PresentedTransform,
				Track.Opacity, Track.bInteractable, bRebased);
			Pawn->SetPhaseAppearance(Pair.Value.bPhased);
		}
	}
}

AGuLiWingmanPawn* AGuLiWingmanPresentationActor::AcquireRemotePawn(
	const FGuLiWingmanHandle& Handle)
{
	while (!RemotePawnPool.IsEmpty())
	{
		if (AGuLiWingmanPawn* Pawn =
			RemotePawnPool.Pop(EAllowShrinking::No).Get())
		{
			return Pawn->InitializeRemotePresentation(
				Handle, RemoteMesh ? RemoteMesh.Get() : OwnerMesh.Get())
				? Pawn : nullptr;
		}
	}
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGuLiWingmanPawn* Pawn = GetWorld()->SpawnActor<AGuLiWingmanPawn>(
		AGuLiWingmanPawn::StaticClass(), FTransform::Identity, Parameters);
	if (!Pawn || !Pawn->InitializeRemotePresentation(
		Handle, RemoteMesh ? RemoteMesh.Get() : OwnerMesh.Get()))
	{
		if (Pawn)
		{
			Pawn->Destroy();
		}
		return nullptr;
	}
	return Pawn;
}

void AGuLiWingmanPresentationActor::ReleaseRemotePawn(
	FGuLiWingmanPresentationTrack& Track)
{
	AGuLiWingmanPawn* Pawn = Track.PresentedPawn.Get();
	Track.PresentedPawn.Reset();
	if (!Pawn || Pawn->IsOwnerSimulationPawn())
	{
		return;
	}
	Pawn->ResetForPool();
	RemotePawnPool.AddUnique(Pawn);
}


FGuLiWingmanPresentationTrack* AGuLiWingmanPresentationActor::FindTrack(
	const FGuLiWingmanHandle& Wingman)
{
	FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(Wingman.Flight.Group);
	const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Wingman);
	return Runtime && Runtime->Tracks.IsValidIndex(Slot)
		&& Runtime->Tracks[Slot].Handle == Wingman
		? &Runtime->Tracks[Slot] : nullptr;
}

const FGuLiWingmanPresentationTrack* AGuLiWingmanPresentationActor::FindTrack(
	const FGuLiWingmanHandle& Wingman) const
{
	const FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(
		Wingman.Flight.Group);
	const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Wingman);
	return Runtime && Runtime->Tracks.IsValidIndex(Slot)
		&& Runtime->Tracks[Slot].Handle == Wingman
		? &Runtime->Tracks[Slot] : nullptr;
}
