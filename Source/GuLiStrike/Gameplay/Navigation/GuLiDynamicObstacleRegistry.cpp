// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Engine/World.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "NavigationSystem.h"

namespace
{
FGuLiHybridObjectKey GridObstacleKey(uint32 Handle)
{return {EGuLiHybridObjectKind::ResourceNode,FGuid(0x47524944,0x4F425354,0,Handle),1};}
void PublishGridObstacle(UWorld* World,const FGuLiDynamicObstacle& O)
{
    if (auto* G=World?World->GetSubsystem<UGuLiGroundGridSubsystem>():nullptr;G && G->IsReady())
    {
        const auto Key=GridObstacleKey(O.Handle.Value);
        if (O.Kind==EGuLiDynamicObstacleKind::StaticWorld)
            G->SetStaticObstacle(Key,FGuLiGridMotion::Coverage(*G->Snapshot(),O.Location,GuLiGrid::FootprintFromDiameter(O.RadiusCentimeters*2.)));
        else G->RemoveStaticObstacle(Key);
    }
}
}

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
		const FIntPoint Key(X,Y); const auto* Version = CellVersions.Find(Key);
		Stamp.Cells.Add(Key, Version ? *Version : 0);
	}
	return Stamp;
}
bool FGuLiDynamicObstacleSnapshot::IsRegionCurrent(const FGuLiObstacleRegionStamp& Stamp) const
{
	if (!Stamp.bComplete) return Stamp.SnapshotRevision == Revision;
	for (const auto& Pair : Stamp.Cells)
	{
		const auto* Current = CellVersions.Find(Pair.Key);
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

void FGuLiDynamicObstacleSnapshot::Query(const FVector& P,float Radius,TArray<int32>& Out) const
{
	Out.Reset(); TSet<int32> Seen;
	const FIntPoint Min=Cell(P-FVector(Radius,Radius,0)),Max=Cell(P+FVector(Radius,Radius,0));
	// Long path segments can span many empty cells. Bound the lookup by the snapshot size.
	const int64 CellCount=(int64(Max.X)-Min.X+1)*(int64(Max.Y)-Min.Y+1);
	if (CellCount>Obstacles.Num())
	{
		for (int32 I=0; I<Obstacles.Num(); ++I)
			if (FVector::DistSquared2D(P,Obstacles[I].Location)<=FMath::Square(Radius+Obstacles[I].RadiusCentimeters)) Out.Add(I);
	}
	else for (int32 X=Min.X; X<=Max.X; ++X) for (int32 Y=Min.Y; Y<=Max.Y; ++Y)
		if (const auto* Bucket=Cells.Find({X,Y})) for (int32 I : *Bucket)
			if (!Seen.Contains(I)) { Seen.Add(I); Out.Add(I); }
	if (Ore)
	{
		TArray<uint32> Nodes; Ore->Query(P,Radius,Nodes);
		for (uint32 Node:Nodes) Out.Add(-int32(Node));
	}
}
const FGuLiDynamicObstacle& FGuLiDynamicObstacleSnapshot::GetObstacle(int32 QueryIndex) const
{
	if (QueryIndex>=0) return Obstacles[QueryIndex];
	check(Ore && Ore->Find(uint32(-int64(QueryIndex))));
	return Ore->Find(uint32(-int64(QueryIndex)))->Obstacle;
}
const FGuLiDynamicObstacle* FGuLiDynamicObstacleSnapshot::FindObstacle(uint32 Handle) const
{
	if (Handle&0x80000000u)
	{
		const auto* Node=Ore?Ore->Find(Handle&0x7fffffffu):nullptr;
		return Node ? &Node->Obstacle : nullptr;
	}
	const auto* Index=ByHandle.Find(Handle);
	return Index ? &Obstacles[*Index] : nullptr;
}
bool FGuLiDynamicObstacleSnapshot::IsSegmentClear(const FVector& From,const FVector& To,float Radius,bool bAllowEscape) const
{
	TArray<int32> Nearby; Query((From+To)*.5f,Radius+FVector::Dist2D(From,To)*.5f,Nearby);
	const FVector2D A(From), B(To), Segment=B-A;
	for (int32 I : Nearby)
	{
		const auto& O=GetObstacle(I); if (FMath::Abs(To.Z-O.Location.Z)>300.f+O.HalfHeight) continue;
		const FVector2D Center(O.Location);
		const double R=Radius+O.RadiusCentimeters;
		const double StartDistance=FVector2D::DistSquared(A,Center), EndDistance=FVector2D::DistSquared(B,Center);
		if (bAllowEscape && StartDistance<R*R && EndDistance>StartDistance+1.) continue;
		const double T=Segment.SizeSquared()>UE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Center-A,Segment)/Segment.SizeSquared(),0.,1.) : 0.;
		if (FVector2D::DistSquared(A+Segment*T,Center)<R*R) return false;
	}
	return true;
}
TSharedRef<const FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe> UGuLiDynamicObstacleRegistrySubsystem::GetSnapshot()
{
	check(IsInGameThread());
	if (!Snapshot || Snapshot->Revision!=Revision)
	{
		auto Next=MakeShared<FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe>(); Next->Revision=Revision; Next->Obstacles=Obstacles;
		Next->CellVersions = CellVersions;
		Next->Ore = OreIndex;
		for (int32 I=0; I<Next->Obstacles.Num(); ++I)
		{
			const auto& O=Next->Obstacles[I]; Next->ByHandle.Add(O.Handle.Value,I);
			const FVector Radius(O.RadiusCentimeters,O.RadiusCentimeters,0);
			const auto Min=Next->Cell(O.Location-Radius),Max=Next->Cell(O.Location+Radius);
			for (int32 X=Min.X; X<=Max.X; ++X) for (int32 Y=Min.Y; Y<=Max.Y; ++Y) Next->Cells.FindOrAdd({X,Y}).Add(I);
		}
		Snapshot=Next;
	}
	return Snapshot.ToSharedRef();
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
	OreIndex.Reset(); OrePublications=0;
	CellVersions.Reset(); NextCellVersion = 0;
	ObstaclesChanged.Clear();
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
    PublishGridObstacle(GetWorld(),Obstacles[Index]);
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
    PublishGridObstacle(GetWorld(),Stored);
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
    if (auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady()) G->RemoveStaticObstacle(GridObstacleKey(Handle.Value));
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
	ObstaclesChanged.Broadcast(Revision);
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
