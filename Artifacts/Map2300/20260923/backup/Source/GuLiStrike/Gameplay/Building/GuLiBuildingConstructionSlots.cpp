#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

TArray<FTransform> UGuLiBuildingLifecycleComponent::GetConstructionSlotPoses() const
{
	TArray<FTransform> Result;
	if (!bSlotsPending) for (const auto& Slot : ConstructionSlots) Result.Add(Slot.Pose);
	return Result;
}
int32 UGuLiBuildingLifecycleComponent::GetReservedConstructionSlots() const
{
	int32 Count=0;
	if (!bSlotsPending && State.Phase==EGuLiBuildingPhase::UnderConstruction)
		for (const auto& Slot : ConstructionSlots) Count+=Slot.Owner.IsValid();
	return Count;
}

void UGuLiBuildingLifecycleComponent::PrepareConstructionSlots(ACharacter& Prototype)
{
	if (!GetOwner()->HasAuthority() || State.Phase!=EGuLiBuildingPhase::UnderConstruction) return;
	ConstructionPrototype=&Prototype;
	if (bSlotsPending) GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->QueueConstructionSlots(*this);
}
void UGuLiBuildingLifecycleComponent::InvalidateConstructionSlots(const FBox& Bounds)
{
	if (!GetOwner()->HasAuthority() || State.Phase!=EGuLiBuildingPhase::UnderConstruction
		|| !Bounds.ExpandBy(GetDefinition().CollisionExtent.Size2D()+2000).IsInsideXY(GetGroundLocation())) return;
	ConstructionSlots.Reset(); NextSlotSample=0; ++SlotGeneration; bSlotsPending=true;
	if (ConstructionPrototype.IsValid()) GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->QueueConstructionSlots(*this);
}
bool UGuLiBuildingLifecycleComponent::BuildNextConstructionSlot()
{
	if (State.Phase!=EGuLiBuildingPhase::UnderConstruction || !bSlotsPending) return true;
	auto* Prototype=ConstructionPrototype.Get();
	if (!Prototype || !CastChecked<IGuLiEngineeringVehicle>(Prototype)->GetEngineeringTravelBounds().IsValid) return false;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiConstruction_BuildSlot);
	const FVector Extent=GetDefinition().CollisionExtent;
	const double Clearance=FMath::Max(Prototype->GetCapsuleComponent()->GetScaledCapsuleRadius(),
		Prototype->GetNavAgentPropertiesRef().AgentRadius)+80;
	const FVector Local[] = {FVector(Extent.X+Clearance,0,0),FVector(0,Extent.Y+Clearance,0),
		FVector(-Extent.X-Clearance,0,0),FVector(0,-Extent.Y-Clearance,0)};
	const FVector Ground=GetGroundLocation();
	const FVector Desired=Ground+GetOwner()->GetActorQuat().RotateVector(Local[NextSlotSample++]);
	FTransform Pose;
	if (GuLiWorkPosition::ProjectPose(*Prototype,Desired,(Ground-Desired).Rotation(),Pose))
	{ FGuLiConstructionSlot Slot; Slot.Pose=Pose; ConstructionSlots.Add(MoveTemp(Slot)); }
	bSlotsPending=NextSlotSample<4;
	return !bSlotsPending;
}
EGuLiWorkPositionAvailability UGuLiBuildingLifecycleComponent::GetConstructionAvailability() const
{
	using Status=EGuLiWorkPositionAvailability;
	if (State.Phase!=EGuLiBuildingPhase::UnderConstruction) return Status::NoValidPositions;
	if (bSlotsPending) return Status::Pending;
	if (ConstructionSlots.IsEmpty()) return Status::NoValidPositions;
	return ConstructionSlots.ContainsByPredicate([](const auto& Slot) { return !Slot.Owner.IsValid(); }) ? Status::Available : Status::Occupied;
}
EGuLiWorkPositionAvailability UGuLiBuildingLifecycleComponent::TryReserveConstructionSlot(ACharacter& Vehicle, uint32 Task,
	TConstArrayView<int32> Excluded, FGuLiConstructionSlotReservation& Out, FVector& Position)
{
	using Status=EGuLiWorkPositionAvailability;
	if (!Vehicle.HasAuthority() || CastChecked<IGuLiEngineeringVehicle>(&Vehicle)->GetTeam()!=GetTeam()) return Status::NoValidPositions;
	if (ValidateConstructionSlot(Vehicle,Out) && Out.Task==Task)
	{ Position=ConstructionSlots[Out.Slot].Pose.GetLocation(); return Status::Available; }
	ReleaseConstructionSlot(Vehicle,Out); Out={};
	PrepareConstructionSlots(Vehicle);
	const auto Availability=GetConstructionAvailability();
	if (Availability==Status::Pending || Availability==Status::NoValidPositions) return Availability;
	int32 Best=INDEX_NONE; double Distance=TNumericLimits<double>::Max();
	for (int32 I=0; I<ConstructionSlots.Num(); ++I)
	{
		const auto& Slot=ConstructionSlots[I];
		if (Excluded.Contains(I) || (Slot.Owner.IsValid() && Slot.Owner.Get()!=&Vehicle)) continue;
		const double D=Vehicle.FindComponentByClass<UGuLiEngineeringTravelComponent>()->EstimateWorkDistance(Slot.Pose.GetLocation());
		if (D<Distance) { Distance=D; Best=I; }
	}
	if (Best==INDEX_NONE) return Status::Occupied;
	auto& Slot=ConstructionSlots[Best]; Slot.Owner=&Vehicle; Slot.Task=Task;
	Out={State.InstanceId,Best,SlotGeneration,Task}; Position=Slot.Pose.GetLocation(); return Status::Available;
}
bool UGuLiBuildingLifecycleComponent::ValidateConstructionSlot(const ACharacter& Vehicle,const FGuLiConstructionSlotReservation& R) const
{
	return R.Building==State.InstanceId && R.Generation==SlotGeneration && !bSlotsPending && ConstructionSlots.IsValidIndex(R.Slot)
		&& ConstructionSlots[R.Slot].Owner.Get()==&Vehicle && ConstructionSlots[R.Slot].Task==R.Task
		&& State.Phase==EGuLiBuildingPhase::UnderConstruction && CastChecked<IGuLiEngineeringVehicle>(&Vehicle)->GetTeam()==GetTeam();
}
void UGuLiBuildingLifecycleComponent::ReleaseConstructionSlot(const ACharacter& Vehicle,const FGuLiConstructionSlotReservation& R)
{
	if (R.Building!=State.InstanceId || R.Generation!=SlotGeneration || !ConstructionSlots.IsValidIndex(R.Slot)) return;
	auto& Slot=ConstructionSlots[R.Slot];
	if (Slot.Owner.Get()==&Vehicle && Slot.Task==R.Task) { Slot.Owner.Reset(); Slot.Task=0; }
}
void UGuLiBuildingLifecycleComponent::AccumulateConstructionWork(float Work)
{
	check(GetOwner()->HasAuthority());
	if (State.Phase!=EGuLiBuildingPhase::UnderConstruction || Work<=0 || !FMath::IsFinite(Work)) return;
	PendingConstructionWork+=Work; SetComponentTickEnabled(true);
}
void UGuLiBuildingLifecycleComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt,TickType,Function);
	const float Work=PendingConstructionWork; PendingConstructionWork=0;
	SetComponentTickEnabled(false);
	if (GetOwner()->HasAuthority() && Work>0) AddConstructionWork(Work);
}
