#include "Gameplay/Resources/GuLiResourceInventoryComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Gameplay/Resources/Private/GuLiHybridResourceInventoryState.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Engine/World.h"

namespace
{
bool DecodeInventory(TConstArrayView<uint8> Bytes,FGuLiHybridResourceInventoryState& Out)
{FGuLiHybridStateArchive A(Bytes,1024*1024);Out.Serialize(A);return A.Done();}
const FGuLiHybridComponentCapabilityRegistration InventoryCapability({TEXT("ResourceInventory"),FGuLiHybridResourceInventoryState::Version,
    []()->const UClass* {return UGuLiResourceInventoryComponent::StaticClass();},
    [](const UActorComponent& Component,TArray<uint8>& Bytes)
    {
        FGuLiHybridResourceInventoryState State;
        if (!CastChecked<UGuLiResourceInventoryComponent>(&Component)->CaptureState(State)) return false;
        FGuLiHybridStateArchive A(1024*1024);State.Serialize(A);return A.Finish(Bytes);
    },
    [](const UActorComponent& Component,TConstArrayView<uint8> Bytes)
    {
        FGuLiHybridResourceInventoryState State;return DecodeInventory(Bytes,State)
            && CastChecked<UGuLiResourceInventoryComponent>(&Component)->ValidateState(State);
    },
    [](UActorComponent& Component,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        if (Stage!=EGuLiHybridRestoreStage::Link) return true;
        FGuLiHybridResourceInventoryState State;return DecodeInventory(Bytes,State)
            && CastChecked<UGuLiResourceInventoryComponent>(&Component)->RestoreState(State);
    },nullptr,
    [](UActorComponent& Component,uint64,double Delta)
    {CastChecked<UGuLiResourceInventoryComponent>(&Component)->AdvanceLogical(float(Delta));},nullptr,nullptr,nullptr,nullptr,nullptr,
    [](const UActorComponent& C,FGuLiHybridManifestData& M,const FString& D,FString& E)
    {return CastChecked<UGuLiResourceInventoryComponent>(&C)->ExportDefinition(M,D,E);}});
}

UGuLiResourceInventoryComponent::UGuLiResourceInventoryComponent() {PrimaryComponentTick.bCanEverTick=false;}
FGuLiHybridObjectKey UGuLiResourceInventoryComponent::GetInventoryKey() const
{
    if (EntityKey.IsValid()) return EntityKey;
    const auto* R=GetWorld()?GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>():nullptr;
    return R?R->GetSettlementKey(GetOwner()):FGuLiHybridObjectKey();
}
bool UGuLiResourceInventoryComponent::BindEntity(UWorld& World,const FGuLiHybridObjectState& Object)
{
    if (GetOwner() || EntityKey.IsValid() || GetWorld()!=&World) return false;
    EntityWorld=&World;EntityKey=Object.Key;EntityDefinition=Object.Definition;
    const auto* A=World.GetSubsystem<UGuLiHybridActorWorldSubsystem>();if (!A) return false;
    const auto P=TEXT("resource/profile/")+Object.Definition.ToString()+TEXT("/");
    bCargo=A->GetManifest().Numbers.Contains(P+TEXT("cargo/capacity"));
    bProcessing=A->GetManifest().Numbers.Contains(P+TEXT("processor/capacity"));
    if ((!bCargo && !bProcessing) || !ConfigureFromManifest()) return false;
    auto* Hybrid=World.GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Hybrid->SubscribeObjectReceipt(this,GetInventoryKey(),[this](const auto& R){ApplyCommittedResource(R);}))
        Hybrid->OnTransactionCompleted.AddDynamic(this,&ThisClass::ApplyCommittedResource);return true;
}
void UGuLiResourceInventoryComponent::UnbindEntity()
{
    if (auto* W=EntityWorld.Get()) if (auto* H=W->GetSubsystem<UGuLiHybridWorldSubsystem>())
        {H->UnsubscribeObjectReceipt(this);H->OnTransactionCompleted.RemoveDynamic(this,&ThisClass::ApplyCommittedResource);}
    EntityKey={};EntityDefinition=NAME_None;EntityWorld.Reset();
}
void UGuLiResourceInventoryComponent::BeginPlay()
{
    Super::BeginPlay();
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()) && !ConfigureFromManifest())
    {GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Resource capability differs from its exported definition"));return;}
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {if (!H->SubscribeObjectReceipt(this,GetInventoryKey(),[this](const auto& R){ApplyCommittedResource(R);})) H->OnTransactionCompleted.AddDynamic(this,&ThisClass::ApplyCommittedResource);}
    else if (auto* R=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()) R->OnResourceSettlement.AddUObject(this,&ThisClass::ApplyCommittedResource);
}
void UGuLiResourceInventoryComponent::EndPlay(EEndPlayReason::Type Reason)
{
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) {H->UnsubscribeObjectReceipt(this);H->OnTransactionCompleted.RemoveDynamic(this,&ThisClass::ApplyCommittedResource);}
    if (auto* R=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()) R->OnResourceSettlement.RemoveAll(this);
    Super::EndPlay(Reason);
}
void UGuLiResourceInventoryComponent::ConfigureProcessing(float Rate,FName Rule)
{
    check(FMath::IsFinite(Rate) && Rate>0 && Rate<=60 && !Rule.IsNone());
    RatePerSecond=Rate;ProcessingRule=Rule;
}
int32 UGuLiResourceInventoryComponent::GetQueuedRawAmount() const
{int32 Total=0;for (const auto& E:Queue) Total+=E.Amount;return Total;}
void UGuLiResourceInventoryComponent::ApplyCommittedResource(const FGuLiHybridTransactionResult& R)
{ApplyResourceResult(R);}
bool UGuLiResourceInventoryComponent::RefreshCommittedInventory()
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridInventoryView View;
    if (!H || !H->GetCommittedInventory(GetInventoryKey(),View,CargoRevision,AppliedRevision)) return false;
    if (bCargo && View.CargoVersion>CargoRevision)
    {
        if (View.Cargo.Blue<0 || View.Cargo.Red<0 || int64(View.Cargo.Blue)+View.Cargo.Red>CargoCapacity)
        {H->Suspend(TEXT("Committed cargo exceeds the declared capability"));return false;}
        Cargo=View.Cargo;CargoRevision=View.CargoVersion;
    }
    if (bProcessing && View.ProcessingVersion>AppliedRevision)
    {
        TArray<FGuLiFactoryQueueEntry> Committed;int64 Total=0;
        for (const auto& Batch:View.Queue)
        {
            if (!Batch.Event.IsValid() || !Batch.Source.IsValid() || Batch.Amount.Blue<0 || Batch.Amount.Red<0
                || (Batch.Beneficiary!=EGuLiTeam::Red && Batch.Beneficiary!=EGuLiTeam::Blue))
            {H->Suspend(TEXT("Invalid committed inventory delivery"));return false;}
            const auto Append=[&](EGuLiResourceType Type,int32 Amount)
            {
                if (!Amount) return;auto& E=Committed.AddDefaulted_GetRef();E.ResourceType=Type;E.Amount=Amount;
                E.SettlementTeam=Batch.Beneficiary;E.DeliveryEvent=Batch.Event;E.Source=Batch.Source;Total+=Amount;
            };
            Append(EGuLiResourceType::Blue,Batch.Amount.Blue);Append(EGuLiResourceType::Red,Batch.Amount.Red);
        }
        if (Total>ProcessingCapacity || Committed.Num()>16384)
        {H->Suspend(TEXT("Committed processing inventory exceeds the declared capability"));return false;}
        Queue=MoveTemp(Committed);AppliedRevision=View.ProcessingVersion;
    }
    return true;
}
void UGuLiResourceInventoryComponent::ApplyResourceResult(const FGuLiHybridTransactionResult& R)
{
    const auto Key=GetInventoryKey();if (!Key.IsValid()) return;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();const bool Pending=R.Event==PendingEvent;
    const auto Fail=[&](const TCHAR* Reason){if (H) H->Suspend(Reason);else ensureMsgf(false,TEXT("%s"),Reason);};
    if (R.Status==EGuLiHybridTransactionStatus::Deferred || R.Status==EGuLiHybridTransactionStatus::Paused) return;
    // Receipts from independent workers can arrive in a different order. The
    // committed capability projection already contains their ordered state;
    // never replay FIFO deltas or compare unrelated work-decision versions.
    if (R.IsCommitted() && R.ResourceOperation!=EGuLiHybridResourceOperation::None
        && (R.ResourceSource==Key || R.Target==Key) && !RefreshCommittedInventory()) return;
    if (R.Event==PendingCargo) PendingCargo.Invalidate();
    if (Pending && R.IsCommitted() && R.ResourceOperation==EGuLiHybridResourceOperation::Process)
    {
        Accumulator=FMath::Max(0.f,Accumulator-1.f);
        NextProcessingTick=PendingAt+uint64(FMath::CeilToDouble(60./RatePerSecond));
    }

    if (Pending && !R.IsCommitted())
        Fail(TEXT("Admitted processing was finally rejected; retain its inventory for reconciliation instead of resubmitting every tick"));
    if (Pending) PendingEvent.Invalidate();
}
void UGuLiResourceInventoryComponent::AdvanceLogical(float Delta)
{
    const bool Reduced=UGuLiHotspotOptions::Enabled(GetWorld(),EGuLiHotspot::ReducedWork);
    RefreshCommittedInventory();
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {
        FGuLiHybridTransactionResult R;
        if (PendingCargo.IsValid() && H->FindTransactionResult(PendingCargo,R)) ApplyResourceResult(R);
        if (PendingEvent.IsValid() && H->FindTransactionResult(PendingEvent,R)) ApplyResourceResult(R);
    }
    // The revision lookup and pending receipt above are the wakeup conditions.
    // Empty/cargo-only inventories have no accumulator or scene work to advance.
    if (Reduced&&(!bProcessing||Queue.IsEmpty())) return;
    auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();auto* Owner=GetOwner();
    const auto* Life=Owner?Owner->FindComponentByClass<UGuLiBuildingLifecycleComponent>():nullptr;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState Object;
    const auto Key=GetInventoryKey();
    const bool CanAdvance=H?H->HasSimulation(Key) && (Reduced?H->GetObjectView(Key,Object):H->GetObject(Key,Object)) && Object.bCompleted
        :Owner && UGuLiHybridActorComponent::HasSimulation(Owner) && (!Life || Life->IsCompleted());
    if (!Resources || !bProcessing || !CanAdvance || Queue.IsEmpty()) return;
    if (!Queue[0].DeliveryEvent.IsValid())
    {
        if (H) H->Suspend(TEXT("Inventory entry has no committed delivery identity"));
        return;
    }
    if (!Resources->IsMiningRuntimeReady()) return;
    Accumulator=FMath::Min(Accumulator+FMath::Max(0.f,Delta)*RatePerSecond,2.f);
    if (PendingEvent.IsValid() || PendingCargo.IsValid() || Accumulator<1 || Resources->GetSettlementTick()<NextProcessingTick || (H && !H->CanPublishFacts())) return;
    if (!Key.IsValid()) return;
    FGuLiHybridResourceRequest R;R.Source=R.Target=Key;R.Event=FGuid::NewGuid();R.Operation=EGuLiHybridResourceOperation::Process;R.Rule=ProcessingRule;
    PendingAt=Resources->GetSettlementTick();PendingEvent=Resources->SubmitResourceSettlement(R);
}
bool UGuLiResourceInventoryComponent::CaptureState(FGuLiHybridResourceInventoryState& Out) const
{
    Out.RatePerSecond=RatePerSecond;Out.ProcessingRule=ProcessingRule;Out.Accumulator=Accumulator;Out.Queue=Queue;
    Out.PendingEvent=PendingEvent;Out.PendingAt=PendingAt;Out.NextProcessingTick=NextProcessingTick;Out.AppliedRevision=AppliedRevision;
    Out.Cargo=Cargo;Out.PendingCargo=PendingCargo;Out.CargoRevision=CargoRevision;
    return ValidateState(Out);
}
bool UGuLiResourceInventoryComponent::ValidateState(const FGuLiHybridResourceInventoryState& State) const
{
    auto Copy=State;FGuLiHybridStateArchive A(1024*1024);Copy.Serialize(A);
    if (!A.Good() || State.RatePerSecond!=RatePerSecond || State.ProcessingRule!=ProcessingRule) return false;
    // A delivery retains its admitted beneficiary after source death/capture.
    // The source's CURRENT faction is not a validity condition for this FIFO.
    int64 Total=0;for (const auto& E:State.Queue) Total+=E.Amount;
    return int64(State.Cargo.Blue)+State.Cargo.Red<=CargoCapacity && Total<=ProcessingCapacity
        && (bCargo || (!State.Cargo.Blue && !State.Cargo.Red && !State.PendingCargo.IsValid()))
        && (bProcessing || (State.Queue.IsEmpty() && !State.PendingEvent.IsValid()))
        && (!bProcessing || !ProcessingRule.IsNone());
}
bool UGuLiResourceInventoryComponent::RestoreState(const FGuLiHybridResourceInventoryState& State)
{
    if (!ValidateState(State)) return false;
    Accumulator=State.Accumulator;Queue=State.Queue;PendingEvent=State.PendingEvent;PendingAt=State.PendingAt;
    NextProcessingTick=State.NextProcessingTick;AppliedRevision=State.AppliedRevision;
    Cargo=State.Cargo;PendingCargo=State.PendingCargo;CargoRevision=State.CargoRevision;return true;
}
