// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GuLiBattlePlayerController.generated.h"

class UGuLiPlayerNetSyncComponent;
#if !UE_BUILD_SHIPPING
class SGuLiGMPanel;
#endif

/** 公共玩家连接：每个本地World有自己的实例；不绑定指挥、飞行或载具输入。 */
UCLASS()
class GULISTRIKE_API AGuLiBattlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGuLiBattlePlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void SetupInputComponent() override;

#if !UE_BUILD_SHIPPING
	/** Per-local-player runtime GM surface. No RPC or Blueprint entry point is exposed. */
	void ToggleGMPanel();
	void CloseGMPanel();
	bool IsGMPanelOpen() const { return bGMPanelOpen; }
#endif

	// 本地访问拥有连接上的组件；真正的跨端通信声明在组件内，此 Getter 不是 RPC。
	UFUNCTION(BlueprintPure, Category = "Battle|Network")
	UGuLiPlayerNetSyncComponent* GetPlayerNetSyncComponent() const { return PlayerNetSyncComponent; }


	// 保留历史子对象名以兼容 Commander 蓝图；派生构造函数可替换为专业网络组件的子类。
	static const FName PlayerNetSyncComponentName;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if !UE_BUILD_SHIPPING
	/** Role-aware controllers override this to reconstruct their normal cursor and input mode. */
	virtual void RestoreGameplayInputAfterGMPanel();
#endif

private:
#if !UE_BUILD_SHIPPING
	void RemoveGMPanel(bool bRestoreGameplayInput);

	TSharedPtr<SGuLiGMPanel> GMPanel;
	bool bGMPanelOpen = false;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle|Network", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGuLiPlayerNetSyncComponent> PlayerNetSyncComponent;

};
