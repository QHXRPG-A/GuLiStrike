#pragma once
#include "GuLiNetGameplayTypes.h"
#include <functional>

namespace GuLiNet
{
struct RuntimeManifest;
struct CapabilityContext
{
    const TransactionRequest& Request;
    const Rule& Operation;
    const ObjectRecord& Source;
    const std::map<ObjectKey,ObjectRecord>& Objects;
    const Economy& Accounts;
    const ResourceLedger& Resources;
    const std::map<Id,Member>& Members;
    // Optional committed index for Request.Target; null retains standalone callers.
    const std::set<ObjectKey>* LiveChildren=nullptr;
};
// Prepared changes are isolated until the generic scheduler has validated ALL
// of them. Rejected work cannot partially debit an account or change ownership.
struct CapabilityDelta
{
    std::map<ObjectKey,ObjectRecord> Objects;
    std::optional<Economy> Accounts;
    std::optional<ResourceLedger> Resources;
    std::map<std::uint32_t,Team> TerritoryOwners;
};
struct CapabilityContract
{
    std::string Name;
    std::uint32_t Version=0;
    std::function<bool(const ObjectRecord&,const CapabilityState&)> ValidateState;
    std::function<bool(const Rule&)> ValidateRule;
    std::function<CommitStatus(const CapabilityContext&,CapabilityDelta&)> Prepare;
    std::function<bool(const ObjectRecord&,const ObjectRecord&)> ValidateTransition;
    std::function<bool(RuntimeManifest&,std::string&)> InitializeManifest;
    std::function<bool(const RuntimeManifest&,std::string&)> ValidateManifest;
    // Optional ownership projection for objects whose affiliation is gameplay
    // state while their simulation grant deliberately stays fixed.
    std::function<Team(const ObjectRecord&)> Affiliation;
    // Pure admission hooks, executed in stable capability-name order. Numeric
    // changes are frozen with the attack/effect; no world access is permitted.
    std::function<bool(const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&,const std::string&,Rule&)> ResolveRule;
    std::function<bool(const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest&,ObjectRecord&)> DeriveSpawn;
    std::function<bool(const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&)> ValidateCreation;
    bool AcceptsRetainedSource=false;
    // Cross-object invariants inspect the entire staged delta before commit.
    std::function<bool(const std::map<ObjectKey,ObjectRecord>&,std::span<const ObjectRecord>,const std::set<ObjectKey>*)> ValidateMutation;
    // Capability-owned cross-object accounting. The output is routed through
    // the same owner work protocol; the network never knows population rules.
    std::function<bool(const std::map<ObjectKey,ObjectRecord>&,std::map<ObjectKey,ObjectRecord>&)> ProjectChanges;
    std::function<bool(std::map<ObjectKey,ObjectRecord>&)> InitializeOwnedState;
    // Admission supplied by the spawned family itself, including families
    // whose producer does not carry that child's capability.
    std::function<bool(const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest&,ObjectRecord&,bool&)> FreezeSpawn;
};
GULINETCORE_API bool RegisterCapability(CapabilityContract Contract);
GULINETCORE_API const CapabilityContract* FindCapability(const std::string& Name);
GULINETCORE_API bool ValidateObjectCapabilities(const ObjectRecord& Object);
GULINETCORE_API bool InitializeManifestCapabilities(RuntimeManifest& Manifest,std::string& Error);
GULINETCORE_API bool ValidateManifestCapabilities(const RuntimeManifest& Manifest,std::string& Error);
GULINETCORE_API bool ValidateCapabilityCommit(const CommitResult& Result,const std::map<ObjectKey,ObjectRecord>& Previous,
    const std::map<std::string,std::set<ObjectKey>>* MutationMembers=nullptr);
GULINETCORE_API Team ObjectAffiliation(const ObjectRecord& Object);
GULINETCORE_API bool ResolveCapabilityRule(const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&,const std::string&,Rule&);
GULINETCORE_API bool DeriveSpawnCapabilities(const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest&,ObjectRecord&);
GULINETCORE_API bool ValidateCapabilityMutation(const std::map<ObjectKey,ObjectRecord>& Previous,std::span<const ObjectRecord> Changes,
    const std::map<std::string,std::set<ObjectKey>>* MutationMembers=nullptr);
GULINETCORE_API bool ProjectCapabilityChanges(const std::map<ObjectKey,ObjectRecord>& Previous,std::map<ObjectKey,ObjectRecord>& Changes);
GULINETCORE_API bool InitializeOwnedCapabilityState(std::map<ObjectKey,ObjectRecord>& Objects);
}
