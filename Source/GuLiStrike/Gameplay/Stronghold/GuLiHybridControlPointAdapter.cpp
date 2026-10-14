#include "Gameplay/Stronghold/GuLiHybridControlPoint.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Engine/World.h"

namespace GuLiHybridControlPoint
{
constexpr uint32 Version=2;
struct FPrivate {FGuLiHybridObjectKey Key;FGuid Pending;uint64 At=0,Relation=0;};
void Serialize(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Schema=Version;A.Value(Schema);A.Value(V.Key);A.Value(V.Pending);A.Value(V.At);A.Value(V.Relation);
    if (Schema!=Version || !V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::Territory
        || (!V.Pending.IsValid() && (V.At || V.Relation))) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V) {FGuLiHybridStateArchive A(Bytes);Serialize(A,V);return A.Done();}
}
class FGuLiHybridControlPointAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<AGuLiTerritoryOutpostActor> Actor;
    FGuLiHybridObjectState Record;
    FGuLiHybridControlPointState Point;
    GuLiHybridControlPoint::FPrivate Private;
    bool Local=false;
    UWorld* World() const {return Owner.IsValid()?Owner->GetWorld():nullptr;}
    bool Create()
    {
        if (Actor.IsValid()) return true;auto* W=World();auto* Resources=W?W->GetSubsystem<UGuLiResourceWorldSubsystem>():nullptr;
        const auto* Map=Resources?Resources->GetMapDefinition():nullptr;const int32 Index=Point.Index;
        if (!Map || !Map->Territories.IsValidIndex(Index) || Record.Key.Kind!=EGuLiHybridObjectKind::Territory) return false;
        const FString Definition=Record.Definition.ToString();const FString Suffix=Definition.RightChop(10);
        if (!Definition.StartsWith(TEXT("territory/")) || !Suffix.IsNumeric() || FCString::Atoi(*Suffix)<=0) return false;
        const FTransform Transform(FRotator::ZeroRotator,Record.Location+FVector(0,0,500));
        auto* P=W->SpawnActorDeferred<AGuLiTerritoryOutpostActor>(AGuLiTerritoryOutpostActor::StaticClass(),Transform,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!P) return false;Actor=P;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(P);P->AddInstanceComponent(Binding);Binding->RegisterComponent();Binding->Install(Record,Local);
        P->InitializeOutpost(uint8(Index),Map->Territories[Index].TerritoryId,Point.Owner,FCString::Atoi(*Suffix));
        P->FinishSpawning(Transform);P->SetReplicates(false);P->SetActorTickEnabled(false);
        if (!Resources->RegisterHybridOutpost(*P,Index)) {Release();return false;}
        Resources->ApplyHybridControlPoint(Record,false);return true;
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        GuLiHybridControlPoint::FPrivate V;if (!GuLiHybridControlPoint::Decode(Bytes,V) || V.Key!=Record.Key) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) {Release();return true;}
        if (Stage==EGuLiHybridRestoreStage::Create) return Create();
        if (Stage==EGuLiHybridRestoreStage::Link) Private=V;
        if (Stage==EGuLiHybridRestoreStage::Activate) ApplyLedger(Record,0,false);
        return true;
    }
public:
    explicit FGuLiHybridControlPointAdapter(UGuLiHybridActorWorldSubsystem& Value):Owner(&Value) {}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return GuLiHybridControlPoint::Version;}
    bool Prepare(const FGuLiHybridObjectState& State) override {Record=State;Point=GuLiHybridControlPoint::Read(State);Private.Key=State.Key;return Point.Index!=INDEX_NONE && Create();}
    void SetAuthority(const FGuLiHybridObjectState& State,bool IsLocal) override
    {Record=State;Point=GuLiHybridControlPoint::Read(State);Local=IsLocal;if (Actor.IsValid()) Actor->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,Local);}
    void Advance(uint64 Tick,double) override
    {
        auto* H=World()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!Local || !Actor.IsValid() || H->IsBattlePaused()) return;
        if (Private.Pending.IsValid())
        {
            FGuLiHybridTransactionResult Result;
            if (H->FindTransactionResult(Private.Pending,Result))
            {
                if (Result.Status==EGuLiHybridTransactionStatus::Deferred || Result.Status==EGuLiHybridTransactionStatus::Paused) return;
                if (!Result.IsCommitted() && Record.RelationRevision==Private.Relation)
                {H->Suspend(TEXT("Control point request was rejected without a concurrent state change"));return;}
                Private={Record.Key};
            }
            else if (Record.RelationRevision>Private.Relation && Point.SampledAt>=Private.At) Private={Record.Key};
            else return;
        }
        if (Tick<Point.SampledAt+Point.SamplePeriod) return;
        const auto Counts=World()->GetSubsystem<UGuLiResourceWorldSubsystem>()->GetHybridCaptureCounts(Tick,Point.Index);
        const uint32 Red=uint32(Counts.X),Blue=uint32(Counts.Y);
        const auto Event=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/ControlPointRequests"),EGuLiHybridObjectKind::Territory,
            Record.Key.StableId.ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/%u/%llu/%llu"),Record.Key.LifeGeneration,Tick,Record.RelationRevision)).StableId;
        const auto Pending=GuLiHybridControlPoint::Submit(*H,Record,Event,Red,Blue);
        if (Pending.IsValid()) {Private.Pending=Pending;Private.At=Tick;Private.Relation=Record.RelationRevision;}
    }
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override {return false;}
    bool ApplyMotion(TConstArrayView<uint8>,uint64) override {return false;}
    void ApplyLedger(const FGuLiHybridObjectState& State,uint64,bool Effects) override
    {
        Record=State;Point=GuLiHybridControlPoint::Read(State);if (!Actor.IsValid()) return;
        Actor->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,Local);
        World()->GetSubsystem<UGuLiResourceWorldSubsystem>()->ApplyHybridControlPoint(State,Effects);
    }
    bool Capture(TArray<uint8>& Bytes) const override
    {auto V=Private;FGuLiHybridStateArchive A;GuLiHybridControlPoint::Serialize(A,V);return A.Finish(Bytes);}
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {GuLiHybridControlPoint::FPrivate V;return GuLiHybridControlPoint::Decode(Bytes,V) && V.Key==Record.Key;}
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override {return RestoreImpl(Bytes,Stage);}
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override {return RestoreImpl(Bytes,Stage);}
    void Release() override
    {
        if (auto* P=Actor.Get())
        {if (auto* R=World()->GetSubsystem<UGuLiResourceWorldSubsystem>()) R->UnregisterHybridOutpost(*P,Point.Index);P->Destroy();}
        Actor.Reset();
    }
};
static const FGuLiHybridAdapterRegistration ControlPointFamily({TEXT("ControlPoint"),EGuLiHybridObjectKind::Territory,GuLiHybridControlPoint::Version,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridControlPointAdapter>(Owner);},nullptr,nullptr,EGuLiHybridOwnershipPolicy::SharedGroup,3,false,TEXT("match/resources")});
