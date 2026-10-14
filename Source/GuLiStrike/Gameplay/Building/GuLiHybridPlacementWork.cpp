#include "Gameplay/Building/GuLiHybridPlacementWork.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Engine/World.h"

FGuid GuLiHybridPlacementEvent(UWorld& World,const FGuid& Command,const TCHAR* Operation)
{
    return FGuLiHybridManifest::MakeStableKey(World.GetOutermost()->GetName(),EGuLiHybridObjectKind::Building,
        TEXT("placement/")+Command.ToString(EGuidFormats::Digits)+TEXT("/")+Operation).StableId;
}
namespace GuLiPlacementWork
{
constexpr uint32 Version=2;
int32 Definition(FName Name)
{
    const FString Text=Name.ToString(); if (!Text.StartsWith(TEXT("work/place/"))) return 0;
    const auto Suffix=Text.RightChop(11); return Suffix.IsNumeric()?FCString::Atoi(*Suffix):0;
}
struct FPrivate
{
    FGuLiHybridObjectKey Key,Target;
    FGuid Token;
    int64 Reservation=0;
    uint8 Stage=0; // Reserve, spawn, refund, retire.
    bool Submitted=false;
};
void Serialize(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Schema=Version; A.Value(Schema); if (Schema!=Version) A.Reject();
    A.Value(V.Key); A.Value(V.Target); A.Value(V.Token); A.Value(V.Reservation); A.Value(V.Stage); A.Value(V.Submitted);
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::BusinessWork || !V.Target.IsValid()
        || V.Target.Kind!=EGuLiHybridObjectKind::Building || V.Reservation<=0 || V.Stage>3
        || (V.Stage==0 && V.Token.IsValid()) || ((V.Stage==1 || V.Stage==2) && !V.Token.IsValid())) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V)
{ FGuLiHybridStateArchive A(Bytes,1024); Serialize(A,V); return A.Done(); }
}

/** An admitted operation owns its async work independently of a carrier,
 * its current player role and any UE component callbacks. */
class FGuLiHybridPlacementWork final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    FGuLiHybridObjectState Record;
    GuLiPlacementWork::FPrivate Pending;
    bool Local=false;
    UWorld* World() const {return Owner.IsValid()?Owner->GetWorld():nullptr;}
    UGuLiHybridWorldSubsystem* Hybrid() const {auto* W=World();return W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;}
    FGuid Event(const TCHAR* Operation) const {return GuLiHybridPlacementEvent(*World(),Record.Key.StableId,Operation);}
    GuLiPlacementWork::FPrivate Initial(const FGuLiHybridObjectState& V) const
    {
        GuLiPlacementWork::FPrivate P; P.Key=V.Key;
        P.Target=FGuLiHybridManifest::MakeStableKey(World()->GetOutermost()->GetName(),EGuLiHybridObjectKind::Building,
            TEXT("placement/")+V.Key.StableId.ToString(EGuidFormats::Digits));
        const auto ID=GuLiHybridPlacementEvent(*World(),V.Key.StableId,TEXT("reserve"));
        P.Reservation=static_cast<int64>((uint64(ID.A)<<32|ID.B)&uint64(MAX_int64)); if (!P.Reservation) P.Reservation=1;
        return P;
    }
    FGuid CurrentEvent() const {return Event(Pending.Stage==0?TEXT("reserve"):Pending.Stage==1?TEXT("spawn"):Pending.Stage==2?TEXT("refund"):TEXT("retire"));}
    void ApplyReceipt(const FGuLiHybridTransactionResult& R)
    {
        if (R.Event!=CurrentEvent() || R.Status==EGuLiHybridTransactionStatus::Deferred || R.Status==EGuLiHybridTransactionStatus::Paused) return;
        Pending.Submitted=false;
        if (Pending.Stage==0)
        {
            if (R.IsCommitted() && R.ReservationToken.IsValid()) {Pending.Token=R.ReservationToken;Pending.Stage=1;}
            else Pending.Stage=3;
        }
        else if (Pending.Stage==1) Pending.Stage=R.IsCommitted()?3:2;
        else if (Pending.Stage==2)
        {
            if (R.IsCommitted()) Pending.Stage=3;
            else Hybrid()->Suspend(TEXT("An admitted placement refund was finally rejected; reservation must remain visible"));
        }
        else if (!R.IsCommitted()) Hybrid()->Suspend(TEXT("An admitted placement work retirement was rejected"));
    }
public:
    explicit FGuLiHybridPlacementWork(UGuLiHybridActorWorldSubsystem& V):Owner(&V){}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return GuLiPlacementWork::Version;}
    bool Prepare(const FGuLiHybridObjectState& V) override
    {
        const auto D=GuLiPlacementWork::Definition(V.Definition);
        if (!Owner.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::BusinessWork || D<1
            || !Owner->GetManifest().Strings.Contains(FString::Printf(TEXT("adapter/definition/building/%d"),D))
            || !V.Parent.IsValid() || !V.SpawnEvent.IsValid()) return false;
        FGuLiHybridObjectState Source;
        if (!Hybrid() || !Hybrid()->GetObject(V.Parent,Source)
            || !Owner->GetManifest().Rules.ContainsByPredicate([&](const auto& R)
            {return R.Kind==EGuLiHybridRuleKind::Spawn && R.SpawnKind==EGuLiHybridObjectKind::BusinessWork
                && R.SourceDefinition==Source.Definition.ToString() && R.TargetDefinition==V.Definition.ToString();})) return false;
        Record=V; Pending=Initial(V); return true;
    }
    void SetAuthority(const FGuLiHybridObjectState& V,bool bLocal) override {Record=V;Local=bLocal;}
    void ApplyLedger(const FGuLiHybridObjectState& V,uint64,bool) override {Record=V;}
    void Advance(uint64,double) override
    {
        auto* H=Hybrid(); if (!Local || !H || Record.LifeState!=1 || !H->CanPublishFacts()) return;
        FGuLiHybridTransactionResult Receipt;
        if (H->FindTransactionResult(CurrentEvent(),Receipt)) ApplyReceipt(Receipt);
        if (!H->CanPublishFacts() || Pending.Submitted || Record.LifeState!=1) return;
        const int32 D=GuLiPlacementWork::Definition(Record.Definition);
        if (Pending.Stage==1)
        {
            FGuLiHybridObjectState Existing;
            if (H->GetObject(Pending.Target,Existing) && Existing.SpawnEvent==Event(TEXT("spawn")) && Existing.Parent==Record.Key)
            {Pending.Stage=3;return;}
            const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog(); const auto* Definition=Catalog?Catalog->FindById(D):nullptr;
            FString Reason;
            if (!Definition || !UGuLiBuildingPlacementComponent::ValidateAdmittedPlacement(*World(),*Definition,Record.Location,Record.FacingYawDegrees,Record.Controller,&Reason))
            {
                UE_LOG(LogTemp,Warning,TEXT("Hybrid placement work %s definition=%d refunds after site validation: %s at %s"),
                    *Record.Key.StableId.ToString(),D,Definition?*Reason:TEXT("definition-unavailable"),*Record.Location.ToString());
                Pending.Stage=2;return;
            }
            FGuLiHybridObjectRequest R; R.Source=Record.Key; R.Target=Pending.Target; R.Event=CurrentEvent();
            R.Rule=FName(*FString::Printf(TEXT("place/building/%d"),D)); R.Operation=EGuLiHybridObjectOperation::Spawn;
            R.Location=Record.Location;R.FacingYaw=Record.FacingYawDegrees;R.ReservationRequest=Pending.Reservation;R.ReservationToken=Pending.Token;
            Pending.Submitted=H->SubmitObjectOperation(R).IsValid();
        }
        else if (Pending.Stage==3)
        {
            FGuLiHybridObjectRequest R;R.Source=R.Target=Record.Key;R.Event=CurrentEvent();R.Operation=EGuLiHybridObjectOperation::Destroy;
            R.Rule=FName(*FString::Printf(TEXT("retire/work/place/%d"),D));R.ExpectedRelationRevision=Record.RelationRevision;
            Pending.Submitted=H->SubmitObjectOperation(R).IsValid();
        }
        else
        {
            FGuLiHybridEconomyRequest R;R.Source=Record.Key;R.Event=CurrentEvent();
            R.Rule=FName(*FString::Printf(TEXT("%s/building/%d"),Pending.Stage==0?TEXT("reserve"):TEXT("refund"),D));
            R.Operation=Pending.Stage==0?EGuLiHybridEconomyOperation::Reserve:EGuLiHybridEconomyOperation::Refund;
            R.ReservationRequest=Pending.Reservation;R.ReservationToken=Pending.Token;
            Pending.Submitted=H->SubmitEconomy(R).IsValid();
        }
    }
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override {return false;}
    bool ApplyMotion(TConstArrayView<uint8>,uint64) override {return false;}
    bool Capture(TArray<uint8>& Bytes) const override
    {auto V=Pending;FGuLiHybridStateArchive A(1024);GuLiPlacementWork::Serialize(A,V);return A.Finish(Bytes);}
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiPlacementWork::FPrivate V; if (!GuLiPlacementWork::Decode(Bytes,V)) return false;const auto Base=Initial(Record);
        return V.Key==Record.Key && V.Target==Base.Target && V.Reservation==Base.Reservation;
    }


    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override
    {
        if (!Validate(Bytes)) return false;
        if (Stage==EGuLiHybridRestoreStage::Link) return GuLiPlacementWork::Decode(Bytes,Pending);
        return true;
    }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override {return Restore(Bytes,Stage);}
    void Release() override {Local=false;}
};
static const FGuLiHybridAdapterRegistration GGuLiPlacementWorkAdapterFamily({TEXT("PlacementWork"),EGuLiHybridObjectKind::BusinessWork,GuLiPlacementWork::Version,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridPlacementWork>(Owner);},nullptr,nullptr,EGuLiHybridOwnershipPolicy::SharedGroup,0,false,TEXT("match/resources")});
