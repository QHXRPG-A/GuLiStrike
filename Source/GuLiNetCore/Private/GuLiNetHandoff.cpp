#include "GuLiNetClient.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <charconv>
#include <chrono>

namespace GuLiNet
{
namespace
{
Revision StateIdentity(std::span<const std::uint8_t> Data)
{const auto Hash=Sha256(Data);Revision Value=0;std::from_chars(Hash.data(),Hash.data()+16,Value,16);return Value?Value:1;}
bool Count(Reader& R,std::uint32_t& N,std::uint32_t Max) {return R.U32(N) && N<=Max;}
}
Bytes EncodeHandoff(const ObjectHandoff& V)
{
    Writer W;W.U32(HandoffVersion);W.U64(V.IdValue);W.Key(V.Object);W.Identity(V.Source);W.Identity(V.Target);W.Identity(V.Group);
    W.U64(V.PreviousOwnerVersion);W.U64(V.NextOwnerVersion);W.U8(static_cast<std::uint8_t>(V.Reason));
    W.F64(V.Deadline);W.F64(V.ReservedMilliseconds);W.U8(V.TargetPrepared?1:0);W.U64(V.FrozenState);W.U64(V.InstalledState);return W.Take();
}
bool DecodeHandoff(std::span<const std::uint8_t> Data,ObjectHandoff& Out)
{
    Reader R(Data);ObjectHandoff V;std::uint32_t Version;std::uint8_t Reason,Ready;
    if (!R.U32(Version) || Version!=HandoffVersion || !R.U64(V.IdValue) || !V.IdValue || !R.Key(V.Object) || !V.Object.Valid()
        || !R.Identity(V.Source) || !V.Source.Valid() || !R.Identity(V.Target) || !V.Target.Valid() || V.Source==V.Target || !R.Identity(V.Group)
        || !R.U64(V.PreviousOwnerVersion) || !R.U64(V.NextOwnerVersion) || !V.PreviousOwnerVersion || V.NextOwnerVersion!=V.PreviousOwnerVersion+1
        || !R.U8(Reason) || Reason>static_cast<std::uint8_t>(HandoffReason::Leave) || !R.F64(V.Deadline) || !R.F64(V.ReservedMilliseconds)
        || !R.U8(Ready) || Ready>1 || !R.U64(V.FrozenState) || !R.U64(V.InstalledState) || !R.Done()) return false;
    V.Reason=static_cast<HandoffReason>(Reason);V.TargetPrepared=Ready!=0;Out=V;return true;
}
Bytes EncodeHandoffState(const ObjectHandoffState& V)
{
    Writer W;W.U32(V.Version);W.Identity(V.Match);W.Identity(V.Source);W.Identity(V.Target);W.U64(V.Transfer);
    W.U32(static_cast<std::uint32_t>(V.Objects.size()));
    for (const auto& O:V.Objects) {WriteObject(W,O.Record);W.U32(O.AdapterVersion);W.Blob(O.State);}
    W.U32(static_cast<std::uint32_t>(V.Systems.size()));
    for (const auto& S:V.Systems)
    {
        W.Text(S.Name);W.Identity(S.Player);W.U32(S.AdapterVersion);W.U32(static_cast<std::uint32_t>(S.Objects.size()));
        for (const auto& K:S.Objects) W.Key(K);W.Blob(S.State);
    }
    W.U32(static_cast<std::uint32_t>(V.Numeric.size()));for (const auto& F:V.Numeric) WriteStateFragment(W,F);
    W.U32(static_cast<std::uint32_t>(V.ExecutedCommands.size()));
    for (const auto& [K,C]:V.ExecutedCommands) {W.Identity(K.first);W.Key(K.second);W.U64(C.At);W.U8(C.Accepted?1:0);}
    W.U32(static_cast<std::uint32_t>(V.IncomingCommands.size()));
    for (const auto& M:V.IncomingCommands) {Bytes Wire;if (!EncodeMessage(M,Wire)) return {};W.Blob(Wire);}
    W.U32(static_cast<std::uint32_t>(V.Pending.size()));
    for (const auto& R:V.Pending) {Bytes Wire;if (!EncodeMessage({R.Header,EncodeTransaction(R)},Wire)) return {};W.Blob(Wire);}
    return W.Take();
}
bool DecodeHandoffState(std::span<const std::uint8_t> Data,ObjectHandoffState& Out)
{
    Reader R(Data);ObjectHandoffState V;std::uint32_t N;std::set<ObjectKey> Keys;
    if (!R.U32(V.Version) || V.Version!=HandoffVersion || !R.Identity(V.Match) || !V.Match.Valid()
        || !R.Identity(V.Source) || !V.Source.Valid() || !R.Identity(V.Target) || !V.Target.Valid()
        || !R.U64(V.Transfer) || !Count(R,N,4096) || !N) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        PrivateObjectState O;
        if (!ReadObject(R,O.Record) || !Keys.insert(O.Record.Grant.Object).second || !HasPrivateCustody(O.Record.Grant,V.Source)
            || !R.U32(O.AdapterVersion) || (O.Record.Materialized && !O.AdapterVersion) || !R.Blob(O.State)) return false;
        V.Objects.push_back(std::move(O));
    }
    if (!Count(R,N,256)) return false;std::set<std::string> Systems;
    for (std::uint32_t I=0;I<N;++I)
    {
        PrivateSystemState S;std::uint32_t CountValue;
        if (!R.Text(S.Name,128) || !Systems.insert(S.Name).second || !R.Identity(S.Player) || S.Player!=V.Source
            || !R.U32(S.AdapterVersion) || !S.AdapterVersion || !Count(R,CountValue,4096)) return false;
        std::set<ObjectKey> Claims;
        for (std::uint32_t J=0;J<CountValue;++J)
        {ObjectKey K;if (!R.Key(K) || !Keys.contains(K) || !Claims.insert(K).second) return false;S.Objects.push_back(K);}
        if (!R.Blob(S.State)) return false;V.Systems.push_back(std::move(S));
    }
    if (!Count(R,N,100000)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {StateFragment F;if (!ReadStateFragment(R,F) || !Keys.contains(F.Key.Object)) return false;V.Numeric.push_back(std::move(F));}
    if (!Count(R,N,100000)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        Id Event;ObjectKey K;CommandOutcome C;std::uint8_t Accepted;
        if (!R.Identity(Event) || !R.Key(K) || !Keys.contains(K) || !R.U64(C.At) || !R.U8(Accepted) || Accepted>1) return false;
        C.Accepted=Accepted!=0;if (!V.ExecutedCommands.emplace(std::pair{Event,K},C).second) return false;
    }
    if (!Count(R,N,16384)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        Bytes Wire;Message M;FrozenCommand C;
        if (!R.Blob(Wire) || !DecodeMessage(Wire,M) || M.Header.Kind!=MessageKind::Command || M.Header.Match!=V.Match || !DecodeCommand(M.Body,C)) return false;
        for (const auto& Member:C.Members) if (!Keys.contains(Member.Object)) return false;
        V.IncomingCommands.push_back(std::move(M));
    }
    if (!Count(R,N,16384)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        Bytes Wire;Message M;TransactionRequest T;
        if (!R.Blob(Wire) || !DecodeMessage(Wire,M) || M.Header.Match!=V.Match || !DecodeTransaction(M.Header,M.Body,T) || !Keys.contains(T.Header.Object)) return false;
        V.Pending.push_back(std::move(T));
    }
    if (!R.Done()) return false;Out=std::move(V);return true;
}
bool ClientWorld::CaptureObjects(std::span<const ObjectKey> Keys,Id Target,Revision Transfer,ObjectHandoffState& Out) const
{
    const auto Started=std::chrono::steady_clock::now();
    ObjectHandoffState S;S.Match=Match;S.Source=Player;S.Target=Target;S.Transfer=Transfer;
    std::set<ObjectKey> Scope(Keys.begin(),Keys.end());std::map<std::string,std::vector<ObjectKey>> Shared;
    if (Scope.empty() || Scope.size()!=Keys.size()) return false;
    for (const auto& K:Keys)
    {
        const auto O=Directory.find(K);if (O==Directory.end() || !HasPrivateCustody(O->second.Grant,Player)) return HandoffIssue("capture/custody");
        if (Numbers.Busy(K)) return HandoffIssue("capture/busy-numeric");
        for (const auto& [E,W]:WorkFlows) for (const auto& M:W.Work.Changes) if (M.After.Key.Object==K) return HandoffIssue("capture/busy-work");
        PrivateObjectState V;V.Record=O->second;
        if (O->second.Materialized)
        {
            const auto A=Adapters.find(K);if (A==Adapters.end()) return false;
            const auto Previous=ActiveObject;ActiveObject=K;const bool Captured=A->second->Capture(V.State);ActiveObject=Previous;
            if (!Captured) return HandoffIssue("capture/object");
            V.AdapterVersion=A->second->Version();
            for (const auto& Name:A->second->RequiredSystems()) Shared[Name].push_back(K);
        }
        S.Objects.push_back(std::move(V));
    }
    for (const auto& [Name,A]:Systems) for (const auto& K:Keys)
        if (A->UsesObject(K) && std::find(Shared[Name].begin(),Shared[Name].end(),K)==Shared[Name].end()) Shared[Name].push_back(K);
    for (const auto& [Name,Objects]:Shared)
    {
        const auto A=Systems.find(Name);if (A==Systems.end()) return false;
        PrivateSystemState Part;Part.Name=Name;Part.Player=Player;Part.Objects=Objects;Part.AdapterVersion=A->second->Version();
        if (!A->second->Capture(Objects,Part.State)) return HandoffIssue("capture/system/"+Name);S.Systems.push_back(std::move(Part));
    }
    S.Numeric=Numbers.Export(Keys);
    for (const auto& [K,C]:Commands) if (Scope.contains(K.second)) S.ExecutedCommands.emplace(K,C);
    auto KeepCommand=[&](const Message& M)
    {
        FrozenCommand C;if (!DecodeCommand(M.Body,C)) return false;
        std::erase_if(C.Members,[&](const auto& Member){return !Scope.contains(Member.Object);});
        if (!C.Members.empty()) S.IncomingCommands.push_back({M.Header,EncodeCommand(C)});return true;
    };
    for (const auto& M:IncomingCommands) if (!KeepCommand(M)) return false;
    for (const auto& K:Keys) if (const auto I=HandoffCommands.find(K);I!=HandoffCommands.end()) for (const auto& M:I->second) if (!KeepCommand(M)) return false;
    for (const auto& [E,P]:Pending) if (Scope.contains(P.Request.Header.Object)) S.Pending.push_back(P.Request);
    const auto Encoded=EncodeHandoffState(S);if (Encoded.empty()) return false;
    if (Transfer)
    {
        MeasureHandoff("capture/total",Encoded.size());
        for (const auto& Object:S.Objects) MeasureHandoff("capture/object-state",Object.State.size());
        for (const auto& Part:S.Systems) MeasureHandoff("capture/system/"+Part.Name,Part.State.size());
        for (const auto& Part:S.Numeric)
            MeasureHandoff("capture/numeric/"+Part.Key.Capability.substr(0,Part.Key.Capability.find('/')),Part.Value.size());
        HandoffSizes["capture/microseconds"]+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-Started).count();
    }
    Out=std::move(S);return true;
}
bool ClientWorld::PrepareHandoff(const ObjectHandoff& T)
{
    const auto O=Directory.find(T.Object);
    if (O==Directory.end()) return HandoffIssue("prepare/missing-object");
    if (O->second.Grant.OwnerEpoch!=T.PreviousOwnerVersion || Numbers.Owner(T.Object)!=T.Source) return HandoffIssue("prepare/owner-version");
    Transfers[T.Object]=T;
    if (T.Target!=Player) return true;
    if (!Install(O->second,false)) return HandoffIssue("prepare/adapter");
    const bool Ready=SendMessage({},MessageKind::HandoffReady,{},T.Object,EncodeHandoff(T));
    if (const auto Early=EarlyHandoffStates.find(T.IdValue);Early!=EarlyHandoffStates.end())
    {const auto State=std::move(Early->second.second);EarlyHandoffStates.erase(Early);return InstallHandoff(T.Source,State) && Ready;}
    return Ready;
}
bool ClientWorld::FreezeHandoff(const ObjectHandoff& T)
{
    if (T.Source!=Player || Numbers.Owner(T.Object)!=Player || Directory.at(T.Object).Grant.OwnerEpoch!=T.PreviousOwnerVersion) return false;
    Transfers[T.Object]=T;FreezeRequested.insert(T.Object);
    for (const auto& [K,O]:Directory) if (O.Parent==T.Object && O.Grant.Policy==OwnershipPolicy::ParentWork
        && O.Grant.State<Life::Dead && HasPrivateCustody(O.Grant,Player))
    {
        const auto Child=Transfers.find(K);
        if (Child==Transfers.end() || Child->second.Target!=T.Target || Child->second.Group!=T.Group) return true;
    }
    if (const auto Sent=OutgoingStates.find(T.Object);Sent!=OutgoingStates.end())
    {
        auto& Current=Transfers.at(T.Object);Current.FrozenState=StateIdentity(Sent->second);
        Writer W;W.U8(0);W.Blob(EncodeHandoff(Current));
        return SendMessage({},MessageKind::HandoffInstalled,{},T.Object,W.Take());
    }
    if (HandoffAdmission && !HandoffAdmission(T.Target)) return true;
    ObjectHandoffState S;const std::array<ObjectKey,1> Keys{T.Object};
    // Capture at the update boundary; a busy transaction keeps the old owner running.
    if (!CaptureObjects(Keys,T.Target,T.IdValue,S)) return true;
    const auto Body=EncodeHandoffState(S);auto& Current=Transfers.at(T.Object);Current.FrozenState=StateIdentity(Body);
    if (!SendMessage(T.Target,MessageKind::HandoffState,{},T.Object,Body)) return true;
    // Queue admission and authority change happen at the same object boundary.
    // Backpressure must leave the old owner running, not freeze unsent state.
    Frozen.insert(T.Object);
    if (const auto A=Adapters.find(T.Object);A!=Adapters.end()) A->second->SetAuthority(Directory.at(T.Object).Grant,false);
    OutgoingStates[T.Object]=Body;FreezeRequested.erase(T.Object);
    Writer W;W.U8(0);W.Blob(EncodeHandoff(Current));return SendMessage({},MessageKind::HandoffInstalled,{},T.Object,W.Take());
}
bool ClientWorld::StageBootstrapDirectory(std::span<const ObjectHandoffState> Parts)
{
    std::set<ObjectKey> Seen;
    for (const auto& S:Parts)
    {
        if (S.Transfer || S.Match!=Match || S.Target!=Player || !Members.contains(S.Source)) return false;
        for (const auto& P:S.Objects)
        {
            const auto K=P.Record.Grant.Object;
            if (!Seen.insert(K).second || !HasPrivateCustody(P.Record.Grant,S.Source)) return false;
            if (const auto O=Directory.find(K);O!=Directory.end() && (O->second.Definition!=P.Record.Definition || O->second.Ordinal!=P.Record.Ordinal)) return false;
        }
    }
    // Declare every object before any adapter links a parent or a capability
    // dependency. A join may split those objects across different senders.
    for (const auto& S:Parts) for (const auto& P:S.Objects)
    {
        if (!Numbers.Install(P.Record)) return false;
        StoreRecord(Numbers.Objects().at(P.Record.Grant.Object));
        if (const auto G=PendingGrants.find(P.Record.Grant.Object);G!=PendingGrants.end())
        {const auto A=G->second;PendingGrants.erase(G);if (!InstallGrant(A)) return false;}
    }
    std::set<ObjectKey> Preparing,Prepared;
    std::function<bool(ObjectKey)> Prepare=[&](ObjectKey K)
    {
        if (Prepared.contains(K)) return true;
        const auto O=Directory.find(K);if (O==Directory.end() || !Preparing.insert(K).second) return false;
        if (O->second.Parent.Valid() && Directory.contains(O->second.Parent) && !Prepare(O->second.Parent)) return false;
        if (O->second.Materialized && !Install(O->second,false)) return false;
        Preparing.erase(K);Prepared.insert(K);return true;
    };
    for (const auto& K:Seen) if (!Prepare(K)) return false;
    return true;
}
bool ClientWorld::ImportObjects(const ObjectHandoffState& S)
{
    if (S.Version!=HandoffVersion || S.Match!=Match || S.Target!=Player || !Members.contains(S.Source)) return false;
    std::vector<ObjectKey> Keys;std::map<std::string,std::vector<ObjectKey>> Required;
    for (const auto& P:S.Objects)
    {
        const auto K=P.Record.Grant.Object;
        if (!HasPrivateCustody(P.Record.Grant,S.Source)) return false;
        if (!Directory.contains(K)) {if (!Install(P.Record,true)) return false;}
        if (Numbers.Owner(K)!=S.Source || !Install(P.Record,false)) return false;Keys.push_back(K);
        if (!P.Record.Materialized) continue;
        const auto& A=Adapters.at(K);if (A->Version()!=P.AdapterVersion || !A->Validate(P.State)) return HandoffIssue("import/object-validation");
        for (const auto& N:A->RequiredSystems()) Required[N].push_back(K);
    }
    for (const auto& [N,Objects]:Required)
    {
        const auto I=std::find_if(S.Systems.begin(),S.Systems.end(),[&](const auto& P){return P.Name==N;});
        if (I==S.Systems.end() || !Systems.contains(N) || Systems.at(N)->Version()!=I->AdapterVersion
            || !Systems.at(N)->Validate({*I},Objects)) return HandoffIssue("import/required-system/"+N);
    }
    for (const auto& Part:S.Systems)
    {
        if (!Systems.contains(Part.Name) || Systems.at(Part.Name)->Version()!=Part.AdapterVersion
            || !Systems.at(Part.Name)->Validate({Part},Part.Objects)) return HandoffIssue("import/system/"+Part.Name);
        for (const auto& K:Part.Objects) if (std::find(Keys.begin(),Keys.end(),K)==Keys.end()) return false;
        if (const auto R=Required.find(Part.Name);R!=Required.end())
            for (const auto& K:R->second) if (std::find(Part.Objects.begin(),Part.Objects.end(),K)==Part.Objects.end()) return false;
        Required[Part.Name]=Part.Objects;
    }
    std::vector<const PrivateObjectState*> Ordered;std::set<ObjectKey> Visiting,Visited;
    std::function<bool(const PrivateObjectState&)> Visit=[&](const PrivateObjectState& P)
    {
        const auto K=P.Record.Grant.Object;if (Visited.contains(K)) return true;if (!Visiting.insert(K).second) return false;
        auto Dependencies=P.Record.Materialized?Adapters.at(K)->RestoreDependencies(P.State):std::vector<ObjectKey>{};
        if (P.Record.Parent.Valid()) Dependencies.push_back(P.Record.Parent);
        for (const auto& D:Dependencies)
        {
            const auto I=std::find_if(S.Objects.begin(),S.Objects.end(),[&](const auto& Q){return Q.Record.Grant.Object==D;});
            if (I!=S.Objects.end()) {if (!Visit(*I)) return false;} else if (!Directory.contains(D)) return false;
        }
        Visiting.erase(K);Visited.insert(K);Ordered.push_back(&P);return true;
    };
    for (const auto& P:S.Objects) if (!Visit(P)) return HandoffIssue("import/dependencies");
    // All definitions and required pieces have been validated before touching entities.
    if (!Numbers.Import(S.Source,S.Numeric,true)) return HandoffIssue("import/numeric");
    for (const auto& P:S.Objects)
    {Frozen.insert(P.Record.Grant.Object);Numbers.SetLogicalAge(P.Record.Grant.Object,P.Record.LogicalAge);}
    RefreshRecords(S.Numeric);
    for (const auto& P:S.Objects) Directory.at(P.Record.Grant.Object).LogicalAge=P.Record.LogicalAge;
    auto AbortImport=[&]()
    {
        // Failed preparation never grants simulation. Remove partially linked private
        // tasks as well, so a later attempt cannot inherit stale navigation or work.
        for (const auto& Part:S.Systems)
            if (S.Transfer) Systems.at(Part.Name)->Restore({Part},Part.Objects,RestoreStage::Detach);
        for (const auto& K:Keys)
        {
            Frozen.erase(K);
            if (const auto A=Adapters.find(K);A!=Adapters.end()) A->second->SetAuthority(Directory.at(K).Grant,false);
        }
        return false;
    };
    for (const auto Stage:{RestoreStage::Detach,RestoreStage::Create,RestoreStage::Link,RestoreStage::Activate})
    {
        for (const auto* P:Ordered) if (P->Record.Materialized)
        {
            const auto Previous=ActiveObject;ActiveObject=P->Record.Grant.Object;
            const bool Restored=S.Transfer?Adapters.at(ActiveObject)->Restore(P->State,Stage):Adapters.at(ActiveObject)->RestoreQuery(P->State,Stage);ActiveObject=Previous;
            if (!Restored) {HandoffIssue("restore/object/"+std::to_string(unsigned(Stage)));return AbortImport();}
        }
        for (const auto& [Name,Objects]:Required)
        {
            const auto& Part=*std::find_if(S.Systems.begin(),S.Systems.end(),[&](const auto& P){return P.Name==Name;});
            if (!(S.Transfer?Systems.at(Name)->Restore({Part},Objects,Stage):Systems.at(Name)->RestoreQuery({Part},Objects,Stage)))
            {HandoffIssue("restore/system/"+Name+"/"+std::to_string(unsigned(Stage)));return AbortImport();}
        }
    }
    for (const auto& P:S.Objects)
    {
        const auto K=P.Record.Grant.Object;Frozen.insert(K);Directory.at(K).LogicalAge=P.Record.LogicalAge;Numbers.SetLogicalAge(K,P.Record.LogicalAge);
        if (const auto A=Adapters.find(K);A!=Adapters.end()) A->second->SetAuthority(Directory.at(K).Grant,false);
    }
    for (const auto& [Key,Value]:S.ExecutedCommands) Commands.insert_or_assign(Key,Value);
    if (S.Transfer)
    {
        for (const auto& M:S.IncomingCommands) IncomingCommands.push_back(M);
        for (const auto& R:S.Pending) Pending.try_emplace(R.Header.Event,PendingTransaction{R,{},0});
    }
    return true;
}
bool ClientWorld::InstallHandoff(Id Sender,const ObjectHandoffState& S)
{
    if (Sender!=S.Source || S.Target!=Player || S.Objects.size()!=1) return false;
    const auto K=S.Objects.front().Record.Grant.Object;const auto T=Transfers.find(K);
    if (T==Transfers.end())
    {
        // Control WebSocket and P2P can arrive in either order. Keep a bounded early delivery.
        if (EarlyHandoffStates.size()>=256 || !Directory.contains(K) || Numbers.Owner(K)!=Sender) return false;
        EarlyHandoffStates.try_emplace(S.Transfer,NetworkTime,S);return true;
    }
    if (T->second.IdValue!=S.Transfer || T->second.Source!=Sender || T->second.Target!=Player) return HandoffIssue("receive/stale-transfer");
    if (const auto Existing=IncomingStates.find(S.Transfer);Existing!=IncomingStates.end()
        && EncodeHandoffState(Existing->second)!=EncodeHandoffState(S)) return false;
    if (!IncomingStates.contains(S.Transfer))
    {
        const auto Started=std::chrono::steady_clock::now();const bool Imported=ImportObjects(S);
        HandoffSizes["import/microseconds"]+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-Started).count();
        ++HandoffSizes["import/attempts"];
        if (!Imported) return false;IncomingStates.emplace(S.Transfer,S);
    }
    T->second.InstalledState=StateIdentity(EncodeHandoffState(S));
    Writer W;W.U8(1);W.Blob(EncodeHandoff(T->second));return SendMessage({},MessageKind::HandoffInstalled,{},K,W.Take());
}
bool ClientWorld::CommitHandoffs(std::span<const std::pair<ObjectHandoff,Authority>> Group)
{
    if (Group.empty() || CommittingOwnershipGroup) return false;
    std::set<ObjectKey> Keys;const Id Identity=Group.front().first.Group;
    for (const auto& [T,A]:Group)
    {
        if (T.Group!=Identity || T.Object!=A.Object || A.OwnerEpoch!=T.NextOwnerVersion
            || !Keys.insert(T.Object).second || !Directory.contains(T.Object)) return false;
        const auto Done=CompletedTransfers.find(T.Object);
        if (T.Target==Player && !IncomingStates.contains(T.IdValue)
            && (Done==CompletedTransfers.end() || Done->second.IdValue!=T.IdValue)) return false;
    }
    CommittingOwnershipGroup=true;
    bool Good=true;
    for (const auto& [T,A]:Group) if (!CommitHandoff(T,A)) {Good=false;break;}
    if (!Good) for (const auto& K:Keys) Frozen.insert(K);
    CommittingOwnershipGroup=false;
    for (const auto& K:Keys) if (const auto A=Adapters.find(K);A!=Adapters.end())
        A->second->SetAuthority(Directory.at(K).Grant,HasSimulation(K));
    return Good;
}
bool ClientWorld::CommitHandoff(const ObjectHandoff& T,const Authority& Grant)
{
    if (T.Object!=Grant.Object || T.NextOwnerVersion!=Grant.OwnerEpoch) return false;
    if (const auto Done=CompletedTransfers.find(T.Object);Done!=CompletedTransfers.end() && Done->second.IdValue==T.IdValue)
        return Directory.at(T.Object).Grant.OwnerEpoch==Grant.OwnerEpoch;
    if (T.Target==Player && !IncomingStates.contains(T.IdValue)) return false;
    if (T.Source==Player)
    {
        const auto I=OutgoingStates.find(T.Object);ObjectHandoffState State;
        if (I!=OutgoingStates.end() && DecodeHandoffState(I->second,State))
            for (const auto& P:State.Systems)
                if (!Systems.at(P.Name)->Restore({P},P.Objects,RestoreStage::Detach)) return false;
    }
    Frozen.erase(T.Object);FreezeRequested.erase(T.Object);OutgoingStates.erase(T.Object);
    if (!InstallGrant(Grant)) return false;Transfers.erase(T.Object);CompletedTransfers[T.Object]=T;
    if (T.Target==Player)
    {
        IncomingStates.erase(T.IdValue);
        // Observers need the imported final numeric state under the new grant,
        // including event decisions that raced with the old owner's stream.
        const std::array<ObjectKey,1> Keys{T.Object};const auto Parts=Numbers.Export(Keys);
        Writer W;W.U32(static_cast<std::uint32_t>(Parts.size()));for (const auto& P:Parts) WriteStateFragment(W,P);
        Broadcast(MessageKind::StateFragments,{},T.Object,W.Take());
        for (auto& [E,P]:Pending) if (P.Request.Header.Object==T.Object)
        {P.Request.Header.Sender=Player;P.Request.Header.OwnerEpoch=Grant.OwnerEpoch;P.Request.Header.ControlEpoch=Grant.ControlEpoch;P.RetryAt=0;}
    }
    else if (T.Source==Player)
    {
        for (auto I=Pending.begin();I!=Pending.end();) if (I->second.Request.Header.Object==T.Object) I=Pending.erase(I);else ++I;
    }
    if (T.Source==Player)
    {
        for (auto I=IncomingCommands.begin();I!=IncomingCommands.end();)
        {
            FrozenCommand C;if (!DecodeCommand(I->Body,C)) {I=IncomingCommands.erase(I);continue;}
            auto One=C;std::erase_if(One.Members,[&](const auto& M){return M.Object!=T.Object;});
            if (!One.Members.empty()) QueueHandoffCommand(T.Object,{I->Header,EncodeCommand(One)});
            std::erase_if(C.Members,[&](const auto& M){return M.Object==T.Object;});
            if (C.Members.empty()) I=IncomingCommands.erase(I);else {I->Body=EncodeCommand(C);++I;}
        }
    }
    const auto Q=HandoffCommands.find(T.Object);
    if (Q!=HandoffCommands.end())
    {
        auto QueuedCommands=std::move(Q->second);HandoffCommands.erase(Q);
        for (auto& M:QueuedCommands)
        {
            FrozenCommand C;if (!DecodeCommand(M.Body,C)) return false;
            for (auto& Member:C.Members) {Member.OwnerEpoch=Grant.OwnerEpoch;Member.ControlEpoch=Grant.ControlEpoch;}
            M.Body=EncodeCommand(C);
            if (T.Target==Player) IncomingCommands.push_back(std::move(M));
            else {Writer W;W.U64(T.IdValue);Bytes Wire;if (!EncodeMessage(M,Wire)) return false;W.Blob(Wire);SendMessage(T.Target,MessageKind::HandoffCommands,C.Event,T.Object,W.Take());}
        }
    }
    return true;
}
void ClientWorld::CancelHandoff(const ObjectHandoff& T)
{
    const auto TransferIt=Transfers.find(T.Object);if (TransferIt==Transfers.end() || TransferIt->second.IdValue!=T.IdValue) return;
    if (T.Target==Player)
    {
        if (const auto S=IncomingStates.find(T.IdValue);S!=IncomingStates.end())
        {
            for (const auto& P:S->second.Systems) Systems.at(P.Name)->Restore({P},P.Objects,RestoreStage::Detach);
            for (const auto& Request:S->second.Pending)
                if (const auto P=Pending.find(Request.Header.Event);P!=Pending.end() && P->second.Request.Header.Sender==T.Source) Pending.erase(P);
        }
    }
    Transfers.erase(TransferIt);Frozen.erase(T.Object);FreezeRequested.erase(T.Object);OutgoingStates.erase(T.Object);IncomingStates.erase(T.IdValue);
    if (const auto A=Adapters.find(T.Object);A!=Adapters.end()) A->second->SetAuthority(Directory.at(T.Object).Grant,HasSimulation(T.Object));
    if (T.Source==Player)
    {
        for (auto I=IncomingCommands.begin();I!=IncomingCommands.end();)
        {
            FrozenCommand C;if (!DecodeCommand(I->Body,C)) {I=IncomingCommands.erase(I);continue;}
            auto One=C;std::erase_if(One.Members,[&](const auto& M){return M.Object!=T.Object;});
            if (!One.Members.empty()) HandoffCommands[T.Object].push_back({I->Header,EncodeCommand(One)});
            std::erase_if(C.Members,[&](const auto& M){return M.Object==T.Object;});
            if (C.Members.empty()) I=IncomingCommands.erase(I);else {I->Body=EncodeCommand(C);++I;}
        }
    }
    const auto Q=HandoffCommands.find(T.Object);
    if (Q!=HandoffCommands.end())
    {
        auto Queue=std::move(Q->second);HandoffCommands.erase(Q);
        for (const auto& M:Queue)
        {
            FrozenCommand C;if (!DecodeCommand(M.Body,C)) continue;
            if (T.Source==Player) IncomingCommands.push_back(M);else if (M.Header.Sender==Player) Route(C);
        }
    }
}
bool ClientWorld::RebuildFromReplica(ObjectKey K)
{
    const auto I=Adapters.find(K);if (I==Adapters.end()) return !Directory.at(K).Materialized;
    I->second->ResetControlInput();I->second->SetAuthority(Directory.at(K).Grant,HasSimulation(K));
    if (const auto C=LatestCommands.find(K);C!=LatestCommands.end())
    {
        auto Command=C->second;auto& M=Command.Members.front();M.OwnerEpoch=Directory.at(K).Grant.OwnerEpoch;M.ControlEpoch=Directory.at(K).Grant.ControlEpoch;
        return I->second->Command(Command,M);
    }
    return true;
}
void ClientWorld::ReconcileHandoffs(std::span<const ObjectHandoff> Active)
{
    const auto Existing=Transfers;
    std::map<Id,std::vector<std::pair<ObjectHandoff,Authority>>> Commits;
    for (const auto& [K,T]:Existing)
    {
        if (std::any_of(Active.begin(),Active.end(),[&](const auto& A){return A.IdValue==T.IdValue && A.Object==K;})) continue;
        const auto O=Directory.find(K);if (O==Directory.end()) continue;
        if (O->second.Grant.OwnerEpoch==T.PreviousOwnerVersion) CancelHandoff(T);
        else if (O->second.Grant.OwnerEpoch==T.NextOwnerVersion)
            Commits[T.Group.Valid()?T.Group:StableId("GuLiSingleHandoff/v1",std::to_string(T.IdValue))].emplace_back(T,O->second.Grant);
    }
    for (const auto& [ID,Batch]:Commits) CommitHandoffs(Batch);
    for (const auto& T:Active) if (!Transfers.contains(T.Object) || Transfers.at(T.Object).IdValue!=T.IdValue) PrepareHandoff(T);
}
void ClientWorld::FinishBootstrap()
{
    for (auto I=Frozen.begin();I!=Frozen.end();) if (!Transfers.contains(*I)) I=Frozen.erase(I);else ++I;
    for (auto& [K,A]:Adapters) A->SetAuthority(Directory.at(K).Grant,HasSimulation(K));
}
}
