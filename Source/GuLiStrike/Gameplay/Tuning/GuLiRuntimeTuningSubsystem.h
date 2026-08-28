// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiRuntimeTuningSubsystem.generated.h"

class AGuLiStrikeShip;

/**
 * Per-World, non-persistent runtime tuning owner.
 *
 * The registry is an explicit whitelist. DataTable values replace C++
 * fallbacks once during World initialization; accepted GM overrides then take
 * precedence until this World is destroyed.
 */
UCLASS()
class GULISTRIKE_API UGuLiRuntimeTuningSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	FGuLiRuntimeTuningResult GetValue(const FString& Key) const;
	TArray<FGuLiRuntimeTuningEntryView> ListValues(const FString& Prefix = FString()) const;
	FGuLiRuntimeTuningResult SetValue(const FString& Key, const FString& ValueText);
	TArray<FGuLiRuntimeTuningResult> ResetValues(const FString& KeyOrAll);

	FGuLiSoldierRuntimeTuningValues GetBaselineSoldierValues() const;
	FGuLiSoldierRuntimeTuningValues GetEffectiveSoldierValues() const;
	FGuLiShipRuntimeTuningValues GetEffectiveShipValues() const;
	uint32 GetTuningRevision() const { return TuningRevision; }

	/** Applies the current reserved GM.Runtime layer to a newly spawned Ship. */
	bool ApplyCurrentShipTuning(AGuLiStrikeShip& Ship) const;

	/** Called by authority only after Mass shared movement parameters commit. */
	void NotifySoldierMovementSpeedCommitted(
		float CommittedMoveSpeedCmPerSecond,
		int32 AppliedEntityCount);

	/** Pure permission rule used by the command adapter and automation tests. */
	static bool IsMutationAllowedForNetMode(ENetMode NetMode);

private:
	void LoadSoldierBaselines();
	int32 ApplyEffectiveSoldierValues();
	int32 ApplyEffectiveShipValues() const;
	void ApplyChangedResults(TArray<FGuLiRuntimeTuningResult*>& ChangedResults);
	void AdvanceRevisionAndPublish(float CommittedMoveSpeedCmPerSecond);
	void PublishReplicatedState(float CommittedMoveSpeedCmPerSecond) const;
	FGuLiRuntimeTuningResult MakeMutationRejectedResult(const FString& Key) const;

	FGuLiRuntimeTuningRegistry Registry;
	uint32 TuningRevision = 0u;
	bool bSoldierMovementSpeedPublicationPending = false;
};
