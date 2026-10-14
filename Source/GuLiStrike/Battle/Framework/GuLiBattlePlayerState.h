// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Skills/GuLiWeaponChannelTypes.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiBattlePlayerState.generated.h"

class UGuLiCommanderSkillComponent;
class UGuLiShipBuildComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiCommanderPlayerStateChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiWeaponChangeResultSignature, const FGuLiWeaponChangeResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FGuLiResourceInventoryChangedSignature,
	const FGuLiResourceAmounts&,
	Inventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiResourcePrivateStateChangedSignature);

/** 协调成员状态的本地玩家镜像；身份、席位和就绪由 Hybrid 提交结果应用。 */
UCLASS()
class GULISTRIKE_API AGuLiBattlePlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AGuLiBattlePlayerState();
	UFUNCTION(BlueprintPure, Category="Ship|Build") UGuLiShipBuildComponent* GetShipBuild() const { return ShipBuild; }
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UFUNCTION(BlueprintPure, Category="Commander|Skills") UGuLiCommanderSkillComponent* GetCommanderSkills() const { return CommanderSkills; }
	/** Server-local automatic-weapon/upgrades command; no client RPC. */

	/** Committed, read-only equipment view; locked/empty slots remain visible for progression. */
	UFUNCTION(BlueprintPure, Category="Battle|Weapons")
	TArray<FGuLiWeaponChannelView> GetWeaponChannels(EGuLiWeaponDomain Domain) const;

	/** Empty SkillId unequips the selected slot. RequestId must be reused when retrying. */
	UFUNCTION(BlueprintCallable, Category="Battle|Weapons")
	void ServerRequestEquipWeapon(FGuid RequestId, FGuLiWeaponBindingKey Binding, FName SkillId, int64 ExpectedLoadoutRevision);

	UPROPERTY(BlueprintAssignable, Category="Battle|Weapons")
	FGuLiWeaponChangeResultSignature OnWeaponChangeResult;

	UFUNCTION(BlueprintPure, Category="Battle|Weapons")
	FGuLiWeaponChangeResult GetLastWeaponChangeResult() const { return LastWeaponChangeResult; }

	static constexpr uint8 InvalidSlotIndex = MAX_uint8;

	// 跨派生类的 PlayerState 迁移：只保留身份/席位，新连接或新战局清除两种就绪资格。
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;

	/** 仅服务器创建缺失 GUID；已有 GUID 保留，供重连/无缝切图路径恢复身份。 */
	void EnsureServerPlayerGuid();

	/** 服务器本地修改阵营/角色/席位并撤销旧就绪状态；远端客户端不能通过此普通函数发起 RPC。 */
	void SetServerRoleAssignment(EGuLiTeam NewTeam, EGuLiCommanderRole NewRole, uint8 NewSlotIndex);
	/** 服务器公共握手通过后设置；不能因此放行士兵姿态或指挥请求。 */
	void SetServerBattleReady(bool bNewBattleReady);

	/** 服务器完成士兵名册握手后设置，与角色是否为 Commander 无关。 */
	void SetServerSoldierStreamReady(bool bNewSyncReady);

	// 历史 API 继续只操作士兵流门，供旧调用方兼容使用。
	void SetServerSyncReady(bool bNewSyncReady) { SetServerSoldierStreamReady(bNewSyncReady); }
	void SetServerObserver();

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	FGuid GetPlayerGuid() const { return PlayerGuid; }
    UFUNCTION(BlueprintPure,Category="Battle|Role") int64 GetSeatVersion() const { return SeatVersion; }
    void SetHybridSeatVersion(uint64 Version);

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	EGuLiTeam GetTeam() const { return Team; }

	UFUNCTION(BlueprintPure, Category = "Battle|Player")
	EGuLiCommanderRole GetBattleRole() const { return CommanderRole; }

	UFUNCTION(BlueprintPure, Category = "Battle|Player")
	uint8 GetBattleSlotIndex() const { return SlotIndex; }

	// 旧蓝图函数保留为公共身份字段的兼容访问，不创建第二份身份。
	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	EGuLiCommanderRole GetCommanderRole() const { return GetBattleRole(); }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	uint8 GetCommanderSlotIndex() const { return GetBattleSlotIndex(); }

	UFUNCTION(BlueprintPure, Category = "Battle|Player")
	bool IsBattleReady() const { return bBattleReady; }

	UFUNCTION(BlueprintPure, Category = "Battle|Player")
	bool IsSoldierStreamReady() const { return bSyncReady; }

	// 旧 API 仍返回士兵流状态，公共 BattleReady 不能替代它。
	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	bool IsSyncReady() const { return IsSoldierStreamReady(); }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	bool IsCommander() const
	{
		return CommanderRole == EGuLiCommanderRole::Commander && Team != EGuLiTeam::Unassigned;
	}

	/** Owner-only view of this player's team inventory. Other teams never receive it. */
	UFUNCTION(BlueprintPure, Category = "Resources|Economy")
	FGuLiResourceAmounts GetResourceInventory() const { return ResourcePrivateState.Inventory; }

	/** Full connection-owned team view; never copied to another session or an opposing client. */
	const FGuLiTeamResourcePrivateState& GetResourcePrivateState() const
	{
		return ResourcePrivateState;
	}
	const FGuLiMiningVehiclePrivateState* FindMiningVehiclePrivateState(
		FGuLiControllableActorId StableActorId) const
	{
		return ResourcePrivateState.FindMiningVehicle(StableActorId);
	}

	/** Server-local mirror entry used by the Commander resource adapter. */
	bool SetServerResourcePrivateState(FGuLiTeamResourcePrivateState NewState);

	UPROPERTY(BlueprintAssignable, Category = "Resources|Economy")
	FGuLiResourceInventoryChangedSignature OnResourceInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Resources|Economy")
	FGuLiResourcePrivateStateChangedSignature OnResourcePrivateStateChanged;

	// 本地 UI/逻辑通知，由已提交成员状态应用触发。
	UPROPERTY(BlueprintAssignable, Category = "Commander|Player")
	FGuLiCommanderPlayerStateChangedSignature OnCommanderPlayerStateChanged;

private:
    UPROPERTY() int64 SeatVersion=1;
	friend class UGuLiHybridWorldSubsystem;
	bool ApplyHybridRole(const FGuid& Identity,EGuLiTeam NewTeam,EGuLiCommanderRole NewRole,uint8 NewSlot,bool bReady);
	UPROPERTY(VisibleAnywhere, Category="Ship|Build") TObjectPtr<UGuLiShipBuildComponent> ShipBuild;
	UFUNCTION()
	void ClientReceiveWeaponChangeResult(const FGuLiWeaponChangeResult& Result);
	/** Shared server handler for the RPC; returns the immediate result before any fixed-step commit. */
	FGuLiWeaponChangeResult ProcessEquipWeaponRequest(FGuid RequestId, const FGuLiWeaponBindingKey& Binding,
		FName SkillId, int64 ExpectedLoadoutRevision);
	void HandleArmyWeaponLoadoutCommitted(uint32 Revision);
#if WITH_DEV_AUTOMATION_TESTS
	friend class FGuLiWeaponEquipmentTransactionTest;
#endif
	struct FWeaponRequestRecord
	{
		int64 ExpectedRevision = 0;
		FGuLiWeaponChangeResult Result;
	};
	TMap<FGuid, FWeaponRequestRecord> WeaponRequests;
	TArray<FGuid> WeaponRequestOrder;
	uint32 WeaponRequestEpoch = 0u;
	double NextWeaponRequestSeconds = 0.0;
	FGuLiWeaponChangeResult LastWeaponChangeResult;

	UPROPERTY(VisibleAnywhere, Category = "Battle|Abilities")
	TObjectPtr<UGuLiCommanderSkillComponent> CommanderSkills;

	void NotifyStateChanged();

	UFUNCTION()
	void OnRep_ResourcePrivateState(FGuLiTeamResourcePrivateState PreviousState);

	UPROPERTY()
	FGuid PlayerGuid;

	UPROPERTY()
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY()
	EGuLiCommanderRole CommanderRole = EGuLiCommanderRole::Observer;

	UPROPERTY()
	uint8 SlotIndex = InvalidSlotIndex;

	/** 本地镜像保留就绪 Getter；资格由已提交目录和世界状态决定。 */
	UPROPERTY()
	bool bBattleReady = false;

	/** 保留字段和 Getter 供玩法、UI 消费，不另建复制握手。 */
	UPROPERTY()
	bool bSyncReady = false;

	/** Session state only; intentionally omitted from CopyProperties/OverrideWith. */
	UPROPERTY()
	FGuLiTeamResourcePrivateState ResourcePrivateState;
};
