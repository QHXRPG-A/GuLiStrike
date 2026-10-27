#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayEffectTypes.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkTypes.h"
#include "GuLiGroundMechBlinkComponent.generated.h"

class AGuLiGroundMechCharacter;
class AGuLiGCN_GroundMechBlink;
class UAbilitySystemComponent;
class UNiagaraSystem;
class UMaterialInterface;
struct FGuLiGroundMechConfiguration;
struct FGuLiHybridGroundInput;

/** Hybrid owns simulation time; Character explicitly drives this component at the existing call sites. */
UCLASS(ClassGroup=(GuLiStrike))
class GULISTRIKE_API UGuLiGroundMechBlinkComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiGroundMechBlinkComponent();
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void Initialize();
    void Shutdown();
    bool QueueRequest(FGuLiHybridGroundInput& Input, uint16 Yaw);
    const FGuLiGroundMechBlinkState& GetState() const { return BlinkState; }
    float GetPoseAlpha() const;
    float GetCooldownRemaining() const { return BlinkState.Cooldown; }
    float GetCooldownDuration() const;
    bool CanRunSimulation() const;
    bool CanActivate() const;
    bool IsRestoringAbility() const { return bRestoringBlinkAbility; }
    bool PrepareDestination();
    void StartFromAbility();
    void CommitCooldown();
    void AbilityEnded(bool bCancelled);
    void Interrupt();
    void AdvanceSimulation(float DeltaSeconds);
    void ApplySnapshot(const FGuLiGroundMechBlinkState& State);
    void PrepareRestore();
    bool CompletePendingRestore();
    void ResetControl();
    void Present();
    void ResetPresentation();
    UNiagaraSystem* GetSystem() const { return BlinkSystem; }
    const TArray<TObjectPtr<UMaterialInterface>>& GetShapeMaterials() const { return BlinkShapeMaterials; }
    FVector GetVfxScale() const { return BlinkVfxScale; }
    bool HasPresentationResources() const;
    void RegisterCue(AGuLiGCN_GroundMechBlink* Cue);
    void UnregisterCue(AGuLiGCN_GroundMechBlink* Cue);
private:
    AGuLiGroundMechCharacter* Mech() const;
    UAbilitySystemComponent* ASC() const;
    const FGuLiGroundMechConfiguration& Configuration() const;
    void SyncGASState();
    bool RestoreAbility();
    void SendEvent(bool bInterrupted=false);
    void NotifyStateChanged();
    void StopPresentation(bool bImmediate);
    UPROPERTY() FGuLiGroundMechBlinkState BlinkState;
    FGameplayAbilitySpecHandle BlinkAbilityHandle;
    FActiveGameplayEffectHandle BlinkCooldownEffect;
    bool bRestoringBlinkAbility=false;
    bool bBlinkQueryActiveTag=false;
    bool bBlinkAbilityRestorePending=false;
    bool bBlinkPresentationPrimed=false;
    bool bBlinkCueActive=false;
    bool bShuttingDown=false;
    uint8 PresentedBlinkPhase=0, NotifiedBlinkPhase=0;
    bool bNotifiedBlinkCooldown=false;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> BlinkSystem;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> BlinkShapeMaterials;
    FVector PreparedBlinkDestination=FVector::ZeroVector;
    FVector BlinkVfxScale=FVector::OneVector;
    TArray<TWeakObjectPtr<AGuLiGCN_GroundMechBlink>> BlinkCues;
    int32 PresentedBlinkSerial=0;
    FVector PresentedBlinkEnd=FVector::ZeroVector;
    uint32 BlinkSimulationDisplacementRevision=0;
};
