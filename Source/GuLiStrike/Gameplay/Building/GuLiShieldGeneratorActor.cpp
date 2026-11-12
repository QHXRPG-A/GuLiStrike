#include "Gameplay/Building/GuLiShieldGeneratorActor.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Engine/World.h"

AGuLiShieldGeneratorActor::AGuLiShieldGeneratorActor()
{
    Inventory=CreateDefaultSubobject<UGuLiResourceInventoryComponent>(TEXT("ResourceInventory"));
    Inventory->bProcessing=true;
}
void AGuLiShieldGeneratorActor::BeginPlay()
{
    Super::BeginPlay();
    const auto& Life=*FindComponentByClass<UGuLiBuildingLifecycleComponent>();
    const auto& Circles=GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>()->GetAvoidanceCatalog().Buildings.FindChecked(Life.GetState().DefinitionId);
    for (const auto& Circle:Circles) if (Circle.PhaseMask&2)
        BoundaryRadius=FMath::Max(BoundaryRadius,float(Circle.LocalCenter.Size2D())+Circle.Radius);
    GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->RegisterDropoff(*this);
}
void AGuLiShieldGeneratorActor::EndPlay(EEndPlayReason::Type Reason)
{
    GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->UnregisterDropoff(*this);
    Super::EndPlay(Reason);
}
FGuLiControllableActorId AGuLiShieldGeneratorActor::GetStableActorId() const
{
    return FGuLiControllableActorId(FindComponentByClass<UGuLiBuildingLifecycleComponent>()->GetState().InstanceId);
}
FGuLiResourceFactoryPrivateState AGuLiShieldGeneratorActor::MakePrivateState() const
{
    FGuLiResourceFactoryPrivateState State;
    State.StableActorId=GetStableActorId();State.QueuedRawAmount=GetQueuedRawAmount();return State;
}
TArray<FVector> AGuLiShieldGeneratorActor::GetUnloadPoints(FVector From,float VehicleRadius,float ContactMargin) const
{
    // The existing 50 cm arrival tolerance remains outside the building boundary.
    const float Radius=BoundaryRadius+VehicleRadius+ContactMargin+50.f;
    const int32 Count=FMath::Max(8,FMath::FloorToInt(2.f*PI*Radius/(2.f*VehicleRadius+ContactMargin)));
    const FVector Center=GetBuildingGroundLocation();
    TArray<FVector> Points;Points.Reserve(Count);
    for (int32 I=0;I<Count;++I)
    {
        const float Angle=2.f*PI*I/Count;
        Points.Add(Center+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0));
    }
    Points.StableSort([&](const FVector& A,const FVector& B){return FVector::DistSquared2D(From,A)<FVector::DistSquared2D(From,B);});
    return Points;
}
static const FGuLiHybridBuildingArchetypeRegistration ShieldArchetype({TEXT("PlacedBuilding"),50,
    [](const FGuLiBuildingDefinition& D){return D.Category==EGuLiBuildingCategory::ShieldGenerator;},
    []()->UClass* {return AGuLiShieldGeneratorActor::StaticClass();}});
