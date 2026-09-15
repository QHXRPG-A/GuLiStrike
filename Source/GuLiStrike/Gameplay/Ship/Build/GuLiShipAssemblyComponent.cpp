#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

UGuLiShipAssemblyComponent::UGuLiShipAssemblyComponent() { SetIsReplicatedByDefault(true); }
AGuLiStrikeShip& UGuLiShipAssemblyComponent::Ship() const { return *CastChecked<AGuLiStrikeShip>(GetOwner()); }
void UGuLiShipAssemblyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiShipAssemblyComponent, Manifest);
	DOREPLIFETIME(UGuLiShipAssemblyComponent, Runtime);
}
void UGuLiShipAssemblyComponent::BeginPlay() { Super::BeginPlay(); if (!GetOwner()->HasAuthority()) OnRep_Manifest(); }
void UGuLiShipAssemblyComponent::EndPlay(const EEndPlayReason::Type Reason) { ReleaseBuild(); Super::EndPlay(Reason); }

bool UGuLiShipAssemblyComponent::PrepareBuild(const FGuLiShipRunBuildState& Build, FString& Error)
{
	check(!bCommitting);
	DiscardPreparedBuild();
	if (!Catalog || Build.MatchEpoch <= 0 || Build.BuildRevision < 1)
	{
		Error = TEXT("Assembly requires a catalogue and an initialized match build."); return false;
	}
	if (CompiledCatalog != Catalog || CompiledRevision != Catalog->Revision)
	{
		if (!Catalog->Compile(Rules)) { Error = Rules.Error; return false; }
		CompiledCatalog = Catalog; CompiledRevision = Catalog->Revision;
	}
	if (!Catalog->Resolve(Rules, Build, PreparedResolved, Error)) return false;
	PreparedBuild = Build;
	PreparedKeepConfigurations.Reset();
	const bool bCanReuse = AppliedCatalog == Catalog && AppliedCatalogRevision == Catalog->Revision && AppliedBuild.MatchEpoch == Build.MatchEpoch;
	for (FName ConfigurationId : PreparedResolved.GroupConfigurationIds)
	{
		const auto& Definition = *Catalog->FindGroup(ConfigurationId);
		if (!Definition.bExecutable) { Error = ConfigurationId.ToString() + TEXT(" has no implemented gameplay."); DiscardPreparedBuild(); return false; }
		if (bCanReuse && Groups.ContainsByPredicate([ConfigurationId](const auto& Group) { return Group.ConfigurationId == ConfigurationId; }))
		{ PreparedKeepConfigurations.Add(ConfigurationId); continue; }
		auto& Group = StagedGroups.AddDefaulted_GetRef();
		Group.ConfigurationId = ConfigurationId; Group.GroupId = Definition.GroupId;
		for (const auto& Mount : Definition.Mounts)
		{
			UClass* Class = Mount.PartClass.LoadSynchronous();
			const auto* CDO = Class ? Class->GetDefaultObject<UGuLiStrikeShipPartComponent>() : nullptr;
			const auto* Occupant = Ship().GetPartAt(Mount.SocketName);
			if (!CDO || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
				|| !Ship().GetHullMeshComponent()->DoesSocketExist(Mount.SocketName) || !CDO->CanAttachToSocket(Mount.SocketName)
				|| (Occupant && !OwnsPart(Occupant)))
			{
				Error = FString::Printf(TEXT("Cannot prepare %s at %s: class, socket compatibility, or occupancy failed."), *ConfigurationId.ToString(), *Mount.SocketName.ToString());
				DiscardPreparedBuild(); return false;
			}
			auto* Part = NewObject<UGuLiStrikeShipPartComponent>(&Ship(), Class);
			Group.Parts.Add(Part);
			Part->SetAutoActivate(false);
			Part->SetHiddenInGame(true, true);
			Part->SetupAttachment(Ship().GetHullMeshComponent(), Mount.SocketName);
			Part->SetRelativeTransform(CDO->PartRelativeTransform);
			Part->RegisterComponent();
			if (!Part->IsRegistered() || !Part->IsVisualMeshReady())
			{
				Error = TEXT("Part visual resources could not be prepared: ") + Mount.SocketName.ToString();
				DiscardPreparedBuild(); return false;
			}
		}
		for (const auto& Ability : Definition.Capabilities)
		{
			auto* Capability = NewObject<UGuLiShipCapabilityComponent>(&Ship(), Ability.ComponentClass);
			Group.Capabilities.Add(Capability);
			Capability->SetAutoActivate(false);
			Capability->Prepare(Group.GroupId, Ability.CapabilityId, Ability.Configuration);
			Capability->RegisterComponent();
		}
	}
	bPrepared = true;
	return true;
}

void UGuLiShipAssemblyComponent::DiscardPreparedBuild()
{
	for (auto& Group : StagedGroups)
	{
		for (const auto& Capability : Group.Capabilities) { Capability->ReleaseCapability(); Capability->DestroyComponent(); }
		for (const auto& Part : Group.Parts) Part->DestroyComponent();
	}
	StagedGroups.Reset(); bPrepared = false;
}

void UGuLiShipAssemblyComponent::CommitPreparedBuild()
{
	check(bPrepared && !bCommitting);
	bCommitting = true;
	TArray<FGuLiShipAssembledGroup> Removed;
	for (int32 Index = Groups.Num() - 1; Index >= 0; --Index)
	{
		if (!PreparedKeepConfigurations.Contains(Groups[Index].ConfigurationId))
		{
			Removed.Add(MoveTemp(Groups[Index])); Groups.RemoveAt(Index);
		}
	}
	TArray<UGuLiStrikeShipPartComponent*> PreviousParts, NewParts;
	for (auto& Group : Removed)
	{
		for (const auto& Capability : Group.Capabilities) { Capability->RuntimeChanged.RemoveAll(this); Capability->ReleaseCapability(); }
		for (const auto& Part : Group.Parts) if (Part) PreviousParts.Add(Part);
	}
	for (auto& Group : StagedGroups)
	{
		for (const auto& Capability : Group.Capabilities)
		{
			Capability->RuntimeChanged.AddUObject(this, &ThisClass::PublishRuntime);
			Capability->StateChanged.AddUObject(&Ship(), &AGuLiStrikeShip::HandleCapabilityStateChanged);
		}
		for (const auto& Part : Group.Parts) NewParts.Add(Part);
		Groups.Add(MoveTemp(Group));
	}
	StagedGroups.Reset();
	if (AppliedBuild.MatchEpoch != PreparedBuild.MatchEpoch) ActivationReceipts.Reset();
	AppliedBuild = PreparedBuild;
	AppliedResolved = MoveTemp(PreparedResolved);
	AppliedCatalog = Catalog; AppliedCatalogRevision = Catalog->Revision;
	Ship().CommitAssemblyParts(PreviousParts, NewParts);
	for (auto& Group : Removed)
	{
		for (const auto& Capability : Group.Capabilities) Capability->DestroyComponent();
		for (const auto& Part : Group.Parts) if (Part) Part->DestroyComponent();
	}
	if (GetOwner()->HasAuthority())
	{
		Manifest.Catalog = Catalog; Manifest.CatalogRevision = Catalog->Revision; Manifest.Build = AppliedBuild;
	}
	EvaluateRuntimeRequirements();
	bPrepared = false; bCommitting = false;
	if (GetOwner()->HasAuthority()) PublishRuntime(); else OnRep_Runtime();
	Ship().FinishAssemblyCommit();
}

bool UGuLiShipAssemblyComponent::IsAvailableForChoice() const
{
	return !GetOwner()->IsActorBeingDestroyed() && Ship().IsPlayerControlled() && Ship().GetCombatHealthComponent()->IsAlive();
}
bool UGuLiShipAssemblyComponent::OwnsSocket(FName Socket) const
{
	for (const auto& Group : Groups) for (const auto& Mount : AppliedCatalog->FindGroup(Group.ConfigurationId)->Mounts)
		if (Mount.SocketName == Socket) return true;
	return false;
}
bool UGuLiShipAssemblyComponent::OwnsPart(const UGuLiStrikeShipPartComponent* Part) const
{
	for (const auto& Group : Groups) if (Group.Parts.Contains(Part)) return true;
	return false;
}
bool UGuLiShipAssemblyComponent::NotifyPartDestroyed(UGuLiStrikeShipPartComponent* Part)
{
	if (bCommitting) return false;
	for (auto& Group : Groups)
	{
		const int32 Index = Group.Parts.IndexOfByKey(Part);
		if (Index == INDEX_NONE) continue;
		Group.Parts[Index] = nullptr;
		if (GetOwner()->HasAuthority()) { EvaluateRuntimeRequirements(); PublishRuntime(); }
		return true;
	}
	return false;
}
void UGuLiShipAssemblyComponent::EvaluateRuntimeRequirements()
{
	FGuLiShipRuleContext Context = AppliedResolved.Context;
	Context.InstalledCapabilities.Reset();
	for (const auto& Group : Groups)
	{
		if (Group.Parts.Contains(nullptr)) continue;
		for (const auto& Capability : AppliedCatalog->FindGroup(Group.ConfigurationId)->Capabilities)
		{
			Context.InstalledCapabilities.Add(Capability.CapabilityId);
			for (auto Tag : Capability.ProvidedTags) Context.InstalledCapabilities.Add(Tag.GetTagName());
		}
	}
	for (auto& Group : Groups)
	{
		const auto& Definition = *AppliedCatalog->FindGroup(Group.ConfigurationId);
		for (int32 Index = 0; Index < Group.Capabilities.Num(); ++Index)
			Group.Capabilities[Index]->SetCapabilityEnabled(!Group.Parts.Contains(nullptr)
				&& UGuLiShipBuildCatalog::Evaluate(Definition.Capabilities[Index].RuntimeRequirements, Context));
	}
}

void UGuLiShipAssemblyComponent::PublishRuntime()
{
	if (bCommitting || !GetOwner()->HasAuthority()) return;
	Runtime.MatchEpoch = AppliedBuild.MatchEpoch; Runtime.BuildRevision = AppliedBuild.BuildRevision;
	Runtime.Capabilities.Reset();
	Runtime.MissingSockets.Reset();
	for (const auto& Group : Groups)
	{
		for (const auto& Capability : Group.Capabilities)
			Capability->BuildRuntimeView(Runtime.Capabilities.AddDefaulted_GetRef());
		const auto& Mounts = AppliedCatalog->FindGroup(Group.ConfigurationId)->Mounts;
		for (int32 Index = 0; Index < Group.Parts.Num(); ++Index)
			if (!Group.Parts[Index]) Runtime.MissingSockets.Add(Mounts[Index].SocketName);
	}
	GetOwner()->ForceNetUpdate();
}
void UGuLiShipAssemblyComponent::OnRep_Manifest()
{
	if (!HasBegunPlay() || !Manifest.Catalog || Manifest.Build.BuildRevision < 1
		|| (AppliedBuild.MatchEpoch == Manifest.Build.MatchEpoch && AppliedBuild.BuildRevision == Manifest.Build.BuildRevision
			&& AppliedCatalog == Manifest.Catalog && AppliedCatalogRevision == Manifest.CatalogRevision)) return;
	Catalog = Manifest.Catalog;
	FString Error;
	if (Manifest.CatalogRevision != Catalog->Revision || !PrepareBuild(Manifest.Build, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Ship assembly snapshot could not be prepared: %s"), *Error); return;
	}
	CommitPreparedBuild();
}
void UGuLiShipAssemblyComponent::OnRep_Runtime()
{
	if (Runtime.MatchEpoch != AppliedBuild.MatchEpoch || Runtime.BuildRevision != AppliedBuild.BuildRevision) return;
	for (auto& Group : Groups)
	{
		const auto& Mounts = AppliedCatalog->FindGroup(Group.ConfigurationId)->Mounts;
		for (int32 Index = 0; Index < Group.Parts.Num(); ++Index)
			if (Group.Parts[Index] && Runtime.MissingSockets.Contains(Mounts[Index].SocketName)) Group.Parts[Index]->DestroyComponent();
	}
	for (const auto& View : Runtime.Capabilities)
		if (auto* Capability = FindCapability(View.GroupId, View.CapabilityId))
		{
			Capability->ApplyRuntimeView(View);
			Capability->SetCapabilityEnabled(View.State == EGuLiShipCapabilityState::Enabled);
		}
}
UGuLiShipCapabilityComponent* UGuLiShipAssemblyComponent::FindCapability(FName GroupId, FName CapabilityId) const
{
	for (const auto& Group : Groups) if (Group.GroupId == GroupId)
		for (const auto& Capability : Group.Capabilities) if (Capability->GetCapabilityId() == CapabilityId) return Capability;
	return nullptr;
}
void UGuLiShipAssemblyComponent::ServerRequestActivation_Implementation(FGuid RequestId, FName GroupId, FName CapabilityId,
	FName ActionId, int64 MatchEpoch, int64 BuildRevision)
{
	FGuLiShipCapabilityActivationResult Result; Result.RequestId = RequestId;
	if (const auto* Previous = ActivationReceipts.Find(RequestId))
	{
		if (Previous->GroupId == GroupId && Previous->CapabilityId == CapabilityId && Previous->ActionId == ActionId
			&& Previous->MatchEpoch == MatchEpoch && Previous->BuildRevision == BuildRevision) Result = Previous->Result;
		else Result.Reason = TEXT("Activation request ID was already used for different content.");
		ClientResolveActivation(Result); return;
	}
	if (!RequestId.IsValid() || !IsAvailableForChoice() || MatchEpoch != AppliedBuild.MatchEpoch || BuildRevision != AppliedBuild.BuildRevision)
		Result.Reason = TEXT("Activation requires the current owned Ship, match and build revision.");
	else if (auto* Capability = FindCapability(GroupId, CapabilityId); Capability && Capability->IsCapabilityEnabled())
		Result.bAccepted = Capability->RequestActivation(ActionId, Result.Reason);
	else Result.Reason = TEXT("Requested capability is absent or suspended.");
	if (RequestId.IsValid()) ActivationReceipts.Add(RequestId, {GroupId, CapabilityId, ActionId, MatchEpoch, BuildRevision, Result});
	ClientResolveActivation(Result);
}
void UGuLiShipAssemblyComponent::ClientResolveActivation_Implementation(const FGuLiShipCapabilityActivationResult& Result)
{ OnActivationResolved.Broadcast(Result); }
void UGuLiShipAssemblyComponent::ReleaseBuild()
{
	bCommitting = true;
	DiscardPreparedBuild();
	for (auto& Group : Groups)
	{
		for (const auto& Capability : Group.Capabilities) { Capability->RuntimeChanged.RemoveAll(this); Capability->ReleaseCapability(); Capability->DestroyComponent(); }
		for (const auto& Part : Group.Parts) if (Part) Part->DestroyComponent();
	}
	Groups.Reset(); bCommitting = false;
}
