// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Skills/GuLiWeaponChannelTypes.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#include "GuLiShipAbilityTypes.generated.h"

inline constexpr uint32 GULI_WINGMAN_PROTOCOL_VERSION = 14u;
inline constexpr int32 GULI_MAX_WINGMAN_WEAPON_CHANNELS = 8;
inline constexpr double GULI_WINGMAN_AUTOMATIC_FIRE_BUDGET_PER_SECOND = 50.0;

/** The three group-level Wingman action categories in the first vertical slice. */
UENUM(BlueprintType)
enum class EGuLiShipAbilitySlot : uint8
{
	None = 0,
	Formation,
	BasicWeapon,
	Missile
};

/** Persistent abilities publish configuration; triggered abilities authorize one request. */
UENUM(BlueprintType)
enum class EGuLiShipAbilityActivationPolicy : uint8
{
	WhileGranted = 0,
	OnInputTriggered
};

/** Execution behavior selected by a committed Wingman weapon channel. */
UENUM(BlueprintType)
enum class EGuLiWingmanWeaponKind : uint8
{
	BasicAutomatic = 0,
	Missile
};

GULISTRIKE_API FGameplayTag GuLiGetShipAbilitySlotTag(EGuLiShipAbilitySlot Slot);
GULISTRIKE_API bool GuLiIsPersistentShipAbilitySlot(EGuLiShipAbilitySlot Slot);
GULISTRIKE_API FName GuLiGetDefaultWingmanTypeId();
GULISTRIKE_API FName GuLiGetDefaultWeaponSlotId(EGuLiShipAbilitySlot Slot);
GULISTRIKE_API FName GuLiGetDefaultWeaponSkillId(FGameplayTag AbilityId);

/**
 * Session-only Ship loadout stored by stable AbilityId. Ability spec handles,
 * active instances, effects, and cooldowns intentionally never survive respawn.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipAbilityLoadoutState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Abilities")
	TArray<FGameplayTag> AbilityIds;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 Revision = 1u;

	void Normalize();
	bool IsWellFormed(FString* OutError = nullptr) const;
	bool Contains(FGameplayTag AbilityId) const;
	bool HasSameSelection(const FGuLiShipAbilityLoadoutState& Other) const;

	static FGuLiShipAbilityLoadoutState MakeNativeV1();
	static FGuLiShipAbilityLoadoutState MakeNativeV2();
	static FGuLiShipAbilityLoadoutState MakeNativeV3();

	friend bool operator==(const FGuLiShipAbilityLoadoutState& Lhs, const FGuLiShipAbilityLoadoutState& Rhs)
	{
		return Lhs.Revision == Rhs.Revision && Lhs.AbilityIds == Rhs.AbilityIds;
	}

	friend bool operator!=(const FGuLiShipAbilityLoadoutState& Lhs, const FGuLiShipAbilityLoadoutState& Rhs)
	{
		return !(Lhs == Rhs);
	}
};

/** Versioned formation behavior selected by the authoritative Formation ability. */
UENUM(BlueprintType)
enum class EGuLiWingmanFormationModel : uint8
{
	DoubleRingLegacy = 0,
	SwarmOrbit
};

/**
 * Pure numeric SwarmOrbit payload. Values are copied into the reliable group
 * snapshot; Mass never reads the source DataAsset or hangar capability.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanSwarmOrbitTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float InnerSoftRadiusCentimeters = 24000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float OuterSoftRadiusCentimeters = 52000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float VerticalHalfExtentCentimeters = 14000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float HullExclusionRadiusCentimeters = 14000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float SwirlSpeedMinCentimetersPerSecond = 7200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float SwirlSpeedMaxCentimetersPerSecond = 10400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float CurlStrengthCentimetersPerSecond = 2800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float NoiseSpatialScaleCentimeters = 22000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float NoiseTemporalScaleSeconds = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float AxisPrecessionAmount = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float AxisPrecessionRadiansPerSecond = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float BoundaryReturnSpeedCentimetersPerSecond = 5600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float PreferredRadiusReturnSpeedCentimetersPerSecond = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float VerticalReturnSpeedCentimetersPerSecond = 2800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float AlignmentWeight = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float CatchUpStyleWeight = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float RecoveryStyleWeight = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation|SwarmOrbit")
	float ResponseTimeSeconds = 1.25f;

	bool IsWellFormed() const;
	void AddToStableHash(uint64& Hash) const;
};

/** Stable identity/revision context supplied by the authoritative Ship/group host. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipAbilityProjectionContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuid ShipInstanceId;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	/** Stable growth/loadout owner; deliberately independent from the lease owner. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuid OwnerPlayerGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FName WingmanTypeId;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 ShipGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 GroupGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 FormationCommandRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 EffectiveClientSimTick = 0u;

	bool IsWellFormed() const
	{
		return ShipInstanceId.IsValid() && MatchEpoch != 0u
			&& (Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue)
			&& OwnerPlayerGuid.IsValid() && !WingmanTypeId.IsNone()
			&& ShipGeneration != 0u && GroupGeneration != 0u;
	}

	friend bool operator==(const FGuLiShipAbilityProjectionContext& Lhs, const FGuLiShipAbilityProjectionContext& Rhs)
	{
		return Lhs.ShipInstanceId == Rhs.ShipInstanceId
			&& Lhs.MatchEpoch == Rhs.MatchEpoch
			&& Lhs.Team == Rhs.Team
			&& Lhs.OwnerPlayerGuid == Rhs.OwnerPlayerGuid
			&& Lhs.WingmanTypeId == Rhs.WingmanTypeId
			&& Lhs.ShipGeneration == Rhs.ShipGeneration
			&& Lhs.GroupGeneration == Rhs.GroupGeneration
			&& Lhs.FormationCommandRevision == Rhs.FormationCommandRevision
			&& Lhs.EffectiveClientSimTick == Rhs.EffectiveClientSimTick;
	}

	friend bool operator!=(const FGuLiShipAbilityProjectionContext& Lhs, const FGuLiShipAbilityProjectionContext& Rhs)
	{
		return !(Lhs == Rhs);
	}
};

/**
 * Numeric guidance inputs copied out of the authoritative formation DataAsset.
 * Lease owners (including a backup owner) consume this value without loading or
 * inspecting the hangar capability. These are Wingman simulation constants, not Ship
 * attributes.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanFormationRuntimeConfig
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	EGuLiWingmanFormationModel Model = EGuLiWingmanFormationModel::DoubleRingLegacy;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities|Formation")
	uint32 GuidanceAlgorithmVersion = 1u;

	/** Stable per-group seed derived from authoritative identity, never a local random source. */
	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities|Formation")
	uint32 FormationSeed = 1u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	FGuLiWingmanSwarmOrbitTuning SwarmOrbit;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	uint8 InnerRingSlots = 13u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	uint8 OuterRingSlots = 12u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	float InnerRingRadiusCentimeters = 60000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	float OuterRingRadiusCentimeters = 90000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	float InnerRingHeightCentimeters = 15000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	float OuterRingHeightCentimeters = -15000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	float InnerAngularSpeedRadiansPerSecond = 0.16f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	float OuterAngularSpeedRadiansPerSecond = 0.12f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MinimumSpeedCentimetersPerSecond = 6000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float CruiseSpeedCentimetersPerSecond = 9000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float CatchUpSpeedCentimetersPerSecond = 15000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MaximumAccelerationCentimetersPerSecondSquared = 4000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MaximumDecelerationCentimetersPerSecondSquared = 3200.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MaximumTurnRateDegreesPerSecond = 80.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MaximumBankDegrees = 45.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Avoidance")
	float AgentRadiusCentimeters = 1500.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Avoidance")
	float SeparationRadiusCentimeters = 3000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Avoidance")
	float ObstacleLookAheadCentimeters = 5000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Recovery")
	float CatchUpDistanceCentimeters = 120000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Recovery")
	float RecoveryDistanceCentimeters = 250000.0f;

	bool IsWellFormed() const;
	void AddToStableHash(uint64& Hash) const;
};

/** Client-consumable targeting cadence copied from one weapon definition. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanWeaponRuntimeConfig
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FName EffectConfigId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FGuLiWingmanAttackProfile Attack;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float Damage = 10.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float RangeCentimeters = 150000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float CooldownSeconds = 2.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float TargetConeHalfAngleDegrees = 20.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	bool bRequiresLineOfSight = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float ProjectileSpeedCentimetersPerSecond = 120000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float ProjectileLifetimeSeconds = 3.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float SweepRadiusCentimeters = 50.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float MaximumHomingTurnRateDegreesPerSecond = 0.0f;

	bool IsWellFormed() const;
	void AddToStableHash(uint64& Hash) const;
};

/** One committed, immutable Wingman weapon channel in the reliable projection. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanWeaponChannelConfig
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FGuLiWeaponBindingKey Binding;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FName SkillId;

	/** Action catalog/entry identity. It is not the growth or execution identity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FGameplayTag AbilityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	EGuLiWingmanWeaponKind Kind = EGuLiWingmanWeaponKind::BasicAutomatic;

	/** Empty for independent automatic slots; explicit for shared active cooldowns. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FName CooldownGroupId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	bool bEnabled = false;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities|Weapon")
	uint32 ProfileRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities|Weapon")
	uint32 DefinitionRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities|Weapon")
	uint64 DefinitionChecksum = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FGuLiWingmanWeaponRuntimeConfig Runtime;

	bool IsWellFormed() const;
	void AddToStableHash(uint64& Hash) const;
};

/**
 * Reliable, component-independent projection consumed by the current Lease Owner and
 * its backup. Transport reliability/ACK belongs to the relay component; this
 * value is the immutable protocol-v9 payload and its deterministic hash.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroupAbilityConfigSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuid ShipInstanceId;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuid OwnerPlayerGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FName WingmanTypeId;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 ShipGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 GroupGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 AbilitySetRevision = 0u;

	/** Complete committed channel-list revision, independent from formation commands. */
	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 LoadoutRevision = 0u;

	/** Reliable projection sequence; independent from the ability-set grant revision. */
	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 SnapshotRevision = 0u;

	/** False is an explicit replicated tombstone, used when the old group ends. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	bool bGroupAbilitiesValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGameplayTag FormationAbilityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGameplayTag BasicWeaponAbilityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGameplayTag MissileAbilityId;

	/** Canonical protocol-v9 weapon truth. Legacy fixed fields below are mirrors only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	TArray<FGuLiWingmanWeaponChannelConfig> WeaponChannels;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 FormationDefinitionRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint64 FormationDefinitionChecksum = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 BasicWeaponDefinitionRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint64 BasicWeaponDefinitionChecksum = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 MissileDefinitionRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint64 MissileDefinitionChecksum = 0u;

	/** Stable numeric projection; backup owners never need an AbilitySpec or DataAsset lookup. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuLiWingmanFormationRuntimeConfig FormationRuntime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuLiWingmanWeaponRuntimeConfig BasicWeaponRuntime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuLiWingmanWeaponRuntimeConfig MissileRuntime;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 FormationCommandRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 EffectiveClientSimTick = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint64 SnapshotHash = 0u;

	uint64 ComputeStableHash() const;
	void RefreshHash();
	bool HasRequiredV1Abilities() const;
	const FGuLiWingmanWeaponChannelConfig* FindWeaponChannel(
		const FGuLiWeaponBindingKey& Binding) const;
	const FGuLiWingmanWeaponChannelConfig* FindFirstWeaponChannel(
		EGuLiWingmanWeaponKind Kind) const;
	bool IsUsableByLeaseOwner() const { return bGroupAbilitiesValid && IsWellFormed(); }
	bool IsWellFormed() const;
	bool HasSameVersion(const FGuLiGroupAbilityConfigSnapshot& Other) const;
};

/** Canonical FNV-1a helpers shared by definition and snapshot checksums. */
namespace GuLiShipAbilityHash
{
	inline constexpr uint64 OffsetBasis = 14695981039346656037ull;
	inline constexpr uint64 Prime = 1099511628211ull;

	GULISTRIKE_API void AddUInt32(uint64& Hash, uint32 Value);
	GULISTRIKE_API void AddUInt64(uint64& Hash, uint64 Value);
	GULISTRIKE_API void AddFloat(uint64& Hash, float Value);
	GULISTRIKE_API void AddBool(uint64& Hash, bool bValue);
	GULISTRIKE_API void AddString(uint64& Hash, const FString& Value);
	GULISTRIKE_API void AddTag(uint64& Hash, FGameplayTag Tag);
	GULISTRIKE_API uint64 Finish(uint64 Hash);
}
