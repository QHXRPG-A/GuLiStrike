#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiHybridActorComponent.generated.h"

/** Actor identity and authority view. The World adapter owns simulation and ledger application. */
UCLASS(ClassGroup=(GuLiStrike))
class GULISTRIKE_API UGuLiHybridActorComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiHybridActorComponent();
    void Install(const FGuLiHybridObjectState& InState,bool bLocalSimulation);
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    const FGuLiHybridObjectState& GetState() const { return State; }
    UFUNCTION(BlueprintPure,Category="Hybrid|Authority") FGuLiHybridObjectKey GetObjectKey() const { return State.Key; }
    UFUNCTION(BlueprintPure,Category="Hybrid|Authority") bool CanSimulate() const;
    UFUNCTION(BlueprintPure,Category="Hybrid|Authority") bool CanControl() const;
    UFUNCTION(BlueprintPure,Category="Hybrid|Clock") double GetBattleSeconds() const;
    static const UGuLiHybridActorComponent* Find(const AActor* Actor);
    UFUNCTION(BlueprintPure,Category="Hybrid|Authority",meta=(DefaultToSelf="Actor",DisplayName="Has Object Simulation Authority"))
    static bool HasSimulation(const AActor* Actor);
    UFUNCTION(BlueprintPure,Category="Hybrid|Clock",meta=(DefaultToSelf="Actor",DisplayName="Get Logical Battle Seconds"))
    static double BattleSeconds(const AActor* Actor);
private:
    FGuLiHybridObjectState State;
    bool bLocal=false;
};
