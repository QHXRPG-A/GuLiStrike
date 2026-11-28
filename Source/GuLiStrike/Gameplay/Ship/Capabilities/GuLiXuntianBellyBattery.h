#pragma once

#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Ship/Capabilities/GuLiShipCapabilityComponent.h"
#include "GuLiXuntianBellyBattery.generated.h"

/** A mount group, its authorized impact field and its frozen projectile share this configuration. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiXuntianShellDefinition final : public UGuLiProjectileEffectDefinition
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Battery", meta=(ClampMin="1", ClampMax="3"))
    int32 Level = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Battery", meta=(Units="s", ClampMin="0.01"))
    float RoundSeconds = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Battery", meta=(Units="s", ClampMin="0.01"))
    float BurstInterval = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Battery")
    FName FieldRow;
};

UCLASS()
class GULISTRIKE_API UGuLiXuntianBellyBattery final : public UGuLiShipCapabilityComponent
{
    GENERATED_BODY()
public:
    UGuLiXuntianShellDefinition* GetShell() const { return Cast<UGuLiXuntianShellDefinition>(CapabilityConfiguration); }
    virtual bool ValidateConfiguration(const UDataAsset* Config, FString& Error) const override;
    virtual FName GetExclusiveRuntimeDomain() const override { return TEXT("Xuntian.Belly"); }
    virtual uint32 GetPrivateStateVersion() const override { return 1; }
    virtual uint64 GetConfigurationChecksum(const UDataAsset* Config) const override;
    virtual bool CapturePrivateState(TArray<uint8>& Bytes) const override;
    virtual bool ValidatePrivateState(TConstArrayView<uint8> Bytes, const UDataAsset* Config) const override;
    virtual bool RestorePrivateState(TConstArrayView<uint8> Bytes) override;
};
