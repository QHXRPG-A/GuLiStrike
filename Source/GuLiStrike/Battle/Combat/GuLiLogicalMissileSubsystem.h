// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiLogicalMissileSubsystem.generated.h"

UENUM(BlueprintType)
enum class EGuLiLogicalMissileTerminalReason : uint8
{
	Impact = 0,
	TargetLost,
	Expired,
	Blocked,
	MatchEpochEnded,
	Invalid
};

/** Authority request for one logical missile. It creates no Actor. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiLogicalMissileLaunchRequest
{
	GENERATED_BODY()

	/** Frozen at launch. A missile from an older match can never damage the next epoch. */
	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuid MissileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuid ShotId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuLiTargetHandle Source;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuLiWingmanHandle Emitter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuLiTargetHandle Target;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FVector LaunchPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FVector LaunchDirection = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile", meta = (ClampMin = "1.0"))
	float SpeedCentimetersPerSecond = 30000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile", meta = (ClampMin = "0.0"))
	float TurnRateDegreesPerSecond = 90.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile", meta = (ClampMin = "0.0"))
	float SweepRadiusCentimeters = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile", meta = (ClampMin = "0.01"))
	float MaximumLifetimeSeconds = 8.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile", meta = (ClampMin = "0.0"))
	float Damage = 100.0f;

	bool IsWellFormed() const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiLogicalMissileState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuid MissileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuid ShotId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuLiTargetHandle Source;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuLiWingmanHandle Emitter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuLiTargetHandle Target;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FVector Velocity = FVector::ZeroVector;

	/** Monotonic fixed-step sequence used by unreliable visual corrections. */
	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile")
	uint32 SimulationSequence = 0u;

	float SpeedCentimetersPerSecond = 0.0f;
	float TurnRateDegreesPerSecond = 0.0f;
	float SweepRadiusCentimeters = 0.0f;
	float MaximumLifetimeSeconds = 0.0f;
	float AgeSeconds = 0.0f;
	float Damage = 0.0f;
	float CorrectionAccumulator = 0.0f;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiLogicalMissileTerminalEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuid MissileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	EGuLiLogicalMissileTerminalReason Reason = EGuLiLogicalMissileTerminalReason::Invalid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile")
	uint32 SimulationSequence = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile")
	FGuLiDamageCommitResult DamageResult;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiLogicalMissileLaunchEvent, const FGuLiLogicalMissileState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiLogicalMissileCorrectionEvent, const FGuLiLogicalMissileState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiLogicalMissileFinishedEvent, const FGuLiLogicalMissileTerminalEvent&);

/** Server-only 30 Hz logical missile service. Visual clients consume its events through a relay. */
UCLASS()
class GULISTRIKE_API UGuLiLogicalMissileSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Returns false without partial creation if the ID/request is invalid or duplicated. */
	bool LaunchMissile(const FGuLiLogicalMissileLaunchRequest& Request);

	/** Pure preflight for the all-or-nothing Flight transaction. */
	bool CanLaunchFlightSalvo(TConstArrayView<FGuLiLogicalMissileLaunchRequest> Requests) const;

	/** A prefiltered Flight salvo; all records are created or none are. */
	bool LaunchFlightSalvo(TConstArrayView<FGuLiLogicalMissileLaunchRequest> Requests, int32& OutLaunchedCount);

	bool ContainsMissile(const FGuid& MissileId) const;
	int32 GetActiveMissileCount() const { return ActiveMissiles.Num(); }
	uint64 GetSimulationStepCount() const { return SimulationStepCount; }

	FGuLiLogicalMissileLaunchEvent OnLaunch;
	FGuLiLogicalMissileCorrectionEvent OnCorrection;
	FGuLiLogicalMissileFinishedEvent OnFinished;

private:
	bool ValidateLaunchRequest(const FGuLiLogicalMissileLaunchRequest& Request) const;
	void CreateMissileUnchecked(const FGuLiLogicalMissileLaunchRequest& Request);
	void StepMissiles(float FixedDeltaSeconds);
	bool StepMissile(FGuLiLogicalMissileState& Missile, float FixedDeltaSeconds,
		FGuLiLogicalMissileTerminalEvent& OutTerminal);
	void FinishMissile(int32 Index, const FGuLiLogicalMissileTerminalEvent& Event);
	static FVector TurnDirection(const FVector& CurrentDirection, const FVector& DesiredDirection,
		float MaximumRadians);

	TArray<FGuLiLogicalMissileState> ActiveMissiles;
	TSet<FGuid> ActiveMissileIds;
	UPROPERTY(Transient)
	TObjectPtr<UGuLiDamageLedgerSubsystem> DamageLedger;
	double FixedStepAccumulator = 0.0;
	uint64 SimulationStepCount = 0u;
};
