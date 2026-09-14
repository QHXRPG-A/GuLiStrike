#include "Gameplay/Stronghold/GuLiStrongholdNetworkPresentationComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

UGuLiStrongholdNetworkPresentationComponent::UGuLiStrongholdNetworkPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickInterval = .25f;
}
void UGuLiStrongholdNetworkPresentationComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt,TickType,TickFunction);
	auto& Resources = *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (!Resources.IsResourceWorldActive() || !Resources.IsRuntimeReady()) return;
	const auto& WorldState = *Resources.GetResourceWorldState();
	const auto& Edges = WorldState.GetTransportEdges();
	if (Lines.IsEmpty())
	{
		const int32 FieldId = UGuLiBuildingCatalog::LoadDefaultCatalog()->FindById(7)->GateFieldId;
		const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdGate(FieldId);
		auto* Energy = Config.EnergyMaterial.LoadSynchronous(); check(Energy);
		for (const auto& Edge : Edges)
		{
			const FVector A = Resources.GetTerritoryGroundLocation(Edge.X) + FVector(0,0,Config.LaneHeight);
			const FVector B = Resources.GetTerritoryGroundLocation(Edge.Y) + FVector(0,0,Config.LaneHeight);
			auto* Line = NewObject<UStaticMeshComponent>(GetOwner());
			Line->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
			Line->SetCollisionEnabled(ECollisionEnabled::NoCollision); Line->SetCanEverAffectNavigation(false); Line->SetCastShadow(false);
			Line->SetWorldLocation((A+B)*.5);
			Line->SetWorldRotation(FRotationMatrix::MakeFromZ(B-A).Rotator());
			Line->SetWorldScale3D(FVector(.8,.8,FVector::Distance(A,B)/100.));
			auto* Material = UMaterialInstanceDynamic::Create(Energy,this); Line->SetMaterial(0,Material);
			Line->RegisterComponent(); Lines.Add(Line); Materials.Add(Material);
		}
	}
	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		const auto& Edge = Edges[Index];
		const auto Team = WorldState.GetTerritoryOwner(Edge.X);
		const bool bOpen = Resources.CanUseStrongholdTransit(Edge.X,Team) && Resources.CanUseStrongholdTransit(Edge.Y,Team);
		Lines[Index]->SetVisibility(!WorldState.GetTerritories()[Edge.X].bEncircled && !WorldState.GetTerritories()[Edge.Y].bEncircled);
		Materials[Index]->SetScalarParameterValue(TEXT("Opacity"), bOpen ? .35f : .035f);
	}
}
