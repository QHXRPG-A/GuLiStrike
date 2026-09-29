#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GuLiRogueCardSettings.generated.h"
class UDataTable;
class UStringTable;
class UMaterialInterface;
class AActor;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Rogue Cards"))
class GULISTRIKE_API UGuLiRogueCardSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UGuLiRogueCardSettings();
	UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> Cards;
	UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UStringTable> Texts;
	UPROPERTY(Config, EditAnywhere, Category="Presentation") TSoftClassPtr<AActor> DirectorClass;
	UPROPERTY(Config, EditAnywhere, Category="Presentation") TSoftObjectPtr<UMaterialInterface> CaptureMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Presentation") TSoftObjectPtr<UMaterialInterface> FrameMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Presentation", meta=(ClampMin="0")) float BlurStrength=8.f;
	UPROPERTY(Config, EditAnywhere, Category="Presentation", meta=(ClampMin="0.01")) float FadeSeconds=.2f;
};
