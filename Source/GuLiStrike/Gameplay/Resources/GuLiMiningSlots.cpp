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
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"

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
	return FString::Printf(TEXT("Profiles=%d PendingClusters=%d Slots=%d Occupied=%d RejectedVehicles=%d RejectedSlots=%d SlotCacheBytes=%llu RejectionCacheBytes=%llu"),
		SlotProfiles.Num(),Pending,Slots,Occupied,RejectedSlots.Num(),Rejections,SlotBytes,RejectionBytes);
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
	if (UGuLiNetworkSettings::IsHybrid(GetWorld())) Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();
	GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->StaticRegionChanged.AddUObject(this,&ThisClass::DirtySlots);
	InitializeHybridOperations();
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
				Cluster.Slots.Reset(); Cluster.NextSample=0; Cluster.bPending=true; ++Cluster.Generation;
			}
}
void UGuLiMiningVehicleManager::Tick(float)
{
	PollHybridOperations();
	if (GetWorld()->GetNetMode()==NM_Client) return;
	const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	const auto* Map = Resources ? Resources->GetMapDefinition() : nullptr;
	if (!Map || !Resources->IsMiningRuntimeReady() || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return;
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
			// Prepare active work first within the same bounded sample/time budget.
			// A requested cluster must not wait behind the whole map's cache bake.
			Profile.RequestedClusters.RemoveAll([&](int32 I){return !Profile.Clusters.IsValidIndex(I) || !Profile.Clusters[I].bPending;});
			int32 Index;
			if (!Profile.RequestedClusters.IsEmpty())
			{
				Index=Profile.RequestedClusters[0];Profile.RequestedClusters.RemoveAt(0,1,EAllowShrinking::No);
				Profile.RequestedClusters.Add(Index);
			}
			else {Index=Profile.BuildCursor;Profile.BuildCursor=(Profile.BuildCursor+1)%Profile.Clusters.Num();}
			auto& Slots=Profile.Clusters[Index]; if (!Slots.bPending) continue;
			const auto& Cluster=Map->Clusters[Index];
			if (Resources->IsClusterEmpty(Cluster.ClusterId)) { Slots.bPending=false; continue; }
			const int32 Sample=Slots.NextSample++; ++Samples;
			const double Radius=Cluster.ObstacleRadiusCentimeters+Vehicle->GetNavAgentPropertiesRef().AgentRadius+30;
			const FVector Direction=FVector::ForwardVector.RotateAngleAxis(Sample*45.f,FVector::UpVector);
			FTransform Pose;
			if (GuLiWorkPosition::ProjectPose(*Vehicle,Cluster.Center+Direction*Radius,(-Direction).Rotation(),Pose))
			{
				FGuLiMiningSlot Slot; Slot.Pose=Pose; Slot.OriginalSample=static_cast<uint8>(Sample);
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
	if (!ValidateHybridReservation(Vehicle,R)) return false;
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
	ReleaseHybridSlot(Vehicle,R);
	if (!ValidateReservation(Vehicle,R)) return;
	auto& Slot=SlotProfiles.FindChecked(R.Profile).Clusters[R.Cluster-1].Slots[R.Slot]; Slot.Owner.Reset(); Slot.Task=0;
}
void UGuLiMiningVehicleManager::RejectSlot(AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& R,
	EGuLiMiningSlotFailure Failure)
{
	if (!Vehicle.HasAuthority() || !ValidateReservation(Vehicle,R)) return;
	auto& Rejections=RejectedSlots.FindOrAdd(&Vehicle);
	Rejections.RemoveAll([&](const auto& F) { return F.Slot.Profile==R.Profile && F.Slot.Cluster==R.Cluster && F.Slot.Slot==R.Slot; });
	const bool bUntilChanged = Failure==EGuLiMiningSlotFailure::NoVisibleNodes
		|| Vehicle.FindComponentByClass<UGuLiEngineeringTravelComponent>()->WasPathUnreachable();
	Rejections.Add({R,Vehicle.GetActorLocation(),bUntilChanged ? TNumericLimits<double>::Max() : UGuLiHybridActorComponent::BattleSeconds(&Vehicle)+1,
		GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetStaticRevision(),Failure,
		GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->GetClusterRemainingRaw(R.Cluster),
		Vehicle.GetActorTransform(),UGuLiHybridActorComponent::BattleSeconds(&Vehicle)+1});
	ReleaseSlot(Vehicle,R);
}
EGuLiWorkPositionAvailability UGuLiMiningVehicleManager::TryReserveSlot(AGuLiMiningVehiclePawn& Vehicle, uint16 Preferred, uint32 Task,
	FGuLiMiningSlotReservation& Out, FVector& Position)
{
	return TryReserveHybridSlot(Vehicle,Preferred,Task,Out,Position);
}
EGuLiWorkPositionAvailability UGuLiMiningVehicleManager::AssignCoveredNode(AGuLiMiningVehiclePawn& Vehicle,
	const FGuLiMiningSlotReservation& R, const FTransform& MiningPose, uint32& Node, FVector& Target)
{
	return AssignHybridNode(Vehicle,R,MiningPose,Node,Target);
}
