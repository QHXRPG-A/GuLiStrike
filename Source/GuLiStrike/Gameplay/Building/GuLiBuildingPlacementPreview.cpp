// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingPlacementPreview.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
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

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(SceneRoot);
	PreviewMesh->SetMobility(EComponentMobility::Movable);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->SetCanEverAffectNavigation(false);
	PreviewMesh->SetCastShadow(false);
	// The production missile mesh uses Nanite, while the placement ghost must use
	// one translucent override material. Force this cosmetic component onto the
	// fallback mesh; otherwise Nanite emits one warning per material section every
	// rendered frame and the editor log/memory grows without bound.
	PreviewMesh->bDisallowNanite = true;
}

bool AGuLiBuildingPlacementPreview::Configure(
	const FGuLiBuildingDefinition& Definition,
	const int32 PreviewVfxId)
{
	UMaterialInterface* PreviewMaterial = GuLiVfx::Load<UMaterialInterface>(this, PreviewVfxId);
	if (!Definition.IsUsable() || !PreviewMaterial || !PreviewMesh)
	{
		return false;
	}

	PreviewMesh->SetStaticMesh(Definition.Mesh);
	PreviewMesh->SetRelativeLocation(Definition.VisualOffset);
	PreviewMesh->SetRelativeRotation(FRotator::ZeroRotator);
	PreviewMesh->SetRelativeScale3D(GuLiVfx::Scale(this, PreviewVfxId, Definition.MeshScale));

	PreviewMaterialInstance = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
	if (!PreviewMaterialInstance)
	{
		return false;
	}
	bHasPlacementValidity = false;
	for (int32 MaterialIndex = 0; MaterialIndex < PreviewMesh->GetNumMaterials(); ++MaterialIndex)
	{
		PreviewMesh->SetMaterial(MaterialIndex, PreviewMaterialInstance);
	}
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
		bHasPlacementValidity = true;
		bLastPlacementValidity = bValid;
	}
}
