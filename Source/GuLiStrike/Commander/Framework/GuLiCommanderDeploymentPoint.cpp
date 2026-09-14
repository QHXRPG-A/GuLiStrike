// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderDeploymentPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"

AGuLiCommanderDeploymentPoint::AGuLiCommanderDeploymentPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bNetLoadOnClient = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DeploymentRoot")));
#if WITH_EDITORONLY_DATA
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("DeploymentDirection"));
	Arrow->SetupAttachment(GetRootComponent());
	Arrow->ArrowSize = 8.0f;
	Arrow->SetHiddenInGame(true);
#endif
}

FVector AGuLiCommanderDeploymentPoint::GetSlotLocation(const int32 SlotIndex) const
{
	const FVector Offset(
		(SlotIndex / Columns - (Rows - 1) * 0.5f) * SpacingCentimeters,
		(SlotIndex % Columns - (Columns - 1) * 0.5f) * SpacingCentimeters,
		0.0f);
	return GetActorTransform().TransformPosition(Offset);
}
