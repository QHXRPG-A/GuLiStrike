#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiConstructionPresentationComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
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
    if (GetOwner()->HasAuthority())
    {
        GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()->OnDeath.AddDynamic(this, &ThisClass::StopWork);
        GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->OnTransportStarted.AddUObject(this, &ThisClass::SuspendConstructionEffects);
        ControlStateHandle = GetOwner()->FindComponentByClass<UGuLiExternalUnitControlComponent>()->OnStateApplied.AddWeakLambda(this, [this]
        {
            if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) SuspendConstructionEffects();
        });
    }
}
void UGuLiConstructionWorkComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopWork();
    if (auto* Travel = GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()) Travel->OnTransportStarted.RemoveAll(this);
    if (auto* Control = GetOwner()->FindComponentByClass<UGuLiExternalUnitControlComponent>()) Control->OnStateApplied.Remove(ControlStateHandle);
    if (auto* Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()) Health->OnDeath.RemoveDynamic(this, &ThisClass::StopWork);
    if (GetOwner()->HasAuthority()) GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->UnregisterConstructionPrototype(*CastChecked<ACharacter>(GetOwner()));
    Super::EndPlay(Reason);
}
bool UGuLiConstructionWorkComponent::QueryPosition(const UGuLiBuildingLifecycleComponent& Building,
    FGuLiConstructionSlotIntent& Out, FVector& Position) const
{
    TArray<int32> Excluded;
    for (const auto& Rejection : Rejected)
        if (IsRejectionCurrent(Rejection, Building)) Excluded.AddUnique(Rejection.Slot);
    const auto& Vehicle = *CastChecked<ACharacter>(GetOwner());
    const uint32 StableId = CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetStableActorId().Value;
    return Building.QueryConstructionPosition(Vehicle, StableId, Excluded, Out, Position) == EGuLiWorkPositionAvailability::Available;
}

bool UGuLiConstructionWorkComponent::AssignBuilding(UGuLiBuildingLifecycleComponent& Building, bool bAutomatic)
{
    check(GetOwner()->HasAuthority());
    FVector Position; float UnusedLength;
    if (!PrepareBuilding(Building, Position, UnusedLength)) return false;
    StopWork(); ++TaskVersion; Target = &Building; bAutomaticOrder = bAutomatic;
    OrderReason = TEXT("已接单，尚未占用施工位");
    SetComponentTickEnabled(true);
    return true;
}

bool UGuLiConstructionWorkComponent::PrepareBuilding(const UGuLiBuildingLifecycleComponent& Building,
    FVector& OutPosition, float& OutLength) const
{
    // A readiness probe only. Route length no longer participates in accepting work.
    OutLength = 0;
    FGuLiConstructionSlotIntent Candidate;
    return QueryPosition(Building, Candidate, OutPosition);
}

void UGuLiConstructionWorkComponent::StopWork()
{
    PublishConstructionActivity(false);
    if (auto* Building = Target.Get()) Building->ReleaseConstructionSlot(*CastChecked<ACharacter>(GetOwner()), Reservation);
    if (auto* Travel = GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()) Travel->StopAtSafePoint();
    Target.Reset(); Reservation = {}; Intent = {};
    SetComponentTickEnabled(false);
    bMoveRequested = bApplyingWork = bMoveFailed = bOrderCancelled = bAutomaticOrder = false;
    NextPositionCheck = 0;
}

void UGuLiConstructionWorkComponent::SelectPosition()
{
    if (!GetOwner()->HasAuthority() || IsTerminalFailure()) return;
    if (!QueryPosition(*Target, Intent, WorkPosition))
    {
        CancelOrder(TEXT("施工位已满或不可用，退单"));
        return;
    }
    OrderReason = TEXT("前往施工位，途中不占位");
}

void UGuLiConstructionWorkComponent::CancelOrder(const FString& Reason, bool bRejectPosition, bool bUnreachable)
{
    if (bOrderCancelled) return;
    PublishConstructionActivity(false);
    if (bRejectPosition && Intent.IsValid())
    {
        Rejected.Add({ Intent.Slot, bUnreachable ? TNumericLimits<double>::Max() : GetWorld()->GetTimeSeconds() + 1.0,
            GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetStaticRevision(),
            GetOwner()->GetActorLocation(), Intent.Generation, Intent.Building });
        if (Rejected.Num() > 64) Rejected.RemoveAt(0);
    }
    if (auto* Building = Target.Get()) Building->ReleaseConstructionSlot(*CastChecked<ACharacter>(GetOwner()), Reservation);
    Reservation = {}; bApplyingWork = false; bOrderCancelled = true;
    OrderReason = Reason;
    GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
}

void UGuLiConstructionWorkComponent::RetryPosition()
{
    const auto* Travel = GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>();
    CancelOrder(Travel->WasPathUnreachable() ? TEXT("施工位不可达，退单") : TEXT("施工位或路径失效，退单"),
        true, Travel->WasPathUnreachable());
}

bool UGuLiConstructionWorkComponent::IsRejectionCurrent(const FRejected& R,const UGuLiBuildingLifecycleComponent& Building) const
{
    if (R.Building!=Building.GetState().InstanceId || R.RetryAt<=GetWorld()->GetTimeSeconds()
        || R.Generation!=Building.GetConstructionSlotGeneration()) return false;
    // A lost arrival race excludes the position for the full second, even while
    // braking. Only unreachable-path failures depend on the previous start point.
    if (R.RetryAt != TNumericLimits<double>::Max()) return true;
    if (!R.Start.Equals(GetOwner()->GetActorLocation(),100)) return false;
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
    if (IsTerminalFailure()) return false;
    PublishConstructionActivity(false);
    if (!Target->IsConstructionPositionAvailable(*CastChecked<ACharacter>(GetOwner()), Intent))
    {
        CancelOrder(TEXT("目标施工位已被占用，退单"), true);
        return false;
    }
    bMoveRequested = true;
    bMoveFailed = !GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginWorkMove(WorkPosition, 50);
    if (bMoveFailed) RetryPosition();
    return !bMoveFailed;
}

bool UGuLiConstructionWorkComponent::BeginBehaviorConstruction()
{
    if (!GetOwner()->HasAuthority() || GetBehaviorResult() != EGuLiCommanderWorkResult::Arrived) return false;
    if (!Target->TryOccupyConstructionSlot(*CastChecked<ACharacter>(GetOwner()), TaskVersion, Intent, Reservation))
    {
        CancelOrder(TEXT("抵达抢位失败，退单"), true);
        return false;
    }
    GetOwner()->SetActorRotation(FRotator(0, (Target->GetGroundLocation() - GetOwner()->GetActorLocation()).Rotation().Yaw, 0));
    bApplyingWork = true;
    OrderReason = TEXT("已抵达并占位施工");
    return true;
}

bool UGuLiConstructionWorkComponent::IsTerminalFailure() const
{
    return bOrderCancelled || !Target.IsValid() || Target->GetState().Phase == EGuLiBuildingPhase::Destroyed
        || Target->GetTeam() != CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetTeam();
}

EGuLiCommanderWorkPhase UGuLiConstructionWorkComponent::GetBehaviorPhase() const
{
    using Phase = EGuLiCommanderWorkPhase;
    if (bOrderCancelled) return Phase::ConstructionCancelled;
    if (bApplyingWork) return Phase::ConstructionWorking;
    if (bMoveRequested) return GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsWaitingForPath()
        ? Phase::ConstructionWaitingPath : Phase::ConstructionMoving;
    return Intent.IsValid() ? Phase::ConstructionIntended : Phase::ConstructionPrepared;
}

EGuLiCommanderWorkResult UGuLiConstructionWorkComponent::GetBehaviorResult() const
{
    using Result = EGuLiCommanderWorkResult;
    if (IsTerminalFailure()) return Result::Failed;
    if (Target->IsCompleted()) return Result::Complete;
    if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) return Result::Running;
    if (!Intent.IsValid()) return Result::None;
    if (bApplyingWork && !Target->ValidateConstructionSlot(*CastChecked<ACharacter>(GetOwner()), Reservation)) return Result::OutOfRange;
    const auto* Travel = GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>();
    if (bMoveFailed || (bMoveRequested && Travel->GetMoveStatus() == EGuLiEngineeringMoveStatus::Failed)) return Result::OutOfRange;
    if (!bMoveRequested) return Result::TargetReady;
    if (Travel->IsRouting() || Travel->IsWaitingForPath()) return Result::Running;
    if (FVector::Dist2D(GetOwner()->GetActorLocation(), WorkPosition) > 100)
        return Travel->GetMoveStatus() == EGuLiEngineeringMoveStatus::Arrived ? Result::OutOfRange : Result::Running;
    return bApplyingWork ? Result::Running : Result::Arrived;
}

FString UGuLiConstructionWorkComponent::GetConstructionDebug() const
{
    return FString::Printf(TEXT("Building=%u Intent=%d Generation=%u Occupied=%d Cancelled=%d Reason=%s"),
        Intent.Building, Intent.Slot, Intent.Generation, Reservation.IsValid(), bOrderCancelled, *OrderReason);
}

bool UGuLiConstructionWorkComponent::HasLocalReplacement() const
{
    if (!bAutomaticOrder || bApplyingWork || !Target.IsValid()
        || UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())
        || GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit()) return false;
    const int32 Current = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->FindTerritoryIndex(GetOwner()->GetActorLocation());
    if (Current == INDEX_NONE || Target->GetState().TerritoryIndex == Current) return false;
    TArray<UGuLiBuildingLifecycleComponent*> LocalBuildings;
    GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(LocalBuildings, Current);
    for (const auto* Building : LocalBuildings)
    {
        FGuLiConstructionSlotIntent Candidate;
        FVector Position;
        if (QueryPosition(*Building, Candidate, Position)) return true;
    }
    return false;
}

void UGuLiConstructionWorkComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt, TickType, Function);
    if (!GetOwner()->HasAuthority()) return;
    if (!GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()->IsAlive()) { StopWork(); return; }
    if (bOrderCancelled) { PublishConstructionActivity(false); return; }
    if (GetWorld()->GetTimeSeconds() >= NextPositionCheck)
    {
        NextPositionCheck = GetWorld()->GetTimeSeconds() + .2;
        const auto* Building = Target.Get();
        if (!Building || Building->GetState().Phase == EGuLiBuildingPhase::Destroyed)
            CancelOrder(TEXT("建筑已销毁，退单"));
        else if (Building->IsCompleted()) CancelOrder(TEXT("建筑已完成，结束工单"));
        else if (Building->GetTeam() != CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetTeam())
            CancelOrder(TEXT("建筑归属变化，退单"));
        else if (Intent.IsValid() && !bApplyingWork
            && !Building->IsConstructionPositionAvailable(*CastChecked<ACharacter>(GetOwner()), Intent))
            CancelOrder(TEXT("途中施工位已被占用或失效，退单"), true);
        else if (HasLocalReplacement()) CancelOrder(TEXT("当前据点出现可用工单，取消外据点工单并重新接单"));
    }
    const auto* Travel = GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>();
    const bool bCanWork = !IsTerminalFailure() && GetBehaviorResult() == EGuLiCommanderWorkResult::Running
        && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())
        && bApplyingWork && !Travel->IsRouting() && !Travel->IsInTransit() && !Travel->IsWaitingForPath()
        && Travel->GetMoveStatus() != EGuLiEngineeringMoveStatus::Moving
        && Target->ValidateConstructionSlot(*CastChecked<ACharacter>(GetOwner()), Reservation)
        && FVector::Dist2D(GetOwner()->GetActorLocation(), WorkPosition) <= 100 && Dt > 0;
    PublishConstructionActivity(bCanWork);
    if (bCanWork) Target->AccumulateConstructionWork(Dt);
}

void UGuLiConstructionWorkComponent::SuspendConstructionEffects()
{
    PublishConstructionActivity(false);
}

void UGuLiConstructionWorkComponent::PublishConstructionActivity(bool bActive)
{
    if (!GetOwner()->HasAuthority()) return;
    auto* Building = bActive ? Target.Get() : nullptr;
    if (auto* Previous = ContributingTo.Get(); Previous && Previous != Building)
        Previous->SetConstructionContributor(*GetOwner(), false);
    ContributingTo = Building;
    if (Building) Building->SetConstructionContributor(*GetOwner(), true);
    if (auto* Visual = GetOwner()->FindComponentByClass<UGuLiConstructionPresentationComponent>())
        Visual->SetConstructionAuthority(Building);
}
