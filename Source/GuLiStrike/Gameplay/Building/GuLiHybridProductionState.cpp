#include "Gameplay/Building/Private/GuLiHybridProductionState.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "GameFramework/Actor.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridActorComponent.h"

namespace
{
bool DecodeProduction(TConstArrayView<uint8> Bytes,FGuLiHybridProductionPrivateState& Out)
{FGuLiHybridStateArchive A(Bytes,32768);Out.Serialize(A);return A.Done();}
bool DecodePublic(TConstArrayView<uint8> Bytes,float& Cycle,bool& Producing)
{FGuLiHybridStateArchive A(Bytes,32);A.Value(Cycle);A.Value(Producing);return A.Done();}
const FGuLiHybridComponentCapabilityRegistration ProductionCapability({TEXT("Production"),4,
    []()->const UClass* {return UGuLiBuildingProductionComponent::StaticClass();},
    [](const UActorComponent& Component,TArray<uint8>& Bytes)
    {
        FGuLiHybridProductionPrivateState State;
        if (!CastChecked<UGuLiBuildingProductionComponent>(&Component)->CaptureHybridProduction(State)) return false;
        FGuLiHybridStateArchive A(32768);State.Serialize(A);return A.Finish(Bytes);
    },
    [](const UActorComponent& Component,TConstArrayView<uint8> Bytes)
    {
        FGuLiHybridProductionPrivateState State;return DecodeProduction(Bytes,State)
            && CastChecked<UGuLiBuildingProductionComponent>(&Component)->ValidateHybridProduction(State);
    },
    [](UActorComponent& Component,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        if (Stage!=EGuLiHybridRestoreStage::Link) return true;
        FGuLiHybridProductionPrivateState State;return DecodeProduction(Bytes,State)
            && CastChecked<UGuLiBuildingProductionComponent>(&Component)->RestoreHybridProduction(State);
    },nullptr,
    [](UActorComponent& Component,uint64,double Delta)
    {CastChecked<UGuLiBuildingProductionComponent>(&Component)->AdvanceHybridProduction(float(Delta));},nullptr,nullptr,
    [](const UActorComponent& Component,TArray<uint8>& Bytes)
    {
        const auto* C=CastChecked<UGuLiBuildingProductionComponent>(&Component);float Cycle=C->GetCycleSeconds();bool Producing=C->IsProducing();
        FGuLiHybridStateArchive A(32);A.Value(Cycle);A.Value(Producing);return A.Finish(Bytes);
    },
    [](const UActorComponent& Component,TConstArrayView<uint8> Bytes)
    {float Cycle=0;bool Producing=false;return DecodePublic(Bytes,Cycle,Producing) && CastChecked<UGuLiBuildingProductionComponent>(&Component)->ValidateHybridProductionMotion(Cycle,Producing);},
    [](UActorComponent& Component,TConstArrayView<uint8> Bytes)
    {float Cycle=0;bool Producing=false;return DecodePublic(Bytes,Cycle,Producing) && CastChecked<UGuLiBuildingProductionComponent>(&Component)->ApplyHybridProductionMotion(Cycle,Producing);},nullptr,nullptr,
    [](UActorComponent& C,uint64 E){CastChecked<UGuLiBuildingProductionComponent>(&C)->ResetResourceRequests(E);}});
}
bool UGuLiBuildingProductionComponent::CaptureHybridProduction(FGuLiHybridProductionPrivateState& Out) const
{
    Out={};Out.Cycle=CycleSeconds;Out.ObservedTeam=ObservedTeam;Out.Producing=bProducing;
    Out.GridRemaining=GridRemaining;Out.GridCandidate=GridCandidate;Out.GridRetryDelay=FMath::Max(0.,GridRetryAt-UGuLiHybridActorComponent::BattleSeconds(GetOwner()));
    for (const auto& J:HybridJobs) Out.Jobs.Add({J.Event,J.Target,J.Location,J.FacingYaw,J.bSubmitted});
    return ValidateHybridProduction(Out);
}
bool UGuLiBuildingProductionComponent::ValidateHybridProduction(const FGuLiHybridProductionPrivateState& State) const
{
    auto Copy=State;FGuLiHybridStateArchive A(32768);Copy.Serialize(A);
    const auto* Life=GetOwner()?GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>():nullptr;
    if (!A.Good() || !Life) return false;const auto& Definition=Life->GetDefinition();
    if (!State.Jobs.IsEmpty())
    {
        const auto* Host=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
        FGuLiHybridAdapterFamily Family;FString Error;
        if (!Host || !FGuLiHybridAdapterRegistry::ResolveDefinition(Host->GetManifest(),
            FString::Printf(TEXT("unit/%d"),Definition.ProductionUnitId),Family,Error)
            || State.Jobs.ContainsByPredicate([&](const auto& J){return J.Target.Kind!=Family.Kind;})) return false;
    }
    return State.GridRemaining+State.Jobs.Num()<=FMath::Max(0,Definition.ProductionCount)
        && State.Cycle<=FMath::Max(0.f,Definition.ProductionSeconds)
        && (Definition.HasProductionCapability() || (!State.Producing && State.Jobs.IsEmpty()));
}
bool UGuLiBuildingProductionComponent::RestoreHybridProduction(const FGuLiHybridProductionPrivateState& State)
{
    if (!ValidateHybridProduction(State)) return false;
    for (const auto& J:HybridJobs) ReleaseGridBirth(J.Target,!J.bSubmitted);
    CycleSeconds=State.Cycle;ObservedTeam=State.ObservedTeam;bProducing=State.Producing;HybridJobs.Reset();
    GridRemaining=State.GridRemaining;GridCandidate=State.GridCandidate;GridRetryAt=UGuLiHybridActorComponent::BattleSeconds(GetOwner())+State.GridRetryDelay;
    for (const auto& J:State.Jobs) HybridJobs.Add({J.Event,J.Target,J.Location,J.FacingYaw,J.Submitted});
    RecoverGridBirths();
    return true;
}
bool UGuLiBuildingProductionComponent::ValidateHybridProductionMotion(float Cycle,bool Producing) const
{
    const auto* Life=GetOwner()?GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>():nullptr;
    if (!Life || !FMath::IsFinite(Cycle) || Cycle<0 || Cycle>FMath::Max(0.f,Life->GetDefinition().ProductionSeconds)
        || (Producing && !Life->GetDefinition().HasProductionCapability())) return false;
    return true;
}
bool UGuLiBuildingProductionComponent::ApplyHybridProductionMotion(float Cycle,bool Producing)
{
    if (!ValidateHybridProductionMotion(Cycle,Producing)) return false;
    CycleSeconds=Cycle;bProducing=Producing;return true;
}
