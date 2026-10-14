#pragma once
#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"
#include "GuLiNetUpgrade.h"

// Kept behind the runtime module's private boundary; reflected interfaces remain UE types.
struct FGuLiShipSharedBuildRules { GuLiNet::CompiledUpgradeCatalog Rules; };
namespace GuLiShipBuildBridge
{
inline std::string Name(FName V) {return V.IsNone()?std::string{}:std::string(TCHAR_TO_UTF8(*V.ToString().ToLower()));}
inline GuLiNet::UpgradeExpression Expression(const FGuLiShipRuleExpression& V)
{
    GuLiNet::UpgradeExpression Out;Out.Root=V.Root;
    for (const auto& N:V.Nodes)
    {
        auto& P=Out.Nodes.emplace_back();P.Op=static_cast<GuLiNet::UpgradeCondition>(N.Op);P.Subject=Name(N.SubjectId);P.Required=N.Required;
        for (auto Child:N.Children) P.Children.push_back(static_cast<uint32>(Child));
    }
    return Out;
}
inline GuLiNet::UpgradeContext Context(const FGuLiShipRuleContext& V)
{
    GuLiNet::UpgradeContext Out;
    for (auto N:V.ChosenNodes) Out.Chosen.insert(Name(N));for (auto N:V.InstalledCapabilities) Out.Installed.insert(Name(N));
    for (const auto& P:V.RouteLevels) Out.RouteLevels.emplace(Name(P.Key),P.Value);return Out;
}
inline std::vector<std::string> History(const FGuLiShipRunBuildState& V)
{std::vector<std::string> Out;for (auto N:V.ChosenNodeIds) Out.push_back(Name(N));return Out;}
inline void Resolved(const GuLiNet::UpgradeResolved& V,FGuLiShipResolvedBuild& Out)
{
    Out={};for (const auto& N:V.ActiveNodes) Out.ActiveNodeIds.Add(FName(UTF8_TO_TCHAR(N.c_str())));
    // Preserve the production assembly order used by FName, independent of wire text ordering.
    Out.ActiveNodeIds.Sort(FNameLexicalLess());
    for (const auto& N:V.Groups) Out.GroupConfigurationIds.Add(FName(UTF8_TO_TCHAR(N.c_str())));
    for (const auto& N:V.Context.Chosen) Out.Context.ChosenNodes.Add(FName(UTF8_TO_TCHAR(N.c_str())));
    for (const auto& N:V.Context.Installed) Out.Context.InstalledCapabilities.Add(FName(UTF8_TO_TCHAR(N.c_str())));
    for (const auto& P:V.Context.RouteLevels) Out.Context.RouteLevels.Add(FName(UTF8_TO_TCHAR(P.first.c_str())),P.second);
}
}
