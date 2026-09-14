// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "GuLiTeamOutlineCameraManager.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Per-view enemy outline, including different teams in split-screen views. */
UCLASS(Config=Game)
class GULISTRIKE_API AGuLiTeamOutlineCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
public:
	virtual void InitializeFor(APlayerController* PC) override;
	virtual void UpdateCamera(float DeltaTime) override;

private:
	UPROPERTY(Config, EditDefaultsOnly, Category="Presentation|Outline")
	TSoftObjectPtr<UMaterialInterface> OutlineMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OutlineInstance;
	FPostProcessSettings OutlineSettings;
};
