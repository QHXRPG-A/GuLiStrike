// Copyright Epic Games, Inc. All Rights Reserved.
#include "Gameplay/Presentation/GuLiTeamOutlineCameraManager.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

void AGuLiTeamOutlineCameraManager::InitializeFor(APlayerController* PC)
{
	Super::InitializeFor(PC);
	if (!PC->IsLocalController() || GetNetMode() == NM_DedicatedServer) return;
	OutlineInstance = UMaterialInstanceDynamic::Create(GuLiVfx::Load<UMaterialInterface>(this, OutlineVfxId), this);
	OutlineSettings.AddBlendable(OutlineInstance, 1.0f);
}

void AGuLiTeamOutlineCameraManager::UpdateCamera(float DeltaTime)
{
	Super::UpdateCamera(DeltaTime);
	if (!OutlineInstance) return;
	const AGuLiBattlePlayerState* PlayerState = PCOwner->GetPlayerState<AGuLiBattlePlayerState>();
	const EGuLiTeam Team = PlayerState ? PlayerState->GetTeam() : EGuLiTeam::Unassigned;
	OutlineInstance->SetScalarParameterValue(TEXT("LocalTeam"), static_cast<uint8>(Team));
	AddCachedPPBlend(OutlineSettings, 1.0f);
}
