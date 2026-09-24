#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Engine/World.h"
#include "AIController.h"

namespace
{
	FVector EntryPoint(const FVector& From, const FVector& Center)
	{
		const double Half = GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM-200;
		return FVector(FMath::Clamp(From.X,Center.X-Half,Center.X+Half),
			FMath::Clamp(From.Y,Center.Y-Half,Center.Y+Half),Center.Z);
	}
}
double UGuLiEngineeringTravelComponent::PlanWorkRoute(const FVector& Target, TArray<FTransitLeg>* Out) const
{
	if (Out) Out->Reset();
	const FVector Start = GetOwner()->GetActorLocation();
	double Best = FVector::Dist2D(Start,Target);
	const auto* Map = Resources().GetMapDefinition();
	if (!Map || !Resources().IsRuntimeReady()) return Best;
	const int32 StartTerritory = Resources().FindTerritoryIndex(Start);
	if (StartTerritory != INDEX_NONE && StartTerritory == Resources().FindTerritoryIndex(Target)) return Best;
	const auto& Snapshot = Resources().GetTransportNetwork().GetSnapshot();
	const EGuLiTeam Team = Vehicle().GetTeam();
	const auto& Nodes = Snapshot.Nodes;
	if (RouteCacheRevision != Snapshot.Revision || RouteCacheTeam != Team || !RouteCacheStart.Equals(Start,1)
		|| RouteCosts.Num() != Nodes.Num())
	{
		RouteCacheRevision = Snapshot.Revision; RouteCacheStart = Start; RouteCacheTeam = Team;
		RouteCosts.Init(TNumericLimits<double>::Max(),Nodes.Num());
		RouteParents.Init(INDEX_NONE,Nodes.Num()); RouteSources.Init(INDEX_NONE,Nodes.Num());
		TArray<bool> Visited; Visited.Init(false,Nodes.Num());
		// A graph edge walks to an embarkation territory, then travels within its region.
		// Cache all station costs once per vehicle pose/revision; slot ranking only does O(stations) arithmetic.
		int32 Previous = INDEX_NONE;
		for (int32 Iteration=0; Iteration<=Nodes.Num(); ++Iteration)
		{
			const FVector Position = Previous==INDEX_NONE ? Start : Nodes[Previous].GroundLocation;
			const double Cost = Previous==INDEX_NONE ? 0 : RouteCosts[Previous];
			TMap<int32,TPair<double,int32>> RegionEntries;
			for (int32 Source=0; Source<Nodes.Num(); ++Source)
			{
				if (Nodes[Source].Team!=Team) continue;
				const FVector Entry = EntryPoint(Position,Map->Territories[Nodes[Source].TerritoryIndex].Center);
				const double NextCost = Cost+FVector::Dist2D(Position,Entry);
				auto* BestEntry=RegionEntries.Find(Nodes[Source].RegionId);
				if (!BestEntry || NextCost<BestEntry->Key) RegionEntries.Add(Nodes[Source].RegionId,{NextCost,Source});
			}
			// Every exit in a region shares its cheapest entry. Avoid a source/exit
			// Cartesian product on every Dijkstra step as the match gains stations.
			for (int32 Destination=0; Destination<Nodes.Num(); ++Destination)
			{
				if (Visited[Destination] || Nodes[Destination].Team!=Team) continue;
				const auto* Entry=RegionEntries.Find(Nodes[Destination].RegionId);
				if (Entry && Entry->Value!=Destination && Entry->Key+1<RouteCosts[Destination])
				{ RouteCosts[Destination]=Entry->Key; RouteParents[Destination]=Previous; RouteSources[Destination]=Entry->Value; }
			}
			int32 Next=INDEX_NONE;
			for (int32 I=0; I<Nodes.Num(); ++I)
				if (!Visited[I] && Nodes[I].Team==Team && RouteCosts[I]<TNumericLimits<double>::Max()
					&& (Next==INDEX_NONE || RouteCosts[I]<RouteCosts[Next])) Next=I;
			if (Next==INDEX_NONE) break;
			Visited[Next]=true; Previous=Next;
		}
	}
	int32 Destination=INDEX_NONE;
	for (int32 I=0; I<Nodes.Num(); ++I)
	{
		if (Nodes[I].Team!=Team || RouteSources[I]==INDEX_NONE) continue;
		const double Cost = RouteCosts[I]+FVector::Dist2D(Nodes[I].GroundLocation,Target);
		if (Cost+1<Best) { Best=Cost; Destination=I; }
	}
	if (Out)
		for (int32 I=Destination, Guard=0; I!=INDEX_NONE && Guard++<Nodes.Num(); I=RouteParents[I])
		{
			const int32 Source=RouteSources[I];
			const FVector From = RouteParents[I]==INDEX_NONE ? Start : Nodes[RouteParents[I]].GroundLocation;
			Out->Insert({Nodes[Source].TerritoryIndex,Nodes[I].TerritoryIndex,
				EntryPoint(From,Map->Territories[Nodes[Source].TerritoryIndex].Center)},0);
		}
	return Best;
}
double UGuLiEngineeringTravelComponent::EstimateWorkDistance(const FVector& Target) const { return PlanWorkRoute(Target,nullptr); }
bool UGuLiEngineeringTravelComponent::BeginWorkMove(const FVector& Target, float Radius)
{
	if (!GetOwner()->HasAuthority() || Target.ContainsNaN() || IsRouting()) return false;
	CancelGroundMove(); Controller().StopMovement();
	bWorkJourney=true; WorkGoal=Target; WorkAcceptance=Radius;
	PlanWorkRoute(Target,&WorkLegs); AdvanceWorkRoute();
	return GroundStatus!=EGuLiEngineeringMoveStatus::Failed;
}
void UGuLiEngineeringTravelComponent::AdvanceWorkRoute()
{
	if (WorkLegs.IsEmpty())
	{
		if (!MoveOnGround(WorkGoal,WorkAcceptance)) GroundStatus=EGuLiEngineeringMoveStatus::Failed;
		return;
	}
	const auto Leg=WorkLegs[0];
	if (Resources().FindTerritoryIndex(GetOwner()->GetActorLocation())!=Leg.Source)
	{
		if (!MoveOnGround(Leg.Entry,50)) GroundStatus=EGuLiEngineeringMoveStatus::Failed;
		return;
	}
	FGuLiPreparedTransit Transit;
	Transit.Route=Resources().GetTransportNetwork().FindRoute(Leg.Source,Leg.Target,Vehicle().GetTeam());
	if (Transit.Route.Num()<2)
	{
		WorkLegs.Reset(); MoveOnGround(WorkGoal,WorkAcceptance); return;
	}
	Transit.FieldId=Resources().GetTransportNetwork().GetSnapshot().FindNode(Leg.Source)->TransitFieldId;
	Transit.ClickLocation=WorkGoal;
	WorkLegs.RemoveAt(0); GroundStatus=EGuLiEngineeringMoveStatus::Moving;
	BeginTransport(Transit);
}
