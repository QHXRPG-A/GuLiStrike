#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UGuLiHybridActorComponent::UGuLiHybridActorComponent()
{ PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(false); }
void UGuLiHybridActorComponent::Install(const FGuLiHybridObjectState& InState,bool bLocalSimulation)
{
    auto* World=GetWorld(); auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Actor=GetOwner();
    if (Hybrid && Actor && State.Key.IsValid() && State.Key!=InState.Key) Hybrid->UnbindActor(State.Key,*Actor);
    State=InState; bLocal=bLocalSimulation;
    if (Hybrid && Actor && !Hybrid->BindActor(State.Key,*Actor,Cast<IGuLiEngineeringVehicle>(Actor)?State.Ordinal:0)) Hybrid->Suspend(TEXT("Conflicting local actor or ordinal for an object life"));
}
void UGuLiHybridActorComponent::EndPlay(EEndPlayReason::Type Reason)
{
    auto* World=GetWorld(); auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (Hybrid && GetOwner()) Hybrid->UnbindActor(State.Key,*GetOwner());
    Super::EndPlay(Reason);
}
bool UGuLiHybridActorComponent::CanSimulate() const
{
    const auto* World=GetWorld(); const auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    return Hybrid && State.Key.IsValid() && bLocal && Hybrid->HasSimulation(State.Key) && !Hybrid->IsBattlePaused();
}
bool UGuLiHybridActorComponent::CanControl() const
{
    const auto* World=GetWorld(); const auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    return Hybrid && Hybrid->HasControl(State.Key) && !Hybrid->IsBattlePaused();
}
double UGuLiHybridActorComponent::GetBattleSeconds() const { return BattleSeconds(GetOwner()); }
const UGuLiHybridActorComponent* UGuLiHybridActorComponent::Find(const AActor* Actor)
{ return IsValid(Actor)?Actor->FindComponentByClass<UGuLiHybridActorComponent>():nullptr; }
bool UGuLiHybridActorComponent::HasSimulation(const AActor* Actor)
{
    if (!IsValid(Actor)) return false;
    const auto* Identity=Find(Actor); return Identity && Identity->CanSimulate();
}
double UGuLiHybridActorComponent::BattleSeconds(const AActor* Actor)
{
    auto* World=IsValid(Actor)?Actor->GetWorld():nullptr;
    if (const auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr)
    {const auto* Binding=Find(Actor);return Binding?double(Hybrid->GetObjectLogicalTick(Binding->GetObjectKey()))/60.:Hybrid->GetLogicalSeconds();}
    return World?World->GetTimeSeconds():0.;
}
