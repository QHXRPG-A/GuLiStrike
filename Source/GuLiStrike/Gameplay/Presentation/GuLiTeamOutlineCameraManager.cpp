// Copyright Epic Games, Inc. All Rights Reserved.
#include "Gameplay/Presentation/GuLiTeamOutlineCameraManager.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Camera/GuLiGroundMechCameraComponent.h"

void AGuLiTeamOutlineCameraManager::InitializeFor(APlayerController* PC)
{
	Super::InitializeFor(PC);
	// The local-player SceneUI layer now composites silhouettes after all scene effects.
}

void AGuLiTeamOutlineCameraManager::UpdateCamera(float DeltaTime)
{
    // Release only the white fade written by this view on the preceding frame.
    if (AppliedBarrageFlash>0.f && bEnableFading && !bAutoAnimateFade && FadeColor==FLinearColor::White
        && FMath::IsNearlyEqual(FadeAmount,AppliedBarrageFlash)) StopCameraFade();
    AppliedBarrageFlash=BarrageFlash=0.f;
	Super::UpdateCamera(DeltaTime);
    if (BarrageFlash>0.f && !bEnableFading)
    {
        SetManualCameraFade(BarrageFlash,FLinearColor::White,false);
        AppliedBarrageFlash=BarrageFlash;
    }
}

void AGuLiTeamOutlineCameraManager::UpdateViewTargetInternal(FTViewTarget& Target,float DeltaTime)
{
    Super::UpdateViewTargetInternal(Target,DeltaTime);
    const auto* Ranger=Cast<AGuLiGroundMechCharacter>(Target.Target);
    if (Ranger && PCOwner && PCOwner->IsLocalPlayerController() && PCOwner->GetPawn()==Ranger)
        BarrageFlash=Ranger->GetCameraController()->ApplyBarrageView(Target.POV);
}
