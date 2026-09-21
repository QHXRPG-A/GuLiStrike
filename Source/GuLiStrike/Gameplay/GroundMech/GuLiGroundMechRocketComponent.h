#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "GuLiGroundMechRocketComponent.generated.h"

class UAbilitySystemComponent;
class UGuLiGroundMechMovementComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGuLiRocketPresentationChanged, float, FuelRatio, bool, bThrusting, float, Opacity);

/** GAS/configuration boundary and reconstructable cosmetics. Resource integration belongs to CMC. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiGroundMechRocketComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiGroundMechRocketComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void RefreshActorInfo();
    UFUNCTION(BlueprintCallable, Category="Mech|Rocket Jump") void SetRocketJumpInput(bool bHeld);
    UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") float GetFuelRatio() const;
    UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") float GetFuel() const;
    UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") bool IsThrusting() const;
    UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") bool IsJetActive() const;
    UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") float GetFuelBarOpacity() const { return FuelBarOpacity; }
    UPROPERTY(BlueprintAssignable, Category="Mech|Rocket Jump") FGuLiRocketPresentationChanged OnPresentationChanged;
    bool IsConfigured() const { return bConfigured; }
    bool IsInputHeld() const { return bInputHeld; }
    bool IsAbilityActive() const { return bAbilityActive; }
    bool CanUseRocketControls() const;
    bool CanActivateRocket() const;
    const FGuLiStrikeMechSkillsRow& GetConfiguration() const { return Configuration; }
    void SetAbilityActive(bool bActive);
    void Interrupt();
    void FinishMovement(float Fuel, bool bThrusting, bool bReplay);
    void ReceiveInitialFuel(float Fuel);
    void ConfirmMovementBaseline() { bFuelBaselineReceived = true; }
private:
    UAbilitySystemComponent* ASC() const;
    UGuLiGroundMechMovementComponent* Movement() const;
    void CreatePresentation();
    void UpdatePresentation(float Delta);
    void RefreshJetState();
    void ApplyJetPresentation();
    UFUNCTION() void OnRep_JetActive();
    void DestroyPresentation();
    void ApplyFuelDelta(float Delta);
    UPROPERTY(EditDefaultsOnly, Category="Mech|Rocket Jump") TSoftObjectPtr<UDataTable> SkillTable;
    UPROPERTY(EditDefaultsOnly, Category="Mech|Rocket Jump") FName SkillRow = TEXT("RocketJump");
    UPROPERTY(Transient) FGuLiStrikeMechSkillsRow Configuration;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> JetSystem;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FuelMaterial;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> LeftJet;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> RightJet;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> FuelBar;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FuelMID;
    UPROPERTY(Replicated) bool bReplicatedThrusting = false;
    UPROPERTY(ReplicatedUsing=OnRep_JetActive) bool bReplicatedJetActive = false;
    FGameplayAbilitySpecHandle AbilityHandle;
    bool bConfigured = false, bInputHeld = false, bAbilityActive = false;
    bool bFuelBaselineReceived = false, bShown = false, bJetsActive = false, bJetsCreated = false;
    float FuelBarOpacity = 0.f, LastRatio = -1.f, LastOpacity = -1.f;
    bool bLastThrusting = false;
};
