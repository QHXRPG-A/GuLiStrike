#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "GuLiNetHash.h"
#include "Engine/World.h"

namespace
{
constexpr uint32 WorkVersion=GuLiNet::FrozenWorkSchema;
bool Recipe(const FGuLiHybridObjectState& State,GuLiNet::FrozenWorkRecipe& Out)
{
    GuLiNet::ObjectRecord Work;Work.Grant.Object=GuLiNetBridge::ToCore(State.Key);Work.Grant.Faction=GuLiNetBridge::ToCore(State.Team);
    Work.Grant.SourceLease=GuLiNetBridge::ToCore(State.SpawnEvent);Work.Parent=GuLiNetBridge::ToCore(State.Parent);
    Work.BornAt=State.BornAt;Work.Definition=TCHAR_TO_UTF8(*State.Definition.ToString());
    if (!State.FrozenWork.IsEmpty()) Work.FrozenWork.assign(State.FrozenWork.GetData(),State.FrozenWork.GetData()+State.FrozenWork.Num());
    return GuLiNet::DecodeFrozenWork(Work,Out);
}
struct FWorkState
{
    FGuLiHybridObjectKey Key;FGuid Event;bool Submitted=false;uint64 SubmittedAt=0;
    void Serialize(FGuLiHybridStateArchive& A)
    {
        uint32 Version=WorkVersion;A.Value(Version);if (Version!=WorkVersion) A.Reject();
        A.Value(Key);A.Value(Event);A.Value(Submitted);A.Value(SubmittedAt);
        if (!Key.IsValid() || Key.Kind!=EGuLiHybridObjectKind::BusinessWork || !Event.IsValid() || (!Submitted && SubmittedAt)) A.Reject();
    }
};
class FTransactionWork final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    FGuLiHybridObjectState Record;FWorkState Private;bool Local=false;
    UGuLiHybridWorldSubsystem* Hybrid() const
    {return Owner.IsValid() && Owner->GetWorld()?Owner->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;}
public:
    explicit FTransactionWork(UGuLiHybridActorWorldSubsystem& In):Owner(&In){}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return WorkVersion;}
    bool Prepare(const FGuLiHybridObjectState& State) override
    {
        GuLiNet::FrozenWorkRecipe Frozen;if (!Recipe(State,Frozen)) return false;
        Record=State;Private={State.Key,GuLiNetBridge::ToUE(Frozen.Request.Header.Event),false,0};return true;
    }
    void SetAuthority(const FGuLiHybridObjectState& State,bool InLocal) override {Record=State;Local=InLocal;}
    void ApplyLedger(const FGuLiHybridObjectState& State,uint64,bool) override {Record=State;}
    void Advance(uint64 Tick,double) override
    {
        auto* H=Hybrid();if (!Local || Record.LifeState!=1 || Private.Submitted || !H || !H->CanPublishFacts()) return;
        const auto Accepted=H->SubmitAdmittedWork(Record.Key);
        if (Accepted.IsValid()) {Private.Submitted=true;Private.SubmittedAt=Tick;}
    }
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override {return false;}
    bool ApplyMotion(TConstArrayView<uint8>,uint64) override {return false;}
    bool Capture(TArray<uint8>& Bytes) const override
    {auto Copy=Private;FGuLiHybridStateArchive A(256);Copy.Serialize(A);return A.Finish(Bytes);}
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        FWorkState State;FGuLiHybridStateArchive A(Bytes,256);State.Serialize(A);GuLiNet::FrozenWorkRecipe Frozen;
        return A.Done() && Recipe(Record,Frozen) && State.Key==Record.Key
            && State.Event==GuLiNetBridge::ToUE(Frozen.Request.Header.Event) && (!State.Submitted || State.SubmittedAt>=Record.BornAt);
    }
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override
    {
        if (!Validate(Bytes)) return false;
        if (Stage==EGuLiHybridRestoreStage::Link) {FGuLiHybridStateArchive A(Bytes,256);Private.Serialize(A);return A.Done();}
        return true;
    }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override {return Restore(Bytes,Stage);}


    void Release() override {Private={};}
    void ResetResourceRequests(uint64) override {Private.Submitted=false;Private.SubmittedAt=0;}
};
const FGuLiHybridAdapterRegistration TransactionWorkFamily({TEXT("TransactionWork"),EGuLiHybridObjectKind::BusinessWork,WorkVersion,
    [](UGuLiHybridActorWorldSubsystem& World,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FTransactionWork>(World);},nullptr,nullptr,EGuLiHybridOwnershipPolicy::ParentWork,3,false});
}
