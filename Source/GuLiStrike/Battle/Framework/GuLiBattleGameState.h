// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Battle/Combat/GuLiMissileVisualSubsystem.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"
#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "GameFramework/GameState.h"
#include "GuLiBattleGameState.generated.h"

/** 角色席位的公共复制条目；保留历史反射名称以兼容现有蓝图。 */
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

/**
 * Retained public entry for one live Wingman group. The Bundle is one complete,
 * validated six-scope cut; high-frequency Accepted batches are deliberately not
 * stored here so a pose update cannot turn into reliable Bootstrap churn.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanPublicBootstrapState
{
	GENERATED_BODY()
	UPROPERTY() bool bPhased = false;
	UPROPERTY() bool bExternalActionsLocked = false;
	UPROPERTY() TArray<FGuLiWingmanAcceptedBatch> ExternalDisplacementBaselines;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|PublicRelay")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|PublicRelay")
	FGuLiWingmanBootstrapBundle Bootstrap;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|PublicRelay")
	EGuLiWingmanGroupLifecycle Lifecycle = EGuLiWingmanGroupLifecycle::Unavailable;

	/** Monotonic per-GameState publication revision; useful for diagnostics only. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|PublicRelay")
	uint32 PublicationRevision = 0u;

	bool IsWellFormed() const
	{
		return Group.IsValid() && Bootstrap.IsWellFormed()
			&& Bootstrap.Commit.Group == Group
			&& Lifecycle != EGuLiWingmanGroupLifecycle::Unavailable
			&& Lifecycle != EGuLiWingmanGroupLifecycle::Revoked
			&& PublicationRevision != 0u;
	}
};

/**
 * 双端可见的战局元数据与 5v5 席位目录：仅服务器修改，客户端读取复制副本。
 * 与 PlayerController 上的 OwnerOnly 选择状态不同，这里向相关客户端公开战局信息。
 */
UCLASS()
class GULISTRIKE_API AGuLiBattleGameState : public AGameState
{
	GENERATED_BODY()

public:
	AGuLiBattleGameState();
	virtual ~AGuLiBattleGameState() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器本地初始化；已有有效战局标识时保留，重复调用不会每次更换战局。 */
	void InitializeServerMatchState();

	/**
	 * 服务器本地申请十个玩法席位之一；成功时填充三个 Out 参数并返回 true，失败输出无效席位/观察者。
	 * PlayerGuid 用于恢复身份；PreferredSlotIndex 优先尝试切图/重连前的席位，但不能抢占他人席位。
	 * 原席位不可用时按默认红蓝均衡顺序尝试，客户端不能用此函数直接指定身份。
	 */
	bool ClaimRoleSlot(
		const FGuid& PlayerGuid,
		uint8 PreferredSlotIndex,
		uint8& OutSlotIndex,
		EGuLiTeam& OutTeam,
		EGuLiCommanderRole& OutRole);

	/**
	 * 服务器按角色优先级分配新身份；旧 GUID/优先席位先恢复，同角色仍按既有红蓝均衡顺序。
	 * 此列表只调整尝试顺序，不允许抢占；未列出的角色最后按默认顺序补试。
	 */
	bool ClaimRoleSlotWithPriority(
		const FGuid& PlayerGuid,
		uint8 PreferredSlotIndex,
		const TArray<EGuLiCommanderRole>& InitialRolePriority,
		uint8& OutSlotIndex,
		EGuLiTeam& OutTeam,
		EGuLiCommanderRole& OutRole);

	/** 服务器本地释放席位；仅在席位仍属于 ExpectedPlayerGuid 时生效，避免旧退出回调清掉新玩家。 */
	void ReleaseRoleSlot(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid);

	/** 服务器按席位及玩家 GUID 双重核对，仅更新公共战局握手状态。 */
	void SetRoleSlotBattleReady(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid, bool bReady);

	/** 把士兵流就绪位映射到公共席位目录；旧 API 名称保持兼容。 */
	void SetRoleSlotSyncReady(uint8 SlotIndex, const FGuid& ExpectedPlayerGuid, bool bReady);

	uint16 GetProtocolVersion() const { return ProtocolVersion; }

	uint32 GetMatchEpoch() const { return MatchEpoch; }

	UFUNCTION(BlueprintPure, Category = "Commander|Match")
	FGuid GetMatchId() const { return MatchId; }

	UFUNCTION(BlueprintPure, Category = "Commander|Lobby")
	TArray<FGuLiCommanderRoleSlotState> GetRoleSlots() const { return RoleSlots; }

	/**
	 * Authority-only. Reliably publishes and retains a six-scope Bootstrap for
	 * existing clients and LateJoin. Identical cuts are suppressed.
	 */
	bool ServerPublishWingmanBootstrap(
		const FGuLiWingmanBootstrapBundle& Bootstrap,
		EGuLiWingmanGroupLifecycle Lifecycle);

	/** Authority-only reliable tombstone event plus removal from the retained live set. */
	void ServerRevokeWingmanGroup(const FGuLiWingmanGroupHandle& Group);

	/** Authority-only unreliable public pose stream. This state is never accepted back by authority. */
	void ServerPublishWingmanAcceptedBatch(const FGuLiWingmanAcceptedBatch& AcceptedBatch);
	void ServerPublishWingmanExternalControl(const FGuLiWingmanGroupHandle& Group, bool bPhased, bool bLocked,
		const TArray<FGuLiWingmanAcceptedBatch>& Baselines);
	/** Authority-only activation path for one complete all-Flight atomic result. */
	void ServerPublishWingmanAcceptedAtomicBatch(
		const TArray<FGuLiWingmanAcceptedBatch>& AcceptedFlights);

	/** Client-side entry for the per-connection 10 Hz public pose stream. */
	void ReceivePublicWingmanAcceptedBatches(
		const TArray<FGuLiWingmanAcceptedBatch>& AcceptedBatches);

	const TArray<FGuLiWingmanPublicBootstrapState>& GetPublicWingmanBootstraps() const
	{
		return PublicWingmanBootstraps;
	}

	/** Server-only durable owner of all Wingman Relay cores in this battle world. */
	FGuLiWingmanRelayAuthorityRegistry* GetWingmanRelayAuthorityRegistry();
	const FGuLiWingmanRelayAuthorityRegistry* GetWingmanRelayAuthorityRegistry() const;

	// 本进程通知；名字中的 Multicast 指多订阅者委托，不是 NetMulticast RPC。
	UPROPERTY(BlueprintAssignable, Category = "Commander|Lobby")
	FGuLiCommanderRoleSlotsChangedSignature OnRoleSlotsChanged;

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
	void OnRep_MatchEpoch();

	UFUNCTION()
	void OnRep_PublicWingmanBootstraps();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastReceiveWingmanBootstrap(const FGuLiWingmanPublicBootstrapState& PublicState);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastRevokeWingmanGroup(const FGuLiWingmanGroupHandle& Group);

	/** Reliable launch, unreliable correction, reliable terminal visual-only bridge. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReceiveMissileLaunch(const FGuLiMissileVisualLaunchDTO& Event);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastReceiveMissileCorrection(const FGuLiMissileVisualCorrectionDTO& Event);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastReceiveMissileTerminal(const FGuLiMissileVisualTerminalDTO& Event);

	void HandleLogicalMissileLaunch(const FGuLiLogicalMissileState& Missile);
	void HandleLogicalMissileCorrection(const FGuLiLogicalMissileState& Missile);
	void HandleLogicalMissileTerminal(const FGuLiLogicalMissileTerminalEvent& Event);
	void ApplyMissileVisualLaunch(const FGuLiMissileVisualLaunchDTO& Event);
	void ApplyMissileVisualCorrection(const FGuLiMissileVisualCorrectionDTO& Event);
	void ApplyMissileVisualTerminal(const FGuLiMissileVisualTerminalDTO& Event);

	void ApplyRetainedWingmanBootstraps();
	void FlushPublicWingmanAcceptedBatches();
	void HandlePublicWingmanBootstrap(const FGuLiWingmanPublicBootstrapState& PublicState);
	void HandlePublicWingmanRevocation(const FGuLiWingmanGroupHandle& Group);
	void HandlePublicWingmanAcceptedBatch(const FGuLiWingmanAcceptedBatch& AcceptedBatch);
	FGuid GetLocalWingmanViewerPlayerGuid() const;
	bool IsLocalWingmanLeaseOwner(const FGuLiWingmanBootstrapBundle& Bootstrap) const;

	// 协议兼容版本；下列字段由 GetLifetimeReplicatedProps 注册，无 OwnerOnly 限制。
	UPROPERTY(Replicated)
	uint16 ProtocolVersion = GULI_BATTLE_PROTOCOL_VERSION;

	// 当前战局的非零隔离标识；与每连接的 SyncGeneration、每帧 FrameSequence 不同。
	UPROPERTY(ReplicatedUsing = OnRep_MatchEpoch)
	uint32 MatchEpoch = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Commander|Match", meta = (AllowPrivateAccess = "true"))
	FGuid MatchId;

	UPROPERTY(ReplicatedUsing = OnRep_RoleSlots)
	TArray<FGuLiCommanderRoleSlotState> RoleSlots;

	/** Reliable retained state: written only by authority, consumed read-only by clients. */
	UPROPERTY(ReplicatedUsing = OnRep_PublicWingmanBootstraps)
	TArray<FGuLiWingmanPublicBootstrapState> PublicWingmanBootstraps;

	uint32 NextWingmanPublicationRevision = 1u;
	TMap<FGuLiWingmanGroupHandle, FGuLiWingmanBootstrapBundle> ClientWingmanBootstrapCache;
	TSet<FGuLiWingmanGroupHandle> AppliedPublicWingmanGroups;
	TArray<FGuLiWingmanAcceptedBatch> LatestPublicWingmanAcceptedBatches;
	TUniquePtr<FGuLiWingmanRelayAuthorityRegistry> WingmanRelayAuthorityRegistry;
	FTimerHandle WingmanPublicPosePublishTimer;
	TWeakObjectPtr<UGuLiLogicalMissileSubsystem> BoundLogicalMissiles;
};
