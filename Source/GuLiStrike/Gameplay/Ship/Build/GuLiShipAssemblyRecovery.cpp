#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridReflectedState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/GuLiHybridManifest.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"

namespace
{
constexpr uint32 Version=3,MaximumBytes=1024*1024;
struct FCapabilityState
{
    FName Id;FString Class;uint32 Version=0;uint64 ConfigurationHash=0;
    EGuLiShipCapabilityState State=EGuLiShipCapabilityState::Prepared;TArray<uint8> Private;
};
struct FGroupState {FName Configuration;TArray<FName> Missing;TArray<FCapabilityState> Capabilities;};
struct FReceipt
{
    FGuid Event;FName Group,Capability,Action;int64 Epoch=0,BuildRevision=0;
    FGuLiShipCapabilityActivationResult Result;
};
struct FState
{
    FString Catalog;int32 CatalogRevision=0;FName Domain,UpgradeRule;FGuLiShipRunBuildState Build;
    TArray<FGroupState> Groups;TArray<FReceipt> Receipts;
    struct FChoice {FGuid Event,Requester;FName Node;int64 Epoch=0,Revision=0;bool Resolved=false,Notified=false;FGuLiShipChoiceResult Result;};
    TArray<FChoice> Choices;
};
void SerializeState(FGuLiHybridStateArchive& A,FState& S)
{
    uint32 Schema=Version;A.Value(Schema);if (Schema!=Version) A.Reject();
    A.String(S.Catalog);A.Value(S.CatalogRevision);A.Value(S.Domain);A.Value(S.UpgradeRule);FGuLiHybridReflectedState::Value(A,S.Build);
    A.Array(S.Groups,128,[](auto& Ar,auto& G)
    {
        Ar.Value(G.Configuration);Ar.Array(G.Missing,128,[](auto& R,auto& N){R.Value(N);if (N.IsNone()) R.Reject();});
        Ar.Array(G.Capabilities,32,[](auto& R,auto& C)
        {R.Value(C.Id);R.String(C.Class);R.Value(C.Version);R.Value(C.ConfigurationHash);
         R.Enum(C.State,uint8(EGuLiShipCapabilityState::Suspended));R.Bytes(C.Private,256*1024);
         if (C.Id.IsNone() || C.Class.IsEmpty() || !C.Version || !C.ConfigurationHash || C.Private.IsEmpty()) R.Reject();});
        if (G.Configuration.IsNone()) Ar.Reject();
        TSet<FName> Missing,Ids;for (const auto N:G.Missing) {if (Missing.Contains(N)) Ar.Reject();Missing.Add(N);}
        for (const auto& C:G.Capabilities) {if (Ids.Contains(C.Id)) Ar.Reject();Ids.Add(C.Id);}
    });
    A.Array(S.Receipts,4096,[](auto& Ar,auto& R)
    {
        Ar.Value(R.Event);Ar.Value(R.Group);Ar.Value(R.Capability);Ar.Value(R.Action);Ar.Value(R.Epoch);Ar.Value(R.BuildRevision);
        FGuLiHybridReflectedState::Value(Ar,R.Result);
        if (!R.Event.IsValid() || R.Event!=R.Result.RequestId || R.Group.IsNone() || R.Capability.IsNone()
            || R.Action.IsNone() || R.Epoch<=0 || R.BuildRevision<1) Ar.Reject();
    });
    A.Array(S.Choices,1024,[](auto& Ar,auto& R)
    {
        Ar.Value(R.Event);Ar.Value(R.Requester);Ar.Value(R.Node);Ar.Value(R.Epoch);Ar.Value(R.Revision);Ar.Value(R.Resolved);Ar.Value(R.Notified);
        FGuLiHybridReflectedState::Value(Ar,R.Result);
        if (!R.Event.IsValid() || !R.Requester.IsValid() || R.Node.IsNone() || R.Epoch<=0 || R.Revision<1
            || R.Result.RequestId!=R.Event || R.Result.BuildRevision<1 || (R.Notified && !R.Resolved)
            || (!R.Resolved && (R.Result.bCommitted || R.Result.bBusinessEffects))
            || (R.Result.bBusinessEffects && !R.Result.bCommitted)) Ar.Reject();
    });
    TSet<FName> Groups;TSet<FGuid> Events;
    for (const auto& G:S.Groups) {if (Groups.Contains(G.Configuration)) A.Reject();Groups.Add(G.Configuration);}
    for (const auto& R:S.Receipts) {if (Events.Contains(R.Event)) A.Reject();Events.Add(R.Event);}
    Events.Reset();for (const auto& R:S.Choices) {if (Events.Contains(R.Event) || R.Epoch>S.Build.MatchEpoch || R.Revision>S.Build.BuildRevision) A.Reject();Events.Add(R.Event);}
    if (S.Catalog.IsEmpty() || S.CatalogRevision<=0 || S.Domain.IsNone() || S.UpgradeRule.IsNone() || S.Build.MatchEpoch<=0
        || S.Build.BuildRevision!=int64(S.Build.ChosenNodeIds.Num())+1) A.Reject();
}
bool Read(TConstArrayView<uint8> Bytes,FState& State)
{FGuLiHybridStateArchive A(Bytes,MaximumBytes);SerializeState(A,State);return A.Done();}
bool Compatible(const UGuLiShipAssemblyComponent& Assembly,const FState& S)
{
    const auto* Catalog=Assembly.Catalog.Get();if (!Catalog || Catalog->GetPathName()!=S.Catalog
        || Catalog->Revision!=S.CatalogRevision || Assembly.UpgradeDomain!=S.Domain || Assembly.UpgradeRule!=S.UpgradeRule) return false;
    FGuLiShipCompiledBuildRules Rules;FGuLiShipResolvedBuild Resolved;FString Error;
    if (!Catalog->Compile(Rules) || !Catalog->Resolve(Rules,S.Build,Resolved,Error)
        || S.Groups.Num()!=Resolved.GroupConfigurationIds.Num()) return false;
    for (const auto& Choice:S.Choices)
    {
        if (!Catalog->Upgrades.ContainsByPredicate([&](const auto& N){return N.NodeId==Choice.Node;})) return false;
        if (Choice.Result.bCommitted && (!S.Build.ChosenNodeIds.IsValidIndex(Choice.Revision-1)
            || S.Build.ChosenNodeIds[Choice.Revision-1]!=Choice.Node || Choice.Result.BuildRevision<Choice.Revision+1)) return false;
    }
    for (const auto& G:S.Groups)
    {
        if (!Resolved.GroupConfigurationIds.Contains(G.Configuration)) return false;
        const auto* D=Catalog->FindGroup(G.Configuration);if (!D || !D->bExecutable || D->Capabilities.Num()!=G.Capabilities.Num()) return false;
        for (const auto Missing:G.Missing) if (!D->Mounts.ContainsByPredicate([&](const auto& M){return M.SocketName==Missing;})) return false;
        for (const auto& C:G.Capabilities)
        {
            const auto* Def=D->Capabilities.FindByPredicate([&](const auto& V){return V.CapabilityId==C.Id;});
            const auto* CDO=Def && Def->ComponentClass?Def->ComponentClass->GetDefaultObject<UGuLiShipCapabilityComponent>():nullptr;
            if (!CDO || C.Class!=Def->ComponentClass->GetPathName() || C.Version!=CDO->GetPrivateStateVersion()
                || C.ConfigurationHash!=CDO->GetConfigurationChecksum(Def->Configuration) || !CDO->ValidatePrivateState(C.Private,Def->Configuration)
                || (!G.Missing.IsEmpty() && C.State==EGuLiShipCapabilityState::Enabled)) return false;
        }
    }
    return true;
}
const FGuLiHybridComponentCapabilityRegistration EquipmentCapability({TEXT("Equipment"),Version,
    []()->const UClass* {return UGuLiShipAssemblyComponent::StaticClass();},
    [](const UActorComponent& C,TArray<uint8>& B) {return CastChecked<UGuLiShipAssemblyComponent>(&C)->CaptureHybridState(B);},
    [](const UActorComponent& C,TConstArrayView<uint8> B) {return CastChecked<UGuLiShipAssemblyComponent>(&C)->ValidateHybridState(B);},
    [](UActorComponent& C,TConstArrayView<uint8> B,EGuLiHybridRestoreStage Stage) {return CastChecked<UGuLiShipAssemblyComponent>(&C)->RestoreHybridState(B,Stage);},
    nullptr,[](UActorComponent& C,uint64 T,double D) {auto* A=CastChecked<UGuLiShipAssemblyComponent>(&C);A->AdvanceHybridChoices();A->AdvanceHybridCapabilities(T,D);},
    [](UActorComponent& C,const FGuLiHybridObjectState& S,uint64,bool) {return CastChecked<UGuLiShipAssemblyComponent>(&C)->ApplyHybridLedger(S);},
    [](UActorComponent& C,TConstArrayView<uint8> B) {return CastChecked<UGuLiShipAssemblyComponent>(&C)->PrepareHybridRestore(B);}});
}
bool UGuLiShipAssemblyComponent::CaptureHybridState(TArray<uint8>& Bytes) const
{
    if (!Catalog || bPrepared || bCommitting || bRestoringPrivate) return false;
    FState S;S.Catalog=Catalog->GetPathName();S.CatalogRevision=Catalog->Revision;S.Domain=UpgradeDomain;S.UpgradeRule=UpgradeRule;S.Build=AppliedBuild;
    for (const auto& G:Groups)
    {
        const auto* D=Catalog->FindGroup(G.ConfigurationId);if (!D || D->Mounts.Num()!=G.Parts.Num()) return false;
        auto& Saved=S.Groups.AddDefaulted_GetRef();Saved.Configuration=G.ConfigurationId;
        for (int32 Index=0;Index<G.Parts.Num();++Index) if (!G.Parts[Index]) Saved.Missing.Add(D->Mounts[Index].SocketName);
        for (const auto& Capability:G.Capabilities)
        {
            const auto* C=Capability.Get();
            if (!C) return false;auto& V=Saved.Capabilities.AddDefaulted_GetRef();V.Id=C->GetCapabilityId();V.Class=C->GetClass()->GetPathName();
            const auto* Def=D->Capabilities.FindByPredicate([&](const auto& Entry){return Entry.CapabilityId==V.Id;});if (!Def) return false;
            V.Version=C->GetPrivateStateVersion();V.ConfigurationHash=C->GetConfigurationChecksum(Def->Configuration);V.State=C->GetCapabilityState();
            if (!V.Version || !V.ConfigurationHash || !C->CapturePrivateState(V.Private)) return false;
        }
    }
    S.Groups.Sort([](const auto& L,const auto& R){return L.Configuration.LexicalLess(R.Configuration);});
    TArray<FGuid> Events;ActivationReceipts.GenerateKeyArray(Events);Events.Sort([](const FGuid& L,const FGuid& R){return L.ToString()<R.ToString();});
    for (const auto Event:Events)
    {const auto& R=ActivationReceipts[Event];S.Receipts.Add({Event,R.GroupId,R.CapabilityId,R.ActionId,R.MatchEpoch,R.BuildRevision,R.Result});}
    ChoiceReceipts.GenerateKeyArray(Events);Events.Sort([](const FGuid& L,const FGuid& R){return L.ToString()<R.ToString();});
    for (const auto Event:Events)
    {const auto& R=ChoiceReceipts[Event];S.Choices.Add({Event,R.Requester,R.Node,R.Epoch,R.Revision,R.Resolved,R.Notified,R.Result});}
    if (!Compatible(*this,S)) return false;FGuLiHybridStateArchive A(MaximumBytes);SerializeState(A,S);return A.Finish(Bytes);
}
bool UGuLiShipAssemblyComponent::ValidateHybridState(TConstArrayView<uint8> Bytes) const
{FState S;return Read(Bytes,S) && Compatible(*this,S);}
bool UGuLiShipAssemblyComponent::PrepareHybridRestore(TConstArrayView<uint8> Bytes)
{
    FState S;if (!Read(Bytes,S) || !Compatible(*this,S)) return false;
    bRestoringPrivate=true;
    const auto* Battle=GetWorld()->GetGameState<AGuLiBattleGameState>();if (!Battle || Battle->GetMatchEpoch()<=0) return false;
    S.Build.MatchEpoch=Battle->GetMatchEpoch();FString Error;
    if (!PrepareBuild(S.Build,Error)) return false;CommitPreparedBuild();
    TGuardValue<bool> Committing(bCommitting,true);
    for (auto& G:Groups)
    {
        const auto* Saved=S.Groups.FindByPredicate([&](const auto& V){return V.Configuration==G.ConfigurationId;});
        if (!Saved) return false;const auto& Mounts=Catalog->FindGroup(G.ConfigurationId)->Mounts;
        for (int32 Index=0;Index<G.Parts.Num();++Index) if (Saved->Missing.Contains(Mounts[Index].SocketName) && G.Parts[Index])
        {G.Parts[Index]->DestroyComponent();G.Parts[Index]=nullptr;}
    }
    return true;
}
bool UGuLiShipAssemblyComponent::RestoreHybridState(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
{
    FState S;if (!Read(Bytes,S) || !Compatible(*this,S)) return false;
    if (Stage==EGuLiHybridRestoreStage::Detach || Stage==EGuLiHybridRestoreStage::Create) return true;
    if (!bRestoringPrivate) return false;
    if (Stage==EGuLiHybridRestoreStage::Link)
    {
        ChoiceReceipts.Reset();
        for (const auto& R:S.Choices) ChoiceReceipts.Add(R.Event,{R.Node,R.Requester,R.Epoch,R.Revision,R.Resolved,R.Notified,R.Result});
        for (auto& G:Groups)
        {
            const auto* Saved=S.Groups.FindByPredicate([&](const auto& V){return V.Configuration==G.ConfigurationId;});if (!Saved) return false;
            for (const auto& Capability:G.Capabilities)
            {
                auto* C=Capability.Get();
                const auto* V=Saved->Capabilities.FindByPredicate([&](const auto& Entry){return Entry.Id==C->GetCapabilityId();});
                if (!V || !C->RestorePrivateState(V->Private) || !C->RestoreCapabilityState(V->State)) return false;
            }
        }
        ActivationReceipts.Reset();for (const auto& R:S.Receipts)
            ActivationReceipts.Add(R.Event,{R.Group,R.Capability,R.Action,R.Epoch,R.BuildRevision,R.Result});
        if (auto* Player=Ship().GetPlayerState<AGuLiBattlePlayerState>()) Player->GetShipBuild()->AdoptExistingShip(*this);
        return true;
    }
    if (Stage!=EGuLiHybridRestoreStage::Activate) return false;
    for (auto& G:Groups) for (const auto& Capability:G.Capabilities) Capability->NotifyRestoredCapabilityState();
    bRestoringPrivate=false;EvaluateRuntimeRequirements();PublishRuntime();return true;
}
bool UGuLiShipAssemblyComponent::ApplyHybridLedger(const FGuLiHybridObjectState& State)
{
    if (!Catalog || UpgradeDomain.IsNone() || bPrepared || bCommitting) return false;
    FGuLiShipRunBuildState Candidate;const auto* Battle=GetWorld()->GetGameState<AGuLiBattleGameState>();
    if (!Battle || Battle->GetMatchEpoch()<=0) return false;Candidate.MatchEpoch=Battle->GetMatchEpoch();Candidate.BuildRevision=1;
    if (const auto* Choice=State.Upgrades.Find(UpgradeDomain))
    {
        if (Choice->CatalogRevision!=uint64(Catalog->Revision) || Choice->Revision!=uint64(Choice->Chosen.Num()) || Choice->Revision>=uint64(MAX_int64)) return false;
        Candidate.BuildRevision=int64(Choice->Revision)+1;Candidate.ChosenNodeIds=Choice->Chosen;
    }
    if (AppliedBuild.MatchEpoch!=Candidate.MatchEpoch || AppliedBuild.BuildRevision!=Candidate.BuildRevision || AppliedBuild.ChosenNodeIds!=Candidate.ChosenNodeIds)
    {FString Error;if (!PrepareBuild(Candidate,Error)) return false;CommitPreparedBuild();}
    if (auto* Player=Ship().GetPlayerState<AGuLiBattlePlayerState>()) Player->GetShipBuild()->AdoptExistingShip(*this);
    return true;
}
bool UGuLiShipAssemblyComponent::ExportHybridCapabilityContracts(FGuLiHybridManifestData& Data,FString& Error) const
{
    if (!Catalog || UpgradeDomain.IsNone() || UpgradeRule.IsNone()) return false;
    Data.Strings.Add(TEXT("equipment/")+UpgradeDomain.ToString().ToLower()+TEXT("/upgrade_rule"),UpgradeRule.ToString().ToLower());
    for (const auto& G:Catalog->Groups)
    {
        if (!G.bExecutable) continue;
        for (const auto& D:G.Capabilities)
        {
        const auto* CDO=D.ComponentClass?D.ComponentClass->GetDefaultObject<UGuLiShipCapabilityComponent>():nullptr;
        const auto Schema=CDO?CDO->GetPrivateStateVersion():0;const auto Hash=CDO?CDO->GetConfigurationChecksum(D.Configuration):0;
        if (!CDO || !Schema || !Hash)
        {Error=TEXT("Executable equipment has no complete capability contract: ")+G.ConfigurationId.ToString()+TEXT("/")+D.CapabilityId.ToString();return false;}
        const FString Base=TEXT("equipment/")+UpgradeDomain.ToString().ToLower()+TEXT("/")+G.ConfigurationId.ToString().ToLower()
            +TEXT("/")+D.CapabilityId.ToString().ToLower()+TEXT("/");
        Data.Numbers.Add(Base+TEXT("private_version"),Schema);Data.Strings.Add(Base+TEXT("class"),D.ComponentClass->GetPathName());
        Data.Strings.Add(Base+TEXT("configuration_hash"),LexToString(Hash));
        }
    }
    return true;
}
bool UGuLiShipAssemblyComponent::ValidateHybridCapabilityContracts(const FGuLiHybridManifestData& Data) const
{
    FGuLiHybridManifestData Expected;FString Error;if (!ExportHybridCapabilityContracts(Expected,Error)) return false;
    const FString Prefix=TEXT("equipment/")+UpgradeDomain.ToString().ToLower()+TEXT("/");
    for (const auto& [K,V]:Expected.Numbers) {const auto* Found=Data.Numbers.Find(K);if (!Found || *Found!=V) return false;}
    for (const auto& [K,V]:Expected.Strings) if (Data.Strings.FindRef(K)!=V) return false;
    for (const auto& [K,V]:Data.Numbers) if (K.StartsWith(Prefix) && !Expected.Numbers.Contains(K)) return false;
    for (const auto& [K,V]:Data.Strings) if (K.StartsWith(Prefix) && !Expected.Strings.Contains(K)) return false;
    return true;
}
