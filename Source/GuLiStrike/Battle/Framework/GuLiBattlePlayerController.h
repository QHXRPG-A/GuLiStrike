// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GuLiBattlePlayerController.generated.h"

class UGuLiPlayerNetSyncComponent;

/** 公共玩家连接：服务器与拥有客户端各有实例；不绑定指挥、飞行或载具输入。 */
UCLASS()
class GULISTRIKE_API AGuLiBattlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGuLiBattlePlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// 引擎在 Pawn 销毁前通知其 Controller；只把服务器真实死亡交给公共 GameMode 排队复活。
	virtual void PawnPendingDestroy(APawn* InPawn) override;

	// 本地访问拥有连接上的组件；真正的跨端通信声明在组件内，此 Getter 不是 RPC。
	UFUNCTION(BlueprintPure, Category = "Battle|Network")
	UGuLiPlayerNetSyncComponent* GetPlayerNetSyncComponent() const { return PlayerNetSyncComponent; }

	// 保留历史子对象名以兼容 Commander 蓝图；派生构造函数可替换为专业网络组件的子类。
	static const FName PlayerNetSyncComponentName;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 防止退出清理 Pawn 时把本连接重新排入出生队列。
	bool bEndingPlay = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle|Network", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGuLiPlayerNetSyncComponent> PlayerNetSyncComponent;
};
