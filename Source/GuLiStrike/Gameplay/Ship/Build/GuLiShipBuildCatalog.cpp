#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"
#include "Gameplay/Ship/Build/Private/GuLiShipSharedBuildRules.h"
#include "Gameplay/Ship/Capabilities/GuLiShipCapabilityComponent.h"

const FGuLiShipUpgradeNode* UGuLiShipBuildCatalog::FindUpgrade(FName Id) const
{return Upgrades.FindByPredicate([Id](const auto& N){return N.NodeId==Id;});}
const FGuLiShipPartGroupDefinition* UGuLiShipBuildCatalog::FindGroup(FName Id) const
{return Groups.FindByPredicate([Id](const auto& G){return G.ConfigurationId==Id;});}

bool UGuLiShipBuildCatalog::Compile(FGuLiShipCompiledBuildRules& Out) const
{
    using namespace GuLiShipBuildBridge;
    Out={};if (Revision<1) {Out.Error=TEXT("Catalogue revision must be positive.");return false;}
    GuLiNet::UpgradeCatalog Data;Data.Version=Revision;
    for (const auto& N:Upgrades)
    {
        auto& V=Data.Nodes.emplace_back();V.Id=Name(N.NodeId);V.Route=Name(N.RouteId);V.Level=N.Level;
        V.Replaces=Name(N.ReplacesNodeId);V.Group=Name(N.GroupConfigurationId);V.Requires=Expression(N.AcquisitionRequirements);
    }
    for (const auto& G:Groups)
    {
        auto& V=Data.Groups.emplace_back();V.Configuration=Name(G.ConfigurationId);V.Identity=Name(G.GroupId);V.Executable=G.bExecutable;
        for (const auto& M:G.Mounts)
        {
            if (M.PartClass.IsNull()) {Out.Error=TEXT("Group has a missing part class: ")+G.ConfigurationId.ToString();return false;}
            V.Slots.push_back(Name(M.SocketName));
        }
        for (const auto& P:G.Capabilities)
        {
            auto& C=V.Capabilities.emplace_back();C.Id=Name(P.CapabilityId);C.RuntimeRequires=Expression(P.RuntimeRequirements);
            for (const FGameplayTag Tag:P.ProvidedTags) C.Tags.push_back(Name(Tag.GetTagName()));
            if (G.bExecutable)
            {
                if (!P.ComponentClass || P.ComponentClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
                {Out.Error=TEXT("Executable group requires a concrete capability class.");return false;}
                const auto* CDO=P.ComponentClass->GetDefaultObject<UGuLiShipCapabilityComponent>();
                if (!CDO->ValidateConfiguration(P.Configuration,Out.Error)) return false;
                C.ExclusiveDomain=Name(CDO->GetExclusiveRuntimeDomain());
            }
        }
    }
    for (const auto& E:Exclusions)
    {auto& V=Data.Exclusions.emplace_back();V.Id=Name(E.RuleId);for (auto R:E.Routes) V.Routes.push_back(Name(R));}
    auto Shared=MakeShared<FGuLiShipSharedBuildRules>();std::string Error;
    if (!GuLiNet::CompileUpgradeCatalog(Data,Shared->Rules,Error)) {Out.Error=UTF8_TO_TCHAR(Error.c_str());return false;}
    const auto& R=Shared->Rules;
    for (const auto& P:R.NodeIndices) Out.NodeIndices.Add(FName(UTF8_TO_TCHAR(P.first.c_str())),int32(P.second));
    for (const auto& P:R.GroupIndices) Out.GroupIndices.Add(FName(UTF8_TO_TCHAR(P.first.c_str())),int32(P.second));
    for (const auto& P:R.Conflicts) for (const auto& Id:P.second)
        Out.ConflictsByRoute.FindOrAdd(FName(UTF8_TO_TCHAR(P.first.c_str()))).Add(FName(UTF8_TO_TCHAR(Id.c_str())));
    const auto Graph=[](const auto& In,auto& To)
    {for (const auto& P:In) {auto& Items=To.FindOrAdd(FName(UTF8_TO_TCHAR(P.first.c_str())));for (const auto& Id:P.second) Items.Add(FName(UTF8_TO_TCHAR(Id.c_str())));}};
    Graph(R.Dependencies,Out.Dependencies);Graph(R.ReverseDependencies,Out.ReverseDependencies);
    Graph(R.RuntimeDependencies,Out.RuntimeDependencies);Graph(R.RuntimeReverseDependencies,Out.RuntimeReverseDependencies);
    for (const auto& Id:R.Order) Out.TopologicalOrder.Add(FName(UTF8_TO_TCHAR(Id.c_str())));
    for (const auto& Id:R.RuntimeOrder) Out.RuntimeTopologicalOrder.Add(FName(UTF8_TO_TCHAR(Id.c_str())));
    Out.Shared=MoveTemp(Shared);Out.bValid=true;return true;
}
bool UGuLiShipBuildCatalog::Evaluate(const FGuLiShipRuleExpression& E,const FGuLiShipRuleContext& C,FString* Failure)
{
    const bool Satisfied=GuLiNet::EvaluateUpgradeExpression(GuLiShipBuildBridge::Expression(E),GuLiShipBuildBridge::Context(C));
    if (!Satisfied && Failure) *Failure=TEXT("Required choices, route level, or installed capability are unavailable.");return Satisfied;
}
bool UGuLiShipBuildCatalog::Resolve(const FGuLiShipCompiledBuildRules& Rules,const FGuLiShipRunBuildState& State,
    FGuLiShipResolvedBuild& Out,FString& Error) const
{
    Out={};Error.Reset();if (!Rules.bValid || !Rules.Shared) {Error=Rules.Error;return false;}
    GuLiNet::UpgradeResolved V;std::string Reason;
    if (!GuLiNet::ResolveUpgradeHistory(Rules.Shared->Rules,GuLiShipBuildBridge::History(State),V,Reason))
    {Error=UTF8_TO_TCHAR(Reason.c_str());return false;}
    GuLiShipBuildBridge::Resolved(V,Out);
    // Runtime assembly retains the established FName order after portable rule evaluation.
    Out.GroupConfigurationIds.Reset();for (auto Id:Out.ActiveNodeIds) Out.GroupConfigurationIds.Add(FindUpgrade(Id)->GroupConfigurationId);
    return true;
}
bool UGuLiShipBuildCatalog::CanChoose(const FGuLiShipCompiledBuildRules& Rules,const FGuLiShipRunBuildState& State,
    FName Id,FString& Error,bool RequireExecutable) const
{
    Error.Reset();if (!Rules.bValid || !Rules.Shared) {Error=Rules.Error;return false;}
    std::string Reason;const bool Allowed=GuLiNet::CanChooseUpgrade(Rules.Shared->Rules,GuLiShipBuildBridge::History(State),GuLiShipBuildBridge::Name(Id),Reason,RequireExecutable);
    Error=UTF8_TO_TCHAR(Reason.c_str());return Allowed;
}
