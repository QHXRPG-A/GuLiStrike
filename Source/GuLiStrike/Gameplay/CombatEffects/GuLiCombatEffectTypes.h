// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GuLiCombatEffectTypes.generated.h"

class UGuLiProjectileEffectDefinition;
class UGuLiSpellFieldDefinition;

UENUM(BlueprintType)
enum class EGuLiSpellFieldTiming : uint8 { Instant, Delayed, Periodic };

/** Authority-side values loaded from the SpellFields table and frozen when an effect is created. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiSpellFieldConfig
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Spell Field") FName ConfigId;
	UPROPERTY(BlueprintReadOnly, Category="Spell Field", meta=(ClampMin="0.0")) float Damage = 30.0f;
	UPROPERTY(BlueprintReadOnly, Category="Spell Field", meta=(ClampMin="0.0", Units="cm")) float Radius = 800.0f;
	UPROPERTY(BlueprintReadOnly, Category="Spell Field") EGuLiSpellFieldTiming Timing = EGuLiSpellFieldTiming::Instant;
	UPROPERTY(BlueprintReadOnly, Category="Spell Field", meta=(ClampMin="0.0", Units="s")) float Delay = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Spell Field", meta=(ClampMin="0.0", Units="s")) float Duration = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Spell Field", meta=(ClampMin="0.033", Units="s")) float PulseInterval = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category="Spell Field", meta=(ClampMin="0.0", Units="s")) float DissipationSeconds = 3.0f;
	bool IsValid() const;
};

/** Table-authored presentation-space weapon points, grouped by unit type and stable weapon slot. */
USTRUCT()
struct GULISTRIKE_API FGuLiWeaponMountConfig
{
	GENERATED_BODY()
	UPROPERTY() uint16 UnitTypeId = 1;
	UPROPERTY() FName SlotId = TEXT("BasicAttack");
	/** Final Crowd static-mesh local coordinates, in centimeters. */
	UPROPERTY() TArray<FVector> Muzzles;
	/** Target unit visual aim point in the same Crowd local coordinate space. */
	UPROPERTY() FVector AimOffset = FVector::ZeroVector;
	UPROPERTY() bool bCalibrated = false;
	bool IsValid() const;
};

UENUM(BlueprintType)
enum class EGuLiCombatEffectKind : uint8 { Projectile, SpellField };

UENUM(BlueprintType)
enum class EGuLiCombatEffectPhase : uint8 { Waiting, Active, Dissipating, Finished };

UENUM(BlueprintType)
enum class EGuLiCombatEffectEndReason : uint8 { None, Impact, Completed, Expired, Cancelled, EpochEnded };

/** A cast's authority-resolved provenance. No Mass entity, particle or Actor pointer crosses this boundary. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCombatEffectContext
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect") FGuLiTargetHandle Source;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect") FGuLiWingmanHandle Emitter;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect") FGuLiTargetHandle Target;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect") FGuLiWeaponBindingKey WeaponBinding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect") FName SkillId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect", meta=(ClampMin="0.0")) float Damage = 30.0f;
	/** Optional server-side correlation IDs; missing IDs are allocated by the runtime. Not a client RPC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect") FGuid ShotId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Effect") FGuid RootEventId;
	UPROPERTY() uint32 MatchEpoch = 0;
	UPROPERTY() uint32 ProfileRevision = 0;
	UPROPERTY() uint32 LoadoutRevision = 0;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiProjectileMotionSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="1", Units="cm/s")) float Speed = 6000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="0.01", Units="s")) float LiftSeconds = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="0", Units="cm")) float MinimumLiftHeight = 600.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="0", Units="cm")) float MaximumLiftHeight = 1000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="0", Units="cm")) float LateralOffset = 200.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="1", Units="cm")) float ConvergenceDistance = 1600.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="1", Units="deg/s")) float TurnRate = 240.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="0", Units="cm")) float SweepRadius = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Motion", meta=(ClampMin="0.01", Units="s")) float MaximumLifetime = 8.0f;
	bool IsValid() const;
};

/** Replicated presentation state, never a damage command. Config is frozen at launch. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCombatEffectState
{
	GENERATED_BODY()
	UPROPERTY() uint32 MatchEpoch = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FGuid EffectId;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") EGuLiCombatEffectKind Kind = EGuLiCombatEffectKind::Projectile;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") EGuLiCombatEffectPhase Phase = EGuLiCombatEffectPhase::Active;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") EGuLiCombatEffectEndReason EndReason = EGuLiCombatEffectEndReason::None;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FGuLiTargetHandle Source;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FGuLiTargetHandle Target;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") TSoftObjectPtr<UGuLiProjectileEffectDefinition> ProjectileDefinition;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") TSoftObjectPtr<UGuLiSpellFieldDefinition> FieldDefinition;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FVector_NetQuantize Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FVector_NetQuantize Velocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FVector_NetQuantize LaunchLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FVector_NetQuantize LastTargetLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FVector_NetQuantizeNormal LaunchDirection = FVector::ForwardVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FGuLiProjectileMotionSettings Motion;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") bool bFixedPoint = false;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int32 RandomSeed = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int32 VariantIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") float StartTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") float SampleTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") float ActivationTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") float EndTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") float Radius = 0.0f;
	UPROPERTY() uint32 Sequence = 0;
	bool IsWellFormed() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FGuLiCombatEffectState> : TStructOpsTypeTraitsBase2<FGuLiCombatEffectState>
{ enum { WithNetSerializer = true }; };

/** Unreliable kinematic correction; never repeats definitions, damage or the frozen launch payload. */
USTRUCT()
struct GULISTRIKE_API FGuLiCombatEffectCorrection
{
	GENERATED_BODY()
	UPROPERTY() uint32 MatchEpoch = 0;
	UPROPERTY() FGuid EffectId;
	UPROPERTY() uint32 Sequence = 0;
	UPROPERTY() FVector_NetQuantize Location = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize Velocity = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize LastTargetLocation = FVector::ZeroVector;
	UPROPERTY() float SampleTime = 0;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FGuLiCombatEffectCorrection> : TStructOpsTypeTraitsBase2<FGuLiCombatEffectCorrection>
{ enum { WithNetSerializer = true }; };

/** An accepted hitscan shot. Endpoints are complete on the first rendered frame. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCombatShotCue
{
	GENERATED_BODY()
	UPROPERTY() uint32 MatchEpoch = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FGuid ShotId;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FGuLiTargetHandle Source;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FGuLiTargetHandle Target;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FName SlotId;
	UPROPERTY() uint16 UnitTypeId = 0;
	UPROPERTY() uint8 MuzzleIndex = 0;
	/** Wingman uses logical +X attachment coordinates, independent of Commander unit mounts. */
	UPROPERTY() FVector MuzzleOffset = FVector::ZeroVector;
	UPROPERTY() float KeepAliveSeconds = 2.0f;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FVector_NetQuantize Start = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") FVector_NetQuantize End = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") float ServerTime = 0.0f;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FGuLiCombatShotCue> : TStructOpsTypeTraitsBase2<FGuLiCombatShotCue>
{ enum { WithNetSerializer = true }; };

/** Thin firing adapter input. The execution domain supplies poses; the runtime owns effect behavior. */
struct GULISTRIKE_API FGuLiCombatAttackRequest
{
	FGuLiCombatEffectContext Context;
	FName ExecutorId = TEXT("DirectSingleTarget");
	uint16 UnitTypeId = 0;
	FTransform SourceTransform = FTransform::Identity;
	FVector TargetLocation = FVector::ZeroVector;
	uint64 ShotOrdinal = 0;
	FVector MuzzleOffset = FVector::ZeroVector;
	UGuLiProjectileEffectDefinition* Projectile = nullptr;
	FGuLiSpellFieldConfig FrozenField;
	FGuLiProjectileMotionSettings Motion;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCombatEffectCounters
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int64 ProjectilesLaunched = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int64 FieldsCreated = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int64 Pulses = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int64 DamageCommits = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int64 ShotsPublished = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") int64 CandidateChecks = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effect") double LastStepMilliseconds = 0.0;
};

namespace GuLiCombatEffects
{
	GULISTRIKE_API FVector LiftPosition(const FGuLiCombatEffectState& State, float Age);
	/** Pure, fixed-step, seed-stable flight. Caller owns target refresh and authoritative swept collision. */
	GULISTRIKE_API FVector AdvanceProjectile(FGuLiCombatEffectState& State, float NewAge, float DeltaSeconds);
	GULISTRIKE_API bool IntersectsSphere(const FVector& Center, float Radius, const FGuLiCombatTargetSnapshot& Target);
	/** Number of scheduled pulses due by Now, with the periodic interval [Activation, End). */
	GULISTRIKE_API int32 PulsesDue(EGuLiSpellFieldTiming Timing, double Activation, double Duration, double Interval, double Now);
	GULISTRIKE_API FGuid DamageId(const FGuid& EffectId, int32 PulseIndex, const FGuLiTargetHandle& Target);
	GULISTRIKE_API bool IsNewerState(const FGuLiCombatEffectState& Incoming, const FGuLiCombatEffectState& Previous);
}
