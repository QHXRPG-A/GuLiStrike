#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
    if (!FGuLiHybridManifest::Read(UGuLiNetworkSettings::Resolve().ManifestFile,Manifest,Error))
    { GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(Error); return; }
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
                if (!FGuLiHybridAdapterRegistry::Resolve(Weak->GetManifest(),State,Family,Why))
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
    Manifest={}; Super::Deinitialize();
}
