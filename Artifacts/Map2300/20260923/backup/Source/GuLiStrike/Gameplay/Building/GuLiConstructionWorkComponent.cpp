#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

UGuLiConstructionWorkComponent::UGuLiConstructionWorkComponent()
{
    PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickInterval=.1f;
}
void UGuLiConstructionWorkComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner()->HasAuthority()) GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->RegisterConstructionPrototype(*CastChecked<ACharacter>(GetOwner()));
}
void UGuLiConstructionWorkComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopWork();
    if (GetOwner()->HasAuthority()) GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->UnregisterConstructionPrototype(*CastChecked<ACharacter>(GetOwner()));
    Super::EndPlay(Reason);
}
bool UGuLiConstructionWorkComponent::AssignBuilding(UGuLiBuildingLifecycleComponent& Building)
{
    check(GetOwner()->HasAuthority());
    FVector Position; float Length;
    if (!PrepareBuilding(Building,Position,Length)) return false;
    StopWork(); ++TaskVersion; Target=&Building;
    Building.PrepareConstructionSlots(*CastChecked<ACharacter>(GetOwner()));
    return true;
}
bool UGuLiConstructionWorkComponent::PrepareBuilding(const UGuLiBuildingLifecycleComponent& Building,FVector& OutPosition,float& OutLength) const
{
    if (Building.GetTeam()!=CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetTeam()
        || Building.GetState().Phase!=EGuLiBuildingPhase::UnderConstruction
        || Building.GetConstructionAvailability()==EGuLiWorkPositionAvailability::NoValidPositions) return false;
    OutPosition=Building.GetGroundLocation();
    OutLength=GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->EstimateWorkDistance(OutPosition);
    return true;
}
void UGuLiConstructionWorkComponent::StopWork()
{
    if (auto* Building=Target.Get()) Building->ReleaseConstructionSlot(*CastChecked<ACharacter>(GetOwner()),Reservation);
    if (auto* Travel=GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>())
        if (!Travel->StopAtSafePoint()) Travel->CancelGroundMove();
    Target.Reset(); Reservation={}; SetComponentTickEnabled(false);
    bMoveRequested=bApplyingWork=bMoveFailed=bWaitingPosition=bNoPositions=false; NextPositionCheck=0;
}
void UGuLiConstructionWorkComponent::ReservePosition()
{
    if (!GetOwner()->HasAuthority() || IsTerminalFailure() || GetWorld()->GetTimeSeconds()<NextPositionCheck) return;
    NextPositionCheck=GetWorld()->GetTimeSeconds()+.2;
    auto& Building=*Target.Get();
    const auto* Registry=GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
    Rejected.RemoveAll([&](const auto& R) {
        const auto* RejectedBuilding=Registry->Find(R.Building);
        return !RejectedBuilding || !IsRejectionCurrent(R,*RejectedBuilding); });
    TArray<int32> Excluded; for (const auto& R : Rejected) if (R.Building==Building.GetState().InstanceId) Excluded.AddUnique(R.Slot);
    const auto Result=Building.TryReserveConstructionSlot(*CastChecked<ACharacter>(GetOwner()),TaskVersion,Excluded,Reservation,WorkPosition);
    bWaitingPosition=Result!=EGuLiWorkPositionAvailability::Available;
    bNoPositions=Result==EGuLiWorkPositionAvailability::NoValidPositions;
}
void UGuLiConstructionWorkComponent::RetryPosition()
{
    if (auto* Building=Target.Get(); Building && Reservation.IsValid())
    {
        Rejected.Add({Reservation.Slot,GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->WasPathUnreachable()
            ? TNumericLimits<double>::Max() : GetWorld()->GetTimeSeconds()+1,
            GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetStaticRevision(),GetOwner()->GetActorLocation(),Reservation.Generation,Reservation.Building});
        if (Rejected.Num()>64) Rejected.RemoveAt(0);
        Building->ReleaseConstructionSlot(*CastChecked<ACharacter>(GetOwner()),Reservation);
    }
    GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
    Reservation={}; bMoveRequested=bApplyingWork=bMoveFailed=false; bWaitingPosition=true;
    NextPositionCheck=GetWorld()->GetTimeSeconds()+.2; SetComponentTickEnabled(false);
}
bool UGuLiConstructionWorkComponent::IsRejectionCurrent(const FRejected& R,const UGuLiBuildingLifecycleComponent& Building) const
{
    if (R.Building!=Building.GetState().InstanceId || R.RetryAt<=GetWorld()->GetTimeSeconds()
        || !R.Start.Equals(GetOwner()->GetActorLocation(),100) || R.Generation!=Building.GetConstructionSlotGeneration()) return false;
    FBox Bounds(ForceInit); Bounds+=R.Start; Bounds+=Building.GetGroundLocation();
    return !GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->HasStaticChangesSince(R.Revision,Bounds.ExpandBy(2000));
}
bool UGuLiConstructionWorkComponent::AreAllPositionsRejected(const UGuLiBuildingLifecycleComponent& Building) const
{
    const auto Poses=Building.GetConstructionSlotPoses();
    if (Poses.IsEmpty()) return false;
    for (int32 I=0; I<Poses.Num(); ++I)
        if (!Rejected.ContainsByPredicate([&](const auto& R) { return R.Slot==I && IsRejectionCurrent(R,Building); })) return false;
    return true;
}
bool UGuLiConstructionWorkComponent::BeginBehaviorMove()
{
    if (IsTerminalFailure() || !Target->ValidateConstructionSlot(*CastChecked<ACharacter>(GetOwner()),Reservation)) return false;
    bMoveRequested=true;
    bMoveFailed=!GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginWorkMove(WorkPosition,50);
    return !bMoveFailed;
}
bool UGuLiConstructionWorkComponent::BeginBehaviorConstruction()
{
    if (!GetOwner()->HasAuthority() || GetBehaviorResult()!=EGuLiCommanderWorkResult::Arrived) return false;
    const auto* Building=Target.Get();
    GetOwner()->SetActorRotation(FRotator(0,(Building->GetGroundLocation()-GetOwner()->GetActorLocation()).Rotation().Yaw,0));
    bApplyingWork=true; SetComponentTickEnabled(true); return true;
}
bool UGuLiConstructionWorkComponent::IsTerminalFailure() const
{
    return !Target.IsValid() || Target->GetState().Phase==EGuLiBuildingPhase::Destroyed || bNoPositions
        || Target->GetTeam()!=CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetTeam();
}
EGuLiCommanderWorkPhase UGuLiConstructionWorkComponent::GetBehaviorPhase() const
{
    using Phase=EGuLiCommanderWorkPhase;
    if (bWaitingPosition) return Phase::ConstructionWaitingPosition;
    if (bApplyingWork) return Phase::ConstructionWorking;
    if (bMoveRequested) return GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsWaitingForPath()
        ? Phase::ConstructionWaitingPath : Phase::ConstructionMoving;
    return Reservation.IsValid() ? Phase::ConstructionReserved : Phase::ConstructionPrepared;
}
EGuLiCommanderWorkResult UGuLiConstructionWorkComponent::GetBehaviorResult() const
{
    using Result=EGuLiCommanderWorkResult;
    if (IsTerminalFailure()) return Result::Failed;
    if (Target->IsCompleted()) return Result::Complete;
    if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) return Result::Running;
    if (bWaitingPosition) return Result::WaitingPosition;
    if (!Reservation.IsValid()) return Result::None;
    if (!Target->ValidateConstructionSlot(*CastChecked<ACharacter>(GetOwner()),Reservation)) return Result::OutOfRange;
    const auto* Travel=GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>();
    if (bMoveFailed || (bMoveRequested && Travel->GetMoveStatus()==EGuLiEngineeringMoveStatus::Failed)) return Result::OutOfRange;
    if (!bMoveRequested) return Result::TargetReady;
    if (Travel->IsRouting() || Travel->IsWaitingForPath()) return Result::Running;
    if (FVector::Dist2D(GetOwner()->GetActorLocation(),WorkPosition)>100)
        return Travel->GetMoveStatus()==EGuLiEngineeringMoveStatus::Arrived ? Result::OutOfRange : Result::Running;
    return bApplyingWork ? Result::Running : Result::Arrived;
}
void UGuLiConstructionWorkComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,TickType,Function);
    if (IsTerminalFailure() || !GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()->IsAlive()) { StopWork(); return; }
    if (Target->IsCompleted() || GetBehaviorResult()!=EGuLiCommanderWorkResult::Running
        || UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) return;
    if (bApplyingWork && !GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsRouting()
        && Target->ValidateConstructionSlot(*CastChecked<ACharacter>(GetOwner()),Reservation)
        && FVector::Dist2D(GetOwner()->GetActorLocation(),WorkPosition)<=100) Target->AccumulateConstructionWork(Dt);
}
