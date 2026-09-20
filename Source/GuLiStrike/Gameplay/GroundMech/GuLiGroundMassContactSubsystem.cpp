#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool UGuLiGroundMassContactSubsystem::ShouldCreateSubsystem(UObject *Outer) const
{
	const auto *World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiGroundMassContactSubsystem::ResetSamples()
{
	++CacheGeneration;
	Samples.Reset();
	VisualContacts.Reset();
	CurrentSnapshot.Reset();
	LatestSequence = 0;
	LatestSimulationSample = 0.0;
	bClockReady = false;
	ClockSimulationAnchor = ClockLocalAnchor = 0.0;
	NextRefreshWorldSeconds = 0.0;
}

void UGuLiGroundMassContactSubsystem::Deinitialize()
{
	if (auto *Source = NetSync.Get())
		Source->OnPoseChunkReceived.Remove(PoseHandle);
	if (auto *Source = StateReplicator.Get())
		Source->OnRosterDelta.Remove(RosterHandle);
	NetSync.Reset();
	StateReplicator.Reset();
	ResetSamples();
	Super::Deinitialize();
}

void UGuLiGroundMassContactSubsystem::BindSources()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bTestSource)
		return;
#endif
	UWorld *World = GetWorld();
	if (!World)
		return;
	auto *Roster = StateReplicator.Get();
	if (!Roster)
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
		{
			Roster = *It;
			break;
		}
	auto *State = World->GetGameState<AGuLiBattleGameState>();
	const uint32 Epoch = State ? State->GetMatchEpoch() : 0u;
	auto *Controller = World->GetFirstPlayerController();
	auto *Sync = Controller && Controller->IsLocalController()
					 ? Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>()
					 : nullptr;
	const uint32 Connection = Sync ? Sync->GetConnectionGeneration() : 0u;
	const uint32 Generation = Sync ? Sync->GetSyncGeneration() : 0u;
	const bool bClient = World->GetNetMode() == NM_Client;
	const bool bReady =
		Epoch != 0u && (!bClient || (Sync && Roster && Sync->IsConnectionReady() && Sync->IsSoldierStreamReady() &&
									 Roster->GetSnapshotMatchEpoch() == Epoch));
	if (StateReplicator.Get() != Roster || NetSync.Get() != Sync || SourceEpoch != Epoch ||
		ConnectionGeneration != Connection || SyncGeneration != Generation || bSourceReady != bReady)
	{
		if (auto *Old = NetSync.Get())
			Old->OnPoseChunkReceived.Remove(PoseHandle);
		if (auto *Old = StateReplicator.Get())
			Old->OnRosterDelta.Remove(RosterHandle);
		ResetSamples();
		StateReplicator = Roster;
		NetSync = Sync;
		SourceEpoch = Epoch;
		ConnectionGeneration = Connection;
		SyncGeneration = Generation;
		bSourceReady = bReady;
		PoseHandle.Reset();
		RosterHandle.Reset();
		if (Sync)
			PoseHandle = Sync->OnPoseChunkReceived.AddUObject(this, &ThisClass::HandlePoseChunk);
		if (Roster)
			RosterHandle = Roster->OnRosterDelta.AddUObject(this, &ThisClass::HandleRosterDelta);
	}
}

void UGuLiGroundMassContactSubsystem::Tick(float DeltaTime)
{
	BindSources();
	if (GetWorld() && GetWorld()->GetTimeSeconds() >= NextRefreshWorldSeconds)
		RefreshSnapshot();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (auto It = VisualContacts.CreateIterator(); It; ++It)
		if (Now - It.Value().LastTouched > 0.2)
			It.RemoveCurrent();
}

TStatId UGuLiGroundMassContactSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiGroundMassContactSubsystem, STATGROUP_Tickables);
}

bool UGuLiGroundMassContactSubsystem::FillBody(const FGuLiSoldierStateItem &State, FGuLiGroundMassBody &Body) const
{
	if (!State.SoldierId.IsValid() || !State.IsAlive() || State.bPhased)
		return false;
	const auto *Data = GetWorld() ? GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	const auto *Definition = Data ? Data->FindSoldierDefinition(State.UnitTypeId) : nullptr;
	Body.SoldierId = State.SoldierId;
	Body.Team = State.Team;
	Body.UnitTypeId = State.UnitTypeId;
	Body.RadiusCentimeters = Definition ? Definition->GetMassAvoidanceRadius(150.0f) : 150.0f;
	GuLiGroundMassCollision::ResolveBodyBounds(Definition ? Definition->GetModelBoundsCentimeters() : FBox(ForceInit),
											   Body);
	return Body.IsValid();
}

void UGuLiGroundMassContactSubsystem::HandlePoseChunk(const FGuLiSoldierPoseChunk &Chunk)
{
	const auto *Roster = StateReplicator.Get();
	if (!bSourceReady || !Roster || Chunk.AuthorityEpoch != SourceEpoch || Chunk.FrameSequence == 0u ||
		!FMath::IsFinite(Chunk.ServerTimeSeconds) || Chunk.ServerTimeSeconds < 0.0f)
		return;
	if (LatestSequence == 0u || int32(Chunk.FrameSequence - LatestSequence) > 0)
	{
		const auto *PC = GetWorld()->GetFirstPlayerController();
		const auto *PlayerState = PC ? PC->PlayerState.Get() : nullptr;
		const double HalfRTT = PlayerState ? FMath::Max(0.0f, PlayerState->GetPingInMilliseconds()) * 0.0005 : 0.0;
		LatestSequence = Chunk.FrameSequence;
		LatestSimulationSample = FMath::Max(LatestSimulationSample, static_cast<double>(Chunk.ServerTimeSeconds));
		ClockSimulationAnchor = FMath::Max(EstimateSimulationNow(), Chunk.ServerTimeSeconds + HalfRTT);
		ClockLocalAnchor = GetWorld()->GetTimeSeconds();
		bClockReady = true;
	}
	for (const auto &Pose : Chunk.Samples)
	{
		const auto *State = Roster->FindSoldierState(Pose.SoldierId);
		if (!State || !State->IsAlive() || State->bPhased)
			continue;
		if (State->DisplacementFrameFloor && int32(Chunk.FrameSequence - State->DisplacementFrameFloor) < 0)
			continue;
		const auto *Previous = Samples.Find(Pose.SoldierId.Value);
		if (Previous && int32(Chunk.FrameSequence - Previous->Sequence) <= 0)
			continue;
		if (Previous && Chunk.ServerTimeSeconds < Previous->Body.SampleSimulationSeconds)
			continue;
		FSample Sample;
		Sample.Sequence = Chunk.FrameSequence;
		Sample.bTeleport = Pose.IsTeleport();
		Sample.Body.Location = Pose.GetWorldLocationCentimeters();
		Sample.Body.Velocity = Pose.GetVelocityCentimetersPerSecond();
		Sample.Body.Rotation =
			FRotator(0.0f, GuLiCommanderProtocol::DequantizeYawDegrees(Pose.FacingYaw), 0.0f).Quaternion();
		Sample.Body.SampleSimulationSeconds = Chunk.ServerTimeSeconds;
		Sample.Body.DisplacementRevision = State->DisplacementFrameFloor;
		if (Sample.bTeleport && (!Previous || !Previous->bTeleport))
			Sample.Body.DisplacementRevision = Chunk.FrameSequence;
		else if (Sample.bTeleport && Previous)
			Sample.Body.DisplacementRevision = Previous->Body.DisplacementRevision;
		if (FillBody(*State, Sample.Body))
			Samples.Add(Pose.SoldierId.Value, Sample);
	}
}

void UGuLiGroundMassContactSubsystem::HandleRosterDelta(const FGuLiSoldierRosterDelta &Delta)
{
	const auto *Roster = StateReplicator.Get();
	if (!Roster)
		return;
	if (Roster->GetSnapshotMatchEpoch() != SourceEpoch)
	{
		ResetSamples();
		bSourceReady = false;
		return;
	}
	if (Delta.bReset)
	{
		ResetSamples();
		return;
	}
	// Copy only the small lifecycle overlay; historical moves keep their original immutable frame.
	TSharedPtr<FGuLiGroundMassSnapshot> Frame;
	if (CurrentSnapshot)
		Frame = MakeShared<FGuLiGroundMassSnapshot>(*CurrentSnapshot);
	auto Update = [&](FGuLiSoldierId Id)
	{
		const auto *State = Roster->FindSoldierState(Id);
		FGuLiGroundMassBody Body;
		bool bValid = false;
		if (State && State->IsAlive() && !State->bPhased)
		{
			const auto *Sample = Samples.Find(Id.Value);
			if (State->DisplacementFrameFloor &&
				(!Sample || int32(Sample->Sequence - State->DisplacementFrameFloor) < 0))
			{
				Body.Location = State->DisplacementLocation;
				Body.Rotation = FRotator(0, State->DisplacementYaw, 0).Quaternion();
				Body.SampleSimulationSeconds = State->DisplacementSimulationTime;
				Body.DisplacementRevision = State->DisplacementFrameFloor;
				if (FillBody(*State, Body))
				{
					FSample Seed;
					Seed.Body = Body;
					Seed.Sequence = State->DisplacementFrameFloor - 1;
					Samples.Add(Id.Value, Seed);
					bValid = true;
				}
			}
			else if (Sample)
			{
				Body = Sample->Body;
				bValid = FillBody(*State, Body);
			}
		}
		if (!bValid)
		{
			Samples.Remove(Id.Value);
			VisualContacts.Remove(Id.Value);
		}
		if (Frame)
		{
			if (bValid)
				Body = Body.Extrapolated(
					static_cast<float>(FMath::Max(0.0, Frame->SimulationSeconds - Body.SampleSimulationSeconds)));
			Frame->Overrides.Add(Id.Value, bValid ? Body : FGuLiGroundMassBody{});
		}
	};
	for (auto Id : Delta.Removed)
		Update(Id);
	for (auto Id : Delta.Added)
		Update(Id);
	for (const auto &Pair : Delta.Changed)
		if (EnumHasAnyFlags(Pair.Value, EGuLiSoldierStateChange::Life | EGuLiSoldierStateChange::Phase |
											EGuLiSoldierStateChange::Displacement | EGuLiSoldierStateChange::Type |
											EGuLiSoldierStateChange::Team))
			Update(Pair.Key);
	if (Frame)
	{
		TArray<FGuLiGroundMassBody> Bodies;
		for (const auto &Pair : Frame->Overrides)
			if (Pair.Value.IsValid())
				Bodies.Add(Pair.Value);
		auto Index = MakeShared<FGuLiGroundMassSpatialIndex>();
		Index->Rebuild(Bodies, Frame->SimulationSeconds);
		Frame->OverrideIndex = Index;
		CurrentSnapshot = Frame;
	}
}

void UGuLiGroundMassContactSubsystem::RefreshSnapshot()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMassContact_RefreshSnapshot);
	if (!GetWorld())
		return;
	// Keep the 10 Hz deadline across frame jitter, without rebuilding repeatedly after a hitch.
	NextRefreshWorldSeconds = FMath::Max(NextRefreshWorldSeconds + 0.1, GetWorld()->GetTimeSeconds());
#if WITH_DEV_AUTOMATION_TESTS
	if (bTestSource && !StateReplicator.IsValid())
		return;
#endif
	if (!bSourceReady)
		return;
	const double Start = FPlatformTime::Seconds();
	TArray<FGuLiGroundMassBody> Bodies;
	auto Frame = MakeShared<FGuLiGroundMassSnapshot>();
	Frame->CacheGeneration = CacheGeneration;
	if (auto *Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>(); Authority
#if WITH_DEV_AUTOMATION_TESTS
																					 && !bTestSource
#endif
	)
	{
		Authority->BuildGroundCollisionSnapshot(Bodies, Frame->Epoch, Frame->FrameSequence, Frame->SimulationSeconds);
		ClockSimulationAnchor = Frame->SimulationSeconds;
		ClockLocalAnchor = GetWorld()->GetTimeSeconds();
		bClockReady = true;
	}
	else
	{
		Frame->Epoch = SourceEpoch;
		Frame->FrameSequence = LatestSequence;
		Frame->SimulationSeconds = LatestSimulationSample;
		const auto *Roster = StateReplicator.Get();
		if (!Roster)
			return;
		Bodies.Reserve(Samples.Num());
		for (auto &Pair : Samples)
		{
			const auto *State = Roster->FindSoldierState(FGuLiSoldierId(Pair.Key));
			auto Body = Pair.Value.Body;
			if (!State || !FillBody(*State, Body))
				continue;
			Bodies.Add(Body.Extrapolated(
				static_cast<float>(FMath::Max(0.0, Frame->SimulationSeconds - Body.SampleSimulationSeconds))));
		}
	}
	auto Index = MakeShared<FGuLiGroundMassSpatialIndex>();
	Index->Rebuild(Bodies, Frame->SimulationSeconds);
	Frame->Index = Index;
	CurrentSnapshot = Frame;
#if !UE_BUILD_SHIPPING
	++Stats.SnapshotRefreshes;
	Stats.LastSnapshotMilliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
	Stats.MaximumSnapshotMilliseconds = FMath::Max(Stats.MaximumSnapshotMilliseconds, Stats.LastSnapshotMilliseconds);
#endif
}

double UGuLiGroundMassContactSubsystem::EstimateSimulationNow() const
{
	return bClockReady && GetWorld()
			   ? ClockSimulationAnchor + FMath::Max(0.0, GetWorld()->GetTimeSeconds() - ClockLocalAnchor)
			   : 0.0;
}

FGuLiGroundMassMoveContext UGuLiGroundMassContactSubsystem::CaptureMove(float Duration)
{
	BindSources();
	if (!CurrentSnapshot)
		RefreshSnapshot();
	FGuLiGroundMassMoveContext Result;
	Result.Snapshot = CurrentSnapshot;
	Result.Duration = Duration;
	Result.StartSimulationSeconds =
		FMath::Max(CurrentSnapshot ? CurrentSnapshot->SimulationSeconds : 0.0, EstimateSimulationNow() - Duration);
	return Result;
}

void UGuLiGroundMassContactSubsystem::QueryMassBodies(const FGuLiGroundMassMoveContext &Move, const FBox2D &Bounds,
													  double StartSeconds, float Duration,
													  TArray<FGuLiGroundMassBody> &Out)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMassContact_Query);
	Out.Reset();
	int32 Raw = 0;
	if (Move.Snapshot)
		Move.Snapshot->Query(Bounds, StartSeconds, Duration, Out, &Raw);
#if !UE_BUILD_SHIPPING
	++Stats.Queries;
	Stats.RawQueryCandidates += Raw;
#endif
}

void UGuLiGroundMassContactSubsystem::QueryMassBodies(const FBox2D &Bounds, float Duration,
													  TArray<FGuLiGroundMassBody> &Out)
{
	const auto Move = CaptureMove(Duration);
	QueryMassBodies(Move, Bounds, Move.StartSimulationSeconds, Duration, Out);
}

bool UGuLiGroundMassContactSubsystem::FindMassBody(FGuLiSoldierId Id, FGuLiGroundMassBody &Out)
{
	const auto Move = CaptureMove(0.0f);
	return Move.Snapshot && Move.Snapshot->Find(Id, Move.StartSimulationSeconds, Out);
}

void UGuLiGroundMassContactSubsystem::MarkLocalContact(const FGuLiGroundMassBody &Body)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
		return;
	auto &Contact = VisualContacts.FindOrAdd(Body.SoldierId.Value);
	Contact.Pose = FTransform(Body.Rotation, Body.Location);
	Contact.Frame = GFrameCounter;
	Contact.LastTouched = GetWorld()->GetTimeSeconds();
}

void UGuLiGroundMassContactSubsystem::ApplyContactPresentation(FGuLiSoldierId Id, FTransform &InOutPose)
{
	const auto *Contact = VisualContacts.Find(Id.Value);
	if (!Contact)
		return;
	const float Alpha =
		Contact->Frame == GFrameCounter
			? 0.0f
			: FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - Contact->LastTouched) / 0.2), 0.0f, 1.0f);
	const FVector Scale = InOutPose.GetScale3D();
	InOutPose.Blend(Contact->Pose, InOutPose, Alpha);
	InOutPose.SetScale3D(Scale);
}

void UGuLiGroundMassContactSubsystem::RecordSideHits(int32 Count)
{
#if !UE_BUILD_SHIPPING
	Stats.SideHits += Count;
#endif
}
void UGuLiGroundMassContactSubsystem::RecordSupportContact()
{
#if !UE_BUILD_SHIPPING
	++Stats.SupportContacts;
#endif
}
#if WITH_DEV_AUTOMATION_TESTS
void UGuLiGroundMassContactSubsystem::TestOnly_SetBodies(TConstArrayView<FGuLiGroundMassBody> Bodies)
{
	bTestSource = true;
	bSourceReady = true;
	SourceEpoch = 1;
	auto Frame = MakeShared<FGuLiGroundMassSnapshot>();
	Frame->Epoch = 1;
	Frame->FrameSequence = LatestSequence++;
	Frame->CacheGeneration = CacheGeneration;
	Frame->SimulationSeconds = GetWorld()->GetTimeSeconds();
	auto Index = MakeShared<FGuLiGroundMassSpatialIndex>();
	Index->Rebuild(Bodies, Frame->SimulationSeconds);
	Frame->Index = Index;
	CurrentSnapshot = Frame;
	ClockSimulationAnchor = Frame->SimulationSeconds;
	ClockLocalAnchor = GetWorld()->GetTimeSeconds();
	bClockReady = true;
}
void UGuLiGroundMassContactSubsystem::TestOnly_SetRoster(AGuLiSoldierStateReplicator *Roster, uint32 Epoch)
{
	bTestSource = true;
	bSourceReady = true;
	SourceEpoch = Epoch;
	StateReplicator = Roster;
	RosterHandle = Roster->OnRosterDelta.AddUObject(this, &ThisClass::HandleRosterDelta);
}
#endif
