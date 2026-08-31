// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Components/ActorComponent.h"
#include "GuLiPlayerNetSyncComponent.generated.h"

class AGuLiBattleGameState;
class AGuLiBattlePlayerState;
class APlayerController;

/** 连接的公共握手标记，仅复制给拥有者；不包含士兵名册、载具或武器协议。 */
USTRUCT()
struct FGuLiConnectionBootstrapState
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 ProtocolVersion = GULI_BATTLE_PROTOCOL_VERSION;

	UPROPERTY()
	uint32 Generation = 0u;

	UPROPERTY()
	uint32 MatchEpoch = 0u;

	UPROPERTY()
	FGuid PlayerGuid;

	UPROPERTY()
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY()
	EGuLiCommanderRole Role = EGuLiCommanderRole::Unassigned;

	UPROPERTY()
	uint8 SlotIndex = MAX_uint8;

	// 与身份和代次放在同一标记内，避免沿用 PlayerState 上一代的就绪位。
	UPROPERTY()
	bool bServerAcknowledged = false;
};

/** 可复用的本地滑动窗口；调用者持有自己的预算，载具输入不能挤占指挥官命令额度。 */
struct FGuLiNetworkRequestWindow
{
	bool Consume(double NowSeconds, int32 MaximumRequests, double WindowSeconds);
	void Reset() { RequestTimes.Reset(); }

private:
	TArray<double> RequestTimes;
};

/** 公共握手的纯数据判定；服务端仍须将其与当前 GameState/PlayerState 比较。 */
namespace GuLiConnectionBootstrap
{
	bool IsValidIdentity(const FGuLiConnectionBootstrapState& State);
	bool HasSameIdentity(
		const FGuLiConnectionBootstrapState& Lhs,
		const FGuLiConnectionBootstrapState& Rhs);
}

/**
 * 所有玩法共用的玩家连接组件。依附 PlayerController 的 Owning Connection，只有一套公共握手。
 * 服务器决定战局与身份，客户端确认相关属性已到达；公共就绪不代表士兵流、载具或武器已就绪。
 * 指挥官组件通过继承增加自己的名册与命令协议，未来其他玩法不必依赖 Mass。
 */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class UGuLiPlayerNetSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiPlayerNetSyncComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** 服务器本地入口：身份/战局改变时启动新代次；等待中的同代次保持稳定，可每 Tick 调用。 */
	void EnsureServerConnectionBootstrap();

	/** 本端公共握手是否已由服务器确认，并且仍匹配当前战局和身份；不能作为姿态接收权限。 */
	UFUNCTION(BlueprintPure, Category = "Battle|Network")
	bool IsConnectionReady() const;

	/** 专业模块用此代次绑定自己的握手；数值非零本身不代表已就绪。 */
	uint32 GetConnectionGeneration() const { return ConnectionBootstrap.Generation; }

protected:
	APlayerController* GetOwningPlayerController() const;
	AGuLiBattlePlayerState* GetBattlePlayerState() const;
	AGuLiBattleGameState* GetBattleGameState() const;

	/** 两端本地生命周期钩子；派生类清除专业同步、旧命令与缓存，不在这里创建另一套公共握手。 */
	virtual void OnConnectionBootstrapReset();
	virtual void OnConnectionBootstrapReady();

private:
	// 拥有客户端报告已观察到的完整标记；服务器不接受旧代次或与当前分配不符的身份。
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeConnectionBootstrap(const FGuLiConnectionBootstrapState& AppliedState);

	UFUNCTION()
	void OnRep_ConnectionBootstrap();

	bool BuildCurrentConnectionIdentity(FGuLiConnectionBootstrapState& OutState) const;
	void ResetServerConnectionBootstrap();
	void UpdateClientConnectionBootstrap();
	void MirrorBattleReadyToRoleSlot(bool bReady) const;

	UPROPERTY(ReplicatedUsing = OnRep_ConnectionBootstrap)
	FGuLiConnectionBootstrapState ConnectionBootstrap;

	// 以下均为本端运行时状态，不写入存档，也不另行复制。
	TWeakObjectPtr<AGuLiBattlePlayerState> BootstrapPlayerState;
	uint32 NextConnectionGeneration = 0u;
	uint32 ClientObservedGeneration = 0u;
	bool bServerConnectionReady = false;
	bool bClientConnectionReady = false;
	double NextClientAcknowledgementTime = 0.0;
	FGuLiNetworkRequestWindow ConnectionAcknowledgementWindow;
};
