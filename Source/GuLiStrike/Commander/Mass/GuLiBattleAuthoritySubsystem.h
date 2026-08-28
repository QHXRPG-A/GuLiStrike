// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"
#include "GuLiBattleAuthoritySubsystem.generated.h"

class AGuLiCommanderPlayerState;
class ANavigationData;
struct FGuLiBattleAuthorityState;

/** Keeps the PImpl delete expression out of UHT-generated translation units. */
struct FGuLiBattleAuthorityStateDeleter
{
	void operator()(FGuLiBattleAuthorityState* State) const;
};

/**
 * Server-only authority for 500 independently identified Mass Soldiers.
 *
 * ControlCohorts and OrderFormations are transient server records. FMassEntityHandle
 * remains private to this subsystem and never crosses the network contract boundary.
 */
UCLASS(Config = Game)
class GULISTRIKE_API UGuLiBattleAuthoritySubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UGuLiBattleAuthoritySubsystem();
	virtual ~UGuLiBattleAuthoritySubsystem() override;

	//~ Begin USubsystem / UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	//~ End USubsystem / UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/** Resolves a circle intent against authoritative Soldier positions and freezes temporary cohorts. */
	bool ResolveSelection(
		const AGuLiCommanderPlayerState& PlayerState,
		const FGuLiSelectionRequest& Request,
		FGuLiCommanderSelectionState& InOutSelection,
		FGuLiCommandAck& OutAck);

	/** Validates SelectionRevision and creates one transient OrderFormation per legal cohort. */
	bool IssueMove(
		const AGuLiCommanderPlayerState& PlayerState,
		const FGuLiMoveRequest& Request,
		const FGuLiCommanderSelectionState& Selection,
		FGuLiCommandAck& OutAck);

	/** Removes invalid/depleted cohorts and refreshes their alive/order summary. Authority only. */
	bool RefreshSelection(EGuLiTeam Team, FGuLiCommanderSelectionState& InOutSelection) const;

	/** Server C++ damage entry point. Already-destroyed or unknown Soldiers are rejected. */
	bool ApplyDamage(FGuLiSoldierId SoldierId, uint8 Amount);

	/** Applies validated World-session values in bulk; movement speed commits on the next 30 Hz step. */
	int32 ApplyRuntimeTuning(const FGuLiSoldierRuntimeTuningValues& Values);

	const FGuLiSoldierRuntimeTuningValues& GetBaselineRuntimeTuning() const
	{
		return BaselineRuntimeTuning;
	}

	const FGuLiSoldierRuntimeTuningValues& GetEffectiveRuntimeTuning() const
	{
		return EffectiveRuntimeTuning;
	}

	/** Movement speed currently committed to the active Mass shared parameters. */
	float GetCommittedMovementSpeedCmPerSecond() const
	{
		return MovementSpeedCentimetersPerSecond;
	}

	/** Reliable current-state payload. Continuous positions are not included. */
	void BuildSoldierStateSnapshot(TArray<FGuLiSoldierStateItem>& OutStates) const;

	/** Captures one 10 Hz unreliable movement frame, split into <=32 Soldier chunks. */
	void CaptureSoldierPoseChunks(
		TArray<FGuLiSoldierPoseChunk>& OutChunks,
		uint32 AuthorityEpoch);

	/** Local/server presentation and debug lookup; never accepts a client-authored position. */
	bool TryGetSoldierTransform(FGuLiSoldierId SoldierId, FTransform& OutTransform) const;

	int32 GetActiveOrderFormationCount() const;
	int32 GetAuthoritativeMemberCount() const;
	uint32 GetServerSimTick() const;
	bool HasSpawnedAuthorityPopulation() const;

private:
	bool IsAuthorityWorld() const;
	bool TrySpawnAuthorityPopulation();
	void DestroyAuthorityPopulation();
	void TickAuthority(float FixedDeltaSeconds);
	void TickLocalFlowFields();
	void ApplyPendingMovementSpeed();
	void NotifyMovementSpeedCommitted(int32 AppliedEntityCount) const;

	UFUNCTION()
	void HandleNavigationGenerationFinished(ANavigationData* NavigationData);

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Spawn")
	FVector RedSpawnCenter = FVector(-221397.0, 130463.0, 0.0);

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Spawn")
	FVector BlueSpawnCenter = FVector(221397.0, -130463.0, 0.0);

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Formation", meta = (ClampMin = "1000.0", Units = "cm"))
	float GroupSpacingCentimeters = 15000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Formation", meta = (ClampMin = "100.0", Units = "cm"))
	float MemberSpacingCentimeters = 1800.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float MemberAgentRadiusCentimeters = 750.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Movement", meta = (ClampMin = "1.0", Units = "cm/s"))
	float MovementSpeedCentimetersPerSecond = 3600.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Movement", meta = (ClampMin = "1.0", Units = "deg/s"))
	float FacingRateDegreesPerSecond = 90.0f;

	/** Phase-two server feature switch. Shared NavMesh paths remain the fallback. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|FlowField")
	bool bEnableLocalFlowField = false;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|FlowField", meta = (ClampMin = "1"))
	int32 FlowFieldWalkabilitySamplesPerTick = 512;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|FlowField", meta = (ClampMin = "100.0", Units = "cm"))
	float FlowFieldCorridorHalfWidthCentimeters = 15000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Authority|Life", meta = (ClampMin = "0.0", Units = "s"))
	float WreckLifetimeSeconds = 5.0f;

	FGuLiSoldierRuntimeTuningValues BaselineRuntimeTuning;
	FGuLiSoldierRuntimeTuningValues EffectiveRuntimeTuning;
	TOptional<float> PendingMovementSpeedCmPerSecond;

	TUniquePtr<FGuLiBattleAuthorityState, FGuLiBattleAuthorityStateDeleter> AuthorityState;
};
