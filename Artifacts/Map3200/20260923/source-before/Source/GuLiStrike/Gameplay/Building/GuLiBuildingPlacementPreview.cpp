// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingPlacementPreview.h"
#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Gameplay/Building/GuLiBuildingVisuals.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"

#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/DecalComponent.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace GuLiBuildingPreview
{
	const FName TintParameter(TEXT("TintColor"));
	const FLinearColor ValidColor(0.04f, 1.0f, 0.12f, 1.0f);
	const FLinearColor InvalidColor(1.0f, 0.03f, 0.02f, 1.0f);
}

AGuLiBuildingPlacementPreview::AGuLiBuildingPlacementPreview()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);

	GridDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("PlacementGrid"));
	GridDecal->SetupAttachment(SceneRoot);
	GridDecal->SetRelativeRotation(FRotator(-90, 0, 0));
	GridDecal->SetFadeScreenSize(0.0f);
}

bool AGuLiBuildingPlacementPreview::Configure(
	const FGuLiBuildingDefinition& Definition,
	const int32 PreviewVfxId)
{
	UMaterialInterface* PreviewMaterial = GuLiVfx::Load<UMaterialInterface>(this, PreviewVfxId);
	auto* GridMaterial = GuLiVfx::Load<UMaterialInterface>(this,
		GetDefault<UGuLiBuildingConstructionSettings>()->GridVfxId);
	if (!Definition.IsUsable() || !PreviewMaterial || !GridMaterial)
	{
		return false;
	}

	PreviewMaterialInstance = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
	GridMaterialInstance = UMaterialInstanceDynamic::Create(GridMaterial, this);
	if (!PreviewMaterialInstance || !GridMaterialInstance)
	{
		return false;
	}
	bHasPlacementValidity = false;
	if (!GuLiBuildingVisuals::CreatePreviewMeshes(*this, *SceneRoot, Definition, PreviewMaterialInstance, PreviewMeshes)) return false;
	for (UMeshComponent* Mesh : PreviewMeshes)
		Mesh->SetRelativeScale3D(GuLiVfx::Scale(this, PreviewVfxId, Mesh->GetRelativeScale3D()));
	GridMaterialInstance->SetScalarParameterValue(TEXT("GridSize"), GuLiBuildingPlacementPolicy::GridSizeCentimeters);
	GridDecal->SetDecalMaterial(GridMaterialInstance);
	// The decal's local X is depth; after projecting down, Z spans footprint X.
	GridDecal->DecalSize = FVector(100.0f, Definition.CollisionExtent.Y, Definition.CollisionExtent.X);
	SetPlacementValidity(false);
	return true;
}

void AGuLiBuildingPlacementPreview::SetPlacementTransform(
	const FVector& GroundLocation,
	const float YawDegrees)
{
	SetActorLocationAndRotation(
		GroundLocation,
		FRotator(0.0f, FRotator::NormalizeAxis(YawDegrees), 0.0f),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AGuLiBuildingPlacementPreview::SetPlacementValidity(const bool bValid)
{
	if (PreviewMaterialInstance
		&& (!bHasPlacementValidity || bLastPlacementValidity != bValid))
	{
		PreviewMaterialInstance->SetVectorParameterValue(
			GuLiBuildingPreview::TintParameter,
			bValid ? GuLiBuildingPreview::ValidColor : GuLiBuildingPreview::InvalidColor);
		if (GridMaterialInstance) GridMaterialInstance->SetVectorParameterValue(
			GuLiBuildingPreview::TintParameter,
			bValid ? GuLiBuildingPreview::ValidColor : GuLiBuildingPreview::InvalidColor);
		bHasPlacementValidity = true;
		bLastPlacementValidity = bValid;
	}
}
