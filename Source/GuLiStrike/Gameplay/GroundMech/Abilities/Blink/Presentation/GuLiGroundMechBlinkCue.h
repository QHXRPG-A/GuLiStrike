#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "GuLiGroundMechBlinkCue.generated.h"

class AGuLiGroundMechCharacter;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Local-only, finite carrier; the owning Ground snapshot determines activation. */
UCLASS(NotBlueprintable)
class AGuLiGCN_GroundMechBlink : public AGameplayCueNotify_Actor
{
    GENERATED_BODY()
public:
    AGuLiGCN_GroundMechBlink();
    static void RegisterNativeCue();
    virtual bool OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
    virtual bool WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
    virtual bool OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
    virtual void OnOwnerDestroyed(AActor* DestroyedActor) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual bool Recycle() override;
    virtual void Tick(float DeltaSeconds) override;
    void ForceStop();
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Trail;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> EnergyBody;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> EnergyWeapon;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> EnergyMaterials;
    TWeakObjectPtr<AGuLiGroundMechCharacter> Mech;
    TWeakObjectPtr<USkeletalMeshComponent> BodyMesh;
    TWeakObjectPtr<USkeletalMeshComponent> WeaponMesh;
    bool bBodyWasHidden=false,bWeaponWasHidden=false,bOwnsVisibility=false;
    bool bTrailStarted=false,bRemoving=false,bHistorical=false,bShapeFinished=false;
    bool bFinishing=false;
    UFUNCTION() void SystemFinished(UNiagaraComponent* Component);
    void Finish();
    void StartTrail();
    void CreateEnergyShape(AGuLiGroundMechCharacter& Pawn);
    void UpdateEnergyShape();
    void RestoreModel();
};
