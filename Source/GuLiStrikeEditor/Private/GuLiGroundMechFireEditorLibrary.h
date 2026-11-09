#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiGroundMechFireEditorLibrary.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UMaterialInterface;

/** Editor-only authoring of the Ground player's approved, project-owned fire assets. */
UCLASS()
class UGuLiGroundMechFireEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Numeric particle and renderer geometry for the approved explosion; never changes assets. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static FString ReadExplosiveImpactParticles(UNiagaraComponent* Component);
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static FString ConfigureExplosiveHead(UNiagaraSystem* System,bool bApply);
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static FString ConfigureImpactVariant(UNiagaraSystem* System,const TArray<UMaterialInterface*>& OriginalMaterials,
        const TArray<UMaterialInterface*>& VariantMaterials,bool bApply);
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static FString ConfigureBullet(UNiagaraSystem* System,UMaterialInterface* Material,FVector Scale,float Lifetime,bool bApply);
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static bool ConfigureMuzzle(UNiagaraSystem* System);
    /** Dimensions = width, maximum trace length, bend amplitude (world cm). */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|GroundMech|Editor")
    static FString ConfigurePersistentTrail(UNiagaraSystem* System,UMaterialInterface* Material,FVector Dimensions,
        float HoldSeconds,float FadeSeconds,float ProjectileLifetime,bool bApply);
};
