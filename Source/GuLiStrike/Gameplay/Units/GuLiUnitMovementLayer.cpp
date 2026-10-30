#include "Gameplay/Units/GuLiUnitMovementLayer.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Actor.h"

TOptional<EGuLiUnitMovementLayer> GuLiUnitMovement::LayerForActor(const AActor* Actor)
{
    for (int32 Depth=0;Actor && Depth<4;++Depth)
    {
        if (const auto* Binding=UGuLiHybridActorComponent::Find(Actor))
        {
            const auto& State=Binding->GetState();
            if (State.Key.Kind==EGuLiHybridObjectKind::GroundPlayer) return EGuLiUnitMovementLayer::Ground;
            if (State.Key.Kind!=EGuLiHybridObjectKind::Soldier && State.Key.Kind!=EGuLiHybridObjectKind::Miner
                && State.Key.Kind!=EGuLiHybridObjectKind::Engineer) return {};
            const FString Name=State.Definition.ToString();
            const auto* Data=Actor->GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>();
            const auto* Unit=Data && Name.StartsWith(TEXT("unit/"))?Data->FindDefinition(FCString::Atoi(*Name.Mid(5))):nullptr;
            return Unit?TOptional<EGuLiUnitMovementLayer>(Unit->MovementLayer):TOptional<EGuLiUnitMovementLayer>();
        }
        Actor=Actor->GetAttachParentActor()?Actor->GetAttachParentActor():Actor->GetOwner();
    }
    return {};
}

void GuLiUnitMovement::IgnoreOtherLayerBodies(UWorld& World,FCollisionQueryParams& Params,
    const FVector& Center,float Radius,EGuLiUnitMovementLayer Layer)
{
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_Pawn);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FOverlapResult> Hits;
    World.OverlapMultiByObjectType(Hits,Center,FQuat::Identity,Objects,FCollisionShape::MakeSphere(Radius),Params);
    for (const auto& Hit:Hits)
        if (const auto Other=LayerForActor(Hit.GetActor());Other.IsSet() && Other.GetValue()!=Layer)
            Params.AddIgnoredActor(Hit.GetActor());
}
