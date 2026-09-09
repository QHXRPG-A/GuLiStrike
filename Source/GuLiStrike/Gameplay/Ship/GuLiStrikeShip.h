// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#include "Battle/Combat/GuLiWingmanReplenishmentController.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "GameFramework/Character.h"
#include "GuLiStrikeShip.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UDataTable;
class UAbilitySystemComponent;
class UGuLiCombatHealthComponent;
class UGuLiShipAimComponent;
class UGuLiShipAbilitySet;
class UGuLiShipAbilitySystemComponent;
class UGuLiWingmanWeaponDefinition;
class UGuLiShipMovementComponent;
class UGuLiShipWorldHUDComponent;
class UGuLiShipTargetingRangeComponent;
class UGuLiWingmanRelayComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UGuLiStrikeShipPartComponent;
class UGuLiStrikeEnginePart;
class UGuLiStrikeWeaponPart;
struct FInputActionValue;
struct FGameplayAbilitySpecHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FGuLiWingmanMissileLockPredictedSignature,
	FGuid, RequestId,
	bool, bPredictedLocked,
	FGuLiTargetHandle, PredictedTarget,
	FVector, AimOrigin,
	FVector, AimForward);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FGuLiWingmanMissileSalvoResolvedSignature,
	FGuid, RequestId,
	EGuLiWingmanRejectReason, RejectReason,
	FGuLiTargetHandle, ServerSelectedTarget,
	uint8, FlightIndex,
	int32, LaunchedCount);

/** 出生时自带的部件及其安装的舰体 socket */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipDefaultPart
{
	GENERATED_BODY()

	/** 该部件要安装到的舰体 socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship")
	FName SocketName;

	/** 要安装的部件组件类 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship")
	TSubclassOf<UGuLiStrikeShipPartComponent> PartClass;
};

/** 数值修饰：临时改变飞行参数的倍率（超载/减速等），统一在 RecomputeStats 里结算 */
USTRUCT(BlueprintType)
struct FGuLiStrikeStatModifier
{
	GENERATED_BODY()

	/** 修饰名（添加时同名覆盖，移除按名移除） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ship")
	FName Name;

	/** 极速倍率（1 = 不变） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ship", meta=(ClampMin = 0))
	float MaxSpeedMultiplier = 1.0f;

	/** 加速度倍率（1 = 不变） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ship", meta=(ClampMin = 0))
	float AccelerationMultiplier = 1.0f;
};

/**
 * 服务器编写的 GM.Runtime 状态：把极速/加速度倍率、启用位与版本放在同一个复制结构中消费。
 * 客户端 OnRep 按整份当前结构重建保留修饰层；这不代表跨 Actor 的状态也会原子到达。
 */
USTRUCT()
struct FGuLiShipGMRuntimeReplicatedState
{
	GENERATED_BODY()

	UPROPERTY()
	float MaxSpeedMultiplier = 1.0f;

	UPROPERTY()
	float AccelerationMultiplier = 1.0f;

	UPROPERTY()
	uint32 Revision = 0u;

	UPROPERTY()
	bool bActive = false;
};

/** 服务器发布的完整装配名册；空数组也是有效裸舰配置，Revision=0 才表示尚未发布。 */
USTRUCT()
struct FGuLiShipLoadoutState
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 Revision = 0u;

	UPROPERTY()
	TArray<FGuLiStrikeShipDefaultPart> Parts;
};

/** 已安装部件的内部记录 */
struct FGuLiStrikeInstalledPart
{
	FName SocketName;
	TWeakObjectPtr<UGuLiStrikeShipPartComponent> Part;
};

/**
 *  玩家操控的模块化 DIY 飞船。
 *  舰体是单个静态网格体（网格上的 socket 即挂点）；
 *  部件是运行时挂到这些 socket 上的 UStaticMeshComponent 子类，
 *  支持飞行中热切换。部件行为（数值贡献、开火等）由部件类自己实现
 *  （多态分发），飞船只负责装配与聚合——新增部件类型无需改飞船代码。
 */
UCLASS(abstract)
class AGuLiStrikeShip : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

	/** 飞船舰体静态网格体；其上的 socket 是部件挂点 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* HullMesh;

	/** 跟随在飞船后上方的相机臂 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* SpringArm;

	/** 玩家相机 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* Camera;

	/** Pawn-owned ASC. OwnerActor and AvatarActor are always this Ship. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UGuLiShipAbilitySystemComponent* ShipAbilitySystem;

	/** Custom authoritative Ship health; numeric health intentionally stays outside GAS. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UGuLiCombatHealthComponent* CombatHealth;

	/** Local-only GAS reticle ownership, virtual cursor and aim-camera bridge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UGuLiShipAimComponent* ShipAim;

	/** Local-only world-space HUD presenter; creates no visible nodes for remote Ships or servers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UGuLiShipWorldHUDComponent* ShipWorldHUD;

	/** Local-only wire sphere showing the automatic wingman acquisition radius. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UGuLiShipTargetingRangeComponent* WingmanTargetingRange;

protected:

	/** 本飞船被操控期间添加的输入映射上下文 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputMappingContext* ShipMappingContext;

	/** 前进推进输入（W） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* ForwardAction;

	/** 后退推进输入（S） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* BackwardAction;

	/** 右移平移输入（D） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* StrafeRightAction;

	/** 左移平移输入（A） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* StrafeLeftAction;

	/** 上升输入（Space） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* AscendAction;

	/** 下降输入（C） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* DescendAction;

	/** 左转输入（Q，偏航） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* TurnLeftAction;

	/** 右转输入（E，偏航） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* TurnRightAction;

	/** 鼠标视角输入（2D 增量） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** 滚轮缩放输入（拉近/拉远相机） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* ZoomAction;

	/** 开火输入 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* FireAction;

	/** 僚机主动导弹齐射输入；缺省为空时不影响现有飞船武器输入。 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* WingmanMissileAction;

	/** 加力输入 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* BoostAction;

	/** 循环更换引擎部件的输入 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* CycleEnginesAction;

	/** 循环更换武器部件的输入 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* CycleWeaponsAction;

	/** 出生时装上的部件 */
	UPROPERTY(EditDefaultsOnly, Category="Ship")
	TArray<FGuLiStrikeShipDefaultPart> DefaultParts;

	/** 全部部件类的热切换目录 */
	UPROPERTY(EditDefaultsOnly, Category="Ship")
	TArray<TSubclassOf<UGuLiStrikeShipPartComponent>> PartCatalogue;

	/** 部件数值表（RowName = 部件的 PartId）；空表 = 全部沿用部件蓝图默认数值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship|Data")
	UDataTable* PartDataTable;

	/** 飞船调参表（RowName = TuningPreset）；空表 = 沿用本类默认数值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship|Data")
	UDataTable* TuningDataTable;

	/** 相机参数表（RowName = TuningPreset，与调参表共用预设名）；空表 = 沿用本类默认数值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship|Data")
	UDataTable* CameraDataTable;

	/** 出生时应用的调参预设行名 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship|Data")
	FName TuningPreset = FName(TEXT("Default"));

	/** 生效中的数值修饰列表（超载/减速等）；仅 RecomputeStats 依据它写移动参数 */
	UPROPERTY(BlueprintReadWrite, Category="Ship|Stats")
	TArray<FGuLiStrikeStatModifier> StatModifiers;

	/** Server-owned atomic state for the reserved GM.Runtime modifier layer. */
	// 由 GetLifetimeReplicatedProps 注册，无 OwnerOnly 条件；相关客户端通过 OnRep 重建本地层。
	UPROPERTY(ReplicatedUsing=OnRep_GMRuntimeState)
	FGuLiShipGMRuntimeReplicatedState GMRuntimeState;

	/** 所有相关客户端重建部件；组件对象不直接跨端共享。 */
	UPROPERTY(ReplicatedUsing=OnRep_LoadoutState)
	FGuLiShipLoadoutState LoadoutState;

	/** Optional authored catalog; a deterministic native v1 set is used when this is unset. */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Abilities")
	TObjectPtr<UGuLiShipAbilitySet> ShipAbilitySet;

	/** Reliable ASC-independent projection consumed by Lease owners and backups. */
	UPROPERTY(ReplicatedUsing=OnRep_GroupAbilityConfig)
	FGuLiGroupAbilityConfigSnapshot GroupAbilityConfig;

	/** 裸舰体质量（不含任何部件） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float HullMass = 100.0f;

	/** 标称推重比对应的极速 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0))
	float BaseMaxSpeed = 1200.0f;

	/** 标称推重比对应的加速度 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0))
	float BaseAcceleration = 400.0f;

	/** 映射到基础飞行性能的推重比 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0.01))
	float NominalThrustRatio = 6.0f;

	/** 推重比速度倍率的下限 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0.1))
	float SpeedMultiplierMin = 0.5f;

	/** 推重比速度倍率的上限 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float SpeedMultiplierMax = 2.0f;

	/** 按住加力键时的额外推力倍率 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float BoostThrustMultiplier = 2.0f;

	/** 鼠标 Y 增量每单位对应的相机俯仰角度 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling")
	float MousePitchScale = 1.0f;

	/** 鼠标 X 增量每单位对应的相机偏航角度 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling")
	float MouseYawScale = 1.0f;

	/** 偏航最大角速度（度/秒，Q/E） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float YawRate = 40.0f;

	/** 偏航响应速度：按键后角速度爬升到目标的快慢（越大起步越快） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0.1))
	float YawResponseSpeed = 3.0f;

	/** 偏航惯性衰减速度：松键后角速度归零的快慢（越小惯性越足、滑得越远） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0.05))
	float YawStopDamping = 1.0f;

	/** 转向时机身向转弯侧的倾斜角（压弯效果） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0, ClampMax = 45))
	float MaxBankAngle = 5.0f;

	/** 倾斜回正的插值速度 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0.1))
	float BankInterpSpeed = 4.0f;

	/** 自动转向的插值速度（越小转向越沉稳） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0.1))
	float OrientTurnSpeed = 2.5f;

	/** 推进时是否自动转向推进方向（默认关闭：平移玩法，朝向只由 Q/E 偏航控制） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling")
	bool bOrientToMovement = false;

	/** 推进意图与船头夹角余弦低于此值时不自动转向（倒退/纯侧移保持船头，S 作为反推减速） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = -1, ClampMax = 1))
	float OrientMinForwardDot = 0.3f;

	/** 相机臂俯仰下限（度） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling")
	float CameraPitchMin = -80.0f;

	/** 相机臂俯仰上限（度） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling")
	float CameraPitchMax = 80.0f;

	/** 默认相机臂长（厘米）：出生时的期望臂长与滚轮缩放起点 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float CameraDefaultArmLength = 3000.0f;

	/** 滚轮每格伸缩的相机臂长度（厘米） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float CameraZoomStep = 4000.0f;

	/** 滚轮期望臂长下限（厘米）；实际臂长由 Tick 避障每帧结算，此值只约束期望值 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float CameraZoomMin = 50000.0f;

	/** 相机臂长度上限（厘米） */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float CameraZoomMax = 160000.0f;

	/** 相机避障扫掠的球半径（厘米）；同时是贴面时镜头与舰面的最小间隙 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 10))
	float CameraCollisionProbeRadius = 250.0f;

	/** 避障拉回时的臂长下限（厘米），防止贴死轨道球心 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float CameraCollisionMinArm = 500.0f;

	/** 舰体网格体偏移，让飞船几何中心对齐 Actor 原点 */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Components")
	FVector HullMeshOffset = FVector(0.0f, 0.0f, 700.0f);

	/** 当前已安装的部件（每个 socket 一个） */
	TArray<FGuLiStrikeInstalledPart> InstalledParts;

	/** 玩家滚轮设定的期望臂长（厘米）；实际臂长每帧由自身舰避障扫掠结算 */
	float DesiredArmLength = 0.0f;

	/** 舰体包围球半径（厘米，BeginPlay 从舰体资产缓存）；相机避障第一段短走廊的边界 */
	float HullBoundingRadius = 0.0f;

	/** 全部已装引擎部件的推力总和 */
	float TotalThrust = 0.0f;

	/** 舰体质量加上所有已装部件质量 */
	float TotalMass = 0.0f;

	/** 最近一次数值重算后的当前极速 */
	float CurrentMaxSpeed = 0.0f;

	/** Prevents replicated/World-subsystem GM state from firing stat events before BeginPlay setup. */
	bool bRuntimeStatsInitialized = false;

public:

	/** 构造函数 */
	AGuLiStrikeShip(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFUNCTION(BlueprintPure, Category="Ship|Abilities")
	UGuLiShipAbilitySystemComponent* GetShipAbilitySystemComponent() const { return ShipAbilitySystem; }

	UFUNCTION(BlueprintPure, Category="Ship|Combat")
	UGuLiCombatHealthComponent* GetCombatHealthComponent() const { return CombatHealth; }

	/** The authored hull mesh whose bounds and sockets define the visible Ship body. */
	UFUNCTION(BlueprintPure, Category="Ship|Components")
	UStaticMeshComponent* GetHullMeshComponent() const { return HullMesh; }

	UFUNCTION(BlueprintPure, Category="Ship|Aiming")
	UGuLiShipAimComponent* GetShipAimComponent() const { return ShipAim; }

	UFUNCTION(BlueprintPure, Category="Ship|UI")
	UGuLiShipWorldHUDComponent* GetShipWorldHUDComponent() const { return ShipWorldHUD; }

	const FGuLiGroupAbilityConfigSnapshot& GetGroupAbilityConfig() const { return GroupAbilityConfig; }
	/** Read-only authority diagnostics; never advances or releases a replenishment timer. */
	bool TryGetWingmanReplenishmentSchedule(
		const FGuLiWingmanHandle& Wingman,
		uint64& OutScheduleId,
		double& OutReplenishAtSeconds,
		bool& bOutDue) const;

	/** Cosmetic, local prediction only. The target is never sent back to authority. */
	UPROPERTY(BlueprintAssignable, Category="Ship|Wingman|Missile")
	FGuLiWingmanMissileLockPredictedSignature OnWingmanMissileLockPredicted;

	/** Reliable authoritative reconciliation for one stable request identity. */
	UPROPERTY(BlueprintAssignable, Category="Ship|Wingman|Missile")
	FGuLiWingmanMissileSalvoResolvedSignature OnWingmanMissileSalvoResolved;

	/** 本地相机与输入上下文维护；服务器持续开火。飞行积分仅由移动组件执行。 */
	virtual void Tick(float DeltaTime) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Ship|Wingman|Target")
	void SetWingmanAttackTarget(const FGuLiTargetHandle& Target);
	UFUNCTION(BlueprintCallable, Category="Ship|Wingman|Target")
	void ClearWingmanAttackTarget();
	UFUNCTION(BlueprintPure, Category="Ship|Wingman|Target")
	FGuLiWingmanAttackTarget GetWingmanAttackTarget() const { return WingmanAttackTarget; }
	UPROPERTY(EditDefaultsOnly, Category="Ship|Wingman|Target")
	FDataTableRowHandle WingmanTargetingRow;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ship|Wingman|Target")
	bool bShowWingmanTargetingRange = true;
	UPROPERTY(Replicated)
	FGuLiWingmanAttackTarget WingmanAttackTarget;
	UFUNCTION(Server, Reliable)
	void ServerSetWingmanAttackTarget(const FGuLiTargetHandle& Target);

protected:

	/** 玩法初始化 */
	virtual void BeginPlay() override;

	/** 被控制器操控时的初始化 */
	virtual void NotifyControllerChanged() override;
	virtual void UnPossessed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 添加输入绑定 */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** 处理前进推进输入 */
	void ThrustForward(const FInputActionValue& Value);

	/** 处理后退推进输入 */
	void ThrustBackward(const FInputActionValue& Value);

	/** 处理右移平移输入 */
	void ThrustRight(const FInputActionValue& Value);

	/** 处理左移平移输入 */
	void ThrustLeft(const FInputActionValue& Value);

	/** 处理上升输入 */
	void ThrustUp(const FInputActionValue& Value);

	/** 处理下降输入 */
	void ThrustDown(const FInputActionValue& Value);

	/** 处理左转输入（Q，偏航 + 左倾压弯） */
	void TurnLeft(const FInputActionValue& Value);

	/** 处理右转输入（E，偏航 + 右倾压弯） */
	void TurnRight(const FInputActionValue& Value);

	/** 处理鼠标视角输入（只环绕相机，不改变飞船朝向） */
	void Look(const FInputActionValue& Value);

	/** 处理滚轮缩放输入（伸缩相机臂，钳制在限位内） */
	void ZoomCamera(const FInputActionValue& Value);

	/** 处理加力按下 */
	void BoostStart(const FInputActionValue& Value);

	/** 处理加力松开 */
	void BoostEnd(const FInputActionValue& Value);

	/** 处理开火输入 */
	void Fire(const FInputActionValue& Value);
	void StopFire(const FInputActionValue& Value);
	void WingmanMissilePressed(const FInputActionValue& Value);
	void WingmanMissileReleased(const FInputActionValue& Value);

	/** 处理引擎热切换输入 */
	void CycleEngines(const FInputActionValue& Value);

	/** 处理武器热切换输入 */
	void CycleWeapons(const FInputActionValue& Value);

	/** 供蓝图响应部件安装 */
	UFUNCTION(BlueprintImplementableEvent, Category="Ship", meta=(DisplayName = "Part Installed"))
	void BP_OnPartInstalled(UGuLiStrikeShipPartComponent* Part, FName SocketName);

	/** 供蓝图响应部件拆除 */
	UFUNCTION(BlueprintImplementableEvent, Category="Ship", meta=(DisplayName = "Part Uninstalled"))
	void BP_OnPartUninstalled(UGuLiStrikeShipPartComponent* Part, FName SocketName);

	/** 供蓝图响应飞行数值变化 */
	UFUNCTION(BlueprintImplementableEvent, Category="Ship", meta=(DisplayName = "Stats Changed"))
	void BP_OnStatsChanged();

	// 客户端复制回调；可能早于 BeginPlay，先重建修饰层，初始化完成后才重算并触发蓝图可见反馈。
	UFUNCTION()
	void OnRep_GMRuntimeState();

public:

	/** 服务器校验后同步安装并返回成功；拥有客户端只发送目录意图并返回 false，最终状态由 OnRep 确认。 */
	UFUNCTION(BlueprintCallable, Category="Ship")
	bool InstallPart(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass, FName SocketName);

	/** 服务器拆除指定槽位；拥有客户端仅发异步意图，不能在本地改变权威装配。 */
	UFUNCTION(BlueprintCallable, Category="Ship")
	bool UninstallPart(FName SocketName);

	/** 返回指定 socket 上当前安装的部件（没有则返回空） */
	UFUNCTION(BlueprintCallable, Category="Ship")
	UGuLiStrikeShipPartComponent* GetPartAt(FName SocketName) const;

	/** 循环切换指定基类（如引擎）的全部已装部件到目录中的下一个 */
	UFUNCTION(BlueprintCallable, Category="Ship")
	void CycleParts(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass);

	/** 服务器本地执行一次武器冷却检查；客户端调用不会开火，不是 RPC。 */
	UFUNCTION(BlueprintCallable, Category="Ship")
	void FireInstalledWeapons();

	/** 重算飞行数值并应用到移动组件 */
	UFUNCTION(BlueprintCallable, Category="Ship")
	void RecomputeStats();

	/** 添加/同名覆盖一个数值修饰并立即应用（如超载 +20% 极速） */
	UFUNCTION(BlueprintCallable, Category="Ship|Stats")
	void AddStatModifier(FName Name, float MaxSpeedMultiplier, float AccelerationMultiplier);

	/** 按名移除一个数值修饰并立即应用 */
	UFUNCTION(BlueprintCallable, Category="Ship|Stats")
	void RemoveStatModifier(FName Name);

	/** 设置保留的 GM.Runtime 修饰层；不会覆盖其它 Gameplay Modifier。 */
	// 服务器本地 setter，倍率必须有限且在 0..100；无权限/非法输入返回 false，不是客户端可发的 RPC。
	bool SetGMRuntimeMultipliers(float MaxSpeedMultiplier, float AccelerationMultiplier);

	/** 仅移除保留的 GM.Runtime 修饰层。 */
	// 服务器撤销保留层并复制默认倍率/停用位；不会删除其他数值修饰。
	void ClearGMRuntimeMultipliers();

	/** 查询 GM.Runtime 修饰层，供非 Shipping GM 注册表和自动化验证使用。 */
	// 读取本端副本；活动时写出倍率并返回 true，停用时输出 1/1 并返回 false。
	bool GetGMRuntimeMultipliers(float& OutMaxSpeedMultiplier, float& OutAccelerationMultiplier) const;

	uint32 GetGMRuntimeTuningRevision() const { return GMRuntimeState.Revision; }

	/** 部件被外部直接销毁时由部件回调：同步注册表并重算数值 */
	void NotifyPartDestroyed(UGuLiStrikeShipPartComponent* Part);

	/** 全部已装引擎部件的推力总和 */
	UFUNCTION(BlueprintPure, Category="Ship|Stats")
	float GetTotalThrust() const { return TotalThrust; }

	/** 舰体质量加全部已装部件质量 */
	UFUNCTION(BlueprintPure, Category="Ship|Stats")
	float GetTotalMass() const { return TotalMass; }

	/** 由推重比推导的当前极速 */
	UFUNCTION(BlueprintPure, Category="Ship|Stats")
	float GetCurrentMaxSpeed() const { return CurrentMaxSpeed; }

public:
	/** 本端只读；权限来自当前拥有者、Air 身份、公共握手和装配/移动配置屏障，与士兵名册无关。 */
	UFUNCTION(BlueprintPure, Category="Ship|Network")
	bool IsShipReady() const;

	uint32 GetLoadoutRevision() const { return LoadoutState.Revision; }

	UFUNCTION(BlueprintPure, Category="Ship|Network")
	UGuLiShipMovementComponent* GetShipMovement() const;

	/** 武器部件执行前的服务器资格检查；不能在客户端当成授权。 */
	bool CanExecuteServerWeapon(const UGuLiStrikeShipPartComponent* Part) const;
	/** 服务器以胶囊权威变换和原始网格基准组合炮口父变换，排除 Listen Server 的视觉平滑偏移。 */
	bool GetServerPartTransform(const UGuLiStrikeShipPartComponent* Part, FTransform& OutTransform) const;

private:
	UFUNCTION()
	void OnRep_LoadoutState();

	UFUNCTION()
	void OnRep_GroupAbilityConfig();

	UFUNCTION()
	void HandleShipDeath();

	// 拥有客户端只提交目录索引/槽位/所见装配版本；-1 表示卸下，不接受客户端任意类。
	UFUNCTION(Server, Reliable)
	void ServerRequestPartChange(FName SocketName, int32 CatalogueIndex, uint32 ExpectedRevision, uint32 ExpectedBarrier);

	// 批量循环只允许引擎/武器两类，候选及最终装配均由服务器当前目录决定。
	UFUNCTION(Server, Reliable)
	void ServerRequestCycleParts(bool bEngines, uint32 ExpectedRevision, uint32 ExpectedBarrier);

	// 只在按下/松开时调用；连续出弹在服务器 Tick 内依照每个武器原有冷却执行。
	UFUNCTION(Server, Reliable)
	void ServerSetFiring(bool bRequested, uint32 ExpectedConfigRevision, uint32 ExpectedBarrier);

	/** Explicit request is the sole firing path; the server-side predicted GA callback never fires. */
	UFUNCTION(Server, Reliable)
	void ServerRequestWingmanMissileSalvo(
		FGuid RequestId,
		FIntVector AimDirectionMilli,
		FGuLiWeaponBindingKey WeaponBinding,
		FName SkillId,
		uint32 AbilitySetRevision,
		uint32 LoadoutRevision,
		uint32 ProfileRevision,
		uint32 MissileDefinitionRevision);

	UFUNCTION(Client, Reliable)
	void ClientResolveWingmanMissileSalvo(
		FGuid RequestId,
		EGuLiWingmanRejectReason RejectReason,
		FGuLiTargetHandle ServerSelectedTarget,
		uint8 FlightIndex,
		int32 LaunchedCount);

	bool CanUseShipControls() const;
	bool CanAcceptServerIntent() const;
	bool IsKnownPartClass(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass) const;
	bool InstallPartLocally(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass, FName SocketName);
	bool UninstallPartLocally(FName SocketName);
	void PublishLoadout();
	void ApplyReplicatedLoadout();
	void UpdateShipInputContext();
	void RemoveShipInputContext();
	void SetFiringIntent(bool bRequested);
	void InitializeShipAbilitySystem();
	void PublishGroupAbilityConfig();
	void RefreshLocalOwnedWingmanGroup();
	void RefreshServerCombatRegistration();
	void RefreshWingmanRelayBinding();
	void InstallWingmanWorldValidator(
		UGuLiWingmanRelayComponent& Relay,
		const FGuLiWingmanGroupHandle& Group);
	void MaintainWingmanCombatLifecycle();
	void RevokeWingmanGroupAuthority();
	bool EnsureWingmanCombatCoordinator();
	void RegisterWingmanCombatTargets();
	void UnregisterWingmanCombatTargets();
	static FGuLiTargetHandle MakeWingmanTargetHandle(const FGuLiWingmanHandle& Wingman);
	bool BuildLocalWingmanMissileAim(FVector& OutAimOrigin, FVector& OutAimForward) const;
	bool SelectLocalPredictedWingmanTarget(
		const FVector& AimOrigin,
		const FVector& AimForward,
		const FGuLiWeaponBindingKey& WeaponBinding,
		FGuLiTargetHandle& OutTarget) const;
	void ExecuteServerWingmanMissileSalvo(
		const FGuid& RequestId,
		const FIntVector& AimDirectionMilli,
		const FGuLiWeaponBindingKey& WeaponBinding,
		FName SkillId,
		uint32 AbilitySetRevision,
		uint32 LoadoutRevision,
		uint32 ProfileRevision,
		uint32 MissileDefinitionRevision);
	void BroadcastWingmanMissileResult(
		const FGuid& RequestId,
		const FGuLiWingmanMissileSalvoResult& Result);
	void RememberWingmanMissileRequestResult(
		const FGuid& RequestId,
		const FGuLiWingmanMissileSalvoResult& Result);
	void HandleServerWingmanFireIntentAccepted(const FGuLiWingmanFireIntent& Intent);
	void HandleGroupAbilityProjectionChanged();
	void HandleTriggeredShipWeaponAbility(
		FGameplayAbilitySpecHandle LocalSpecHandle,
		FGuLiWeaponBindingKey WeaponBinding,
		FName SkillId,
		FGameplayTag CatalogAbilityId,
		uint32 AbilitySetRevision,
		bool bLocallyPredicted);

	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InstalledInputSubsystem;
	uint32 AppliedLoadoutRevision = 0u;
	bool bEditingLoadout = false;
	bool bLoadoutDirty = false;
	bool bLocalFireHeld = false;
	bool bServerFiring = false;
	bool bEndingShipPlay = false;
	bool bShipAbilityDelegatesBound = false;
	bool bShipDeathHandled = false;
	FGuid ShipInstanceId;
	uint32 ShipGeneration = 0u;
	uint32 GroupGeneration = 0u;
	FGuLiWeaponBindingKey ActiveMissileInputBinding;
	UPROPERTY(Transient)
	TObjectPtr<UGuLiShipAbilitySet> RuntimeShipAbilitySet;
	TWeakObjectPtr<UGuLiWingmanRelayComponent> BoundWingmanRelay;
	FGuLiWingmanGroupHandle BoundWorldValidatorGroup;
	TUniquePtr<FGuLiWingmanCombatCoordinator> WingmanCombatCoordinator;
	FGuLiWingmanReplenishmentController WingmanReplenishmentController;
	FGuLiWingmanRelayServer* BoundCombatRelayCore = nullptr;
	uint32 BoundCombatAbilitySnapshotRevision = 0u;
	TArray<FGuLiTargetHandle> RegisteredWingmanCombatTargets;
	TMap<FGuid, FGuLiWingmanMissileSalvoResult> WingmanMissileRequestResults;
	TArray<FGuid> WingmanMissileRequestOrder;
	bool bWingmanActiveRosterCutPublishPending = false;
	double LastServerLoadoutIntentTime = -1.0;
	double NextLoadoutRetryTime = 0.0;

	/** Rebuilds exactly one reserved modifier entry from the replicated state. */
	void ApplyGMRuntimeStateLocally(bool bRecompute = true);

	/** 在与该 socket 兼容的目录条目内循环切换此槽位的部件 */
	bool CyclePartAtSocket(UClass* PartClass, FName SocketName);

	/** 从 PartDataTable 按 PartId 列匹配行（行名 = 表内 name 列）并覆盖部件实例数值；无表/无行时保持蓝图默认值 */
	bool ApplyPartRow(UGuLiStrikeShipPartComponent* Part) const;

	/** 从 TuningDataTable 按 TuningPreset（= 表内 name 列）查行并覆盖本飞船飞行数值 */
	bool ApplyTuningRow();

	/** 从 CameraDataTable 按 TuningPreset 查行并覆盖相机/避障数值（含默认臂长） */
	bool ApplyCameraRow();

	/** 相机避障：端点门控 + 两段式由外向内扫掠，把实际臂长结算进 SpringArm（唯一写者，Tick 调用） */
	void ResolveCameraArmCollision();
};
