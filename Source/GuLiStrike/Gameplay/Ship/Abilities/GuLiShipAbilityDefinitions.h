// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "GuLiShipAbilityDefinitions.generated.h"

/** Immutable group guidance authored for a formation ability. */
UCLASS(BlueprintType, Const)
class GULISTRIKE_API UGuLiWingmanFormationDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UGuLiWingmanFormationDefinition();

	UPROPERTY(EditAnywhere, Category = "Formation", meta = (ClampMin = "1"))
	uint32 Revision = 4u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation", meta = (ClampMin = "1", ClampMax = "25"))
	uint8 ExpectedWingmanCount = 25u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation", meta = (ClampMin = "1", ClampMax = "25"))
	uint8 FlightCount = 5u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation")
	EGuLiWingmanFormationModel Model = EGuLiWingmanFormationModel::DoubleRingLegacy;

	UPROPERTY(EditAnywhere, Category = "Formation", meta = (ClampMin = "1"))
	uint32 GuidanceAlgorithmVersion = 1u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|SwarmOrbit",
		meta = (EditCondition = "Model == EGuLiWingmanFormationModel::SwarmOrbit", EditConditionHides))
	FGuLiWingmanSwarmOrbitTuning SwarmOrbit;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing", meta = (ClampMin = "1", ClampMax = "25"))
	uint8 InnerRingSlots = 13u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing", meta = (ClampMin = "1", ClampMax = "25"))
	uint8 OuterRingSlots = 12u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing", meta = (ClampMin = "1.0"))
	float InnerRingRadiusCentimeters = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing", meta = (ClampMin = "1.0"))
	float OuterRingRadiusCentimeters = 18000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing")
	float InnerRingHeightCentimeters = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing")
	float OuterRingHeightCentimeters = -3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing", meta = (ClampMin = "0.001"))
	float InnerAngularSpeedRadiansPerSecond = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation|DoubleRing", meta = (ClampMin = "0.001"))
	float OuterAngularSpeedRadiansPerSecond = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "1.0"))
	float MinimumFlightSpeedCentimetersPerSecond = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "1.0"))
	float CruiseFlightSpeedCentimetersPerSecond = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "1.0"))
	float CatchUpFlightSpeedCentimetersPerSecond = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "0.1", ClampMax = "180.0"))
	float MaximumTurnRateDegreesPerSecond = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "1.0"))
	float MaximumAccelerationCentimetersPerSecondSquared = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "1.0"))
	float MaximumDecelerationCentimetersPerSecondSquared = 640.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "0.1", ClampMax = "90.0"))
	float MaximumBankDegrees = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance", meta = (ClampMin = "1.0"))
	float AgentRadiusCentimeters = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance", meta = (ClampMin = "1.0"))
	float SeparationRadiusCentimeters = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance", meta = (ClampMin = "1.0"))
	float ObstacleLookAheadCentimeters = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "1.0"))
	float CatchUpDistanceCentimeters = 24000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "1.0"))
	float RecoveryDistanceCentimeters = 50000.0f;

	bool IsWellFormed(FString* OutError = nullptr) const;
	bool BuildRuntimeConfig(
		uint32 FormationSeed,
		FGuLiWingmanFormationRuntimeConfig& OutRuntime,
		FString* OutError = nullptr) const;
	uint64 ComputeStableChecksum() const;
};

/**
 * Wingman weapon authorization/tuning. These are Mass/combat inputs, not Ship
 * attributes, and are never applied through a GameplayEffect or AttributeSet.
 */
UCLASS(BlueprintType, Const)
class GULISTRIKE_API UGuLiWingmanWeaponDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UGuLiWingmanWeaponDefinition();

	/** Production attacks resolve GuLiStrikeSecondaryWeapons.xlsx / WingmanWeapons on authority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Attack")
	FDataTableRowHandle AttackProfileRow;
	/** Explicit native fallback for transient fixtures; an authored row is never silently bypassed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Attack")
	FGuLiWingmanAttackProfile Attack;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Attack")
	TSoftObjectPtr<class UGuLiProjectileEffectDefinition> AttackProjectile;
	UGuLiProjectileEffectDefinition* ResolveAttackProjectile() const;
	bool BuildRuntimeConfig(FGuLiWingmanWeaponRuntimeConfig& OutRuntime, FString* OutError = nullptr) const;
	/** Read back the same resolved values used by authority and loadout checksums. */
	UFUNCTION(BlueprintPure, Category="Weapon")
	bool GetResolvedWeaponConfig(FGuLiWingmanWeaponRuntimeConfig& OutRuntime) const;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = "1"))
	uint32 Revision = 1u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EGuLiWingmanWeaponKind Kind = EGuLiWingmanWeaponKind::BasicAutomatic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0"))
	float Damage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1.0"))
	float RangeCentimeters = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.033333333"))
	float CooldownSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1.0"))
	float ProjectileSpeedCentimetersPerSecond = 24000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.01"))
	float ProjectileLifetimeSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0"))
	float SweepRadiusCentimeters = 10.0f;

	/** Full cone angle measured from reticle forward; 180 permits any forward hemisphere target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.1", ClampMax = "180.0"))
	float TargetConeHalfAngleDegrees = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting")
	bool bRequiresLineOfSight = true;

	/** Missile-only, used by the server's 30 Hz logical projectile integrator. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Missile", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaximumHomingTurnRateDegreesPerSecond = 0.0f;

	bool IsWellFormed(FString* OutError = nullptr) const;
	uint64 ComputeStableChecksum() const;
};
