#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

void UGuLiBuildingLifecycleComponent::AdvanceHybridContributors(uint64 At,float Dt)
{
    const auto* Identity=UGuLiHybridActorComponent::Find(GetOwner());
    if (!Identity || !UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !FMath::IsFinite(Dt) || Dt<=0
        || State.Phase!=EGuLiBuildingPhase::UnderConstruction) return;
    auto* Registry=GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
    TArray<ACharacter*> Vehicles; Registry->GetConstructionVehicles(Vehicles);
    Vehicles.Sort([](const ACharacter& A,const ACharacter& B)
    {const auto* X=UGuLiHybridActorComponent::Find(&A); const auto* Y=UGuLiHybridActorComponent::Find(&B);
     return X && Y && X->GetObjectKey().StableId<Y->GetObjectKey().StableId;});
    if (!ConstructionPrototype.IsValid()) for (auto* Vehicle:Vehicles)
        if (const auto* Source=UGuLiHybridActorComponent::Find(Vehicle); Source && Source->GetState().Team==GetTeam())
        {PrepareConstructionSlots(*Vehicle); break;}
    TMap<ACharacter*,FGuLiHybridConstructionFact> Facts;
    for (auto* Vehicle:Vehicles)
    {
        const auto* Source=UGuLiHybridActorComponent::Find(Vehicle);
        const auto* Work=Vehicle->FindComponentByClass<UGuLiConstructionWorkComponent>();
        if (!Source || !Work || Source->GetObjectKey().Kind!=EGuLiHybridObjectKind::Engineer
            || Source->GetState().LifeState!=1 || Source->GetState().Team!=GetTeam()) continue;
        FGuLiHybridConstructionFact Fact;
        if (Source->CanSimulate()) Fact=Work->GetHybridWorkFact(At);
        else if (!Work->GetObservedHybridFact(Fact)) continue;
        if (Fact.Target!=Identity->GetObjectKey() || !Fact.bOccupy || !Fact.Task
            || Fact.Intent.Building!=State.InstanceId || Fact.Intent.Generation!=SlotGeneration
            || !ConstructionSlots.IsValidIndex(Fact.Intent.Slot) || bSlotsPending) continue;
        if (FVector::Dist2D(Vehicle->GetActorLocation(),ConstructionSlots[Fact.Intent.Slot].Pose.GetLocation())>100) continue;
        Facts.Add(Vehicle,Fact);
    }
    // A stale/lost request cannot hold a slot or keep supplying construction work.
    for (int32 Index=0; Index<ConstructionSlots.Num(); ++Index)
    {
        auto& Slot=ConstructionSlots[Index];
        const auto* Fact=Facts.Find(Slot.Owner.Get());
        if (!Fact || Fact->Task!=Slot.Task || Fact->Intent.Slot!=Index)
        {Slot.Owner.Reset(); Slot.Task=0;}
    }
    for (auto* Vehicle:Vehicles) if (const auto* Fact=Facts.Find(Vehicle))
    {
        auto& Slot=ConstructionSlots[Fact->Intent.Slot];
        if (!Slot.Owner.IsValid()) {Slot.Owner=Vehicle; Slot.Task=Fact->Task;}
    }
    TSet<TWeakObjectPtr<AActor>> Contributors;
    for (const auto& Slot:ConstructionSlots)
        if (auto* Vehicle=Slot.Owner.Get()) if (const auto* Fact=Facts.Find(Vehicle); Fact && Fact->Task==Slot.Task && Fact->bReady)
            Contributors.Add(Vehicle);
    ActiveContributors=MoveTemp(Contributors); const bool Any=!ActiveContributors.IsEmpty();
    if (State.bHasActiveBuilders!=Any) {State.bHasActiveBuilders=Any; OnConstructionStateChanged.Broadcast();}
    if (Any) AccumulateConstructionWork(Dt*ActiveContributors.Num());
}
