#pragma once
#include "CoreMinimal.h"
#include "GuLiStrongholdGateConfig.generated.h"
class UMaterialInterface;
class UNiagaraSystem;

USTRUCT()
struct FGuLiStrongholdGateConfig
{
	GENERATED_BODY()
	UPROPERTY() int32 Id = 0;
	UPROPERTY() float Radius = 4000;
	UPROPERTY() float LaneHeight = 10000;
	UPROPERTY() float AscentSeconds = .5f;
	UPROPERTY() float AccelerationSeconds = 1;
	UPROPERTY() float DecelerationSeconds = .5f;
	UPROPERTY() float ExitFlashSeconds = .2f;
	UPROPERTY() float SpeedMultiplier = 50;
	UPROPERTY() float ExitRadius = 4000;
	UPROPERTY() TSoftObjectPtr<UMaterialInterface> EnergyMaterial;
	UPROPERTY() TSoftObjectPtr<UMaterialInterface> GateMaterial;
	UPROPERTY() TSoftObjectPtr<UNiagaraSystem> TrailSystem;
	UPROPERTY() TSoftObjectPtr<UNiagaraSystem> FlashSystem;
};
