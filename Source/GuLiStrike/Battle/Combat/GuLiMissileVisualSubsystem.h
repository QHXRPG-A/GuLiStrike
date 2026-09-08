// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiMissileVisualSubsystem.generated.h"

/** Reliable logical-launch payload. Clients may reconstruct Niagara/audio without an Actor. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiMissileVisualLaunchDTO
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuid MissileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuid RootEventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuLiWeaponBindingKey WeaponBinding;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FName SkillId;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 ProfileRevision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuLiWingmanHandle Emitter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuLiTargetHandle Target;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FVector_NetQuantize Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FVector_NetQuantize Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	float ServerWorldTimeSeconds = 0.0f;

	bool IsWellFormed() const;
};

/** Unreliable correction. Sequence makes reordering harmless. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiMissileVisualCorrectionDTO
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuid MissileId;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 SimulationSequence = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FVector_NetQuantize Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FVector_NetQuantize Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	float ServerWorldTimeSeconds = 0.0f;

	bool IsWellFormed() const;
};

/** Reliable terminal tombstone. Late unreliable corrections are ignored thereafter. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiMissileVisualTerminalDTO
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuid MissileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuid RootEventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuLiWeaponBindingKey WeaponBinding;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FName SkillId;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 ProfileRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 SimulationSequence = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	EGuLiLogicalMissileTerminalReason Reason = EGuLiLogicalMissileTerminalReason::Invalid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	float ServerWorldTimeSeconds = 0.0f;

	bool IsWellFormed() const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiMissileVisualState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuid MissileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuid RootEventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuLiWeaponBindingKey WeaponBinding;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FName SkillId;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 ProfileRevision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuLiWingmanHandle Emitter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FGuLiTargetHandle Target;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Missile|Visual")
	uint32 SimulationSequence = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Missile|Visual")
	float LastServerWorldTimeSeconds = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FGuLiMissileVisualLaunchSignature, const FGuLiMissileVisualLaunchDTO&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FGuLiMissileVisualCorrectionSignature, const FGuLiMissileVisualCorrectionDTO&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FGuLiMissileVisualTerminalSignature, const FGuLiMissileVisualTerminalDTO&, Event);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiMissileVisualLaunchNative, const FGuLiMissileVisualLaunchDTO&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiMissileVisualCorrectionNative, const FGuLiMissileVisualCorrectionDTO&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiMissileVisualTerminalNative, const FGuLiMissileVisualTerminalDTO&);

/**
 * Client/listen-only, non-Actor visual reconstruction state. It owns no combat
 * truth and never sends data back to authority.
 */
UCLASS()
class GULISTRIKE_API UGuLiMissileVisualSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	void BeginEpoch(uint32 NewMatchEpoch);
	bool ApplyLaunch(const FGuLiMissileVisualLaunchDTO& Event);
	bool ApplyCorrection(const FGuLiMissileVisualCorrectionDTO& Event);
	bool ApplyTerminal(const FGuLiMissileVisualTerminalDTO& Event);

	UFUNCTION(BlueprintPure, Category = "Combat|Missile|Visual")
	bool TryGetVisualState(const FGuid& MissileId, FGuLiMissileVisualState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Combat|Missile|Visual")
	int32 GetActiveVisualCount() const { return ActiveVisuals.Num(); }

	uint32 GetVisualMatchEpoch() const { return MatchEpoch; }

	UPROPERTY(BlueprintAssignable, Category = "Combat|Missile|Visual")
	FGuLiMissileVisualLaunchSignature OnVisualLaunch;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Missile|Visual")
	FGuLiMissileVisualCorrectionSignature OnVisualCorrection;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Missile|Visual")
	FGuLiMissileVisualTerminalSignature OnVisualTerminal;

	FGuLiMissileVisualLaunchNative OnVisualLaunchNative;
	FGuLiMissileVisualCorrectionNative OnVisualCorrectionNative;
	FGuLiMissileVisualTerminalNative OnVisualTerminalNative;

private:
	void RememberTerminal(const FGuid& MissileId, uint32 Sequence);

	TMap<FGuid, FGuLiMissileVisualState> ActiveVisuals;
	TMap<FGuid, uint32> TerminalSequences;
	TArray<FGuid> TerminalOrder;
	uint32 MatchEpoch = 0u;
	int32 MaximumRememberedTerminals = 512;
};
