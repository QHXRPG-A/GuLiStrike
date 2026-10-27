#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GuLiGroundMechBlinkVfxDefinition.generated.h"

class UMaterialInterface;

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroundMechBlinkMaterialVariant
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Blink") TSoftObjectPtr<UMaterialInterface> Source;
    UPROPERTY(EditAnywhere,Category="Blink") TSoftObjectPtr<UMaterialInterface> Phase;
};

/** Material parents for the independent energy proxy; never applied to the real model. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiGroundMechBlinkVfxDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,Category="Blink") TArray<FGuLiGroundMechBlinkMaterialVariant> BodyMaterials;
};
