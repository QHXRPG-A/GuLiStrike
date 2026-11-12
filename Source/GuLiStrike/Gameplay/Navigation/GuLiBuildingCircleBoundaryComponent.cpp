#include "Gameplay/Navigation/GuLiBuildingCircleBoundaryComponent.h"
#include "Gameplay/Navigation/GuLiCircularNavigationComponent.h"
#include "Gameplay/Navigation/GuLiCircularBoundaries.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Navigation/GuLiCircleCollisionFiltering.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiGroundAccessRampComponent.h"
#include "NavModifierComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UGuLiBuildingCircleBoundaryComponent::UGuLiBuildingCircleBoundaryComponent()
{PrimaryComponentTick.bCanEverTick=false;}

bool UGuLiBuildingCircleBoundaryComponent::Install(UGuLiBuildingLifecycleComponent& Life,const FGuLiAvoidanceCatalog& Catalog)
{
    auto* Owner=Life.GetOwner();const auto* Profile=Catalog.Buildings.Find(Life.GetState().DefinitionId);
    if (!Owner || !Profile || !Life.GetState().InstanceId) return false;
    auto* Component=Owner->FindComponentByClass<UGuLiBuildingCircleBoundaryComponent>();
    if (!Component)
    {
        Component=NewObject<UGuLiBuildingCircleBoundaryComponent>(Owner,NAME_None,RF_Transient);
        Component->Lifecycle=&Life;Component->Profile=*Profile;Component->Definition=Life.GetState().DefinitionId;
        Owner->AddInstanceComponent(Component);Component->RegisterComponent();
        Component->Navigation=NewObject<UGuLiCircularNavigationComponent>(Owner,NAME_None,RF_Transient);
        Owner->AddInstanceComponent(Component->Navigation);Component->Navigation->RegisterComponent();
        Life.OnConstructionStateChanged.AddUObject(Component,&ThisClass::RefreshBoundary);
    }
    Component->RefreshBoundary();return true;
}

void UGuLiBuildingCircleBoundaryComponent::RefreshBoundary()
{
    if (!Lifecycle || !Navigation) return;
    const auto& State=Lifecycle->GetState();const auto Transform=GetOwner()->GetActorTransform();
    if (LastPhase==uint8(State.Phase) && LastTransform.Equals(Transform)) return;
    RefreshCollisionSources();
    auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();if (!Registry) return;
    const uint64 NextSource=GuLiCircleAvoidance::BuildingNamespace|State.InstanceId;
    if (Source && Source!=NextSource) Registry->RemoveCircleGroup(Source);
    Source=NextSource;
    TArray<GuLiCircleAvoidance::FCircle> Circles;
    const uint8 Phase=State.Phase==EGuLiBuildingPhase::UnderConstruction?1:State.Phase==EGuLiBuildingPhase::Completed?2:0;
    if (Phase)
    {
        // A small immutable actor-local profile replaces repeated DataTable access.
        FGuLiAvoidanceCatalog Input;Input.Buildings.Add(Definition,Profile);
        if (!ensure(GuLiCircleAvoidance::BuildBuildingCircles(Input,Definition,State.InstanceId,
            Phase,Transform,++Version,Circles))) return;
    }
    if (!Registry->ReplaceCircleGroup(Source,Circles)) return;
    Navigation->SetCircles(Circles);LastPhase=uint8(State.Phase);LastTransform=Transform;
}

void UGuLiBuildingCircleBoundaryComponent::RefreshCollisionSources()
{
    if (!Lifecycle) return;
    TArray<AActor*> Parts;GetOwner()->GetAllChildActors(Parts,true);Parts.Add(GetOwner());
    for (auto* Part:Parts)
    {
        TInlineComponentArray<UPrimitiveComponent*> Primitives(Part);
        for (auto* Primitive:Primitives)
        {
            if (Primitive->IsA<UGuLiGroundAccessRampComponent>()) continue;
            Primitive->SetCanEverAffectNavigation(false);
            Primitive->SetMaskFilterOnBodyInstance(Primitive->BodyInstance.GetMaskFilter()|GuLiCircleCollisionFiltering::CircularBoundaryMask);
        }
        TInlineComponentArray<UNavModifierComponent*> OldModifiers(Part);
        for (auto* Modifier:OldModifiers) Modifier->SetNavigationRelevancy(false);
    }
}

void UGuLiBuildingCircleBoundaryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Lifecycle) Lifecycle->OnConstructionStateChanged.RemoveAll(this);
    if (Source) if (auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()) Registry->RemoveCircleGroup(Source);
    Super::EndPlay(Reason);
}
