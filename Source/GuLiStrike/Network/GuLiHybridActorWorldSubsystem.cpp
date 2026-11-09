#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridEntityState.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
TAutoConsoleVariable<int32> EntityLayoutCache(TEXT("gs.Hybrid.EntityLayoutCache"),1,
    TEXT("Prepare immutable entity capability names once per World at startup. 0 scans the manifest at each object birth for comparison."));
}
bool UGuLiHybridActorWorldSubsystem::IsEntityLayoutCacheEnabled() const
{return EntityLayoutCache.GetValueOnGameThread()!=0;}
const TArray<FName>* UGuLiHybridActorWorldSubsystem::FindEntityCapabilityNames(FName Definition) const
{return IsEntityLayoutCacheEnabled()?EntityCapabilityNames.Find(Definition):nullptr;}

bool UGuLiHybridActorWorldSubsystem::ResolveAdapter(const FGuLiHybridObjectState& State,FGuLiHybridAdapterFamily& Out,FString& Error) const
{
    const uint64 Revision=FGuLiHybridAdapterRegistry::GetRevision();
    if (AdapterRegistryRevision!=Revision) {AdapterDefinitions.Reset();AdapterRegistryRevision=Revision;}
    if (const auto* Cached=AdapterDefinitions.Find(State.Definition);Cached && Cached->Kind==State.Key.Kind)
    {Out=*Cached;Error.Reset();return true;}
    // The manifest belongs to this World and is immutable. Validate each exact
    // definition once per native registry revision, including its declared kind.
    if (!FGuLiHybridAdapterRegistry::Resolve(Manifest,State,Out,Error)) return false;
    AdapterDefinitions.Add(State.Definition,Out);return true;
}

bool UGuLiHybridActorWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* World=Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && !FParse::Param(FCommandLine::Get(),TEXT("GuLiAcceptanceStandby"))
        && UGuLiNetworkSettings::IsHybrid(World) && Super::ShouldCreateSubsystem(Outer);
}
void UGuLiHybridActorWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();
    FString Error;
    if (!FGuLiHybridManifest::Read(UGuLiNetworkSettings::Resolve(GetWorld()).ManifestFile,Manifest,Error))
    { GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(Error); return; }
    // Manifest is private and immutable until this World is deinitialized.
    const double IndexStart=FPlatformTime::Seconds();
    FGuLiHybridEntityState::BuildDefinitionIndex(Manifest,EntityCapabilityNames);
    EntityCapabilityIndexBuildMilliseconds=(FPlatformTime::Seconds()-IndexStart)*1000.;
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    TArray<FGuLiHybridAdapterFamily> Families;
    if (!FGuLiHybridAdapterRegistry::GetFamilies(Families,Error))
    {Hybrid->Suspend(Error);return;}
    TSet<EGuLiHybridObjectKind> Kinds;
    for (const auto& Family:Families)
    {
        if (Family.Install && !Family.Install(*this))
        {Hybrid->Suspend(TEXT("Could not install gameplay shared-state systems: ")+Family.Name.ToString());return;}
        Kinds.Add(Family.Kind);
    }
    for (const auto Kind:Kinds)
    {
        if (!Hybrid->RegisterFactory(Kind,*this,
            [Weak=TWeakObjectPtr<UGuLiHybridActorWorldSubsystem>(this)](const FGuLiHybridObjectState& State)->TSharedPtr<IGuLiHybridSimulationAdapter>
            {
                if (!Weak.IsValid()) return {};
                FGuLiHybridAdapterFamily Family;FString Why;
                if (!Weak->ResolveAdapter(State,Family,Why))
                {Weak->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(Why);return {};}
                auto Adapter=Family.Create(*Weak,State);
                if (!Adapter || Adapter->Version()!=Family.StateVersion)
                {Weak->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Adapter implementation version differs from its contract: ")+Family.Name.ToString());return {};}
                return Adapter;
            }))
        {Hybrid->Suspend(TEXT("A gameplay object kind already has another factory"));return;}
    }
}
void UGuLiHybridActorWorldSubsystem::Deinitialize()
{
    if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) Hybrid->UnregisterFactories(*this);
    AdapterDefinitions.Reset();AdapterRegistryRevision=0;
    EntityCapabilityNames.Reset();EntityCapabilityIndexBuildMilliseconds=0;Manifest={}; Super::Deinitialize();
}
