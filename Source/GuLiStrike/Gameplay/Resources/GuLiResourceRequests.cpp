#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Engine/World.h"
UGuLiResourceWorldSubsystem::~UGuLiResourceWorldSubsystem()=default;
FGuLiHybridObjectKey UGuLiResourceWorldSubsystem::GetSettlementKey(const AActor* Actor) const
{
    const auto* Binding=UGuLiHybridActorComponent::Find(Actor);
    return Binding?Binding->GetObjectKey():FGuLiHybridObjectKey{};
}
uint64 UGuLiResourceWorldSubsystem::GetSettlementTick() const
{
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    return H?H->GetLogicalTick():0;
}
FGuid UGuLiResourceWorldSubsystem::SubmitResourceSettlement(const FGuLiHybridResourceRequest& Request)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    return H?H->SubmitResourceOperation(Request):FGuid{};
}
