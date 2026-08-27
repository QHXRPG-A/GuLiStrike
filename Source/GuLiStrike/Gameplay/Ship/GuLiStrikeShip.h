// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GuLiStrikeShip.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UDataTable;
class UGuLiStrikeShipPartComponent;
class UGuLiStrikeEnginePart;
class UGuLiStrikeWeaponPart;
struct FInputActionValue;

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
class AGuLiStrikeShip : public ACharacter
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

	/** 加力键当前是否按住 */
	bool bBoosting = false;

	/** 本帧推进意图的世界空间累积（Tick 里消费，用于自动转向判定） */
	FVector PendingThrustIntent = FVector::ZeroVector;

	/** 当前转向输入累积：-1 左 / +1 右（Tick 里消费，用于压弯倾斜） */
	float PendingTurnInput = 0.0f;

	/** 本帧侧移输入累积：-1 左 / +1 右（Tick 里消费，用于侧移压弯倾斜） */
	float PendingStrafeInput = 0.0f;

	/** 当前偏航角速度（度/秒，带正负号）——转向惯性来源 */
	float YawVelocity = 0.0f;

	/** 玩家滚轮设定的期望臂长（厘米）；实际臂长每帧由自身舰避障扫掠结算 */
	float DesiredArmLength = 0.0f;

	/** 舰体包围球半径（厘米，BeginPlay 从舰体资产缓存）；相机避障第一段短走廊的边界 */
	float HullBoundingRadius = 0.0f;

	/** 当前压弯倾斜角（平滑过渡用） */
	float CurrentBankRoll = 0.0f;

	/** 全部已装引擎部件的推力总和 */
	float TotalThrust = 0.0f;

	/** 舰体质量加上所有已装部件质量 */
	float TotalMass = 0.0f;

	/** 最近一次数值重算后的当前极速 */
	float CurrentMaxSpeed = 0.0f;

public:

	/** 构造函数 */
	AGuLiStrikeShip();

	/** 每帧更新：依据推进意图自动转向（倒退/纯侧移除外） */
	virtual void Tick(float DeltaTime) override;

protected:

	/** 玩法初始化 */
	virtual void BeginPlay() override;

	/** 被控制器操控时的初始化 */
	virtual void NotifyControllerChanged() override;

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

public:

	/** 在舰体 socket 上安装指定类的部件，替换该槽位上已有的部件 */
	UFUNCTION(BlueprintCallable, Category="Ship")
	bool InstallPart(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass, FName SocketName);

	/** 拆除指定 socket 上的部件（如有） */
	UFUNCTION(BlueprintCallable, Category="Ship")
	bool UninstallPart(FName SocketName);

	/** 返回指定 socket 上当前安装的部件（没有则返回空） */
	UFUNCTION(BlueprintCallable, Category="Ship")
	UGuLiStrikeShipPartComponent* GetPartAt(FName SocketName) const;

	/** 循环切换指定基类（如引擎）的全部已装部件到目录中的下一个 */
	UFUNCTION(BlueprintCallable, Category="Ship")
	void CycleParts(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass);

	/** 让所有已脱离冷却的武器部件开火 */
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

private:

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
