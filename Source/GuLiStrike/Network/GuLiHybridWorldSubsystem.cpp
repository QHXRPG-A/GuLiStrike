#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiStrike.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Network/Private/GuLiGameplayComputePool.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridObjectStateEnvelope.h"
#include "Network/Private/GuLiHybridPeerPayload.h"
#include "GuLiNetHash.h"
#include "GuLiNetAsyncClient.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTLS.h"
#include "Trace/Trace.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "GuLiNetManifest.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattleGameMode.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridManifestPortable.h"
#include "Network/GuLiHybridCommandRegistry.h"
#include "Network/GuLiHybridLedgerBinding.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridEntityState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
using namespace GuLiNet;
using namespace GuLiNetBridge;
TAutoConsoleVariable<int32> YieldBetweenCaptureCuts(TEXT("gs.Hybrid.YieldBetweenCaptureCuts"),1,
    TEXT("Advance retained logical time between completed private-state cuts. Latched when the Hybrid client starts; 0 selects the comparison path."));
bool ResolveCaptureYield()
{
    int32 Value=YieldBetweenCaptureCuts.GetValueOnGameThread();
    FParse::Value(FCommandLine::Get(),TEXT("GuLiYieldBetweenCaptureCuts="),Value);
    return Value!=0;
}
TAutoConsoleVariable<int32> FastPackCacheKey(TEXT("gs.Hybrid.FastPackCacheKey"),1,
    TEXT("Use a lightweight local compression-cache index with full byte equality. Protocol integrity hashes are unchanged. Latched at client creation."));
TAutoConsoleVariable<int32> NativePeerSend(TEXT("gs.Hybrid.NativePeerSend"),1,
    TEXT("Queue prepared native peer messages without copying through UE message containers. 0 selects the comparison bridge."));
TAutoConsoleVariable<int32> ReusePayloadCompressor(TEXT("gs.Hybrid.ReusePayloadCompressor"),1,
    TEXT("Reuse one transaction-worker zlib workspace, resetting all stream state for each packet. Latched at client creation; 0 uses fresh compressors."));
TAutoConsoleVariable<int32> FastSmallPayloadCompression(TEXT("gs.Hybrid.FastSmallPayloadCompression"),1,
    TEXT("Use zlib best-speed for reliable bodies up to 64 KiB. Lossless wire codecs and larger private cuts are unchanged. Latched at client creation; 0 is the comparison path."));
bool ResolveFastSmallPayloadCompression()
{
    int32 Value=FastSmallPayloadCompression.GetValueOnGameThread();
    FParse::Value(FCommandLine::Get(),TEXT("GuLiFastSmallPayloadCompression="),Value);
    return Value!=0;
}
bool ResolveFastPackCacheKey()
{
    int32 Value=FastPackCacheKey.GetValueOnGameThread();
    FParse::Value(FCommandLine::Get(),TEXT("GuLiFastPackCacheKey="),Value);
    return Value!=0;
}
bool ResolveCompressorReuse()
{
    int32 Value=ReusePayloadCompressor.GetValueOnGameThread();
    FParse::Value(FCommandLine::Get(),TEXT("GuLiReusePayloadCompressor="),Value);
    return Value!=0;
}
TConstArrayView<uint8> View(std::span<const uint8> B) { return MakeArrayView(B.data(),static_cast<int32>(B.size())); }
Bytes StageBootstrapBody(Bytes Body)
{
    Reader Prefix(Body);uint32 Tag=0;
    if (Prefix.U32(Tag) && Tag==GuLiHybridPeerPayload::Tag) return Body;
    Message M;M.Header.Kind=MessageKind::HandoffState;M.Body=std::move(Body);Bytes Packed;
    return GuLiHybridPeerPayload::Pack(M,Packed)?std::move(Packed):std::move(M.Body);
}
bool ReadBootstrapBody(const Bytes& Stored,Bytes& Body)
{
    Message M;M.Header.Kind=MessageKind::HandoffState;M.Body=Stored;
    if (!GuLiHybridPeerPayload::Unpack(M)) return false;
    Body=std::move(M.Body);return Body.size()<=MaxPrivateStateBytes;
}
bool ResolveRecordTarget(UWorld& World,const FGuLiHybridObjectState& Object,FGuLiTargetHandle& Out)
{
    const auto* Actors=World.GetSubsystem<UGuLiHybridActorWorldSubsystem>();if (!Actors) return false;
    FGuLiHybridAdapterFamily Family;FString Error;
    if (!FGuLiHybridAdapterRegistry::Resolve(Actors->GetManifest(),Object,Family,Error) || !Family.ResolveTarget) return false;
    const auto* Battle=World.GetGameState<AGuLiBattleGameState>();
    return Family.ResolveTarget(Actors->GetManifest(),Object,Battle?Battle->GetMatchEpoch():0,Out);
}
TSharedPtr<FJsonValue> MakeCommitEvidence(uint64 Sequence,const CommitResult& V,const std::string& ResultHash,const std::string& DecisionHash)
{
    // Immutable evidence is encoded once on receipt, not for every later frame
    // snapshot. Keep both the wire identity and the complete decision identity.
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("sequence"),double(Sequence));
    J->SetStringField(TEXT("event"),ToUE(V.Event).ToString(EGuidFormats::Digits));J->SetNumberField(TEXT("kind"),uint8(V.Kind));J->SetNumberField(TEXT("status"),uint8(V.Status));
    J->SetStringField(TEXT("state_domain"),ToUE(V.StateDomain.StableId).ToString(EGuidFormats::Digits));J->SetNumberField(TEXT("state_version"),double(V.StateVersion));
    J->SetStringField(TEXT("target"),ToUE(V.Target.StableId).ToString(EGuidFormats::Digits));
    J->SetNumberField(TEXT("faction"),uint8(V.Faction));
    auto Balance=MakeShared<FJsonObject>();Balance->SetNumberField(TEXT("blue"),V.Balance.Blue);
    Balance->SetNumberField(TEXT("red"),V.Balance.Red);Balance->SetNumberField(TEXT("version"),double(V.Balance.Version));
    J->SetObjectField(TEXT("balance"),Balance);
    auto Payment=MakeShared<FJsonObject>();Payment->SetStringField(TEXT("token"),ToUE(V.Reserved.Token).ToString(EGuidFormats::Digits));
    Payment->SetStringField(TEXT("account"),ToUE(V.Reserved.Key.Account).ToString(EGuidFormats::Digits));
    Payment->SetStringField(TEXT("request"),LexToString(V.Reserved.Key.Request));
    Payment->SetNumberField(TEXT("blue"),V.Reserved.Cost.Blue);Payment->SetNumberField(TEXT("red"),V.Reserved.Cost.Red);
    J->SetObjectField(TEXT("payment"),Payment);
    J->SetNumberField(TEXT("applied_damage"),V.Damage.Applied);J->SetNumberField(TEXT("absorbed_damage"),V.Damage.Absorbed);
    J->SetNumberField(TEXT("remaining_health"),V.Vitals.Value);J->SetBoolField(TEXT("killed"),V.Damage.Killed);
    J->SetStringField(TEXT("death_event"),ToUE(V.DeathEvent).ToString(EGuidFormats::Digits));J->SetStringField(TEXT("reward_event"),ToUE(V.RewardEvent).ToString(EGuidFormats::Digits));
    J->SetStringField(TEXT("capability"),UTF8_TO_TCHAR(V.Capability.c_str()));J->SetStringField(TEXT("work_event"),ToUE(V.WorkEvent).ToString(EGuidFormats::Digits));
    J->SetStringField(TEXT("result_hash"),UTF8_TO_TCHAR(ResultHash.c_str()));
    J->SetStringField(TEXT("decision_hash"),UTF8_TO_TCHAR(DecisionHash.c_str()));
    J->SetStringField(TEXT("work_source"),V.FinalizedWork?ToUE(V.FinalizedWork->Parent.StableId).ToString(EGuidFormats::Digits):FGuid().ToString(EGuidFormats::Digits));
    TArray<TSharedPtr<FJsonValue>> Mutations;
    for (const auto& O:V.Objects)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("id"),ToUE(O.Grant.Object.StableId).ToString(EGuidFormats::Digits));
        Item->SetNumberField(TEXT("generation"),O.Grant.Object.Generation);Item->SetNumberField(TEXT("life"),uint8(O.Grant.State));
        Mutations.Add(MakeShared<FJsonValueObject>(Item));
    }
    J->SetArrayField(TEXT("objects"),Mutations);
    TArray<TSharedPtr<FJsonValue>> Births;for (const auto& B:V.Births) Births.Add(MakeShared<FJsonValueString>(ToUE(B.Grant.Object.StableId).ToString(EGuidFormats::Digits)));
    J->SetArrayField(TEXT("births"),Births);return MakeShared<FJsonValueObject>(J);
}
bool CapturesGameplayEvidence()
{
#if !UE_BUILD_SHIPPING
    static const bool Enabled=[]
    {
        FString Run,Case;FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),Case);
        return Case!=TEXT("steady") && Case!=TEXT("performance")
            && FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),Run) && !Run.IsEmpty();
    }();
    return Enabled;
#else
    return false;
#endif
}
FGuLiHybridMiningLease ToMiningLease(const MiningPositionLease& Value)
{
    FGuLiHybridMiningLease Out; Out.Source=ToUE(Value.Source); Out.ClusterId=static_cast<int32>(Value.Cluster);
    Out.Sample=Value.Sample; Out.Task=Value.Task; Out.Token=ToUE(Value.Token);
    Out.NodeId=static_cast<int32>(Value.Node); Out.Revision=static_cast<int64>(Value.Version); return Out;
}
class FObjectBridge final : public IObjectAdapter
{
    TSharedPtr<IGuLiHybridSimulationAdapter> Adapter;
    TWeakObjectPtr<UObject> Owner;
    FGuLiHybridObjectState State;
    FName SimulationTraceName;
    TMap<FName,TArray<uint8>> AppliedCapabilities;
    FGuLiHybridEntityState Entities;
    mutable FGuLiHybridComponentBindingContext ComponentBindings;
    mutable uint64 ActorBindingRevision=MAX_uint64;
    mutable TWeakObjectPtr<AActor> ActorBinding;
    bool ValidActorLayout() const
    {
        const auto* Actor=BoundActor();if (!Actor) return true;
        const auto* World=Owner.IsValid()?Owner->GetWorld():nullptr;
        const auto* Actors=World?World->GetSubsystem<UGuLiHybridActorWorldSubsystem>():nullptr;
        return Actors && FGuLiHybridComponentState::ValidateDefinitionLayout(Actors->GetManifest(),State.Definition.ToString(),*Actor,&ComponentBindings);
    }
    AActor* BoundActor() const
    {
        auto* W=Owner.IsValid()?Owner->GetWorld():nullptr;auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
        if (!H) return nullptr;
        const uint64 Revision=H->GetActorBindingRevision();
        if (ActorBindingRevision!=Revision)
        {ActorBinding=H->FindBoundActor(State.Key);ActorBindingRevision=Revision;}
        auto* Actor=ActorBinding.Get();
        return Actor && !Actor->IsActorBeingDestroyed()?Actor:nullptr;
    }
    bool PrepareObject(const ObjectRecord& R,std::span<const uint8> Private={})
    {
        State=ToUE(R);
        ActorBindingRevision=MAX_uint64;ActorBinding.Reset();
        // Cache one small timer name per definition, never per object or frame.
        SimulationTraceName=FName(*(TEXT("GuLiSim_Object/")+State.Definition.ToString()));
        const auto* Actors=Owner.IsValid()?Owner->GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>():nullptr;
        const TCHAR* Failure=nullptr;
        GuLiHybridObjectState::FParts Restored;
        if (!Actors) Failure=TEXT("actor registry unavailable");
        else if (!Private.empty() && !GuLiHybridObjectState::Decode(Private,Version(),Restored)) Failure=TEXT("private preparation envelope");
        else if (!(Private.empty()?Adapter->Prepare(State):Adapter->PrepareRestore(State,View(Restored.Family)))) Failure=TEXT("family materialization");
        else if (!ValidActorLayout()) Failure=TEXT("component layout");
        else if (!Entities.Prepare(*Owner->GetWorld(),State,Actors->GetManifest(),Actors->FindEntityCapabilityNames(State.Definition))) Failure=TEXT("entity capabilities");
        else if (!FGuLiHybridLedgerBindingRegistration::Apply(*Owner->GetWorld(),State,AppliedCapabilities,false)) Failure=TEXT("committed capability application");
        if (!Failure) return true;
        UE_LOG(LogTemp,Log,TEXT("Hybrid object preparation pending: object=%s definition=%s ordinal=%u stage=%s"),
            *State.Key.StableId.ToString(EGuidFormats::Digits),*State.Definition.ToString(),State.Ordinal,Failure);
        return false;
    }
    bool Wrap(const TArray<uint8>& Family,Bytes& Out) const
    {
        TArray<uint8> Components,Applications,EntityBytes;auto* Actor=BoundActor();
        if (!Entities.Capture(EntityBytes)) return false;
        if (Actor && !FGuLiHybridComponentState::Capture(*Actor,Components,&ComponentBindings)) return false;
        if (!FGuLiHybridLedgerBindingRegistration::ValidateCursors(State,AppliedCapabilities)
            || !FGuLiHybridLedgerBindingRegistration::Capture(AppliedCapabilities,Applications)) return false;
        Out=GuLiHybridObjectState::Encode(Version(),std::span<const uint8>(Family.GetData(),Family.Num()),Actor!=nullptr,
            std::span<const uint8>(Components.GetData(),Components.Num()),std::span<const uint8>(Applications.GetData(),Applications.Num()),
            std::span<const uint8>(EntityBytes.GetData(),EntityBytes.Num()));return !Out.empty();
    }
    bool RestoreParts(std::span<const uint8> Bytes,RestoreStage Stage,bool Query)
    {
        const auto Fail=[&](const TCHAR* Reason)
        {
            UE_LOG(LogTemp,Log,TEXT("Hybrid object restore failed: object=%s definition=%s ordinal=%u stage=%u query=%d reason=%s"),
                *State.Key.StableId.ToString(EGuidFormats::Digits),*State.Definition.ToString(),State.Ordinal,unsigned(Stage),Query,Reason);
            return false;
        };
        GuLiHybridObjectState::FParts V;
        if (!Owner.IsValid() || !GuLiHybridObjectState::Decode(Bytes,Version(),V)) return Fail(TEXT("private envelope"));
        if (Stage==RestoreStage::Create)
        {
            TMap<FName,TArray<uint8>> Applications;
            if (!FGuLiHybridLedgerBindingRegistration::Restore(View(V.Applications),Applications)
                || !FGuLiHybridLedgerBindingRegistration::ValidateCursors(State,Applications)) return Fail(TEXT("capability cursors"));
            AppliedCapabilities=MoveTemp(Applications);
        }
        if (!(Query?Adapter->RestoreQuery(View(V.Family),static_cast<EGuLiHybridRestoreStage>(Stage))
            :Adapter->Restore(View(V.Family),static_cast<EGuLiHybridRestoreStage>(Stage)))) return Fail(TEXT("family state"));
        if (!Entities.Restore(View(V.Entities),static_cast<EGuLiHybridRestoreStage>(Stage))) return Fail(TEXT("entity state"));
        if (Stage==RestoreStage::Detach) {ComponentBindings.Reset();return true;}
        auto* Actor=BoundActor();if ((Actor!=nullptr)!=V.HasActor) return Fail(TEXT("actor presence"));
        // All objects must exist before a capability links references to another object.
        if (Stage==RestoreStage::Create)
            return (ValidActorLayout() && (!Actor || FGuLiHybridComponentState::PrepareRestore(*Actor,View(V.Components),&ComponentBindings)))
                || Fail(TEXT("component preparation"));
        return (!Actor || FGuLiHybridComponentState::Restore(*Actor,View(V.Components),static_cast<EGuLiHybridRestoreStage>(Stage),&ComponentBindings))
            || Fail(TEXT("component state"));
    }
public:
    explicit FObjectBridge(TSharedPtr<IGuLiHybridSimulationAdapter> V) : Adapter(MoveTemp(V)), Owner(Adapter->LifetimeOwner()) {}
    uint32 Version() const override { return Owner.IsValid() ? Adapter->Version() : 0; }
    std::vector<std::string> RequiredSystems() const override
    {
        std::vector<std::string> Out;
        if (Owner.IsValid()) for (const auto Name : Adapter->RequiredSharedStateSystems()) Out.emplace_back(TCHAR_TO_UTF8(*Name.ToString()));
        return Out;
    }
    bool Prepare(const ObjectRecord& R) override
    {return PrepareObject(R);}
    bool PrepareRestore(const ObjectRecord& R,std::span<const uint8> Private) override
    {return PrepareObject(R,Private);}
    std::vector<ObjectKey> RestoreDependencies(std::span<const uint8> Bytes) const override
    {
        std::vector<ObjectKey> Out;GuLiHybridObjectState::FParts V;
        if (!Owner.IsValid() || !GuLiHybridObjectState::Decode(Bytes,Version(),V)) return Out;
        for (const auto& Key:Adapter->RestoreDependencies(View(V.Family))) Out.push_back(ToCore(Key));
        if (V.HasActor) for (const auto& Key:FGuLiHybridComponentState::RestoreDependencies(View(V.Components)))
            if (std::find(Out.begin(),Out.end(),ToCore(Key))==Out.end()) Out.push_back(ToCore(Key));
        return Out;
    }
    void SetAuthority(const Authority& A, bool Local) override
    {
        State.Controller=ToUE(A.Controller); State.SimulationOwner=ToUE(A.SimOwner);
        State.OwnershipGroup=ToUE(A.OwnershipGroup);State.GroupEpoch=A.GroupEpoch;
        State.OwnerEpoch=A.OwnerEpoch; State.ControlEpoch=A.ControlEpoch; State.LifeState=static_cast<uint8>(A.State);
        State.bDormant=A.Dormant; State.StateCustodian=ToUE(A.StateCustodian);
        if (Owner.IsValid()) {Adapter->SetAuthority(State,Local);Entities.ApplyLedger(State);}
    }
    void Advance(Tick At,double Delta) override
    {
        TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(SimulationTraceName);
        if (!Owner.IsValid()) return;
        auto* W=Owner->GetWorld();auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
        auto* Actor=BoundActor();
        const uint32 Period=Adapter->AdvancePeriod();
        const bool FamilyDue=Period && At%Period==0;
        // Recheck on the game thread after all preceding ledger/command/restore
        // callbacks. Never cache this decision on the transaction owner: a new
        // relocation step must wake on the very next original logical tick.
        if (!Actor && !FamilyDue && !Entities.HasPeriodicWork()) return;
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ObjectComponents);
            if (Actor)
                if (!FGuLiHybridComponentState::Advance(*Actor,At,Delta,&ComponentBindings))
                {H->Suspend(TEXT("Invalid object capability bindings"));return;}
        }
        if (FamilyDue) {TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ObjectFamily);Adapter->Advance(At,Delta);}
        {TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ObjectEntities);Entities.Advance(At,Delta);}
        // Component motion belongs to a bound Actor. Mass poses and entity
        // capabilities have their own publication paths, including relocation.
        if (Actor)
        {TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ObjectPublicMotion);if (H) H->PublishComponentMotionIfDue(State.Key,&ComponentBindings);}
    }
    uint32 AdvancePeriod() const override
    {return BoundActor()||!Entities.IsEmpty()?1:Adapter->AdvancePeriod();}
    bool NeedsAdvanceOnMain(Tick At) const override
    {
        if (!Owner.IsValid()) return false;
        const uint32 Period=Adapter->AdvancePeriod();
        return (Period && At%Period==0) || BoundActor() || Entities.HasPeriodicWork();
    }
    std::shared_ptr<IObjectMotionCodec> CreateMotionCodec() override {return Adapter->CreateMotionCodec();}
    bool ApplyDecodedMotion(const IObjectMotionOutput& V,Tick At) override
    {return Owner.IsValid()&&(!Entities.AcceptMotion(At)||Adapter->ApplyDecodedMotion(V,At));}
    bool Command(const FrozenCommand& C,const CommandMember& M) override
    {
        if (!Owner.IsValid() || !Owner->GetWorld()) return false;
        const auto Command=ToUE(C);const auto Member=ToUE(M);
        const auto Capability=Entities.Command(Command,Member);
        if (Capability.IsSet())
        {
            if (!Capability.GetValue()) UE_LOG(LogTemp,Warning,TEXT("Hybrid command %s operation=%s object=%s rejected by entity capability"),
                *Command.Event.ToString(),*Command.Operation.ToString(),*State.Key.StableId.ToString());
            return Capability.GetValue();
        }
        const auto Result=FGuLiHybridCommandRegistration::Execute(*Owner->GetWorld(),State,Command,Member);
        const bool Accepted=Result.IsSet()?Result.GetValue():Adapter->Execute(Command,Member);
        if (!Accepted) UE_LOG(LogTemp,Warning,TEXT("Hybrid command %s operation=%s object=%s definition=%s rejected by %s; bound_actor=%s"),
            *Command.Event.ToString(),*Command.Operation.ToString(),*State.Key.StableId.ToString(),*State.Definition.ToString(),
            Result.IsSet()?TEXT("command capability"):TEXT("object adapter"),*GetNameSafe(BoundActor()));
        return Accepted;
    }
    bool ApplyMotion(std::span<const uint8> B,Tick At) override {return Owner.IsValid() && (!Entities.AcceptMotion(At) || Adapter->ApplyMotion(View(B),At));}
    bool ApplyPublicState(std::span<const uint8> B,Tick At) override
    {auto* Actor=BoundActor();return Owner.IsValid() && (!Entities.AcceptMotion(At) || (Actor && FGuLiHybridComponentState::ApplyPublic(*Actor,View(B),&ComponentBindings)));}
    bool ApplyObjectEvent(std::span<const uint8> B,Tick At) override
    {return Owner.IsValid() && Adapter->ApplyMotion(View(B),At);}
    bool MotionFeedback(Id Peer,std::span<const uint8> B) override
    { return Owner.IsValid() && Adapter->ApplyMotionFeedback(ToUE(Peer),View(B)); }
    void ApplyLedger(const ObjectRecord& R,Revision Rev,bool Effects) override
    {
        State=ToUE(R);if (!Owner.IsValid()) return;Adapter->ApplyLedger(State,Rev,Effects);Entities.ApplyLedger(State);
        if (auto* Actor=BoundActor();Actor && !FGuLiHybridComponentState::ApplyLedger(*Actor,State,Rev,Effects,&ComponentBindings))
            Actor->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Object capability rejected committed ledger state"));
        if (!FGuLiHybridLedgerBindingRegistration::Apply(*Owner->GetWorld(),State,AppliedCapabilities,Effects))
            Owner->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Ledger binding rejected committed capability state"));
    }
    bool Capture(Bytes& Out) const override
    {
        TArray<uint8> Data; if (!Owner.IsValid() || !Adapter->Capture(Data) || Data.IsEmpty()) return false;
        return Wrap(Data,Out);
    }
    bool Validate(std::span<const uint8> B) const override
    {
        GuLiHybridObjectState::FParts V;
        if (!Owner.IsValid() || !GuLiHybridObjectState::Decode(B,Version(),V) || !Adapter->Validate(View(V.Family))
            || !Entities.Validate(View(V.Entities))) return false;
        TMap<FName,TArray<uint8>> Applications;
        if (!FGuLiHybridLedgerBindingRegistration::Restore(View(V.Applications),Applications)
            || !FGuLiHybridLedgerBindingRegistration::ValidateCursors(State,Applications)) return false;
        auto* Actor=BoundActor();return (Actor!=nullptr)==V.HasActor
            && (!Actor || FGuLiHybridComponentState::Validate(*Actor,View(V.Components),&ComponentBindings));
    }
    bool Restore(std::span<const uint8> B,RestoreStage Stage) override
    { return RestoreParts(B,Stage,false); }
    bool RestoreQuery(std::span<const uint8> B,RestoreStage Stage) override
    { return RestoreParts(B,Stage,true); }
    void ResetControlInput() override { if (Owner.IsValid()) Adapter->ResetControlInput(); }
    void BeginResourceRestore(Revision) override
    {AppliedCapabilities.Reset();ComponentBindings.Reset();}
    void ResetResourceRequests(Revision E) override
    {
        if (!Owner.IsValid()) return;
        Adapter->ResetResourceRequests(E);Entities.ResetResourceRequests(E);
        if (auto* Actor=BoundActor()) FGuLiHybridComponentState::ResetResourceRequests(*Actor,E,&ComponentBindings);
    }
    void Release() override { ActorBindingRevision=MAX_uint64;ActorBinding.Reset();ComponentBindings.Reset();Entities.Reset();if (Owner.IsValid()) Adapter->Release(); }
};
class FSharedStateBridge final : public ISharedStateAdapter
{
    TSharedPtr<IGuLiHybridSharedStateAdapter> Adapter;
    TWeakObjectPtr<UObject> Owner;
    FName SimulationTraceName;
    static TArray<FGuLiHybridObjectKey> Keys(const std::vector<ObjectKey>& Objects)
    { TArray<FGuLiHybridObjectKey> Out; for (const auto& K : Objects) Out.Add(ToUE(K)); return Out; }
    static TArray<FGuLiHybridSharedStatePiece> Pieces(const std::vector<PrivateSystemState>& Shards)
    {
        TArray<FGuLiHybridSharedStatePiece> Out;
        for (const auto& S : Shards)
        {
            auto& V=Out.AddDefaulted_GetRef(); V.Name=FName(UTF8_TO_TCHAR(S.Name.c_str())); V.Player=ToUE(S.Player);
            V.Version=S.AdapterVersion; V.Objects=Keys(S.Objects); V.Bytes.Append(S.State.data(),static_cast<int32>(S.State.size()));
        }
        return Out;
    }
public:
    void ResetResourceRequests(std::span<const ObjectKey> Objects,Revision E) override
    {if (Owner.IsValid()) Adapter->ResetResourceRequests(Keys(std::vector<ObjectKey>(Objects.begin(),Objects.end())),E);}
    bool CaptureCurrent(const std::vector<ObjectKey>& Objects,Bytes& Out) const override
    {
        TArray<uint8> B;if (!Owner.IsValid() || !Adapter->CaptureCurrent(Keys(Objects),B)) return false;
        Out.assign(B.GetData(),B.GetData()+B.Num());return true;
    }
    FSharedStateBridge(FName Name,TSharedPtr<IGuLiHybridSharedStateAdapter> V)
        : Adapter(MoveTemp(V)),Owner(Adapter->LifetimeOwner()),SimulationTraceName(*(TEXT("GuLiSim_Shared/")+Name.ToString())) {}
    uint32 Version() const override { return Owner.IsValid() ? Adapter->Version() : 0; }
    bool UsesObject(ObjectKey Key) const override { return Owner.IsValid() && Adapter->UsesObject(ToUE(Key)); }
    void Advance(Tick At,double DeltaSeconds) override
    { TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(SimulationTraceName);if (Owner.IsValid()) Adapter->Advance(At,DeltaSeconds); }
    bool Capture(const std::vector<ObjectKey>& Objects,Bytes& Out) const override
    {
        TArray<uint8> Data;
        if (!Owner.IsValid() || !Adapter->Capture(Keys(Objects),Data) || Data.IsEmpty()) return false;
        Out.assign(Data.GetData(),Data.GetData()+Data.Num()); return true;
    }
    bool Validate(const std::vector<PrivateSystemState>& Shards,const std::vector<ObjectKey>& Objects) const override
    { return Owner.IsValid() && Adapter->Validate(Pieces(Shards),Keys(Objects)); }
    bool Restore(const std::vector<PrivateSystemState>& Shards,const std::vector<ObjectKey>& Objects,RestoreStage Stage) override
    { return Owner.IsValid() && Adapter->Restore(Pieces(Shards),Keys(Objects),static_cast<EGuLiHybridRestoreStage>(Stage)); }
    bool RestoreQuery(const std::vector<PrivateSystemState>& Shards,const std::vector<ObjectKey>& Objects,RestoreStage Stage) override
    { return Owner.IsValid() && Adapter->RestoreQuery(Pieces(Shards),Keys(Objects),static_cast<EGuLiHybridRestoreStage>(Stage)); }
};
}
struct UGuLiHybridWorldSubsystem::FState
{
    RuntimeManifest Manifest;
    Revision SelectionVersion=0,MembershipVersion=0;
    Id MembershipPlayer;
    bool MembershipJoin=false,MembershipAbrupt=false,BootstrapSent=false,MembershipAcked=false;
    std::set<Id> BootstrapSources;
    std::map<Id,std::set<uint32>> BootstrapParts;
    std::map<Id,uint32> BootstrapCounts;
    std::map<Id,uint64> BootstrapSourceBytes;
    std::map<Id,uint64> BootstrapSourceStagedBytes;
    std::map<std::pair<Id,uint32>,Bytes> BootstrapStates;
    std::vector<ObjectHandoffState> BootstrapPreparedParts;
    std::shared_ptr<const std::map<std::pair<Id,uint32>,Bytes>> BootstrapReadSource;
    uint8 BootstrapApplyStage=0;
    uint64 BootstrapStateBytes=0,BootstrapStatePeakBytes=0,BootstrapCapturedBytes=0;
    uint64 BootstrapStagedBytes=0,BootstrapStagedPeakBytes=0,BootstrapCapturedStagedBytes=0;
    std::vector<Bytes> BootstrapOutgoing;
    std::vector<ObjectKey> BootstrapCaptureKeys;
    std::deque<std::pair<size_t,size_t>> BootstrapCaptureQueue;
    bool BootstrapCaptureStarted=false,BootstrapCaptureComplete=false;
    uint32 BootstrapSendPart=0;
    bool BootstrapApplied=false;
    uint32 BootstrapCaptureObjects=0,BootstrapCaptureOffset=0,BootstrapCaptureSplits=0;
    FString BootstrapBlocked;
    double BootstrapDirectorySeconds=0,BootstrapImportSeconds=0,BootstrapCompletedSeconds=0,BootstrapStageStarted=0;
    uint64 ResultEvidenceSequence=0;
    uint64 LocalSubmissionEvidenceSequence=0;
    uint64 HitEvidenceSequence=0;
    double ReviewSimulationRate=1;
    std::deque<TSharedPtr<FJsonValue>> ResultEvidence;
    std::deque<TSharedPtr<FJsonValue>> LocalSubmissionEvidence;
    std::deque<TSharedPtr<FJsonValue>> HitEvidence;
    std::map<Id,PreparedCommit> HeldCommits;
    TArray<TSharedPtr<FJsonValue>> HandoffEvidence;
    std::set<ObjectKey> Selected;
    std::map<ObjectKey,double> DeselectedAt,OperatedAt;
    std::map<ObjectKey,ObjectHandoff> Handoffs;
    std::set<ObjectKey> LostOwnerObjects;
    TArray<TPair<FGuid,FGuLiHybridMessage>> EarlyBootstrap;
    uint64 EarlyBootstrapBytes=0;
    bool SelectionDirty=false;
    std::set<ObjectKey> PendingOperations;
    double MembershipPausedAt=0;
    uint64 HandoffCount=0,HandoffBytes=0,HandoffQueuedBytes=0;
    TMap<FGuid,double> HandoffStarted;
    double LastHandoffMilliseconds=0;
    bool DetailedReview=false;
    TMap<FName,FGuLiHybridRuleEntry> UpgradeRules;
    ResourceLedgerState ResourceCatalog;
    ResourceDirectory ResourceView;
    std::map<uint32,uint32> MiningCatalog;
    MiningReservationDirectory MiningView;
    FString ResourceLayoutHash;
    struct FFactory { TWeakObjectPtr<UObject> Owner; FGuLiHybridAdapterFactory Create; };
    TMap<EGuLiHybridObjectKind,FFactory> Factories;
    TMap<FName,TSharedPtr<IGuLiHybridSharedStateAdapter>> SharedStateSystems;
    TMap<FGuLiTargetHandle,FGuLiHybridObjectKey> Targets;
    // Authenticated terminal metadata can resolve a retained flight's exact
    // old target life without installing an entity or simulation custody.
    TMap<FGuLiHybridObjectKey,FGuLiHybridObjectState> RetiredTargetMetadata;
    uint64 RetiredTargetLookups=0;
    TMap<FGuLiHybridObjectKey,TWeakObjectPtr<AActor>> Actors;
    uint64 ActorBindingRevision=0;
    TMap<uint32,FGuLiHybridObjectKey> ActorTaskKeys;
    TWeakObjectPtr<UGuLiHybridSessionSubsystem> Session;
    FGuLiNetworkLaunchConfig Config;
    std::unique_ptr<AsyncClientWorld> Client;
    bool bReusePayloadCompressor=false;
    bool bFastSmallPayloadCompression=false;
    AsyncClientStats LastAsyncStats;
    double UnreportedSimulationMilliseconds=0;
    uint32 HUDQueryDepth=0;
    uint64 FullObjectCopies=0,HUDFullObjectCopies=0;
    std::map<ObjectKey,ObjectRecord> WaitingDirectory;
    std::set<ObjectKey> DirectoryKeys;
    std::set<ObjectKey> CoordinatorDirectoryKeys;
    std::map<ObjectKey,Revision> Offers;
    TArray<TSharedPtr<FJsonValue>> AssignmentDiagnostics;
    void RecordAssignment(ObjectKey K,Revision Version,const TCHAR* Status)
    {
        if (!DetailedReview) return;
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("object"),ToUE(K.StableId).ToString(EGuidFormats::Digits));
        J->SetNumberField(TEXT("generation"),K.Generation);J->SetNumberField(TEXT("proposed_version"),double(Version));
        J->SetStringField(TEXT("status"),Status);
        if (Client) if (const auto O=Client->Objects().find(K);O!=Client->Objects().end())
        {
            J->SetBoolField(TEXT("known"),true);J->SetNumberField(TEXT("current_version"),double(O->second.Grant.OwnerEpoch));
            J->SetNumberField(TEXT("life"),uint8(O->second.Grant.State));J->SetBoolField(TEXT("materialized"),O->second.Materialized);
        }
        if (AssignmentDiagnostics.Num()>=64) AssignmentDiagnostics.RemoveAt(0,1,EAllowShrinking::No);
        AssignmentDiagnostics.Add(MakeShared<FJsonValueObject>(J));
    }
    struct FMotionPublishStats { uint64 Attempts=0,Accepted=0,AuthorityBlocked=0,ClockBlocked=0,QueueFailed=0,LastTick=0; };
    std::map<ObjectKey,FMotionPublishStats> MotionPublished;
    // Epochs prevent a previous branch's timestamp from suppressing recovery.
    std::map<ObjectKey,std::tuple<Revision,Revision,GuLiNet::Tick>> PublicMotionPublished;
    FString Failure;
    std::set<ObjectKey> RequiredDirectoryKeys;
    uint32 ExpectedObjects=0;
    bool DirectoryComplete=false, Ready=false,DirectoryResumePending=false;
    bool RequiredDirectoryValidated=false;
    double NextReadySend=0,NextBirthRecovery=0;
    uint64 BirthRecoveryAnnouncements=0;
    double DirectoryStarted=0;
    Resources Red,Blue;
    TMap<FGuid,TFunction<void(const FGuLiHybridTransactionResult&)>> TransactionCallbacks;
    struct FReceiptListener {TWeakObjectPtr<UObject> Owner;TFunction<void(const FGuLiHybridTransactionResult&)> Callback;bool Active=true;};
    std::map<ObjectKey,TArray<TSharedPtr<FReceiptListener>>> ObjectReceiptListeners;
    TMap<FGuid,FGuLiHybridTransactionResult> TransactionResults;
    TArray<FGuid> TransactionResultOrder;
    int32 TransactionResultHead=0;
    TMap<FGuid,FGuLiDamageCommitResult> DamageResults;
    TArray<FGuid> DamageResultOrder;
    int32 DamageResultHead=0;

};
void UGuLiHybridWorldSubsystem::FStateDeleter::operator()(FState* Value) const { delete Value; }
UGuLiHybridWorldSubsystem::UGuLiHybridWorldSubsystem() : State(new FState) {}
UGuLiHybridWorldSubsystem::~UGuLiHybridWorldSubsystem() = default;
bool UGuLiHybridWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return !FParse::Param(FCommandLine::Get(),TEXT("GuLiAcceptanceStandby"))
        && Super::ShouldCreateSubsystem(Outer) && UGuLiNetworkSettings::IsHybrid(Cast<UWorld>(Outer));
}
void UGuLiHybridWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World); State->Config=UGuLiNetworkSettings::Resolve(&World);
    FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&ThisClass::ApplyLateGameplay);
    FString ReviewCase;FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),ReviewCase);
    State->DetailedReview=ReviewCase.StartsWith(TEXT("fault_"));
    FGuLiHybridManifestData Manifest; FString CatalogError;
    if (!FGuLiHybridManifest::Read(State->Config.ManifestFile,Manifest,CatalogError)) { Suspend(CatalogError); return; }
    for (const auto& Rule:Manifest.Rules) if (Rule.Kind==EGuLiHybridRuleKind::Upgrade) State->UpgradeRules.Add(FName(*Rule.Id),Rule);
    if (!ReadPortableManifest(State->Config.ManifestFile,State->Manifest,CatalogError)) {Suspend(CatalogError);return;}
    std::string ResourceError;
    if (!ReadResourceCatalog(State->Manifest,State->ResourceCatalog,ResourceError)) { Suspend(UTF8_TO_TCHAR(ResourceError.c_str())); return; }
    for (const auto& [Node,Value]:State->ResourceCatalog.Nodes) State->MiningCatalog.emplace(Node,Value.Cluster);
    State->ResourceLayoutHash=Manifest.Strings.FindRef(TEXT("resource_map/LayoutHash"));
    if (World.GetNetMode()!=NM_Standalone) { Suspend(TEXT("Hybrid requires an independent local world")); return; }
    auto* GI=World.GetGameInstance(); auto* Session=GI ? GI->GetSubsystem<UGuLiHybridSessionSubsystem>() : nullptr;
    if (!Session) { Suspend(TEXT("Hybrid GameInstance session unavailable")); return; }
    State->Session=Session;
    Session->OnControl.AddUObject(this,&ThisClass::HandleControl);
    Session->AdmitPeer.BindUObject(this,&ThisClass::HandlePeer);
    Session->AdmitNativePeer.BindUObject(this,&ThisClass::HandleNativePeer);
    Session->CanAdmitPeer.BindUObject(this,&ThisClass::CanAdmitPeer);
    Session->OnReviewReceiptReleased.AddUObject(this,&ThisClass::HandleReleasedCommit);
    Session->OnFailure.AddUObject(this,&ThisClass::HandleFailure);
    FString Error; if (!Session->Start(State->Config,Error)) Suspend(Error);
}
void UGuLiHybridWorldSubsystem::Deinitialize()
{
    FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
    if (auto* Session=State->Session.Get())
    {
        Session->OnControl.RemoveAll(this); Session->AdmitPeer.Unbind();Session->AdmitNativePeer.Unbind();Session->CanAdmitPeer.Unbind(); Session->OnFailure.RemoveAll(this);
        Session->OnReviewReceiptReleased.RemoveAll(this);
        Message Leave; Leave.Header.Kind=MessageKind::Leave; Session->SendControl(ToUE(Leave)); Session->Stop();
    }
    State->Client.reset(); State->Factories.Reset(); State->SharedStateSystems.Reset(); State->Targets.Reset(); State->Actors.Reset(); State->Session.Reset();
    State->ActorTaskKeys.Reset();
    State->ResourceView={}; State->ResourceCatalog={};
    State->UpgradeRules.Reset();
    State->MiningView={}; State->MiningCatalog.clear(); OnMiningReservationsChanged.Clear();
    State->ResourceLayoutHash.Reset(); OnResourceDirectoryChanged.Clear(); OnResourceNodeChanged.Clear();
    State->TransactionCallbacks.Reset();
    State->TransactionResults.Reset(); State->TransactionResultOrder.Reset();State->TransactionResultHead=0;
    State->HeldCommits.clear();State->BootstrapReadSource.reset();State->BootstrapPreparedParts.clear();State->BootstrapCaptureQueue.clear();
    State->DamageResults.Reset(); State->DamageResultOrder.Reset();State->DamageResultHead=0;
    State->PublicMotionPublished.clear();
    OnReady.Clear(); OnOwnershipStateApplied.Clear();OnObjectChanged.Clear();OnFailure.Clear();OnTransactionCompleted.Clear();OnCommandCompleted.Clear();Super::Deinitialize();
}
bool UGuLiHybridWorldSubsystem::SubmitGameplayCalculation(FName Group,std::shared_ptr<GuLiNet::IGameplayCalculation> Work,
    uint64 InputBytes,uint64 MaximumResultBytes,std::function<void()> Apply)
{
    check(IsInGameThread());
    return State->Client && State->Client->SubmitGameplay(TCHAR_TO_UTF8(*Group.ToString()),std::move(Work),InputBytes,MaximumResultBytes,std::move(Apply));
}
bool UGuLiHybridWorldSubsystem::ConfirmGameplayCalculation(std::shared_ptr<GuLiNet::IGameplayCalculation> Work,uint64 Bytes)
{return State->Client&&State->Client->ConfirmGameplay(std::move(Work),Bytes);}
bool UGuLiHybridWorldSubsystem::HasPendingGameplayCalculation(FName Group) const
{return State->Client && State->Client->HasPendingGameplay(TCHAR_TO_UTF8(*Group.ToString()));}
void UGuLiHybridWorldSubsystem::ApplyLateGameplay(UWorld* World,ELevelTick,float)
{
    if (World==GetWorld() && State->Client
        && UGuLiHotspotOptions::Enabled(World,EGuLiHotspot::GameplayWorker))
        State->Client->PumpApplications();
}
bool UGuLiHybridWorldSubsystem::RegisterFactory(EGuLiHybridObjectKind Kind,UObject& Owner,FGuLiHybridAdapterFactory Create)
{
    if (Kind==EGuLiHybridObjectKind::None || !Create || State->Factories.Contains(Kind)) return false;
    State->Factories.Add(Kind,{&Owner,MoveTemp(Create)}); return true;
}
void UGuLiHybridWorldSubsystem::UnregisterFactories(const UObject& Owner)
{
    for (auto It=State->Factories.CreateIterator(); It; ++It) if (It.Value().Owner==&Owner) It.RemoveCurrent();
    for (auto It=State->SharedStateSystems.CreateIterator(); It; ++It)
        if (It.Value()->LifetimeOwner()==&Owner) It.RemoveCurrent();
}
bool UGuLiHybridWorldSubsystem::RegisterSharedStateSystem(FName Name,TSharedPtr<IGuLiHybridSharedStateAdapter> Adapter)
{
    if (Name.IsNone() || !Adapter || !IsValid(Adapter->LifetimeOwner()) || !Adapter->Version()
        || State->SharedStateSystems.Contains(Name)) return false;
    if (State->Client && !State->Client->RegisterSystem(TCHAR_TO_UTF8(*Name.ToString()),std::make_unique<FSharedStateBridge>(Name,Adapter))) return false;
    State->SharedStateSystems.Add(Name,MoveTemp(Adapter)); return true;
}
bool UGuLiHybridWorldSubsystem::BindTarget(const FGuLiTargetHandle& Target,const FGuLiHybridObjectKey& Key)
{
    if (!Target.IsValid() || !Key.IsValid()) return false;
    if (const auto* Old=State->Targets.Find(Target)) return *Old==Key;
    State->Targets.Add(Target,Key); return true;
}
bool UGuLiHybridWorldSubsystem::BindActor(const FGuLiHybridObjectKey& Key,AActor& Actor,uint32 Ordinal)
{
    if (!Key.IsValid() || Actor.GetWorld()!=GetWorld() || Actor.IsActorBeingDestroyed()) return false;
    const auto* Existing=State->Actors.Find(Key);
    if (Existing && Existing->IsValid() && Existing->Get()!=&Actor) return false;
    const bool Changed=!Existing || Existing->Get()!=&Actor;
    if (Ordinal)
    {
        if (!Ordinal) return false;
        if (const auto* Old=State->ActorTaskKeys.Find(Ordinal); Old && *Old!=Key && FindBoundActor(*Old)) return false;
        State->ActorTaskKeys.Add(Ordinal,Key);
    }
    if (Changed) {State->Actors.Add(Key,&Actor);++State->ActorBindingRevision;}
    return true;
}
void UGuLiHybridWorldSubsystem::UnbindActor(const FGuLiHybridObjectKey& Key,const AActor& Actor)
{
    if (const auto* Existing=State->Actors.Find(Key); Existing && Existing->Get()==&Actor)
    {State->Actors.Remove(Key);++State->ActorBindingRevision;}
}
uint32 UGuLiHybridWorldSubsystem::GetActorTaskGeneration(uint32 Ordinal) const
{
    // Keep this life mapping through detach and EndPlay so queue cancellation
    // still addresses the old life after its UE handle has been released.
    const auto* Key=State->ActorTaskKeys.Find(Ordinal); return Key?Key->LifeGeneration:0;
}
bool UGuLiHybridWorldSubsystem::ResolveActorTaskKey(uint32 Ordinal,FGuLiHybridObjectKey& Out) const
{
    const auto* Key=State->ActorTaskKeys.Find(Ordinal);if (!Key) return false;
    Out=*Key;return true;
}
AActor* UGuLiHybridWorldSubsystem::FindBoundActor(const FGuLiHybridObjectKey& Key) const
{
    const auto* Existing=State->Actors.Find(Key); auto* Actor=Existing?Existing->Get():nullptr;
    return Actor && !Actor->IsActorBeingDestroyed()?Actor:nullptr;
}
uint64 UGuLiHybridWorldSubsystem::GetActorBindingRevision() const {return State->ActorBindingRevision;}
bool UGuLiHybridWorldSubsystem::ResolveTarget(const FGuLiTargetHandle& Target,FGuLiHybridObjectKey& Out) const
{ const auto* K=State->Targets.Find(Target); if (!K) return false; Out=*K; return true; }
uint32 UGuLiHybridWorldSubsystem::GetLatestRetiredLife(const FGuLiHybridObjectKey& Stable) const
{
    const auto Key=ToCore(Stable);
    return State->Client?State->Client->NumericState().LastRetiredLife(Key.Kind,Key.StableId):0;
}
bool UGuLiHybridWorldSubsystem::GetObject(const FGuLiHybridObjectKey& Key,FGuLiHybridObjectState& Out) const
{
    if (!State->Client) return false;
    const auto It=State->Client->Objects().find(ToCore(Key)); if (It==State->Client->Objects().end()) return false;
    Out=ToUE(It->second); return true;
}
bool UGuLiHybridWorldSubsystem::GetTeamCapabilityObject(EGuLiTeam Team,FName Capability,FGuLiHybridObjectState& Out) const
{
    if (!State->Client) return false;
    const auto* Object=State->Client->FindTeamCapability(ToCore(Team),TCHAR_TO_UTF8(*Capability.ToString()));
    if (!Object) return false;Out=ToUE(*Object);return true;
}
bool UGuLiHybridWorldSubsystem::GetObjectView(const FGuLiHybridObjectKey& Key,FGuLiHybridObjectState& Out) const
{
    if (!State->Client) return false;
    const auto I=State->Client->Objects().find(ToCore(Key));if (I==State->Client->Objects().end()) return false;
    Out=ToUEView(I->second);return true;
}
bool UGuLiHybridWorldSubsystem::GetObjectLifecycle(const FGuLiHybridObjectKey& Key,FGuid& SimulationOwner,uint8& LifeState,GuLiNet::ObjectReadCursor* Lookup) const
{
    if (!State->Client) return false;
    const ObjectRecord* Record=nullptr;
    if (Lookup) Record=State->Client->FindObject(ToCore(Key),*Lookup);
    else
    {
        const auto I=State->Client->Objects().find(ToCore(Key));
        if (I!=State->Client->Objects().end()) Record=&I->second;
    }
    if (!Record) return false;
    SimulationOwner=ToUE(Record->Grant.SimOwner);
    LifeState=static_cast<uint8>(Record->Grant.State);
    return true;
}
FGuLiHybridObjectKey UGuLiHybridWorldSubsystem::GetTeamCapabilityKey(EGuLiTeam Team,FName Capability) const
{
    if (!State->Client) return {};const auto* O=State->Client->FindTeamCapability(ToCore(Team),TCHAR_TO_UTF8(*Capability.ToString()));
    return O?ToUE(O->Grant.Object):FGuLiHybridObjectKey{};
}
void UGuLiHybridWorldSubsystem::GetCommanderDefinitions(TArray<FName>& Out) const
{
    Out.Reset();if (!State->Client) return;
    for (const auto& [Name,Count]:State->Client->CommanderDefinitions()) Out.Add(FName(UTF8_TO_TCHAR(Name.c_str())));
    Out.Sort(FNameLexicalLess());
}
bool UGuLiHybridWorldSubsystem::GetObjectMetadata(const FGuLiHybridObjectKey& Key,FGuLiHybridObjectState& Out) const
{
    if (GetObject(Key,Out)) return true;
    const auto* Retired=State->RetiredTargetMetadata.Find(Key);
    if (!Retired || Retired->LifeState!=uint8(Life::Destroyed)) return false;
    Out=*Retired;++State->RetiredTargetLookups;return true;
}
bool UGuLiHybridWorldSubsystem::HasSimulation(const FGuLiHybridObjectKey& Key,bool Dead) const
{ return State->Client && State->Client->HasSimulation(ToCore(Key),Dead); }
void UGuLiHybridWorldSubsystem::GetObjects(TArray<FGuLiHybridObjectState>& Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_FullObjectCopy);
    ++State->FullObjectCopies;if (State->HUDQueryDepth) ++State->HUDFullObjectCopies;
    Out.Reset(); if (!State->Client) return;
    Out.Reserve(static_cast<int32>(State->Client->Objects().size()));
    for (const auto& [K,O] : State->Client->Objects()) { (void)K; Out.Add(ToUE(O)); }
}
TSharedRef<FJsonObject> UGuLiHybridWorldSubsystem::GetSimulationCensus() const
{
    auto Root=MakeShared<FJsonObject>();auto Definitions=MakeShared<FJsonObject>();
    if (const auto* Options=GetWorld()->GetSubsystem<UGuLiHotspotOptions>())
        Root->SetObjectField(TEXT("sync_identity"),Options->GetSyncIdentity());
    auto Teams=MakeShared<FJsonObject>();
    const Id Local=State->Session.IsValid()?ToCore(State->Session->GetLocalPlayerId()):Id{};
    struct FCounts {uint64 Total=0,Alive=0,LocalOwned=0,PeerOwned=0,Simulating=0,Dormant=0,Dead=0,Destroyed=0;};
    std::map<std::string,FCounts> ByDefinition,ByTeam;
    uint64 Soldiers=0,LocalSoldiers=0,PeerSoldiers=0;
    if (State->Client) for (const auto& [K,O]:State->Client->Objects())
    {
        const bool Alive=O.Grant.State==Life::Alive,Owned=O.Grant.SimOwner==Local;
        const bool Simulating=State->Client->HasSimulation(K);
        const auto Count=[&](FCounts& C)
        {
            ++C.Total;C.Alive+=Alive;C.LocalOwned+=Alive&&Owned;
            C.PeerOwned+=Alive&&!Owned&&O.Grant.SimOwner.Valid();C.Simulating+=Simulating;
            C.Dormant+=O.Grant.Dormant;C.Dead+=O.Grant.State==Life::Dead;C.Destroyed+=O.Grant.State==Life::Destroyed;
        };
        Count(ByDefinition[O.Definition]);Count(ByTeam[std::to_string(uint8(O.Grant.Faction))]);
        if (K.Kind==ObjectKind::Soldier && Alive)
        {++Soldiers;LocalSoldiers+=Simulating;PeerSoldiers+=!Owned&&O.Grant.SimOwner.Valid();}
    }
    const auto Write=[](const auto& Source,const TSharedRef<FJsonObject>& Destination)
    {
        for (const auto& [Name,C]:Source)
        {
            auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("total"),double(C.Total));
            J->SetNumberField(TEXT("alive"),double(C.Alive));J->SetNumberField(TEXT("local_alive_owned"),double(C.LocalOwned));
            J->SetNumberField(TEXT("peer_alive_owned"),double(C.PeerOwned));J->SetNumberField(TEXT("local_simulation"),double(C.Simulating));
            J->SetNumberField(TEXT("dormant"),double(C.Dormant));J->SetNumberField(TEXT("dead"),double(C.Dead));
            J->SetNumberField(TEXT("destroyed"),double(C.Destroyed));Destination->SetObjectField(UTF8_TO_TCHAR(Name.c_str()),J);
        }
    };
    Write(ByDefinition,Definitions);Write(ByTeam,Teams);
    Root->SetBoolField(TEXT("ready"),State->Client && CanPublishFacts());
    Root->SetStringField(TEXT("local_player"),ToUE(Local).ToString(EGuidFormats::Digits));
    Root->SetNumberField(TEXT("live_soldiers"),double(Soldiers));Root->SetNumberField(TEXT("local_simulating_soldiers"),double(LocalSoldiers));
    Root->SetNumberField(TEXT("peer_alive_soldiers"),double(PeerSoldiers));
    Root->SetNumberField(TEXT("directory_records"),State->Client?double(State->Client->Objects().size()):0);
    Root->SetNumberField(TEXT("retired_target_records"),State->RetiredTargetMetadata.Num());
    Root->SetNumberField(TEXT("logical_tick"),double(GetLogicalTick()));
    Root->SetObjectField(TEXT("by_definition"),Definitions);Root->SetObjectField(TEXT("by_team"),Teams);
    return Root;
}
void UGuLiHybridWorldSubsystem::BeginHUDQueryScope() {check(IsInGameThread());++State->HUDQueryDepth;}
void UGuLiHybridWorldSubsystem::EndHUDQueryScope() {check(IsInGameThread() && State->HUDQueryDepth);--State->HUDQueryDepth;}
bool UGuLiHybridWorldSubsystem::HasControl(const FGuLiHybridObjectKey& Key) const
{ return State->Client && State->Client->HasControl(ToCore(Key)); }
bool UGuLiHybridWorldSubsystem::RouteCommand(const FGuLiHybridCommand& Command)
{
    if (!State->Client) return false;
    TArray<FGuLiHybridObjectKey> Objects;for (const auto& Member:Command.Members) Objects.AddUnique(Member.Key);
    ReportDirectOperation(Objects);
    return State->Client->Route(ToCore(Command));
}
uint64 UGuLiHybridWorldSubsystem::GetObjectLogicalTick(const FGuLiHybridObjectKey& Key) const
{ return State->Client?State->Client->ObjectNow(ToCore(Key)):0; }
uint64 UGuLiHybridWorldSubsystem::GetLogicalTick() const { return State->Client ? State->Client->ContextNow() : 0; }
uint64 UGuLiHybridWorldSubsystem::GetLocalLogicalTick() const { return State->Client ? State->Client->Now() : 0; }
TSharedRef<FJsonObject> UGuLiHybridWorldSubsystem::GetReviewCommandResult(const FGuid& Event) const
{
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("event"),Event.ToString(EGuidFormats::Digits));
    TArray<TSharedPtr<FJsonValue>> Members;
    const auto Result=State->Client?State->Client->CommandResult(ToCore(Event)):std::optional<CommandReceipt>{};
    if (Result) for (const auto& M:Result->Members)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("object"),ToUE(M.Object.StableId).ToString(EGuidFormats::Digits));
        J->SetNumberField(TEXT("kind"),uint8(M.Object.Kind));J->SetNumberField(TEXT("generation"),M.Object.Generation);
        J->SetNumberField(TEXT("owner_version"),double(M.OwnerEpoch));J->SetNumberField(TEXT("control_version"),double(M.ControlEpoch));
        J->SetBoolField(TEXT("accepted"),M.Accepted);Members.Add(MakeShared<FJsonValueObject>(J));
    }
    Root->SetArrayField(TEXT("executed"),Members);return Root;
}
TSharedRef<FJsonObject> UGuLiHybridWorldSubsystem::GetReviewEvidenceSnapshot() const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiReview_WorldSnapshot);
    auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("ready"),IsWorldReady() && !IsBattlePaused());
    Root->SetBoolField(TEXT("world_ready"),IsWorldReady());Root->SetBoolField(TEXT("paused"),IsBattlePaused());Root->SetStringField(TEXT("failure"),State->Failure);
    Root->SetBoolField(TEXT("state_ready"),IsWorldReady());
    Root->SetNumberField(TEXT("birth_recovery_announcements"),double(State->BirthRecoveryAnnouncements));
    Root->SetNumberField(TEXT("logical_tick"),double(GetLogicalTick()));Root->SetNumberField(TEXT("epoch"),State->Client?double(State->Client->Epoch()):0);
    Root->SetNumberField(TEXT("waiting_adapters"),State->WaitingDirectory.size());Root->SetNumberField(TEXT("pending_assignments"),State->Offers.size());
    Root->SetObjectField(TEXT("async_transactions"),GetTransactionCaptureSnapshot());
    TArray<TSharedPtr<FJsonValue>> Assignments;
    for (const auto& [K,Version]:State->Offers)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("object"),ToUE(K.StableId).ToString(EGuidFormats::Digits));
        Item->SetNumberField(TEXT("proposed_version"),double(Version));
        if (State->Client) if (const auto O=State->Client->Objects().find(K);O!=State->Client->Objects().end())
        {Item->SetNumberField(TEXT("current_version"),double(O->second.Grant.OwnerEpoch));Item->SetStringField(TEXT("definition"),UTF8_TO_TCHAR(O->second.Definition.c_str()));}
        Assignments.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("assignment_offers"),Assignments);
    Root->SetArrayField(TEXT("assignment_offer_diagnostics"),State->AssignmentDiagnostics);
    Root->SetNumberField(TEXT("handoff_count"),double(State->HandoffCount));Root->SetNumberField(TEXT("handoff_bytes"),double(State->HandoffBytes));
    Root->SetNumberField(TEXT("last_handoff_ms"),State->LastHandoffMilliseconds);Root->SetNumberField(TEXT("membership_version"),double(State->MembershipVersion));
    auto Membership=MakeShared<FJsonObject>();
    Membership->SetStringField(TEXT("player"),ToUE(State->MembershipPlayer).ToString(EGuidFormats::Digits));
    Membership->SetBoolField(TEXT("join"),State->MembershipJoin);Membership->SetBoolField(TEXT("sent"),State->BootstrapSent);
    Membership->SetBoolField(TEXT("imported"),State->BootstrapApplied);Membership->SetBoolField(TEXT("acked"),State->MembershipAcked);
    Membership->SetNumberField(TEXT("elapsed"),State->MembershipPausedAt>0?FPlatformTime::Seconds()-State->MembershipPausedAt:0);
    Membership->SetNumberField(TEXT("capture_objects"),State->BootstrapCaptureObjects);Membership->SetNumberField(TEXT("capture_offset"),State->BootstrapCaptureOffset);
    Membership->SetNumberField(TEXT("capture_splits"),State->BootstrapCaptureSplits);
    Membership->SetNumberField(TEXT("send_part"),State->BootstrapSendPart);Membership->SetNumberField(TEXT("send_parts_total"),State->BootstrapOutgoing.size());
    Membership->SetNumberField(TEXT("captured_state_bytes"),double(State->BootstrapCapturedBytes));
    Membership->SetNumberField(TEXT("captured_staged_bytes"),double(State->BootstrapCapturedStagedBytes));
    Membership->SetNumberField(TEXT("received_staged_bytes"),double(State->BootstrapStagedBytes));
    Membership->SetNumberField(TEXT("peak_staged_bytes"),double(State->BootstrapStagedPeakBytes));
    Membership->SetBoolField(TEXT("deferred_materialization"),State->Client && State->Client->IsBootstrapMaterializationDeferred());
    Membership->SetNumberField(TEXT("received_state_bytes"),double(State->BootstrapStateBytes));
    Membership->SetNumberField(TEXT("peak_received_state_bytes"),double(State->BootstrapStatePeakBytes));
    Membership->SetStringField(TEXT("blocked"),State->BootstrapBlocked);
    Membership->SetNumberField(TEXT("directory_seconds"),State->BootstrapDirectorySeconds);
    Membership->SetNumberField(TEXT("import_seconds"),State->BootstrapImportSeconds);
    Membership->SetNumberField(TEXT("completed_seconds"),State->BootstrapCompletedSeconds);
    Membership->SetNumberField(TEXT("directory_expected"),State->ExpectedObjects);Membership->SetNumberField(TEXT("directory_received"),State->DirectoryKeys.size());
    TArray<TSharedPtr<FJsonValue>> BootstrapPeers;
    if (State->Session.IsValid()) for (const auto& M:State->Session->GetMembers())
    {
        const auto Peer=ToCore(M.Player);if (!M.bOnline || Peer==ToCore(State->Session->GetLocalPlayerId())) continue;
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("player"),M.Player.ToString(EGuidFormats::Digits));J->SetBoolField(TEXT("ready"),M.bReady);
        J->SetNumberField(TEXT("parts_expected"),State->BootstrapCounts.contains(Peer)?State->BootstrapCounts.at(Peer):0);
        J->SetNumberField(TEXT("parts_received"),State->BootstrapParts.contains(Peer)?State->BootstrapParts.at(Peer).size():0);
        J->SetNumberField(TEXT("state_bytes"),State->BootstrapSourceBytes.contains(Peer)?double(State->BootstrapSourceBytes.at(Peer)):0);
        J->SetNumberField(TEXT("staged_bytes"),State->BootstrapSourceStagedBytes.contains(Peer)?double(State->BootstrapSourceStagedBytes.at(Peer)):0);
        J->SetBoolField(TEXT("acknowledged"),State->BootstrapSources.contains(Peer));BootstrapPeers.Add(MakeShared<FJsonValueObject>(J));
    }
    Membership->SetArrayField(TEXT("peers"),BootstrapPeers);Root->SetObjectField(TEXT("membership_details"),Membership);
    auto Clock=MakeShared<FJsonObject>();Clock->SetStringField(TEXT("stop_reason"),!State->Client?TEXT("initializing"):IsBattlePaused()?TEXT("membership"):TEXT("advancing"));Root->SetObjectField(TEXT("battle_clock"),Clock);
    TArray<TSharedPtr<FJsonValue>> Objects;uint32 Local=0;const double Now=FPlatformTime::Seconds();
    if (State->Client) for (const auto& [K,O]:State->Client->Objects())
    {
        if (O.Grant.State==Life::Destroyed && (O.Grant.Policy==OwnershipPolicy::RetainedSource || O.Grant.Policy==OwnershipPolicy::ParentWork)) continue;
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),ToUE(K.StableId).ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("definition"),UTF8_TO_TCHAR(O.Definition.c_str()));J->SetNumberField(TEXT("generation"),K.Generation);
        J->SetStringField(TEXT("ownership_group"),ToUE(O.Grant.OwnershipGroup).ToString(EGuidFormats::Digits));J->SetNumberField(TEXT("group_version"),double(O.Grant.GroupEpoch));
        J->SetNumberField(TEXT("ownership_policy"),uint8(O.Grant.Policy));J->SetNumberField(TEXT("life"),uint8(O.Grant.State));
        J->SetStringField(TEXT("controller"),ToUE(O.Grant.Controller).ToString(EGuidFormats::Digits));J->SetStringField(TEXT("simulation_owner"),ToUE(O.Grant.SimOwner).ToString(EGuidFormats::Digits));
        J->SetNumberField(TEXT("owner_version"),double(O.Grant.OwnerEpoch));J->SetNumberField(TEXT("logical_age"),double(O.LogicalAge));
        J->SetBoolField(TEXT("local_simulation"),State->Client->HasSimulation(K));J->SetBoolField(TEXT("dormant"),O.Grant.Dormant);
        J->SetBoolField(TEXT("selected"),State->Selected.contains(K));J->SetBoolField(TEXT("handoff_pending"),State->Handoffs.contains(K));
        const double Last=FMath::Max(State->DeselectedAt.contains(K)?State->DeselectedAt.at(K):Now,State->OperatedAt.contains(K)?State->OperatedAt.at(K):0.);
        J->SetNumberField(TEXT("idle_seconds"),FMath::Max(0.,Now-Last));J->SetNumberField(TEXT("health"),O.Vitals.Value);J->SetNumberField(TEXT("shield"),O.Vitals.Shield);
        if (State->Client->HasSimulation(K)) ++Local;Objects.Add(MakeShared<FJsonValueObject>(J));
    }
    Root->SetBoolField(TEXT("fixed_roles"),true);
    Root->SetArrayField(TEXT("objects"),Objects);Root->SetNumberField(TEXT("object_count"),Objects.Num());Root->SetNumberField(TEXT("local_objects"),Local);
    Root->SetNumberField(TEXT("retired_target_records"),State->RetiredTargetMetadata.Num());
    Root->SetNumberField(TEXT("retired_target_lookups"),double(State->RetiredTargetLookups));
    auto HandoffIssues=MakeShared<FJsonObject>();
    if (State->Client) for (const auto& [Stage,Count]:State->Client->HandoffDiagnostics()) HandoffIssues->SetNumberField(UTF8_TO_TCHAR(Stage.c_str()),double(Count));
    Root->SetObjectField(TEXT("handoff_issues"),HandoffIssues);
    auto HandoffMeasurements=MakeShared<FJsonObject>();
    if (State->Client) for (const auto& [Part,Value]:State->Client->HandoffMeasurements()) HandoffMeasurements->SetNumberField(UTF8_TO_TCHAR(Part.c_str()),double(Value));
    Root->SetObjectField(TEXT("handoff_measurements"),HandoffMeasurements);
    Root->SetStringField(TEXT("resource_recovery_implementation"),TEXT("current-snapshot-v3"));
    TArray<TSharedPtr<FJsonValue>> ResourceSnapshots;
    if (State->Client) for (const auto& [Key,V]:State->Client->ResourceSnapshotStatuses())
    {
        auto Item=MakeShared<FJsonObject>();const auto& G=V.Authority;
        Item->SetStringField(TEXT("group"),UTF8_TO_TCHAR(Key.ToString().c_str()));
        Item->SetStringField(TEXT("owner"),UTF8_TO_TCHAR(G.Owner.ToString().c_str()));
        Item->SetStringField(TEXT("backup"),UTF8_TO_TCHAR(G.Backup.ToString().c_str()));
        Item->SetStringField(TEXT("request_epoch"),LexToString(G.RequestEpoch));
        Item->SetStringField(TEXT("owner_version"),LexToString(G.OwnerVersion));
        Item->SetStringField(TEXT("recovering"),LexToString(G.Recovery));
        Item->SetStringField(TEXT("installed_recovery"),LexToString(V.InstalledRecovery));
        Item->SetStringField(TEXT("snapshot"),LexToString(V.InstalledSnapshot));
        Item->SetStringField(TEXT("snapshot_epoch"),LexToString(V.InstalledEpoch));
        Item->SetStringField(TEXT("sample"),LexToString(V.InstalledSample));
        Item->SetStringField(TEXT("digest"),UTF8_TO_TCHAR(V.InstalledDigest.c_str()));
        Item->SetNumberField(TEXT("complete_copies"),V.CompleteCopies);Item->SetNumberField(TEXT("stored_bytes"),double(V.StoredBytes));
        Item->SetNumberField(TEXT("queued_versions"),V.QueuedVersions);
        TArray<TSharedPtr<FJsonValue>> InstalledObjects;
        for (const auto K:V.InstalledObjects)
        {
            auto Object=MakeShared<FJsonObject>();Object->SetStringField(TEXT("id"),UTF8_TO_TCHAR(K.StableId.ToString().c_str()));
            Object->SetNumberField(TEXT("kind"),unsigned(K.Kind));Object->SetNumberField(TEXT("generation"),K.Generation);
            InstalledObjects.Add(MakeShared<FJsonValueObject>(Object));
        }
        Item->SetArrayField(TEXT("installed_objects"),InstalledObjects);ResourceSnapshots.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("resource_snapshots"),ResourceSnapshots);
    Root->SetNumberField(TEXT("executed_commands"),State->Client?double(State->Client->ExecutedCommandCount()):0);
    Root->SetNumberField(TEXT("rejected_motion_lives"),State->Client?double(State->Client->RejectedMotionLives()):0);
    Root->SetNumberField(TEXT("rejected_state_imports"),State->Client?double(State->Client->RejectedStateImports()):0);
    Root->SetStringField(TEXT("last_state_import_failure"),State->Client?UTF8_TO_TCHAR(State->Client->LastStateImportFailureReason().c_str()):TEXT(""));
    const auto ReceiveRejections=[](const auto& Records)
    {
      TArray<TSharedPtr<FJsonValue>> Values;
      for (const auto& R:Records) {
        auto J=MakeShared<FJsonObject>();
        J->SetStringField(TEXT("object"),ToUE(R.Header.Object.StableId).ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("sender"),ToUE(R.Header.Sender).ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("event"),ToUE(R.Header.Event).ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("match"),ToUE(R.Header.Match).ToString(EGuidFormats::Digits));
        J->SetNumberField(TEXT("epoch"),double(R.Header.Epoch));
        J->SetNumberField(TEXT("kind"),uint8(R.Header.Kind));J->SetNumberField(TEXT("object_kind"),uint8(R.Header.Object.Kind));
        J->SetNumberField(TEXT("generation"),R.Header.Object.Generation);J->SetNumberField(TEXT("owner_version"),double(R.Header.OwnerEpoch));
        J->SetNumberField(TEXT("received_at"),double(R.ReceivedAt));J->SetNumberField(TEXT("sent_at"),double(R.Header.At));
        J->SetNumberField(TEXT("bytes"),double(R.BodyBytes));J->SetBoolField(TEXT("known"),R.ObjectKnown);
        J->SetBoolField(TEXT("materialized"),R.Materialized);J->SetNumberField(TEXT("life"),uint8(R.State));
        Values.Add(MakeShared<FJsonValueObject>(J));
      }
      return Values;
    };
    if (State->Client)
    {
        Root->SetArrayField(TEXT("first_receive_rejections"),ReceiveRejections(State->Client->FirstReceiveRejections()));
        Root->SetArrayField(TEXT("recent_receive_rejections"),ReceiveRejections(State->Client->RecentReceiveRejections()));
    }
    Root->SetNumberField(TEXT("duplicate_results"),State->Client?double(State->Client->DuplicateResults()):0);
    Root->SetNumberField(TEXT("pending_transactions"),State->Client?double(State->Client->PendingTransactions().size()):0);
    auto WorkCounts=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> WorkSamples;
    if (State->Client)
    {
        for (const auto& [Name,Count]:State->Client->WorkCounts()) WorkCounts->SetNumberField(UTF8_TO_TCHAR(Name.c_str()),double(Count));
        for (const auto& D:State->Client->WorkDiagnostics())
        {
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),ToUE(D.Event).ToString(EGuidFormats::Digits));
            J->SetStringField(TEXT("stage"),UTF8_TO_TCHAR(D.Stage.c_str()));J->SetNumberField(TEXT("status"),uint8(D.Status));
            J->SetStringField(TEXT("source"),ToUE(D.Source.StableId).ToString(EGuidFormats::Digits));
            J->SetStringField(TEXT("target"),ToUE(D.Target.StableId).ToString(EGuidFormats::Digits));
            J->SetNumberField(TEXT("source_kind"),uint8(D.Source.Kind));J->SetNumberField(TEXT("source_generation"),D.Source.Generation);
            J->SetStringField(TEXT("request_sender"),ToUE(D.RequestSender).ToString(EGuidFormats::Digits));
            J->SetNumberField(TEXT("request_owner_version"),double(D.RequestOwnerVersion));
            J->SetNumberField(TEXT("request_at"),double(D.RequestAt));J->SetNumberField(TEXT("started_at"),double(D.StartedAt));
            J->SetStringField(TEXT("rule"),UTF8_TO_TCHAR(D.Rule.c_str()));J->SetStringField(TEXT("wait_reason"),UTF8_TO_TCHAR(D.WaitReason.c_str()));
            J->SetBoolField(TEXT("numeric_source_known"),D.NumericSourceKnown);
            J->SetStringField(TEXT("numeric_source_owner"),ToUE(D.NumericSourceOwner).ToString(EGuidFormats::Digits));
            J->SetNumberField(TEXT("numeric_source_owner_version"),double(D.NumericSourceOwnerVersion));
            J->SetNumberField(TEXT("numeric_source_life"),uint8(D.NumericSourceLife));J->SetNumberField(TEXT("numeric_source_age"),double(D.NumericSourceAge));
            J->SetNumberField(TEXT("participants"),double(D.Participants));J->SetNumberField(TEXT("prepared"),double(D.Prepared));J->SetNumberField(TEXT("committed"),double(D.Committed));
            J->SetStringField(TEXT("missing_object"),ToUE(D.Missing.Object.StableId).ToString(EGuidFormats::Digits));
            J->SetStringField(TEXT("missing_capability"),UTF8_TO_TCHAR(D.Missing.Capability.c_str()));
            J->SetNumberField(TEXT("required_version"),double(D.RequiredVersion));J->SetNumberField(TEXT("current_version"),double(D.CurrentVersion));
            WorkSamples.Add(MakeShared<FJsonValueObject>(J));
        }
    }
    Root->SetObjectField(TEXT("transaction_counts"),WorkCounts);Root->SetArrayField(TEXT("transaction_waits"),WorkSamples);
    TSharedPtr<FJsonObject> Commands=MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("directory_hash"),State->Client?UTF8_TO_TCHAR(State->Client->DirectoryHash().c_str()):TEXT(""));
    for (uint8 Kind=1;Kind<=uint8(ObjectKind::BusinessWork);++Kind)
        Commands->SetNumberField(FString::FromInt(Kind),State->Client?double(State->Client->ExecutedCommandCount(ObjectKind(Kind))):0);
    Root->SetObjectField(TEXT("executed_commands_by_kind"),Commands);
    Root->SetNumberField(TEXT("executed_engineer_commands"),State->Client?double(State->Client->ExecutedCommandCount(ObjectKind::Engineer)):0);
    Root->SetNumberField(TEXT("executed_miner_commands"),State->Client?double(State->Client->ExecutedCommandCount(ObjectKind::Miner)):0);
    TArray<TSharedPtr<FJsonValue>> Decisions;
    Decisions.Reserve(int32(State->ResultEvidence.size()));
    for (const auto& V:State->ResultEvidence) Decisions.Add(V);
    TArray<TSharedPtr<FJsonValue>> LocalSubmissions;
    for (const auto& V:State->LocalSubmissionEvidence) LocalSubmissions.Add(V);
    TArray<TSharedPtr<FJsonValue>> Unsettled;
    if (State->Client)
    {
        for (const auto& V:State->Client->UnsettledEvidence()) Unsettled.Add(MakeCommitEvidence(0,V.Result,V.ResultHash,V.DecisionHash));
        Root->SetNumberField(TEXT("unsettled_receipt_count"),double(State->Client->UnsettledReceiptCount()));
    }
    Root->SetArrayField(TEXT("unsettled_receipts"),Unsettled);
    TArray<TSharedPtr<FJsonValue>> Hits;Hits.Reserve(int32(State->HitEvidence.size()));
    for (const auto& V:State->HitEvidence) Hits.Add(V);
    Root->SetArrayField(TEXT("damage_requests"),Hits);
    TArray<TSharedPtr<FJsonValue>> Receivers;
    if (State->Client) for (const auto& [Key,D]:State->Client->MotionDiagnostics())
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),ToUE(Key.StableId).ToString(EGuidFormats::Digits));
        J->SetNumberField(TEXT("generation"),Key.Generation);J->SetNumberField(TEXT("received"),double(D.Received));
        J->SetNumberField(TEXT("applied"),double(D.Applied));J->SetNumberField(TEXT("authority_rejected"),double(D.AuthorityRejected));
        J->SetNumberField(TEXT("stale"),double(D.Stale));Receivers.Add(MakeShared<FJsonValueObject>(J));
    }
    Root->SetArrayField(TEXT("motion_receivers"),Receivers);

    TArray<TSharedPtr<FJsonValue>> Factories; for (const auto& [Kind,Factory] : State->Factories)
        if (Factory.Owner.IsValid()) Factories.Add(MakeShared<FJsonValueNumber>(uint8(Kind)));
    Root->SetArrayField(TEXT("factory_kinds"),Factories);
    int32 Owned=0,SimulationObjects=0;
    TArray<TSharedPtr<FJsonValue>> PlayerObjects;
    if (State->Client) for (const auto& [Key,O] : State->Client->Objects())
    {
        if (State->Client->HasSimulation(Key,true)) ++Owned;
        if (O.Grant.State!=Life::Destroyed) ++SimulationObjects;
        if (O.Grant.Policy==OwnershipPolicy::ControllerPinned && !O.Grant.RetireOnVacancy)
        {
            auto Player=MakeShared<FJsonObject>();
            Player->SetStringField(TEXT("stable_id"),ToUE(Key.StableId).ToString(EGuidFormats::Digits));
            Player->SetStringField(TEXT("seat"),O.Grant.ControllerSeat==Role::Ground?TEXT("Ground"):TEXT("Air"));
            Player->SetStringField(TEXT("controller"),ToUE(O.Grant.Controller).ToString(EGuidFormats::Digits));
            Player->SetStringField(TEXT("simulation_owner"),ToUE(O.Grant.SimOwner).ToString(EGuidFormats::Digits));
            Player->SetNumberField(TEXT("life_generation"),Key.Generation);
            Player->SetNumberField(TEXT("life_state"),uint8(O.Grant.State));
            Player->SetNumberField(TEXT("owner_epoch"),double(O.Grant.OwnerEpoch));
            if (const auto It=State->MotionPublished.find(Key); It!=State->MotionPublished.end())
            {
                const auto& D=It->second; auto M=MakeShared<FJsonObject>();
                M->SetNumberField(TEXT("attempts"),double(D.Attempts)); M->SetNumberField(TEXT("accepted"),double(D.Accepted));
                M->SetNumberField(TEXT("authority_blocked"),double(D.AuthorityBlocked)); M->SetNumberField(TEXT("clock_blocked"),double(D.ClockBlocked));
                M->SetNumberField(TEXT("queue_failed"),double(D.QueueFailed)); M->SetNumberField(TEXT("last_tick"),double(D.LastTick));
                Player->SetObjectField(TEXT("motion_publish"),M);
            }
            if (const auto It=State->Client->MotionDiagnostics().find(Key); It!=State->Client->MotionDiagnostics().end())
            {
                const auto& D=It->second; auto M=MakeShared<FJsonObject>();
                M->SetNumberField(TEXT("received"),double(D.Received)); M->SetNumberField(TEXT("applied"),double(D.Applied));
                M->SetNumberField(TEXT("authority_rejected"),double(D.AuthorityRejected));
                M->SetNumberField(TEXT("stale"),double(D.Stale)); M->SetNumberField(TEXT("invalid_state"),double(D.InvalidState));
                M->SetNumberField(TEXT("last_received_tick"),double(D.LastReceived)); M->SetNumberField(TEXT("last_applied_tick"),double(D.LastApplied));
                M->SetNumberField(TEXT("received_owner_epoch"),double(D.ReceivedOwnerEpoch)); M->SetNumberField(TEXT("expected_owner_epoch"),double(D.ExpectedOwnerEpoch));
                M->SetStringField(TEXT("sender"),ToUE(D.Sender).ToString(EGuidFormats::Digits));
                M->SetStringField(TEXT("expected_sender"),ToUE(D.ExpectedSender).ToString(EGuidFormats::Digits));
                Player->SetObjectField(TEXT("motion"),M);
            }
            Player->SetBoolField(TEXT("local_simulation"),State->Client->HasSimulation(Key,true));
            Player->SetBoolField(TEXT("dormant"),O.Grant.Dormant);
            Player->SetStringField(TEXT("state_custodian"),ToUE(O.Grant.StateCustodian).ToString(EGuidFormats::Digits));
            Player->SetBoolField(TEXT("local_private_custody"),HasPrivateCustody(O.Grant,ToCore(State->Session->GetLocalPlayerId())));
            PlayerObjects.Add(MakeShared<FJsonValueObject>(Player));
        }
    }
    Root->SetArrayField(TEXT("player_objects"),PlayerObjects);
    TArray<TSharedPtr<FJsonValue>> Publishers;
    for (const auto& [Key,D]:State->MotionPublished)
    {
        auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("id"),ToUE(Key.StableId).ToString(EGuidFormats::Digits));
        M->SetNumberField(TEXT("generation"),Key.Generation);M->SetNumberField(TEXT("accepted"),double(D.Accepted));
        M->SetNumberField(TEXT("authority_blocked"),double(D.AuthorityBlocked));M->SetNumberField(TEXT("queue_failed"),double(D.QueueFailed));
        Publishers.Add(MakeShared<FJsonValueObject>(M));
    }
    Root->SetArrayField(TEXT("motion_publishers"),Publishers);
    TArray<TSharedPtr<FJsonValue>> Members;
    if (State->Session.IsValid()) for (const auto& M : State->Session->GetMembers())
    {
        auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("player"),M.Player.ToString(EGuidFormats::Digits));
        V->SetStringField(TEXT("team"),M.Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"));
        V->SetStringField(TEXT("role"),M.Role==EGuLiCommanderRole::Commander?TEXT("Commander")
            :M.Role==EGuLiCommanderRole::Ground?TEXT("Ground"):M.Role==EGuLiCommanderRole::Air?TEXT("Air"):TEXT("Observer"));
        V->SetBoolField(TEXT("online"),M.bOnline); V->SetNumberField(TEXT("seat_version"),double(M.SeatVersion));
        Members.Add(MakeShared<FJsonValueObject>(V));
    }
    Root->SetArrayField(TEXT("role_members"),Members);
    Root->SetNumberField(TEXT("local_objects"),Owned);
    Root->SetNumberField(TEXT("simulation_object_count"),SimulationObjects);
    Root->SetArrayField(TEXT("client_results"),Decisions);
    Root->SetArrayField(TEXT("local_submission_rejections"),LocalSubmissions);
    Root->SetArrayField(TEXT("handoff_history"),State->HandoffEvidence);
    if (State->Client)
    {
        const auto& S=State->Client->NumericState().WorldResources().Snapshot();auto Resources=MakeShared<FJsonObject>();
        Resources->SetNumberField(TEXT("processed_blue"),S.Processed.Blue);Resources->SetNumberField(TEXT("processed_red"),S.Processed.Red);
        Resources->SetNumberField(TEXT("factory_count"),S.Factories.size());Resources->SetNumberField(TEXT("miner_count"),S.Miners.size());
        int64 Remaining=0,Cargo=0,Queued=0;
        for (const auto& [K,V]:S.Remaining) Remaining+=V;
        for (const auto& [K,V]:S.Miners) Cargo+=int64(V.Cargo.Blue)+V.Cargo.Red;
        for (const auto& [K,V]:S.Factories) Queued+=ResourceLedger::Queued(V);
        Resources->SetNumberField(TEXT("remaining_raw"),Remaining);Resources->SetNumberField(TEXT("cargo_raw"),Cargo);Resources->SetNumberField(TEXT("queued_raw"),Queued);
        Root->SetObjectField(TEXT("resources"),Resources);
    }
    return Root;
}
FString UGuLiHybridWorldSubsystem::GetReviewEvidenceJson() const
{
    FString Json;FJsonSerializer::Serialize(GetReviewEvidenceSnapshot(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));return Json;
}
double UGuLiHybridWorldSubsystem::GetLogicalSeconds() const { return double(GetLogicalTick())/TicksPerSecond; }
double UGuLiHybridWorldSubsystem::GetLocalLogicalSeconds() const { return double(GetLocalLogicalTick())/TicksPerSecond; }
bool UGuLiHybridWorldSubsystem::SetReviewSimulationRate(double Rate)
{
#if !UE_BUILD_SHIPPING
    FString Run,Case;
    if (FMath::IsFinite(Rate) && Rate>=0 && Rate<=1
        && FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),Run) && !Run.IsEmpty()
        && FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),Case) && Case.StartsWith(TEXT("fault_")))
    {State->ReviewSimulationRate=Rate;return true;}
#endif
    return false;
}
bool UGuLiHybridWorldSubsystem::ReplayReviewBusinessRequest(FGuid Event,int32 Count,TSharedPtr<FJsonObject>& Evidence,FString& Error)
{
#if !UE_BUILD_SHIPPING
    auto Denied=[&](const TCHAR* Why) {Error=Why;return false;};
    if (!State->DetailedReview || !State->Client || !State->Session.IsValid() || !Event.IsValid()
        || Count<1 || Count>256 || !IsWorldReady() || !CanPublishFacts())
        return Denied(TEXT("Recorded business replay requires an active fault acceptance room"));
    const auto Supply=State->Manifest.Numbers.find("acceptance/resource_supply");
    const auto Fixture=State->Manifest.Strings.find("acceptance/preparation");
    const bool Preparation=Supply!=State->Manifest.Numbers.end() && Supply->second==1
        && Fixture!=State->Manifest.Strings.end() && Fixture->second=="gf-fast-spatial-v2";
    const auto Local=ToCore(State->Session->GetLocalPlayerId()),Room=ToCore(State->Session->GetRoomId());
    std::string Encoded;
    const bool Complete=State->Client->Inspect("replay/"+ToCore(Event).ToString()+"/"+std::to_string(Count),
        [Event,Count,Preparation,Local,Room](ClientWorld& Client)
    {
        TSharedPtr<FJsonObject> Evidence;FString Error;
        auto Denied=[&](const TCHAR* Why) {Error=Why;return false;};
        auto Execute=[&]()->bool
        {
    const auto& Numeric=Client.NumericState();
    const auto Original=Numeric.Result(ToCore(Event));
    const bool PreparedCredit=Preparation && Original && Original->Kind==TransactionKind::Credit;
    if (!Original || Original->Status!=CommitStatus::Committed
        || (!PreparedCredit && Original->Kind!=TransactionKind::Extract && Original->Kind!=TransactionKind::Deliver && Original->Kind!=TransactionKind::Process))
        return Denied(TEXT("Replay requires an original committed resource event in the authorized preparation profile"));
    const std::array<ObjectKey,1> Domain{Original->StateDomain};const auto Parts=Numeric.Export(Domain);
    const auto Name="event/"+ToCore(Event).ToString();
    const auto Found=std::find_if(Parts.begin(),Parts.end(),[&](const auto& F){return F.Key.Capability==Name;});
    if (Found==Parts.end()) return Denied(TEXT("Original canonical request is unavailable at this numeric owner"));
    Bytes StoredDecision,Fingerprint,Body;Reader Stored(Found->Value);
    if (!Stored.Blob(StoredDecision) || !Stored.Blob(Fingerprint) || !Stored.Done())
        return Denied(TEXT("Original event fragment is invalid"));
    Envelope Header;Reader Request(Fingerprint);
    if (!Request.Key(Header.Object) || !Request.U64(Header.At) || !Request.Blob(Body) || !Request.Done())
        return Denied(TEXT("Original request fingerprint is invalid"));
    const auto Source=Client.Objects().find(Header.Object);
    if (Source==Client.Objects().end() || Numeric.Owner(Header.Object)!=Local || Numeric.Busy(Original->StateDomain))
        return Denied(TEXT("Replay requires the current authenticated source owner and an idle execution domain"));
    Header.Kind=MessageKind::Transaction;Header.Match=Room;Header.Epoch=Client.Epoch();
    Header.Sender=Local;Header.Event=ToCore(Event);Header.OwnerEpoch=Source->second.Grant.OwnerEpoch;
    Header.ControlEpoch=Source->second.Grant.ControlEpoch;Header.Sequence=1;
    TransactionRequest OriginalRequest;
    if (!DecodeTransaction(Header,Body,OriginalRequest) || OriginalRequest.Kind!=Original->Kind
        || Numeric.Owner(Numeric.ExecutionDomain(OriginalRequest))!=Local
        || EncodeTransaction(OriginalRequest)!=Body || StoredDecision!=EncodeCommit(*Original))
        return Denied(TEXT("Replay does not identify the unchanged original request and committed decision"));
    std::set<ObjectKey> Keys{Original->StateDomain};
    for (const auto& Required:Original->RequiredStates) Keys.insert(Required.Object);
    const std::vector<ObjectKey> Scope(Keys.begin(),Keys.end());
    auto NumericHash=[&]()
    {
        const auto Fragments=Numeric.Export(Scope);Writer W;W.U32(static_cast<uint32>(Fragments.size()));
        for (const auto& F:Fragments) WriteStateFragment(W,F);return W.Good()?Sha256(W.Take()):std::string{};
    };
    const auto Before=NumericHash(),DecisionBefore=Sha256(EncodeCommitDecision(*Original));
    if (Before.empty()) return Denied(TEXT("Original numeric cut exceeds the bounded replay evidence size"));
    const auto DuplicatesBefore=Client.DuplicateResults();int32 Received=0;
    // ReceivePeer is the production transaction handler. Submit would replace
    // the original logical time and therefore would not replay the same request.
    for (int32 I=0;I<Count;++I) if (Client.ReceivePeer(Local,{Header,Body})) ++Received;
    const auto AfterResult=Numeric.Result(ToCore(Event));
    Evidence=MakeShared<FJsonObject>();
    auto Hash=[&](const TCHAR* Key,const std::string& Value){Evidence->SetStringField(Key,UTF8_TO_TCHAR(Value.c_str()));};
    Evidence->SetStringField(TEXT("scope"),TEXT("local-authenticated-request-replay"));
    Evidence->SetStringField(TEXT("original_event"),Event.ToString(EGuidFormats::Digits));
    Evidence->SetStringField(TEXT("source"),ToUE(Header.Object.StableId).ToString(EGuidFormats::Digits));
    Evidence->SetNumberField(TEXT("business_kind"),uint8(Original->Kind));
    Evidence->SetNumberField(TEXT("original_at"),double(Header.At));
    Evidence->SetNumberField(TEXT("requested_replays"),Count);Evidence->SetNumberField(TEXT("local_replays"),Received);
    Evidence->SetNumberField(TEXT("duplicate_delta"),double(Client.DuplicateResults()-DuplicatesBefore));
    Hash(TEXT("numeric_before_sha256"),Before);Hash(TEXT("numeric_after_sha256"),NumericHash());
    Hash(TEXT("decision_hash_before"),DecisionBefore);
    Hash(TEXT("decision_hash_after"),AfterResult?Sha256(EncodeCommitDecision(*AfterResult)):std::string{});
    Hash(TEXT("request_fingerprint_sha256"),Sha256(Fingerprint));
    return true;
        };
        const bool Good=Execute();if (!Good) {Evidence=MakeShared<FJsonObject>();Evidence->SetStringField(TEXT("error"),Error);}
        FString Json;const auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Evidence.ToSharedRef(),Writer);
        return std::string(TCHAR_TO_UTF8(*Json));
    },Encoded);
    if (!Complete) return Denied(State->Client->IsOperationPending()?TEXT("Recorded business replay pending"):TEXT("Recorded business replay input queue rejected"));
    const auto Reader=TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(Encoded.c_str()));
    if (!FJsonSerializer::Deserialize(Reader,Evidence) || !Evidence) return Denied(TEXT("Invalid recorded replay response"));
    Error.Reset();if (Evidence->TryGetStringField(TEXT("error"),Error)) return false;
    return true;
#else
    Error=TEXT("Recorded business replay is unavailable");return false;
#endif
}
bool UGuLiHybridWorldSubsystem::IsBattlePaused() const { return !State->Client || State->Client->IsPaused(); }
bool UGuLiHybridWorldSubsystem::CanPublishFacts() const { return State->Client && State->Client->CanPublishFacts(); }
bool UGuLiHybridWorldSubsystem::IsWorldReady() const { return State->Ready && State->Failure.IsEmpty(); }
const FString& UGuLiHybridWorldSubsystem::GetFailure() const { return State->Failure; }
void UGuLiHybridWorldSubsystem::Suspend(const FString& Reason)
{
    if (State->Client) State->Client->Cancel(TCHAR_TO_UTF8(*Reason));
    if (State->Failure.IsEmpty())
    { State->Failure=Reason; UE_LOG(LogTemp,Error,TEXT("Hybrid battle suspended: %s"),*Reason); OnFailure.Broadcast(Reason); }
}
void UGuLiHybridWorldSubsystem::HandleFailure(const FString& Error)
{
    if (State->Session.IsValid() && State->Session->IsReconnecting()) return;
    Suspend(Error);
}
TStatId UGuLiHybridWorldSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiHybridWorldSubsystem,STATGROUP_Tickables); }

void UGuLiHybridWorldSubsystem::TryWorldReady()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_WorldReady);
    if (!State->Client || !State->DirectoryComplete || !State->Failure.IsEmpty()
        || State->Client->IsBootstrapMaterializationDeferred()) return;
    for (auto It=State->WaitingDirectory.begin(); It!=State->WaitingDirectory.end();)
    {
        if (State->Client->Install(It->second,true)) It=State->WaitingDirectory.erase(It); else ++It;
    }
    if (!State->WaitingDirectory.empty() || State->DirectoryKeys.size()<State->ExpectedObjects) return;
    // This client's directory retains retired records and never removes keys.
    // New coordinator registrations invalidate the presence check below; roster
    // acknowledgement/reconnect retries still run on every readiness attempt.
    if (!State->RequiredDirectoryValidated)
    {
        for (const auto& Key:State->RequiredDirectoryKeys) if (!State->Client->Objects().contains(Key)) return;
        State->RequiredDirectoryValidated=true;
    }
    const auto* Member=State->Session->GetMembers().FindByPredicate([&](const FGuLiHybridMember& M)
    { return M.Player==State->Session->GetLocalPlayerId() && M.bOnline; });
    const double Now=FPlatformTime::Seconds();
    // A restore may overtake the initial WorldReady. Retry until the roster
    // acknowledges this connection, including a reconnecting fixed-role player.
    if ((!State->Ready || (Member && !Member->bReady)) && Now>=State->NextReadySend)
    {
        State->NextReadySend=Now+1.;
        Message Ready; Ready.Header.Kind=MessageKind::WorldReady;
        if (State->Session->SendControl(ToUE(Ready)))
        { const bool First=!State->Ready; State->Ready=true; if (First) OnReady.Broadcast(); }
    }
}
void UGuLiHybridWorldSubsystem::Tick(float Delta)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_WorldTick);
    SynchronizeGameplayRoster();
    TryWorldReady();
    if (!State->Client) return;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_ApplyCompletedWork);
        State->Client->PollNetwork(FPlatformTime::Seconds());
    }

    if (State->DirectoryResumePending)
    {
        if (State->Client->SetBootstrapMaterializationDeferred(false)) State->DirectoryResumePending=false;
        else if (!State->Client->Error().empty()) {Suspend(UTF8_TO_TCHAR(State->Client->Error().c_str()));return;}
    }
    const auto Async=State->Client->Statistics();const auto Previous=State->LastAsyncStats;State->LastAsyncStats=Async;
    State->UnreportedSimulationMilliseconds+=Async.SimulationMilliseconds-Previous.SimulationMilliseconds;
    if (auto* Session=State->Session.Get()) if (Async.SimulationSteps>Previous.SimulationSteps)
    {Session->AccumulateSimulationCost(State->UnreportedSimulationMilliseconds,uint32(Async.SimulationSteps-Previous.SimulationSteps));State->UnreportedSimulationMilliseconds=0;}
    if (auto* Perf=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>();Perf && Perf->IsCapturing())
    {
        Perf->Record(TEXT("HybridWorker"),Async.WorkerMilliseconds-Previous.WorkerMilliseconds);
        Perf->Record(TEXT("HybridMainApply"),Async.MainApplyMilliseconds-Previous.MainApplyMilliseconds);
        Perf->Record(TEXT("HybridQueueWait"),Async.LastQueueMilliseconds);
        Perf->Record(TEXT("HybridInputMiB"),double(Async.InputBytes)/(1024*1024));
        Perf->Record(TEXT("HybridOutputMiB"),double(Async.OutputBytes)/(1024*1024));
    }
    PumpMembership();
    // Membership imports install the complete peer-authored directory here,
    // after the first readiness attempt earlier in this tick.
    TryWorldReady();
    if (!State->Failure.IsEmpty()) return;
    const bool ImportingMembership=State->MembershipVersion && State->MembershipJoin && !State->BootstrapApplied
        && State->Session.IsValid() && State->MembershipPlayer==ToCore(State->Session->GetLocalPlayerId());
    // The room's existing membership deadline governs the in-flight import.
    // Initial-world readiness cannot diagnose missing objects before it arrives.
    if (State->DirectoryComplete && !State->Ready && !ImportingMembership
        && FPlatformTime::Seconds()-State->DirectoryStarted>State->Config.ReadyTimeout)
    {
        FString Missing;
        for (const auto& [K,V] : State->WaitingDirectory)
        { Missing=FString::Printf(TEXT("Missing/failed object adapter: kind=%u definition=%s id=%s"),unsigned(K.Kind),UTF8_TO_TCHAR(V.Definition.c_str()),UTF8_TO_TCHAR(K.StableId.ToString().c_str())); break; }
        Suspend(Missing.IsEmpty() ? FString::Printf(TEXT("Hybrid directory count mismatch: installed=%llu expected=%u membership=%llu imported=%d"),
            uint64(State->Client->Objects().size()),State->ExpectedObjects,State->MembershipVersion,State->BootstrapApplied) : Missing); return;
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_QueueSimulation);
        State->Client->Advance(Delta*State->ReviewSimulationRate);
    }

    if (!State->Client->Error().empty()) Suspend(UTF8_TO_TCHAR(State->Client->Error().c_str()));
}

void UGuLiHybridWorldSubsystem::SynchronizeGameplayRoster()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_SynchronizeGameplayRoster);
    auto* Session=State->Session.Get(); auto* World=GetWorld();
    auto* Battle=World ? World->GetGameState<AGuLiBattleGameState>() : nullptr;
    if (!Session || !State->Client || !Battle || !State->Failure.IsEmpty()) return;
    auto* LocalController=World->GetFirstPlayerController();
    auto* LocalState=LocalController ? LocalController->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
    if (!LocalState) return;
    TSet<FGuid> Online;
    for (const auto& Member : Session->GetMembers()) if (Member.bOnline) Online.Add(Member.Player);
    const auto Slots=Battle->GetRoleSlots();
    for (const auto& Slot : Slots)
        if (Slot.PlayerGuid.IsValid() && !Session->GetMembers().ContainsByPredicate([&](const auto& M)
            { return M.bOnline && M.Player==Slot.PlayerGuid && M.Team==Slot.Team && M.Role==Slot.Role; }))
            Battle->ReleaseRoleSlot(Slot.SlotIndex,Slot.PlayerGuid);
    for (const auto& Member : Session->GetMembers())
    {
        if (!Member.bOnline) continue;
        if (Member.Role==EGuLiCommanderRole::Observer)
        {
            if (Member.Player==Session->GetLocalPlayerId())
            { LocalState->ApplyHybridRole(Member.Player,Member.Team,Member.Role,MAX_uint8,false); LocalState->SetHybridSeatVersion(Member.SeatVersion); }
            continue;
        }
        uint8 Slot=MAX_uint8;
        for (const auto& Candidate : Battle->GetRoleSlots())
            if (Candidate.Team==Member.Team && Candidate.Role==Member.Role) { Slot=Candidate.SlotIndex; break; }
        if (Slot==MAX_uint8 || !Battle->ClaimExactRoleSlot(Member.Player,Slot))
        { Suspend(TEXT("Hybrid roster conflicts with a gameplay role slot")); return; }
        AGuLiBattlePlayerState* Player=Member.Player==Session->GetLocalPlayerId() ? LocalState : nullptr;
        if (!Player) for (APlayerState* Entry : Battle->PlayerArray)
            if (auto* PS=Cast<AGuLiBattlePlayerState>(Entry); PS && PS->GetPlayerGuid()==Member.Player) { Player=PS; break; }
        if (!Player)
        {
            Player=World->SpawnActor<AGuLiBattlePlayerState>();
            if (Player) Player->SetReplicates(false);
        }
        const bool Ready=Member.bReady && IsWorldReady() && !IsBattlePaused();
        if (!Player || !Player->ApplyHybridRole(Member.Player,Member.Team,Member.Role,Slot,Ready))
        { Suspend(TEXT("Hybrid gameplay identity binding failed")); return; }
        Player->SetHybridSeatVersion(Member.SeatVersion);
        Battle->SetRoleSlotBattleReady(Slot,Member.Player,Ready); Battle->SetRoleSlotSyncReady(Slot,Member.Player,Ready);
    }
    if (LocalState->IsCommander() && LocalState->IsBattleReady())
        if (auto* Mode=World->GetAuthGameMode<AGuLiBattleGameMode>(); Mode && !Mode->EnsureHybridCommanderPawn(*LocalController))
            Suspend(TEXT("Hybrid Commander camera/input Pawn could not be created from the production role configuration"));
}

bool UGuLiHybridWorldSubsystem::PublishMotion(const FGuLiHybridObjectKey& Key,const TArray<uint8>& Body)
{
    if (!State->Client || !State->Client->Objects().contains(ToCore(Key))) return false;
    auto& Stats=State->MotionPublished[ToCore(Key)]; ++Stats.Attempts; Stats.LastTick=GetLogicalTick();
    if (!State->Session.IsValid() || !HasSimulation(Key)) {++Stats.AuthorityBlocked;return false;}
    if (!State->Client->CanPublishFacts()) {++Stats.ClockBlocked;return false;}
    const auto& A=State->Client->Objects().at(ToCore(Key)).Grant;
    Message M; M.Header.Kind=MessageKind::Motion; M.Header.Object=A.Object; M.Header.OwnerEpoch=A.OwnerEpoch;
    M.Header.ControlEpoch=A.ControlEpoch; M.Header.At=GetObjectLogicalTick(Key);
    if (!State->Client->StampObjectPublication(M.Header)) {++Stats.ClockBlocked;return false;}
    if (!Body.IsEmpty()) M.Body.assign(Body.GetData(),Body.GetData()+Body.Num());
    const bool Queued=State->Session->BroadcastPeer(ToUE(M));
    if (Queued) ++Stats.Accepted; else ++Stats.QueueFailed;
    return Queued;
}
void UGuLiHybridWorldSubsystem::PublishComponentMotionIfDue(const FGuLiHybridObjectKey& Key,FGuLiHybridComponentBindingContext* Context)
{
    if (!State->Client || !State->Session.IsValid() || !CanPublishFacts() || !HasSimulation(Key)) return;
    const auto K=ToCore(Key);const auto& O=State->Client->Objects().at(K);
    const auto I=State->PublicMotionPublished.find(K);const auto Now=GetObjectLogicalTick(Key);
    if (I!=State->PublicMotionPublished.end() && std::get<0>(I->second)==State->Client->Epoch()
        && std::get<1>(I->second)==O.Grant.OwnerEpoch && Now>=std::get<2>(I->second)
        && Now-std::get<2>(I->second)<GuLiNet::TicksPerSecond/FGuLiHybridComponentState::PublicMotionHz) return;
    const auto* Actor=FindBoundActor(Key);
    if (!Actor) return;
    TArray<uint8> Public;
    if (UGuLiHotspotOptions::Enabled(GetWorld(),EGuLiHotspot::ComponentCache))
    {
        const auto Capture=FGuLiHybridComponentState::TryCapturePublic(*Actor,Public,Context);
        if (Capture==EGuLiHybridPublicCapture::Absent) return;
        if (Capture==EGuLiHybridPublicCapture::Invalid) {Suspend(TEXT("Cannot capture public object capabilities"));return;}
    }
    else
    {
        if (!FGuLiHybridComponentState::HasPublicState(*Actor)) return;
        if (!FGuLiHybridComponentState::CapturePublic(*Actor,Public)) {Suspend(TEXT("Cannot capture public object capabilities"));return;}
    }
    Message M;M.Header.Kind=MessageKind::ComponentState;M.Header.Object=K;M.Header.OwnerEpoch=O.Grant.OwnerEpoch;
    M.Header.ControlEpoch=O.Grant.ControlEpoch;M.Header.At=Now;M.Body.assign(Public.GetData(),Public.GetData()+Public.Num());
    if (!State->Client->StampObjectPublication(M.Header)) return;
    if (State->Session->BroadcastPeer(ToUE(M))) State->PublicMotionPublished[K]={State->Client->Epoch(),O.Grant.OwnerEpoch,Now};
}
bool UGuLiHybridWorldSubsystem::PublishMotionTo(const FGuLiHybridObjectKey& Key,const FGuid& Peer,const TArray<uint8>& Body,uint64 CapturedTick)
{
    if (!State->Client || !State->Session.IsValid() || !HasSimulation(Key) || !State->Client->CanPublishFacts()) return false;
    const auto& A=State->Client->Objects().at(ToCore(Key)).Grant;
    Message M; M.Header.Kind=MessageKind::Motion; M.Header.Object=A.Object; M.Header.OwnerEpoch=A.OwnerEpoch;
    M.Header.ControlEpoch=A.ControlEpoch; M.Header.At=CapturedTick==MAX_uint64?GetObjectLogicalTick(Key):CapturedTick;
    if (!State->Client->StampObjectPublication(M.Header)) return false;
    if (!Body.IsEmpty()) M.Body.assign(Body.GetData(),Body.GetData()+Body.Num());
    return State->Session->SendPeer(Peer,ToUE(M));
}
bool UGuLiHybridWorldSubsystem::PublishEffect(const FGuLiHybridObjectKey& Key,const FGuid& Event,const TArray<uint8>& Body)
{
    if (!State->Client || !State->Session.IsValid() || !HasSimulation(Key) || !State->Client->CanPublishFacts() || !Event.IsValid()
        || Body.IsEmpty()) return false;
    return State->Client->PublishObjectEvent(ToCore(Key),ToCore(Event),Bytes(Body.GetData(),Body.GetData()+Body.Num()));
}
bool UGuLiHybridWorldSubsystem::PublishMotionAcknowledgment(const FGuLiHybridObjectKey& Key,const TArray<uint8>& Body)
{
    FGuLiHybridObjectState Object;
    if (!State->Session.IsValid() || Body.Num()>256 || !GetObject(Key,Object) || HasSimulation(Key)) return false;
    Message M; M.Header.Kind=MessageKind::MotionAcknowledgment; M.Header.Object=ToCore(Key);
    M.Header.OwnerEpoch=Object.OwnerEpoch; M.Header.ControlEpoch=Object.ControlEpoch; M.Header.At=GetLogicalTick();
    if (!State->Client || !State->Client->StampObjectPublication(M.Header)) return false;
    if (!Body.IsEmpty()) M.Body.assign(Body.GetData(),Body.GetData()+Body.Num());
    return State->Session->SendPeer(Object.SimulationOwner,ToUE(M));
}
bool UGuLiHybridWorldSubsystem::ResolveObjectTarget(const FGuLiHybridObjectKey& Key,FGuLiTargetHandle& Out) const
{
    FGuLiHybridObjectState Object;
    if (!GetObjectMetadata(Key,Object)) return false;
    for (const auto& Pair:State->Targets) if (Pair.Value==Key) { Out=Pair.Key; return true; }
    return ResolveRecordTarget(*GetWorld(),Object,Out);
}
bool UGuLiHybridWorldSubsystem::FindDamageResult(const FGuid& Event,FGuLiDamageCommitResult& Out) const
{ if (const auto* Result=State->DamageResults.Find(Event)) { Out=*Result; return true; } return false; }
FGuid UGuLiHybridWorldSubsystem::SubmitDamage(const FGuLiDamageRequest& R,uint64 StartedAt,TFunction<void(const FGuLiDamageCommitResult&)> Callback,const FGuLiHybridObjectKey* EffectAuthority,TOptional<uint64> OriginalAt)
{
    FGuLiHybridObjectKey Source,Target;
    if (!State->Client || !ResolveTarget(R.Target,Target) || R.SkillId.IsNone()) return {};
    if (EffectAuthority)
    {
        if (!EffectAuthority->IsValid() || !HasSimulation(*EffectAuthority,true)) return {};
        Source=*EffectAuthority;
    }
    else if (!ResolveTarget(R.Source,Source)) return {};
    TransactionRequest V; V.Header.Event=ToCore(R.DamageEventId); V.Header.Object=ToCore(Source); V.Header.At=GetObjectLogicalTick(Source);
    if (OriginalAt.IsSet()) V.Header.At=OriginalAt.GetValue();
    V.Kind=TransactionKind::Damage; V.Target=ToCore(Target);
    V.RootEvent=ToCore(R.RootEventId.IsValid() ? R.RootEventId : R.ShotId); V.StartedAt=StartedAt;
    FGuLiHybridObjectState SourceState;
    if (!GetObject(Source,SourceState)) return {};
    V.RuleId=TCHAR_TO_UTF8(*(EffectAuthority ? TEXT("settle/")+SourceState.Definition.ToString()
        : FGuLiHybridManifest::MakeDamageRuleId(SourceState.Definition.ToString(),R.SkillId,R.WeaponBinding.SlotId)));
    V.TargetX=R.HitLocation.X; V.TargetY=R.HitLocation.Y;
    const TWeakObjectPtr<UGuLiHybridWorldSubsystem> Weak(this);
    const auto Event=State->Client->Submit(V,[Weak,Callback=MoveTemp(Callback)](const CommitResult& Result)
    {
        if (!Weak.IsValid() || !Callback) return;
        FGuLiDamageCommitResult Out;
        Out.Status=Result.Status==CommitStatus::Committed ? EGuLiDamageCommitStatus::Committed
            : Result.Status==CommitStatus::Duplicate ? EGuLiDamageCommitStatus::Duplicate : EGuLiDamageCommitStatus::RejectedByAdapter;
        Out.AppliedDamage=Result.Damage.Applied; Out.AbsorbedDamage=Result.Damage.Absorbed; Out.RemainingHealth=Result.Vitals.Value;
        Out.bKilled=Result.Damage.Killed; Out.DeathEventId=ToUE(Result.DeathEvent); Out.RewardEventId=ToUE(Result.RewardEvent);
        Out.CommitOrdinal=Result.StateVersion; Callback(Out);
    },OriginalAt.IsSet());
    if (Event.Valid() && CapturesGameplayEvidence())
    {
        FGuLiHybridObjectKey Attacker;const bool OriginKnown=ResolveTarget(R.Source,Attacker);
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("sequence"),double(++State->HitEvidenceSequence));
        J->SetStringField(TEXT("event"),ToUE(Event).ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("root"),ToUE(V.RootEvent).ToString(EGuidFormats::Digits));J->SetStringField(TEXT("shot"),R.ShotId.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("source"),Attacker.StableId.ToString(EGuidFormats::Digits));J->SetNumberField(TEXT("source_kind"),uint8(Attacker.Kind));
        J->SetBoolField(TEXT("source_known"),OriginKnown);J->SetStringField(TEXT("authority"),Source.StableId.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("target"),Target.StableId.ToString(EGuidFormats::Digits));J->SetNumberField(TEXT("target_generation"),Target.LifeGeneration);
        J->SetStringField(TEXT("skill"),R.SkillId.ToString());J->SetStringField(TEXT("subject"),R.WeaponBinding.SubjectId.ToString());
        J->SetStringField(TEXT("slot"),R.WeaponBinding.SlotId.ToString());J->SetNumberField(TEXT("requested_damage"),R.Damage);
        if (State->HitEvidence.size()>=4096) State->HitEvidence.pop_front();
        State->HitEvidence.push_back(MakeShared<FJsonValueObject>(J));
    }
    return ToUE(Event);
}
bool UGuLiHybridWorldSubsystem::GetResourceQuerySummary(int32& Nodes,int64& Remaining,uint64& Revision) const
{
    Nodes=static_cast<int32>(State->ResourceView.Remaining.size()); Remaining=0; Revision=State->ResourceView.Version;
    for (const auto& [Node,Amount]:State->ResourceView.Remaining) Remaining+=Amount;
    return State->ResourceView.Configured;
}
bool UGuLiHybridWorldSubsystem::GetResourceNodeRemaining(uint32 Node,uint8& Remaining) const
{
    const auto It=State->ResourceView.Remaining.find(Node);
    if (It==State->ResourceView.Remaining.end()) return false;
    Remaining=It->second; return true;
}
bool UGuLiHybridWorldSubsystem::GetResourceDirectoryMetadata(FString& LayoutHash,int32& Nodes,uint64& Revision) const
{
    LayoutHash=State->ResourceLayoutHash; Nodes=static_cast<int32>(State->ResourceView.Remaining.size());
    Revision=State->ResourceView.Version; return State->ResourceView.Configured;
}
EGuLiTeam UGuLiHybridWorldSubsystem::GetResourceTerritoryOwner(uint32 Territory) const
{
    return Territory<State->ResourceView.Owners.size()?ToUE(State->ResourceView.Owners[Territory]):EGuLiTeam::Unassigned;
}
FGuid UGuLiHybridWorldSubsystem::SubmitResourceOperation(const FGuLiHybridResourceRequest& Request)
{ return SubmitResourceOperationNative(Request,{}); }
FGuid UGuLiHybridWorldSubsystem::SubmitResourceOperationNative(const FGuLiHybridResourceRequest& R,TFunction<void(const FGuLiHybridTransactionResult&)> Callback)
{
    FGuLiHybridObjectState Source;
    if (!State->Client || R.Operation==EGuLiHybridResourceOperation::None || R.Operation>EGuLiHybridResourceOperation::Process
        || R.Rule.IsNone() || !GetObject(R.Source,Source) || (!HasSimulation(R.Source) && !HasControl(R.Source)) || !CanPublishFacts() || R.NodeId<0) return {};
    if ((R.Operation==EGuLiHybridResourceOperation::Extract && (R.NodeId<=0 || R.Target.IsValid() || !R.MiningToken.IsValid()))
        || (R.Operation!=EGuLiHybridResourceOperation::Extract && (R.NodeId!=0 || !R.Target.IsValid() || R.MiningToken.IsValid()))) return {};
    TransactionRequest V; V.Header.Event=ToCore(R.Event.IsValid()?R.Event:FGuid::NewGuid()); V.Header.Object=ToCore(R.Source);
    V.Header.At=V.StartedAt=GetObjectLogicalTick(R.Source); V.RootEvent=V.Header.Event;
    V.Kind=static_cast<TransactionKind>(static_cast<uint8>(R.Operation)+8); V.Faction=ToCore(Source.Team);
    V.Target=ToCore(R.Target); V.RuleId=TCHAR_TO_UTF8(*R.Rule.ToString()); V.ResourceNode=static_cast<uint32>(R.NodeId);
    V.MiningToken=ToCore(R.MiningToken);
    const Id Accepted=HasSimulation(R.Source,true)?State->Client->SubmitAdmitted(V):State->Client->SubmitControlled(V,true); const FGuid Event=ToUE(Accepted);
    if (Accepted.Valid() && Callback && !State->TransactionCallbacks.Contains(Event)) State->TransactionCallbacks.Add(Event,MoveTemp(Callback));
    return Event;
}
FGuid UGuLiHybridWorldSubsystem::SubmitAdmittedWork(const FGuLiHybridObjectKey& Work)
{
    if (!State->Client || !CanPublishFacts() || !HasSimulation(Work)) return {};
    const auto I=State->Client->Objects().find(ToCore(Work));FrozenWorkRecipe Recipe;
    if (I==State->Client->Objects().end() || !DecodeFrozenWork(I->second,Recipe)) return {};
    auto Request=Recipe.Request;Request.Header.Event=State->Client->ContinuationEvent(I->second);
    Request.Header.ResourceGroup={};Request.Header.ResourceEpoch=0;
    Request.Header.Object=ToCore(Work);Request.Header.At=GetObjectLogicalTick(Work);
    return ToUE(State->Client->Submit(std::move(Request),{}));
}
bool UGuLiHybridWorldSubsystem::GetMiningLease(const FGuLiHybridObjectKey& Source,FGuLiHybridMiningLease& Lease) const
{
    const auto Found=State->MiningView.Leases.find(ToCore(Source));
    if (Found==State->MiningView.Leases.end()) return false; Lease=ToMiningLease(Found->second); return true;
}
bool UGuLiHybridWorldSubsystem::GetMiningReservationSummary(int32& Leases,uint64& Revision) const
{
    Leases=static_cast<int32>(State->MiningView.Leases.size()); Revision=State->MiningView.Version;
    return State->MiningView.Configured;
}
bool UGuLiHybridWorldSubsystem::IsMiningPositionAvailable(const FGuLiHybridObjectKey& Source,int32 Cluster,uint8 Sample) const
{
    if (!State->MiningView.Configured) return false;
    const auto Key=ToCore(Source);
    for (const auto& [Owner,Lease]:State->MiningView.Leases)
        if (Owner!=Key && Lease.Cluster==static_cast<uint32>(Cluster) && Lease.Sample==Sample) return false;
    return true;
}
bool UGuLiHybridWorldSubsystem::IsMiningNodeAvailable(const FGuLiHybridObjectKey& Source,uint32 Node) const
{
    if (!State->MiningView.Configured) return false;
    const auto Key=ToCore(Source);
    for (const auto& [Owner,Lease]:State->MiningView.Leases) if (Owner!=Key && Lease.Node==Node) return false;
    return true;
}
FGuid UGuLiHybridWorldSubsystem::SubmitMiningOperation(const FGuLiHybridMiningRequest& Request)
{ return SubmitMiningOperationNative(Request,{}); }
FGuid UGuLiHybridWorldSubsystem::SubmitMiningOperationNative(const FGuLiHybridMiningRequest& R,TFunction<void(const FGuLiHybridTransactionResult&)> Callback)
{
    FGuLiHybridObjectState Source;
    if (!State->Client || R.Rule.IsNone()
        || R.Operation==EGuLiHybridMiningOperation::None || R.Operation>EGuLiHybridMiningOperation::Release
        || !GetObject(R.Source,Source) || Source.LifeState!=static_cast<uint8>(Life::Alive)
        || (!HasSimulation(R.Source) && !HasControl(R.Source)) || !CanPublishFacts()) return {};
    if (R.Operation==EGuLiHybridMiningOperation::ClaimPosition)
    {
        if (R.ClusterId<=0 || R.Sample>=MiningSamplesPerCluster || R.Task<=0 || R.Task>MAX_uint32
            || R.Token.IsValid() || R.NodeId) return {};
    }
    else if (!R.Token.IsValid() || R.ClusterId || R.Sample || R.Task
        || (R.Operation==EGuLiHybridMiningOperation::ClaimNode ? R.NodeId<=0 : R.NodeId!=0)) return {};
    TransactionRequest V; V.Header.Event=ToCore(R.Event.IsValid()?R.Event:FGuid::NewGuid()); V.Header.Object=ToCore(R.Source);
    V.Header.At=V.StartedAt=GetObjectLogicalTick(R.Source); V.RootEvent=V.Header.Event; V.Faction=ToCore(Source.Team);
    V.Kind=R.Operation==EGuLiHybridMiningOperation::ClaimPosition ? TransactionKind::ClaimMiningPosition
        : R.Operation==EGuLiHybridMiningOperation::ClaimNode ? TransactionKind::ClaimMiningNode : TransactionKind::ReleaseMiningPosition;
    V.RuleId=TCHAR_TO_UTF8(*R.Rule.ToString()); V.MiningCluster=static_cast<uint32>(R.ClusterId); V.MiningSample=R.Sample;
    V.MiningTask=static_cast<uint32>(R.Task); V.MiningToken=ToCore(R.Token); V.ResourceNode=static_cast<uint32>(R.NodeId);
    const Id Accepted=HasSimulation(R.Source,true)?State->Client->Submit(V,{}):State->Client->SubmitControlled(V,false); const FGuid Event=ToUE(Accepted);
    if (Accepted.Valid() && Callback && !State->TransactionCallbacks.Contains(Event)) State->TransactionCallbacks.Add(Event,MoveTemp(Callback));
    return Event;
}
FGuid UGuLiHybridWorldSubsystem::SubmitEconomy(const FGuLiHybridEconomyRequest& Request)
{ return SubmitEconomyNative(Request,{}); }
FGuid UGuLiHybridWorldSubsystem::SubmitEconomyNative(const FGuLiHybridEconomyRequest& R,TFunction<void(const FGuLiHybridTransactionResult&)> Callback)
{
    FGuLiHybridObjectState Source;
    if (!State->Client || R.Operation==EGuLiHybridEconomyOperation::None || R.Operation>EGuLiHybridEconomyOperation::Refund
        || R.Rule.IsNone() || !GetObject(R.Source,Source) || (!HasSimulation(R.Source,true) && !HasControl(R.Source))
        || IsBattlePaused() || (R.Operation!=EGuLiHybridEconomyOperation::Credit && R.ReservationRequest<=0)) return {};
    if ((R.Operation==EGuLiHybridEconomyOperation::Commit || R.Operation==EGuLiHybridEconomyOperation::Refund)
        && !R.ReservationToken.IsValid()) return {};
    TransactionRequest V; V.Header.Event=ToCore(R.Event.IsValid() ? R.Event : FGuid::NewGuid());
    V.Header.Object=ToCore(R.Source); V.Header.At=GetObjectLogicalTick(R.Source); V.StartedAt=V.Header.At;
    V.Kind=static_cast<TransactionKind>(R.Operation); V.RootEvent=V.Header.Event; V.RuleId=TCHAR_TO_UTF8(*R.Rule.ToString());
    V.Faction=ToCore(Source.Team); V.ReservationKey={DeriveEconomyAccount(ToCore(R.Source)),static_cast<uint64>(FMath::Max<int64>(0,R.ReservationRequest))};
    V.ReservationToken=ToCore(R.ReservationToken);
    const Id Accepted=!HasSimulation(R.Source,true)?State->Client->SubmitControlled(V,CanAdmitWork(V.Kind)):
        CanAdmitWork(V.Kind)?State->Client->SubmitAdmitted(V):State->Client->Submit(V,{});
    const FGuid Event=ToUE(Accepted);
    if (Accepted.Valid() && Callback && !State->TransactionCallbacks.Contains(Event)) State->TransactionCallbacks.Add(Event,MoveTemp(Callback));
    return ToUE(Accepted);
}
FGuid UGuLiHybridWorldSubsystem::SubmitObjectOperation(const FGuLiHybridObjectRequest& Request)
{ return SubmitObjectOperationNative(Request,{}); }
FGuid UGuLiHybridWorldSubsystem::SubmitObjectOperationNative(const FGuLiHybridObjectRequest& R,TFunction<void(const FGuLiHybridTransactionResult&)> Callback)
{
    FGuLiHybridObjectState Source;
    if (!State->Client || R.Operation==EGuLiHybridObjectOperation::None || R.Operation>EGuLiHybridObjectOperation::Relation
        || R.Rule.IsNone() || !GetObject(R.Source,Source) || (!HasSimulation(R.Source,true) && !HasControl(R.Source)) || IsBattlePaused()
        || R.Location.ContainsNaN() || !FMath::IsFinite(R.FacingYaw) || R.ExpectedRelationRevision<0 || R.ReservationRequest<0
        || (R.Operation==EGuLiHybridObjectOperation::Spawn && !R.Target.IsValid())
        || (R.ReservationRequest>0)!=R.ReservationToken.IsValid()) return {};
    TransactionRequest V; V.Header.Event=ToCore(R.Event.IsValid() ? R.Event : FGuid::NewGuid());
    V.Header.Object=ToCore(R.Source); V.Header.At=GetObjectLogicalTick(R.Source); V.StartedAt=V.Header.At;
    if (R.OriginalAt.IsSet()) V.Header.At=V.StartedAt=R.OriginalAt.GetValue();
    V.RootEvent=V.Header.Event; V.Kind=static_cast<TransactionKind>(static_cast<uint8>(R.Operation)+4);
    if (R.StartedAt) { if (R.StartedAt>V.Header.At) return {}; V.StartedAt=R.StartedAt; }
    V.RuleId=TCHAR_TO_UTF8(*R.Rule.ToString()); V.Faction=ToCore(Source.Affiliation);
    V.Target=ToCore(R.Target.IsValid() ? R.Target : R.Source); V.TargetX=R.Location.X; V.TargetY=R.Location.Y; V.TargetZ=R.Location.Z;
    V.FacingYaw=R.FacingYaw; V.Related=ToCore(R.Related); V.ExpectedRelationVersion=static_cast<uint64>(R.ExpectedRelationRevision);
    V.ReservationKey={DeriveEconomyAccount(ToCore(R.Source)),static_cast<uint64>(R.ReservationRequest)};
    V.ReservationToken=ToCore(R.ReservationToken);
    if (!R.CapabilityPayload.IsEmpty()) V.CapabilityPayload.assign(R.CapabilityPayload.GetData(),R.CapabilityPayload.GetData()+R.CapabilityPayload.Num());
    const Id Accepted=HasSimulation(R.Source,true)?State->Client->Submit(V,{},R.OriginalAt.IsSet()):State->Client->SubmitControlled(V,false); const FGuid Event=ToUE(Accepted);
    if (Accepted.Valid() && Callback && !State->TransactionCallbacks.Contains(Event)) State->TransactionCallbacks.Add(Event,MoveTemp(Callback));
    return Event;
}
bool UGuLiHybridWorldSubsystem::FindTransactionResult(const FGuid& Event,FGuLiHybridTransactionResult& Out) const
{
    const auto* Found=State->TransactionResults.Find(Event); if (!Found) return false; Out=*Found; return true;
}
bool UGuLiHybridWorldSubsystem::IsRequestPending(FGuid Event) const
{
    if (!State->Client) return false;const auto Requests=State->Client->PendingTransactions();
    return std::any_of(Requests.begin(),Requests.end(),[&](const auto& R){return R.Header.Event==ToCore(Event);});
}
bool UGuLiHybridWorldSubsystem::FindCommandResult(FGuid Event,FGuLiHybridCommandResult& Out) const
{
    if (!State->Client) return false;const auto Result=State->Client->CommandResult(ToCore(Event));
    if (!Result) return false;Out.Event=Event;Out.LogicalTick=Result->At;
    for (const auto& M:Result->Members) Out.Members.Add({ToUE(M.Object),M.ControlEpoch,M.OwnerEpoch,M.Accepted});return true;
}
FGuid UGuLiHybridWorldSubsystem::SubmitCapability(const FGuLiHybridObjectKey& Source,FGuid Event,FName Rule,uint64 ExpectedRevision,
    TConstArrayView<uint8> Payload,EGuLiTeam Faction)
{
    if (!State->Client || !Event.IsValid() || Rule.IsNone() || !CanPublishFacts() || (!HasSimulation(Source) && !HasControl(Source))
        || Payload.Num()>int32(MaximumCapabilityBytes) || Faction>EGuLiTeam::Blue) return {};
    TransactionRequest R;R.Header.Object=ToCore(Source);R.Header.Event=ToCore(Event);R.Header.At=GetObjectLogicalTick(Source);
    R.Target=R.Header.Object;R.RootEvent=R.Header.Event;R.StartedAt=R.Header.At;R.ExpectedRelationVersion=ExpectedRevision;
    R.Kind=TransactionKind::Capability;R.Faction=ToCore(Faction);R.RuleId=TCHAR_TO_UTF8(*Rule.ToString());
    if (!Payload.IsEmpty()) R.CapabilityPayload.assign(Payload.GetData(),Payload.GetData()+Payload.Num());
    return ToUE(HasSimulation(Source)?State->Client->Submit(R,{}):State->Client->SubmitControlled(R,false));
}
FGuid UGuLiHybridWorldSubsystem::SubmitUpgrade(const FGuLiHybridUpgradeRequest& Request)
{return SubmitUpgradeNative(Request,{});}
FGuid UGuLiHybridWorldSubsystem::SubmitUpgradeNative(const FGuLiHybridUpgradeRequest& R,TFunction<void(const FGuLiHybridTransactionResult&)> Callback)
{
    FGuLiHybridObjectState Source;
    if (!State->Client || R.Rule.IsNone() || R.Node.IsNone() || R.ExpectedSelectionRevision<0
        || !GetObject(R.Source,Source) || Source.LifeState!=static_cast<uint8>(Life::Alive)
        || !HasControl(R.Source) || !CanPublishFacts()) return {};
    const auto* Rule=State->UpgradeRules.Find(R.Rule);
    if (!Rule || Rule->SourceDefinition!=Source.Definition.ToString() || Rule->TargetDefinition!=Rule->SourceDefinition) return {};
    TransactionRequest V;V.Header.Event=ToCore(R.Event.IsValid()?R.Event:FGuid::NewGuid());V.Header.Object=ToCore(R.Source);
    V.Header.At=V.StartedAt=GetObjectLogicalTick(R.Source);V.RootEvent=V.Header.Event;V.Target=V.Header.Object;
    V.Kind=TransactionKind::Upgrade;V.Faction=ToCore(Source.Team);V.RuleId=TCHAR_TO_UTF8(*R.Rule.ToString().ToLower());
    V.UpgradeNode=TCHAR_TO_UTF8(*R.Node.ToString().ToLower());V.ExpectedUpgradeVersion=static_cast<uint64>(R.ExpectedSelectionRevision);
    ReportDirectOperation({R.Source});
    const Id Accepted=State->Client->SubmitControlled(V,true);const FGuid Event=ToUE(Accepted);
    if (Accepted.Valid() && Callback && !State->TransactionCallbacks.Contains(Event)) State->TransactionCallbacks.Add(Event,MoveTemp(Callback));
    return Event;
}
FGuLiResourceAmounts UGuLiHybridWorldSubsystem::GetTeamBalance(EGuLiTeam Team) const
{
    return Team==EGuLiTeam::Red ? ToUE(State->Red) : Team==EGuLiTeam::Blue ? ToUE(State->Blue) : FGuLiResourceAmounts{};
}
uint64 UGuLiHybridWorldSubsystem::GetResourceRequestEpoch(const FGuLiHybridObjectKey& Source) const
{
    if (!State->Client) return 0;
    auto K=ToCore(Source);std::set<ObjectKey> Seen;
    const auto& Objects=State->Client->Objects();const auto& Groups=State->Client->ResourceGroups();
    while (K.Valid() && Seen.size()<64 && Seen.insert(K).second)
    {
        const auto O=Objects.find(K);if (O==Objects.end()) break;
        if (const auto G=Groups.find(O->second.Grant.OwnershipGroup);G!=Groups.end()) return G->second.RequestEpoch;
        K=O->second.Parent;
    }
    return 0;
}
bool UGuLiHybridWorldSubsystem::GetCommittedReservation(const FGuLiHybridObjectKey& Source,uint64 Request,FGuid& Token) const
{
    if (!State->Client) return false;
    const auto& Pending=State->Client->NumericState().Reservations;
    const auto I=Pending.find({DeriveEconomyAccount(ToCore(Source)),Request});
    if (I==Pending.end()) return false;Token=ToUE(I->second.Token);return true;
}
void UGuLiHybridWorldSubsystem::HandlePreparedCommit(const PreparedCommit& Prepared)
{
    if (!State->Client) return;
    if (Prepared.Result.Status==CommitStatus::Interrupted) State->HeldCommits.erase(Prepared.Result.Event);
    if (Prepared.Result.Status!=CommitStatus::Interrupted && State->Session.IsValid() && State->Session->IsReviewReceiptHoldArmed())
    {
        Message M;M.Header.Kind=MessageKind::TransactionResult;M.Header.Event=Prepared.Result.Event;M.Header.Object=Prepared.Result.StateDomain;M.Body=Prepared.Encoding;
        if (State->Session->HoldReviewLocalReceipt(ToUE(M))) {State->HeldCommits[Prepared.Result.Event]=Prepared;return;}
    }
    ApplyPreparedCommit(Prepared);
}
void UGuLiHybridWorldSubsystem::HandleReleasedCommit(const FGuLiHybridMessage& MessageValue)
{
    const auto I=State->HeldCommits.find(ToCore(MessageValue.Event));if (I==State->HeldCommits.end()) return;
    auto Prepared=std::move(I->second);State->HeldCommits.erase(I);ApplyPreparedCommit(Prepared);
}
void UGuLiHybridWorldSubsystem::ApplyPreparedCommit(const PreparedCommit& Prepared)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_MainReceipt);
    const auto& Result=Prepared.Result;
    if (!State->Client||!State->Client->IsCurrentResourceResult(Result)) return;
    const bool bLocalRejected=Prepared.LocalRejection||Result.Status==CommitStatus::Interrupted;
    if (CapturesGameplayEvidence())
    {
        if (bLocalRejected)
        {
            // Worker admission failure is local delivery feedback, never an
            // authoritative numeric settlement for cross-client comparison.
            if (State->LocalSubmissionEvidence.size()>=4096) State->LocalSubmissionEvidence.pop_front();
            auto Evidence=MakeCommitEvidence(++State->LocalSubmissionEvidenceSequence,Result,Prepared.ResultHash,Prepared.DecisionHash);
            Evidence->AsObject()->SetStringField(TEXT("reason"),UTF8_TO_TCHAR(Prepared.LocalRejectionReason.c_str()));
            Evidence->AsObject()->SetBoolField(TEXT("local_submission_only"),true);
            State->LocalSubmissionEvidence.push_back(MoveTemp(Evidence));
        }
        else
        {
            if (State->ResultEvidence.size()>=4096) State->ResultEvidence.pop_front();
            State->ResultEvidence.push_back(MakeCommitEvidence(++State->ResultEvidenceSequence,Result,Prepared.ResultHash,Prepared.DecisionHash));
        }
    }

        if (Result.Kind==TransactionKind::Damage && Result.Status!=CommitStatus::Deferred && Result.Status!=CommitStatus::Paused)
        {
            const FGuid Event=ToUE(Result.Event); FGuLiDamageCommitResult Out;
            Out.Status=Result.Status==CommitStatus::Committed ? EGuLiDamageCommitStatus::Committed
                : Result.Status==CommitStatus::Duplicate ? EGuLiDamageCommitStatus::Duplicate : EGuLiDamageCommitStatus::RejectedByAdapter;
            Out.AppliedDamage=Result.Damage.Applied; Out.AbsorbedDamage=Result.Damage.Absorbed; Out.RemainingHealth=Result.Vitals.Value;
            Out.bKilled=Result.Damage.Killed; Out.DeathEventId=ToUE(Result.DeathEvent); Out.RewardEventId=ToUE(Result.RewardEvent);
            Out.CommitOrdinal=Result.StateVersion;
            if (!State->DamageResults.Contains(Event))
            {
                if (State->DamageResultOrder.Num()>=16384)
                {
                    {State->DamageResults.Remove(State->DamageResultOrder[State->DamageResultHead]);State->DamageResultOrder[State->DamageResultHead]=Event;State->DamageResultHead=(State->DamageResultHead+1)%16384;}
                }
                else State->DamageResultOrder.Add(Event);
            }
            State->DamageResults.Add(Event,Out);
        }
        if (Result.Kind!=TransactionKind::Damage && Result.Status!=CommitStatus::Deferred && Result.Status!=CommitStatus::Paused
            && (bLocalRejected || Result.StateVersion || Result.Status==CommitStatus::Committed || Result.Status==CommitStatus::Duplicate))
        {
            FGuLiHybridTransactionResult Out; Out.Event=ToUE(Result.Event);
            const bool RejectedAdmission=Result.Kind==TransactionKind::AdmitWork
                && Result.Status!=CommitStatus::Committed && Result.Status!=CommitStatus::Duplicate && Result.WorkEvent.Valid();
            if (RejectedAdmission) Out.Event=ToUE(Result.WorkEvent);
            Out.Status=static_cast<EGuLiHybridTransactionStatus>(Result.Status); Out.Team=ToUE(Result.Faction);
            Out.Balance=ToUE(Result.Balance); Out.ReservationToken=ToUE(Result.Reserved.Token); Out.ReservedCost=ToUE(Result.Reserved.Cost);
            Out.LedgerRevision=static_cast<int64>(Result.StateVersion);
            Out.bBusinessEffects=Out.IsCommitted() && true;
            Out.Target=ToUE(Result.Target);
            Out.MiningRevision=static_cast<int64>(Result.MiningRevision);
            if (IsMiningTransaction(Result.Kind))
            {
                Out.MiningOperation=static_cast<EGuLiHybridMiningOperation>(static_cast<uint8>(MiningOperation(Result.Kind))+1);
                Out.MiningStatus=static_cast<EGuLiHybridMiningStatus>(Result.Mining.Status); Out.MiningLease=ToMiningLease(Result.Mining.Lease);
            }
            if (IsObjectTransaction(Result.Kind)) Out.ObjectOperation=static_cast<EGuLiHybridObjectOperation>(static_cast<uint8>(Result.Kind)-4);
            if (IsResourceTransaction(Result.Kind))
            {
                Out.ResourceOperation=static_cast<EGuLiHybridResourceOperation>(static_cast<uint8>(Result.Kind)-8);
                Out.ResourceSource=ToUE(Result.Resource.Source); Out.ResourceNode=static_cast<int32>(Result.Resource.Node);
                Out.RemainingRaw=Result.Resource.Remaining; Out.Cargo=ToUE(Result.Resource.Cargo); Out.RawAmount=ToUE(Result.Resource.RawAmount);
                Out.Credited=ToUE(Result.Resource.Credit); Out.Beneficiary=ToUE(Result.Resource.Beneficiary);
                Out.DeliveryEvent=ToUE(Result.Resource.Delivery); Out.ResourceRevision=static_cast<int64>(Result.Resource.Version);
            }
            if (RejectedAdmission && IsResourceTransaction(Result.WorkOperation))
                Out.ResourceOperation=static_cast<EGuLiHybridResourceOperation>(static_cast<uint8>(Result.WorkOperation)-8);
            if (!Result.Objects.empty())
            {
                const auto& Object=Result.Objects.front(); State->DirectoryKeys.insert(Object.Grant.Object);
                Out.LifeState=static_cast<uint8>(Object.Grant.State); Out.bCompleted=Object.Completed;
                Out.bSupplied=Object.Supplied; Out.bPhased=Object.Phased; Out.Parent=ToUE(Object.Parent);
                Out.RelationRevision=static_cast<int64>(Object.RelationVersion);
            }
            TFunction<void(const FGuLiHybridTransactionResult&)> Callback;
            if (!State->TransactionResults.Contains(Out.Event))
            {
                if (State->TransactionResultOrder.Num()>=16384)
                {
                    {State->TransactionResults.Remove(State->TransactionResultOrder[State->TransactionResultHead]);State->TransactionResultOrder[State->TransactionResultHead]=Out.Event;State->TransactionResultHead=(State->TransactionResultHead+1)%16384;}
                }
                else State->TransactionResultOrder.Add(Out.Event);
            }
            State->TransactionResults.Add(Out.Event,Out);
            if (auto* Found=State->TransactionCallbacks.Find(Out.Event)) { Callback=MoveTemp(*Found); State->TransactionCallbacks.Remove(Out.Event); }
            if (Callback) Callback(Out);
            {
                std::set<ObjectKey> Keys{Result.StateDomain,Result.Target,Result.Resource.Source,Result.Mining.Source};
                for (const auto& O:Result.Objects) {Keys.insert(O.Grant.Object);Keys.insert(O.Parent);}
                for (const auto& O:Result.Births) {Keys.insert(O.Grant.Object);Keys.insert(O.Parent);}
                if (Result.FinalizedWork) {Keys.insert(Result.FinalizedWork->Grant.Object);Keys.insert(Result.FinalizedWork->Parent);}
                // A listener can unregister or replace itself during delivery.
                TArray<TSharedPtr<FState::FReceiptListener>> Listeners;
                for (const auto K:Keys) if (const auto I=State->ObjectReceiptListeners.find(K);I!=State->ObjectReceiptListeners.end()) Listeners.Append(I->second);
                TSet<UObject*> Delivered;
                for (const auto& L:Listeners) if (L->Active && L->Owner.IsValid() && !Delivered.Contains(L->Owner.Get()))
                {Delivered.Add(L->Owner.Get());L->Callback(Out);}
            }
            OnTransactionCompleted.Broadcast(Out); // Public Blueprint listeners remain compatible.
        }

}
bool UGuLiHybridWorldSubsystem::SubscribeObjectReceipt(UObject* Listener,const FGuLiHybridObjectKey& Key,TFunction<void(const FGuLiHybridTransactionResult&)> Callback)
{
    if (!Listener||!Key.IsValid()||!Callback) return false;
    UnsubscribeObjectReceipt(Listener);auto L=MakeShared<FState::FReceiptListener>();L->Owner=Listener;L->Callback=MoveTemp(Callback);
    State->ObjectReceiptListeners[ToCore(Key)].Add(MoveTemp(L));return true;
}
void UGuLiHybridWorldSubsystem::UnsubscribeObjectReceipt(UObject* Listener)
{
    for (auto I=State->ObjectReceiptListeners.begin();I!=State->ObjectReceiptListeners.end();)
    {
        I->second.RemoveAll([&](const auto& L){if (!L->Owner.IsValid()||L->Owner.Get()==Listener) {L->Active=false;return true;}return false;});
        if (I->second.IsEmpty()) I=State->ObjectReceiptListeners.erase(I);else ++I;
    }
}
void UGuLiHybridWorldSubsystem::ApplyQueryDelta(const ClientQueryDelta& Delta)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_MainResourceDelta);
    const auto& N=State->Client->NumericState();const auto& R=N.WorldResources().Snapshot();const auto& M=N.MiningPositions().Snapshot();
    State->Red=N.TeamEconomy().Balance(Team::Red);State->Blue=N.TeamEconomy().Balance(Team::Blue);
    const bool ResourceChanged=Delta.ResourceSeed||Delta.OwnersChanged||!Delta.Remaining.empty();
    if (Delta.ResourceSeed) State->ResourceView.Remaining.clear();
    for (const auto& [K,V]:Delta.Remaining) State->ResourceView.Remaining[K]=V;
    if (Delta.OwnersChanged) State->ResourceView.Owners=R.Owners;
    State->ResourceView.Configured=R.Configured;
    if (ResourceChanged) ++State->ResourceView.Version;
    if (Delta.MiningChanged)
    {
        State->MiningView.Configured=M.Configured;++State->MiningView.Version;
        for (const auto& [K,V]:Delta.Leases) State->MiningView.Leases[K]=V;
        for (const auto K:Delta.RemovedLeases) State->MiningView.Leases.erase(K);
    }
    // All related projections are visible before any dependent listener runs.
    if (Delta.ResourceSeed||Delta.OwnersChanged) OnResourceDirectoryChanged.Broadcast();
    else for (const auto& [K,V]:Delta.Remaining) OnResourceNodeChanged.Broadcast(K,V);
    if (Delta.MiningChanged) OnMiningReservationsChanged.Broadcast();
}
TSharedRef<FJsonObject> UGuLiHybridWorldSubsystem::GetTransactionCaptureSnapshot() const
{
    auto Async=MakeShared<FJsonObject>();const auto A=State->Client?State->Client->Statistics():AsyncClientStats{};
    Async->SetNumberField(TEXT("input_bytes"),double(A.InputBytes));Async->SetNumberField(TEXT("output_admitted_bytes"),double(A.OutputBytes));
    Async->SetNumberField(TEXT("result_reserved_bytes"),double(A.ResultReservedBytes));Async->SetNumberField(TEXT("input_peak"),double(A.InputPeak));Async->SetNumberField(TEXT("output_peak"),double(A.OutputPeak));
    Async->SetNumberField(TEXT("input_rejected"),double(A.InputRejected));Async->SetNumberField(TEXT("result_backpressure"),double(A.ResultBackpressure));
    Async->SetNumberField(TEXT("queue_wait_ms"),A.LastQueueMilliseconds);Async->SetNumberField(TEXT("queue_wait_peak_ms"),A.MaximumQueueMilliseconds);
    Async->SetNumberField(TEXT("worker_ms"),A.WorkerMilliseconds);Async->SetNumberField(TEXT("main_apply_ms"),A.MainApplyMilliseconds);
    Async->SetNumberField(TEXT("control_bytes"),double(A.ControlBytes));Async->SetNumberField(TEXT("control_peak"),double(A.ControlPeak));
    Async->SetNumberField(TEXT("full_object_copies"),double(State->FullObjectCopies));Async->SetNumberField(TEXT("hud_full_object_copies"),double(State->HUDFullObjectCopies));
    Async->SetNumberField(TEXT("inputs"),double(A.Inputs));Async->SetNumberField(TEXT("applications"),double(A.Applications));
    Async->SetNumberField(TEXT("logical_steps"),double(A.SimulationSteps));Async->SetNumberField(TEXT("simulation_ms"),A.SimulationMilliseconds);
    Async->SetBoolField(TEXT("capture_yield_enabled"),A.CaptureYieldEnabled);
    Async->SetBoolField(TEXT("fast_pack_cache_key_enabled"),A.FastPackCacheKeyEnabled);
    Async->SetBoolField(TEXT("native_peer_send_enabled"),NativePeerSend.GetValueOnGameThread()!=0);
    Async->SetBoolField(TEXT("native_peer_receive_enabled"),State->Session.IsValid() && State->Session->IsNativePeerReceiveEnabled());
    Async->SetBoolField(TEXT("reuse_payload_compressor_enabled"),State->bReusePayloadCompressor);
    Async->SetBoolField(TEXT("fast_small_payload_compression_enabled"),State->bFastSmallPayloadCompression);
    if (const auto* Actors=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>())
    {
        Async->SetBoolField(TEXT("entity_layout_cache_enabled"),Actors->IsEntityLayoutCacheEnabled());
        Async->SetNumberField(TEXT("entity_layout_definitions"),Actors->GetEntityCapabilityDefinitionCount());
        Async->SetNumberField(TEXT("entity_layout_build_ms"),Actors->GetEntityCapabilityIndexBuildMilliseconds());
    }
    Async->SetNumberField(TEXT("capture_yield_advances"),double(A.CaptureYieldAdvances));
    Async->SetStringField(TEXT("input_byte_basis"),TEXT("Admission budget estimate, including retained gameplay snapshots; not socket bytes or measured heap allocation"));
    Async->SetNumberField(TEXT("queued_input_bytes"),double(A.QueuedInputBytes));
    Async->SetNumberField(TEXT("gameplay_admitted_bytes"),double(A.GameplayAdmittedBytes));
    Async->SetNumberField(TEXT("oldest_input_ms"),A.OldestInputMilliseconds);Async->SetNumberField(TEXT("oldest_control_ms"),A.OldestControlMilliseconds);
    auto Kinds=MakeShared<FJsonObject>();
    for (size_t I=0;I<A.InputKinds.size();++I)
    {
        const auto& K=A.InputKinds[I];if (!K.Accepted && !K.Rejected) continue;auto V=MakeShared<FJsonObject>();
        V->SetNumberField(TEXT("accepted"),double(K.Accepted));V->SetNumberField(TEXT("processed"),double(K.Processed));V->SetNumberField(TEXT("rejected"),double(K.Rejected));
        V->SetNumberField(TEXT("accepted_bytes"),double(K.AcceptedBytes));V->SetNumberField(TEXT("processed_bytes"),double(K.ProcessedBytes));
        V->SetNumberField(TEXT("queued_bytes"),double(K.QueuedBytes));V->SetNumberField(TEXT("queued_count"),double(K.QueuedCount));
        V->SetNumberField(TEXT("work_ms"),K.WorkMilliseconds);V->SetNumberField(TEXT("wait_ms"),K.WaitMilliseconds);V->SetNumberField(TEXT("max_wait_ms"),K.MaximumWaitMilliseconds);
        Kinds->SetObjectField(UTF8_TO_TCHAR(AsyncInputNames[I]),V);
    }
    Async->SetObjectField(TEXT("input_kinds"),Kinds);
    auto Rules=MakeShared<FJsonObject>();for (const auto& [K,N]:A.RequestRules) Rules->SetNumberField(UTF8_TO_TCHAR(K.c_str()),double(N));
    auto Receipts=MakeShared<FJsonObject>();for (const auto& [K,N]:A.ReceiptKinds)
        Receipts->SetNumberField(FString::Printf(TEXT("%u/%u"),unsigned(K>>8),unsigned(K&255)),double(N));
    Async->SetObjectField(TEXT("request_kind_rule"),Rules);Async->SetObjectField(TEXT("receipt_kind_status"),Receipts);
    if (State->Client)
        for (const auto& [Name,Count]:State->Client->WorkCounts())
            Async->SetNumberField(TEXT("core_")+FString(UTF8_TO_TCHAR(Name.c_str())),double(Count));
    return Async;
}
bool UGuLiHybridWorldSubsystem::GetCommittedInventory(const FGuLiHybridObjectKey& Key,FGuLiHybridInventoryView& Out,
    uint64 AfterCargoVersion,uint64 AfterProcessingVersion) const
{
    Out={};if (!State->Client || !Key.IsValid()) return false;
    const auto& Resources=State->Client->NumericState().WorldResources().Snapshot();const auto K=ToCore(Key);
    if (const auto I=Resources.Miners.find(K);I!=Resources.Miners.end())
    {Out.CargoVersion=I->second.Version;Out.NextExtraction=I->second.NextExtraction;if (Out.CargoVersion>AfterCargoVersion) Out.Cargo=ToUE(I->second.Cargo);}
    if (const auto I=Resources.Factories.find(K);I!=Resources.Factories.end())
    {
        Out.ProcessingVersion=I->second.Version;
        Out.NextProcessing=I->second.NextProcessing;
        if (Out.ProcessingVersion>AfterProcessingVersion)
            for (const auto& Entry:I->second.Queue)
                Out.Queue.Add({ToUE(Entry.Event),ToUE(Entry.Source),ToUE(Entry.Beneficiary),ToUE(Entry.Amount)});
    }
    return Out.CargoVersion || Out.ProcessingVersion;
}
void UGuLiHybridWorldSubsystem::HandleControl(const FGuLiHybridMessage& Value)
{
    auto* Session=State->Session.Get();if (!Session) return;const Message V=ToCore(Value);Reader R(V.Body);
    if (V.Header.Kind==MessageKind::Welcome)
    {
        // Reconnecting the coordinator never replaces the running local world.
        if (State->Client) {State->DirectoryComplete=false;State->SelectionDirty=true;return;}
        auto* Battle=GetWorld()->GetGameState<AGuLiBattleGameState>();
        if (!Battle || !Battle->ApplyHybridMatchIdentity(Session->GetRoomId(),V.Header.Epoch)) {Suspend(TEXT("Hybrid match identity unavailable"));return;}
        const TWeakObjectPtr<UGuLiHybridWorldSubsystem> Weak(this);
        State->bReusePayloadCompressor=ResolveCompressorReuse();
        State->bFastSmallPayloadCompression=ResolveFastSmallPayloadCompression();
        // Only TransmitWorker invokes this packer. AsyncClientWorld joins that
        // worker before releasing the callback and its private workspace.
        const auto Compressor=State->bReusePayloadCompressor
            ?std::make_shared<GuLiHybridPeerPayload::FDeflateContext>():nullptr;
        const bool FastCompression=State->bFastSmallPayloadCompression;
        const auto FastCompressor=State->bReusePayloadCompressor&&FastCompression
            ?std::make_shared<GuLiHybridPeerPayload::FDeflateContext>(Z_BEST_SPEED):nullptr;
        State->Client=std::make_unique<AsyncClientWorld>(ToCore(Session->GetRoomId()),ToCore(Session->GetLocalPlayerId()),V.Header.Epoch,
            [Weak](const ObjectRecord& O)->std::unique_ptr<IObjectAdapter>
            {
                auto* Self=Weak.Get();if (!Self) return {};
                auto* F=Self->State->Factories.Find(static_cast<EGuLiHybridObjectKind>(O.Grant.Object.Kind));
                if (!F || !F->Owner.IsValid()) return {};auto A=F->Create(ToUE(O));
                return A && IsValid(A->LifetimeOwner())?std::make_unique<FObjectBridge>(MoveTemp(A)):nullptr;
            },[Weak](Id To,const Message& M,const Bytes& Packed,bool bPacked)
            {
                auto* Self=Weak.Get();auto* S=Self?Self->State->Session.Get():nullptr;
                bool Accepted=false;
                if (S)
                {
                    if (!To.Valid()) Accepted=S->SendControl(ToUE(M));
                    else if (NativePeerSend.GetValueOnGameThread()!=0)
                        Accepted=S->SendPeerPreparedNative(ToUE(To),M,View(Packed),bPacked,M.ReferenceBodyBytes);
                    else Accepted=S->SendPeerPrepared(ToUE(To),ToUE(M),View(Packed),bPacked,M.ReferenceBodyBytes);
                }
                if (Accepted && M.Header.Kind==MessageKind::HandoffState) Self->State->HandoffQueuedBytes+=M.Body.size();
                return Accepted;
            },[Compressor,FastCompressor,FastCompression](Message& M,Bytes& Packed)
            {
                // Keep large private-cut packing and all admission limits intact.
                if (FastCompression && M.Body.size()<=65536)
                {
                    GuLiHybridPeerPayload::FDeflateContext Fresh(Z_BEST_SPEED);
                    return GuLiHybridPeerPayload::Pack(M,Packed,&M.ReferenceBodyBytes,FastCompressor?FastCompressor.get():&Fresh);
                }
                return GuLiHybridPeerPayload::Pack(M,Packed,&M.ReferenceBodyBytes,Compressor.get());
            },CapturesGameplayEvidence(),
            []{FPlatformProcess::SetThreadName(TEXT("GuLiTransactions"));UE::Trace::ThreadRegister(TEXT("GuLiTransactions"),FPlatformTLS::GetCurrentThreadId(),0);},FPlatformTime::Seconds(),
            AsyncExecutionOptions{UGuLiHotspotOptions::Enabled(GetWorld(),EGuLiHotspot::GameplayWorker),
                UGuLiHotspotOptions::Enabled(GetWorld(),EGuLiHotspot::ParallelGameplay)
                    ?AcquireGuLiGameplayComputePool(UGuLiNetworkSettings::Resolve(GetWorld()).GameplayWorkers):nullptr,
                ResolveCaptureYield(),ResolveFastPackCacheKey(),
                [](const char* Stage,Id Event,std::size_t Value)
                {
                    if (GuLiMoveLatency::IsEnabled())
                        UE_LOG(LogGuLiStrike,Display,TEXT("HybridCommandTiming stage=%s clock=%.6f thread=%u event=%s value=%llu"),
                            UTF8_TO_TCHAR(Stage),FPlatformTime::Seconds(),FPlatformTLS::GetCurrentThreadId(),
                            *ToUE(Event).ToString(EGuidFormats::Digits),static_cast<uint64>(Value));
                }});
        State->Client->SetHandoffAdmission([Weak](Id Target)
        {const auto* Self=Weak.Get();const auto* S=Self?Self->State->Session.Get():nullptr;return S && S->CanQueueHandoff(ToUE(Target));});
        State->Client->SetTimingObserver([Weak](const char* Name,double Value)
        {
            const auto* Self=Weak.Get();auto* W=Self?Self->GetWorld():nullptr;
            if (auto* Perf=W?W->GetSubsystem<UGuLiPerformanceSubsystem>():nullptr;Perf&&Perf->IsCapturing()) Perf->RecordSample(FName(UTF8_TO_TCHAR(Name)),Value);
        });
        State->Client->SetQueryObserver([Weak](const ClientQueryDelta& Delta)
        {if (auto* Self=Weak.Get()) Self->ApplyQueryDelta(Delta);});
        State->Client->SetRecordObserver([Weak](const ObjectRecord& Record)
        {if (auto* Self=Weak.Get()) Self->OnObjectChanged.Broadcast(ToUE(Record.Grant.Object));});
        std::string Error;if (!State->Client->Configure(State->Manifest,Error)) {Suspend(UTF8_TO_TCHAR(Error.c_str()));return;}
        State->Client->SetBootstrapMaterializationDeferred(true);
        State->Client->SetCommitCallback([Weak](const PreparedCommit& Result)
        {if (auto* Self=Weak.Get()) Self->HandlePreparedCommit(Result);});
        State->Client->SetCommandReceiptCallback([Weak](const CommandReceipt& Receipt)
        {
            if (auto* Self=Weak.Get())
            {
                FGuLiHybridCommandResult Out;Out.Event=ToUE(Receipt.Event);Out.LogicalTick=Receipt.At;
                for (const auto& M:Receipt.Members) Out.Members.Add({ToUE(M.Object),M.ControlEpoch,M.OwnerEpoch,M.Accepted});
                Self->OnCommandCompleted.Broadcast(Out);
            }
        });
        State->Client->SetOwnershipCommitCallback([Weak](const std::vector<std::pair<ObjectHandoff,Authority>>& Batch,bool Good)
        {
            auto* Self=Weak.Get();if (!Self) return;
            if (!Good) {if (!Self->State->Client->Error().empty()) Self->Suspend(UTF8_TO_TCHAR(Self->State->Client->Error().c_str()));return;}
        for (const auto& [T,Grant]:Batch)
        {
            auto Audit=MakeShared<FJsonObject>();Audit->SetStringField(TEXT("transfer"),FString::Printf(TEXT("%llu"),T.IdValue));
            Audit->SetStringField(TEXT("object"),ToUE(T.Object.StableId).ToString(EGuidFormats::Digits));
            Audit->SetStringField(TEXT("source"),ToUE(T.Source).ToString(EGuidFormats::Digits));Audit->SetStringField(TEXT("target"),ToUE(T.Target).ToString(EGuidFormats::Digits));
            Audit->SetNumberField(TEXT("reason"),uint8(T.Reason));
            Audit->SetStringField(TEXT("frozen_state"),FString::Printf(TEXT("%llu"),T.FrozenState));Audit->SetStringField(TEXT("installed_state"),FString::Printf(TEXT("%llu"),T.InstalledState));
            Audit->SetNumberField(TEXT("owner_version"),double(Grant.OwnerEpoch));Audit->SetNumberField(TEXT("logical_age"),double(Self->State->Client->ObjectNow(T.Object)));
            TArray<TSharedPtr<FJsonValue>> Capabilities;
            if (const auto O=Self->State->Client->Objects().find(T.Object);O!=Self->State->Client->Objects().end())
                for (const auto& [Name,Part]:O->second.Capabilities) Capabilities.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Name.c_str())));
            Audit->SetArrayField(TEXT("capabilities"),Capabilities);
            if (Self->State->HandoffEvidence.Num()>=4096) Self->State->HandoffEvidence.RemoveAt(0,1,EAllowShrinking::No);
            Self->State->HandoffEvidence.Add(MakeShared<FJsonValueObject>(Audit));
            ++Self->State->HandoffCount;if (const auto* Start=Self->State->HandoffStarted.Find(ToUE(T.Object.StableId))) Self->State->LastHandoffMilliseconds=(FPlatformTime::Seconds()-*Start)*1000;
            Self->State->Handoffs.erase(T.Object);Self->State->HandoffStarted.Remove(ToUE(T.Object.StableId));
        }
        Self->OnOwnershipStateApplied.Broadcast();
        });
        for (const auto& [Name,Adapter]:State->SharedStateSystems)
            if (!State->Client->RegisterSystem(TCHAR_TO_UTF8(*Name.ToString()),std::make_unique<FSharedStateBridge>(Name,Adapter)))
            {Suspend(TEXT("Object handoff capability registration failed"));return;}
        State->DirectoryStarted=FPlatformTime::Seconds();return;
    }
    if (!State->Client) return;
    switch (V.Header.Kind)
    {
    case MessageKind::Roster:
    {
        std::map<Id,Member> Members;
        for (const auto& U:Session->GetMembers())
        {
            Member M;M.Player=ToCore(U.Player);M.Faction=ToCore(U.Team);M.Seat=U.Role==EGuLiCommanderRole::Ground?Role::Ground:
                U.Role==EGuLiCommanderRole::Air?Role::Air:U.Role==EGuLiCommanderRole::Observer?Role::Observer:Role::Commander;
            M.SeatVersion=U.SeatVersion;M.OrdinalSlot=U.OrdinalSlot;M.Online=U.bOnline;M.Ready=U.bReady;Members.emplace(M.Player,M);
        }
        State->Client->SetMembers(std::move(Members));break;
    }
    case MessageKind::RegisterObject:
    {
        Authority Grant;std::string Definition;uint32 Ordinal;ObjectKey Parent;
        if (!ReadAuthority(R,Grant) || !R.Text(Definition) || !R.U32(Ordinal) || !R.Key(Parent) || !R.Done() || Grant.Object!=V.Header.Object)
        {Suspend(TEXT("Invalid ownership directory"));return;}
        State->DirectoryKeys.insert(Grant.Object);State->CoordinatorDirectoryKeys.insert(Grant.Object);
        State->RequiredDirectoryValidated=false;
        if (Grant.State==Life::Destroyed)
        {
            State->RequiredDirectoryKeys.erase(Grant.Object);
            // Registration carries the stable definition, ordinal and parent
            // even when no peer retains private custody of this retired life.
            // Keep only families with target identities; retired transient
            // effects do not grow this reference-only cache.
            if (const auto* Catalog=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>())
            {
                const auto& Manifest=Catalog->GetManifest();const FString DefinitionName=UTF8_TO_TCHAR(Definition.c_str());
                const auto Family=Manifest.Strings.FindRef(TEXT("adapter/definition/")+DefinitionName);
                if (!Family.IsEmpty() && Manifest.Numbers.FindRef(TEXT("adapter/family/")+Family+TEXT("/target_binding"))==1)
                {
                    ObjectRecord Retired;Retired.Grant=Grant;Retired.Definition=Definition;Retired.Ordinal=Ordinal;Retired.Parent=Parent;
                    State->RetiredTargetMetadata.Add(ToUE(Grant.Object),ToUE(Retired));
                }
            }
        }
        else State->RequiredDirectoryKeys.insert(Grant.Object);
        const Id Local=ToCore(Session->GetLocalPlayerId());bool RecoverLocal=false;
        if (const auto Old=State->Client->Objects().find(Grant.Object);Old!=State->Client->Objects().end())
        {
            const auto& A=Old->second.Grant;
            const bool OldOnline=Session->GetMembers().ContainsByPredicate([&](const FGuLiHybridMember& M)
            {return M.bOnline && ToCore(M.Player)==A.SimOwner;});
            RecoverLocal=A.SimOwner.Valid() && A.SimOwner!=Local && !OldOnline
                && Grant.SimOwner==Local && Grant.OwnerEpoch>A.OwnerEpoch;
        }
        if (State->MembershipAbrupt && State->MembershipVersion)
            if (const auto Old=State->Client->Objects().find(Grant.Object);Old!=State->Client->Objects().end()
                && HasPrivateCustody(Old->second.Grant,State->MembershipPlayer)) State->LostOwnerObjects.insert(Grant.Object);
        const bool Recover=RecoverLocal || State->LostOwnerObjects.contains(Grant.Object);
        if (!State->Client->InstallDirectoryRecord(Grant,Definition,Ordinal,Parent,Recover))
        {Suspend(TEXT("Ownership directory input queue is full"));return;}
        State->LostOwnerObjects.erase(Grant.Object);
        break;
    }
    case MessageKind::DirectoryComplete:
    {
        uint8 Phase;uint32 Count;std::vector<ObjectHandoff> Active;
        if (!R.U32(State->ExpectedObjects) || !R.U8(Phase) || Phase>uint8(RoomPhase::Aborted) || !R.U32(Count) || Count>100000)
        {Suspend(TEXT("Invalid ownership directory completion"));return;}
        for (uint32 I=0;I<Count;++I) {Bytes B;ObjectHandoff T;if (!R.Blob(B) || !DecodeHandoff(B,T)) {Suspend(TEXT("Invalid active handoff"));return;}Active.push_back(T);}
        if (!R.Done()) return;
        State->Client->ReconcileHandoffs(Active);State->Handoffs=State->Client->ActiveHandoffs();
        for (auto It=State->HandoffStarted.CreateIterator();It;++It)
        {
            bool ActiveObject=false;for (const auto& [K,T]:State->Handoffs) if (ToUE(K.StableId)==It.Key()) {ActiveObject=true;break;}
            if (!ActiveObject) It.RemoveCurrent();
        }
        // An existing service reconnect keeps its running adapters. A fresh
        // client in MembershipChanging must wait for private restore context.
        State->DirectoryResumePending=Phase!=uint8(RoomPhase::MembershipChanging);
        State->DirectoryComplete=true;TryWorldReady();break;
    }
    case MessageKind::AssignmentOffer:
    {
        Authority Grant;std::string Definition;uint32 Ordinal;ObjectKey Parent;Id Owner;Revision Version;
        if (!ReadAuthority(R,Grant) || !R.Text(Definition) || !R.U32(Ordinal) || !R.Key(Parent) || !R.Identity(Owner) || !R.U64(Version) || !R.Done()) return;
        if (Owner!=ToCore(Session->GetLocalPlayerId())) break;
        const auto O=State->Client->Objects().find(Grant.Object);
        State->RecordAssignment(Grant.Object,Version,O==State->Client->Objects().end()?TEXT("offer-unknown"):TEXT("offer-received"));
        if (O==State->Client->Objects().end()) break;
        if (O->second.Grant.OwnerEpoch<Version)
            State->Offers[Grant.Object]=Version;
        break;
    }
    case MessageKind::HandoffPrepare:
    case MessageKind::HandoffFreeze:
    case MessageKind::HandoffCancel:
    {
        Bytes B;ObjectHandoff T;if (!R.Blob(B) || !DecodeHandoff(B,T)) return;
        if (V.Header.Kind==MessageKind::HandoffPrepare)
        {
            State->Handoffs[T.Object]=T;State->HandoffStarted.Add(ToUE(T.Object.StableId),FPlatformTime::Seconds());
            if (!State->Client->PrepareHandoff(T) && T.Target==ToCore(Session->GetLocalPlayerId()))
            {Writer W;W.U64(T.IdValue);Message M;M.Header.Kind=MessageKind::HandoffCancel;M.Header.Object=T.Object;M.Body=W.Take();Session->SendControl(ToUE(M));}
        }
        else if (V.Header.Kind==MessageKind::HandoffFreeze)
        {if (T.Source==ToCore(Session->GetLocalPlayerId())) State->Client->FreezeHandoff(T);}
        else
        {
            State->Client->CancelHandoff(T);
            const auto Old=State->Handoffs.find(T.Object);
            if (Old!=State->Handoffs.end() && Old->second.IdValue==T.IdValue)
            {State->Handoffs.erase(Old);State->HandoffStarted.Remove(ToUE(T.Object.StableId));}
        }break;
    }
    case MessageKind::HandoffCommit:
    {
        uint32 Count;if (!R.U32(Count) || !Count || Count>100000) return;
        std::vector<std::pair<ObjectHandoff,Authority>> Batch;Batch.reserve(Count);
        for (uint32 Index=0;Index<Count;++Index)
        {
            Bytes B;ObjectHandoff T;Authority A;
            if (!R.Blob(B) || !DecodeHandoff(B,T) || !ReadAuthority(R,A)) return;
            Batch.emplace_back(T,A);
        }
        if (!R.Done() || !State->Client->CommitHandoffs(Batch)) return;
        break;
    }
    case MessageKind::ResourceGroups:
    {
        uint32 Count;std::map<Id,ResourceGroupState> Groups;
        if (!R.U32(Count)||Count>4096) return;
        for (uint32 I=0;I<Count;++I) {ResourceGroupState G;if (!ReadResourceGroup(R,G)||!Groups.emplace(G.Group,G).second) return;}
        if (!R.Done()) return;
        std::vector<PreparedCommit> Interrupted;
        for (auto I=State->HeldCommits.begin();I!=State->HeldCommits.end();)
        {
            const auto G=Groups.find(I->second.Result.ResourceGroup);
            if (G==Groups.end() || G->second.RequestEpoch<=I->second.Result.ResourceEpoch) {++I;continue;}
            PreparedCommit P;P.Result.Event=I->first;P.Result.Kind=I->second.Result.Kind;
            P.Result.StateDomain=I->second.Result.StateDomain;P.Result.Target=I->second.Result.Target;P.Result.Faction=I->second.Result.Faction;
            P.Result.Status=CommitStatus::Interrupted;P.Result.ResourceGroup=G->first;P.Result.ResourceEpoch=G->second.RequestEpoch;
            P.LocalRejection=true;P.LocalRejectionReason="Connection interrupted; restored snapshot is authoritative";
            P.Encoding=EncodeCommit(P.Result);Interrupted.push_back(std::move(P));I=State->HeldCommits.erase(I);
        }
        State->Client->SetResourceGroups(std::move(Groups));
        for (const auto& P:Interrupted) ApplyPreparedCommit(P);
        break;
    }
    case MessageKind::MembershipBegin:
    {
        Revision Version;uint8 Op,Abrupt;Id Player;
        if (!R.U64(Version) || !R.U8(Op) || Op>1 || !R.Identity(Player) || !R.U8(Abrupt) || Abrupt>1 || !R.Done()) return;
        if (!State->MembershipVersion) State->MembershipPausedAt=FPlatformTime::Seconds();
        State->Client->SetMembershipPaused(true);
        if (Op==0 && Player==ToCore(Session->GetLocalPlayerId())) State->Client->SetBootstrapMaterializationDeferred(true);
        if (State->MembershipVersion!=Version)
        {
            State->MembershipVersion=Version;State->MembershipPlayer=Player;State->MembershipJoin=Op==0;State->MembershipAbrupt=Abrupt!=0;
            State->BootstrapSent=false;State->MembershipAcked=false;State->BootstrapSources.clear();State->BootstrapParts.clear();State->BootstrapCounts.clear();
            State->BootstrapStates.clear();State->BootstrapSourceBytes.clear();State->BootstrapStateBytes=0;
            State->BootstrapSourceStagedBytes.clear();State->BootstrapStagedBytes=State->BootstrapStagedPeakBytes=State->BootstrapCapturedStagedBytes=0;
            State->BootstrapStatePeakBytes=State->BootstrapCapturedBytes=0;
            State->BootstrapOutgoing.clear();State->BootstrapSendPart=0;
            State->BootstrapCaptureKeys.clear();State->BootstrapCaptureQueue.clear();State->BootstrapCaptureStarted=State->BootstrapCaptureComplete=false;
            State->BootstrapPreparedParts.clear();State->BootstrapReadSource.reset();State->BootstrapApplyStage=0;State->BootstrapStageStarted=0;
            State->BootstrapApplied=false;
            State->BootstrapCaptureObjects=0;State->BootstrapCaptureOffset=0;State->BootstrapCaptureSplits=0;State->BootstrapBlocked.Reset();
            State->BootstrapDirectorySeconds=State->BootstrapImportSeconds=State->BootstrapCompletedSeconds=0;
        }
        break;
    }
    case MessageKind::MembershipCommit:
        if (!State->MembershipVersion && !State->Client->IsPaused()) break;
        if (State->MembershipPausedAt>0)
        {
            const double Delay=FPlatformTime::Seconds()-State->MembershipPausedAt;
            for (auto& [K,At]:State->DeselectedAt) At+=Delay;for (auto& [K,At]:State->OperatedAt) At+=Delay;
            State->MembershipPausedAt=0;
        }
        State->LostOwnerObjects.clear();
        State->BootstrapOutgoing.clear();
        State->BootstrapCaptureKeys.clear();State->BootstrapCaptureQueue.clear();State->BootstrapPreparedParts.clear();State->BootstrapReadSource.reset();
        State->Client->FinishBootstrap();State->Client->SetMembershipPaused(false);State->MembershipVersion=0;
        OnOwnershipStateApplied.Broadcast();break;
    case MessageKind::MatchResult:
    {std::string Reason;if (R.Text(Reason)) Suspend(UTF8_TO_TCHAR(Reason.c_str()));break;}
    default:break;
    }
}
void UGuLiHybridWorldSubsystem::PumpMembership()
{
    auto* S=State->Session.Get();if (!S || !State->Client) return;
    for (auto I=State->Offers.begin();I!=State->Offers.end();)
    {
        const auto Version=I->second;
        const auto O=State->Client->Objects().find(I->first);
        if (O==State->Client->Objects().end()) {++I;continue;}
        // A grant or a controller update can overtake an adapter retry. Prepare
        // the current record, never a stale copy of an earlier authority grant.
        if (O->second.Grant.OwnerEpoch>=Version)
        {State->RecordAssignment(I->first,Version,TEXT("offer-already-versioned"));I=State->Offers.erase(I);continue;}
        // Terminal replicas require no live simulation handles. Their exact
        // numeric life remains terminal when the owner version advances.
        if (O->second.Grant.State<Life::Dead && !State->Client->Install(O->second,false)) {++I;continue;}
        Writer W;W.U64(Version);Message M;M.Header.Kind=MessageKind::AssignmentReady;M.Header.Object=I->first;M.Body=W.Take();
        if (S->SendControl(ToUE(M))) I=State->Offers.erase(I);else ++I;
    }
    if (State->SelectionDirty && State->Ready)
    {
        Writer W;W.U64(State->SelectionVersion?State->SelectionVersion:++State->SelectionVersion);W.U32(static_cast<uint32>(State->Selected.size()));
        for (const auto& K:State->Selected) W.Key(K);Message M;M.Header.Kind=MessageKind::SelectionActivity;M.Body=W.Take();
        State->SelectionDirty=!S->SendControl(ToUE(M));
    }
    if (!State->PendingOperations.empty() && State->Ready)
    {
        Writer W;W.U32(static_cast<uint32>(State->PendingOperations.size()));for (const auto& K:State->PendingOperations) W.Key(K);
        Message M;M.Header.Kind=MessageKind::DirectOperation;M.Body=W.Take();if (S->SendControl(ToUE(M))) State->PendingOperations.clear();
    }
    // A crash can follow publication of a committed birth but precede its
    // metadata declaration. Reannounce the actual received numeric record
    // only from the current custodian of its parent/retired parent chain.
    const double RecoveryNow=FPlatformTime::Seconds();
    if (State->Ready && RecoveryNow>=State->NextBirthRecovery)
    {
        State->NextBirthRecovery=RecoveryNow+1;const Id Local=ToCore(S->GetLocalPlayerId());uint32 Published=0;
        for (const auto& [K,O]:State->Client->Objects())
        {
            if (Published>=32) break;const auto& A=O.Grant;
            if (State->CoordinatorDirectoryKeys.contains(K) || !A.SourceLease.Valid() || !A.SimOwner.Valid() || A.SimOwner==Local) continue;
            const auto* Producer=S->GetMembers().FindByPredicate([&](const FGuLiHybridMember& M){return ToCore(M.Player)==A.SimOwner;});
            if (!Producer || Producer->bOnline) continue;
            std::set<ObjectKey> Seen;ObjectKey Parent=O.Parent;bool Authorized=false;
            while (Parent.Valid() && Seen.size()<64 && Seen.insert(Parent).second)
            {
                if (!State->CoordinatorDirectoryKeys.contains(Parent)) break;
                const auto P=State->Client->Objects().find(Parent);if (P==State->Client->Objects().end()) break;
                const auto& Grant=P->second.Grant;
                if (HasPrivateCustody(Grant,Local) || (Grant.State==Life::Destroyed && Grant.SimOwner==Local)) {Authorized=true;break;}
                if (Grant.State!=Life::Destroyed) break;Parent=P->second.Parent;
            }
            if (!Authorized) continue;
            Writer W;WriteAuthority(W,A);W.Text(O.Definition);W.U32(O.Ordinal);W.Key(O.Parent);
            Message M;M.Header.Kind=MessageKind::RegisterObject;M.Header.Object=K;M.Body=W.Take();
            if (S->SendControl(ToUE(M))) {++Published;++State->BirthRecoveryAnnouncements;}
        }
    }
    if (!State->MembershipVersion || State->MembershipAcked) return;
    auto Early=MoveTemp(State->EarlyBootstrap);State->EarlyBootstrap.Reset();State->EarlyBootstrapBytes=0;
    for (const auto& V:Early) HandlePeer(V.Key,V.Value);
    const Id Local=ToCore(S->GetLocalPlayerId());
    if (State->MembershipJoin)
    {
        if (State->MembershipPlayer!=Local && !State->BootstrapSent)
        {
            if (!State->BootstrapCaptureStarted)
            {
                for (const auto& [K,O]:State->Client->Objects()) if (HasPrivateCustody(O.Grant,Local)) State->BootstrapCaptureKeys.push_back(K);
                const auto Count=State->BootstrapCaptureKeys.size();State->BootstrapCaptureObjects=uint32(Count);
                // A persistent queue retains the interrupted current part across frames.
                constexpr size_t Batch=256;
                for (size_t Offset=0;Offset<Count;Offset+=Batch) State->BootstrapCaptureQueue.emplace_back(Offset,std::min(Batch,Count-Offset));
                State->BootstrapCaptureStarted=true;State->BootstrapBlocked.Reset();
            }
            while (!State->BootstrapCaptureComplete)
            {
                if (State->BootstrapCaptureQueue.empty())
                {if (State->BootstrapOutgoing.empty()) State->BootstrapOutgoing.emplace_back();State->BootstrapCaptureComplete=true;break;}
                const auto [Offset,Count]=State->BootstrapCaptureQueue.front();State->BootstrapCaptureOffset=uint32(Offset);
                // An already committed terminal application can finish after
                // membership pause and after this cohort was enumerated. Its
                // terminal identity is in the Coordinator directory; it has no
                // private simulation left to capture. Keep every live key and
                // all custody checks, including unexpected ownership changes.
                std::vector<ObjectKey> CurrentKeys;CurrentKeys.reserve(Count);
                for (size_t I=Offset;I<Offset+Count;++I)
                {
                    const auto K=State->BootstrapCaptureKeys[I];const auto O=State->Client->Objects().find(K);
                    if (O==State->Client->Objects().end() || O->second.Grant.State!=Life::Destroyed) CurrentKeys.push_back(K);
                }
                if (CurrentKeys.empty()) {State->BootstrapCaptureQueue.pop_front();continue;}
                ObjectHandoffState Cut;
                const bool Captured=State->Client->CaptureObjects(CurrentKeys,State->MembershipPlayer,0,Cut);
                if (!Captured && State->Client->IsOperationPending()) {State->BootstrapBlocked=TEXT("capture/application-ack");return;}
                const auto& Stored=State->Client->CapturedStagedEncoding();
                if (!Captured || Stored.empty() || Stored.size()>MaxMessageBytes-256)
                {
                    if (Count>1)
                    {
                        State->BootstrapCaptureQueue.pop_front();const size_t Half=Count/2;++State->BootstrapCaptureSplits;
                        State->BootstrapCaptureQueue.emplace_front(Offset+Half,Count-Half);State->BootstrapCaptureQueue.emplace_front(Offset,Half);continue;
                    }
                    const auto K=CurrentKeys.front();const auto O=State->Client->Objects().find(K);
                    State->BootstrapBlocked=FString::Printf(TEXT("capture/private-state-unavailable kind=%u id=%s life=%d owner=%s"),
                        unsigned(K.Kind),UTF8_TO_TCHAR(K.StableId.ToString().c_str()),O==State->Client->Objects().end()?-1:int(O->second.Grant.State),
                        O==State->Client->Objects().end()?TEXT("missing"):UTF8_TO_TCHAR(O->second.Grant.SimOwner.ToString().c_str()));return;
                }
                State->BootstrapCapturedBytes+=State->Client->CapturedEncoding().size();State->BootstrapCapturedStagedBytes+=Stored.size();
                if (State->BootstrapCapturedStagedBytes>64ull*1024*1024) {Suspend(TEXT("Bootstrap source exceeds the existing restore capacity"));return;}
                State->BootstrapOutgoing.push_back(Stored);State->BootstrapCaptureQueue.pop_front();
            }
            while (State->BootstrapSendPart<State->BootstrapOutgoing.size())
            {
                const auto I=State->BootstrapSendPart;
                const auto& Stored=State->BootstrapOutgoing[I];
                if (Stored.size()>MaxMessageBytes-256) {Suspend(TEXT("Invalid staged bootstrap capture"));return;}
                Writer W;W.U64(State->MembershipVersion);W.U32(I);W.U32(static_cast<uint32>(State->BootstrapOutgoing.size()));W.Blob(Stored);
                Message M;M.Header.Kind=MessageKind::BootstrapState;M.Body=W.Take();
                if (!State->Client->SendPeer(State->MembershipPlayer,std::move(M))) {State->BootstrapBlocked=TEXT("send");return;}
                ++State->BootstrapSendPart;
            }
            State->BootstrapBlocked.Reset();State->BootstrapSent=true;return;
        }
        if (State->MembershipPlayer==Local)
        {
            for (const auto& M:S->GetMembers()) if (M.bOnline && M.bReady && M.Player!=S->GetLocalPlayerId())
            {
                const Id Peer=ToCore(M.Player);
                if (!State->BootstrapCounts.contains(Peer) || State->BootstrapParts[Peer].size()!=State->BootstrapCounts.at(Peer)) return;
            }
            if (!State->BootstrapApplied)
            {
                auto& Parts=State->BootstrapPreparedParts;
                if (!State->BootstrapApplyStage)
                {
                auto StagedBySource=State->BootstrapSourceStagedBytes;
                uint64 MetadataBytes=0;
                for (const auto& [Key,Stored]:State->BootstrapStates)
                {
                    Bytes Body;ObjectHandoffState Part;
                    if (!ReadBootstrapBody(Stored,Body) || !DecodeHandoffState(Body,Part)) {Suspend(TEXT("Invalid staged bootstrap restore"));return;}
                    // Only private metadata stays expanded. Numeric histories
                    // are decoded one bounded part at a time during import.
                    Part.Numeric.clear();Part.Numeric.shrink_to_fit();
                    const auto Size=EncodeHandoffState(Part).size();StagedBySource[Key.first]+=Size;MetadataBytes+=Size;
                    if (StagedBySource[Key.first]>64ull*1024*1024)
                    {Suspend(TEXT("Bootstrap metadata exceeds authenticated source capacity"));return;}
                    Parts.push_back(std::move(Part));
                }
                State->BootstrapStagedPeakBytes=FMath::Max(State->BootstrapStagedPeakBytes,State->BootstrapStagedBytes+MetadataBytes);
                State->BootstrapReadSource=std::make_shared<const std::map<std::pair<Id,uint32>,Bytes>>(std::move(State->BootstrapStates));
                State->BootstrapApplyStage=1;State->BootstrapStageStarted=FPlatformTime::Seconds();
                }
                if (State->BootstrapApplyStage==1)
                {
                    if (!State->Client->StageBootstrapDirectory(Parts))
                    {if (!State->Client->IsOperationPending()) Suspend(TEXT("Join object directory is incompatible"));return;}
                    State->BootstrapDirectorySeconds=FPlatformTime::Seconds()-State->BootstrapStageStarted;
                    State->BootstrapApplyStage=2;State->BootstrapStageStarted=FPlatformTime::Seconds();
                }
                const auto ReadNumeric=[SourceState=State->BootstrapReadSource](Id Source,const OwnerState::FragmentVisitor& Visit)
                {
                    for (const auto& [Key,Stored]:*SourceState) if (Key.first==Source)
                    {
                        Bytes Body;ObjectHandoffState Part;
                        if (!ReadBootstrapBody(Stored,Body) || !DecodeHandoffState(Body,Part)) return false;
                        for (const auto& V:Part.Numeric) if (!Visit(V)) return false;
                    }
                    return true;
                };
                if (State->BootstrapApplyStage==2)
                {
                    if (!State->Client->ImportBootstrap(Parts,ReadNumeric))
                    {if (!State->Client->IsOperationPending()) Suspend(TEXT("Join object capability import failed"));return;}
                    State->BootstrapApplyStage=3;
                }
                if (!State->Client->SetBootstrapMaterializationDeferred(false))
                {if (!State->Client->Error().empty()) Suspend(UTF8_TO_TCHAR(State->Client->Error().c_str()));return;}
                State->BootstrapImportSeconds=FPlatformTime::Seconds()-State->BootstrapStageStarted;
                State->BootstrapCompletedSeconds=FPlatformTime::Seconds()-State->MembershipPausedAt;
                if (CapturesGameplayEvidence()) UE_LOG(LogTemp,Display,TEXT("Hybrid bootstrap imported parts=%llu directory=%.3fs import=%.3fs elapsed=%.3fs"),
                    uint64(Parts.size()),State->BootstrapDirectorySeconds,State->BootstrapImportSeconds,State->BootstrapCompletedSeconds);
                State->BootstrapApplied=true;
                State->BootstrapPreparedParts.clear();State->BootstrapReadSource.reset();
            }
            for (const auto& [Peer,Count]:State->BootstrapCounts)
            {
                State->BootstrapSources.insert(Peer);Writer Ack;Ack.U64(State->MembershipVersion);
                Message M;M.Header.Kind=MessageKind::BootstrapRequest;M.Body=Ack.Take();if (!S->SendPeer(ToUE(Peer),ToUE(M))) return;
            }
            State->BootstrapStates.clear();State->BootstrapStateBytes=State->BootstrapStagedBytes=0;
        }
        else if (!State->BootstrapSources.contains(State->MembershipPlayer)) return;
    }
    else if (State->Client->HasResourceRecovery()) return;
    else if (State->MembershipAbrupt)
    {
        // The query proxy already holds the last received pose and values.
        // Rebuild its task/nav handles only when ownership has actually moved.
        for (const auto& K:State->LostOwnerObjects) if (State->Client->HasSimulation(K)) State->Client->RebuildFromReplica(K);
    }
    Writer W;W.U64(State->MembershipVersion);Message M;M.Header.Kind=MessageKind::MembershipReady;M.Body=W.Take();
    State->MembershipAcked=S->SendControl(ToUE(M));
}
bool UGuLiHybridWorldSubsystem::CanAdmitPeer(uint16 Kind,uint64 BodyBytes) const
{
    if (!State->Client) return false;
    if (MessageKind(Kind)==MessageKind::BootstrapRequest || MessageKind(Kind)==MessageKind::BootstrapState) return true;
    return State->Client->CanReceivePeer(BodyBytes);
}
bool UGuLiHybridWorldSubsystem::HandlePeer(const FGuid& Peer,const FGuLiHybridMessage& Value)
{
    if (!State->Client) return false;const auto V=ToCore(Value);const auto Sender=ToCore(Peer);
    if (V.Header.Kind==MessageKind::BootstrapRequest)
    {
        Reader R(V.Body);Revision Version;if (R.U64(Version) && R.Done() && Version==State->MembershipVersion && Sender==State->MembershipPlayer)
            State->BootstrapSources.insert(Sender);return true;
    }
    if (V.Header.Kind==MessageKind::BootstrapState)
    {
        if (!State->MembershipVersion)
        {
            if (State->EarlyBootstrap.Num()<4096 && State->EarlyBootstrapBytes+V.Body.size()<=64*1024*1024)
            {State->EarlyBootstrap.Add({Peer,Value});State->EarlyBootstrapBytes+=V.Body.size();return true;}
            return false;
        }
        Reader R(V.Body);Revision Version;uint32 Index,Count;Bytes Body;
        if (!R.U64(Version) || Version!=State->MembershipVersion || !State->MembershipJoin || !State->Session.IsValid()
            || State->MembershipPlayer!=ToCore(State->Session->GetLocalPlayerId()) || !R.U32(Index) || !R.U32(Count) || !Count || Count>100000 || Index>=Count || !R.Blob(Body) || !R.Done()) return true;
        if (State->BootstrapCounts.contains(Sender) && State->BootstrapCounts.at(Sender)!=Count) return true;
        uint64 ReadySources=0;bool AuthorizedSource=false;
        for (const auto& M:State->Session->GetMembers()) if (M.bOnline && M.bReady && M.Player!=State->Session->GetLocalPlayerId())
        {++ReadySources;AuthorizedSource|=ToCore(M.Player)==Sender;}
        if (!AuthorizedSource || !ReadySources || ReadySources>5) return true;
        if (!State->BootstrapParts[Sender].contains(Index))
        {
            if (!Body.empty())
            {
                ObjectHandoffState Part;
                Bytes ExpandedBody;
                if (Body.size()>MaxMessageBytes-256 || !ReadBootstrapBody(Body,ExpandedBody) || !DecodeHandoffState(ExpandedBody,Part) || Part.Source!=Sender
                    || Part.Target!=ToCore(State->Session->GetLocalPlayerId()) || Part.Transfer)
                {Suspend(TEXT("Invalid bootstrap private state"));return true;}
                const auto ExpandedBytes=ExpandedBody.size();auto Stored=StageBootstrapBody(std::move(Body));
                // Bound resident staging, retaining every history fragment in
                // the existing lossless codec. Private cuts expand at most16MiB;
                // wire parts remain4MiB and authenticated staging64MiB/source.
                constexpr uint64 SourceLimit=64ull*1024*1024;
                if (State->BootstrapSourceStagedBytes[Sender]+Stored.size()>SourceLimit
                    || State->BootstrapStagedBytes+Stored.size()>ReadySources*SourceLimit)
                {Suspend(TEXT("Bootstrap restore exceeds authenticated source capacity"));return true;}
                State->BootstrapSourceBytes[Sender]+=ExpandedBytes;State->BootstrapStateBytes+=ExpandedBytes;
                State->BootstrapSourceStagedBytes[Sender]+=Stored.size();State->BootstrapStagedBytes+=Stored.size();
                State->BootstrapStagedPeakBytes=FMath::Max(State->BootstrapStagedPeakBytes,State->BootstrapStagedBytes);
                State->BootstrapStatePeakBytes=FMath::Max(State->BootstrapStatePeakBytes,State->BootstrapStateBytes);
                State->BootstrapStates[{Sender,Index}]=std::move(Stored);
            }
            State->BootstrapParts[Sender].insert(Index);State->BootstrapCounts[Sender]=Count;
        }
        return true;
    }
    return HandleNativePeer(Peer,V);
}
bool UGuLiHybridWorldSubsystem::HandleNativePeer(const FGuid& Peer,const GuLiNet::Message& V)
{
    if (!State->Client) return false;
    // Bootstrap retains the existing bounded staging and membership path.
    if (V.Header.Kind==MessageKind::BootstrapRequest || V.Header.Kind==MessageKind::BootstrapState)
        return HandlePeer(Peer,ToUE(V));
    // ReceivePeer copies into its own input work item. On rejection the peer
    // channel still retains its message; this borrowed view never escapes.
    const bool Admitted=State->Client->ReceivePeer(ToCore(Peer),V);
    if (Admitted && V.Header.Kind==MessageKind::HandoffState) State->HandoffBytes+=V.Body.size();
    return Admitted;
}
void UGuLiHybridWorldSubsystem::SetCommanderSelection(const TArray<FGuLiHybridObjectKey>& Objects)
{
    if (!State->Client || !State->Session.IsValid()) return;std::set<ObjectKey> Selected;
    for (const auto& K:Objects)
    {
        const auto C=ToCore(K);const auto O=State->Client->Objects().find(C);
        if (O!=State->Client->Objects().end() && O->second.Grant.Policy==OwnershipPolicy::CommanderActivity && State->Client->HasControl(C)) Selected.insert(C);
    }
    if (Selected==State->Selected) return;
    const double Now=FPlatformTime::Seconds();for (const auto& K:State->Selected) if (!Selected.contains(K)) State->DeselectedAt[K]=Now;
    State->Selected=std::move(Selected);Writer W;W.U64(++State->SelectionVersion);W.U32(static_cast<uint32>(State->Selected.size()));
    for (const auto& K:State->Selected) W.Key(K);Message M;M.Header.Kind=MessageKind::SelectionActivity;M.Body=W.Take();State->SelectionDirty=!State->Session->SendControl(ToUE(M));
}
void UGuLiHybridWorldSubsystem::ReportDirectOperation(const TArray<FGuLiHybridObjectKey>& Objects)
{
    if (!State->Client || !State->Session.IsValid()) return;std::set<ObjectKey> Keys;
    for (const auto& U:Objects)
    {
        const auto K=ToCore(U);const auto O=State->Client->Objects().find(K);
        if (O!=State->Client->Objects().end() && O->second.Grant.Policy==OwnershipPolicy::CommanderActivity && State->Client->HasControl(K)) Keys.insert(K);
    }
    if (Keys.empty()) return;Writer W;W.U32(static_cast<uint32>(Keys.size()));
    for (const auto& K:Keys) {W.Key(K);State->OperatedAt[K]=FPlatformTime::Seconds();}
    Message M;M.Header.Kind=MessageKind::DirectOperation;M.Body=W.Take();if (!State->Session->SendControl(ToUE(M))) State->PendingOperations.insert(Keys.begin(),Keys.end());
}
FGuLiHybridOwnershipMetrics UGuLiHybridWorldSubsystem::GetOwnershipMetrics() const
{
    return {State->HandoffCount,State->HandoffBytes,State->HandoffQueuedBytes,State->LastHandoffMilliseconds};
}
