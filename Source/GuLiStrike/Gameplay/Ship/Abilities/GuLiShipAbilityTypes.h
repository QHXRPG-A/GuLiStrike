// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GuLiShipAbilityTypes.generated.h"

inline constexpr uint32 GULI_WINGMAN_PROTOCOL_VERSION = 7u;

/** The three group-level Ship GAS slots in the first vertical slice. */
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

GULISTRIKE_API FGameplayTag GuLiGetShipAbilitySlotTag(EGuLiShipAbilitySlot Slot);
GULISTRIKE_API bool GuLiIsPersistentShipAbilitySlot(EGuLiShipAbilitySlot Slot);

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

	friend bool operator==(const FGuLiShipAbilityLoadoutState& Lhs, const FGuLiShipAbilityLoadoutState& Rhs)
	{
		return Lhs.Revision == Rhs.Revision && Lhs.AbilityIds == Rhs.AbilityIds;
	}

	friend bool operator!=(const FGuLiShipAbilityLoadoutState& Lhs, const FGuLiShipAbilityLoadoutState& Rhs)
	{
		return !(Lhs == Rhs);
	}
};

/** Stable identity/revision context supplied by the authoritative Ship/group host. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipAbilityProjectionContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGuid ShipInstanceId;

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
		return ShipInstanceId.IsValid() && ShipGeneration != 0u && GroupGeneration != 0u;
	}

	friend bool operator==(const FGuLiShipAbilityProjectionContext& Lhs, const FGuLiShipAbilityProjectionContext& Rhs)
	{
		return Lhs.ShipInstanceId == Rhs.ShipInstanceId
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
 * inspecting the Ship ASC. These are Wingman simulation constants, not Ship GAS
 * attributes.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanFormationRuntimeConfig
{
	GENERATED_BODY()

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
	float InnerAngularSpeedRadiansPerSecond = 0.08f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Formation")
	float OuterAngularSpeedRadiansPerSecond = 0.06f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MinimumSpeedCentimetersPerSecond = 3000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float CruiseSpeedCentimetersPerSecond = 4500.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float CatchUpSpeedCentimetersPerSecond = 7500.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MaximumAccelerationCentimetersPerSecondSquared = 1000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MaximumDecelerationCentimetersPerSecondSquared = 800.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Flight")
	float MaximumTurnRateDegreesPerSecond = 20.0f;

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
	float RangeCentimeters = 150000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float CooldownSeconds = 2.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	float TargetConeHalfAngleDegrees = 20.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	bool bRequiresLineOfSight = true;

	bool IsWellFormed() const;
	void AddToStableHash(uint64& Hash) const;
};

/**
 * Reliable, ASC-independent projection consumed by the current Lease Owner and
 * its backup. Transport reliability/ACK belongs to the relay component; this
 * value is the immutable protocol-v7 payload and its deterministic hash.
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
	uint32 ShipGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 GroupGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Ship|Abilities")
	uint32 AbilitySetRevision = 0u;

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
