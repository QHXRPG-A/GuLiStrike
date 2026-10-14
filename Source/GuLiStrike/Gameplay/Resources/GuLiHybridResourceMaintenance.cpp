#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "GuLiNetResourceMaintenance.h"
#include "Engine/World.h"

namespace GuLiHybridMaintenance
{
constexpr uint32 Version=1;
struct FPrivate {FGuLiHybridObjectKey Key;FGuid Pending;uint64 Relation=0;};
void Serialize(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Schema=Version;A.Value(Schema);A.Value(V.Key);A.Value(V.Pending);A.Value(V.Relation);
    if (Schema!=Version || !V.Key.IsValid()) A.Reject();
}
bool Read(const FGuLiHybridObjectState& O,GuLiNet::ResourceMaintenanceState& Out)
{
    const auto* C=O.Capabilities.Find(FName(UTF8_TO_TCHAR(GuLiNet::ResourceMaintenanceCapability)));
    if (!C || C->Version!=GuLiNet::ResourceMaintenanceVersion) return false;
    GuLiNet::Reader R({C->Data.GetData(),size_t(C->Data.Num())});return GuLiNet::ReadResourceMaintenance(R,Out) && R.Done();
}
class FAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    FGuLiHybridObjectState Record;
    FPrivate Private;
    bool Local=false;
public:
    explicit FAdapter(UGuLiHybridActorWorldSubsystem& O):Owner(&O) {}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return GuLiHybridMaintenance::Version;}
    bool Prepare(const FGuLiHybridObjectState& O) override
    {GuLiNet::ResourceMaintenanceState S;Record=O;Private.Key=O.Key;return Read(O,S);}
    void SetAuthority(const FGuLiHybridObjectState& O,bool IsLocal) override {Record=O;Local=IsLocal;}
    void Advance(uint64 At,double) override
    {
        if (!Local || !Owner.IsValid()) return;
        auto* H=Owner->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        if (!H || !H->CanPublishFacts()) return;
        if (Private.Pending.IsValid())
        {
            FGuLiHybridTransactionResult Result;
            if (!H->FindTransactionResult(Private.Pending,Result))
            {if (Record.RelationRevision<=Private.Relation) return;}
            else if (Result.Status==EGuLiHybridTransactionStatus::Deferred || Result.Status==EGuLiHybridTransactionStatus::Paused) return;
            Private.Pending.Invalidate();
        }
        GuLiNet::ResourceMaintenanceState S;if (!Read(Record,S)) return;
        const int32 Due=At>=S.Next[0]?0:At>=S.Next[1]?1:INDEX_NONE;if (Due==INDEX_NONE) return;
        const auto Team=Due==0?EGuLiTeam::Red:EGuLiTeam::Blue;
        const auto Event=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/ResourceMaintenance"),EGuLiHybridObjectKind::BusinessWork,
            Record.Key.StableId.ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/%d/%llu/%llu"),Due,S.Next[Due],Record.RelationRevision)).StableId;
        Private.Pending=H->SubmitCapability(Record.Key,Event,TEXT("resource/maintenance"),Record.RelationRevision,{},Team);
        Private.Relation=Record.RelationRevision;
    }
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override {return false;}
    bool ApplyMotion(TConstArrayView<uint8>,uint64) override {return false;}
    void ApplyLedger(const FGuLiHybridObjectState& O,uint64,bool) override {Record=O;}
    bool Capture(TArray<uint8>& Out) const override
    {auto V=Private;FGuLiHybridStateArchive A;Serialize(A,V);return A.Finish(Out);}
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {FPrivate V;FGuLiHybridStateArchive A(Bytes);Serialize(A,V);return A.Done() && V.Key==Record.Key;}
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override
    {
        FPrivate V;FGuLiHybridStateArchive A(Bytes);Serialize(A,V);if (!A.Done() || V.Key!=Record.Key) return false;
        if (Stage==EGuLiHybridRestoreStage::Link) Private=V;return true;
    }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override {return Restore(Bytes,Stage);}
    void Release() override {Local=false;}
};
const FGuLiHybridAdapterRegistration Family({TEXT("ResourceMaintenance"),EGuLiHybridObjectKind::BusinessWork,Version,
    [](UGuLiHybridActorWorldSubsystem& O,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>{return MakeShared<FAdapter>(O);},
    nullptr,nullptr,EGuLiHybridOwnershipPolicy::SharedGroup,3,false,TEXT("match/resources")});
const FGuLiHybridManifestRegistration Provider({TEXT("ResourceMaintenance"),EGuLiHybridManifestPhase::Derived,{TEXT("ControlPoints")},
    [](UWorld&,FGuLiHybridManifestData& M,FString& Error)
    {
        const FString D=TEXT("state/resource-runtime");
        if (!FGuLiHybridAdapterRegistry::BindDefinition(M,D,TEXT("ResourceMaintenance"),Error)) return false;
        M.Strings.Add(TEXT("resource_maintenance/definition/")+D,UTF8_TO_TCHAR(GuLiNet::ResourceMaintenanceCapability));
        M.Numbers.Add(TEXT("capability/resource_maintenance/version"),GuLiNet::ResourceMaintenanceVersion);
        auto& O=M.Objects.AddDefaulted_GetRef();O.Definition=D;O.EstimatedCost=.1;
        O.Key=FGuLiHybridManifest::MakeStableKey(M.MapPackage,EGuLiHybridObjectKind::BusinessWork,TEXT("resource-runtime"));
        auto& R=M.Rules.AddDefaulted_GetRef();R.Id=TEXT("resource/maintenance");R.SourceDefinition=R.TargetDefinition=D;
        R.Kind=EGuLiHybridRuleKind::Capability;R.Capability=UTF8_TO_TCHAR(GuLiNet::ResourceMaintenanceCapability);
        R.CapabilityVersion=GuLiNet::ResourceMaintenanceVersion;R.CapabilityOperation=TEXT("upkeep");
        return true;
    }});
}
