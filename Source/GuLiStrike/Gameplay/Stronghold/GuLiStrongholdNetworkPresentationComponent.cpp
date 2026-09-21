#include "Gameplay/Stronghold/GuLiStrongholdNetworkPresentationComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

UGuLiStrongholdNetworkPresentationComponent::UGuLiStrongholdNetworkPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickInterval = .25f;
}
void UGuLiStrongholdNetworkPresentationComponent::ClearPresentation()
{
	for (UStaticMeshComponent* Primitive : Primitives) Primitive->DestroyComponent();
	Primitives.Reset(); Materials.Reset();
}
void UGuLiStrongholdNetworkPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearPresentation(); Super::EndPlay(Reason);
}
void UGuLiStrongholdNetworkPresentationComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt,TickType,TickFunction);
	const auto& WorldState = *CastChecked<AGuLiResourceWorldState>(GetOwner());
	const auto& Snapshot = WorldState.GetTransportNetwork();
	if (!WorldState.IsAuthorityReady() || Snapshot.Revision == AppliedRevision) return;
	ClearPresentation();
	TMap<int32,FVector> Positions;
	TMap<int32,UMaterialInstanceDynamic*> NodeMaterials;
	auto AddMesh = [this](int32 VfxId, const FVector& Position, const FRotator& Rotation,
		const FVector& Scale, UMaterialInstanceDynamic* Material)
	{
		auto* Mesh = NewObject<UStaticMeshComponent>(GetOwner());
		Mesh->SetStaticMesh(GuLiVfx::Load<UStaticMesh>(this, VfxId));
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCanEverAffectNavigation(false); Mesh->SetCastShadow(false);
		Mesh->SetWorldLocationAndRotation(Position,Rotation); Mesh->SetWorldScale3D(GuLiVfx::Scale(this, VfxId, Scale));
		Mesh->SetMaterial(0,Material); Mesh->RegisterComponent(); Primitives.Add(Mesh);
	};
	for (const auto& Node : Snapshot.Nodes)
	{
		const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdTransit(Node.TransitFieldId);
		auto* Energy = GuLiVfx::Load<UMaterialInterface>(this, Config.EnergyVfxId); if (!Energy) continue;
		auto* Material = UMaterialInstanceDynamic::Create(Energy,this);
		Material->SetVectorParameterValue(TEXT("Tint"),Node.Team == EGuLiTeam::Red
			? FLinearColor(1,.025f,.005f,1) : FLinearColor(0,.55f,1,1));
		Material->SetScalarParameterValue(TEXT("Opacity"),.35f); Materials.Add(Material);
		const FVector Position = Node.GroundLocation+FVector(0,0,Config.LaneHeight);
		Positions.Add(Node.TerritoryIndex,Position); NodeMaterials.Add(Node.TerritoryIndex,Material);
		AddMesh(GuLiVfxIds::TransitNode,Position,FRotator::ZeroRotator,GuLiVfx::Scale(this, Config.EnergyVfxId),Material);
	}
	for (const auto& Edge : Snapshot.Edges)
	{
		if (!Positions.Contains(Edge.A) || !Positions.Contains(Edge.B)) continue;
		const FVector A = Positions[Edge.A], B = Positions[Edge.B];
		AddMesh(GuLiVfxIds::TransitEdge,(A+B)*.5,FRotationMatrix::MakeFromZ(B-A).Rotator(),
			FVector(1,1,FVector::Distance(A,B)/100.),NodeMaterials[Edge.A]);
	}
	AppliedRevision = Snapshot.Revision;
}
