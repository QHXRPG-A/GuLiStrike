// Copyright Epic Games, Inc. All Rights Reserved.
#include "Gameplay/Presentation/GuLiTeamOutlineCameraManager.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

void AGuLiTeamOutlineCameraManager::InitializeFor(APlayerController* PC)
{
	Super::InitializeFor(PC);
	// The local-player SceneUI layer now composites silhouettes after all scene effects.
}

void AGuLiTeamOutlineCameraManager::UpdateCamera(float DeltaTime)
{
	Super::UpdateCamera(DeltaTime);
}
