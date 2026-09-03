// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GameFramework/GameMode.h"
#include "TimerManager.h"
#include "GuLiBattleGameMode.generated.h"

class AGuLiBattlePlayerState;

/** 服务器战局入口：统一身份、席位、按角色出生和复活，不依赖 Mass、指挥输入或载具实现。 */
UCLASS()
class GULISTRIKE_API AGuLiBattleGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AGuLiBattleGameMode();

	virtual void BeginPlay() override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void StartToLeaveMap() override;

	// 无副作用地查询角色配置的具体 Pawn；缺失/无效返回空，由出生入口转观察者，不回退默认 Pawn。
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

	/** 服务器 Pawn 销毁通知；只排队一次，真正创建/占有统一走 RestartPlayer。不是 RPC。 */
	void SchedulePlayerRespawn(APlayerController& PlayerController);
	void CancelPlayerRespawn(APlayerController& PlayerController);

protected:
	// 首次登录、重连恢复 PS、无缝切图共用；在引擎 HandleStartingNewPlayer/RestartPlayer 前分配。
	virtual void GenericPlayerInitialization(AController* Controller) override;
	virtual void HandleMatchHasEnded() override;
	virtual void HandleLeavingMap() override;
	virtual void HandleMatchAborted() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 仅影响后续出生/复活；调整配置不会自动替换已被 Possess 的 Pawn。
	UPROPERTY(EditDefaultsOnly, Category = "Battle|Spawning")
	TMap<EGuLiCommanderRole, TSubclassOf<APawn>> RolePawnClasses;

	// 默认保持原顺序：双方指挥官、地面、空中。测试地图可以把 Air/Ground 提前，不能跳过席位校验。
	UPROPERTY(EditDefaultsOnly, Category = "Battle|Spawning")
	TArray<EGuLiCommanderRole> InitialRolePriority;

	UPROPERTY(EditDefaultsOnly, Category = "Battle|Respawn")
	bool bRespawnPlayers = true;

	// 0 秒也延迟到下一 Tick，避免在旧 Pawn 的销毁调用栈中重入出生流程。
	UPROPERTY(EditDefaultsOnly, Category = "Battle|Respawn", meta = (ClampMin = "0.0", Units = "s"))
	float PlayerRespawnDelaySeconds = 0.0f;

private:
	void AssignPlayerRole(APlayerController& PlayerController);
	void MovePlayerToObserver(APlayerController& PlayerController, const TCHAR* Reason);
	UClass* ResolveConfiguredPawnClass(EGuLiCommanderRole Role) const;
	bool HasCurrentRoleSlot(const AGuLiBattlePlayerState& PlayerState) const;
	bool CanSpawnPlayer(APlayerController& PlayerController) const;
	void StopPendingRespawns();
	void HandleWingmanOwnerDisconnected(
		APlayerController& ExitingController,
		const AGuLiBattlePlayerState& ExitingPlayerState);
	void TryAssignWaitingWingmanGroups();

	// 仅服务器的延时工作；弱指针不延长玩家连接寿命，退出/结束/切图时清理。
	TMap<TWeakObjectPtr<APlayerController>, FTimerHandle> PendingRespawns;
	TSet<TWeakObjectPtr<APlayerController>> ExitingPlayers;
	bool bStopSpawning = false;
};
