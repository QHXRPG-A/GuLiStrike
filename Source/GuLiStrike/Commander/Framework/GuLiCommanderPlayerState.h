// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/PlayerState.h"
#include "GuLiCommanderPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiCommanderPlayerStateChangedSignature);

/** 每位玩家的复制身份与服务器分配结果；其他相关客户端也可读取，不是仅拥有者可见。 */
UCLASS()
class AGuLiCommanderPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	static constexpr uint8 InvalidSlotIndex = MAX_uint8;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 引擎的 PlayerState 状态迁移钩子；复制身份和席位，但不沿用旧战局的同步就绪资格。
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;

	/** 仅服务器创建缺失 GUID；已有 GUID 保留，供重连/无缝切图路径恢复身份。 */
	void EnsureServerPlayerGuid();

	/** 服务器本地修改阵营/角色/席位并撤销旧就绪状态；远端客户端不能通过此普通函数发起 RPC。 */
	void SetServerRoleAssignment(EGuLiTeam NewTeam, EGuLiCommanderRole NewRole, uint8 NewSlotIndex);
	// 服务器 Bootstrap 校验通过后设置；不代表角色一定是 Commander。
	void SetServerSyncReady(bool bNewSyncReady);
	void SetServerObserver();

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	FGuid GetPlayerGuid() const { return PlayerGuid; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	EGuLiTeam GetTeam() const { return Team; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	EGuLiCommanderRole GetCommanderRole() const { return CommanderRole; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	uint8 GetCommanderSlotIndex() const { return SlotIndex; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	bool IsSyncReady() const { return bSyncReady; }

	UFUNCTION(BlueprintPure, Category = "Commander|Player")
	bool IsCommander() const
	{
		return CommanderRole == EGuLiCommanderRole::Commander && Team != EGuLiTeam::Unassigned;
	}

	// 本地 UI/逻辑通知，由服务器 setter 或客户端 RepNotify 触发，委托本身不跨网。
	UPROPERTY(BlueprintAssignable, Category = "Commander|Player")
	FGuLiCommanderPlayerStateChangedSignature OnCommanderPlayerStateChanged;

private:
	void NotifyStateChanged();

	UFUNCTION()
	void OnRep_Assignment();

	UFUNCTION()
	void OnRep_SyncReady();

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	FGuid PlayerGuid;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	EGuLiCommanderRole CommanderRole = EGuLiCommanderRole::Observer;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment)
	uint8 SlotIndex = InvalidSlotIndex;

	UPROPERTY(ReplicatedUsing = OnRep_SyncReady)
	bool bSyncReady = false;
};
