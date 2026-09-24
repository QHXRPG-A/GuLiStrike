#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshPath.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FGuLiSharedMoveRoute::Sample(const FNavLocation& Position, float Tolerance, int32& Cursor, FVector& Out) const
{
	if (State != EState::Ready || !Navigation.Path.IsValid() || !Navigation.Path->IsUpToDate()) return false;
	const int32* Index = PolygonIndex.Find(Position.NodeRef);
	if (!Index) return false;
	// Navigation's current polygon is authoritative. A look-ahead cursor must never skip a wall.
	Cursor = *Index;
	if (!Portals.IsValidIndex(Cursor)) { Out = End; return FVector::DistSquared2D(Position.Location, End) > FMath::Square(Tolerance); }
	Out = Portals[Cursor];
	if (FVector::DistSquared2D(Position.Location, Out) <= FMath::Square(Tolerance))
		Out = Portals.IsValidIndex(Cursor + 1) ? Portals[Cursor + 1] : End;
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
	if (CoverageGeneration != Generation)
	{
		// Negative results expire lazily; accepted points are hints, never reservations or proofs.
		CoverageGeneration = Generation;
		if (Points.IsEmpty() && bExhausted) { Ring = Step = 0; bExhausted = false; }
	}
	while (!bExhausted && !UnassignedMembers.IsEmpty() && Budget.TakeProjection())
	{
		// 6, 12, 18... points. Radial gaps are exactly Pitch; adjacent chords are >= Pitch.
		const double Angle = Ring ? UE_TWO_PI * Step / (6 * Ring) : 0.;
		const FVector P = Click + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Ring * Pitch);
		FNavLocation Nav;
		{
			FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
			// Coverage only: never move a hex point horizontally to find a legal destination.
			if (NavData.ProjectPoint(P, Nav, FVector(.1,.1,5000), NavData.GetDefaultQueryFilter(), nullptr)
				&& Nav.NodeRef && FVector::DistSquared2D(P,Nav.Location) <= .01)
			{
				Nav.Location.X=P.X; Nav.Location.Y=P.Y; Points.Add(Nav);
				const int32 Pick=Random.RandRange(0,UnassignedMembers.Num()-1);
				AssignedPoints.Add(UnassignedMembers[Pick],Nav);
				UnassignedMembers.RemoveAtSwap(Pick,1,EAllowShrinking::No);
				GeneratedRadius=FMath::Max(GeneratedRadius,Ring*Pitch);
			}
		}
		if (!Ring) Ring=1;
		else if (++Step==6*Ring) { Step=0; ++Ring; }
		bExhausted = Ring > 25; // 9000cm * Pitch / 360, retaining the scaled search radius.
	}
}

bool FGuLiSharedMoveIntent::InDockingArea(const FVector& Position) const
{
	return bMetadataReady && FVector::DistSquared2D(Position,Click) <= FMath::Square(GeneratedRadius+Pitch);
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
			++Queries; R.AttemptGeneration=Generation; R.Navigation={}; R.PolygonIndex.Reset(); R.Portals.Reset(); R.IndexCursor=0;
			if (Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->GetPathPoints().IsEmpty())
			{
				R.Navigation.Capture(NavData,Generation,Result.Path);
				R.End=Result.Path->GetPathPoints().Last().Location; R.bPartial=Result.Path->IsPartial();
				if (const auto* Mesh=Result.Path->CastPath<FNavMeshPath>())
				{ FGuLiNavigationWorkBudget::FQueryScope PortalTimer(Budget); Mesh->GetPathCorridorEdges(); }
				R.State=R.Navigation.Nodes.IsEmpty()?FGuLiSharedMoveRoute::EState::Unavailable:FGuLiSharedMoveRoute::EState::Indexing;
			}
			else { ++FailedQueries; R.State=FGuLiSharedMoveRoute::EState::Unavailable; }
			CachedBytes=CachedBytes-Before+R.Bytes();
			if (CachedBytes>MaximumBytes)
			{
				CachedBytes-=R.Bytes(); R.Navigation={}; R.PolygonIndex.Empty(); R.Portals.Empty();
				R.State=FGuLiSharedMoveRoute::EState::Unavailable; CachedBytes+=R.Bytes();
			}
			// One non-preemptible engine query per short work slice.
			return;
		}
		if (R.State==FGuLiSharedMoveRoute::EState::Indexing)
		{
			const SIZE_T Before=R.Bytes();
			while (R.IndexCursor<R.Navigation.Nodes.Num() && Budget.CanWork())
			{
				const int32 I=R.IndexCursor++; const NavNodeRef Ref=R.Navigation.Nodes[I];
				R.PolygonIndex.Add(Ref,I); R.Goal->Covered.Add(Ref,Routes[(Cursor-1)%Routes.Num()]);
				if (const auto* Mesh=R.Navigation.Path->CastPath<FNavMeshPath>())
				{
					const auto& Edges=Mesh->GetPathCorridorEdges();
					if (Edges.IsValidIndex(I)) R.Portals.Add((Edges[I].Left+Edges[I].Right)*.5);
				}
			}
			if (R.IndexCursor==R.Navigation.Nodes.Num()) { R.State=FGuLiSharedMoveRoute::EState::Ready; R.ReadyAt=FPlatformTime::Seconds(); }
			CachedBytes=CachedBytes-Before+R.Bytes();
		}
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
