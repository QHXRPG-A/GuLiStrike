#include "Gameplay/Building/GuLiPioneerBarracksActor.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Gameplay/Models/GuLiModelPresentationComponent.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TimelineComponent.h"
#include "Engine/World.h"
#include "Gameplay/Navigation/GuLiCircularNavigationComponent.h"

AGuLiPioneerBarracksActor::AGuLiPioneerBarracksActor()
{
    Presentation=CreateDefaultSubobject<UChildActorComponent>(TEXT("BarracksPresentation"));
    Presentation->SetupAttachment(RootComponent);
}
bool AGuLiPioneerBarracksActor::InitializeFromDefinition(int32 InDefinitionId,EGuLiTeam InTeam,const FGuid& Builder,
    int32 Territory,EGuLiBuildingOrigin Origin,bool bCompleted)
{
    if (!Super::InitializeFromDefinition(InDefinitionId,InTeam,Builder,Territory,Origin,bCompleted)) return false;
    const auto& D=FindComponentByClass<UGuLiBuildingLifecycleComponent>()->GetDefinition();
    Presentation->SetRelativeLocation(FVector(0,0,-D.CollisionExtent.Z));
    Presentation->SetRelativeScale3D(FVector(D.PresentationScale));
    Presentation->SetChildActorClass(D.PresentationClass);
    return true;
}
void AGuLiPioneerBarracksActor::BeginPlay()
{
    Super::BeginPlay();
    auto* Child=Presentation->GetChildActor();
    UGuLiModelPresentationComponent::InstallForPresentationActor(Child);
    TArray<AActor*> Parts;GetAllChildActors(Parts,true);Parts.Add(this);
    for (auto* Part:Parts)
    {
        TInlineComponentArray<UPrimitiveComponent*> Primitives(Part);
        for (auto* Primitive:Primitives)
        {
            if (Primitive->IsA<UGuLiCircularNavigationComponent>()) continue;
            Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Primitive->SetGenerateOverlapEvents(false);
            Primitive->SetCanEverAffectNavigation(false);
        }
        TInlineComponentArray<UTimelineComponent*> Timelines(Part);
        for (auto* Timeline:Timelines) Timeline->Stop();
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Part);
        for (auto* Mesh:Meshes) if (Mesh->GetFName()==TEXT("Door")) Mesh->SetPosition(3.f,false);
    }
    FindComponentByClass<UGuLiBuildingConstructionVisualComponent>()->RefreshVisualSources();
}
FVector AGuLiPioneerBarracksActor::GetExitLocation(float UnitRadius) const
{
    const auto& Life=*FindComponentByClass<UGuLiBuildingLifecycleComponent>();
    const auto& Circles=GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>()->GetAvoidanceCatalog().Buildings.FindChecked(Life.GetState().DefinitionId);
    float Radius=0;
    for (const auto& C:Circles) if (C.PhaseMask&2) Radius=FMath::Max(Radius,float(C.LocalCenter.Size2D())+C.Radius);
    return GetBuildingGroundLocation()+GetActorForwardVector()*(Radius+UnitRadius+50.f);
}
static const FGuLiHybridBuildingArchetypeRegistration PioneerBarracksArchetype({TEXT("PlacedBuilding"),50,
    [](const FGuLiBuildingDefinition& D){return D.Category==EGuLiBuildingCategory::PioneerBarracks;},
    []()->UClass* {return AGuLiPioneerBarracksActor::StaticClass();}});
