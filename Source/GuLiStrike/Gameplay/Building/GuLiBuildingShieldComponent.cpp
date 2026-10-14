#include "Gameplay/Building/GuLiBuildingShieldComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"

UGuLiBuildingShieldComponent::UGuLiBuildingShieldComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}
void UGuLiBuildingShieldComponent::InitializeShield()
{
    const auto* Life=GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
    bGenerator=Life && Life->GetDefinition().Category==EGuLiBuildingCategory::ShieldGenerator;
}
bool UGuLiBuildingShieldComponent::IsShieldEnabled() const
{
    const auto* Life=GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    return bGenerator && Life && Binding && Life->IsCompleted()
        && GuLiResources::IsPlayableTeam(Life->GetTeam())
        && (Life->GetState().Origin!=EGuLiBuildingOrigin::Gift || Binding->GetState().bSupplied);
}
