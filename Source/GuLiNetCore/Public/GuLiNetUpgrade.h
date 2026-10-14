#pragma once
#include "GuLiNetProtocol.h"

namespace GuLiNet
{
// Data-only selection rules. Engine classes, sockets and visuals are resolved by the caller.
enum class UpgradeCondition : std::uint8_t { All, Any, AtLeast, ChosenNode, RouteLevel, InstalledCapability };
struct UpgradeOperand
{
    UpgradeCondition Op=UpgradeCondition::All;
    std::vector<std::uint32_t> Children;
    std::string Subject;
    std::uint32_t Required=1;
};
struct UpgradeExpression { std::vector<UpgradeOperand> Nodes; std::int32_t Root=-1; };
struct UpgradeCapability
{
    std::string Id,ExclusiveDomain;
    std::vector<std::string> Tags;
    UpgradeExpression RuntimeRequires;
};
struct UpgradeGroup
{
    std::string Configuration,Identity;
    std::vector<std::string> Slots;
    std::vector<UpgradeCapability> Capabilities;
    bool Executable=false;
};
struct UpgradeNode
{
    std::string Id,Route,Replaces,Group;
    std::uint32_t Level=1;
    UpgradeExpression Requires;
};
struct UpgradeExclusion { std::string Id;std::vector<std::string> Routes; };
struct UpgradeCatalog
{
    Revision Version=1;
    std::vector<UpgradeNode> Nodes;
    std::vector<UpgradeGroup> Groups;
    std::vector<UpgradeExclusion> Exclusions;
};
struct UpgradeContext
{
    std::set<std::string> Chosen,Installed;
    std::map<std::string,std::uint32_t> RouteLevels;
};
struct UpgradeResolved
{
    std::vector<std::string> ActiveNodes,Groups;
    UpgradeContext Context;
};
struct UpgradeSelection
{
    Revision CatalogVersion=0,Version=0;
    std::vector<std::string> Chosen;
};
using UpgradeSelections=std::map<std::string,UpgradeSelection>;
struct CompiledUpgradeCatalog
{
    UpgradeCatalog Data;
    std::map<std::string,std::size_t> NodeIndices,GroupIndices;
    std::map<std::string,std::set<std::string>> Conflicts;
    std::map<std::string,std::vector<std::string>> Dependencies,ReverseDependencies,RuntimeDependencies,RuntimeReverseDependencies;
    std::vector<std::string> Order,RuntimeOrder;
    bool Valid=false;
};
inline constexpr std::uint32_t UpgradeCatalogSchema=1;
inline constexpr std::size_t MaximumUpgradeNodes=512,MaximumUpgradeExpression=256,MaximumUpgradeCatalogBytes=4*1024*1024;
GULINETCORE_API bool CompileUpgradeCatalog(const UpgradeCatalog& Data,CompiledUpgradeCatalog& Out,std::string& Error);
GULINETCORE_API bool EvaluateUpgradeExpression(const UpgradeExpression& Expression,const UpgradeContext& Context);
GULINETCORE_API bool ResolveUpgradeHistory(const CompiledUpgradeCatalog& Rules,const std::vector<std::string>& History,UpgradeResolved& Out,std::string& Error);
GULINETCORE_API bool CanChooseUpgrade(const CompiledUpgradeCatalog& Rules,const std::vector<std::string>& History,
    const std::string& Node,std::string& Error,bool RequireExecutable=true);
GULINETCORE_API Bytes EncodeUpgradeCatalog(const UpgradeCatalog& Data);
GULINETCORE_API bool DecodeUpgradeCatalog(std::span<const std::uint8_t> Bytes,UpgradeCatalog& Out);
GULINETCORE_API bool ValidUpgradeSelections(const UpgradeSelections& Values);
GULINETCORE_API void WriteUpgradeSelections(Writer& Out,const UpgradeSelections& Values);
GULINETCORE_API bool ReadUpgradeSelections(Reader& In,UpgradeSelections& Out);
}
