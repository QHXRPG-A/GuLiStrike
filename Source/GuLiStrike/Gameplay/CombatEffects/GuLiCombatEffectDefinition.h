#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "GuLiCombatEffectDefinition.generated.h"

class UNiagaraSystem;
class UNiagaraDataChannelAsset;

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiEffectVisualLayer
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual") TSoftObjectPtr<UNiagaraSystem> System;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual", meta=(ClampMin="0.001")) float Scale = 1.0f;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiEffectVisualVariant
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual") TSoftObjectPtr<UNiagaraSystem> System;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual", meta=(ClampMin="0.001")) float Scale = 1.0f;
	/** None preserves component scaling. A named float receives Scale with unit component scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual") FName ScaleParameterName;
	/** Rotates this world-space burst around +Z from the replicated effect seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual") bool bRandomYaw = false;
	/** Maximum visual lifetime, including smoke; enforces cleanup even for a broken looping template. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual", meta=(ClampMin="0.01", Units="s")) float MaximumLifetime = 3.0f;
	/** Simultaneous layers with independent authored scales, sharing this variant's origin, yaw and lifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Visual") TArray<FGuLiEffectVisualLayer> AdditionalLayers;
};

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiSpellFieldDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	/** Default SpellFields row for direct callers. A weapon context's authored field reference takes precedence; both empty permits the inline prototype fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Field") FName ConfigId;
	/** Inline fallback timing. Table-driven fields replace this at creation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Field") EGuLiSpellFieldTiming Timing = EGuLiSpellFieldTiming::Instant;
	/** Inline fallback and visual authoring reference radius. Table-driven fields use the frozen table radius at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Field", meta=(ClampMin="0", Units="cm")) float Radius = 800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Field", meta=(ClampMin="0", Units="s")) float Delay = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Field", meta=(ClampMin="0.033", Units="s")) float Duration = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Field", meta=(ClampMin="0.033", Units="s")) float PulseInterval = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TSoftObjectPtr<UNiagaraSystem> WaitingSystem;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TSoftObjectPtr<UNiagaraSystem> ActiveLoopSystem;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TArray<FGuLiEffectVisualVariant> ActivationVariants;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0", Units="s")) float DissipationSeconds = 3.0f;
	bool IsValidDefinition() const;
};

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiProjectileEffectDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	/** Production motion is authored in GuLiStrikeSecondaryWeapons.xlsx / Projectiles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile") FDataTableRowHandle MotionProfileRow;
	/** Native/test fallback used only when MotionProfileRow is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile") FGuLiProjectileMotionSettings Motion;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile") TSoftObjectPtr<UGuLiSpellFieldDefinition> ImpactField;
	/** Contains the missile mesh/bright core and flame/ribbon emitters; no replicated visual Actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TSoftObjectPtr<UNiagaraSystem> FlightSystem;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0.001")) float VisualScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0", Units="s")) float TrailFadeSeconds = 0.5f;
	/** Resolves the authoritative table profile; invalid authored rows never fall back. */
	UFUNCTION(BlueprintPure, Category="Projectile")
	bool ResolveMotionSettings(FGuLiProjectileMotionSettings& OutMotion) const;
	bool IsValidDefinition() const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWeaponEffectMount
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mount") int32 UnitTypeId = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mount") FName SlotId = TEXT("BasicAttack");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mount") FName SkillId = TEXT("Strafe");
	/** Coordinates in the rendered mesh's local space; no implicit unit-center fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mount") TArray<FVector> Muzzles;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mount") FVector AimOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mount") TSoftObjectPtr<UGuLiProjectileEffectDefinition> Projectile;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mount") bool bCalibrated = false;
};

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiCombatEffectCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapons") TArray<FGuLiWeaponEffectMount> Mounts;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire") TSoftObjectPtr<UNiagaraDataChannelAsset> GunfireChannel;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire") TSoftObjectPtr<UNiagaraSystem> GunfireSystem;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wingman Laser") TSoftObjectPtr<UNiagaraSystem> WingmanLaserSystem;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wingman Laser", meta=(ClampMin="1", Units="cm")) float LaserLength = 3000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wingman Laser", meta=(ClampMin="1", Units="cm")) float LaserCoreWidth = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wingman Laser", meta=(ClampMin="0")) float LaserIntensity = 24.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wingman Laser") FLinearColor FriendlyLaserTint = FLinearColor(0.05f, 1.0f, 0.12f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wingman Laser") FLinearColor EnemyLaserTint = FLinearColor(1.0f, 0.025f, 0.015f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wingman Laser", meta=(ClampMin="0.01", ClampMax="0.15", Units="s")) float LaserMuzzleSeconds = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="0.01", ClampMax="0.15")) float TracerLifetime = 0.075f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="1")) float TracerWidth = 22.0f;
	/** Keeps a muzzle active between accepted machine-gun shots; it is refreshed at the live presentation pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="0.25", ClampMax="2.0", Units="s")) float MuzzleActivityHoldSeconds = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="10", ClampMax="60", Units="Hz")) float MuzzleRefreshRate = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="0.04", ClampMax="0.2", Units="s")) float MuzzleParticleLifetime = 0.06f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="1", Units="cm")) float MuzzleWidth = 420.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="1", Units="cm")) float MuzzleLength = 1200.0f;
	/** Visual flame remains present; this rate only controls its brightness and nearby ground-light pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="1", ClampMax="30", Units="Hz")) float MuzzleStrobeRate = 9.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire", meta=(ClampMin="0.1", ClampMax="0.9")) float MuzzleStrobeDutyCycle = 0.45f;
	/** Particle lights are sampled and capped; never create one dynamic light per visible shot or active unit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire|Lighting", meta=(ClampMin="0", ClampMax="32")) int32 MaximumMuzzleLightsPerFrame = 12;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire|Lighting", meta=(ClampMin="0", ClampMax="16")) int32 MaximumTracerLightsPerFrame = 6;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire|Lighting", meta=(ClampMin="0", Units="cm")) float MuzzleLightRadius = 2600.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire|Lighting", meta=(ClampMin="0")) float MuzzleLightBrightness = 35.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire|Lighting", meta=(ClampMin="0", Units="cm")) float TracerLightRadius = 1800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire|Lighting", meta=(ClampMin="0")) float TracerLightBrightness = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gunfire") FLinearColor GunfireTint = FLinearColor(1.0f, 0.72f, 0.32f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scalability", meta=(ClampMin="100")) float MaximumVisualDistance = 100000.0f;
	const FGuLiWeaponEffectMount* FindMount(int32 UnitTypeId, FName SlotId) const;
};

UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiCombatEffectSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Combat Effects") TSoftObjectPtr<UGuLiCombatEffectCatalog> Catalog;
	UPROPERTY(Config, EditAnywhere, Category="Projectile Pool", meta=(ClampMin="1")) int32 ProjectilePoolInitialCapacity = 1024;
	UPROPERTY(Config, EditAnywhere, Category="Projectile Pool", meta=(ClampMin="1")) int32 ProjectilePoolGrowthSize = 1024;
};
