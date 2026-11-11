#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshPath.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/IConsoleManager.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"

const GuLiCircleAvoidance::FConfig* FGuLiSharedMoveIntent::CircleProfile(const ANavigationData& Data,uint32 Id)
{
    if (const auto* P=CircleProfiles.Find(Id)) return P;
    auto* World=Data.GetWorld();
    auto* Army=World?World->GetSubsystem<UGuLiBattleAuthoritySubsystem>():nullptr;
    const auto* Units=World?World->GetSubsystem<UGuLiUnitDataSubsystem>():nullptr;
    EGuLiTeam Team;uint16 Type=0;FVector Position;
    if (!Army || !Units || !Army->GetTaskSoldierInfo(FGuLiSoldierId(Id),Team,Type,Position)) return nullptr;
    const auto* P=Units->GetAvoidanceCatalog().Units.Find(Type);
    return P?&CircleProfiles.Add(Id,P->Solver):nullptr;
}

void FGuLiSharedMoveIntent::RememberCircleSlot(const ANavigationData& Data,uint32 Id,const FVector& Point)
{
    const auto* P=CircleProfile(Data,Id);if (!P) return;
    GuLiCircleAvoidance::FCircle C;C.Id=Id;C.Center=Point;C.Radius=P->Radius;
    C.Clearance=P->Clearance;C.ContactMargin=P->ContactMargin;
    C.LayerMask=uint8(1u<<uint8(MovementLayers.FindRef(Id)));AcceptedCircles.Upsert(C);
}

bool FGuLiSharedMoveIntent::RestoreCircleSlots(const ANavigationData& Data,FGuLiNavigationWorkBudget& Budget)
{
    if (AcceptedCircles.Num()==AssignedPoints.Num()) return true;
    if (GridRestoreMembers.IsEmpty()) {AssignedPoints.GenerateKeyArray(GridRestoreMembers);GridRestoreCursor=0;}
    while (GridRestoreCursor<GridRestoreMembers.Num() && Budget.CanWork())
    {
        const uint32 Id=GridRestoreMembers[GridRestoreCursor++];
        RememberCircleSlot(Data,Id,AssignedPoints.FindChecked(Id).Location);
    }
    return GridRestoreCursor==GridRestoreMembers.Num();
}

bool FGuLiSharedMoveIntent::CircleSlotClear(const ANavigationData& Data,uint32 Id,const FVector& Point)
{
    using namespace GuLiCircleAvoidance;
    const auto* P=CircleProfile(Data,Id);if (!P) return false;
    auto* Registry=Data.GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
    const auto Layer=MovementLayers.FindRef(Id);
    if (!Registry || !Registry->GetSnapshot()->IsSegmentClear(Point,Point,P->Radius,false,Layer,
        P->Clearance,P->ContactMargin,0,false)) return false;
    FRequest Q;Q.SelfId=Id;Q.Position=Point;Q.Clearance=P->Clearance;Q.ContactMargin=P->ContactMargin;
    Q.LayerMask=uint8(1u<<uint8(Layer));Q.AllowInitialEscape=false;
    FQueryScratch Scratch;FCounters Counters;const FIndex Empty;
    return !Sweep(Empty,AcceptedCircles,Q,FVector::ZeroVector,P->Radius+P->Clearance+P->ContactMargin,false,Scratch,Counters).Blocked;
}

namespace
{
TAutoConsoleVariable<int32> CompleteReadyRoutes(TEXT("gs.Commander.CompleteReadyRoutes"),1,
	TEXT("Index a completed shared path within its remaining work slice. Keeps the same time/query budgets; 0 defers indexing to a later visit."));
}

bool FGuLiDirectMoveCheck::Matches(const ANavigationData& NavData, uint32 NavGeneration,
	const FNavLocation& Position, const FNavLocation& Destination) const
{
	return Data.Get() == &NavData && Generation == NavGeneration
		&& Start.NodeRef == Position.NodeRef && Target.NodeRef == Destination.NodeRef
		&& Start.Location.Equals(Position.Location, .01) && Target.Location.Equals(Destination.Location, .01);
}

bool FGuLiDirectMoveCheck::Update(const ANavigationData& NavData, uint32 NavGeneration,
	const FNavLocation& Position, const FNavLocation& Destination,
	FGuLiNavigationWorkBudget& Budget, const UObject* Querier)
{
	if (Matches(NavData, NavGeneration, Position, Destination)) return true;
	if (!Budget.TakeProjection()) return false;
	FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
	FVector Hit;
	FNavigationRaycastAdditionalResults Result;
	const bool bObstructed = NavData.Raycast(Position.Location, Destination.Location, Hit,
		&Result, NavData.GetDefaultQueryFilter(), Querier);
	Data = &NavData; Generation = NavGeneration; Start = Position; Target = Destination;
	// XY visibility alone is insufficient for stacked surfaces or the foot of a cliff.
	bClear = !bObstructed && Result.bIsRayEndInCorridor;
	return true;
}

bool FGuLiSharedMoveRoute::Sample(const FNavLocation& Position, float Tolerance, int32& Cursor, FVector& Out) const
{
	if (State != EState::Ready || !Navigation.Path.IsValid() || !Navigation.Path->IsUpToDate()) return false;
	const int32* Index = PolygonIndex.Find(Position.NodeRef);
	if (!Index) return false;
	// Follow the native path corners monotonically. Steering across a polygon
	// edge must not flip the target between two neighboring portal midpoints.
	const auto& Points=Navigation.Path->GetPathPoints();
	if (Points.IsEmpty()) return false;
	Cursor=FMath::Clamp(Cursor,Points.Num()>1?1:0,Points.Num()-1);
	while (Cursor<Points.Num()-1)
	{
		const int32* CornerPolygon=PolygonIndex.Find(Points[Cursor].NodeRef);
		if (FVector::DistSquared2D(Position.Location,Points[Cursor].Location)>FMath::Square(Tolerance)
			&& (!CornerPolygon || *Index<=*CornerPolygon)) break;
		++Cursor;
	}
	Out=Points[Cursor].Location;
	return true;
}

SIZE_T FGuLiSharedMoveRoute::Bytes() const
{
	// Include conservative allowances for native corridor storage and the goal's weak indices.
	return 4096 + sizeof(*this) + Navigation.Nodes.GetAllocatedSize() + Navigation.Nodes.Num()*512
		+ (Navigation.Path.IsValid() ? Navigation.Path->GetPathPoints().GetAllocatedSize() : 0);
}

void FGuLiSharedMoveIntent::Generate(const ANavigationData& NavData, uint32 Generation, FGuLiNavigationWorkBudget& Budget)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_HexCoverage);
	if (!bMetadataReady || Pitch <= 0.f) return;
	if (!RestoreCircleSlots(NavData,Budget)) return;
	if (CoverageGeneration != Generation)
	{
		// Negative results expire lazily; accepted points are hints, never reservations or proofs.
		CoverageGeneration = Generation;
		if (Points.IsEmpty() && bExhausted) { Ring = Step = AirRing = AirStep = 0; bExhausted = false; }
	}
	while (!bExhausted && !UnassignedMembers.IsEmpty() && Budget.TakeProjection())
	{
        int32 Pick=Random.RandRange(0,UnassignedMembers.Num()-1);
        const auto CanGenerate=[&](uint32 Id){return (MovementLayers.FindRef(Id)==EGuLiUnitMovementLayer::LowAir?AirRing:Ring)<=25;};
        if (!CanGenerate(UnassignedMembers[Pick])) Pick=UnassignedMembers.IndexOfByPredicate(CanGenerate);
        if (Pick==INDEX_NONE) {bExhausted=true;break;}
        const bool LowAir=MovementLayers.FindRef(UnassignedMembers[Pick])==EGuLiUnitMovementLayer::LowAir;
        int32& LayerRing=LowAir?AirRing:Ring;int32& LayerStep=LowAir?AirStep:Step;
		// 6, 12, 18... points. Radial gaps are exactly Pitch; adjacent chords are >= Pitch.
		const double Angle = LayerRing ? UE_TWO_PI * LayerStep / (6 * LayerRing) : 0.;
		const FVector P = Click + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (LayerRing * Pitch);
		FNavLocation Nav;
		{
			FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
			// Coverage only: never move a hex point horizontally to find a legal destination.
			if (NavData.ProjectPoint(P, Nav, FVector(.1,.1,5000), NavData.GetDefaultQueryFilter(), nullptr)
				&& Nav.NodeRef && FVector::DistSquared2D(P,Nav.Location) <= .01
                && CircleSlotClear(NavData,UnassignedMembers[Pick],Nav.Location))
			{
				Nav.Location.X=P.X; Nav.Location.Y=P.Y; Points.Add(Nav);
				AssignedPoints.Add(UnassignedMembers[Pick],Nav);
				RememberCircleSlot(NavData,UnassignedMembers[Pick],Nav.Location);
				UnassignedMembers.RemoveAtSwap(Pick,1,EAllowShrinking::No);
				GeneratedRadius=FMath::Max(GeneratedRadius,LayerRing*Pitch);
			}
		}
		if (!LayerRing) LayerRing=1;
		else if (++LayerStep==6*LayerRing) { LayerStep=0; ++LayerRing; }
		bExhausted = Ring > 25 && AirRing > 25; // 9000cm * Pitch / 360, retaining the scaled search radius.
	}
}

bool FGuLiSharedMoveIntent::InDockingArea(const FVector& Position) const
{
	// A single square slot or engineering work point has no generated ring/Pitch.
	// It still needs a nonzero area for the budgeted, verified final connection.
	// This margin does not change square slot spacing or arrival acceptance.
	return bMetadataReady && FVector::DistSquared2D(Position,Click)
		<= FMath::Square(GeneratedRadius+FMath::Max(Pitch,float(GuLiGrid::CellSize)));
}
void FGuLiSharedMoveIntent::GenerateGrid(UGuLiGroundGridSubsystem& Grid,const ANavigationData& NavData,FGuLiNavigationWorkBudget& Budget)
{
    const auto Snapshot=Grid.Snapshot();if (!Snapshot || bGridSlotsReady) return;
    FGuLiNavigationWorkBudget::FScope Scope(Budget);
    if (!GridSlots)
    {
        GridSlots=Grid.CreateSlotWorkspace();if (!GridSlots) return;
        GridRestoreCursor=GridBuildCursor=0;
        AssignedPoints.GenerateKeyArray(GridRestoreMembers);
    }
    while (!bGridMetadataReady && GridBuildCursor<UnassignedMembers.Num() && Budget.CanWork())
    {
        const uint32 Id=UnassignedMembers[GridBuildCursor++];
        if (AssignedPoints.Contains(Id)) continue;
        const int32* Size=GridSizes.Find(Id);
        if (!Size || !GridSlots->Add(Id,*Size,MovementLayers.FindRef(Id))) { bExhausted=true;bGridSlotsReady=true;GridSlots.Reset();return; }
    }
    if (!bGridMetadataReady)
    {
        if (GridBuildCursor<UnassignedMembers.Num() || !Budget.CanWork()) return;
        GridSlots->Freeze(*Snapshot,Click);bGridMetadataReady=true;
    }
    while (GridRestoreCursor<GridRestoreMembers.Num() && Budget.CanWork())
    {
        const uint32 Id=GridRestoreMembers[GridRestoreCursor++];const int32* Size=GridSizes.Find(Id);
        if (!Size) { bExhausted=true;bGridSlotsReady=true;GridSlots.Reset();return; }
        GridSlots->Occupy(Snapshot->AnchorAt(AssignedPoints.FindChecked(Id).Location,*Size),*Size,MovementLayers.FindRef(Id));
        RememberCircleSlot(NavData,Id,AssignedPoints.FindChecked(Id).Location);
    }
    if (GridRestoreCursor<GridRestoreMembers.Num()) return;
    uint32 Id;FIntPoint Anchor;int32 Size;FNavLocation Accepted;
    const auto Navigable=[&](uint32 Member,FIntPoint Cell,int32 Footprint)
    {
        // Next charges one candidate projection. Keep exact formation XY and obtain a real polygon reference.
        FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
        const FVector Point=Snapshot->Center(Cell,Footprint);
        if (!NavData.ProjectPoint(Point,Accepted,FVector(.1,.1,5000),NavData.GetDefaultQueryFilter(),nullptr)
            || !Accepted.NodeRef || !NavData.IsNodeRefValid(Accepted.NodeRef)
            || FVector::DistSquared2D(Point,Accepted.Location)>.01) return false;
        Accepted.Location.X=Point.X;Accepted.Location.Y=Point.Y;
        return CircleSlotClear(NavData,Member,Accepted.Location);
    };
    while (GridSlots->Next(*Snapshot,Budget,Id,Anchor,Size,Navigable))
    {
        AssignedPoints.Add(Id,Accepted);
        RememberCircleSlot(NavData,Id,Accepted.Location);
        GeneratedRadius=FMath::Max(GeneratedRadius,float(FVector::Dist2D(Accepted.Location,Click)));
    }
    if (GridSlots->IsFinished())
    {
        UnassignedMembers.RemoveAll([this](uint32 K){return AssignedPoints.Contains(K);});
        bGridSlotsReady=true;bExhausted=true;bMetadataReady=true;
        GridSlots.Reset();GridRestoreMembers.Empty();
    }
}

TSharedPtr<FGuLiSharedRouteGoal> FGuLiSharedMoveRoutePool::Goal(const FVector& Target)
{
	if (auto* Existing=Goals.Find(Target)) if (auto P=Existing->Pin()) return P;
	if (!Goals.Contains(Target)) GoalKeys.Add(Target);
	auto P=MakeShared<FGuLiSharedRouteGoal>(); P->Target=Target; Goals.Add(Target,P); return P;
}

TSharedPtr<FGuLiSharedMoveRoute> FGuLiSharedMoveRoutePool::Request(const TSharedPtr<FGuLiSharedRouteGoal>& G,
	const FNavLocation& Start, bool bAutomatic, const ANavigationData& NavData, uint32 Generation)
{
	if (!G || !Start.NodeRef) return {};
	const double Now=FPlatformTime::Seconds();
	if (const auto* Found=G->Covered.Find(Start.NodeRef))
		if (auto R=Found->Pin(); R && R->State==FGuLiSharedMoveRoute::EState::Ready && R->Navigation.Data.Get()==&NavData
			&& R->PolygonIndex.Contains(Start.NodeRef) && R->Navigation.Path.IsValid() && R->Navigation.Path->IsUpToDate())
		{ R->LastUsed=Now; ++CacheHits; return R; }
	if (const auto* Found=G->Starts.Find(Start.NodeRef)) if (auto R=Found->Pin(); R && R->State!=FGuLiSharedMoveRoute::EState::Retiring)
	{
		R->LastUsed=Now;
		if (!bAutomatic) R->bAutomatic=false;
		if (R->State==FGuLiSharedMoveRoute::EState::Unavailable && R->AttemptGeneration!=Generation)
		{ G->Starts.Remove(Start.NodeRef); } // Old weak corridor entries retire with the old route.
		else return R;
	}
	if (CachedBytes + 8192 > MaximumBytes) return {};
	auto R=MakeShared<FGuLiSharedMoveRoute>(); R->Handle=NextHandle++; R->Goal=G; R->Start=Start;
	R->bAutomatic=bAutomatic; R->LastUsed=R->QueuedAt=Now;
	G->Starts.Add(Start.NodeRef,R); Routes.Add(R); CachedBytes += R->Bytes(); return R;
}

void FGuLiSharedMoveRoutePool::Tick(bool bAutomatic, UNavigationSystemV1& System, const ANavigationData& NavData,
	uint32 Generation, FGuLiNavigationWorkBudget& Budget)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_SharedRouteWork);
	FGuLiNavigationWorkBudget::FScope Scope(Budget);
	int32 Remaining=Routes.Num();
	auto& Cursor=WorkCursor[bAutomatic?1:0];
	while (Remaining-- > 0 && Budget.CanWork() && !Routes.IsEmpty())
	{
		Cursor %= Routes.Num(); auto& R=*Routes[Cursor++];
		if (R.bAutomatic!=bAutomatic) continue;
		bool bQueried=false;
		if (R.State==FGuLiSharedMoveRoute::EState::Queued)
		{
			if (!Budget.TakePath()) return;
			const SIZE_T Before=R.Bytes();
			FPathFindingResult Result;
			const double QueryStarted=FPlatformTime::Seconds();
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_SharedFindPath);
				FGuLiNavigationWorkBudget::FQueryScope QueryTimer(Budget);
				FPathFindingQuery Query(nullptr,NavData,R.Start.Location,R.Goal->Target);
				Query.SetAllowPartialPaths(true);
				Result=System.FindPathSync(Query);
			}
			QueryCpuSeconds+=FPlatformTime::Seconds()-QueryStarted;
			++Queries; R.AttemptGeneration=Generation; R.Navigation={}; R.PolygonIndex.Reset(); R.IndexCursor=0;
			if (Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->GetPathPoints().IsEmpty())
			{
				R.Navigation.Capture(NavData,Generation,Result.Path);
				R.End=Result.Path->GetPathPoints().Last().Location; R.bPartial=Result.Path->IsPartial();
				R.State=R.Navigation.Nodes.IsEmpty()?FGuLiSharedMoveRoute::EState::Unavailable:FGuLiSharedMoveRoute::EState::Indexing;
			}
			else { ++FailedQueries; R.State=FGuLiSharedMoveRoute::EState::Unavailable; }
			CachedBytes=CachedBytes-Before+R.Bytes();
			if (CachedBytes>MaximumBytes)
			{
				CachedBytes-=R.Bytes(); R.Navigation={}; R.PolygonIndex.Empty();
				R.State=FGuLiSharedMoveRoute::EState::Unavailable; CachedBytes+=R.Bytes();
			}
			bQueried=true;
			if (CompleteReadyRoutes.GetValueOnGameThread()==0) return;
		}
		if (R.State==FGuLiSharedMoveRoute::EState::Indexing)
		{
			const SIZE_T Before=R.Bytes();
			while (R.IndexCursor<R.Navigation.Nodes.Num() && Budget.CanWork())
			{
				const int32 I=R.IndexCursor++; const NavNodeRef Ref=R.Navigation.Nodes[I];
				R.PolygonIndex.Add(Ref,I); R.Goal->Covered.Add(Ref,Routes[(Cursor-1)%Routes.Num()]);
			}
			if (R.IndexCursor==R.Navigation.Nodes.Num()) { R.State=FGuLiSharedMoveRoute::EState::Ready; R.ReadyAt=FPlatformTime::Seconds(); }
			CachedBytes=CachedBytes-Before+R.Bytes();
		}
		// A completed query may finish its cheap index while this same slice has
		// time left. Never issue a second non-preemptible query in this call.
		if (bQueried) return;
	}
}

void FGuLiSharedMoveRoutePool::Maintain(const ANavigationData& NavData, uint32 Generation, FGuLiNavigationWorkBudget& Budget)
{
	FGuLiNavigationWorkBudget::FScope Scope(Budget);
	int32 Remaining=Routes.Num(); const double Now=FPlatformTime::Seconds();
	while (Remaining-- > 0 && Budget.CanWork() && !Routes.IsEmpty())
	{
		MaintenanceCursor %= Routes.Num(); auto& Ptr=Routes[MaintenanceCursor]; auto& R=*Ptr;
		if (Ptr.GetSharedReferenceCount()==1 && (Now-R.LastUsed>30. || CachedBytes>MaximumBytes || R.State==FGuLiSharedMoveRoute::EState::Queued)) R.State=FGuLiSharedMoveRoute::EState::Retiring;
		if (R.State==FGuLiSharedMoveRoute::EState::Retiring)
		{
			while (R.RetireCursor<R.Navigation.Nodes.Num() && Budget.CanWork())
			{
				const auto Ref=R.Navigation.Nodes[R.RetireCursor++];
				if (const auto* P=R.Goal->Covered.Find(Ref); P && P->Pin()==Ptr) R.Goal->Covered.Remove(Ref);
			}
			if (R.RetireCursor<R.Navigation.Nodes.Num()) return;
			if (const auto* P=R.Goal->Starts.Find(R.Start.NodeRef); P && P->Pin()==Ptr) R.Goal->Starts.Remove(R.Start.NodeRef);
			CachedBytes-=R.Bytes(); Routes.RemoveAtSwap(MaintenanceCursor); continue;
		}
		if (R.State==FGuLiSharedMoveRoute::EState::Ready || R.State==FGuLiSharedMoveRoute::EState::Indexing)
		{
			if (R.Navigation.Check(NavData,Generation,Budget)==FGuLiNavigationDependency::ECheck::Invalid)
			{ R.State=FGuLiSharedMoveRoute::EState::Unavailable; R.AttemptGeneration=Generation-1; ++Invalidations; }
		}
		++MaintenanceCursor;
	}
	for (int32 I=0; I<8 && !GoalKeys.IsEmpty() && Budget.CanWork(); ++I)
	{
		GoalCursor %= GoalKeys.Num(); const auto Key=GoalKeys[GoalCursor];
		if (!Goals.FindChecked(Key).IsValid()) { Goals.Remove(Key); GoalKeys.RemoveAtSwap(GoalCursor); }
		else ++GoalCursor;
	}
	Data=&NavData;
}
