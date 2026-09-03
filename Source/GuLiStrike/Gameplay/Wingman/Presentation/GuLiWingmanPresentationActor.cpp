// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h"
#include "MassCommonFragments.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"

namespace
{
	FTransform MakeHiddenTransform()
	{
		return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	}

	bool MatchesConfigGroup(
		const FGuLiGroupAbilityConfigSnapshot& Config,
		const FGuLiWingmanGroupHandle& Group)
	{
		return Config.ShipInstanceId == Group.ShipInstanceId
			&& Config.ShipGeneration == Group.ShipGeneration
			&& Config.GroupGeneration == Group.GroupGeneration;
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
	PrimaryActorTick.TickInterval = 0.0f;

	// A scene root is harmless on authority. Render components are deliberately
	// created later, after the World NetMode is known.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	DefaultPresentationMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/GuLiStrike/Wingman/SM_Wingman_Mass.SM_Wingman_Mass")));
}

void AGuLiWingmanPresentationActor::BeginPlay()
{
	Super::BeginPlay();
	if (!EnsureClientResources())
	{
		SetActorTickEnabled(false);
		return;
	}
	EnsureRemoteMassArchetype();
}

void AGuLiWingmanPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetAllGroups();
	RemoteMassArchetype = FMassArchetypeHandle();
	MassEntitySubsystem = nullptr;
	OwnerPool = FGuLiWingmanInstancePool();
	RemotePool = FGuLiWingmanInstancePool();
	Super::EndPlay(EndPlayReason);
}

void AGuLiWingmanPresentationActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	(void)DeltaSeconds;
	if (!GetWorld() || !GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(GetNetMode()))
	{
		return;
	}

	const double LocalNowSeconds = GetWorld()->GetTimeSeconds();
	for (TPair<FGuLiWingmanGroupHandle, FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		TickGroup(Pair.Value, LocalNowSeconds);
	}
	if (bOwnerRenderStateDirty && OwnerInstances)
	{
		OwnerInstances->MarkRenderStateDirty();
	}
	if (bRemoteRenderStateDirty && RemoteInstances)
	{
		RemoteInstances->MarkRenderStateDirty();
	}
	bOwnerRenderStateDirty = false;
	bRemoteRenderStateDirty = false;
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
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AGuLiWingmanPresentationActor>(
		AGuLiWingmanPresentationActor::StaticClass(),
		FTransform::Identity,
		Parameters);
}

bool AGuLiWingmanPresentationActor::EnsureClientResources()
{
	if (!GetWorld() || !GetWorld()->IsGameWorld()
		|| !GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(GetNetMode()))
	{
		return false;
	}
	if (OwnerInstances && RemoteInstances)
	{
		return true;
	}
	if (!OwnerMesh && !DefaultPresentationMesh.IsNull())
	{
		OwnerMesh = DefaultPresentationMesh.LoadSynchronous();
	}

	OwnerInstances = NewObject<UInstancedStaticMeshComponent>(this, TEXT("OwnerWingmanInstances"));
	RemoteInstances = NewObject<UInstancedStaticMeshComponent>(this, TEXT("RemoteWingmanInstances"));
	if (!OwnerInstances || !RemoteInstances)
	{
		OwnerInstances = nullptr;
		RemoteInstances = nullptr;
		return false;
	}

	ConfigureInstanceComponent(*OwnerInstances, OwnerMesh);
	ConfigureInstanceComponent(*RemoteInstances, RemoteMesh ? RemoteMesh.Get() : OwnerMesh.Get());
	OwnerInstances->SetupAttachment(SceneRoot);
	RemoteInstances->SetupAttachment(SceneRoot);
	OwnerInstances->RegisterComponent();
	RemoteInstances->RegisterComponent();
	const int32 ReserveCount = FMath::Max(1, MaximumPresentedGroups) * GULI_WINGMAN_GROUP_SIZE;
	OwnerInstances->PreAllocateInstancesMemory(ReserveCount);
	RemoteInstances->PreAllocateInstancesMemory(ReserveCount);
	return true;
}

void AGuLiWingmanPresentationActor::ConfigureInstanceComponent(
	UInstancedStaticMeshComponent& Component,
	UStaticMesh* Mesh) const
{
	Component.SetMobility(EComponentMobility::Movable);
	Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component.SetGenerateOverlapEvents(false);
	Component.SetCanEverAffectNavigation(false);
	Component.SetIsReplicated(false);
	Component.SetCastShadow(false);
	Component.SetAffectDistanceFieldLighting(false);
	Component.SetAffectDynamicIndirectLighting(false);
	Component.SetVisibleInRayTracing(false);
	Component.SetCullDistances(
		FMath::Max(0, CullDistanceCentimeters),
		FMath::Max(0, CullDistanceCentimeters));
	Component.NumCustomDataFloats = 1;
	Component.SetStaticMesh(Mesh);
}

bool AGuLiWingmanPresentationActor::EnsureRemoteMassArchetype()
{
	if (!GetWorld() || !GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(GetNetMode()))
	{
		return false;
	}
	if (RemoteMassArchetype.IsValid())
	{
		return true;
	}

	if (!MassEntitySubsystem)
	{
		MassEntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	}
	if (!MassEntitySubsystem)
	{
		return false;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	if (EntityManager.IsProcessing())
	{
		return false;
	}
	const TArray<const UScriptStruct*> FragmentAndTagTypes = {
		FTransformFragment::StaticStruct(),
		FGuLiWingmanIdentityFragment::StaticStruct(),
		FGuLiWingmanRemoteMassTag::StaticStruct()
	};
	FMassArchetypeCreationParams Parameters;
	Parameters.DebugName = TEXT("GuLiRemoteWingmanPresentationOnly");
	RemoteMassArchetype = EntityManager.CreateArchetype(
		FragmentAndTagTypes,
		Parameters);
	return RemoteMassArchetype.IsValid();
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
	if (const FGuLiWingmanPresentationGroupRuntime* Existing = Groups.Find(Bundle.Commit.Group))
	{
		if (Bundle.Commit.CutId < Existing->LastBootstrapCutId)
		{
			return false;
		}
		if (Bundle.Commit.CutId == Existing->LastBootstrapCutId)
		{
			return Existing->Role == PresentationRole
				&& Existing->AbilityConfig.SnapshotRevision == Bundle.AbilityConfig.SnapshotRevision
				&& Existing->AbilityConfig.SnapshotHash == Bundle.AbilityConfig.SnapshotHash;
		}
	}

	FGuLiWingmanPresentationGroupRuntime StagedRuntime;
	if (!BuildRuntimeFromBootstrap(Bundle, PresentationRole, LocalReceiptTimeSeconds, StagedRuntime))
	{
		return false;
	}

	FGuLiWingmanPresentationGroupRuntime* Existing = Groups.Find(Bundle.Commit.Group);
	if (!Existing && Groups.Num() >= FMath::Max(1, MaximumPresentedGroups))
	{
		return false;
	}
	const bool bCanReuseBlock = Existing && Existing->Role == PresentationRole
		&& Existing->InstanceBaseIndex != INDEX_NONE;
	const int32 StagedBaseIndex = bCanReuseBlock
		? Existing->InstanceBaseIndex
		: AllocateInstanceBlock(PresentationRole);
	if (StagedBaseIndex == INDEX_NONE)
	{
		return false;
	}
	StagedRuntime.InstanceBaseIndex = StagedBaseIndex;

	if (Existing)
	{
		DestroyRemoteMirrors(*Existing);
		if (!bCanReuseBlock)
		{
			ReleaseInstanceBlock(Existing->Role, Existing->InstanceBaseIndex);
		}
		*Existing = MoveTemp(StagedRuntime);
		TickGroup(*Existing, LocalReceiptTimeSeconds);
	}
	else
	{
		FGuLiWingmanPresentationGroupRuntime& Added = Groups.Add(
			Bundle.Commit.Group,
			MoveTemp(StagedRuntime));
		TickGroup(Added, LocalReceiptTimeSeconds);
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
	for (const FGuLiWingmanRosterEntry& RosterEntry : Bundle.Roster)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(RosterEntry.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot) || Runtime.Tracks[Slot].Handle.IsValid())
		{
			return false;
		}
		FGuLiWingmanPresentationTrack& Track = Runtime.Tracks[Slot];
		Track.Handle = RosterEntry.Wingman;
		Track.bAlive = !RosterEntry.bDead && !DeadHandles.Contains(RosterEntry.Wingman);
	}
	for (const FGuLiWingmanHealthEntry& HealthEntry : Bundle.Health)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(HealthEntry.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot)
			|| Runtime.Tracks[Slot].Handle != HealthEntry.Wingman)
		{
			return false;
		}
		Runtime.Tracks[Slot].bAlive &= HealthEntry.CurrentHealthPermille > 0u;
	}
	for (const FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
	{
		if (!Track.Handle.IsValid())
		{
			return false;
		}
	}

	TArray<FGuLiWingmanAcceptedBatch> SortedAccepted = Bundle.AcceptedSnapshot;
	SortedAccepted.Sort([](const FGuLiWingmanAcceptedBatch& Lhs, const FGuLiWingmanAcceptedBatch& Rhs)
	{
		if (Lhs.ServerAcceptedTimeSeconds != Rhs.ServerAcceptedTimeSeconds)
		{
			return Lhs.ServerAcceptedTimeSeconds < Rhs.ServerAcceptedTimeSeconds;
		}
		return Lhs.StateRef.AcceptedSequence < Rhs.StateRef.AcceptedSequence;
	});
	for (const FGuLiWingmanAcceptedBatch& AcceptedBatch : SortedAccepted)
	{
		if (!AppendAcceptedBatch(Runtime, AcceptedBatch))
		{
			return false;
		}
	}
	if (Runtime.LastSourceSequence != 0u)
	{
		Runtime.bHasClock = true;
		Runtime.ClockServerSeconds = Runtime.LastSourceTimeSeconds;
		Runtime.ClockLocalReceiptSeconds = LocalReceiptTimeSeconds;
	}
	Runtime.bUsingServerTimeline = PresentationRole == EGuLiWingmanPresentationRole::Remote
		|| Runtime.LastSourceSequence != 0u;
	OutRuntime = MoveTemp(Runtime);
	return true;
}

bool AGuLiWingmanPresentationActor::ValidateAcceptedBatch(
	const FGuLiWingmanPresentationGroupRuntime& Runtime,
	const FGuLiWingmanAcceptedBatch& AcceptedBatch) const
{
	if (!AcceptedBatch.IsWellFormed() || AcceptedBatch.Group != Runtime.Group
		|| AcceptedBatch.AbilitySetRevision != Runtime.AbilityConfig.AbilitySetRevision
		|| AcceptedBatch.FormationCommandRevision != Runtime.AbilityConfig.FormationCommandRevision
		|| AcceptedBatch.FormationDefinitionChecksum != Runtime.AbilityConfig.FormationDefinitionChecksum
		|| !GuLiWingmanPresentationPolicy::IsNewerSequence(
			AcceptedBatch.StateRef.AcceptedSequence,
			Runtime.LastSourceSequence)
		|| (Runtime.LastSourceSequence != 0u
			&& AcceptedBatch.ServerAcceptedTimeSeconds <= Runtime.LastSourceTimeSeconds))
	{
		return false;
	}
	for (const FGuLiWingmanCandidateSample& Sample : AcceptedBatch.Samples)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot) || Runtime.Tracks[Slot].Handle != Sample.Wingman)
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
		FGuLiWingmanPresentationPose Pose;
		if (!GuLiWingmanPresentationPolicy::BuildPose(
			Sample,
			AcceptedBatch.ServerAcceptedTimeSeconds,
			AcceptedBatch.StateRef.AcceptedSequence,
			Pose)
			|| !GuLiWingmanPresentationPolicy::AppendPose(Runtime.Tracks[Slot].Samples, Pose))
		{
			return false;
		}
	}
	Runtime.LastSourceSequence = AcceptedBatch.StateRef.AcceptedSequence;
	Runtime.LastSourceTimeSeconds = AcceptedBatch.ServerAcceptedTimeSeconds;
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

	FGuLiWingmanPresentationGroupRuntime StagedRuntime = *Runtime;
	if (!AppendAcceptedBatch(StagedRuntime, AcceptedBatch))
	{
		return false;
	}
	const double ProgressedServerSeconds = StagedRuntime.bHasClock
		? StagedRuntime.ClockServerSeconds
			+ FMath::Max(0.0, LocalReceiptTimeSeconds - StagedRuntime.ClockLocalReceiptSeconds)
		: AcceptedBatch.ServerAcceptedTimeSeconds;
	StagedRuntime.ClockServerSeconds = FMath::Max(
		AcceptedBatch.ServerAcceptedTimeSeconds,
		ProgressedServerSeconds);
	StagedRuntime.ClockLocalReceiptSeconds = LocalReceiptTimeSeconds;
	StagedRuntime.bHasClock = true;
	StagedRuntime.bUsingServerTimeline = true;
	*Runtime = MoveTemp(StagedRuntime);
	TickGroup(*Runtime, LocalReceiptTimeSeconds);
	return true;
}

bool AGuLiWingmanPresentationActor::ValidateCandidate(
	const FGuLiWingmanPresentationGroupRuntime& Runtime,
	const FGuLiWingmanCandidateBatch& Candidate) const
{
	if (!Candidate.IsWellFormed() || Candidate.Group != Runtime.Group
		|| Candidate.AbilitySetRevision != Runtime.AbilityConfig.AbilitySetRevision
		|| Candidate.FormationCommandRevision != Runtime.AbilityConfig.FormationCommandRevision
		|| Candidate.FormationDefinitionChecksum != Runtime.AbilityConfig.FormationDefinitionChecksum
		|| (!Runtime.bUsingServerTimeline
			&& !GuLiWingmanPresentationPolicy::IsNewerSequence(
				Candidate.CandidateSequence,
				Runtime.LastSourceSequence)))
	{
		return false;
	}
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
		if (!Runtime.Tracks.IsValidIndex(Slot) || Runtime.Tracks[Slot].Handle != Sample.Wingman)
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

	FGuLiWingmanPresentationGroupRuntime StagedRuntime = *Runtime;
	if (StagedRuntime.bUsingServerTimeline)
	{
		for (FGuLiWingmanPresentationTrack& Track : StagedRuntime.Tracks)
		{
			Track.Samples.Reset();
			Track.bHasPresentedTransform = false;
			Track.bInteractable = false;
			Track.Opacity = 0.0f;
		}
		StagedRuntime.LastSourceSequence = 0u;
		StagedRuntime.LastSourceTimeSeconds = 0.0;
		StagedRuntime.bHasClock = false;
		StagedRuntime.bUsingServerTimeline = false;
	}
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Sample.Wingman);
		FGuLiWingmanPresentationPose Pose;
		if (!GuLiWingmanPresentationPolicy::BuildPose(
			Sample,
			LocalTimeSeconds,
			Candidate.CandidateSequence,
			Pose)
			|| !GuLiWingmanPresentationPolicy::AppendPose(StagedRuntime.Tracks[Slot].Samples, Pose))
		{
			return false;
		}
	}
	StagedRuntime.LastSourceSequence = Candidate.CandidateSequence;
	StagedRuntime.LastSourceTimeSeconds = LocalTimeSeconds;
	*Runtime = MoveTemp(StagedRuntime);
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
		|| !FMath::IsFinite(EstimatedServerNowSeconds) || EstimatedServerNowSeconds < 0.0
		|| !FMath::IsFinite(LocalReceiptTimeSeconds) || LocalReceiptTimeSeconds < 0.0)
	{
		return false;
	}
	const double CurrentEstimate = Runtime->bHasClock
		? Runtime->ClockServerSeconds
			+ FMath::Max(0.0, LocalReceiptTimeSeconds - Runtime->ClockLocalReceiptSeconds)
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
	const int32 NewBaseIndex = AllocateInstanceBlock(NewRole);
	if (NewBaseIndex == INDEX_NONE)
	{
		return false;
	}

	DestroyRemoteMirrors(*Runtime);
	ReleaseInstanceBlock(Runtime->Role, Runtime->InstanceBaseIndex);
	Runtime->Role = NewRole;
	Runtime->InstanceBaseIndex = NewBaseIndex;
	Runtime->LastSourceSequence = 0u;
	Runtime->LastSourceTimeSeconds = 0.0;
	Runtime->ClockServerSeconds = 0.0;
	Runtime->ClockLocalReceiptSeconds = 0.0;
	Runtime->bHasClock = false;
	Runtime->bUsingServerTimeline = NewRole == EGuLiWingmanPresentationRole::Remote;
	for (FGuLiWingmanPresentationTrack& Track : Runtime->Tracks)
	{
		Track.Samples.Reset();
		Track.bHasPresentedTransform = false;
		Track.bInteractable = false;
		Track.Opacity = 0.0f;
	}
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
	Track->bAlive = bAlive;
	if (!bAlive)
	{
		Track->Samples.Reset();
		Track->bHasPresentedTransform = false;
		Track->bInteractable = false;
		Track->Opacity = 0.0f;
		UpdateRemoteMirror(*Track, FTransform::Identity, false);
	}
	return true;
}

bool AGuLiWingmanPresentationActor::RemoveGroup(const FGuLiWingmanGroupHandle& Group)
{
	FGuLiWingmanPresentationGroupRuntime Runtime;
	if (!Groups.RemoveAndCopyValue(Group, Runtime))
	{
		return false;
	}
	DestroyRemoteMirrors(Runtime);
	ReleaseInstanceBlock(Runtime.Role, Runtime.InstanceBaseIndex);
	return true;
}

void AGuLiWingmanPresentationActor::ResetAllGroups()
{
	for (TPair<FGuLiWingmanGroupHandle, FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		DestroyRemoteMirrors(Pair.Value);
		ReleaseInstanceBlock(Pair.Value.Role, Pair.Value.InstanceBaseIndex);
	}
	Groups.Reset();
}

bool AGuLiWingmanPresentationActor::TryGetPresentedTransform(
	const FGuLiWingmanHandle& Wingman,
	FTransform& OutTransform) const
{
	const FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	if (!Track || !Track->bAlive || !Track->bHasPresentedTransform || Track->Opacity <= 0.0f)
	{
		return false;
	}
	OutTransform = Track->PresentedTransform;
	return true;
}

bool AGuLiWingmanPresentationActor::IsWingmanInteractable(
	const FGuLiWingmanHandle& Wingman) const
{
	const FGuLiWingmanPresentationTrack* Track = FindTrack(Wingman);
	return Track && Track->bAlive && Track->bHasPresentedTransform && Track->bInteractable;
}

void AGuLiWingmanPresentationActor::GetFreshAcceptedTargetPoses(
	const double EstimatedServerNowSeconds,
	const double MaximumAcceptedAgeSeconds,
	TArray<FGuLiWingmanAcceptedTargetPose>& OutPoses) const
{
	OutPoses.Reset();
	if (!FMath::IsFinite(EstimatedServerNowSeconds) || EstimatedServerNowSeconds < 0.0
		|| !FMath::IsFinite(MaximumAcceptedAgeSeconds) || MaximumAcceptedAgeSeconds < 0.0)
	{
		return;
	}

	for (const TPair<FGuLiWingmanGroupHandle, FGuLiWingmanPresentationGroupRuntime>& Pair : Groups)
	{
		const FGuLiWingmanPresentationGroupRuntime& Runtime = Pair.Value;
		if (Runtime.Role != EGuLiWingmanPresentationRole::Remote
			|| !Runtime.bUsingServerTimeline || !Runtime.LeaseOwnerPlayerGuid.IsValid())
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
			const double AgeSeconds = EstimatedServerNowSeconds - AcceptedPose.SourceTimeSeconds;
			if (!FMath::IsFinite(AgeSeconds) || AgeSeconds < -0.05
				|| AgeSeconds > MaximumAcceptedAgeSeconds
				|| AcceptedPose.Location.ContainsNaN() || !AcceptedPose.Rotation.IsNormalized())
			{
				continue;
			}

			FGuLiWingmanAcceptedTargetPose& TargetPose = OutPoses.AddDefaulted_GetRef();
			TargetPose.Wingman = Track.Handle;
			TargetPose.LeaseOwnerPlayerGuid = Runtime.LeaseOwnerPlayerGuid;
			TargetPose.Transform = FTransform(AcceptedPose.Rotation, AcceptedPose.Location);
			TargetPose.ServerAcceptedTimeSeconds = AcceptedPose.SourceTimeSeconds;
			TargetPose.AcceptedSequence = AcceptedPose.Sequence;
		}
	}
}

void AGuLiWingmanPresentationActor::ConfigurePresentationMeshes(
	UStaticMesh* InOwnerMesh,
	UStaticMesh* InRemoteMesh)
{
	OwnerMesh = InOwnerMesh;
	RemoteMesh = InRemoteMesh;
	if (OwnerInstances)
	{
		OwnerInstances->SetStaticMesh(OwnerMesh);
	}
	if (RemoteInstances)
	{
		RemoteInstances->SetStaticMesh(RemoteMesh ? RemoteMesh.Get() : OwnerMesh.Get());
	}
}

FGuLiWingmanPresentationTrack* AGuLiWingmanPresentationActor::FindTrack(
	const FGuLiWingmanHandle& Wingman)
{
	FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(Wingman.Flight.Group);
	const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Wingman);
	return Runtime && Runtime->Tracks.IsValidIndex(Slot) && Runtime->Tracks[Slot].Handle == Wingman
		? &Runtime->Tracks[Slot]
		: nullptr;
}

const FGuLiWingmanPresentationTrack* AGuLiWingmanPresentationActor::FindTrack(
	const FGuLiWingmanHandle& Wingman) const
{
	const FGuLiWingmanPresentationGroupRuntime* Runtime = Groups.Find(Wingman.Flight.Group);
	const int32 Slot = GuLiWingmanPresentationPolicy::GetStableMemberSlot(Wingman);
	return Runtime && Runtime->Tracks.IsValidIndex(Slot) && Runtime->Tracks[Slot].Handle == Wingman
		? &Runtime->Tracks[Slot]
		: nullptr;
}

FGuLiWingmanInstancePool& AGuLiWingmanPresentationActor::GetInstancePool(
	const EGuLiWingmanPresentationRole PresentationRole)
{
	return PresentationRole == EGuLiWingmanPresentationRole::Owner ? OwnerPool : RemotePool;
}

const FGuLiWingmanInstancePool& AGuLiWingmanPresentationActor::GetInstancePool(
	const EGuLiWingmanPresentationRole PresentationRole) const
{
	return PresentationRole == EGuLiWingmanPresentationRole::Owner ? OwnerPool : RemotePool;
}

UInstancedStaticMeshComponent* AGuLiWingmanPresentationActor::GetInstanceComponent(
	const EGuLiWingmanPresentationRole PresentationRole) const
{
	return PresentationRole == EGuLiWingmanPresentationRole::Owner ? OwnerInstances.Get() : RemoteInstances.Get();
}

int32 AGuLiWingmanPresentationActor::AllocateInstanceBlock(
	const EGuLiWingmanPresentationRole PresentationRole)
{
	UInstancedStaticMeshComponent* Component = GetInstanceComponent(PresentationRole);
	if (!Component)
	{
		return INDEX_NONE;
	}
	FGuLiWingmanInstancePool& Pool = GetInstancePool(PresentationRole);
	if (!Pool.FreeBlockBaseIndices.IsEmpty())
	{
		const int32 ReusedBase = Pool.FreeBlockBaseIndices.Pop(EAllowShrinking::No);
		for (int32 Offset = 0; Offset < GULI_WINGMAN_GROUP_SIZE; ++Offset)
		{
			UpdateInstance(PresentationRole, ReusedBase + Offset, MakeHiddenTransform(), 0.0f);
		}
		return ReusedBase;
	}

	const int32 BaseIndex = Pool.CachedTransforms.Num();
	const FTransform HiddenTransform = MakeHiddenTransform();
	int32 AddedCount = 0;
	for (int32 Offset = 0; Offset < GULI_WINGMAN_GROUP_SIZE; ++Offset)
	{
		const int32 ExpectedIndex = BaseIndex + Offset;
		const int32 InstanceIndex = Component->AddInstance(HiddenTransform, true);
		if (InstanceIndex != ExpectedIndex)
		{
			for (int32 Rollback = 0; Rollback < AddedCount; ++Rollback)
			{
				Component->RemoveInstance(Component->GetInstanceCount() - 1);
			}
			return INDEX_NONE;
		}
		Pool.CachedTransforms.Add(HiddenTransform);
		Pool.CachedOpacities.Add(0.0f);
		Component->SetCustomDataValue(InstanceIndex, 0, 0.0f, false);
		++AddedCount;
	}
	if (PresentationRole == EGuLiWingmanPresentationRole::Owner)
	{
		bOwnerRenderStateDirty = true;
	}
	else
	{
		bRemoteRenderStateDirty = true;
	}
	return BaseIndex;
}

void AGuLiWingmanPresentationActor::ReleaseInstanceBlock(
	const EGuLiWingmanPresentationRole PresentationRole,
	const int32 BaseIndex)
{
	if (BaseIndex == INDEX_NONE)
	{
		return;
	}
	FGuLiWingmanInstancePool& Pool = GetInstancePool(PresentationRole);
	if (!Pool.CachedTransforms.IsValidIndex(BaseIndex)
		|| !Pool.CachedTransforms.IsValidIndex(BaseIndex + GULI_WINGMAN_GROUP_SIZE - 1))
	{
		return;
	}
	for (int32 Offset = 0; Offset < GULI_WINGMAN_GROUP_SIZE; ++Offset)
	{
		UpdateInstance(PresentationRole, BaseIndex + Offset, MakeHiddenTransform(), 0.0f);
	}
	if (!Pool.FreeBlockBaseIndices.Contains(BaseIndex))
	{
		Pool.FreeBlockBaseIndices.Add(BaseIndex);
	}
}

bool AGuLiWingmanPresentationActor::UpdateInstance(
	const EGuLiWingmanPresentationRole PresentationRole,
	const int32 InstanceIndex,
	const FTransform& Transform,
	const float Opacity)
{
	UInstancedStaticMeshComponent* Component = GetInstanceComponent(PresentationRole);
	FGuLiWingmanInstancePool& Pool = GetInstancePool(PresentationRole);
	if (!Component || !Pool.CachedTransforms.IsValidIndex(InstanceIndex)
		|| !Pool.CachedOpacities.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	const float SafeOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	FTransform DesiredTransform = Transform;
	if (SafeOpacity <= 0.0f)
	{
		DesiredTransform.SetScale3D(FVector::ZeroVector);
	}
	const bool bTransformChanged = !Pool.CachedTransforms[InstanceIndex].Equals(DesiredTransform, 0.01f);
	const bool bOpacityChanged = !FMath::IsNearlyEqual(
		Pool.CachedOpacities[InstanceIndex],
		SafeOpacity,
		KINDA_SMALL_NUMBER);
	if (!bTransformChanged && !bOpacityChanged)
	{
		return true;
	}
	if (bTransformChanged && !Component->UpdateInstanceTransform(
		InstanceIndex,
		DesiredTransform,
		true,
		false,
		true))
	{
		return false;
	}
	if (bOpacityChanged && !Component->SetCustomDataValue(InstanceIndex, 0, SafeOpacity, false))
	{
		return false;
	}
	Pool.CachedTransforms[InstanceIndex] = DesiredTransform;
	Pool.CachedOpacities[InstanceIndex] = SafeOpacity;
	if (PresentationRole == EGuLiWingmanPresentationRole::Owner)
	{
		bOwnerRenderStateDirty = true;
	}
	else
	{
		bRemoteRenderStateDirty = true;
	}
	return true;
}

void AGuLiWingmanPresentationActor::TickGroup(
	FGuLiWingmanPresentationGroupRuntime& Runtime,
	const double LocalNowSeconds)
{
	if (Runtime.InstanceBaseIndex == INDEX_NONE)
	{
		return;
	}
	const bool bCanEvaluate = !Runtime.bUsingServerTimeline || Runtime.bHasClock;
	const double EvaluationNowSeconds = Runtime.bUsingServerTimeline
		? Runtime.ClockServerSeconds
			+ FMath::Max(0.0, LocalNowSeconds - Runtime.ClockLocalReceiptSeconds)
		: LocalNowSeconds;
	const double RenderTimeSeconds = Runtime.bUsingServerTimeline
		? EvaluationNowSeconds - static_cast<double>(FMath::Clamp(
			InterpolationBackTimeSeconds,
			0.0f,
			static_cast<float>(GuLiWingmanPresentationPolicy::MaximumExtrapolationSeconds)))
		: EvaluationNowSeconds;

	for (int32 Slot = 0; Slot < Runtime.Tracks.Num(); ++Slot)
	{
		FGuLiWingmanPresentationTrack& Track = Runtime.Tracks[Slot];
		FGuLiWingmanPresentationEvaluation Evaluation;
		if (bCanEvaluate && Track.bAlive)
		{
			Evaluation = GuLiWingmanPresentationPolicy::Evaluate(
				Track.Samples,
				RenderTimeSeconds,
				EvaluationNowSeconds);
		}
		Track.Opacity = Evaluation.Opacity;
		Track.bInteractable = Evaluation.bInteractable;
		Track.bHasPresentedTransform = Evaluation.bVisible;
		if (Evaluation.bVisible)
		{
			Track.PresentedTransform = Evaluation.Transform;
		}
		UpdateInstance(
			Runtime.Role,
			Runtime.InstanceBaseIndex + Slot,
			Evaluation.Transform,
			Evaluation.Opacity);
		if (Runtime.Role == EGuLiWingmanPresentationRole::Remote)
		{
			UpdateRemoteMirror(
				Track,
				Evaluation.Transform,
				Track.bAlive && Evaluation.bInteractable);
		}
	}
}

void AGuLiWingmanPresentationActor::DestroyRemoteMirrors(
	FGuLiWingmanPresentationGroupRuntime& Runtime)
{
	if (!MassEntitySubsystem)
	{
		for (FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
		{
			Track.RemoteMirrorEntity = FMassEntityHandle();
			Track.bRemoteMirrorUpdatePending = false;
			Track.bPendingRemoteMirrorInteractable = false;
		}
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	TArray<FMassEntityHandle> Entities;
	for (FGuLiWingmanPresentationTrack& Track : Runtime.Tracks)
	{
		if (EntityManager.IsEntityValid(Track.RemoteMirrorEntity))
		{
			Entities.Add(Track.RemoteMirrorEntity);
		}
		Track.RemoteMirrorEntity = FMassEntityHandle();
		Track.bRemoteMirrorUpdatePending = false;
		Track.bPendingRemoteMirrorInteractable = false;
	}
	if (!Entities.IsEmpty())
	{
		if (EntityManager.IsProcessing())
		{
			EntityManager.Defer().DestroyEntities(MoveTemp(Entities));
		}
		else
		{
			EntityManager.BatchDestroyEntities(Entities);
		}
	}
}

void AGuLiWingmanPresentationActor::UpdateRemoteMirror(
	FGuLiWingmanPresentationTrack& Track,
	const FTransform& Transform,
	const bool bInteractable)
{
	Track.PendingRemoteMirrorTransform = bInteractable ? Transform : MakeHiddenTransform();
	Track.bPendingRemoteMirrorInteractable = bInteractable;
	Track.bRemoteMirrorUpdatePending = true;
	FlushRemoteMirrorUpdate(Track);
}

void AGuLiWingmanPresentationActor::FlushRemoteMirrorUpdate(
	FGuLiWingmanPresentationTrack& Track)
{
	if (!Track.bRemoteMirrorUpdatePending)
	{
		return;
	}
	if (!MassEntitySubsystem)
	{
		MassEntitySubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UMassEntitySubsystem>()
			: nullptr;
	}
	if (!MassEntitySubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	if (EntityManager.IsProcessing())
	{
		return;
	}
	if (!EnsureRemoteMassArchetype())
	{
		return;
	}
	if (!EntityManager.IsEntityValid(Track.RemoteMirrorEntity))
	{
		Track.RemoteMirrorEntity = FMassEntityHandle();
		if (!Track.bPendingRemoteMirrorInteractable)
		{
			Track.bRemoteMirrorUpdatePending = false;
			return;
		}
		Track.RemoteMirrorEntity = EntityManager.CreateEntity(RemoteMassArchetype);
		if (!EntityManager.IsEntityValid(Track.RemoteMirrorEntity))
		{
			Track.RemoteMirrorEntity = FMassEntityHandle();
			return;
		}
		EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(
			Track.RemoteMirrorEntity).Handle = Track.Handle;
	}
	// Remote archetype contains no guidance, avoidance, dynamics or weapon state.
	// Presentation is therefore its only Transform writer.
	EntityManager.GetFragmentDataChecked<FTransformFragment>(
		Track.RemoteMirrorEntity).SetTransform(Track.PendingRemoteMirrorTransform);
	Track.bRemoteMirrorUpdatePending = false;
}
