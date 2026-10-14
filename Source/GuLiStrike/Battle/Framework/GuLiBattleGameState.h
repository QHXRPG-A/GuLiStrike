// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GameFramework/GameState.h"
#include "GuLiBattleGameState.generated.h"

/** 本地房间席位条目；保留历史反射名称以兼容现有蓝图。 */
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

	/** 公共战局元数据握手完成；不等于士兵名册/姿态流已经就绪。 */
	UPROPERTY(BlueprintReadOnly, Category = "Battle|Lobby")
	bool bBattleReady = false;

	/** 旧字段继续表示士兵流就绪，避免公共握手绕过名册校验。 */
	UPROPERTY(BlueprintReadOnly, Category = "Commander|Lobby")
	bool bSyncReady = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiCommanderRoleSlotsChangedSignature);

/** Local room metadata view populated by the coordination session. */
UCLASS()
class GULISTRIKE_API AGuLiBattleGameState : public AGameState
{
	GENERATED_BODY()

public:
	AGuLiBattleGameState();
	virtual ~AGuLiBattleGameState() override;
	virtual void BeginPlay() override;

    // No local allocation: callers apply identities and seats accepted by the coordination session.
    bool ApplyHybridMatchIdentity(const FGuid& Identity,uint64 Epoch);
    bool ClaimExactRoleSlot(const FGuid& PlayerGuid,uint8 SlotIndex);

	/** 应用已提交的席位释放；仅在席位仍属于 ExpectedPlayerGuid 时生效，避免旧退出回调清掉新玩家。 */
	void ReleaseRoleSlot(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid);

	/** 按席位及玩家 GUID 核对，仅更新公共战局握手状态。 */
	void SetRoleSlotBattleReady(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid, bool bReady);

	/** 把士兵流就绪位映射到公共席位目录；旧 API 名称保持兼容。 */
	void SetRoleSlotSyncReady(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid, bool bReady);

	uint16 GetProtocolVersion() const { return ProtocolVersion; }

	uint32 GetMatchEpoch() const { return MatchEpoch; }

	UFUNCTION(BlueprintPure, Category = "Commander|Match")
	FGuid GetMatchId() const { return MatchId; }

	UFUNCTION(BlueprintPure, Category = "Commander|Lobby")
	TArray<FGuLiCommanderRoleSlotState> GetRoleSlots() const { return RoleSlots; }

	// 本进程通知；名字中的 Multicast 指多订阅者委托，不是 NetMulticast RPC。
	UPROPERTY(BlueprintAssignable, Category = "Commander|Lobby")
	FGuLiCommanderRoleSlotsChangedSignature OnRoleSlotsChanged;

private:
    friend class UGuLiHybridWorldSubsystem;
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
	void OnRep_MatchEpoch();

	// Metadata view populated by the coordination session.
	UPROPERTY()
	uint16 ProtocolVersion = GULI_BATTLE_PROTOCOL_VERSION;

	// 当前战局的非零隔离标识；与每连接的 SyncGeneration、每帧 FrameSequence 不同。
	UPROPERTY()
	uint32 MatchEpoch = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Commander|Match", meta = (AllowPrivateAccess = "true"))
	FGuid MatchId;

	UPROPERTY()
	TArray<FGuLiCommanderRoleSlotState> RoleSlots;

};
