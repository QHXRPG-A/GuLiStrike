// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeShip.h"
#include "GuLiStrikeShipPartComponent.h"
#include "GuLiStrikeShipTableRows.h"
#include "GuLiStrikeEnginePart.h"
#include "GuLiStrikeWeaponPart.h"
#include "GuLiStrike.h"
#include "GuLiStrikeProjectile.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "LandscapeProxy.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"

namespace GuLiStrikeShipPrivate
{
	const FName GMRuntimeModifierName(TEXT("GM.Runtime"));
}

AGuLiStrikeShip::AGuLiStrikeShip()
{
	bReplicates = true;

	// 飞船自己掌控完整的三维姿态
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 创建舰体网格体；其上的 socket 是部件挂点
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull Mesh"));
	HullMesh->SetupAttachment(GetRootComponent());
	// 舰体只参与查询不产生阻挡：相机避障扫掠需要能命中自身舰体
	// （对象类型查询不看响应矩阵，Ignore 所有通道也不妨碍被扫到）
	HullMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HullMesh->SetCollisionObjectType(ECC_WorldStatic);
	HullMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	HullMesh->SetRelativeLocation(HullMeshOffset);

	// 创建弹簧臂（追尾相机位于舰体后上方）
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArm->SetupAttachment(GetRootComponent());

	SpringArm->TargetArmLength = 3000.0f;
	SpringArm->SetRelativeRotation(FRotator(-18.0f, 0.0f, 0.0f));
	SpringArm->bDoCollisionTest = false;
	// 位置滞后关闭：贴面避障半径很小，位置滞后插值会抄近路切进舰体；
	// 改用旋转滞后——镜头沿轨道球面连续滑动（而非弦线穿越），
	// 快速甩视角时每帧步进变小，避障的逐帧检查才接得住。
	SpringArm->bEnableCameraLag = false;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 40.0f;

	// 相机臂不继承机体旋转：飞船转向/滚转时镜头保持朝向，
	// 完全由鼠标控制（世界空间观察角）。
	// 注意：程序化设置继承开关不会自动转成绝对旋转，须显式开启。
	SpringArm->SetUsingAbsoluteRotation(true);

	// 创建相机
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);

	Camera->SetFieldOfView(90.0f);

	// 配置自由 6DOF 飞行（巨型舰手感：低加速 + 缓刹车保留惯性）
	GetCharacterMovement()->GravityScale = 0.0f;
	GetCharacterMovement()->bConstrainToPlane = false;
	GetCharacterMovement()->MaxFlySpeed = BaseMaxSpeed;
	GetCharacterMovement()->MaxAcceleration = BaseAcceleration;
	GetCharacterMovement()->BrakingDecelerationFlying = 60.0f;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 0.0f, 0.0f);
}

void AGuLiStrikeShip::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiStrikeShip, GMRuntimeState);
}

void AGuLiStrikeShip::BeginPlay()
{
	Super::BeginPlay();

	// 切换到自由飞行模式
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);

	// 兜底：实例上的表指针为空时回退到类 CDO 的配置
	// （经 MCPython 赋值的蓝图 CDO 指针在 PIE 实例上偶发取不到，根因见数据管线归档 20260821~22）
	if (!PartDataTable || !TuningDataTable || !CameraDataTable)
	{
		const AGuLiStrikeShip* ShipCDO = GetClass()->GetDefaultObject<AGuLiStrikeShip>();
		if (!PartDataTable)
		{
			PartDataTable = ShipCDO->PartDataTable;
		}
		if (!TuningDataTable)
		{
			TuningDataTable = ShipCDO->TuningDataTable;
		}
		if (!CameraDataTable)
		{
			CameraDataTable = ShipCDO->CameraDataTable;
		}
	}

	// 先应用调参表/相机表预设（可能覆盖本类默认值），再装配部件与推导数值
	ApplyTuningRow();
	ApplyCameraRow();

	// 滚轮期望臂长从蓝图配置的臂长起步；实际臂长由 Tick 的自身舰避障统一结算
	DesiredArmLength = SpringArm ? SpringArm->TargetArmLength : CameraZoomMax;

	// 缓存舰体包围球半径：相机避障第一段扫掠只需走到球外（短走廊），压到非舰物体再退全长
	if (HullMesh)
	{
		if (const UStaticMesh* HullAsset = HullMesh->GetStaticMesh())
		{
			HullBoundingRadius = HullAsset->GetBounds().SphereRadius * HullMesh->GetComponentScale().GetAbsMax();
		}
	}

	// 安装出生配装
	for (const FGuLiStrikeShipDefaultPart& Entry : DefaultParts)
	{
		InstallPart(Entry.PartClass, Entry.SocketName);
	}

	// 服务器从当前 World Registry 初始化后续出生飞船；客户端只消费该
	// Actor 的原子复制状态，避免本地 Registry 与服务器状态抢写。
	if (HasAuthority())
	{
		if (UGuLiRuntimeTuningSubsystem* RuntimeTuning =
			GetWorld()->GetSubsystem<UGuLiRuntimeTuningSubsystem>())
		{
			RuntimeTuning->ApplyCurrentShipTuning(*this);
		}
	}
	else
	{
		ApplyGMRuntimeStateLocally(false);
	}

	// GM state may have arrived before/during BeginPlay. Only now are tuning
	// tables and default parts ready for the first Blueprint-visible recompute.
	bRuntimeStatsInitialized = true;
	RecomputeStats();
}

void AGuLiStrikeShip::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// 玩家操控时抬高近裁剪面：贴面机位（探针间隙 250cm）下，视野边缘擦过的
	// 舰面细结构直接裁掉，避免近处切片闪烁（UCameraComponent 无逐相机覆盖，走全局 CVar）
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (IConsoleVariable* NearClipCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SetNearClippingPlane")))
		{
			NearClipCVar->Set(150.0f, ECVF_SetByGameSetting);
		}
	}

	// 添加飞船专属映射上下文，避免与默认角色上下文打架
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(ShipMappingContext, 0);
		}
	}
}

void AGuLiStrikeShip::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 建立 Enhanced Input 动作绑定
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(ForwardAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustForward);
		EnhancedInputComponent->BindAction(BackwardAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustBackward);
		EnhancedInputComponent->BindAction(StrafeRightAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustRight);
		EnhancedInputComponent->BindAction(StrafeLeftAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustLeft);
		EnhancedInputComponent->BindAction(AscendAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustUp);
		EnhancedInputComponent->BindAction(DescendAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustDown);
		EnhancedInputComponent->BindAction(TurnLeftAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::TurnLeft);
		EnhancedInputComponent->BindAction(TurnRightAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::TurnRight);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::Look);
		EnhancedInputComponent->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ZoomCamera);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::BoostStart);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Completed, this, &AGuLiStrikeShip::BoostEnd);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::Fire);
		EnhancedInputComponent->BindAction(CycleEnginesAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::CycleEngines);
		EnhancedInputComponent->BindAction(CycleWeaponsAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::CycleWeapons);
	}
}

void AGuLiStrikeShip::ThrustForward(const FInputActionValue& Value)
{
	// 记录推进意图（自动转向判定用）
	PendingThrustIntent += GetActorForwardVector();

	// 加力按住时推力翻倍
	AddMovementInput(GetActorForwardVector(), bBoosting ? BoostThrustMultiplier : 1.0f);
}

void AGuLiStrikeShip::ThrustBackward(const FInputActionValue& Value)
{
	PendingThrustIntent -= GetActorForwardVector();

	// 反推减速
	AddMovementInput(GetActorForwardVector(), bBoosting ? -BoostThrustMultiplier : -1.0f);
}

void AGuLiStrikeShip::ThrustRight(const FInputActionValue& Value)
{
	// 记录侧移输入（Tick 里用于压弯倾斜）
	PendingStrafeInput += 1.0f;
	PendingThrustIntent += GetActorRightVector();

	// 加力按住时推力翻倍
	AddMovementInput(GetActorRightVector(), bBoosting ? BoostThrustMultiplier : 1.0f);
}

void AGuLiStrikeShip::ThrustLeft(const FInputActionValue& Value)
{
	// 记录侧移输入（Tick 里用于压弯倾斜）
	PendingStrafeInput -= 1.0f;
	PendingThrustIntent -= GetActorRightVector();

	// 加力按住时推力翻倍
	AddMovementInput(GetActorRightVector(), bBoosting ? -BoostThrustMultiplier : -1.0f);
}

void AGuLiStrikeShip::ThrustUp(const FInputActionValue& Value)
{
	PendingThrustIntent += GetActorUpVector();

	// 加力按住时推力翻倍
	AddMovementInput(GetActorUpVector(), bBoosting ? BoostThrustMultiplier : 1.0f);
}

void AGuLiStrikeShip::ThrustDown(const FInputActionValue& Value)
{
	PendingThrustIntent -= GetActorUpVector();

	// 加力按住时推力翻倍
	AddMovementInput(GetActorUpVector(), bBoosting ? -BoostThrustMultiplier : -1.0f);
}

void AGuLiStrikeShip::TurnLeft(const FInputActionValue& Value)
{
	// 记录转向输入（角速度与压弯都在 Tick 里统一结算）
	PendingTurnInput -= 1.0f;
}

void AGuLiStrikeShip::TurnRight(const FInputActionValue& Value)
{
	// 记录转向输入（角速度与压弯都在 Tick 里统一结算）
	PendingTurnInput += 1.0f;
}

void AGuLiStrikeShip::Look(const FInputActionValue& Value)
{
	// 取输入向量
	const FVector2D InputVector = Value.Get<FVector2D>();

	if (!SpringArm)
	{
		return;
	}

	// 鼠标只环绕相机臂（相机始终对着飞船），不改变飞船朝向
	FRotator ArmRotation = SpringArm->GetRelativeRotation();
	ArmRotation.Yaw += InputVector.X * MouseYawScale;
	ArmRotation.Pitch = FMath::Clamp(ArmRotation.Pitch + InputVector.Y * MousePitchScale, CameraPitchMin, CameraPitchMax);
	SpringArm->SetRelativeRotation(ArmRotation);
}

void AGuLiStrikeShip::ZoomCamera(const FInputActionValue& Value)
{
	// 滚轮上滚（+1）= 拉近；只改期望臂长，实际臂长由 Tick 的自身舰避障统一结算
	const float Notches = Value.Get<float>();
	DesiredArmLength = FMath::Clamp(DesiredArmLength - Notches * CameraZoomStep, CameraZoomMin, CameraZoomMax);
}

namespace
{
	/** 相机避障只认舰船与地形：其余实体（道具/方块/装饰物）不参与镜头避障 */
	bool IsCameraRelevantOwner(const AActor* Owner)
	{
		return Owner && (Owner->IsA<AGuLiStrikeShip>() || Owner->IsA<ALandscapeProxy>());
	}
}

void AGuLiStrikeShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ===== 相机 vs 舰体/地形避障（单写者：只有 ResolveCameraArmCollision 写臂长）=====
	ResolveCameraArmCollision();

	// ===== 偏航角速度（惯性模型）=====
	// 有输入：角速度向目标（±YawRate）平滑爬升；
	// 无输入：角速度按阻尼衰减——松键后飞船带着惯性继续转、缓缓停住
	const float TargetYawVelocity = FMath::Clamp(PendingTurnInput, -1.0f, 1.0f) * YawRate;
	if (PendingTurnInput != 0.0f)
	{
		YawVelocity = FMath::FInterpTo(YawVelocity, TargetYawVelocity, DeltaTime, YawResponseSpeed);
	}
	else
	{
		YawVelocity = FMath::FInterpTo(YawVelocity, 0.0f, DeltaTime, YawStopDamping);
		if (FMath::Abs(YawVelocity) < 0.05f)
		{
			YawVelocity = 0.0f;
		}
	}

	// 应用偏航：只改欧拉偏航分量（等效绕世界竖直轴转向）。
	// 不能用 AddActorLocalRotation——压弯倾斜会让局部上轴歪掉，
	// 绕歪轴偏航会让船头画圆锥、俯仰角随转向相位漂移。
	if (!FMath::IsNearlyZero(YawVelocity))
	{
		FRotator Rotation = GetActorRotation();
		Rotation.Yaw += YawVelocity * DeltaTime;
		SetActorRotation(Rotation);
	}

	// ===== 自动转向（默认关闭）=====
	// 依据推进意图方向自动转向；意图大体朝前才转（倒退/纯侧移保持船头，
	// 避免"船头追速度"的正反馈打转）
	if (bOrientToMovement && PendingThrustIntent.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		const FVector IntentDirection = PendingThrustIntent.GetSafeNormal();
		if (FVector::DotProduct(IntentDirection, GetActorForwardVector()) > OrientMinForwardDot)
		{
			const FRotator TargetRotation = IntentDirection.Rotation();
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaTime, OrientTurnSpeed));
		}
	}

	// ===== 压弯倾斜 =====
	// 偏航（跟随实际角速度，惯性旋转期间保持倾斜）或侧移（A/D，含 W+侧移组合）
	// 时机身向对应侧倾斜 MaxBankAngle，输入结束后平滑回正；直接接管滚转分量
	const float BankDirection = FMath::Clamp(YawVelocity / YawRate + PendingStrafeInput, -1.0f, 1.0f);
	CurrentBankRoll = FMath::FInterpTo(CurrentBankRoll, -BankDirection * MaxBankAngle, DeltaTime, BankInterpSpeed);
	FRotator LeveledRotation = GetActorRotation();
	LeveledRotation.Roll = CurrentBankRoll;
	SetActorRotation(LeveledRotation);

	// 消费本帧输入；下一次输入事件会重新累积
	PendingThrustIntent = FVector::ZeroVector;
	PendingTurnInput = 0.0f;
	PendingStrafeInput = 0.0f;
}

void AGuLiStrikeShip::ResolveCameraArmCollision()
{
	// 只认舰船与地形，其余实体由谓词过滤。SpringArm 自带探测按引擎设计忽略
	// Owner 且已关闭（会幽灵回缩，另案归档）。判据两级：
	// 1) 端点门控——探针球在期望机位压不到相关几何体就直接用期望臂长（臂的路径
	//    不需要干净，只有镜头端点需要；大探针半径下按整条路径扫掠会误推，已踩坑）；
	// 2) 压到才推出——由外向内扫掠取相关命中面（凸包内起步的向外扫掠引擎不报命中，
	//    方向必须由外向内）。两段式：先走舰体包围球外的短走廊（自身舰必被覆盖，
	//    密集场景下走廊内的无关候选最少），短走廊无相关命中（压的是地形/他舰）再
	//    退全长扫掠，语义与单段全长完全等价。
	if (!SpringArm)
	{
		return;
	}

	const FVector PivotLocation = SpringArm->GetComponentLocation();
	const FVector ArmDirection = -SpringArm->GetForwardVector().GetSafeNormal();

	// Socket 偏移（拔高相机等）计入扫掠线：期望机位 = 球心 + 偏移 + 臂方向 × 期望臂长
	const FVector CurrentSocket = SpringArm->GetSocketLocation(USpringArmComponent::SocketName);
	const FVector SocketOffsetWorld = CurrentSocket - (PivotLocation + ArmDirection * SpringArm->TargetArmLength);
	const FVector OrbitPivot = PivotLocation + SocketOffsetWorld;
	const FVector DesiredLocation = OrbitPivot + ArmDirection * DesiredArmLength;

	const FCollisionObjectQueryParams ObjectQuery(
		ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic));
	const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(CameraCollisionProbeRadius);
	FCollisionQueryParams Params(TEXT("ShipCameraVsHull"), /*bTraceComplex=*/false, /*IgnoreActor=*/nullptr);
	Params.bFindInitialOverlaps = false;

	// 门控：期望点压到的实体里有没有舰船/地形
	bool bBlocked = false;
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, DesiredLocation, FQuat::Identity, ObjectQuery, ProbeShape, Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (IsCameraRelevantOwner(Overlap.GetActor()))
		{
			bBlocked = true;
			break;
		}
	}

	float NewArm = DesiredArmLength;
	if (bBlocked)
	{
		// 由外向内扫到期望点，取相关命中的最外侧面（保守推出：镜头在走廊内所有相关几何体外）
		auto SweepRelevantFace = [this, &DesiredLocation, &ObjectQuery, &ProbeShape, &Params](const FVector& Start, float Length) -> float
		{
			TArray<FHitResult> Hits;
			GetWorld()->SweepMultiByObjectType(
				Hits, Start, DesiredLocation, FQuat::Identity, ObjectQuery, ProbeShape, Params);
			float Face = -1.0f;
			for (const FHitResult& Hit : Hits)
			{
				if (IsCameraRelevantOwner(Hit.GetActor()))
				{
					Face = FMath::Max(Face, Length - Hit.Distance);
				}
			}
			return Face;
		};

		float NearestFace = -1.0f;

		// 第一段：舰体包围球外沿（短走廊），期望点在球内时才有效
		const float ShipSphereArm = HullBoundingRadius + CameraCollisionProbeRadius * 2.0f;
		if (ShipSphereArm > DesiredArmLength)
		{
			NearestFace = SweepRelevantFace(OrbitPivot + ArmDirection * ShipSphereArm, ShipSphereArm - DesiredArmLength);
		}

		// 第二段：短走廊无相关命中（压的是地形/他舰等非自身舰物体）→ 全长扫掠
		if (NearestFace < 0.0f)
		{
			const float TraceLength = CameraZoomMax + CameraCollisionProbeRadius * 2.0f;
			NearestFace = SweepRelevantFace(OrbitPivot + ArmDirection * TraceLength, TraceLength - DesiredArmLength);
		}

		if (NearestFace > 0.0f)
		{
			NewArm = FMath::Max(DesiredArmLength + NearestFace, CameraCollisionMinArm);
		}
	}

	// 单写者纪律：实际臂长只有这一处结算
	SpringArm->TargetArmLength = NewArm;
}

void AGuLiStrikeShip::BoostStart(const FInputActionValue& Value)
{
	bBoosting = true;
}

void AGuLiStrikeShip::BoostEnd(const FInputActionValue& Value)
{
	bBoosting = false;
}

void AGuLiStrikeShip::Fire(const FInputActionValue& Value)
{
	FireInstalledWeapons();
}

void AGuLiStrikeShip::CycleEngines(const FInputActionValue& Value)
{
	CycleParts(UGuLiStrikeEnginePart::StaticClass());
}

void AGuLiStrikeShip::CycleWeapons(const FInputActionValue& Value)
{
	CycleParts(UGuLiStrikeWeaponPart::StaticClass());
}

bool AGuLiStrikeShip::InstallPart(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass, FName SocketName)
{
	// 校验部件类
	if (!PartClass)
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("InstallPart: null part class for socket %s"), *SocketName.ToString());
		return false;
	}

	// 舰体网格体上必须存在该 socket
	if (!HullMesh || !HullMesh->DoesSocketExist(SocketName))
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("InstallPart: hull mesh has no socket %s"), *SocketName.ToString());
		return false;
	}

	// 部件必须把该 socket 列为兼容
	const UGuLiStrikeShipPartComponent* PartCDO = Cast<UGuLiStrikeShipPartComponent>(PartClass->GetDefaultObject());
	if (!PartCDO || !PartCDO->CanAttachToSocket(SocketName))
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("InstallPart: %s is not compatible with socket %s"), *PartClass->GetName(), *SocketName.ToString());
		return false;
	}

	// 替换该槽位上已有的部件
	UninstallPart(SocketName);

	// 动态创建部件组件并挂接到 socket
	UGuLiStrikeShipPartComponent* Part = NewObject<UGuLiStrikeShipPartComponent>(this, PartClass);
	Part->RegisterComponent();
	Part->AttachToComponent(HullMesh, FAttachmentTransformRules::KeepRelativeTransform, SocketName);
	Part->SetRelativeTransform(PartCDO->PartRelativeTransform);

	// 数据表数值覆盖（无表/无行时保持蓝图默认值），随后照常走 ContributeStats 聚合
	ApplyPartRow(Part);

	InstalledParts.Add({SocketName, Part});

	BP_OnPartInstalled(Part, SocketName);
	RecomputeStats();

	UE_LOG(LogGuLiStrike, Log, TEXT("InstallPart: %s installed on %s"), *PartClass->GetName(), *SocketName.ToString());
	return true;
}

bool AGuLiStrikeShip::UninstallPart(FName SocketName)
{
	for (int32 Index = 0; Index < InstalledParts.Num(); ++Index)
	{
		if (InstalledParts[Index].SocketName == SocketName)
		{
			if (UGuLiStrikeShipPartComponent* Part = InstalledParts[Index].Part.Get())
			{
				BP_OnPartUninstalled(Part, SocketName);

				// 先移出注册表再销毁：销毁触发的 NotifyPartDestroyed
				// 会发现部件已注销而自然跳过，避免双重事件/重算
				InstalledParts.RemoveAt(Index);
				Part->DestroyComponent();
			}
			else
			{
				// 陈旧条目清理
				InstalledParts.RemoveAt(Index);
			}

			RecomputeStats();
			return true;
		}
	}

	return false;
}

void AGuLiStrikeShip::NotifyPartDestroyed(UGuLiStrikeShipPartComponent* Part)
{
	// 部件被外部直接销毁（未经 UninstallPart）时的兜底同步
	for (int32 Index = 0; Index < InstalledParts.Num(); ++Index)
	{
		if (InstalledParts[Index].Part.Get() == Part)
		{
			BP_OnPartUninstalled(Part, InstalledParts[Index].SocketName);
			InstalledParts.RemoveAt(Index);
			RecomputeStats();

			UE_LOG(LogGuLiStrike, Log, TEXT("NotifyPartDestroyed: %s was destroyed externally, registry synced"), *Part->GetName());
			return;
		}
	}
}

void AGuLiStrikeShip::AddStatModifier(FName Name, float MaxSpeedMultiplier, float AccelerationMultiplier)
{
	if (Name == GuLiStrikeShipPrivate::GMRuntimeModifierName)
	{
		UE_LOG(
			LogGuLiStrike,
			Warning,
			TEXT("AddStatModifier rejected reserved modifier name '%s'; use the server GM runtime interface."),
			*Name.ToString());
		return;
	}

	// 同名覆盖；直接清理后只重算一次。
	StatModifiers.RemoveAll([Name](const FGuLiStrikeStatModifier& Modifier)
	{
		return Modifier.Name == Name;
	});

	FGuLiStrikeStatModifier Modifier;
	Modifier.Name = Name;
	Modifier.MaxSpeedMultiplier = MaxSpeedMultiplier;
	Modifier.AccelerationMultiplier = AccelerationMultiplier;
	StatModifiers.Add(Modifier);

	RecomputeStats();
}

void AGuLiStrikeShip::RemoveStatModifier(FName Name)
{
	if (Name == GuLiStrikeShipPrivate::GMRuntimeModifierName)
	{
		UE_LOG(
			LogGuLiStrike,
			Warning,
			TEXT("RemoveStatModifier rejected reserved modifier name '%s'; use the server GM runtime interface."),
			*Name.ToString());
		return;
	}

	const int32 RemovedCount = StatModifiers.RemoveAll(
		[Name](const FGuLiStrikeStatModifier& Modifier)
		{
			return Modifier.Name == Name;
		});
	if (RemovedCount > 0)
	{
		RecomputeStats();
	}
}

bool AGuLiStrikeShip::SetGMRuntimeMultipliers(
	const float MaxSpeedMultiplier,
	const float AccelerationMultiplier)
{
	if (!FMath::IsFinite(MaxSpeedMultiplier)
		|| !FMath::IsFinite(AccelerationMultiplier)
		|| MaxSpeedMultiplier < 0.0f
		|| AccelerationMultiplier < 0.0f
		|| MaxSpeedMultiplier > 100.0f
		|| AccelerationMultiplier > 100.0f
		|| !HasAuthority())
	{
		return false;
	}

	const bool bStateChanged = !GMRuntimeState.bActive
		|| !FMath::IsNearlyEqual(GMRuntimeState.MaxSpeedMultiplier, MaxSpeedMultiplier)
		|| !FMath::IsNearlyEqual(
			GMRuntimeState.AccelerationMultiplier,
			AccelerationMultiplier);
	GMRuntimeState.bActive = true;
	GMRuntimeState.MaxSpeedMultiplier = MaxSpeedMultiplier;
	GMRuntimeState.AccelerationMultiplier = AccelerationMultiplier;
	if (bStateChanged)
	{
		++GMRuntimeState.Revision;
		if (GMRuntimeState.Revision == 0u)
		{
			GMRuntimeState.Revision = 1u;
		}
	}

	ApplyGMRuntimeStateLocally(bRuntimeStatsInitialized);
	if (bStateChanged)
	{
		ForceNetUpdate();
	}
	return true;
}

void AGuLiStrikeShip::ClearGMRuntimeMultipliers()
{
	if (!HasAuthority())
	{
		return;
	}

	const bool bStateChanged = GMRuntimeState.bActive
		|| !FMath::IsNearlyEqual(GMRuntimeState.MaxSpeedMultiplier, 1.0f)
		|| !FMath::IsNearlyEqual(GMRuntimeState.AccelerationMultiplier, 1.0f);
	GMRuntimeState.bActive = false;
	GMRuntimeState.MaxSpeedMultiplier = 1.0f;
	GMRuntimeState.AccelerationMultiplier = 1.0f;
	if (bStateChanged)
	{
		++GMRuntimeState.Revision;
		if (GMRuntimeState.Revision == 0u)
		{
			GMRuntimeState.Revision = 1u;
		}
	}

	ApplyGMRuntimeStateLocally(bRuntimeStatsInitialized);
	if (bStateChanged)
	{
		ForceNetUpdate();
	}
}

bool AGuLiStrikeShip::GetGMRuntimeMultipliers(
	float& OutMaxSpeedMultiplier,
	float& OutAccelerationMultiplier) const
{
	if (GMRuntimeState.bActive)
	{
		OutMaxSpeedMultiplier = GMRuntimeState.MaxSpeedMultiplier;
		OutAccelerationMultiplier = GMRuntimeState.AccelerationMultiplier;
		return true;
	}

	OutMaxSpeedMultiplier = 1.0f;
	OutAccelerationMultiplier = 1.0f;
	return false;
}

void AGuLiStrikeShip::OnRep_GMRuntimeState()
{
	// Initial replicated properties may arrive before BeginPlay. Build the
	// modifier layer immediately, but defer Blueprint-visible stat callbacks
	// until BeginPlay has initialized tuning tables and default parts.
	ApplyGMRuntimeStateLocally(bRuntimeStatsInitialized);
}

void AGuLiStrikeShip::ApplyGMRuntimeStateLocally(const bool bRecompute)
{
	StatModifiers.RemoveAll([](const FGuLiStrikeStatModifier& Modifier)
	{
		return Modifier.Name == GuLiStrikeShipPrivate::GMRuntimeModifierName;
	});

	if (GMRuntimeState.bActive)
	{
		FGuLiStrikeStatModifier& Modifier = StatModifiers.AddDefaulted_GetRef();
		Modifier.Name = GuLiStrikeShipPrivate::GMRuntimeModifierName;
		Modifier.MaxSpeedMultiplier = GMRuntimeState.MaxSpeedMultiplier;
		Modifier.AccelerationMultiplier = GMRuntimeState.AccelerationMultiplier;
	}
	if (bRecompute)
	{
		RecomputeStats();
	}
}

UGuLiStrikeShipPartComponent* AGuLiStrikeShip::GetPartAt(FName SocketName) const
{
	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		if (Entry.SocketName == SocketName)
		{
			return Entry.Part.Get();
		}
	}

	return nullptr;
}

void AGuLiStrikeShip::CycleParts(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass)
{
	if (!PartClass)
	{
		return;
	}

	// 收集当前装有该类型部件的 socket
	TArray<FName> SocketsToCycle;
	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		if (Entry.Part.IsValid() && Entry.Part->GetClass()->IsChildOf(PartClass))
		{
			SocketsToCycle.Add(Entry.SocketName);
		}
	}

	if (SocketsToCycle.IsEmpty())
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("CycleParts: no installed parts of class %s"), *PartClass->GetName());
		return;
	}

	for (const FName& SocketName : SocketsToCycle)
	{
		CyclePartAtSocket(PartClass, SocketName);
	}
}

bool AGuLiStrikeShip::CyclePartAtSocket(UClass* PartClass, FName SocketName)
{
	// 收集目录里允许接入该 socket 的该类型部件
	TArray<TSubclassOf<UGuLiStrikeShipPartComponent>> Candidates;
	for (const TSubclassOf<UGuLiStrikeShipPartComponent>& CatalogueEntry : PartCatalogue)
	{
		if (!CatalogueEntry || !CatalogueEntry->IsChildOf(PartClass))
		{
			continue;
		}

		if (const UGuLiStrikeShipPartComponent* PartCDO = Cast<UGuLiStrikeShipPartComponent>(CatalogueEntry->GetDefaultObject()))
		{
			if (PartCDO->CanAttachToSocket(SocketName))
			{
				Candidates.AddUnique(CatalogueEntry);
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("CyclePartAtSocket: no compatible parts for socket %s"), *SocketName.ToString());
		return false;
	}

	// 找到当前部件在候选列表中的位置
	int32 CurrentIndex = INDEX_NONE;
	if (const UGuLiStrikeShipPartComponent* CurrentPart = GetPartAt(SocketName))
	{
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Candidates[Index] == CurrentPart->GetClass())
			{
				CurrentIndex = Index;
				break;
			}
		}
	}

	// 切换到下一个候选（循环回绕，只有一个候选时就等于重装一次）
	const int32 NextIndex = (CurrentIndex + 1) % Candidates.Num();
	return InstallPart(Candidates[NextIndex], SocketName);
}

void AGuLiStrikeShip::FireInstalledWeapons()
{
	// 开火行为由各部件自己实现（武器部件自查冷却并出弹，其它部件不响应）
	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		if (UGuLiStrikeShipPartComponent* Part = Entry.Part.Get())
		{
			Part->Fire(this);
		}
	}
}

void AGuLiStrikeShip::RecomputeStats()
{
	// 各部件多态贡献自己的数值，飞船只负责聚合与应用
	FGuLiStrikeShipStats Stats;
	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		if (UGuLiStrikeShipPartComponent* Part = Entry.Part.Get())
		{
			Part->ContributeStats(Stats);
		}
	}

	TotalThrust = Stats.ThrustSum;
	TotalMass = HullMass + Stats.PartMassSum;

	// 推重比围绕标称比值决定基础倍率
	const float ThrustRatio = TotalMass > 0.0f ? TotalThrust / TotalMass : 0.0f;
	const float SpeedMultiplier = FMath::Clamp(ThrustRatio / NominalThrustRatio, SpeedMultiplierMin, SpeedMultiplierMax);

	// 数值修饰层（超载/减速等）：全部倍率连乘叠加
	float ModifierSpeedMultiplier = 1.0f;
	float ModifierAccelerationMultiplier = 1.0f;
	for (const FGuLiStrikeStatModifier& Modifier : StatModifiers)
	{
		ModifierSpeedMultiplier *= Modifier.MaxSpeedMultiplier;
		ModifierAccelerationMultiplier *= Modifier.AccelerationMultiplier;
	}

	CurrentMaxSpeed = BaseMaxSpeed * SpeedMultiplier * ModifierSpeedMultiplier;

	// 移动参数只在 RecomputeStats 里写入——其它系统请走 AddStatModifier，
	// 避免多方直写互相覆盖
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxFlySpeed = CurrentMaxSpeed;
		Movement->MaxAcceleration = BaseAcceleration * SpeedMultiplier * ModifierAccelerationMultiplier;
	}

	BP_OnStatsChanged();
}

bool AGuLiStrikeShip::ApplyPartRow(UGuLiStrikeShipPartComponent* Part) const
{
	if (!Part)
	{
		return false;
	}

	// 无表或部件未登记 PartId：保持蓝图默认数值（向后兼容）
	if (!PartDataTable || Part->PartId.IsNone())
	{
		return false;
	}

	// 行名 = 表内 name 列（部件显示名）；按 PartId 列匹配行
	const FGuLiStrikeShipPartsRow* Row = nullptr;
	FName RowName = NAME_None;
	for (const FName& RowKey : PartDataTable->GetRowNames())
	{
		const FGuLiStrikeShipPartsRow* Candidate =
			PartDataTable->FindRow<FGuLiStrikeShipPartsRow>(RowKey, TEXT("ApplyPartRow"));
		if (Candidate && Candidate->PartId == Part->PartId.ToString())
		{
			Row = Candidate;
			RowName = RowKey;
			break;
		}
	}

	if (!Row)
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("ApplyPartRow: no row with PartId '%s' for part %s, keeping class defaults"), *Part->PartId.ToString(), *Part->GetName());
		return false;
	}

	// 通用数值
	Part->PartMass = Row->PartMass;
	Part->PartDisplayName = FText::FromString(RowName.ToString());

	// 类型专属数值：行里的值写到对应子类实例上
	if (UGuLiStrikeEnginePart* Engine = Cast<UGuLiStrikeEnginePart>(Part))
	{
		Engine->Thrust = Row->Thrust;
	}

	if (UGuLiStrikeWeaponPart* Weapon = Cast<UGuLiStrikeWeaponPart>(Part))
	{
		Weapon->Damage = Row->Damage;
		Weapon->FireRate = Row->FireRate;
		Weapon->MuzzleOffset = Row->Muzzle;
		if (UClass* LoadedClass = Row->ProjectileClass.LoadSynchronous())
		{
			Weapon->ProjectileClass = LoadedClass;
		}
	}

	return true;
}

bool AGuLiStrikeShip::ApplyTuningRow()
{
	// 无表或未选预设：保持本类默认数值（向后兼容）
	if (!TuningDataTable || TuningPreset.IsNone())
	{
		return false;
	}

	const FGuLiStrikeShipTuningRow* Row = TuningDataTable->FindRow<FGuLiStrikeShipTuningRow>(TuningPreset, TEXT("ApplyTuningRow"));
	if (!Row)
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("ApplyTuningRow: no preset '%s' in tuning table, keeping class defaults"), *TuningPreset.ToString());
		return false;
	}

	HullMass = Row->HullMass;
	BaseMaxSpeed = Row->BaseMaxSpeed;
	BaseAcceleration = Row->BaseAcceleration;
	NominalThrustRatio = Row->NominalThrustRatio;
	SpeedMultiplierMin = Row->SpeedMultiplierMin;
	SpeedMultiplierMax = Row->SpeedMultiplierMax;
	BoostThrustMultiplier = Row->BoostThrustMultiplier;

	MousePitchScale = Row->MousePitchScale;
	MouseYawScale = Row->MouseYawScale;
	YawRate = Row->YawRate;
	YawResponseSpeed = Row->YawResponseSpeed;
	YawStopDamping = Row->YawStopDamping;
	MaxBankAngle = Row->MaxBankAngle;
	BankInterpSpeed = Row->BankInterpSpeed;
	OrientTurnSpeed = Row->OrientTurnSpeed;
	bOrientToMovement = Row->OrientToMovement;
	OrientMinForwardDot = Row->OrientMinForwardDot;

	// 舰体偏移在构造函数里已按默认值定位过一次，预设覆盖后需重新应用
	if (HullMesh && HullMeshOffset != Row->HullMeshOffset)
	{
		HullMeshOffset = Row->HullMeshOffset;
		HullMesh->SetRelativeLocation(HullMeshOffset);
	}

	return true;
}

bool AGuLiStrikeShip::ApplyCameraRow()
{
	// 无表或未选预设：保持本类默认数值（向后兼容）
	if (!CameraDataTable || TuningPreset.IsNone())
	{
		return false;
	}

	const FGuLiStrikeShipCameraRow* Row = CameraDataTable->FindRow<FGuLiStrikeShipCameraRow>(TuningPreset, TEXT("ApplyCameraRow"));
	if (!Row)
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("ApplyCameraRow: no preset '%s' in camera table, keeping class defaults"), *TuningPreset.ToString());
		return false;
	}

	CameraDefaultArmLength = Row->CameraDefaultArmLength;
	CameraZoomStep = Row->CameraZoomStep;
	CameraZoomMin = Row->CameraZoomMin;
	CameraZoomMax = Row->CameraZoomMax;
	CameraCollisionProbeRadius = Row->CameraCollisionProbeRadius;
	CameraCollisionMinArm = Row->CameraCollisionMinArm;
	CameraPitchMin = Row->CameraPitchMin;
	CameraPitchMax = Row->CameraPitchMax;

	// 蓝图模板臂长只是初值，表值覆盖后同步给弹簧臂
	// （随后 DesiredArmLength 的初始化从 SpringArm->TargetArmLength 起步）
	if (SpringArm && SpringArm->TargetArmLength != CameraDefaultArmLength)
	{
		SpringArm->TargetArmLength = CameraDefaultArmLength;
	}

	return true;
}
