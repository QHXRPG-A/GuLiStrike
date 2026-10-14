#include "GuLiNetUpgrade.h"
#include <algorithm>
#include <bit>
#include <functional>
#include <limits>

namespace GuLiNet
{
namespace
{
constexpr std::size_t MaximumIds=512,MaximumAlternatives=4096,MaximumWitnessMerges=1000000;
bool Reject(std::string& Error,std::string Message) {Error=std::move(Message);return false;}
bool Identity(const std::string& S)
{return !S.empty() && S.size()<=192 && std::none_of(S.begin(),S.end(),[](unsigned char C){return C<32 || C==127;});}
template<class T> void UniqueAdd(std::vector<T>& V,const T& Item)
{if (std::find(V.begin(),V.end(),Item)==V.end()) V.push_back(Item);}
bool ValidateExpression(const UpgradeExpression& E,const std::set<std::string>& Nodes,
    const std::set<std::string>& Routes,const std::set<std::string>& Capabilities,bool Acquisition,
    std::vector<std::string>& Dependencies,std::string& Error)
{
    if (E.Nodes.empty()) return E.Root==-1 || Reject(Error,"Empty expression must have no root.");
    if (E.Nodes.size()>MaximumUpgradeExpression) return Reject(Error,"Rule expression exceeds capacity.");
    std::vector<std::uint8_t> Colors(E.Nodes.size());
    std::function<bool(std::int32_t)> Visit=[&](std::int32_t Index)
    {
        if (Index<0 || std::size_t(Index)>=E.Nodes.size()) return Reject(Error,"Rule references an invalid child index.");
        if (Colors[Index]==1) return Reject(Error,"Rule expression contains a cycle.");
        if (Colors[Index]==2) return true;
        Colors[Index]=1;const auto& N=E.Nodes[Index];
        if (N.Op<=UpgradeCondition::AtLeast)
        {
            if (N.Children.empty() || N.Children.size()>MaximumUpgradeExpression)
                return Reject(Error,"Rule operator requires bounded children; use an empty expression for true.");
            if (N.Op==UpgradeCondition::AtLeast && (!N.Required || N.Required>N.Children.size()))
                return Reject(Error,"AtLeast count is outside the number of children.");
            std::set<std::uint32_t> Seen;
            for (auto C:N.Children) if (!Seen.insert(C).second || C>std::uint32_t(INT32_MAX) || !Visit(static_cast<std::int32_t>(C)))
                return Reject(Error,Error.empty()?"Duplicate rule child would count the same condition twice.":Error);
        }
        else
        {
            if (!N.Children.empty() || !Identity(N.Subject)) return Reject(Error,"Rule operand requires an identity and no children.");
            switch (N.Op)
            {
            case UpgradeCondition::ChosenNode:
                if (!Nodes.contains(N.Subject)) return Reject(Error,"Unknown prerequisite "+N.Subject+".");
                UniqueAdd(Dependencies,N.Subject);break;
            case UpgradeCondition::RouteLevel:
                if (!Routes.contains(N.Subject) || !N.Required) return Reject(Error,"Invalid route-level operand.");break;
            case UpgradeCondition::InstalledCapability:
                if (Acquisition) return Reject(Error,"Acquisition prerequisites must use persistent choices or route levels.");
                if (!Capabilities.contains(N.Subject)) return Reject(Error,"Unknown installed capability operand.");break;
            default:return Reject(Error,"Unknown rule operator.");
            }
        }
        Colors[Index]=2;return true;
    };
    if (!Visit(E.Root)) return false;
    return std::all_of(Colors.begin(),Colors.end(),[](auto C){return C==2;}) || Reject(Error,"Expression contains unreachable nodes.");
}
bool SortDependencies(const std::map<std::string,std::vector<std::string>>& Graph,std::vector<std::string>& Out,std::string& Error)
{
    std::map<std::string,std::uint8_t> Colors;std::vector<std::string> Path;
    std::function<bool(const std::string&)> Visit=[&](const std::string& Id)
    {
        if (Colors[Id]==2) return true;
        if (Colors[Id]==1)
        {std::string Chain;for (const auto& P:Path) Chain+=P+" -> ";return Reject(Error,"Dependency cycle: "+Chain+Id);}
        if (Path.size()>=MaximumUpgradeNodes) return Reject(Error,"Dependency graph exceeds capacity.");
        Colors[Id]=1;Path.push_back(Id);
        if (const auto Edges=Graph.find(Id);Edges!=Graph.end()) for (const auto& D:Edges->second) if (!Visit(D)) return false;
        Path.pop_back();Colors[Id]=2;Out.push_back(Id);return true;
    };
    for (const auto& [Id,_]:Graph) if (!Visit(Id)) return false;
    return true;
}
bool Obtainable(const CompiledUpgradeCatalog& C,std::string& Error)
{
    using History=std::set<std::string>;using Alternatives=std::vector<History>;
    std::map<std::string,Alternatives> Histories;std::size_t Merges=0;bool Capacity=true;
    const auto Ancestor=[&](const std::string& A,const std::string& B)
    {
        for (std::string Id=B;!Id.empty();Id=C.Data.Nodes.at(C.NodeIndices.at(Id)).Replaces) if (Id==A) return true;
        return false;
    };
    const auto Merge=[&](const Alternatives& Left,const Alternatives& Right)
    {
        Alternatives Result;
        for (const auto& A:Left) for (const auto& B:Right)
        {
            if (++Merges>MaximumWitnessMerges) {Capacity=false;return Alternatives{};}
            bool Compatible=true;
            for (const auto& AId:A) for (const auto& BId:B)
            {
                const auto& AN=C.Data.Nodes.at(C.NodeIndices.at(AId));const auto& BN=C.Data.Nodes.at(C.NodeIndices.at(BId));
                const auto Conflict=C.Conflicts.find(AN.Route);
                if ((Conflict!=C.Conflicts.end() && Conflict->second.contains(BN.Route))
                    || (AN.Route==BN.Route && !Ancestor(AId,BId) && !Ancestor(BId,AId))) Compatible=false;
            }
            if (!Compatible) continue;
            History Combined=A;Combined.insert(B.begin(),B.end());
            if (std::any_of(Result.begin(),Result.end(),[&](const auto& H){return std::includes(Combined.begin(),Combined.end(),H.begin(),H.end());})) continue;
            std::erase_if(Result,[&](const auto& H){return std::includes(H.begin(),H.end(),Combined.begin(),Combined.end());});
            if (Result.size()>=MaximumAlternatives) {Capacity=false;return Alternatives{};}Result.push_back(std::move(Combined));
        }
        return Result;
    };
    for (const auto& Id:C.Order)
    {
        const auto& U=C.Data.Nodes.at(C.NodeIndices.at(Id));const auto& E=U.Requires;
        std::function<Alternatives(std::int32_t)> Visit=[&](std::int32_t Index)->Alternatives
        {
            const auto& N=E.Nodes[Index];
            if (N.Op==UpgradeCondition::ChosenNode) return Histories.at(N.Subject);
            if (N.Op==UpgradeCondition::RouteLevel)
            {
                std::uint32_t Level=UINT32_MAX;for (const auto& Other:C.Data.Nodes)
                    if (Other.Route==N.Subject && Other.Level>=N.Required) Level=std::min(Level,Other.Level);
                Alternatives A;for (const auto& Other:C.Data.Nodes) if (Other.Route==N.Subject && Other.Level==Level)
                {const auto& H=Histories.at(Other.Id);A.insert(A.end(),H.begin(),H.end());}
                if (A.size()>MaximumAlternatives) {Capacity=false;return {};}return A;
            }
            const auto Required=N.Op==UpgradeCondition::All ? N.Children.size():N.Op==UpgradeCondition::Any ? 1:N.Required;
            std::vector<Alternatives> ByCount(Required+1);ByCount[0].emplace_back();
            for (auto Child:N.Children)
            {
                const auto Choices=Visit(static_cast<std::int32_t>(Child));
                for (auto Count=Required;Count>0;--Count)
                {
                    auto Added=Merge(ByCount[Count-1],Choices);
                    if (ByCount[Count].size()+Added.size()>MaximumAlternatives) {Capacity=false;return {};}
                    ByCount[Count].insert(ByCount[Count].end(),Added.begin(),Added.end());
                }
            }
            return ByCount[Required];
        };
        auto A=E.Nodes.empty()?Alternatives{History{}}:Visit(E.Root);
        if (!U.Replaces.empty()) A=Merge(A,Histories.at(U.Replaces));
        A=Merge(A,Alternatives{History{Id}});
        if (!Capacity) return Reject(Error,"Upgrade prerequisite witnesses exceed capacity.");
        if (A.empty()) return Reject(Error,Id+": mandatory prerequisites conflict with exclusions or incompatible upgrades.");
        Histories.emplace(Id,std::move(A));
    }
    return true;
}
void WriteStrings(Writer& W,const std::vector<std::string>& Values)
{W.U32(static_cast<std::uint32_t>(Values.size()));for (const auto& V:Values) W.Text(V);}
bool ReadStrings(Reader& R,std::vector<std::string>& Values,std::size_t Maximum)
{
    std::uint32_t Count=0;if (!R.U32(Count) || Count>Maximum) return false;Values.resize(Count);
    for (auto& V:Values) if (!R.Text(V,192)) return false;return true;
}
void WriteExpression(Writer& W,const UpgradeExpression& E)
{
    W.U32(std::bit_cast<std::uint32_t>(E.Root));W.U32(static_cast<std::uint32_t>(E.Nodes.size()));
    for (const auto& N:E.Nodes)
    {W.U8(static_cast<std::uint8_t>(N.Op));W.Text(N.Subject);W.U32(N.Required);W.U32(static_cast<std::uint32_t>(N.Children.size()));for (auto Child:N.Children) W.U32(Child);}
}
bool ReadExpression(Reader& R,UpgradeExpression& E)
{
    std::uint32_t Root=0,Count=0;if (!R.U32(Root) || !R.U32(Count) || Count>MaximumUpgradeExpression) return false;
    E.Root=std::bit_cast<std::int32_t>(Root);E.Nodes.resize(Count);
    for (auto& N:E.Nodes)
    {
        std::uint8_t Op=0;std::uint32_t Children=0;
        if (!R.U8(Op) || Op>static_cast<std::uint8_t>(UpgradeCondition::InstalledCapability) || !R.Text(N.Subject,192)
            || !R.U32(N.Required) || !R.U32(Children) || Children>MaximumUpgradeExpression) return false;
        N.Op=static_cast<UpgradeCondition>(Op);N.Children.resize(Children);for (auto& Child:N.Children) if (!R.U32(Child)) return false;
    }
    return true;
}
}
bool EvaluateUpgradeExpression(const UpgradeExpression& E,const UpgradeContext& Context)
{
    if (E.Nodes.empty()) return E.Root==-1;
    if (E.Nodes.size()>MaximumUpgradeExpression) return false;
    std::vector<std::uint8_t> Colors(E.Nodes.size());std::vector<bool> Answers(E.Nodes.size());bool Valid=true;
    std::function<bool(std::int32_t)> Visit=[&](std::int32_t Index)
    {
        if (Index<0 || std::size_t(Index)>=E.Nodes.size() || Colors[Index]==1) {Valid=false;return false;}
        if (Colors[Index]==2) return bool(Answers[Index]);Colors[Index]=1;const auto& N=E.Nodes[Index];bool Result=false;
        if (N.Op>=UpgradeCondition::ChosenNode && (!N.Children.empty() || !Identity(N.Subject))) Valid=false;
        switch (N.Op)
        {
        case UpgradeCondition::ChosenNode:Result=Context.Chosen.contains(N.Subject);break;
        case UpgradeCondition::RouteLevel:
            if (const auto Level=Context.RouteLevels.find(N.Subject);Level!=Context.RouteLevels.end()) Result=Level->second>=N.Required;break;
        case UpgradeCondition::InstalledCapability:Result=Context.Installed.contains(N.Subject);break;
        case UpgradeCondition::All:case UpgradeCondition::Any:case UpgradeCondition::AtLeast:
        {
            std::size_t Count=0;std::set<std::uint32_t> Seen;
            for (auto Child:N.Children)
            {
                if (!Seen.insert(Child).second || Child>std::uint32_t(INT32_MAX)) {Valid=false;continue;}
                Count+=Visit(static_cast<std::int32_t>(Child))?1:0;
            }
            const auto Required=N.Op==UpgradeCondition::All?N.Children.size():N.Op==UpgradeCondition::Any?1:N.Required;
            Result=!N.Children.empty() && Required>0 && Required<=N.Children.size() && Count>=Required;break;
        }
        default:Valid=false;
        }
        Colors[Index]=2;Answers[Index]=Result;return Result;
    };
    const bool Answer=Visit(E.Root);return Valid && Answer
        && std::all_of(Colors.begin(),Colors.end(),[](auto Color){return Color==2;});
}
bool CompileUpgradeCatalog(const UpgradeCatalog& Data,CompiledUpgradeCatalog& Out,std::string& Error)
{
    Out={};Error.clear();CompiledUpgradeCatalog C;
    if (!Data.Version || Data.Nodes.size()>MaximumUpgradeNodes || Data.Groups.size()>MaximumIds || Data.Exclusions.size()>128)
        return Reject(Error,"Upgrade catalog exceeds capacity or has no version.");
    std::set<std::string> NodeIds,Routes,Capabilities;
    for (std::size_t I=0;I<Data.Nodes.size();++I)
    {
        const auto& N=Data.Nodes[I];
        if (!Identity(N.Id) || !Identity(N.Route) || !N.Level || N.Level>INT32_MAX || !NodeIds.insert(N.Id).second)
            return Reject(Error,"Upgrade has an empty/duplicate identity or invalid level.");
        Routes.insert(N.Route);C.NodeIndices.emplace(N.Id,I);
    }
    for (std::size_t I=0;I<Data.Groups.size();++I)
    {
        const auto& G=Data.Groups[I];
        if (!Identity(G.Configuration) || !Identity(G.Identity) || !C.GroupIndices.emplace(G.Configuration,I).second
            || G.Slots.size()>128 || G.Capabilities.size()>128) return Reject(Error,"Group has an invalid or duplicate configuration.");
        std::set<std::string> Slots,Ids;
        for (const auto& Slot:G.Slots) if (!Identity(Slot) || !Slots.insert(Slot).second) return Reject(Error,"Group has an invalid or duplicate slot.");
        for (const auto& P:G.Capabilities)
        {
            if (!Identity(P.Id) || !Ids.insert(P.Id).second || P.Tags.size()>128 || (!P.ExclusiveDomain.empty() && !Identity(P.ExclusiveDomain)))
                return Reject(Error,"Group has an invalid or duplicate capability.");
            Capabilities.insert(P.Id);for (const auto& Tag:P.Tags) {if (!Identity(Tag)) return Reject(Error,"Invalid capability tag.");Capabilities.insert(Tag);}
        }
        if (G.Executable && G.Capabilities.empty()) return Reject(Error,"Executable group must provide an implemented capability.");
    }
    std::set<std::string> Exclusions;
    for (const auto& E:Data.Exclusions)
    {
        if (!Identity(E.Id) || !Exclusions.insert(E.Id).second || E.Routes.size()<2 || E.Routes.size()>MaximumIds)
            return Reject(Error,"Exclusion requires a unique ID and bounded routes.");
        std::set<std::string> Seen;
        for (const auto& Route:E.Routes)
        {
            if (!Routes.contains(Route) || !Seen.insert(Route).second) return Reject(Error,"Exclusion references an unknown or duplicate route.");
            for (const auto& Other:E.Routes) if (Other!=Route) C.Conflicts[Route].insert(Other);
        }
    }
    for (const auto& N:Data.Nodes) if (!C.GroupIndices.contains(N.Group)) return Reject(Error,N.Id+" references an unknown group configuration.");
    for (const auto& N:Data.Nodes)
    {
        auto& Dependencies=C.Dependencies[N.Id];
        if (!ValidateExpression(N.Requires,NodeIds,Routes,Capabilities,true,Dependencies,Error)) {Error=N.Id+": "+Error;return false;}
        for (const auto& Operand:N.Requires.Nodes) if (Operand.Op==UpgradeCondition::RouteLevel)
        {
            auto Level=UINT32_MAX;for (const auto& Other:Data.Nodes) if (Other.Route==Operand.Subject && Other.Level>=Operand.Required) Level=std::min(Level,Other.Level);
            if (Level==UINT32_MAX) return Reject(Error,"Route-level prerequisite cannot be supplied by this catalog.");
            for (const auto& Other:Data.Nodes) if (Other.Route==Operand.Subject && Other.Level==Level) UniqueAdd(Dependencies,Other.Id);
        }
        if (!N.Replaces.empty())
        {
            const auto Previous=C.NodeIndices.find(N.Replaces);
            if (Previous==C.NodeIndices.end()) return Reject(Error,"Upgrade references an unknown replacement.");
            const auto& P=Data.Nodes[Previous->second];
            if (P.Route!=N.Route || P.Level>=N.Level || Data.Groups[C.GroupIndices.at(P.Group)].Identity!=Data.Groups[C.GroupIndices.at(N.Group)].Identity)
                return Reject(Error,"Upgrade replacement must advance the same route and group.");
            UniqueAdd(Dependencies,N.Replaces);
        }
        for (const auto& D:Dependencies) UniqueAdd(C.ReverseDependencies[D],N.Id);
    }
    if (!SortDependencies(C.Dependencies,C.Order,Error)) return false;
    std::map<std::string,std::vector<std::string>> Providers;
    std::set<std::string> RuntimeKeys;
    for (const auto& G:Data.Groups) for (const auto& P:G.Capabilities)
    {
        const auto Key=G.Configuration+"/"+P.Id;
        if (!RuntimeKeys.insert(Key).second) return Reject(Error,"Ambiguous runtime capability identity.");
        UniqueAdd(Providers[P.Id],Key);for (const auto& Tag:P.Tags) UniqueAdd(Providers[Tag],Key);
    }
    for (const auto& G:Data.Groups) for (const auto& P:G.Capabilities)
    {
        std::vector<std::string> Ignored;
        if (!ValidateExpression(P.RuntimeRequires,NodeIds,Routes,Capabilities,false,Ignored,Error)) return false;
        const auto Key=G.Configuration+"/"+P.Id;auto& Dependencies=C.RuntimeDependencies[Key];
        for (const auto& Operand:P.RuntimeRequires.Nodes) if (Operand.Op==UpgradeCondition::InstalledCapability)
            for (const auto& Provider:Providers.at(Operand.Subject)) {UniqueAdd(Dependencies,Provider);UniqueAdd(C.RuntimeReverseDependencies[Provider],Key);}
    }
    if (!SortDependencies(C.RuntimeDependencies,C.RuntimeOrder,Error)) return false;
    C.Data=Data;if (!Obtainable(C,Error)) return false;
    C.Valid=true;Out=std::move(C);return true;
}
bool ResolveUpgradeHistory(const CompiledUpgradeCatalog& Rules,const std::vector<std::string>& History,UpgradeResolved& Out,std::string& Error)
{
    Out={};Error.clear();if (!Rules.Valid || History.size()>MaximumUpgradeNodes) return Reject(Error,"Invalid upgrade catalog or history capacity.");
    UpgradeResolved Result;std::map<std::string,std::string> Active;
    for (const auto& Id:History)
    {
        const auto Index=Rules.NodeIndices.find(Id);
        if (Index==Rules.NodeIndices.end() || Result.Context.Chosen.contains(Id)) return Reject(Error,"Build history contains an unknown or duplicate choice.");
        const auto& N=Rules.Data.Nodes[Index->second];
        if (!EvaluateUpgradeExpression(N.Requires,Result.Context)) return Reject(Error,"Required choices, route level, or installed capability are unavailable.");
        const auto Old=Active.find(N.Route);
        if (!N.Replaces.empty() && (Old==Active.end() || Old->second!=N.Replaces)) return Reject(Error,"Upgrade does not replace the currently selected predecessor.");
        if (N.Replaces.empty() && Old!=Active.end()) return Reject(Error,"Route already has a choice; an upgrade requires an explicit replacement.");
        if (const auto Conflicts=Rules.Conflicts.find(N.Route);Conflicts!=Rules.Conflicts.end())
            for (const auto& R:Conflicts->second) if (Result.Context.RouteLevels.contains(R)) return Reject(Error,"Route "+N.Route+" is locked by "+R+" for this match.");
        Result.Context.Chosen.insert(Id);Result.Context.RouteLevels[N.Route]=N.Level;Active[N.Route]=Id;
    }
    for (const auto& [_,Id]:Active) Result.ActiveNodes.push_back(Id);std::sort(Result.ActiveNodes.begin(),Result.ActiveNodes.end());
    std::set<std::string> Slots,Groups,Domains;
    for (const auto& Id:Result.ActiveNodes)
    {
        const auto& N=Rules.Data.Nodes.at(Rules.NodeIndices.at(Id));const auto& G=Rules.Data.Groups.at(Rules.GroupIndices.at(N.Group));
        if (!Groups.insert(G.Identity).second) return Reject(Error,"Two active routes occupy the same group identity.");
        Result.Groups.push_back(G.Configuration);
        for (const auto& Slot:G.Slots) if (!Slots.insert(Slot).second) return Reject(Error,"Active groups both occupy "+Slot+".");
        for (const auto& P:G.Capabilities)
        {
            if (G.Executable && !P.ExclusiveDomain.empty() && !Domains.insert(P.ExclusiveDomain).second)
                return Reject(Error,"Runtime domain permits one capability per object: "+P.ExclusiveDomain);
            Result.Context.Installed.insert(P.Id);Result.Context.Installed.insert(P.Tags.begin(),P.Tags.end());
        }
    }
    Out=std::move(Result);return true;
}
bool CanChooseUpgrade(const CompiledUpgradeCatalog& Rules,const std::vector<std::string>& History,const std::string& Id,std::string& Error,bool RequireExecutable)
{
    if (!Rules.Valid) return Reject(Error,"Invalid upgrade catalog.");
    const auto Index=Rules.NodeIndices.find(Id);if (Index==Rules.NodeIndices.end()) return Reject(Error,"Unknown upgrade node.");
    const auto& N=Rules.Data.Nodes[Index->second];
    if (RequireExecutable && !Rules.Data.Groups.at(Rules.GroupIndices.at(N.Group)).Executable)
        return Reject(Error,"This component's gameplay is not implemented and is outside the reward pool.");
    auto Candidate=History;Candidate.push_back(Id);UpgradeResolved Result;return ResolveUpgradeHistory(Rules,Candidate,Result,Error);
}
Bytes EncodeUpgradeCatalog(const UpgradeCatalog& Data)
{
    CompiledUpgradeCatalog Valid;std::string Error;if (!CompileUpgradeCatalog(Data,Valid,Error)) return {};
    Writer W(MaximumUpgradeCatalogBytes);W.U32(UpgradeCatalogSchema);W.U64(Data.Version);W.U32(static_cast<std::uint32_t>(Data.Nodes.size()));
    for (const auto& N:Data.Nodes) {W.Text(N.Id);W.Text(N.Route);W.Text(N.Replaces);W.Text(N.Group);W.U32(N.Level);WriteExpression(W,N.Requires);}
    W.U32(static_cast<std::uint32_t>(Data.Groups.size()));
    for (const auto& G:Data.Groups)
    {
        W.Text(G.Configuration);W.Text(G.Identity);W.U8(G.Executable?1:0);WriteStrings(W,G.Slots);W.U32(static_cast<std::uint32_t>(G.Capabilities.size()));
        for (const auto& P:G.Capabilities) {W.Text(P.Id);W.Text(P.ExclusiveDomain);WriteStrings(W,P.Tags);WriteExpression(W,P.RuntimeRequires);}
    }
    W.U32(static_cast<std::uint32_t>(Data.Exclusions.size()));for (const auto& E:Data.Exclusions) {W.Text(E.Id);WriteStrings(W,E.Routes);}return W.Take();
}
bool DecodeUpgradeCatalog(std::span<const std::uint8_t> Bytes,UpgradeCatalog& Out)
{
    if (Bytes.empty() || Bytes.size()>MaximumUpgradeCatalogBytes) return false;Reader R(Bytes);UpgradeCatalog V;std::uint32_t Schema=0,Count=0;
    if (!R.U32(Schema) || Schema!=UpgradeCatalogSchema || !R.U64(V.Version) || !R.U32(Count) || Count>MaximumUpgradeNodes) return false;
    V.Nodes.resize(Count);for (auto& N:V.Nodes)
        if (!R.Text(N.Id,192) || !R.Text(N.Route,192) || !R.Text(N.Replaces,192) || !R.Text(N.Group,192) || !R.U32(N.Level) || !ReadExpression(R,N.Requires)) return false;
    if (!R.U32(Count) || Count>MaximumIds) return false;V.Groups.resize(Count);
    for (auto& G:V.Groups)
    {
        std::uint8_t Executable=0;if (!R.Text(G.Configuration,192) || !R.Text(G.Identity,192) || !R.U8(Executable) || Executable>1
            || !ReadStrings(R,G.Slots,128) || !R.U32(Count) || Count>128) return false;
        G.Executable=Executable!=0;G.Capabilities.resize(Count);
        for (auto& P:G.Capabilities) if (!R.Text(P.Id,192) || !R.Text(P.ExclusiveDomain,192) || !ReadStrings(R,P.Tags,128) || !ReadExpression(R,P.RuntimeRequires)) return false;
    }
    if (!R.U32(Count) || Count>128) return false;V.Exclusions.resize(Count);
    for (auto& E:V.Exclusions) if (!R.Text(E.Id,192) || !ReadStrings(R,E.Routes,MaximumIds)) return false;
    CompiledUpgradeCatalog Valid;std::string Error;if (!R.Done() || !CompileUpgradeCatalog(V,Valid,Error)) return false;Out=std::move(V);return true;
}
bool ValidUpgradeSelections(const UpgradeSelections& Values)
{
    if (Values.size()>16) return false;
    for (const auto& [Id,V]:Values)
    {
        if (!Identity(Id) || !V.CatalogVersion || V.Chosen.size()>MaximumUpgradeNodes || V.Version!=V.Chosen.size()) return false;
        std::set<std::string> Unique;for (const auto& N:V.Chosen) if (!Identity(N) || !Unique.insert(N).second) return false;
    }
    return true;
}
void WriteUpgradeSelections(Writer& W,const UpgradeSelections& Values)
{
    W.U32(static_cast<std::uint32_t>(Values.size()));
    for (const auto& [Id,V]:Values) {W.Text(Id);W.U64(V.CatalogVersion);W.U64(V.Version);WriteStrings(W,V.Chosen);}
}
bool ReadUpgradeSelections(Reader& R,UpgradeSelections& Out)
{
    std::uint32_t Count=0;if (!R.U32(Count) || Count>16) return false;UpgradeSelections V;
    for (std::uint32_t Index=0;Index<Count;++Index)
    {
        std::string Id;UpgradeSelection S;
        if (!R.Text(Id,192) || !R.U64(S.CatalogVersion) || !R.U64(S.Version) || !ReadStrings(R,S.Chosen,MaximumUpgradeNodes)
            || !V.emplace(Id,std::move(S)).second) return false;
    }
    if (!ValidUpgradeSelections(V)) return false;Out=std::move(V);return true;
}
}
