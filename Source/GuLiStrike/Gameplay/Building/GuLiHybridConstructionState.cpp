#include "Gameplay/Building/Private/GuLiHybridConstructionState.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

namespace
{
bool DecodeConstruction(TConstArrayView<uint8> Bytes,FGuLiHybridConstructionPrivateState& Out)
{FGuLiHybridStateArchive A(Bytes,4096);Out.Serialize(A);return A.Done();}
bool DecodePublic(TConstArrayView<uint8> Bytes,FGuLiHybridConstructionPublicState& Out)
{FGuLiHybridStateArchive A(Bytes,4096);Out.Serialize(A);return A.Done();}
const FGuLiHybridComponentCapabilityRegistration ConstructionCapability({TEXT("Construction"),3,
    []()->const UClass* {return UGuLiBuildingLifecycleComponent::StaticClass();},
    [](const UActorComponent& Component,TArray<uint8>& Bytes)
    {
        FGuLiHybridConstructionPrivateState State;
        if (!CastChecked<UGuLiBuildingLifecycleComponent>(&Component)->CaptureHybridConstruction(State)) return false;
        FGuLiHybridStateArchive A(4096);State.Serialize(A);return A.Finish(Bytes);
    },
    [](const UActorComponent& Component,TConstArrayView<uint8> Bytes)
    {
        FGuLiHybridConstructionPrivateState State;return DecodeConstruction(Bytes,State)
            && CastChecked<UGuLiBuildingLifecycleComponent>(&Component)->ValidateHybridConstruction(State);
    },
    [](UActorComponent& Component,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        if (Stage!=EGuLiHybridRestoreStage::Link) return true;
        FGuLiHybridConstructionPrivateState State;return DecodeConstruction(Bytes,State)
            && CastChecked<UGuLiBuildingLifecycleComponent>(&Component)->RestoreHybridConstruction(State);
    },
    [](TConstArrayView<uint8> Bytes)
    {
        FGuLiHybridConstructionPrivateState State;TArray<FGuLiHybridObjectKey> Out;
        if (!DecodeConstruction(Bytes,State)) return Out;
        if (State.Prototype.IsValid()) Out.Add(State.Prototype);
        for (const auto& Slot:State.Slots) if (Slot.Owner.IsValid()) Out.AddUnique(Slot.Owner);
        for (const auto& Key:State.Contributors) Out.AddUnique(Key);return Out;
    },
    [](UActorComponent& Component,uint64 Tick,double Delta)
    {
        auto* Life=CastChecked<UGuLiBuildingLifecycleComponent>(&Component);
        Life->AdvanceHybridContributors(Tick,float(Delta));Life->AdvanceHybridConstruction(Tick);
    },nullptr,nullptr,
    [](const UActorComponent& Component,TArray<uint8>& Bytes)
    {
        FGuLiHybridConstructionPublicState State;
        if (!CastChecked<UGuLiBuildingLifecycleComponent>(&Component)->CaptureHybridConstructionPublic(State)) return false;
        FGuLiHybridStateArchive A(4096);State.Serialize(A);return A.Finish(Bytes);
    },
    [](const UActorComponent& Component,TConstArrayView<uint8> Bytes)
    {FGuLiHybridConstructionPublicState State;return DecodePublic(Bytes,State) && CastChecked<UGuLiBuildingLifecycleComponent>(&Component)->ValidateHybridConstructionPublic(State);},
    [](UActorComponent& Component,TConstArrayView<uint8> Bytes)
    {FGuLiHybridConstructionPublicState State;return DecodePublic(Bytes,State) && CastChecked<UGuLiBuildingLifecycleComponent>(&Component)->ApplyHybridConstructionPublic(State);}});
}

TArray<FGuLiHybridObjectKey> UGuLiBuildingLifecycleComponent::GetConstructionContributors() const
{
    TArray<FGuLiHybridObjectKey> Result;
    for (const auto& Weak:ActiveContributors)
        if (const auto* Binding=UGuLiHybridActorComponent::Find(Weak.Get())) Result.Add(Binding->GetObjectKey());
    Result.Sort([](const auto& A,const auto& B){return A.StableId<B.StableId;}); return Result;
}
bool UGuLiBuildingLifecycleComponent::CaptureHybridConstructionPublic(FGuLiHybridConstructionPublicState& Out) const
{
    Out={}; Out.Work=State.WorkDone; Out.Generation=SlotGeneration; Out.PendingSlots=bSlotsPending;
    for (const auto& Slot:ConstructionSlots)
    {
        auto& Copy=Out.Slots.AddDefaulted_GetRef(); Copy.Pose=Slot.Pose;
        if (const auto* Binding=UGuLiHybridActorComponent::Find(Slot.Owner.Get()))
        {Copy.Owner=Binding->GetObjectKey(); Copy.Task=Slot.Task;}
    }
    Out.Contributors=GetConstructionContributors(); return ValidateHybridConstructionPublic(Out);
}
bool UGuLiBuildingLifecycleComponent::ValidateHybridConstructionPublic(const FGuLiHybridConstructionPublicState& In) const
{
    auto Copy=In; FGuLiHybridStateArchive A(4096); Copy.Serialize(A);
    if (!A.Good() || State.DefinitionId<=0 || In.Work>GetDefinition().ConstructionWork) return false;
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    for (const auto& Slot:In.Slots) if (Slot.Owner.IsValid())
    {FGuLiHybridObjectState O; if (!H || !H->GetObject(Slot.Owner,O) || O.Team!=GetTeam()) return false;}
    return true;
}
bool UGuLiBuildingLifecycleComponent::ApplyHybridConstructionPublic(const FGuLiHybridConstructionPublicState& In)
{
    if (!ValidateHybridConstructionPublic(In)) return false;
    // A delayed simulation sample cannot undo authoritative completion/death.
    if (State.Phase!=EGuLiBuildingPhase::UnderConstruction) return true;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    TArray<FGuLiConstructionSlot> Linked; TSet<TWeakObjectPtr<AActor>> Workers;
    for (const auto& Slot:In.Slots)
    {
        auto& Copy=Linked.AddDefaulted_GetRef(); Copy.Pose=Slot.Pose;
        if (Slot.Owner.IsValid()) if (auto* Actor=Cast<ACharacter>(H->FindBoundActor(Slot.Owner)))
        {Copy.Owner=Actor;Copy.Task=Slot.Task;if (In.Contributors.Contains(Slot.Owner)) Workers.Add(Actor);}
    }
    const bool Active=!Workers.IsEmpty(),Changed=State.WorkDone!=In.Work || State.bHasActiveBuilders!=Active;
    ConstructionSlots=MoveTemp(Linked); ActiveContributors=MoveTemp(Workers);
    SlotGeneration=In.Generation; bSlotsPending=In.PendingSlots;
    State.WorkDone=In.Work; State.bHasActiveBuilders=Active;
    if (Changed) OnConstructionStateChanged.Broadcast();
    return true;
}

bool UGuLiBuildingLifecycleComponent::CaptureHybridConstruction(FGuLiHybridConstructionPrivateState& Out) const
{
    Out={}; Out.Work=State.WorkDone; Out.PendingWork=PendingConstructionWork; Out.Generation=SlotGeneration;
    Out.NextSample=NextSlotSample; Out.PendingSlots=bSlotsPending; Out.Completion=HybridCompletionEvent;
    const auto Key=[](const AActor* Actor,FGuLiHybridObjectKey& K)
    {
        const auto* Identity=UGuLiHybridActorComponent::Find(Actor);
        if (!Identity || Identity->GetObjectKey().Kind!=EGuLiHybridObjectKind::Engineer) return false;
        K=Identity->GetObjectKey(); return true;
    };
    if (ConstructionPrototype.IsValid() && !Key(ConstructionPrototype.Get(),Out.Prototype)) return false;
    for (const auto& Slot:ConstructionSlots)
    {
        FGuLiHybridConstructionPrivateState::FSlot Copy; Copy.Pose=Slot.Pose; Copy.Task=Slot.Owner.IsValid()?Slot.Task:0;
        if (Slot.Owner.IsValid() && !Key(Slot.Owner.Get(),Copy.Owner)) return false;
        Out.Slots.Add(Copy);
    }
    for (const auto& Weak:ActiveContributors)
    { FGuLiHybridObjectKey K; if (!Key(Weak.Get(),K)) return false; Out.Contributors.Add(K); }
    Out.Contributors.Sort([](const auto& A,const auto& B){return A.StableId<B.StableId;});
    return ValidateHybridConstruction(Out);
}
bool UGuLiBuildingLifecycleComponent::ValidateHybridConstruction(const FGuLiHybridConstructionPrivateState& In) const
{
    auto Copy=In; FGuLiHybridStateArchive A(4096); Copy.Serialize(A);
    if (!A.Good() || In.Work>GetDefinition().ConstructionWork) return false;
    const auto* H=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    const auto Reference=[&](const FGuLiHybridObjectKey& Key)
    {
        if (!Key.IsValid()) return true; FGuLiHybridObjectState O;
        return H && H->GetObject(Key,O) && O.Team==GetTeam() && O.LifeState!=3;
    };
    if (!Reference(In.Prototype)) return false;
    for (const auto& Slot:In.Slots) if (!Reference(Slot.Owner)) return false;
    for (const auto& Key:In.Contributors) if (!Reference(Key)) return false;
    return true;
}
bool UGuLiBuildingLifecycleComponent::RestoreHybridConstruction(const FGuLiHybridConstructionPrivateState& In)
{
    if (!ValidateHybridConstruction(In)) return false;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const auto Character=[&](const FGuLiHybridObjectKey& Key){return Cast<ACharacter>(H->FindBoundActor(Key));};
    auto* Prototype=In.Prototype.IsValid()?Character(In.Prototype):nullptr;
    if (In.Prototype.IsValid() && !Prototype) return false;
    TArray<FGuLiConstructionSlot> Linked; TSet<TWeakObjectPtr<AActor>> Workers;
    for (const auto& Slot:In.Slots)
    {
        FGuLiConstructionSlot Copy; Copy.Pose=Slot.Pose; Copy.Task=Slot.Task;
        if (Slot.Owner.IsValid()) { Copy.Owner=Character(Slot.Owner); if (!Copy.Owner.IsValid()) return false; }
        Linked.Add(Copy);
    }
    for (const auto& Key:In.Contributors)
    { auto* Actor=Character(Key); if (!Actor) return false; Workers.Add(Actor); }
    State.WorkDone=In.Work; PendingConstructionWork=In.PendingWork; SlotGeneration=In.Generation;
    NextSlotSample=In.NextSample; bSlotsPending=In.PendingSlots; HybridCompletionEvent=In.Completion;
    ConstructionPrototype=Prototype; ConstructionSlots=MoveTemp(Linked); ActiveContributors=MoveTemp(Workers);
    State.bHasActiveBuilders=!ActiveContributors.IsEmpty(); OnConstructionStateChanged.Broadcast(); return true;
}
