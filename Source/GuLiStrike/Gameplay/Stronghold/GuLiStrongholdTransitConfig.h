#pragma once
#include "CoreMinimal.h"
#include "GuLiStrongholdTransitConfig.generated.h"
class UMaterialInterface;
class UNiagaraSystem;

USTRUCT()
struct FGuLiStrongholdTransitConfig
{
	GENERATED_BODY()
	UPROPERTY() int32 Id = 0;
	UPROPERTY() float LaneHeight = 2000;
	UPROPERTY() float AscentSeconds = .5f;
	UPROPERTY() float AccelerationSeconds = 1;
	UPROPERTY() float DecelerationSeconds = .5f;
	UPROPERTY() float ExitFlashSeconds = .2f;
	UPROPERTY() float SpeedMultiplier = 50;
	UPROPERTY() float ExitRadius = 800;
	UPROPERTY() TSoftObjectPtr<UMaterialInterface> EnergyMaterial;
	UPROPERTY() TSoftObjectPtr<UNiagaraSystem> TrailSystem;
	UPROPERTY() TSoftObjectPtr<UNiagaraSystem> FlashSystem;
};
