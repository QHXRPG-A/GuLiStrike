// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/GameState.h"
#include "GuLiCommanderGameState.generated.h"

USTRUCT(BlueprintType)
struct FGuLiCommanderRoleSlotState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Commander|Lobby")
	uint8 SlotIndex = MAX_uint8;

	UPROPERTY(BlueprintReadOnly, Category = "Commander|Lobby")
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(BlueprintReadOnly, Category = "Commander|Lobby")
	EGuLiCommanderRole Role = EGuLiCommanderRole::Unassigned;

	UPROPERTY(BlueprintReadOnly, Category = "Commander|Lobby")
	FGuid PlayerGuid;

	UPROPERTY(BlueprintReadOnly, Category = "Commander|Lobby")
	bool bOccupied = false;

	UPROPERTY(BlueprintReadOnly, Category = "Commander|Lobby")
	bool bSyncReady = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiCommanderRoleSlotsChangedSignature);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FGuLiCommanderRuntimeTuningChangedSignature,
	float,
	uint32);

/**
 * Replicated match metadata and the authoritative 5v5 role-slot directory.
 * Role-slot mutations are server-only; clients consume the replicated array.
 */
UCLASS()
class AGuLiCommanderGameState : public AGameState
{
	GENERATED_BODY()

public:
	AGuLiCommanderGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Initializes a new match identity. Safe to call more than once on the server. */
	void InitializeServerMatchState();

	/**
	 * Claims one of the ten unique gameplay slots. PreferredSlotIndex is used by
	 * seamless-travel/reconnect state when available; otherwise a balanced order is used.
	 */
	bool ClaimRoleSlot(
		const FGuid& PlayerGuid,
		uint8 PreferredSlotIndex,
		uint8& OutSlotIndex,
		EGuLiTeam& OutTeam,
		EGuLiCommanderRole& OutRole);

	/** Releases a slot only when it still belongs to ExpectedPlayerGuid. */
	void ReleaseRoleSlot(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid);

	/** Mirrors the owner bootstrap gate into the public role-slot directory. */
	void SetRoleSlotSyncReady(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid, bool bReady);

	/** Publishes the one-per-World Soldier prediction scalar. Server only. */
	void SetAuthoritativeSoldierMovementTuning(float EffectiveMoveSpeedCmPerSecond, uint32 TuningRevision);

	uint16 GetProtocolVersion() const { return ProtocolVersion; }

	uint32 GetMatchEpoch() const { return MatchEpoch; }

	UFUNCTION(BlueprintPure, Category = "Commander|Match")
	FGuid GetMatchId() const { return MatchId; }

	UFUNCTION(BlueprintPure, Category = "Commander|Lobby")
	TArray<FGuLiCommanderRoleSlotState> GetRoleSlots() const { return RoleSlots; }

	UFUNCTION(BlueprintPure, Category = "Commander|Tuning")
	float GetEffectiveSoldierMoveSpeedCmPerSecond() const
	{
		return EffectiveSoldierMoveSpeedCmPerSecond;
	}

	uint32 GetRuntimeTuningRevision() const { return RuntimeTuningRevision; }

	UPROPERTY(BlueprintAssignable, Category = "Commander|Lobby")
	FGuLiCommanderRoleSlotsChangedSignature OnRoleSlotsChanged;

	FGuLiCommanderRuntimeTuningChangedSignature OnRuntimeTuningChanged;

private:
	void InitializeDefaultRoleSlots();
	bool TryClaimRoleSlot(
		uint8 SlotIndex,
		const FGuid& PlayerGuid,
		uint8& OutSlotIndex,
		EGuLiTeam& OutTeam,
		EGuLiCommanderRole& OutRole);
	void NotifyRoleSlotsChanged();

	UFUNCTION()
	void OnRep_RoleSlots();

	UFUNCTION()
	void OnRep_RuntimeTuning();

	UPROPERTY(Replicated)
	uint16 ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION;

	UPROPERTY(Replicated)
	uint32 MatchEpoch = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Commander|Match", meta = (AllowPrivateAccess = "true"))
	FGuid MatchId;

	UPROPERTY(ReplicatedUsing = OnRep_RoleSlots)
	TArray<FGuLiCommanderRoleSlotState> RoleSlots;

	/** Single replicated scalar used by all client-side Soldier prediction. */
	UPROPERTY(ReplicatedUsing = OnRep_RuntimeTuning)
	float EffectiveSoldierMoveSpeedCmPerSecond = 3600.0f;

	/** Changes only when an effective runtime tuning value changes. */
	UPROPERTY(ReplicatedUsing = OnRep_RuntimeTuning)
	uint32 RuntimeTuningRevision = 0u;
};
