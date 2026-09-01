// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "GuLiBattlePlayerState.generated.h"

class UAbilitySystemComponent;
struct FGuLiArmySkillCommand;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiCommanderPlayerStateChangedSignature);

/** 通用玩家身份与角色复制；公共战局就绪、士兵流就绪分别维护，不互相替代。 */
UCLASS()
class GULISTRIKE_API AGuLiBattlePlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AGuLiBattlePlayerState();
	virtual void BeginPlay() override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	/** Server-local GM/gameplay entry through a real ServerOnly GameplayAbility; no client RPC. */
	bool ExecuteArmySkillCommand(const FGuLiArmySkillCommand& Command, FString& OutError);
	static constexpr uint8 InvalidSlotIndex = MAX_uint8;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
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

	// 本地 UI/逻辑通知，由服务器 setter 或客户端 RepNotify 触发，委托本身不跨网。
	UPROPERTY(BlueprintAssignable, Category = "Commander|Player")
	FGuLiCommanderPlayerStateChangedSignature OnCommanderPlayerStateChanged;

private:
	UPROPERTY(VisibleAnywhere, Category = "Battle|Abilities")
	TObjectPtr<UAbilitySystemComponent> ArmyAbilitySystem;
	bool bArmySkillAbilityGranted = false;
	void InitializeArmyAbilitySystem();

	void NotifyStateChanged();

	UFUNCTION()
	void OnRep_Assignment();

	UFUNCTION()
	void OnRep_BattleReady();

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

	/** 公共战局元数据已完成握手，和士兵名册的流资格独立复制。 */
	UPROPERTY(ReplicatedUsing = OnRep_BattleReady)
	bool bBattleReady = false;

	/** 保留字段名兼容旧复制/反射查找，仅表示士兵流就绪。 */
	UPROPERTY(ReplicatedUsing = OnRep_SyncReady)
	bool bSyncReady = false;
};
