#include "Gameplay/Stronghold/GuLiStrongholdGatePresentationComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdGateComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

void UGuLiStrongholdGatePresentationComponent::InitializePresentation(UGuLiStrongholdGateComponent& Gate)
{
	check(GetWorld()->GetNetMode() != NM_DedicatedServer);
	const auto& Config = Gate.GetConfig();
	if (!Ground)
	{
		Ground = NewObject<UDecalComponent>(GetOwner());
		Ground->SetWorldRotation(FRotator(-90,0,0)); Ground->FadeScreenSize = 0;
		Ground->RegisterComponent();
		auto* Material = Config.GateMaterial.LoadSynchronous(); check(Material);
		GroundMaterial = UMaterialInstanceDynamic::Create(Material,this);
		Ground->SetDecalMaterial(GroundMaterial);
		GroundMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0,.6,1));
		GroundMaterial->SetScalarParameterValue(TEXT("Opacity"), .7f);
		GroundMaterial->SetScalarParameterValue(TEXT("Progress"), 1.f);
		AirNode = NewObject<UStaticMeshComponent>(GetOwner());
		AirNode->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		AirNode->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AirNode->SetCanEverAffectNavigation(false); AirNode->SetCastShadow(false);
		auto* Energy = Config.EnergyMaterial.LoadSynchronous(); check(Energy);
		AirNode->SetMaterial(0,Energy); AirNode->SetWorldScale3D(FVector(5));
		AirNode->RegisterComponent();
	}
	Ground->SetWorldLocation(Gate.GetGroundLocation() + FVector(0,0,100));
	Ground->DecalSize = FVector(1500, Config.Radius,Config.Radius);
	AirNode->SetWorldLocation(Gate.GetGroundLocation() + FVector(0,0,Config.LaneHeight));
}
