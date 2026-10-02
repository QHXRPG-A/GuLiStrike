#include "GuLiNavigationConnectivityLibrary.h"

#include "AI/NavDataGenerator.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Detour/DetourNavMesh.h"
#include "Detour/DetourNavMeshQuery.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GuLiNavigationConnectivitySettings.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastQueryFilter.h"
#include "NavigationSystem.h"
#include "String/LexFromString.h"
#include "UObject/Class.h"
#include "UObject/Package.h"

namespace GuLiNavigationConnectivity
{
	struct FNode
	{
		NavNodeRef Ref = 0;
		bool bGround = false;
		int32 Region = INDEX_NONE;
	};

	/** Compressed adjacency keeps offline analysis linear in polygons plus links. */
	struct FGraph
	{
		ARecastNavMesh* Navigation = nullptr;
		TArray<FNode> Nodes;
		TMap<NavNodeRef, int32> IndexByRef;
		TArray<int32> Offsets, Edges, ReverseOffsets, ReverseEdges;
		int32 ExcludedCount = 0;

		bool Build(UObject* Context, FString& Error)
		{
			if (!IsInGameThread()) { Error = TEXT("Connectivity analysis requires the game thread."); return false; }
			UWorld* World = GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull);
			if (!World) { Error = TEXT("Connectivity analysis requires a world."); return false; }
			const FNavDataConfig* Agent = GuLiCommanderNavigationPolicy::FindRequiredAgentConfig(
				GetDefault<UNavigationSystemV1>()->GetSupportedAgents());
			if (!Agent) { Error = TEXT("CommanderSoldier SupportedAgent configuration is missing or has the wrong radius."); return false; }
			for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
			{
				if (It->GetConfig().Name != Agent->Name) continue;
				if (Navigation) { Error = TEXT("Multiple CommanderSoldier NavData actors; the navigation contract is ambiguous."); return false; }
				Navigation = *It;
			}
			if (!Navigation) { Error = TEXT("CommanderSoldier RecastNavMesh is missing; no default-agent substitution is allowed."); return false; }
			const FNavDataConfig& Actual = Navigation->GetConfig();
			if (!FMath::IsNearlyEqual(Actual.AgentRadius, Agent->AgentRadius, .5f)
				|| !FMath::IsNearlyEqual(Actual.AgentHeight, Agent->AgentHeight, .5f))
			{ Error = TEXT("CommanderSoldier NavData radius/height does not match SupportedAgents; rebuild with the configured agent."); return false; }
			if (const FNavDataGenerator* Generator = Navigation->GetGenerator(); Generator && Generator->IsBuildInProgressCheckDirty())
			{ Error = TEXT("CommanderSoldier navigation is still building; connectivity cannot be certified."); return false; }
			const dtNavMesh* Mesh = Navigation->GetRecastMesh();
			const FSharedConstNavQueryFilter Filter = Navigation->GetDefaultQueryFilter();
			if (!Mesh || !Filter.IsValid() || !Filter->GetImplementation())
			{ Error = TEXT("CommanderSoldier tiles or default Recast query filter are missing."); return false; }
			// This is the same implementation cast and special-link filter used by UE's Recast queries.
			const auto* RecastFilter = static_cast<const FRecastQueryFilter*>(Filter->GetImplementation());
			const dtQueryFilter* DetourFilter = RecastFilter->GetAsDetourQueryFilter();
			UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FRecastSpeciaLinkFilter LinkFilter(System, nullptr);
			LinkFilter.initialize();
			dtNavMeshQuery Query;
			if (dtStatusFailed(Query.init(Mesh, 0, &LinkFilter)))
			{ Error = TEXT("Cannot initialize the Recast polygon filter."); return false; }
			for (int32 TileIndex = 0; TileIndex < Mesh->getMaxTiles(); ++TileIndex)
			{
				const dtMeshTile* Tile = Mesh->getTile(TileIndex);
				if (!Tile || !Tile->header) continue; // Unallocated tile slots are not navigable surfaces.
				for (int32 LinkIndex = 0; LinkIndex < Tile->header->offMeshConCount; ++LinkIndex)
					if (Tile->offMeshCons[LinkIndex].userId != 0 && !System)
					{ Error = TEXT("Custom navigation links require a registered navigation system to evaluate their traversal contract."); return false; }
				const dtPolyRef Base = Mesh->getPolyRefBase(Tile);
				for (int32 PolyIndex = 0; PolyIndex < Tile->header->polyCount; ++PolyIndex)
				{
					const dtPolyRef Ref = Base | static_cast<dtPolyRef>(PolyIndex);
					if (!Query.isValidPolyRef(Ref, DetourFilter)) { ++ExcludedCount; continue; }
					FNode& Node = Nodes.AddDefaulted_GetRef();
					Node.Ref = Ref;
					Node.bGround = Tile->polys[PolyIndex].getType() == DT_POLYTYPE_GROUND;
					IndexByRef.Add(Ref, Nodes.Num() - 1);
				}
			}
			if (Nodes.IsEmpty()) { Error = TEXT("CommanderSoldier has no polygons accepted by its movement filter."); return false; }
			Offsets.Reserve(Nodes.Num() + 1);
			ReverseOffsets.Init(0, Nodes.Num() + 1);
			TArray<NavNodeRef> Neighbors;
			for (const FNode& Node : Nodes)
			{
				Offsets.Add(Edges.Num());
				Neighbors.Reset();
				if (!Navigation->GetPolyNeighbors(Node.Ref, Neighbors))
				{ Error = TEXT("A CommanderSoldier polygon has invalid navigation links."); return false; }
				for (const NavNodeRef Neighbor : Neighbors)
					if (const int32* Index = IndexByRef.Find(Neighbor))
					{ Edges.Add(*Index); ++ReverseOffsets[*Index + 1]; }
			}
			Offsets.Add(Edges.Num());
			for (int32 Index = 1; Index < ReverseOffsets.Num(); ++Index)
				ReverseOffsets[Index] += ReverseOffsets[Index - 1];
			ReverseEdges.SetNumUninitialized(Edges.Num());
			TArray<int32> Cursor = ReverseOffsets;
			for (int32 Index = 0; Index < Nodes.Num(); ++Index)
				for (int32 Edge = Offsets[Index]; Edge < Offsets[Index + 1]; ++Edge)
					ReverseEdges[Cursor[Edges[Edge]]++] = Index;
			if (RecastFilter->IsBacktrackingEnabled())
			{ Swap(Offsets, ReverseOffsets); Swap(Edges, ReverseEdges); }
			return true;
		}

		void Partition()
		{
			struct FVisit { int32 Node, NextEdge; };
			TBitArray<> Visited(false, Nodes.Num());
			TArray<FVisit> Stack;
			TArray<int32> FinishOrder;
			FinishOrder.Reserve(Nodes.Num());
			for (int32 Root = 0; Root < Nodes.Num(); ++Root)
			{
				if (Visited[Root]) continue;
				Visited[Root] = true;
				Stack.Add({Root, Offsets[Root]});
				while (!Stack.IsEmpty())
				{
					FVisit& Visit = Stack.Last();
					if (Visit.NextEdge < Offsets[Visit.Node + 1])
					{
						const int32 Next = Edges[Visit.NextEdge++];
						if (!Visited[Next]) { Visited[Next] = true; Stack.Add({Next, Offsets[Next]}); }
					}
					else { FinishOrder.Add(Visit.Node); Stack.Pop(EAllowShrinking::No); }
				}
			}
			TArray<int32> Pending;
			int32 Region = 0;
			for (int32 Order = FinishOrder.Num() - 1; Order >= 0; --Order)
			{
				const int32 Root = FinishOrder[Order];
				if (Nodes[Root].Region != INDEX_NONE) continue;
				Nodes[Root].Region = Region;
				Pending.Add(Root);
				while (!Pending.IsEmpty())
				{
					const int32 Node = Pending.Pop(EAllowShrinking::No);
					for (int32 Edge = ReverseOffsets[Node]; Edge < ReverseOffsets[Node + 1]; ++Edge)
					{
						FNode& Next = Nodes[ReverseEdges[Edge]];
						if (Next.Region == INDEX_NONE) { Next.Region = Region; Pending.Add(ReverseEdges[Edge]); }
					}
				}
				++Region;
			}
		}

		bool Describe(FGuLiNavigationConnectivityResult& Result, FString& Error) const
		{
			TMap<int32, int32> RegionIndices;
			TArray<FBox> Bounds;
			TArray<FVector> Vertices;
			for (const FNode& Node : Nodes)
			{
				if (!Node.bGround) continue; // Links connect surfaces but are not standing regions.
				int32* Index = RegionIndices.Find(Node.Region);
				if (!Index)
				{
					const int32 NewIndex = Result.Regions.AddDefaulted();
					Index = &RegionIndices.Add(Node.Region, NewIndex);
					Result.Regions[NewIndex].RegionId = Node.Region;
					if (!Navigation->GetPolyCenter(Node.Ref, Result.Regions[NewIndex].RepresentativeLocation))
					{ Error = TEXT("Cannot read a region's representative polygon."); return false; }
					Bounds.Add(FBox(ForceInit));
				}
				FGuLiNavigationConnectedRegion& Region = Result.Regions[*Index];
				++Region.GroundPolygonCount;
				++Result.GroundPolygonCount;
				Region.AreaSquareMeters += Navigation->GetPolySurfaceArea(Node.Ref) / 10000.0;
				Vertices.Reset();
				if (!Navigation->GetPolyVerts(Node.Ref, Vertices))
				{ Error = TEXT("Cannot read a region's polygon bounds."); return false; }
				for (const FVector& Vertex : Vertices) Bounds[*Index] += Vertex;
			}
			if (Result.Regions.IsEmpty()) { Error = TEXT("CommanderSoldier has no walkable ground polygons."); return false; }
			int32 MainRegion = 0;
			for (int32 Index = 0; Index < Result.Regions.Num(); ++Index)
			{
				Result.Regions[Index].BoundsMinimum = Bounds[Index].Min;
				Result.Regions[Index].BoundsMaximum = Bounds[Index].Max;
				if (Result.Regions[Index].AreaSquareMeters > Result.Regions[MainRegion].AreaSquareMeters) MainRegion = Index;
			}
			Result.Regions[MainRegion].bMainRegion = true;
			Result.GraphPolygonCount = Nodes.Num();
			Result.DirectedEdgeCount = Edges.Num();
			Result.ExcludedPolygonCount = ExcludedCount;
			return true;
		}

		bool Reachable(const int32 Start, const int32 End) const
		{
			if (Nodes[Start].Region == Nodes[End].Region) return true;
			TBitArray<> Visited(false, Nodes.Num());
			TArray<int32> Pending;
			Pending.Add(Start); Visited[Start] = true;
			while (!Pending.IsEmpty())
			{
				const int32 Node = Pending.Pop(EAllowShrinking::No);
				for (int32 Edge = Offsets[Node]; Edge < Offsets[Node + 1]; ++Edge)
				{
					const int32 Next = Edges[Edge];
					if (Next == End) return true;
					if (!Visited[Next]) { Visited[Next] = true; Pending.Add(Next); }
				}
			}
			return false;
		}
	};

	void CheckEditorPoints(const TArray<FString>& Args)
	{
		if (Args.Num() != 6)
		{
			UE_LOG(LogTemp, Error, TEXT("gs.Navigation.CheckPoints requires StartX StartY StartZ EndX EndY EndZ in centimeters."));
			return;
		}
		double Values[6];
		for (int32 Index = 0; Index < 6; ++Index)
			if (!LexTryParseString(Values[Index], *Args[Index]))
			{ UE_LOG(LogTemp, Error, TEXT("Invalid coordinate: %s"), *Args[Index]); return; }
		const auto Result = UGuLiNavigationConnectivityLibrary::CheckPointConnectivity(
			GEditor->GetEditorWorldContext().World(), FVector(Values[0], Values[1], Values[2]), FVector(Values[3], Values[4], Values[5]), FVector(10, 10, 50));
		UE_LOG(LogTemp, Display, TEXT("[GULI_NAV_POINTS] status=%s query_valid=%d forward=%d reverse=%d %s"),
			*UEnum::GetValueAsString(Result.Status), Result.bQuerySucceeded, Result.bStartToEnd, Result.bEndToStart, *Result.Message);
	}

	FAutoConsoleCommand CheckPointsCommand(TEXT("gs.Navigation.CheckPoints"),
		TEXT("Read-only CommanderSoldier connectivity: StartX StartY StartZ EndX EndY EndZ (cm). Editor source world; projection extent 10/10/50 cm."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CheckEditorPoints));

	FAutoConsoleCommand CheckConnectivityCommand(TEXT("gs.Navigation.CheckConnectivity"),
		TEXT("Read-only connectivity of all CommanderSoldier walkable ground in the editor source world."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const auto Result = UGuLiNavigationConnectivityLibrary::AnalyzeWorldConnectivity(GEditor->GetEditorWorldContext().World());
			if (Result.bAnalysisSucceeded && Result.bFullyConnected)
			{
				UE_LOG(LogTemp, Display, TEXT("[GULI_NAV_CONNECTIVITY] %s"), *Result.Message);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[GULI_NAV_CONNECTIVITY] %s"), *Result.Message);
			}
			for (const auto& Region : Result.Regions)
			{
				UE_LOG(LogTemp, Display, TEXT("[GULI_NAV_CONNECTIVITY_REGION] region=%d main=%d polygons=%d area_m2=%.3f point_cm=%s min_cm=%s max_cm=%s"),
					Region.RegionId, Region.bMainRegion, Region.GroundPolygonCount, Region.AreaSquareMeters,
					*Region.RepresentativeLocation.ToString(), *Region.BoundsMinimum.ToString(), *Region.BoundsMaximum.ToString());
			}
		}));
}

bool UGuLiNavigationConnectivityLibrary::RequiresConnectedGround(const UWorld& World)
{
	return GetDefault<UGuLiNavigationConnectivitySettings>()->RequiredWorldPackages.Contains(World.GetOutermost()->GetFName());
}

FGuLiNavigationConnectivityResult UGuLiNavigationConnectivityLibrary::AnalyzeWorldConnectivity(UObject* Context)
{
	const double Started = FPlatformTime::Seconds();
	FGuLiNavigationConnectivityResult Result;
	GuLiNavigationConnectivity::FGraph Graph;
	if (Graph.Build(Context, Result.Message))
	{
		Graph.Partition();
		Result.NavigationDataPath = Graph.Navigation->GetPathName();
		Result.bAnalysisSucceeded = Graph.Describe(Result, Result.Message);
		if (!Result.bAnalysisSucceeded)
		{
			// An invalid payload is a dependency error, not a partial connectivity verdict.
			Result.Regions.Reset();
			Result.GroundPolygonCount = 0;
		}
		if (Result.bAnalysisSucceeded)
		{
			Result.bFullyConnected = Result.Regions.Num() == 1;
			Result.Message = FString::Printf(TEXT("CommanderSoldier: %d mutually reachable ground regions, %d ground polygons, %d directed links. %s"),
				Result.Regions.Num(), Result.GroundPolygonCount, Result.DirectedEdgeCount,
				Result.bFullyConnected ? TEXT("All walkable ground is connected.") : TEXT("Disconnected walkable ground; bake certification is refused."));
		}
	}
	Result.AnalysisSeconds = FPlatformTime::Seconds() - Started;
	return Result;
}

FGuLiNavigationPointConnectivityResult UGuLiNavigationConnectivityLibrary::CheckPointConnectivity(
	UObject* Context, const FVector Start, const FVector End, const FVector ProjectionExtent)
{
	const double Started = FPlatformTime::Seconds();
	FGuLiNavigationPointConnectivityResult Result;
	const auto Finish = [&]() { Result.QuerySeconds = FPlatformTime::Seconds() - Started; return Result; };
	if (Start.ContainsNaN() || End.ContainsNaN() || ProjectionExtent.ContainsNaN()
		|| ProjectionExtent.X <= 0 || ProjectionExtent.Y <= 0 || ProjectionExtent.Z <= 0)
	{
		Result.Status = EGuLiNavigationPointConnectivityStatus::InvalidInput;
		Result.Message = TEXT("Start/end must be finite and projection extents must be finite and positive.");
		return Finish();
	}
	GuLiNavigationConnectivity::FGraph Graph;
	if (!Graph.Build(Context, Result.Message)) return Finish();
	Result.NavigationDataPath = Graph.Navigation->GetPathName();
	FNavLocation StartNav, EndNav;
	const FSharedConstNavQueryFilter Filter = Graph.Navigation->GetDefaultQueryFilter();
	const auto ProjectWithinExtent = [&](const FVector& Point, FNavLocation& Projected)
	{
		if (!Graph.Navigation->ProjectPoint(Point, Projected, ProjectionExtent, Filter, nullptr)) return false;
		// Recast can modify the supplied query extent. Preserve the caller's explicit snap contract.
		const FVector Delta = (Projected.Location - Point).GetAbs();
		return Delta.X <= ProjectionExtent.X && Delta.Y <= ProjectionExtent.Y && Delta.Z <= ProjectionExtent.Z;
	};
	if (!ProjectWithinExtent(Start, StartNav))
	{
		Result.Status = EGuLiNavigationPointConnectivityStatus::StartNotNavigable;
		Result.Message = TEXT("Start is not covered by CommanderSoldier navigation within the requested projection extent.");
		return Finish();
	}
	Result.ProjectedStart = StartNav.Location;
	if (!ProjectWithinExtent(End, EndNav))
	{
		Result.Status = EGuLiNavigationPointConnectivityStatus::EndNotNavigable;
		Result.Message = TEXT("End is not covered by CommanderSoldier navigation within the requested projection extent.");
		return Finish();
	}
	Result.ProjectedEnd = EndNav.Location;
	const int32* StartIndex = Graph.IndexByRef.Find(StartNav.NodeRef);
	const int32* EndIndex = Graph.IndexByRef.Find(EndNav.NodeRef);
	if (!StartIndex || !EndIndex)
	{ Result.Message = TEXT("Projected polygon is absent from the filtered graph; navigation changed during the query."); return Finish(); }
	Graph.Partition();
	Result.StartRegionId = Graph.Nodes[*StartIndex].Region;
	Result.EndRegionId = Graph.Nodes[*EndIndex].Region;
	Result.bStartToEnd = Graph.Reachable(*StartIndex, *EndIndex);
	Result.bEndToStart = Graph.Reachable(*EndIndex, *StartIndex);
	Result.bQuerySucceeded = true;
	Result.Status = Result.bStartToEnd && Result.bEndToStart ? EGuLiNavigationPointConnectivityStatus::Connected
		: (Result.bStartToEnd || Result.bEndToStart ? EGuLiNavigationPointConnectivityStatus::OneWay : EGuLiNavigationPointConnectivityStatus::Disconnected);
	Result.Message = FString::Printf(TEXT("CommanderSoldier regions %d -> %d: forward=%d reverse=%d; full directed graph, no partial-path or A* node-limit result."),
		Result.StartRegionId, Result.EndRegionId, Result.bStartToEnd, Result.bEndToStart);
	return Finish();
}
