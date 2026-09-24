#include "Gameplay/Resources/GuLiMiningSlots.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FString UGuLiMiningVehicleManager::GetSlotDebug() const
{
	int32 Pending=0, Slots=0, Occupied=0, Rejections=0;
	uint64 SlotBytes=SlotProfiles.GetAllocatedSize(), RejectionBytes=RejectedSlots.GetAllocatedSize();
	for (const auto& Pair : SlotProfiles)
	{
		SlotBytes+=Pair.Value.Clusters.GetAllocatedSize();
		for (const auto& Cluster : Pair.Value.Clusters)
		{
			Pending+=Cluster.bPending; Slots+=Cluster.Slots.Num();
			SlotBytes+=Cluster.Slots.GetAllocatedSize();
			for (const auto& Slot : Cluster.Slots)
			{
				Occupied+=Slot.Owner.IsValid();
				SlotBytes+=Slot.Nodes.GetAllocatedSize();
			}
		}
	}
	for (const auto& Pair : RejectedSlots)
	{
		Rejections+=Pair.Value.Num(); RejectionBytes+=Pair.Value.GetAllocatedSize();
	}
	return FString::Printf(TEXT("Profiles=%d PendingClusters=%d Slots=%d Occupied=%d AssignedNodes=%d NodeOwners=%d RejectedVehicles=%d RejectedSlots=%d SlotCacheBytes=%llu RejectionCacheBytes=%llu"),
		SlotProfiles.Num(),Pending,Slots,Occupied,AssignedNodes.Num(),NodeOwners.Num(),RejectedSlots.Num(),Rejections,SlotBytes,RejectionBytes);
}
TArray<FTransform> UGuLiMiningVehicleManager::GetClusterSlotPoses(int32 ClusterId,int32 UnitTypeId) const
{
	TArray<FTransform> Result;
	if (const auto* Profile=SlotProfiles.Find(UnitTypeId); Profile && Profile->Clusters.IsValidIndex(ClusterId-1))
		if (!Profile->Clusters[ClusterId-1].bPending) for (const auto& Slot : Profile->Clusters[ClusterId-1].Slots) Result.Add(Slot.Pose);
	return Result;
}
bool UGuLiMiningVehicleManager::AreClusterSlotsReady(int32 ClusterId,int32 UnitTypeId) const
{
	const auto* Profile=SlotProfiles.Find(UnitTypeId);
	return Profile && Profile->Clusters.IsValidIndex(ClusterId-1) && !Profile->Clusters[ClusterId-1].bPending;
}

void UGuLiMiningVehicleManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiDynamicObstacleRegistrySubsystem>();
	GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->StaticRegionChanged.AddUObject(this,&ThisClass::DirtySlots);
}
TStatId UGuLiMiningVehicleManager::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiMiningVehicleManager,STATGROUP_Tickables); }
void UGuLiMiningVehicleManager::DirtySlots(const FBox& Bounds)
{
	const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	const auto* Map = Resources ? Resources->GetMapDefinition() : nullptr;
	if (!Map) return;
	for (auto& Pair : SlotProfiles)
		for (int32 I=0; I<Pair.Value.Clusters.Num(); ++I)
			if (Bounds.ExpandBy(3000).IsInsideXY(Map->Clusters[I].Center))
			{
				auto& Cluster = Pair.Value.Clusters[I];
				for (const auto& Slot : Cluster.Slots) if (auto* Owner=Slot.Owner.Get()) ReleaseNode(*Owner);
				Cluster.Slots.Reset(); Cluster.NextSample=0; Cluster.bPending=true; ++Cluster.Generation;
			}
}
void UGuLiMiningVehicleManager::Tick(float)
{
	if (GetWorld()->GetNetMode()==NM_Client) return;
	const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	const auto* Map = Resources ? Resources->GetMapDefinition() : nullptr;
	if (!Map || !Resources->IsRuntimeReady() || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiMining_BuildSlots);
	for (AGuLiMiningVehiclePawn* Vehicle : Vehicles)
		if (IsValid(Vehicle) && Vehicle->GetUnitTypeId()>0 && Vehicle->GetTravelBounds().IsValid)
		{
			auto& Profile=SlotProfiles.FindOrAdd(Vehicle->GetUnitTypeId());
			if (!Profile.Prototype.IsValid()) Profile.Prototype=Vehicle;
			if (Profile.Clusters.IsEmpty()) Profile.Clusters.SetNum(Map->Clusters.Num());
		}
	const double Start = FPlatformTime::Seconds(); int32 Samples=0;
	for (auto& Pair : SlotProfiles)
	{
		auto& Profile=Pair.Value; auto* Vehicle=Profile.Prototype.Get(); if (!Vehicle) continue;
		int32 Inspected=0;
		while (Inspected++<Profile.Clusters.Num() && Samples<8 && FPlatformTime::Seconds()-Start<.001)
		{
			const int32 Index=Profile.BuildCursor; Profile.BuildCursor=(Profile.BuildCursor+1)%Profile.Clusters.Num();
			auto& Slots=Profile.Clusters[Index]; if (!Slots.bPending) continue;
			const auto& Cluster=Map->Clusters[Index];
			if (Resources->IsClusterEmpty(Cluster.ClusterId)) { Slots.bPending=false; continue; }
			const int32 Sample=Slots.NextSample++; ++Samples;
			const double Radius=Cluster.ObstacleRadiusCentimeters+Vehicle->GetNavAgentPropertiesRef().AgentRadius+30;
			const FVector Direction=FVector::ForwardVector.RotateAngleAxis(Sample*45.f,FVector::UpVector);
			FTransform Pose;
			if (GuLiWorkPosition::ProjectPose(*Vehicle,Cluster.Center+Direction*Radius,(-Direction).Rotation(),Pose))
			{
				FGuLiMiningSlot Slot; Slot.Pose=Pose;
				for (int32 N=0; N<Cluster.NodeCount; ++N)
				{
					const uint32 Id=Map->Nodes[Cluster.FirstNodeIndex+N].NodeId; FVector Target;
					if (Resources->GetNodeMiningTarget(Id,Target) && Vehicle->CanMineTargetFrom(Target,Pose)) Slot.Nodes.Add(Id);
				}
				if (!Slot.Nodes.IsEmpty()) Slots.Slots.Add(MoveTemp(Slot));
			}
			Slots.bPending=Slots.NextSample<8;
		}
	}
}
bool UGuLiMiningVehicleManager::ValidateReservation(const AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& R) const
{
	const auto* Profile=SlotProfiles.Find(R.Profile);
	if (!R.IsValid() || !Profile || !Profile->Clusters.IsValidIndex(R.Cluster-1)) return false;
	const auto& Cluster=Profile->Clusters[R.Cluster-1];
	return !Cluster.bPending && Cluster.Generation==R.Generation && Cluster.Slots.IsValidIndex(R.Slot)
		&& Cluster.Slots[R.Slot].Owner.Get()==&Vehicle && Cluster.Slots[R.Slot].Task==R.Task;
}
bool UGuLiMiningVehicleManager::GetSlotPose(const AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& R, FTransform& Out) const
{
	if (!ValidateReservation(Vehicle,R)) return false;
	Out=SlotProfiles.FindChecked(R.Profile).Clusters[R.Cluster-1].Slots[R.Slot].Pose; return true;
}
void UGuLiMiningVehicleManager::ReleaseSlot(const AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& R)
{
	if (!ValidateReservation(Vehicle,R)) return;
	auto& Slot=SlotProfiles.FindChecked(R.Profile).Clusters[R.Cluster-1].Slots[R.Slot]; Slot.Owner.Reset(); Slot.Task=0;
	ReleaseNode(Vehicle);
}
void UGuLiMiningVehicleManager::RejectSlot(AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& R,
	EGuLiMiningSlotFailure Failure)
{
	if (!Vehicle.HasAuthority() || !ValidateReservation(Vehicle,R)) return;
	auto& Rejections=RejectedSlots.FindOrAdd(&Vehicle);
	Rejections.RemoveAll([&](const auto& F) { return F.Slot.Profile==R.Profile && F.Slot.Cluster==R.Cluster && F.Slot.Slot==R.Slot; });
	const bool bUntilChanged = Failure==EGuLiMiningSlotFailure::NoVisibleNodes
		|| Vehicle.FindComponentByClass<UGuLiEngineeringTravelComponent>()->WasPathUnreachable();
	Rejections.Add({R,Vehicle.GetActorLocation(),bUntilChanged ? TNumericLimits<double>::Max() : GetWorld()->GetTimeSeconds()+1,
		GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetStaticRevision(),Failure,
		GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->GetClusterRemainingRaw(R.Cluster),
		Vehicle.GetActorTransform(),GetWorld()->GetTimeSeconds()+1});
	ReleaseSlot(Vehicle,R);
}
EGuLiWorkPositionAvailability UGuLiMiningVehicleManager::TryReserveSlot(AGuLiMiningVehiclePawn& Vehicle, uint16 Preferred, uint32 Task,
	FGuLiMiningSlotReservation& Out, FVector& Position)
{
	using Status=EGuLiWorkPositionAvailability;
	if (!Vehicle.HasAuthority()) return Status::NoValidPositions;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiMining_SelectCachedSlot);
	auto* Profile=SlotProfiles.Find(Vehicle.GetUnitTypeId()); if (!Profile) return Status::Pending;
	if (Out.Profile==Vehicle.GetUnitTypeId() && ValidateReservation(Vehicle,Out) && Out.Task==Task && (!Preferred || Out.Cluster==Preferred))
	{ Position=Profile->Clusters[Out.Cluster-1].Slots[Out.Slot].Pose.GetLocation(); return Status::Available; }
	ReleaseSlot(Vehicle,Out);
	Out={};
	const auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	const auto& Map=*Resources->GetMapDefinition();
	auto& Rejections=RejectedSlots.FindOrAdd(&Vehicle);
	const auto* Obstacles=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	const double Now=GetWorld()->GetTimeSeconds();
	int32 VisibilityChecks=0;
	Rejections.RemoveAll([&](auto& F) {
		if (!F.Slot.IsValid() || !Profile->Clusters.IsValidIndex(F.Slot.Cluster-1)
		|| F.Slot.Profile!=Vehicle.GetUnitTypeId() || F.Slot.Task!=Task || F.RetryAt<=GetWorld()->GetTimeSeconds()
		|| (F.Failure==EGuLiMiningSlotFailure::Movement && !F.Start.Equals(Vehicle.GetActorLocation(),100))
		|| (F.Failure==EGuLiMiningSlotFailure::NoVisibleNodes && F.ClusterRemaining!=Resources->GetClusterRemainingRaw(F.Slot.Cluster))
		|| F.Slot.Generation!=Profile->Clusters[F.Slot.Cluster-1].Generation
		|| Obstacles->HasStaticChangesSince(F.NavRevision,FBox(F.Start.ComponentMin(Map.Clusters[F.Slot.Cluster-1].Center),
			F.Start.ComponentMax(Map.Clusters[F.Slot.Cluster-1].Center)).ExpandBy(2000))) return true;
		// A moving occluder can leave without changing navigation. Recheck the failed parked
		// pose, never the vehicle's new location, and never launch a path just to test visibility.
		if (F.Failure==EGuLiMiningSlotFailure::NoVisibleNodes && Now>=F.NextVisibilityCheck && VisibilityChecks<2)
		{
			++VisibilityChecks; F.NextVisibilityCheck=Now+1;
			const auto& Slots=Profile->Clusters[F.Slot.Cluster-1].Slots;
			if (!Slots.IsValidIndex(F.Slot.Slot)) return true;
			for (uint32 Id : Slots[F.Slot.Slot].Nodes)
			{
				FVector Target;
				if (Resources->GetNodeMiningTarget(Id,Target) && Vehicle.CanMineTargetFrom(Target,F.FailedPose)) return true;
			}
		}
		return false;
	});
	const int32 Current=Resources->FindTerritoryIndex(Vehicle.GetActorLocation());
	auto* Travel=Vehicle.FindComponentByClass<UGuLiEngineeringTravelComponent>();
	for (int32 Pass=0; Pass<(Preferred ? 1 : 2); ++Pass)
	{
		bool bPending=false, bOccupied=false;
		double Best=TNumericLimits<double>::Max(); FGuLiMiningSlotReservation Candidate;
		for (int32 I=0; I<Profile->Clusters.Num(); ++I)
		{
			const auto& Definition=Map.Clusters[I];
			if ((Preferred && Definition.ClusterId!=Preferred) || (!Preferred && (Definition.TerritoryIndex==Current)!=(Pass==0))
				|| !Resources->CanTeamMineAt(Vehicle.GetTeam(),Definition.ClusterId)) continue;
			auto& Cluster=Profile->Clusters[I];
			if (Cluster.bPending) { bPending=true; continue; }
			for (int32 S=0; S<Cluster.Slots.Num(); ++S)
			{
				auto& Slot=Cluster.Slots[S];
				if (!Slot.Nodes.ContainsByPredicate([&](uint32 N) { return Resources->GetNodeRemainingRaw(N)>0; })) continue;
				if (Slot.Owner.IsValid() && Slot.Owner.Get()!=&Vehicle) { bOccupied=true; continue; }
				if (Rejections.ContainsByPredicate([&](const auto& F) { return F.Slot.Cluster==Definition.ClusterId && F.Slot.Slot==S
					&& F.Slot.Generation==Cluster.Generation; })) continue;
				if (!Slot.Nodes.ContainsByPredicate([&](uint32 N) { const auto* Owner=NodeOwners.Find(N);
					return Resources->GetNodeRemainingRaw(N)>0 && (!Owner || !Owner->IsValid() || Owner->Get()==&Vehicle); }))
				{ bOccupied=true; continue; }
				const double Distance=Travel->EstimateWorkDistance(Slot.Pose.GetLocation());
				if (Distance<Best)
				{ Best=Distance; Candidate={Vehicle.GetUnitTypeId(),Definition.ClusterId,S,Cluster.Generation,Task}; }
			}
		}
		if (Candidate.IsValid())
		{
			auto& Slot=Profile->Clusters[Candidate.Cluster-1].Slots[Candidate.Slot];
			Slot.Owner=&Vehicle; Slot.Task=Task; Out=Candidate; Position=Slot.Pose.GetLocation(); return Status::Available;
		}
		if (bPending) return Status::Pending;
		if (bOccupied) return Status::Occupied;
	}
	return Status::NoValidPositions;
}
EGuLiWorkPositionAvailability UGuLiMiningVehicleManager::AssignCoveredNode(AGuLiMiningVehiclePawn& Vehicle,
	const FGuLiMiningSlotReservation& R, const FTransform& MiningPose, uint32& Node, FVector& Target)
{
	using Status=EGuLiWorkPositionAvailability;
	if (!Vehicle.HasAuthority() || !ValidateReservation(Vehicle,R)) return Status::NoValidPositions;
	const auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (!Resources->CanTeamMineAt(Vehicle.GetTeam(),R.Cluster)) return Status::NoValidPositions;
	const auto& Nodes=SlotProfiles.FindChecked(R.Profile).Clusters[R.Cluster-1].Slots[R.Slot].Nodes;
	if (OwnsNode(Vehicle,Node) && Nodes.Contains(Node) && Resources->GetNodeMiningTarget(Node,Target)
		&& Vehicle.CanMineTargetFrom(Target,MiningPose)) return Status::Available;
	// Keep the slot while trying its other cached nodes; all allocation is on the authority game thread.
	ReleaseNode(Vehicle); Node=0;
	bool bOccupied=false;
	for (uint32 Id : Nodes)
	{
		FVector CandidateTarget;
		if (!Resources->GetNodeMiningTarget(Id,CandidateTarget) || !Vehicle.CanMineTargetFrom(CandidateTarget,MiningPose)) continue;
		const auto* Owner=NodeOwners.Find(Id);
		if (Owner && Owner->IsValid()) { bOccupied=true; continue; }
		Node=Id; Target=CandidateTarget; NodeOwners.Add(Id,&Vehicle); AssignedNodes.Add(&Vehicle,Id);
		return Status::Available;
	}
	return bOccupied ? Status::Occupied : Status::NoValidPositions;
}
