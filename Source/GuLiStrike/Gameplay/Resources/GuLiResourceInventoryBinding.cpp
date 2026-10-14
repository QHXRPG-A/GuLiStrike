#include "Gameplay/Resources/GuLiResourceInventoryComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceInventorySubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Engine/World.h"

bool UGuLiResourceInventoryComponent::ExportDefinition(FGuLiHybridManifestData& M,const FString& D,FString& Error) const
{
    const auto* Profile=GetDefault<UGuLiResourceInventorySettings>()->Entities.FindByPredicate([&](const auto& P){return P.Definition==FName(D);});
    const bool CargoEnabled=Profile?Profile->bCargo:bCargo,ProcessorEnabled=Profile?Profile->bProcessing:bProcessing;
    if (!CargoEnabled && !ProcessorEnabled) return true;
    const FString Owner=TEXT("resource/component/")+D;
    if (const auto* Previous=M.Strings.Find(Owner);Previous && *Previous!=GetName())
    {Error=TEXT("An object must have one inventory component combining its cargo/processing roles: ")+D;return false;}
    M.Strings.Add(Owner,GetName());
    const auto Add=[&](const TCHAR* Kind,double Capacity,double Rate)
    {
        const double Maximum=FCString::Strcmp(Kind,TEXT("cargo"))==0?1000000:10000000;
        if (!FMath::IsFinite(Capacity) || Capacity<1 || Capacity>Maximum || Capacity!=FMath::FloorToDouble(Capacity)
            || !FMath::IsFinite(Rate) || Rate<1./86400. || Rate>60.) return false;
        const FString P=TEXT("resource/profile/")+D+TEXT("/")+Kind+TEXT("/");
        M.Numbers.Add(P+TEXT("capacity"),Capacity);M.Numbers.Add(P+TEXT("period_ticks"),FMath::CeilToDouble(60./Rate));return true;
    };
    if ((CargoEnabled && !Add(TEXT("cargo"),Profile?Profile->CargoCapacity:bUseEconomyDefaults?M.Numbers.FindRef(TEXT("economy/CargoCapacity")):CargoCapacity,
            Profile?Profile->ExtractionRate:bUseEconomyDefaults?M.Numbers.FindRef(TEXT("economy/MiningRatePerSecond")):ExtractionRate))
        || (ProcessorEnabled && !Add(TEXT("processor"),Profile?Profile->ProcessingCapacity:ProcessingCapacity,
            Profile?Profile->ProcessingRate:bUseEconomyDefaults?M.Numbers.FindRef(TEXT("economy/FactoryProcessingRatePerSecond")):ProcessingRate)))
    {Error=TEXT("Invalid inventory component capacity or rate: ")+D;return false;}
    return true;
}
bool UGuLiResourceInventoryComponent::ConfigureFromManifest()
{
    const auto* B=UGuLiHybridActorComponent::Find(GetOwner());const auto* Actors=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
    const FName Definition=EntityKey.IsValid()?EntityDefinition:B?B->GetState().Definition:NAME_None;
    if (!Actors || Definition.IsNone()) return false;
    const auto& M=Actors->GetManifest();const auto D=Definition.ToString();
    // The signed data profile, shared by Actor and Mass hosts, determines the
    // enabled roles. The authored component supplies defaults when no profile exists.
    const bool CargoEnabled=M.Numbers.Contains(TEXT("resource/profile/")+D+TEXT("/cargo/capacity"));
    const bool ProcessorEnabled=M.Numbers.Contains(TEXT("resource/profile/")+D+TEXT("/processor/capacity"));
    if (!CargoEnabled && !ProcessorEnabled) return !bCargo && !bProcessing;
    bCargo=CargoEnabled;bProcessing=ProcessorEnabled;
    const auto Read=[&](const TCHAR* Role,bool Enabled,int32& Capacity,float& Rate)
    {
        const auto P=TEXT("resource/profile/")+D+TEXT("/")+Role+TEXT("/");
        const auto* C=M.Numbers.Find(P+TEXT("capacity"));const auto* T=M.Numbers.Find(P+TEXT("period_ticks"));
        if (!Enabled) return !C && !T;
        if (!C || !T || !FMath::IsFinite(*C) || !FMath::IsFinite(*T) || *C<1 || *C>10000000
            || *T<1 || *T>60*86400 || *C!=FMath::FloorToDouble(*C) || *T!=FMath::FloorToDouble(*T)) return false;
        Capacity=int32(*C);Rate=60.f/float(*T);return true;
    };
    if (!Read(TEXT("cargo"),bCargo,CargoCapacity,ExtractionRate) || !Read(TEXT("processor"),bProcessing,ProcessingCapacity,ProcessingRate)) return false;
    if (bProcessing) ConfigureProcessing(ProcessingRate,FName(*(TEXT("resource/process/")+D)));
    return true;
}
FGuid UGuLiResourceInventoryComponent::RequestExtraction(int32 Node,FGuid Token)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();const auto Key=GetInventoryKey();FGuLiHybridObjectState Object;
    if (!bCargo || !H || !H->HasSimulation(Key) || !H->GetObject(Key,Object) || !H->CanPublishFacts() || PendingCargo.IsValid() || PendingEvent.IsValid()
        || Node<=0 || !Token.IsValid() || int64(Cargo.Blue)+Cargo.Red>=CargoCapacity) return {};
    FGuLiHybridResourceRequest R;R.Source=Key;R.Event=FGuid::NewGuid();R.NodeId=Node;R.MiningToken=Token;
    R.Rule=FName(*(TEXT("resource/extract/")+Object.Definition.ToString()));R.Operation=EGuLiHybridResourceOperation::Extract;
    PendingCargo=H->SubmitResourceOperation(R);return PendingCargo;
}
FGuid UGuLiResourceInventoryComponent::RequestDelivery(AActor* Destination)
{
    const auto* T=UGuLiHybridActorComponent::Find(Destination);return T?RequestDeliveryKey(T->GetObjectKey()):FGuid();
}
FGuid UGuLiResourceInventoryComponent::RequestDeliveryKey(const FGuLiHybridObjectKey& Destination)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();const auto Key=GetInventoryKey();FGuLiHybridObjectState Source,Target;
    const auto* A=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
    if (!bCargo || !H || !A || !H->HasSimulation(Key) || !H->CanPublishFacts() || !H->GetObject(Key,Source) || !H->GetObject(Destination,Target)
        || PendingCargo.IsValid() || PendingEvent.IsValid() || int64(Cargo.Blue)+Cargo.Red<=0
        || !A->GetManifest().Numbers.Contains(TEXT("resource/profile/")+Target.Definition.ToString()+TEXT("/processor/capacity"))) return {};
    // Geometry/docking belongs to the caller's gameplay ability. Admission and
    // immutable beneficiary/capacity remain checked by the resource ledger.
    FGuLiHybridResourceRequest R;R.Source=Key;R.Target=Destination;R.Event=FGuid::NewGuid();
    R.Rule=FName(*(TEXT("resource/deliver/")+Source.Definition.ToString()+TEXT("/")+Target.Definition.ToString()));
    R.Operation=EGuLiHybridResourceOperation::Deliver;PendingCargo=H->SubmitResourceOperation(R);return PendingCargo;
}
namespace
{
const FGuLiHybridManifestRegistration InventoryProfiles({TEXT("ResourceInventoryProfiles"),EGuLiHybridManifestPhase::Derived,{TEXT("Resources"),TEXT("EntityInventories")},
    [](UWorld&,FGuLiHybridManifestData& M,FString& Error)
    {
        TArray<FGuLiHybridAdapterFamily> Families;if (!FGuLiHybridAdapterRegistry::GetFamilies(Families,Error)) return false;
        TArray<FString> Cargo,Processors;
        for (const auto& Pair:M.Strings) if (Pair.Key.StartsWith(TEXT("resource/component/")))
        {
            const auto D=Pair.Key.RightChop(19);const auto Family=FName(*M.Strings.FindRef(TEXT("adapter/definition/")+D));
            const auto* F=Families.FindByPredicate([&](const auto& V){return V.Name==Family;});
            if (!F) {Error=TEXT("Inventory has no registered simulation family: ")+D;return false;}
            if (F->OwnershipPolicy==EGuLiHybridOwnershipPolicy::ControllerPinned)
            {Error=TEXT("Resource simulation group conflicts with controller-pinned capability: ")+D;return false;}
            M.Numbers.Add(TEXT("ownership/definition/")+D+TEXT("/ownership_policy"),uint8(EGuLiHybridOwnershipPolicy::SharedGroup));
            M.Strings.Add(TEXT("ownership/definition/")+D+TEXT("/group"),TEXT("match/resources"));
            for (const auto* Role:{TEXT("cargo"),TEXT("processor")})
            {
                const auto P=TEXT("resource/profile/")+D+TEXT("/")+Role+TEXT("/");
                if (M.Numbers.Contains(P+TEXT("capacity")))
                {M.Numbers.Add(P+TEXT("kind"),uint8(F->Kind));(FCString::Strcmp(Role,TEXT("cargo"))==0?Cargo:Processors).Add(D);}
            }
        }
        Cargo.Sort();Processors.Sort();
        const auto Add=[&](FString ID,EGuLiHybridRuleKind Kind,const FString& Source,const FString& Target=FString())
        {auto& R=M.Rules.AddDefaulted_GetRef();R.Id=MoveTemp(ID);R.Kind=Kind;R.SourceDefinition=Source;R.TargetDefinition=Target;};
        for (const auto& D:Processors) Add(TEXT("resource/process/")+D,EGuLiHybridRuleKind::Process,D);
        for (const auto& D:Cargo)
        {
            Add(TEXT("resource/extract/")+D,EGuLiHybridRuleKind::Extract,D);
            Add(TEXT("resource/position/")+D,EGuLiHybridRuleKind::ClaimMiningPosition,D);
            Add(TEXT("resource/node/")+D,EGuLiHybridRuleKind::ClaimMiningNode,D);
            Add(TEXT("resource/release/")+D,EGuLiHybridRuleKind::ReleaseMiningPosition,D);
            for (const auto& T:Processors) Add(TEXT("resource/deliver/")+D+TEXT("/")+T,EGuLiHybridRuleKind::Deliver,D,T);
        }
        return true;
    }});
}
