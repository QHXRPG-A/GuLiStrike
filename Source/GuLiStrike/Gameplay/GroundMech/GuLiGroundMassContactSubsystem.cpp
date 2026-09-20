#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"

#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	constexpr float SnapshotIntervalSeconds = 0.1f;
	constexpr float DefaultBodyRadiusCentimeters = 150.0f;
	constexpr float MaximumEstimatedClientSpeedCentimetersPerSecond = 5000.0f;

	void ResolveBodyBounds(
		const FGuLiSoldierDefinition* Definition,
		const FTransform& LogicalPose,
		const float RadiusCentimeters,
		float& OutBottomZ,
		float& OutTopZ)
	{
		const FBox LocalBounds = Definition
			? Definition->GetModelBoundsCentimeters()
			: FBox(ForceInit);
		if (LocalBounds.IsValid)
		{
			const FTransform LogicalWorldTransform(
				LogicalPose.GetRotation(),
				LogicalPose.GetLocation(),
				FVector::OneVector);
			const FBox WorldBounds = LocalBounds.TransformBy(LogicalWorldTransform);
			if (WorldBounds.IsValid && WorldBounds.Max.Z > WorldBounds.Min.Z)
			{
				OutBottomZ = WorldBounds.Min.Z;
				OutTopZ = WorldBounds.Max.Z;
				return;
			}
		}
		OutBottomZ = LogicalPose.GetLocation().Z;
		OutTopZ = OutBottomZ + FMath::Max(200.0f, RadiusCentimeters * 2.0f);
	}
}

bool UGuLiGroundMassContactSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiGroundMassContactSubsystem::Deinitialize()
{
	SpatialIndex.Reset();
	PreviousClientLocations.Reset();
	PresentationActor.Reset();
	StateReplicator.Reset();
	LastSnapshotWorldSeconds = -1.0;
	LastClientSampleWorldSeconds = -1.0;
	NextRefreshWorldSeconds = 0.0;
	ClientMatchEpoch = 0u;
	Stats = FGuLiGroundMassContactStats{};
	Super::Deinitialize();
}

void UGuLiGroundMassContactSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	UWorld* World = GetWorld();
	if (!World) return;
	const double Now = World->GetTimeSeconds();
	if (LastSnapshotWorldSeconds < 0.0 || Now + UE_DOUBLE_SMALL_NUMBER >= NextRefreshWorldSeconds)
	{
		const bool bFirstSnapshot = LastSnapshotWorldSeconds < 0.0;
		RefreshSnapshot();
		if (bFirstSnapshot)
		{
			NextRefreshWorldSeconds = Now + SnapshotIntervalSeconds;
		}
		else
		{
			NextRefreshWorldSeconds += SnapshotIntervalSeconds;
			if (NextRefreshWorldSeconds <= Now)
				NextRefreshWorldSeconds = Now + SnapshotIntervalSeconds;
		}
	}
}

TStatId UGuLiGroundMassContactSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiGroundMassContactSubsystem, STATGROUP_Tickables);
}

void UGuLiGroundMassContactSubsystem::RefreshSnapshot()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMassContact_RefreshSnapshot);
#if !UE_BUILD_SHIPPING
	const double StartSeconds = FPlatformTime::Seconds();
#endif
	TArray<FGuLiGroundMassBody> Bodies;
	UWorld* World = GetWorld();
	if (!World) return;
	if (UGuLiBattleAuthoritySubsystem* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
	{
		Authority->BuildGroundCollisionSnapshot(Bodies);
	}
	else
	{
		BuildClientSnapshot(Bodies);
	}
	SpatialIndex.Rebuild(Bodies);
	LastSnapshotWorldSeconds = World->GetTimeSeconds();
#if !UE_BUILD_SHIPPING
	const double Milliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	++Stats.SnapshotRefreshes;
	Stats.LastSnapshotMilliseconds = Milliseconds;
	Stats.MaximumSnapshotMilliseconds = FMath::Max(Stats.MaximumSnapshotMilliseconds, Milliseconds);
#endif
}

void UGuLiGroundMassContactSubsystem::BuildClientSnapshot(TArray<FGuLiGroundMassBody>& OutBodies)
{
	OutBodies.Reset();
	UWorld* World = GetWorld();
	if (!World) return;
	if (!PresentationActor.IsValid())
	{
		for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It)
		{
			PresentationActor = *It;
			break;
		}
	}
	if (!StateReplicator.IsValid())
	{
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
		{
			StateReplicator = *It;
			break;
		}
	}
	AGuLiCommanderPresentationActor* Presentation = PresentationActor.Get();
	AGuLiSoldierStateReplicator* Roster = StateReplicator.Get();
	const UGuLiCommanderDataSubsystem* Data = World->GetSubsystem<UGuLiCommanderDataSubsystem>();
	if (!Presentation || !Roster || !Data) return;

	const uint32 MatchEpoch = Roster->GetSnapshotMatchEpoch();
	if (MatchEpoch == 0u) return;
	if (ClientMatchEpoch != MatchEpoch)
	{
		ClientMatchEpoch = MatchEpoch;
		PreviousClientLocations.Reset();
		LastClientSampleWorldSeconds = -1.0;
	}
	const double Now = World->GetTimeSeconds();
	const float SampleSeconds = LastClientSampleWorldSeconds >= 0.0
		? FMath::Clamp(static_cast<float>(Now - LastClientSampleWorldSeconds), 0.001f, 0.25f)
		: 0.0f;
	TMap<uint32, FVector> NextLocations;
	NextLocations.Reserve(Roster->GetItems().Num());
	OutBodies.Reserve(Roster->GetItems().Num());
	for (const FGuLiSoldierStateItem& State : Roster->GetItems())
	{
		if (!State.SoldierId.IsValid() || !State.IsAlive() || State.bPhased) continue;
		FTransform Pose;
		if (!Presentation->TryGetPresentedSoldierTransform(State.SoldierId, Pose)
			|| Pose.ContainsNaN()) continue;
		const FGuLiSoldierDefinition* Definition = Data->FindSoldierDefinition(State.UnitTypeId);
		const float Radius = Definition
			? Definition->GetMassAvoidanceRadius(DefaultBodyRadiusCentimeters)
			: DefaultBodyRadiusCentimeters;
		FGuLiGroundMassBody& Body = OutBodies.AddDefaulted_GetRef();
		Body.SoldierId = State.SoldierId;
		Body.Team = State.Team;
		Body.UnitTypeId = State.UnitTypeId;
		Body.Location = Pose.GetLocation();
		Body.RadiusCentimeters = Radius;
		if (SampleSeconds > 0.0f)
		{
			if (const FVector* Previous = PreviousClientLocations.Find(State.SoldierId.Value))
				Body.Velocity = ((Body.Location - *Previous) / SampleSeconds)
					.GetClampedToMaxSize(MaximumEstimatedClientSpeedCentimetersPerSecond);
		}
		ResolveBodyBounds(Definition, Pose, Radius, Body.BottomZ, Body.TopZ);
		NextLocations.Add(State.SoldierId.Value, Body.Location);
	}
	PreviousClientLocations = MoveTemp(NextLocations);
	LastClientSampleWorldSeconds = Now;
}

float UGuLiGroundMassContactSubsystem::GetSnapshotAgeSeconds() const
{
	const UWorld* World = GetWorld();
	return World && LastSnapshotWorldSeconds >= 0.0
		? FMath::Clamp(static_cast<float>(World->GetTimeSeconds() - LastSnapshotWorldSeconds), 0.0f, 0.1f)
		: 0.0f;
}

void UGuLiGroundMassContactSubsystem::QueryMassBodies(
	const FBox2D& SweptBounds,
	const float MovementSeconds,
	TArray<FGuLiGroundMassBody>& OutBodies)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMassContact_Query);
	// Actor ticks are not ordered after tickable world subsystems. Build once on demand so
	// the first character move cannot cross an empty cache before this subsystem's first tick.
	if (LastSnapshotWorldSeconds < 0.0)
	{
		RefreshSnapshot();
		if (LastSnapshotWorldSeconds >= 0.0)
			NextRefreshWorldSeconds = LastSnapshotWorldSeconds + SnapshotIntervalSeconds;
	}
	int32 RawCandidates = 0;
	SpatialIndex.Query(
		SweptBounds,
		GetSnapshotAgeSeconds(),
		MovementSeconds,
		OutBodies,
		&RawCandidates);
#if !UE_BUILD_SHIPPING
	++Stats.Queries;
	Stats.RawQueryCandidates += static_cast<uint64>(FMath::Max(0, RawCandidates));
#endif
}

bool UGuLiGroundMassContactSubsystem::FindMassBody(
	const FGuLiSoldierId SoldierId,
	FGuLiGroundMassBody& OutBody)
{
	// Owner-only support replication can arrive before either tickable subsystem or the
	// character has issued a broad-phase query on this client.
	if (LastSnapshotWorldSeconds < 0.0)
	{
		RefreshSnapshot();
		if (LastSnapshotWorldSeconds >= 0.0)
			NextRefreshWorldSeconds = LastSnapshotWorldSeconds + SnapshotIntervalSeconds;
	}
	return SpatialIndex.Find(SoldierId, GetSnapshotAgeSeconds(), OutBody);
}

void UGuLiGroundMassContactSubsystem::RecordSideHits(const int32 Count)
{
#if !UE_BUILD_SHIPPING
	if (Count > 0) Stats.SideHits += static_cast<uint64>(Count);
#else
	(void)Count;
#endif
}

void UGuLiGroundMassContactSubsystem::RecordSupportContact()
{
#if !UE_BUILD_SHIPPING
	++Stats.SupportContacts;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UGuLiGroundMassContactSubsystem::TestOnly_SetBodies(
	const TConstArrayView<FGuLiGroundMassBody> Bodies)
{
	SpatialIndex.Rebuild(Bodies);
	LastSnapshotWorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}
#endif
