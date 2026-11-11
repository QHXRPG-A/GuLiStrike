// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Engine/World.h"
#include "Gameplay/Navigation/GuLiCircularBoundaries.h"
#include "NavigationSystem.h"

FGuLiObstacleRegionStamp FGuLiDynamicObstacleSnapshot::CaptureRegion(
	const FVector& From, const FVector& To, float Radius) const
{
	FGuLiObstacleRegionStamp Stamp; Stamp.SnapshotRevision = Revision;
	const FVector Padding(Radius, Radius, 0);
	const FIntPoint Min = Cell(From.ComponentMin(To) - Padding), Max = Cell(From.ComponentMax(To) + Padding);
	// Exceptional long connectors remain safe without allocating an unbounded dependency map.
	if ((int64(Max.X)-Min.X+1)*(int64(Max.Y)-Min.Y+1) > 512)
	{ Stamp.bComplete = false; return Stamp; }
	for (int32 X=Min.X; X<=Max.X; ++X) for (int32 Y=Min.Y; Y<=Max.Y; ++Y)
	{
		const FIntPoint Key(X,Y); const auto* Version = CellVersions->Find(Key);
		Stamp.Cells.Add(Key, Version ? *Version : 0);
	}
	return Stamp;
}
bool FGuLiDynamicObstacleSnapshot::IsRegionCurrent(const FGuLiObstacleRegionStamp& Stamp) const
{
	if (!Stamp.bComplete) return Stamp.SnapshotRevision == Revision;
	for (const auto& Pair : Stamp.Cells)
	{
		const auto* Current = CellVersions->Find(Pair.Key);
		if ((Current ? *Current : 0) != Pair.Value) return false;
	}
	return true;
}
void UGuLiDynamicObstacleRegistrySubsystem::DirtyObstacleCells(const FGuLiDynamicObstacle& Obstacle)
{
	const FVector Radius(Obstacle.RadiusCentimeters,Obstacle.RadiusCentimeters,0);
	const auto Min = FGuLiDynamicObstacleSnapshot::Cell(Obstacle.Location-Radius);
	const auto Max = FGuLiDynamicObstacleSnapshot::Cell(Obstacle.Location+Radius);
	const uint64 Version = ++NextCellVersion;
	for (int32 X=Min.X; X<=Max.X; ++X) for (int32 Y=Min.Y; Y<=Max.Y; ++Y)
		CellVersions.Add(FIntPoint(X,Y),Version);
}

bool FGuLiDynamicObstacleSnapshot::IsSegmentClear(const FVector& From,const FVector& To,float Radius,
    bool bAllowEscape,EGuLiUnitMovementLayer Layer,float Clearance,float ContactMargin,uint64 IgnoredSource,bool IncludePlayers) const
{
    check(IsInGameThread());check(Static && Players);
    GuLiCircleAvoidance::FRequest Request;Request.Position=From;
    Request.MinimumZ=FMath::Min(From.Z,To.Z);Request.MaximumZ=FMath::Max(From.Z,To.Z)+AgentHeight;
    Request.LayerMask=Layer==EGuLiUnitMovementLayer::LowAir?2:1;
    Request.AllowInitialEscape=bAllowEscape;Request.Clearance=Clearance;Request.ContactMargin=ContactMargin;
    Request.IgnoredSource=IgnoredSource;
    GuLiCircleAvoidance::FQueryScratch Scratch;GuLiCircleAvoidance::FCounters Counters;
    static const GuLiCircleAvoidance::FIndex Empty;
    return !GuLiCircleAvoidance::Sweep(*Static,IncludePlayers?*Players:Empty,Request,To-From,Radius+Clearance+ContactMargin,false,Scratch,Counters).Blocked;
}

TSharedRef<const FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe> UGuLiDynamicObstacleRegistrySubsystem::GetSnapshot()
{
    check(IsInGameThread());
    if (!Snapshot || Snapshot->Revision!=Revision)
    {
        auto Next=MakeShared<FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe>();
        Next->Revision=Revision;Next->Static=&CircularStatic.GetIndex();Next->Players=&PlayerCircles;
        if (const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
            if (const auto* Data=Nav->GetNavDataForAgentName(TEXT("CommanderSoldier"))) Next->AgentHeight=Data->GetConfig().AgentHeight;
        Next->CellVersions=&CellVersions;Snapshot=Next;
    }
    return Snapshot.ToSharedRef();
}

void UGuLiDynamicObstacleRegistrySubsystem::RefreshPlayerCircle(const FGuLiDynamicObstacle& O)
{
    GuLiCircleAvoidance::FCircle C;C.Id=GuLiCircleAvoidance::PlayerNamespace|O.Handle.Value;
    C.Center=O.Location;C.Radius=O.RadiusCentimeters;C.Version=Revision+1;
    C.MinimumZ=O.Location.Z;C.MaximumZ=O.Location.Z+FMath::Max(1.f,O.HalfHeight*2);
    C.LayerMask=O.Kind==EGuLiDynamicObstacleKind::GroundMech?1:3;C.ContactMargin=2;
    PlayerCircles.Upsert(C);
}

DEFINE_LOG_CATEGORY_STATIC(LogGuLiObstacles, Log, All);

namespace
{
	bool IsValidObstacle(const FGuLiDynamicObstacle& Obstacle)
	{
		return !Obstacle.Location.ContainsNaN()
			&& FMath::IsFinite(Obstacle.RadiusCentimeters)
			&& Obstacle.RadiusCentimeters > 0.0f;
	}
}

bool UGuLiDynamicObstacleRegistrySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiDynamicObstacleRegistrySubsystem::Deinitialize()
{
	if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		Nav->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this,&ThisClass::HandleOreNavigationFinished);
	PendingOreNavigationRegions.Reset();
	Snapshot.Reset();
	CircularStatic.Reset();PlayerCircles.Rebuild({});OrePublications=0;
	CellVersions.Reset(); NextCellVersion = 0;
	StaticRegionChanged.Clear(); RegionChanges.Reset();
	Obstacles.Reset();
	IndexByHandle.Reset();
	NextHandle = 1u;
	Revision = 0u;
	Super::Deinitialize();
}

void UGuLiDynamicObstacleRegistrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
		Nav->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this,&ThisClass::HandleOreNavigationFinished);
}

void UGuLiDynamicObstacleRegistrySubsystem::NotifyStaticGeometryOpened(const FBox& Bounds)
{
	const TGuardValue<bool> Opening(bOpeningStaticRegion,true);
	NotifyStaticGeometryChanged(Bounds);
}

void UGuLiDynamicObstacleRegistrySubsystem::HandleOreNavigationFinished(ANavigationData* NavigationData)
{
	check(IsInGameThread());
	if (NavigationData && !PendingOreNavigationRegions.IsEmpty()) bOreNavigationFinished=true;
	FlushOreNavigationNotifications();
}
void UGuLiDynamicObstacleRegistrySubsystem::FlushOreNavigationNotifications()
{
	check(IsInGameThread());
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!bOreNavigationFinished || !Nav || PendingOreNavigationRegions.IsEmpty() || Nav->HasDirtyAreasQueued()
		|| Nav->GetNumRemainingBuildTasks()>0 || Nav->GetNumRunningBuildTasks()>0) return;
	// A failure observed against the old tiles must be reconsidered after the new tiles are installed.
	// Notification only invalidates local caches; their existing query budgets still govern rebuilding.
	const auto Finished=MoveTemp(PendingOreNavigationRegions);
	PendingOreNavigationRegions.Reset();
	bOreNavigationFinished=false;
	for (const auto& Pair:Finished) NotifyStaticGeometryOpened(Pair.Value);
}

FGuLiDynamicObstacleHandle UGuLiDynamicObstacleRegistrySubsystem::RegisterObstacle(
	const FVector& Location,
	const float RadiusCentimeters)
{
	FGuLiDynamicObstacle Obstacle;
	Obstacle.Location = Location;
	Obstacle.RadiusCentimeters = RadiusCentimeters;
	return RegisterObstacle(Obstacle);
}

FGuLiDynamicObstacleHandle UGuLiDynamicObstacleRegistrySubsystem::RegisterObstacle(
	const FGuLiDynamicObstacle& Obstacle)
{
	if (!IsValidObstacle(Obstacle))
	{
		UE_LOG(LogGuLiObstacles, Verbose, TEXT("Rejected invalid obstacle registration."));
		return {};
	}
	checkf(NextHandle < 0x80000000u, TEXT("Dynamic obstacle handle space exhausted."));
	const FGuLiDynamicObstacleHandle Handle{NextHandle++};
	FGuLiDynamicObstacle Stored = Obstacle;
	Stored.Handle = Handle;
	const int32 Index = Obstacles.Add(MoveTemp(Stored));
	IndexByHandle.Add(Handle.Value, Index);
    RefreshPlayerCircle(Obstacles[Index]);
	DirtyObstacleCells(Obstacle);
	PublishStaticRegion(Obstacle);
	PublishChange();
	return Handle;
}

EGuLiObstacleUpdateResult UGuLiDynamicObstacleRegistrySubsystem::UpdateObstacle(
	const FGuLiDynamicObstacleHandle Handle,
	const FGuLiDynamicObstacle& Obstacle)
{
	if (!IsValidObstacle(Obstacle))
	{
		UE_LOG(LogGuLiObstacles, Verbose, TEXT("Rejected invalid update for obstacle %u; retaining last valid data."), Handle.Value);
		return EGuLiObstacleUpdateResult::InvalidData;
	}
	const int32* Index = IndexByHandle.Find(Handle.Value);
	if (!Index) return EGuLiObstacleUpdateResult::NotFound;
	checkSlow(Obstacles.IsValidIndex(*Index));
	FGuLiDynamicObstacle& Stored = Obstacles[*Index];
	const bool bChanged = !Stored.Location.Equals(Obstacle.Location, 0.1f)
		|| !FMath::IsNearlyEqual(Stored.RadiusCentimeters, Obstacle.RadiusCentimeters, 0.1f)
		|| !FMath::IsNearlyEqual(Stored.HalfHeight, Obstacle.HalfHeight, 0.1f)
		|| Stored.Kind != Obstacle.Kind
		|| Stored.Team != Obstacle.Team;
	if (!bChanged) return EGuLiObstacleUpdateResult::Unchanged;
	DirtyObstacleCells(Stored);
	PublishStaticRegion(Stored);
	Stored.Location = Obstacle.Location;
	Stored.RadiusCentimeters = Obstacle.RadiusCentimeters;
	Stored.Kind = Obstacle.Kind;
	Stored.Team = Obstacle.Team;
	Stored.HalfHeight = Obstacle.HalfHeight;
    RefreshPlayerCircle(Stored);
	DirtyObstacleCells(Stored);
	PublishStaticRegion(Stored);
	PublishChange();
	return EGuLiObstacleUpdateResult::Updated;
}

void UGuLiDynamicObstacleRegistrySubsystem::UnregisterObstacle(
	const FGuLiDynamicObstacleHandle Handle)
{
	check(Handle.IsValid());
	const int32 RemovedIndex = IndexByHandle.FindAndRemoveChecked(Handle.Value);
    PlayerCircles.Remove(GuLiCircleAvoidance::PlayerNamespace|Handle.Value);
	DirtyObstacleCells(Obstacles[RemovedIndex]);
	PublishStaticRegion(Obstacles[RemovedIndex]);
	const int32 LastIndex = Obstacles.Num() - 1;
	if (RemovedIndex != LastIndex)
	{
		const uint32 MovedHandle = Obstacles[LastIndex].Handle.Value;
		Obstacles.RemoveAtSwap(RemovedIndex, 1, EAllowShrinking::No);
		IndexByHandle.FindChecked(MovedHandle) = RemovedIndex;
	}
	else
	{
		Obstacles.RemoveAt(LastIndex, 1, EAllowShrinking::No);
	}
	PublishChange();
}

void UGuLiDynamicObstacleRegistrySubsystem::PublishChange()
{
	Revision = Revision == MAX_uint32 ? 1u : Revision + 1u;
}

void UGuLiDynamicObstacleRegistrySubsystem::PublishStaticRegion(const FGuLiDynamicObstacle& Obstacle)
{
	if (Obstacle.Kind != EGuLiDynamicObstacleKind::StaticWorld) return;
	const FBox Bounds = FBox::BuildAABB(Obstacle.Location,FVector(Obstacle.RadiusCentimeters,Obstacle.RadiusCentimeters,100000));
	NotifyStaticGeometryChanged(Bounds);
}
void UGuLiDynamicObstacleRegistrySubsystem::NotifyStaticGeometryChanged(const FBox& Bounds)
{
	if (!Bounds.IsValid) return;
	RegionChanges.Add({++StaticRevision,Bounds});
	if (RegionChanges.Num()>128) RegionChanges.RemoveAt(0,1,EAllowShrinking::No);
	StaticRegionChanged.Broadcast(Bounds);
}
bool UGuLiDynamicObstacleRegistrySubsystem::HasStaticChangesSince(uint32 Since, const FBox& Bounds) const
{
	if (Since==StaticRevision) return false;
	if (RegionChanges.IsEmpty() || Since+1<RegionChanges[0].Revision) return true;
	for (const auto& Change : RegionChanges) if (Change.Revision>Since && Change.Bounds.Intersect(Bounds)) return true;
	return false;
}
