// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeShip.h"
#include "GuLiShipMovementComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
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

AGuLiStrikeShip::AGuLiStrikeShip(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGuLiShipMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	bReplicates = true;
	// 固定 5v5 的玩家载具始终相关；远距飞船相机不能把近旁玩家按默认 150m 距离剔除。
	bAlwaysRelevant = true;

	// 飞船自己掌控完整的三维姿态
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 创建舰体网格体；其上的 socket 是部件挂点
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull Mesh"));
	// 静态舰体跟随 CharacterMesh0 的网络平滑偏移；胶囊仍是服务器碰撞与预测根。
	HullMesh->SetupAttachment(GetMesh());
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
	// GM 状态和装配分别复制；移动组件用完整配置屏障处理它们的跨属性到达顺序。
	DOREPLIFETIME(AGuLiStrikeShip, GMRuntimeState);
	DOREPLIFETIME(AGuLiStrikeShip, LoadoutState);
}

void AGuLiStrikeShip::BeginPlay()
{
	Super::BeginPlay();
	// 旧蓝图可能序列化了原 CharacterMovement 模板；未迁移时明确关闭操控，不能解引用空的专用组件。
	if (!GetShipMovement())
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Ship %s has an incompatible CharMoveComp; recompile its Blueprint with GuLiShipMovementComponent before play."), *GetName());
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		SetActorTickEnabled(false);
		return;
	}

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

	// 默认装配只由服务器决定。客户端等待名册，不能在复制到达前擅自装一套默认部件。
	if (HasAuthority())
	{
		bEditingLoadout = true;
		for (const FGuLiStrikeShipDefaultPart& Entry : DefaultParts)
		{
			InstallPartLocally(Entry.PartClass, Entry.SocketName);
		}
		bEditingLoadout = false;
	}

	// 服务器从当前 World Registry 初始化后续出生飞船；客户端只消费该
	// Actor 的原子复制状态，避免本地 Registry 与服务器状态抢写。
	if (HasAuthority())
	{
		if (UGuLiRuntimeTuningSubsystem* RuntimeTuning = GetWorld()->GetSubsystem<UGuLiRuntimeTuningSubsystem>())
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
	if (HasAuthority())
	{
		PublishLoadout();
	}
	else
	{
		ApplyReplicatedLoadout();
	}
	UpdateShipInputContext();
}

void AGuLiStrikeShip::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	if (UGuLiShipMovementComponent* Movement = GetShipMovement())
	{
		Movement->ClearFlightInput();
		// 当帧短暂解除并重新占有也必须推进身份屏障，不能等 Tick 才发现连接变化。
		Movement->SetAppliedLoadoutRevision(AppliedLoadoutRevision);
	}
	bServerFiring = false;
	UpdateShipInputContext();
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
		// 屏障期间清空输入，按住的键在新配置就绪后重新采样；不会产生逐帧移动之外的 RPC。
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::BoostStart);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Completed, this, &AGuLiStrikeShip::BoostEnd);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::Fire);
		// SetFiringIntent 仅在有效意图变动时发 RPC；持键跨换装屏障后可恢复，稳态每帧不会重发。
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::Fire);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AGuLiStrikeShip::StopFire);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Canceled, this, &AGuLiStrikeShip::StopFire);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Canceled, this, &AGuLiStrikeShip::BoostEnd);
		EnhancedInputComponent->BindAction(CycleEnginesAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::CycleEngines);
		EnhancedInputComponent->BindAction(CycleWeaponsAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::CycleWeapons);
	}
}

void AGuLiStrikeShip::ThrustForward(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddThrustInput(FVector(1.0, 0.0, 0.0));
	}
}

void AGuLiStrikeShip::ThrustBackward(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddThrustInput(FVector(-1.0, 0.0, 0.0));
	}
}

void AGuLiStrikeShip::ThrustRight(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddThrustInput(FVector(0.0, 1.0, 0.0));
		GetShipMovement()->AddStrafeInput(1.0f);
	}
}

void AGuLiStrikeShip::ThrustLeft(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddThrustInput(FVector(0.0, -1.0, 0.0));
		GetShipMovement()->AddStrafeInput(-1.0f);
	}
}

void AGuLiStrikeShip::ThrustUp(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddThrustInput(FVector(0.0, 0.0, 1.0));
	}
}

void AGuLiStrikeShip::ThrustDown(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddThrustInput(FVector(0.0, 0.0, -1.0));
	}
}

void AGuLiStrikeShip::TurnLeft(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddTurnInput(-1.0f);
	}
}

void AGuLiStrikeShip::TurnRight(const FInputActionValue& Value)
{
	if (CanUseShipControls())
	{
		GetShipMovement()->AddTurnInput(1.0f);
	}
}

void AGuLiStrikeShip::Look(const FInputActionValue& Value)
{
	if (!IsLocallyControlled()) { return; }
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
	if (!IsLocallyControlled()) { return; }
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
	UpdateShipInputContext();
	if (IsLocallyControlled())
	{
		ResolveCameraArmCollision();
	}
	if (!HasAuthority() && AppliedLoadoutRevision != LoadoutState.Revision
		&& GetWorld()->GetTimeSeconds() >= NextLoadoutRetryTime)
	{
		NextLoadoutRetryTime = GetWorld()->GetTimeSeconds() + 1.0;
		ApplyReplicatedLoadout();
	}
	if (HasAuthority() && bServerFiring)
	{
		if (CanAcceptServerIntent())
		{
			FireInstalledWeapons();
		}
		else
		{
			// 失去占有、配置切代、公共断线或比赛结束均停止持续射击。
			bServerFiring = false;
		}
	}
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
	if (CanUseShipControls())
	{
		GetShipMovement()->SetBoostInput(true);
	}
}

void AGuLiStrikeShip::BoostEnd(const FInputActionValue& Value)
{
	if (GetShipMovement())
	{
		GetShipMovement()->SetBoostInput(false);
	}
}

void AGuLiStrikeShip::Fire(const FInputActionValue& Value)
{
	SetFiringIntent(true);
}

void AGuLiStrikeShip::StopFire(const FInputActionValue& Value)
{
	SetFiringIntent(false);
}

void AGuLiStrikeShip::CycleEngines(const FInputActionValue& Value)
{
	if (!CanUseShipControls()) { return; }
	ServerRequestCycleParts(true, LoadoutState.Revision, GetShipMovement()->GetMovementBarrierGeneration());
}

void AGuLiStrikeShip::CycleWeapons(const FInputActionValue& Value)
{
	if (!CanUseShipControls()) { return; }
	ServerRequestCycleParts(false, LoadoutState.Revision, GetShipMovement()->GetMovementBarrierGeneration());
}

bool AGuLiStrikeShip::InstallPartLocally(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass, FName SocketName)
{
	// 校验部件类
	if (!PartClass || PartClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
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

	// 先确认新组件能注册，再卸旧件；无效蓝图配置不能令已有槽位被先清空。
	UGuLiStrikeShipPartComponent* Part = NewObject<UGuLiStrikeShipPartComponent>(this, PartClass);
	if (!IsValid(Part)) { return false; }
	Part->RegisterComponent();
	if (!IsValid(Part) || !Part->IsRegistered())
	{
		if (IsValid(Part)) { Part->DestroyComponent(); }
		return false;
	}
	if (!Part->AttachToComponent(HullMesh, FAttachmentTransformRules::KeepRelativeTransform, SocketName))
	{
		Part->DestroyComponent();
		return false;
	}
	UninstallPartLocally(SocketName);
	Part->SetRelativeTransform(PartCDO->PartRelativeTransform);

	// 数据表数值覆盖（无表/无行时保持蓝图默认值），随后照常走 ContributeStats 聚合
	ApplyPartRow(Part);

	InstalledParts.Add({SocketName, Part});
	bLoadoutDirty = true;

	BP_OnPartInstalled(Part, SocketName);
	if (!IsValid(Part) || !Part->IsRegistered() || GetPartAt(SocketName) != Part
		|| Part->GetAttachParent() != HullMesh || Part->GetAttachSocketName() != SocketName)
	{
		// 蓝图事件也可能销毁/改挂接；失败不能把半套名册 ACK 为就绪。
		if (GetPartAt(SocketName) == Part) { UninstallPartLocally(SocketName); }
		return false;
	}
	RecomputeStats();

	UE_LOG(LogGuLiStrike, Log, TEXT("InstallPart: %s installed on %s"), *PartClass->GetName(), *SocketName.ToString());
	return true;
}

bool AGuLiStrikeShip::UninstallPartLocally(FName SocketName)
{
	for (int32 Index = 0; Index < InstalledParts.Num(); ++Index)
	{
		if (InstalledParts[Index].SocketName == SocketName)
		{
			const TWeakObjectPtr<UGuLiStrikeShipPartComponent> RemovedPart = InstalledParts[Index].Part;
			// 蓝图卸载通知可以再次换装；必须先注销，不能在回调后继续使用旧数组索引。
			InstalledParts.RemoveAt(Index);
			bLoadoutDirty = true;
			if (UGuLiStrikeShipPartComponent* Part = RemovedPart.Get())
			{
				BP_OnPartUninstalled(Part, SocketName);
				// 回调可能已销毁旧件；同槽位新装的部件不属于这次卸载。
				if (RemovedPart.IsValid()) { RemovedPart->DestroyComponent(); }
			}

			RecomputeStats();
			return true;
		}
	}

	return false;
}

void AGuLiStrikeShip::NotifyPartDestroyed(UGuLiStrikeShipPartComponent* Part)
{
	if (bEditingLoadout || bEndingShipPlay) { return; }
	// 部件被外部直接销毁（未经 UninstallPart）时的兜底同步。
	for (int32 Index = 0; Index < InstalledParts.Num(); ++Index)
	{
		if (InstalledParts[Index].Part.Get() == Part)
		{
			const FName SocketName = InstalledParts[Index].SocketName;
			const FString PartName = GetNameSafe(Part);
			InstalledParts.RemoveAt(Index);
			bLoadoutDirty = true;
			// 先注销再通知，避免蓝图回调卸下另一槽位后使 Index 失效。
			BP_OnPartUninstalled(Part, SocketName);
			if (HasAuthority()) { PublishLoadout(); }
			else
			{
				AppliedLoadoutRevision = 0u;
				if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->SetAppliedLoadoutRevision(0u); }
			}

			UE_LOG(LogGuLiStrike, Log, TEXT("NotifyPartDestroyed: %s was destroyed externally, registry synced"), *PartName);
			return;
		}
	}
}

void AGuLiStrikeShip::AddStatModifier(FName Name, float MaxSpeedMultiplier, float AccelerationMultiplier)
{
	if (!HasAuthority() || !FMath::IsFinite(MaxSpeedMultiplier) || !FMath::IsFinite(AccelerationMultiplier)
		|| MaxSpeedMultiplier < 0.0f || AccelerationMultiplier < 0.0f)
	{
		return;
	}
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
	if (!HasAuthority()) { return; }
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

// 服务器先改复制状态，再主动应用到本机；C++ 属性赋值不会自动替服务器执行客户端 RepNotify。
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

// 初始复制可能早于 BeginPlay；bRuntimeStatsInitialized 防止在表与默认部件就绪前重算反馈。
void AGuLiStrikeShip::OnRep_GMRuntimeState()
{
	// Initial replicated properties may arrive before BeginPlay. Build the
	// modifier layer immediately, but defer Blueprint-visible stat callbacks
	// until BeginPlay has initialized tuning tables and default parts.
	ApplyGMRuntimeStateLocally(bRuntimeStatsInitialized);
}

// 按保留名称先删后建，重复 OnRep 不会叠加多个 GM.Runtime 修饰；bRecompute 控制初始化期间的重算。
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
	if (!HasAuthority())
	{
		if (CanUseShipControls() && (PartClass == UGuLiStrikeEnginePart::StaticClass() || PartClass == UGuLiStrikeWeaponPart::StaticClass()))
		{
			ServerRequestCycleParts(PartClass == UGuLiStrikeEnginePart::StaticClass(), LoadoutState.Revision, GetShipMovement()->GetMovementBarrierGeneration());
		}
		return;
	}
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

	const bool bWasEditing = bEditingLoadout;
	bEditingLoadout = true;
	bool bChanged = false;
	for (const FName& SocketName : SocketsToCycle)
	{
		bChanged |= CyclePartAtSocket(PartClass, SocketName);
	}
	bEditingLoadout = bWasEditing;
	if ((bChanged || bLoadoutDirty) && !bEditingLoadout) { PublishLoadout(); }
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
	return InstallPartLocally(Candidates[NextIndex], SocketName);
}

void AGuLiStrikeShip::FireInstalledWeapons()
{
	if (!CanAcceptServerIntent())
	{
		return;
	}
	// Fire 是可重写的蓝图事件；回调热换装不得使迭代器失效，也不能继续旧配置的这一轮开火。
	const TArray<FGuLiStrikeInstalledPart> PartsSnapshot = InstalledParts;
	const uint32 LoadoutRevision = LoadoutState.Revision;
	const uint32 MovementBarrier = GetShipMovement()->GetMovementBarrierGeneration();
	for (const FGuLiStrikeInstalledPart& Entry : PartsSnapshot)
	{
		if (!CanAcceptServerIntent() || LoadoutState.Revision != LoadoutRevision
			|| GetShipMovement()->GetMovementBarrierGeneration() != MovementBarrier)
		{
			break;
		}
		if (UGuLiStrikeShipPartComponent* Part = Entry.Part.Get())
		{
			if (GetPartAt(Entry.SocketName) == Part && CanExecuteServerWeapon(Part))
			{
				Part->Fire(this);
			}
		}
	}
}

void AGuLiStrikeShip::RecomputeStats()
{
	if (bEditingLoadout || !bRuntimeStatsInitialized || bEndingShipPlay)
	{
		return;
	}
	// ContributeStats 是蓝图事件，使用快照防止回调换装修改正在遍历的数组。
	const TArray<FGuLiStrikeInstalledPart> PartsSnapshot = InstalledParts;
	const uint32 LoadoutRevision = LoadoutState.Revision;
	const auto HasSameInstalledParts = [this, &PartsSnapshot]()
	{
		if (InstalledParts.Num() != PartsSnapshot.Num()) { return false; }
		for (int32 Index = 0; Index < PartsSnapshot.Num(); ++Index)
		{
			if (InstalledParts[Index].SocketName != PartsSnapshot[Index].SocketName
				|| InstalledParts[Index].Part != PartsSnapshot[Index].Part)
			{
				return false;
			}
		}
		return true;
	};
	FGuLiStrikeShipStats Stats;
	for (const FGuLiStrikeInstalledPart& Entry : PartsSnapshot)
	{
		if (UGuLiStrikeShipPartComponent* Part = Entry.Part.Get())
		{
			Part->ContributeStats(Stats);
			// 嵌套换装已经按新名册重算；外层旧快照不能覆盖较新的配置或 UI 数值。
			if (bEndingShipPlay || LoadoutState.Revision != LoadoutRevision || !HasSameInstalledParts())
			{
				return;
			}
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

	// 服务器提交一份完整配置；移动组件通过版本屏障切换，两端禁止混用旧预测与新参数。
	// 客户端计算出的 UI 数值不能越过权威配置直接改 CMC。
	if (HasAuthority())
	{
		FGuLiShipMovementConfig Config;
		Config.MaxFlySpeed = CurrentMaxSpeed;
		Config.MaxAcceleration = BaseAcceleration * SpeedMultiplier * ModifierAccelerationMultiplier;
		Config.BrakingDecelerationFlying = GetCharacterMovement()->BrakingDecelerationFlying;
		Config.BoostThrustMultiplier = BoostThrustMultiplier;
		Config.YawRate = YawRate;
		Config.YawResponseSpeed = YawResponseSpeed;
		Config.YawStopDamping = YawStopDamping;
		Config.MaxBankAngle = MaxBankAngle;
		Config.BankInterpSpeed = BankInterpSpeed;
		Config.OrientTurnSpeed = OrientTurnSpeed;
		Config.OrientMinForwardDot = OrientMinForwardDot;
		Config.bOrientToMovement = bOrientToMovement;
		if (!GetShipMovement() || !GetShipMovement()->CommitServerMovementConfig(Config, LoadoutState.Revision))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Ship movement configuration rejected: ship=%s loadout=%u; movement remains gated."),
				*GetName(), LoadoutState.Revision);
		}
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
		const FGuLiStrikeShipPartsRow* Candidate = PartDataTable->FindRow<FGuLiStrikeShipPartsRow>(RowKey, TEXT("ApplyPartRow"));
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


UGuLiShipMovementComponent* AGuLiStrikeShip::GetShipMovement() const
{
	return Cast<UGuLiShipMovementComponent>(GetCharacterMovement());
}

bool AGuLiStrikeShip::IsShipReady() const
{
	const UGuLiShipMovementComponent* Movement = GetShipMovement();
	return !bEndingShipPlay && AppliedLoadoutRevision != 0u
		&& AppliedLoadoutRevision == LoadoutState.Revision && Movement && Movement->IsMovementConfigReady();
}

bool AGuLiStrikeShip::CanUseShipControls() const
{
	return IsLocallyControlled() && IsShipReady();
}

bool AGuLiStrikeShip::CanAcceptServerIntent() const
{
	const APlayerController* OwningPC = Cast<APlayerController>(GetController());
	const AGuLiBattlePlayerState* BattlePS = OwningPC ? OwningPC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const AGuLiBattleGameState* BattleGS = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	return HasAuthority() && OwningPC && OwningPC->GetPawn() == this
		&& BattlePS && BattlePS->GetBattleRole() == EGuLiCommanderRole::Air && BattlePS->IsBattleReady()
		&& BattleGS && BattleGS->IsMatchInProgress() && IsShipReady();
}

bool AGuLiStrikeShip::CanExecuteServerWeapon(const UGuLiStrikeShipPartComponent* Part) const
{
	return CanAcceptServerIntent() && Part && Part->GetOwner() == this
		&& Part->GetAttachParent() == HullMesh && HullMesh->DoesSocketExist(Part->GetAttachSocketName())
		&& InstalledParts.ContainsByPredicate([Part](const FGuLiStrikeInstalledPart& Entry) { return Entry.Part.Get() == Part; });
}

bool AGuLiStrikeShip::GetServerPartTransform(const UGuLiStrikeShipPartComponent* Part, FTransform& OutTransform) const
{
	if (!CanExecuteServerWeapon(Part) || !GetMesh()) { return false; }
	const FTransform MeshBase(GetBaseRotationOffset(), GetBaseTranslationOffset(), GetMesh()->GetRelativeScale3D());
	OutTransform = Part->GetRelativeTransform()
		* HullMesh->GetSocketTransform(Part->GetAttachSocketName(), RTS_Component)
		* HullMesh->GetRelativeTransform() * MeshBase * GetActorTransform();
	return true;
}

bool AGuLiStrikeShip::IsKnownPartClass(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass) const
{
	return PartClass && !PartClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		&& (PartCatalogue.Contains(PartClass) || DefaultParts.ContainsByPredicate(
			[PartClass](const FGuLiStrikeShipDefaultPart& Entry) { return Entry.PartClass == PartClass; }));
}

bool AGuLiStrikeShip::InstallPart(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass, FName SocketName)
{
	if (!HasAuthority())
	{
		const int32 CatalogueIndex = PartCatalogue.IndexOfByKey(PartClass);
		if (CanUseShipControls() && CatalogueIndex != INDEX_NONE)
		{
			ServerRequestPartChange(SocketName, CatalogueIndex, LoadoutState.Revision, GetShipMovement()->GetMovementBarrierGeneration());
		}
		// 异步请求不等于装配成功；旧 bool 返回值只有服务器同步安装成功才为 true。
		return false;
	}
	if (!IsKnownPartClass(PartClass) || bEndingShipPlay) { return false; }
	const bool bWasEditing = bEditingLoadout;
	bEditingLoadout = true;
	const bool bInstalled = InstallPartLocally(PartClass, SocketName);
	bEditingLoadout = bWasEditing;
	if (bLoadoutDirty && !bEditingLoadout && bRuntimeStatsInitialized) { PublishLoadout(); }
	return bInstalled;
}

bool AGuLiStrikeShip::UninstallPart(FName SocketName)
{
	if (!HasAuthority())
	{
		if (CanUseShipControls()) { ServerRequestPartChange(SocketName, INDEX_NONE, LoadoutState.Revision, GetShipMovement()->GetMovementBarrierGeneration()); }
		return false;
	}
	if (bEndingShipPlay) { return false; }
	const bool bWasEditing = bEditingLoadout;
	bEditingLoadout = true;
	const bool bRemoved = UninstallPartLocally(SocketName);
	bEditingLoadout = bWasEditing;
	if (bLoadoutDirty && !bEditingLoadout && bRuntimeStatsInitialized) { PublishLoadout(); }
	return bRemoved;
}

void AGuLiStrikeShip::PublishLoadout()
{
	if (!HasAuthority() || bEditingLoadout || !bRuntimeStatsInitialized || bEndingShipPlay || !GetShipMovement()) { return; }
	LoadoutState.Parts.Reset();
	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		if (const UGuLiStrikeShipPartComponent* Part = Entry.Part.Get())
		{
			FGuLiStrikeShipDefaultPart& Published = LoadoutState.Parts.AddDefaulted_GetRef();
			Published.SocketName = Entry.SocketName;
			Published.PartClass = Part->GetClass();
		}
	}
	if (++LoadoutState.Revision == 0u) { ++LoadoutState.Revision; }
	bLoadoutDirty = false;
	AppliedLoadoutRevision = LoadoutState.Revision;
	GetShipMovement()->SetAppliedLoadoutRevision(AppliedLoadoutRevision);
	bServerFiring = false;
	RecomputeStats();
	ForceNetUpdate();
}

void AGuLiStrikeShip::OnRep_LoadoutState()
{
	// 复制可能早于 BeginPlay；名册留在属性中，BeginPlay 会消费相同入口。
	ApplyReplicatedLoadout();
}

void AGuLiStrikeShip::ApplyReplicatedLoadout()
{
	if (HasAuthority() || !bRuntimeStatsInitialized || bEndingShipPlay || !GetShipMovement()
		|| LoadoutState.Revision == 0u || AppliedLoadoutRevision == LoadoutState.Revision) { return; }
	GetShipMovement()->SetAppliedLoadoutRevision(0u);
	AppliedLoadoutRevision = 0u;
	bEditingLoadout = true;
	// 先注销，再销毁，防止部件 OnComponentDestroyed 回调再次修改迭代中的名册。
	const TArray<FGuLiStrikeInstalledPart> PreviousParts = InstalledParts;
	InstalledParts.Reset();
	for (const FGuLiStrikeInstalledPart& Entry : PreviousParts)
	{
		if (UGuLiStrikeShipPartComponent* Part = Entry.Part.Get())
		{
			BP_OnPartUninstalled(Part, Entry.SocketName);
			Part->DestroyComponent();
		}
	}
	bool bComplete = true;
	TSet<FName> SeenSockets;
	for (const FGuLiStrikeShipDefaultPart& Entry : LoadoutState.Parts)
	{
		if (SeenSockets.Contains(Entry.SocketName) || !IsKnownPartClass(Entry.PartClass)
			|| !InstallPartLocally(Entry.PartClass, Entry.SocketName))
		{
			bComplete = false;
			break;
		}
		SeenSockets.Add(Entry.SocketName);
	}
	bEditingLoadout = false;
	if (bComplete)
	{
		AppliedLoadoutRevision = LoadoutState.Revision;
		GetShipMovement()->SetAppliedLoadoutRevision(AppliedLoadoutRevision);
		RecomputeStats();
	}
	// 资产/组件未能完整准备时保持关闭，Tick 低频重试；不能 ACK 半套装配。
}

void AGuLiStrikeShip::ServerRequestPartChange_Implementation(FName SocketName, int32 CatalogueIndex, uint32 ExpectedRevision, uint32 ExpectedBarrier)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!CanAcceptServerIntent() || ExpectedRevision != LoadoutState.Revision || ExpectedBarrier != GetShipMovement()->GetMovementBarrierGeneration()
		|| Now - LastServerLoadoutIntentTime < 0.1 || !HullMesh || !HullMesh->DoesSocketExist(SocketName))
	{
		return;
	}
	LastServerLoadoutIntentTime = Now;
	if (CatalogueIndex == INDEX_NONE)
	{
		UninstallPart(SocketName);
	}
	else if (PartCatalogue.IsValidIndex(CatalogueIndex))
	{
		// 内部安装仍校验部件类型、真实 socket 和部件 CDO 兼容表。
		InstallPart(PartCatalogue[CatalogueIndex], SocketName);
	}
}

void AGuLiStrikeShip::ServerRequestCycleParts_Implementation(bool bEngines, uint32 ExpectedRevision, uint32 ExpectedBarrier)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!CanAcceptServerIntent() || ExpectedRevision != LoadoutState.Revision || ExpectedBarrier != GetShipMovement()->GetMovementBarrierGeneration() || Now - LastServerLoadoutIntentTime < 0.1) { return; }
	LastServerLoadoutIntentTime = Now;
	CycleParts(bEngines ? UGuLiStrikeEnginePart::StaticClass() : UGuLiStrikeWeaponPart::StaticClass());
}

void AGuLiStrikeShip::SetFiringIntent(bool bRequested)
{
	if (!GetShipMovement()) { bLocalFireHeld = false; bServerFiring = false; return; }
	if (bRequested && !CanUseShipControls()) { return; }
	if (bLocalFireHeld == bRequested) { return; }
	bLocalFireHeld = bRequested;
	if (HasAuthority())
	{
		ServerSetFiring_Implementation(bRequested, GetShipMovement()->GetMovementConfigRevision(), GetShipMovement()->GetMovementBarrierGeneration());
	}
	else
	{
		ServerSetFiring(bRequested, GetShipMovement()->GetMovementConfigRevision(), GetShipMovement()->GetMovementBarrierGeneration());
	}
}

void AGuLiStrikeShip::ServerSetFiring_Implementation(bool bRequested, uint32 ExpectedConfigRevision, uint32 ExpectedBarrier)
{
	// 停火必须能在就绪被撤销后执行；旧配置的迟到开火不能重启新配置下的武器。
	if (!bRequested)
	{
		bServerFiring = false;
		return;
	}
	bServerFiring = CanAcceptServerIntent() && ExpectedConfigRevision == GetShipMovement()->GetMovementConfigRevision()
		&& ExpectedBarrier == GetShipMovement()->GetMovementBarrierGeneration();
}

void AGuLiStrikeShip::RemoveShipInputContext()
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InstalledInputSubsystem.Get())
	{
		if (ShipMappingContext) { Subsystem->RemoveMappingContext(ShipMappingContext); }
	}
	InstalledInputSubsystem.Reset();
}

void AGuLiStrikeShip::UpdateShipInputContext()
{
	APlayerController* OwningPC = Cast<APlayerController>(GetController());
	const AGuLiBattlePlayerState* BattlePS = OwningPC ? OwningPC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const bool bOwnAirView = !bEndingShipPlay && IsLocallyControlled() && OwningPC
		&& BattlePS && BattlePS->GetBattleRole() == EGuLiCommanderRole::Air;
	UEnhancedInputLocalPlayerSubsystem* DesiredSubsystem = bOwnAirView
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(OwningPC->GetLocalPlayer()) : nullptr;
	if (InstalledInputSubsystem.Get() != DesiredSubsystem)
	{
		SetFiringIntent(false);
		if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->ClearFlightInput(); }
		RemoveShipInputContext();
		if (DesiredSubsystem && ShipMappingContext)
		{
			DesiredSubsystem->AddMappingContext(ShipMappingContext, 0);
			InstalledInputSubsystem = DesiredSubsystem;
			OwningPC->SetInputMode(FInputModeGameOnly());
			OwningPC->bShowMouseCursor = false;
			// 相机近裁剪仅在本地拥有者启用，远端副本/独立服务器不改本地渲染设置。
			if (IConsoleVariable* NearClipCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SetNearClippingPlane")))
			{
				NearClipCVar->Set(150.0f, ECVF_SetByGameSetting);
			}
		}
	}
	if (!CanUseShipControls())
	{
		SetFiringIntent(false);
		if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->ClearFlightInput(); }
	}
}

void AGuLiStrikeShip::UnPossessed()
{
	bServerFiring = false;
	bLocalFireHeld = false;
	if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->ClearFlightInput(); }
	RemoveShipInputContext();
	Super::UnPossessed();
}

void AGuLiStrikeShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingShipPlay = true;
	bServerFiring = false;
	bLocalFireHeld = false;
	if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->ClearFlightInput(); }
	RemoveShipInputContext();
	Super::EndPlay(EndPlayReason);
}
