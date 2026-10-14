#include "Gameplay/Building/GuLiConstructedUnitComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "GuLiNetReplacement.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

namespace
{
const FGuLiHybridComponentCapabilityRegistration Conversion({TEXT("ConstructedUnit"),1,
    []()->const UClass* {return UGuLiConstructedUnitComponent::StaticClass();},
    [](const UActorComponent& C,TArray<uint8>& B){return CastChecked<UGuLiConstructedUnitComponent>(&C)->CaptureHybrid(B);},
    [](const UActorComponent& C,TConstArrayView<uint8> B){return const_cast<UGuLiConstructedUnitComponent*>(CastChecked<UGuLiConstructedUnitComponent>(&C))->RestoreHybrid(B,false);},
    [](UActorComponent& C,TConstArrayView<uint8> B,EGuLiHybridRestoreStage S)
    {
        auto* V=CastChecked<UGuLiConstructedUnitComponent>(&C);
        if (S==EGuLiHybridRestoreStage::Link) return V->RestoreHybrid(B,true);
        if (S==EGuLiHybridRestoreStage::Activate) V->ActivateHybridRestore();return true;
    },nullptr,[](UActorComponent& C,uint64 Tick,double){CastChecked<UGuLiConstructedUnitComponent>(&C)->AdvanceHybrid(Tick);},
    [](UActorComponent& C,const FGuLiHybridObjectState& S,uint64,bool){return CastChecked<UGuLiConstructedUnitComponent>(&C)->ApplyHybridLedger(S);}});
}
bool UGuLiConstructedUnitComponent::CaptureHybrid(TArray<uint8>& Bytes) const
{
    uint32 V=1;auto Event=HybridConversionEvent;auto Tick=EarliestConversionTick;bool Started=bConversionStarted;
    FGuLiHybridStateArchive A(128);A.Value(V);A.Value(Event);A.Value(Tick);A.Value(Started);return A.Finish(Bytes);
}
bool UGuLiConstructedUnitComponent::RestoreHybrid(TConstArrayView<uint8> Bytes,bool Apply)
{
    uint32 V=0;FGuid Event;uint64 Tick=0;bool Started=false;
    FGuLiHybridStateArchive A(Bytes,128);A.Value(V);A.Value(Event);A.Value(Tick);A.Value(Started);
    if (!A.Done() || V!=1 || (Started!=bool(Tick)) || (Event.IsValid() && !Started)) return false;
    if (Apply) {HybridConversionEvent=Event;EarliestConversionTick=Tick;bConversionStarted=Started;bNavigationRebuilt=false;}
    return true;
}
void UGuLiConstructedUnitComponent::ActivateHybridRestore()
{
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    // Restore is intentionally paused. Rebuilding navigation is preparation,
    // not battle advancement, so query the grant without the clock gate.
    if (bConversionStarted && Binding && H && Binding->GetState().LifeState==1 && H->HasSimulation(Binding->GetObjectKey()))
    {bConversionStarted=false;BeginConversion();}
}
void UGuLiConstructedUnitComponent::AdvanceHybrid(uint64 Tick)
{
    auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());auto* Life=Lifecycle.Get();
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Binding || !H || !Life || !Binding->CanSimulate() || !H->CanPublishFacts()
        || Binding->GetState().LifeState!=1 || !Life->IsCompleted() || Life->GetDefinition().CompletionUnitTypeId<=0) return;
    if (!bConversionStarted) BeginConversion();
    if (!bNavigationRebuilt || Tick<EarliestConversionTick || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return;
    if (HybridConversionEvent.IsValid())
    {
        FGuLiHybridTransactionResult R;
        if (H->FindTransactionResult(HybridConversionEvent,R) && !R.IsCommitted()
            && R.Status!=EGuLiHybridTransactionStatus::Paused && R.Status!=EGuLiHybridTransactionStatus::Deferred)
            H->Suspend(TEXT("Construction replacement rejected; retained population claim requires reconciliation"));
        return;
    }
    HybridConversionEvent=H->SubmitCapability(Binding->GetObjectKey(),FGuid::NewGuid(),
        FName(*(TEXT("replace/")+Binding->GetState().Definition.ToString())),Binding->GetState().RelationRevision,{},Binding->GetState().Team);
}
bool UGuLiConstructedUnitComponent::ApplyHybridLedger(const FGuLiHybridObjectState& State)
{
    const auto* S=State.Capabilities.Find(TEXT("replacement"));if (!S) return true;
    if (S->Version!=GuLiNet::Replacement::Version) return false;
    if (S->Data.IsEmpty()) return true;
    GuLiNet::Reader Reader({S->Data.GetData(),size_t(S->Data.Num())});GuLiNet::ObjectKey K;
    if (!Reader.Key(K) || !Reader.Done() || !K.Valid() || State.LifeState!=3) return false;
    FGuLiHybridObjectState Child;
    if (GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObject(GuLiNetBridge::ToUE(K),Child))
    {
        if (Child.Parent!=State.Key || !Child.Ordinal || Child.Key.Kind!=EGuLiHybridObjectKind::Soldier) return false;
        ConvertedSoldierId=FGuLiSoldierId(Child.Ordinal);
    }
    HybridConversionEvent.Invalidate();GetOwner()->SetActorEnableCollision(false);GetOwner()->SetActorHiddenInGame(true);
    bVisualRetired=true;SetComponentTickEnabled(false);return true;
}
