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
 * 双端可见的战局元数据与 5v5 席位目录：仅服务器修改，客户端读取复制副本。
 * 与 PlayerController 上的 OwnerOnly 选择状态不同，这里向相关客户端公开战局信息。
 */
UCLASS()
class AGuLiCommanderGameState : public AGameState
{
	GENERATED_BODY()

public:
	AGuLiCommanderGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器本地初始化；已有有效战局标识时保留，重复调用不会每次更换战局。 */
	void InitializeServerMatchState();

	/**
	 * Claims one of the ten unique gameplay slots. PreferredSlotIndex is used by
	 * seamless-travel/reconnect state when available; otherwise a balanced order is used.
	 */
	// 服务器本地申请席位；成功时填充三个 Out 参数并返回 true；失败保持无效席位/观察者输出。
	// PlayerGuid 用于重连识别，PreferredSlotIndex 仅是优先尝试，不允许抢占他人席位。
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
	// 服务器发布已提交的士兵速度（cm/s）及调参版本；本地调用，不是给客户端开放的调参 RPC。
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

	// 本进程通知；名字中的 Multicast 指多订阅者委托，不是 NetMulticast RPC。
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

	// 协议兼容版本；下列字段由 GetLifetimeReplicatedProps 注册，无 OwnerOnly 限制。
	UPROPERTY(Replicated)
	uint16 ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION;

	// 当前战局的非零隔离标识；与每连接的 SyncGeneration、每帧 FrameSequence 不同。
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
	// 速度与版本是两个复制属性；不要依赖两个 OnRep 的先后顺序来模拟原子事务。
	UPROPERTY(ReplicatedUsing = OnRep_RuntimeTuning)
	uint32 RuntimeTuningRevision = 0u;
};
