// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeShip.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Capabilities/GuLiLogicalCooldownComponent.h"
#include "Gameplay/Ship/Private/GuLiHybridShipInput.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "Gameplay/Data/Generated/GuLiStrikeShipTableRows.h"
#include "GuLiShipMovementComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Ship/Aiming/GuLiShipAimComponent.h"
#include "Gameplay/Ship/GuLiShipCameraCollision.h"
#include "Gameplay/Ship/UI/GuLiShipWorldHUDComponent.h"
#include "Gameplay/Ship/GuLiShipTargetingRangeComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "Battle/Combat/GuLiWingmanWeaponPolicy.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Gameplay/Data/GuLiObjectScale.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/PlayerController.h"
#include "GuLiStrikeShipPartComponent.h"
#include "GuLiStrikeShipTableRows.h"
#include "GuLiStrikeEnginePart.h"
#include "GuLiStrikeWeaponPart.h"
#include "GuLiStrike.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanTargetAcquisition.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
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
#include "EngineUtils.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

namespace GuLiStrikeShipPrivate
{
	const FName GMRuntimeModifierName(TEXT("GM.Runtime"));

	uint32 AllocateShipGeneration()
	{
		static uint32 NextGeneration = 1u;
		const uint32 Result = NextGeneration;
		NextGeneration = NextGeneration == MAX_uint32 ? 1u : NextGeneration + 1u;
		return FMath::Max(1u, Result);
	}
}

AGuLiStrikeShip::AGuLiStrikeShip(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGuLiShipMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	bReplicates = false;
	// 固定 5v5 的玩家载具始终相关；远距飞船相机不能把近旁玩家按默认 150m 距离剔除。
	bAlwaysRelevant = true;

	ShipAssembly = CreateDefaultSubobject<UGuLiShipAssemblyComponent>(TEXT("ShipAssembly"));
    CreateDefaultSubobject<UGuLiLogicalCooldownComponent>(TEXT("LogicalCooldowns"));
	CombatHealth = CreateDefaultSubobject<UGuLiCombatHealthComponent>(TEXT("CombatHealth"));
	CreateDefaultSubobject<UGuLiTeamOutlineComponent>(TEXT("TeamOutline"));
	auto* ExternalControl=CreateDefaultSubobject<UGuLiExternalUnitControlComponent>(TEXT("ExternalUnitControl"));
	ExternalControl->bPlayerVehicle=true;ExternalControl->bAirborne=true;
	ShipAim = CreateDefaultSubobject<UGuLiShipAimComponent>(TEXT("ShipAim"));
	ShipWorldHUD = CreateDefaultSubobject<UGuLiShipWorldHUDComponent>(TEXT("ShipWorldHUD"));
	WingmanTargetingRange = CreateDefaultSubobject<UGuLiShipTargetingRangeComponent>(TEXT("WingmanTargetingRange"));
	WingmanTargetingRange->SetupAttachment(GetRootComponent());

	// Keep the active Wingman ability usable even when an existing Ship Blueprint has not yet
	// overridden the newly introduced property. The action is project-owned and mapped to RMB
	// in IMC_Ship by the idempotent editor deployment script.
	static ConstructorHelpers::FObjectFinder<UInputAction> WingmanMissileInputFinder(
		TEXT("/Game/GuLiStrike/Input/Actions/IA_Ship_WingmanMissile.IA_Ship_WingmanMissile"));
	if (WingmanMissileInputFinder.Succeeded())
	{
		WingmanMissileAction = WingmanMissileInputFinder.Object;
	}

	// 飞船自己掌控完整的三维姿态
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 创建舰体网格体；其上的 socket 是部件挂点
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull Mesh"));
	// 静态舰体跟随 CharacterMesh0 的网络平滑偏移；胶囊仍是服务器碰撞与预测根。
	HullMesh->SetupAttachment(GetMesh());
	// Scale the common model parent once: hidden socket hull, visible hull and parts inherit it.
	GetMesh()->SetRelativeScale3D(FVector(0.2f));
	// 舰体只参与查询不产生阻挡：相机避障扫掠需要能命中自身舰体
	// （对象类型查询不看响应矩阵，Ignore 所有通道也不妨碍被扫到）
	HullMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HullMesh->SetCollisionObjectType(ECC_WorldStatic);
	HullMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	HullMesh->SetRelativeLocation(HullMeshOffset);

	// 创建弹簧臂（追尾相机位于舰体后上方）
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArm->SetupAttachment(GetRootComponent());

	SpringArm->TargetArmLength = CameraDefaultArmLength;
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
	GetCharacterMovement()->BrakingDecelerationFlying = 12.0f;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 0.0f, 0.0f);
}





void AGuLiStrikeShip::BeginPlay()
{
	Super::BeginPlay();
	TInlineComponentArray<UMeshComponent*> AuthoredMeshes(this);
	for (UMeshComponent* AuthoredMesh : AuthoredMeshes) GuLiObjectScale::ApplyOutlineScale(AuthoredMesh);
	InitializeShipCapabilities();
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

	// 滚轮期望臂长从蓝图配置的臂长起步；实际臂长由 Tick 的舰体/地形避障统一结算
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
	InitializeShipCapabilities();
	if (HasAuthority())
	{
		PublishLoadout();
	}
	else
	{
		ApplyReplicatedLoadout();
	}
	UpdateShipInputContext();
	if (ShipAim)
	{
		ShipAim->HandleOwnerControllerChanged();
	}
	if (ShipWorldHUD)
	{
		ShipWorldHUD->HandleOwnerControllerChanged();
	}
}

void AGuLiStrikeShip::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	InitializeShipCapabilities();
	if (HangarCapability)
	{
		HangarCapability->SetActiveAbilityInputEnabled(GetController() != nullptr);
	}
	if (UGuLiShipMovementComponent* Movement = GetShipMovement())
	{
		Movement->ClearFlightInput();
		// 当帧短暂解除并重新占有也必须推进身份屏障，不能等 Tick 才发现连接变化。
		Movement->SetAppliedLoadoutRevision(AppliedLoadoutRevision);
	}
	UpdateShipInputContext();
	if (ShipAim)
	{
		ShipAim->HandleOwnerControllerChanged();
	}
	if (ShipWorldHUD)
	{
		ShipWorldHUD->HandleOwnerControllerChanged();
	}
}
void AGuLiStrikeShip::SetCarrierDormant(bool Dormant)
{
    if (!HasAuthority()) return;
    bCarrierDormant=Dormant; OnRep_CarrierDormant();
}
void AGuLiStrikeShip::OnRep_CarrierDormant()
{
    auto* Movement=GetShipMovement(); if (!Movement) return;
    if (bCarrierDormant)
    {
        HybridInput={}; bSemanticFlightInput=false; ConsumeMovementInputVector();
        Movement->FreezeCarrierMotion();
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
		// 屏障期间清空输入，按住的键在新配置就绪后重新采样；不会产生逐帧移动之外的 RPC。
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::BoostStart);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Completed, this, &AGuLiStrikeShip::BoostEnd);
		if (WingmanMissileAction)
		{
			EnhancedInputComponent->BindAction(WingmanMissileAction, ETriggerEvent::Started,
				this, &AGuLiStrikeShip::WingmanMissilePressed);
			EnhancedInputComponent->BindAction(WingmanMissileAction, ETriggerEvent::Completed,
				this, &AGuLiStrikeShip::WingmanMissileReleased);
			EnhancedInputComponent->BindAction(WingmanMissileAction, ETriggerEvent::Canceled,
				this, &AGuLiStrikeShip::WingmanMissileReleased);
		}
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
	if (ShipAim && ShipAim->ConsumeLookInput(InputVector))
	{
		return;
	}

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
	// 滚轮上滚（+1）= 拉近；只改期望臂长，实际臂长由 Tick 的舰体/地形避障统一结算
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
	PumpHybridFlightInput();
		if (UGuLiNetworkSettings::CanPresent(GetWorld()) && IsLocallyControlled()) ResolveCameraArmCollision();
		return;
}

void AGuLiStrikeShip::ResolveCameraArmCollision()
{
	// SpringArm 自带探测会忽略 Owner，无法同时满足“不要钻进巨舰自身”与“不要穿过
	// Landscape”，因此这里仍是唯一臂长写者：先把埋进自身舰体的端点向外推出，再从
	// 轨道中心到候选机位连续球扫掠。后一步会在山体位于飞船与相机之间时主动收臂。
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

	// 端点若位于自身舰体内，必须从舰外向内扫到外表面；从内部向外扫不会稳定返回命中。
	bool bInsideOwnHull = false;
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, DesiredLocation, FQuat::Identity, ObjectQuery, ProbeShape, Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Overlap.GetActor() == this)
		{
			bInsideOwnHull = true;
			break;
		}
	}

	auto SweepFirstMatching = [this, &ObjectQuery, &ProbeShape](
		const FVector& Start,
		const FVector& End,
		const bool bIgnoreOwningShip,
		const bool bMatchOwningShip,
		FHitResult& OutHit) -> bool
	{
		FCollisionQueryParams SweepParams(TEXT("ShipCameraCorridor"), false,
			bIgnoreOwningShip ? this : nullptr);
		SweepParams.bFindInitialOverlaps = true;
		// Object queries can stop at an unrelated WorldStatic/WorldDynamic blocker. Retry while
		// explicitly ignoring each rejected actor so a Landscape behind decorations is still found.
		for (int32 Attempt = 0; Attempt < 32; ++Attempt)
		{
			FHitResult Hit;
			if (!GetWorld()->SweepSingleByObjectType(
				Hit, Start, End, FQuat::Identity, ObjectQuery, ProbeShape, SweepParams))
			{
				return false;
			}
			const AActor* HitOwner = Hit.GetActor();
			const bool bMatches = bMatchOwningShip
				? HitOwner == this
				: IsCameraRelevantOwner(HitOwner);
			if (bMatches)
			{
				OutHit = Hit;
				return true;
			}
			if (HitOwner)
			{
				SweepParams.AddIgnoredActor(HitOwner);
			}
			else if (const UPrimitiveComponent* HitComponent = Hit.GetComponent())
			{
				SweepParams.AddIgnoredComponent(HitComponent);
			}
			else
			{
				return false;
			}
		}
		return false;
	};

	float NewArm = DesiredArmLength;
	if (bInsideOwnHull)
	{
		const float OutsideArm = FMath::Max(CameraZoomMax, HullBoundingRadius)
			+ CameraCollisionProbeRadius * 2.0f;
		FHitResult HullHit;
		if (SweepFirstMatching(
			OrbitPivot + ArmDirection * OutsideArm,
			DesiredLocation,
			false,
			true,
			HullHit))
		{
			NewArm = FMath::Max(NewArm, OutsideArm - HullHit.Distance);
		}
	}

	// Unlike the former endpoint-only gate, this tests the entire view corridor. The owning
	// hull is ignored here because the orbit starts inside it; Landscapes and other Ships remain.
	FHitResult CorridorHit;
	if (SweepFirstMatching(
		OrbitPivot,
		OrbitPivot + ArmDirection * NewArm,
		true,
		false,
		CorridorHit))
	{
		NewArm = GuLiShipCameraCollision::ConstrainArmToBlockingDistance(
			NewArm, CorridorHit.Distance, CameraCollisionMinArm);
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

bool AGuLiStrikeShip::SubmitGameplayWeaponInput(const FGuLiWeaponBindingKey& Binding,bool Pressed)
{
    if (!HangarCapability || !Binding.IsWellFormed()) return false;
    if (!Pressed) {HangarCapability->AbilityWeaponBindingReleased(Binding);return true;}
    return CanUseShipControls() && GroupAbilityConfig.FindWeaponChannel(Binding)
        && HangarCapability->AbilityWeaponBindingPressed(Binding);
}

void AGuLiStrikeShip::WingmanMissilePressed(const FInputActionValue& Value)
{
	if (CanUseShipControls() && HangarCapability)
	{
		const FGuLiWingmanWeaponChannelConfig* Channel =
			GroupAbilityConfig.FindFirstWeaponChannel(EGuLiWingmanWeaponKind::Missile);
        if (Channel && SubmitGameplayWeaponInput(Channel->Binding,true))
		{
			ActiveMissileInputBinding = Channel->Binding;
		}
	}
}

void AGuLiStrikeShip::WingmanMissileReleased(const FInputActionValue& Value)
{
	if (HangarCapability)
	{
        SubmitGameplayWeaponInput(ActiveMissileInputBinding,false);
		ActiveMissileInputBinding = FGuLiWeaponBindingKey{};
	}
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
	if (!IsValid(Part) || !Part->IsRegistered() || !Part->IsVisualMeshReady())
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
	ShipAssembly->NotifyPartDestroyed(Part);
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
	Part->SetModelId(Row->ModelId);
	Part->PartDisplayName = FText::FromString(RowName.ToString());

	// 类型专属数值：行里的值写到对应子类实例上
	if (UGuLiStrikeEnginePart* Engine = Cast<UGuLiStrikeEnginePart>(Part))
	{
		Engine->Thrust = Row->Thrust;
	}

	if (UGuLiStrikeWeaponPart* Weapon = Cast<UGuLiStrikeWeaponPart>(Part))
	{
		Weapon->MuzzleOffset = Row->Muzzle;
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
	HullModelId = Row->HullModelId;
	if (auto* Model = GuLiModels::Load<UStaticMesh>(this, HullModelId)) HullMesh->SetStaticMesh(Model);
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

bool AGuLiStrikeShip::SubmitGameplayFlightInput(FVector LocalThrust,float Turn,bool bBoost)
{
	if (!IsLocallyControlled() || LocalThrust.ContainsNaN() || LocalThrust.GetAbsMax()>1.00001 || !FMath::IsFinite(Turn) || FMath::Abs(Turn)>1.00001)
		return false;
	if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
	{
		const auto* Binding=UGuLiHybridActorComponent::Find(this);
		if (!Binding || !Binding->CanControl()) return false;
		HybridInput.Thrust=LocalThrust; HybridInput.Turn=Turn; HybridInput.Strafe=LocalThrust.Y; HybridInput.bBoost=bBoost;
		bSemanticFlightInput=true; return true;
	}
	if (!CanUseShipControls()) return false;
	auto* Movement=GetShipMovement(); Movement->AddThrustInput(LocalThrust); Movement->AddTurnInput(Turn);
	Movement->AddStrafeInput(LocalThrust.Y); Movement->SetBoostInput(bBoost); return true;
}

void AGuLiStrikeShip::PumpHybridFlightInput()
{
	const auto* Binding=UGuLiHybridActorComponent::Find(this); auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	if (!Binding || !Hybrid || !Binding->CanControl() || !IsLocallyControlled()) return;
	if (!bSemanticFlightInput)
	{
		const auto Input=GetShipMovement()->ConsumeHybridControllerInput();
		HybridInput.Thrust=Input.LocalThrust; HybridInput.Turn=Input.Turn; HybridInput.Strafe=Input.Strafe; HybridInput.bBoost=Input.bBoost;
	}
	const uint64 Tick=Hybrid->GetLogicalTick();
	if (bHybridInputSent && Tick==LastHybridInputTick) return;
	if (bHybridInputSent && HybridInput==LastHybridInput && Tick<LastHybridInputTick+30) return;
	FGuLiHybridObjectState State; if (!Hybrid->GetObject(Binding->GetObjectKey(),State)) return;
	FGuLiHybridCommand Command; Command.Event=FGuid::NewGuid(); Command.LogicalTick=Tick; Command.Operation=TEXT("ShipFlightInput");
	FGuLiHybridStateArchive Archive(128); auto Input=HybridInput; GuLiHybridShip::Input(Archive,Input);
	if (!Archive.Finish(Command.Arguments)) return;
	Command.Members.Add({State.Key,State.ControlEpoch,State.OwnerEpoch,0,State.Location});
	if (Hybrid->RouteCommand(Command)) { LastHybridInput=HybridInput; LastHybridInputTick=Tick; bHybridInputSent=true; }
}

void AGuLiStrikeShip::AdvanceHybridFlight(float DeltaSeconds,const FGuLiHybridShipInput& Input)
{
	if (!UGuLiHybridActorComponent::HasSimulation(this)) return;
	GetShipMovement()->AdvanceHybridFlight(DeltaSeconds,Input);
	RefreshServerCombatRegistration();
}

bool AGuLiStrikeShip::IsShipReady() const
{
	const UGuLiShipMovementComponent* Movement = GetShipMovement();
	return !bEndingShipPlay && AppliedLoadoutRevision != 0u
		&& AppliedLoadoutRevision == LoadoutState.Revision && Movement
        && (bCarrierDormant ? Movement->GetMovementConfigRevision()!=0u && Movement->GetEffectiveMovementConfig().IsValid()
            && Movement->GetAppliedLoadoutRevision()==AppliedLoadoutRevision : Movement->IsMovementConfigReady());
}

bool AGuLiStrikeShip::CanUseShipControls() const
{
	if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
	{
		const auto* Binding=UGuLiHybridActorComponent::Find(this);
		return Binding && Binding->CanControl() && IsLocallyControlled() && IsShipReady() && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(this);
	}
	return IsLocallyControlled() && IsShipReady() && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(this);
}

bool AGuLiStrikeShip::CanAcceptServerIntent() const
{
	if (UGuLiNetworkSettings::IsHybrid(GetWorld())) return UGuLiHybridActorComponent::HasSimulation(this)
		&& IsShipReady() && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(this);
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)) { return false; }
	const APlayerController* OwningPC = Cast<APlayerController>(GetController());
	const AGuLiBattlePlayerState* BattlePS = OwningPC ? OwningPC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const AGuLiBattleGameState* BattleGS = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	return HasAuthority() && OwningPC && OwningPC->GetPawn() == this
		&& BattlePS && BattlePS->GetBattleRole() == EGuLiCommanderRole::Air && BattlePS->IsBattleReady()
		&& BattleGS && BattleGS->IsMatchInProgress() && IsShipReady();
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
	if (!IsKnownPartClass(PartClass) || bEndingShipPlay || ShipAssembly->OwnsSocket(SocketName)) { return false; }
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
	if (bEndingShipPlay || ShipAssembly->OwnsSocket(SocketName)) { return false; }
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
	LoadoutState.BuildRevision = ShipAssembly->GetAppliedBuildRevision();
	LoadoutState.Parts.Reset();
	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		if (const UGuLiStrikeShipPartComponent* Part = Entry.Part.Get(); Part && !ShipAssembly->OwnsPart(Part))
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
	RecomputeStats();

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
	if (LoadoutState.BuildRevision != ShipAssembly->GetAppliedBuildRevision()) { GetShipMovement()->SetAppliedLoadoutRevision(0u); AppliedLoadoutRevision = 0u; return; }
	GetShipMovement()->SetAppliedLoadoutRevision(0u);
	AppliedLoadoutRevision = 0u;
	bEditingLoadout = true;
	// 先注销，再销毁，防止部件 OnComponentDestroyed 回调再次修改迭代中的名册。
	const TArray<FGuLiStrikeInstalledPart> PreviousParts = InstalledParts;
	InstalledParts.RemoveAll([this](const auto& Entry) { return !ShipAssembly->OwnsPart(Entry.Part.Get()); });
	for (const FGuLiStrikeInstalledPart& Entry : PreviousParts)
	{
		if (UGuLiStrikeShipPartComponent* Part = Entry.Part.Get(); Part && !ShipAssembly->OwnsPart(Part))
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

void AGuLiStrikeShip::ServerRequestPartChange(FName SocketName, int32 CatalogueIndex, uint32 ExpectedRevision, uint32 ExpectedBarrier)
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

void AGuLiStrikeShip::ServerRequestCycleParts(bool bEngines, uint32 ExpectedRevision, uint32 ExpectedBarrier)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!CanAcceptServerIntent() || ExpectedRevision != LoadoutState.Revision || ExpectedBarrier != GetShipMovement()->GetMovementBarrierGeneration() || Now - LastServerLoadoutIntentTime < 0.1) { return; }
	LastServerLoadoutIntentTime = Now;
	CycleParts(bEngines ? UGuLiStrikeEnginePart::StaticClass() : UGuLiStrikeWeaponPart::StaticClass());
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
		if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->ClearFlightInput(); }
	}
}

void AGuLiStrikeShip::InitializeShipCapabilities()
{
	if (!HasAuthority() || bEndingShipPlay) return;
	if (!ShipInstanceId.IsValid())
	{
		if (const auto* Binding=UGuLiHybridActorComponent::Find(this))
		{ ShipInstanceId=Binding->GetObjectKey().StableId; ShipGeneration=Binding->GetObjectKey().LifeGeneration; }
		else { ShipInstanceId=FGuid::NewGuid(); ShipGeneration=GuLiStrikeShipPrivate::AllocateShipGeneration(); }
	}
	CombatHealth->OnDeath.AddUniqueDynamic(this, &AGuLiStrikeShip::HandleShipDeath);
	if (!bRuntimeStatsInitialized) return;
	if (auto* State = GetPlayerState<AGuLiBattlePlayerState>(); State && ShipAssembly->Catalog)
	{
		FString Error;
		if (!State->GetShipBuild()->RestoreShip(*ShipAssembly, Error))
			UE_LOG(LogGuLiStrike, Warning, TEXT("Ship build restoration deferred: %s"), *Error);
	}
	RefreshServerCombatRegistration();
}

void AGuLiStrikeShip::HandleCapabilityStateChanged(UGuLiShipCapabilityComponent* Capability, EGuLiShipCapabilityState State)
{
	if (auto* Hangar = Cast<UGuLiShipHangarCapabilityComponent>(Capability))
	{
		if (State == EGuLiShipCapabilityState::Enabled) BindHangarCapability(Hangar);
		else if (HangarCapability == Hangar) UnbindHangarCapability(Hangar);
	}
}

void AGuLiStrikeShip::BindHangarCapability(UGuLiShipHangarCapabilityComponent* Capability)
{
	check(!HangarCapability || HangarCapability == Capability);
	HangarCapability = Capability;
	HangarCapability->InitializeShipActorInfo(this);
	HangarCapability->OnProjectionChanged().AddUObject(this, &AGuLiStrikeShip::HandleGroupAbilityProjectionChanged);
	HangarCapability->OnWeaponAbilityAuthorized().AddUObject(this, &AGuLiStrikeShip::HandleTriggeredShipWeaponAbility);
	HangarCapability->SetActiveAbilityInputEnabled(GetController() != nullptr);
	{
		const auto* Binding=UGuLiHybridActorComponent::Find(this);
		if (!Binding) return;
		auto Context=HangarCapability->GetProjectionContext();
		if (!Context.IsWellFormed())
		{
			Context.ShipInstanceId=Binding->GetObjectKey().StableId;Context.ShipGeneration=Binding->GetObjectKey().LifeGeneration;
			Context.Team=Binding->GetState().Team;Context.OwnerPlayerGuid=Binding->GetState().Controller;
			Context.WingmanTypeId=HangarCapability->GetAppliedAbilitySet()->WingmanTypeId;
			uint64 Hash=GuLiShipAbilityHash::OffsetBasis;
			GuLiShipAbilityHash::AddString(Hash,Context.ShipInstanceId.ToString());
			GuLiShipAbilityHash::AddUInt32(Hash,Context.ShipGeneration);
			GuLiShipAbilityHash::AddString(Hash,HangarCapability->GetSourceGroupId().ToString().ToLower());
			GuLiShipAbilityHash::AddString(Hash,HangarCapability->GetCapabilityId().ToString().ToLower());
			Context.GroupGeneration=uint32(Hash)^uint32(Hash>>32);if (!Context.GroupGeneration) Context.GroupGeneration=1;
			Context.FormationCommandRevision=Context.EffectiveClientSimTick=1;
		}
		Context.MatchEpoch=GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
		GroupGeneration=Context.GroupGeneration;HangarCapability->SetProjectionContext(Context);PublishGroupAbilityConfig();
	}
	if (ShipAim) ShipAim->HandleOwnerControllerChanged();
}

void AGuLiStrikeShip::UnbindHangarCapability(UGuLiShipHangarCapabilityComponent* Capability)
{
	check(HangarCapability == Capability);
	PublishGroupAbilityConfig();

	HangarCapability->OnProjectionChanged().RemoveAll(this);
	HangarCapability->OnWeaponAbilityAuthorized().RemoveAll(this);
	HangarCapability = nullptr;
	if (ShipAim) ShipAim->HandleOwnerControllerChanged();
}

void AGuLiStrikeShip::HandleGroupAbilityProjectionChanged()
{
	PublishGroupAbilityConfig();
}

void AGuLiStrikeShip::PublishGroupAbilityConfig()
{
	if (!HasAuthority() || !HangarCapability)
	{
		return;
	}

	FGuLiGroupAbilityConfigSnapshot NewSnapshot;
	if (!HangarCapability->BuildGroupAbilityConfigSnapshot(NewSnapshot)
		|| GroupAbilityConfig.HasSameVersion(NewSnapshot))
	{
		return;
	}

	GroupAbilityConfig = MoveTemp(NewSnapshot);

	// RepNotify is not invoked on authority. Listen/standalone owner simulation
	// consumes the same immutable payload through the same path.
	OnRep_GroupAbilityConfig();
	}

void AGuLiStrikeShip::OnRep_GroupAbilityConfig()
{
	if (!GroupAbilityConfig.IsWellFormed())
	{
		return;
	}
	if (HangarCapability)
	{
		HangarCapability->ObserveReplicatedGroupAbilityConfig(GroupAbilityConfig);
	}

	if (UGuLiWingmanSimulationSubsystem* Simulation =
		GetWorld() ? GetWorld()->GetSubsystem<UGuLiWingmanSimulationSubsystem>() : nullptr)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = GroupAbilityConfig.ShipInstanceId;
		Group.ShipGeneration = GroupAbilityConfig.ShipGeneration;
		Group.GroupGeneration = GroupAbilityConfig.GroupGeneration;
		if (!GroupAbilityConfig.bGroupAbilitiesValid)
		{
			Simulation->DestroyOwnedGroup(Group);
		}
	}
}

void AGuLiStrikeShip::RefreshServerCombatRegistration()
{
	const auto* Binding=UGuLiHybridActorComponent::Find(this);
		if (!Binding || !CombatHealth) return;
		FGuLiTargetHandle Target; Target.Kind=EGuLiTargetKind::Ship; Target.AuthorityId=Binding->GetObjectKey().StableId;
		Target.Generation=Binding->GetObjectKey().LifeGeneration;
		GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->BindTarget(Target,Binding->GetObjectKey());
		return;
}



bool AGuLiStrikeShip::BuildLocalWingmanMissileAim(
	FVector& OutAimOrigin,
	FVector& OutAimForward) const
{
	OutAimOrigin = FVector::ZeroVector;
	OutAimForward = FVector::ZeroVector;
	if (!IsLocallyControlled())
	{
		return false;
	}
	if (Camera)
	{
		OutAimOrigin = Camera->GetComponentLocation();
		OutAimForward = Camera->GetForwardVector().GetSafeNormal();
	}
	else if (SpringArm)
	{
		OutAimOrigin = SpringArm->GetSocketLocation(USpringArmComponent::SocketName);
		OutAimForward = SpringArm->GetForwardVector().GetSafeNormal();
	}
	return !OutAimOrigin.ContainsNaN() && !OutAimForward.IsNearlyZero();
}

bool AGuLiStrikeShip::SelectLocalPredictedWingmanTarget(
	const FVector& AimOrigin,
	const FVector& AimForward,
	const FGuLiWeaponBindingKey& WeaponBinding,
	FGuLiTargetHandle& OutTarget) const
{
	OutTarget = FGuLiTargetHandle{};
	const FGuLiWingmanWeaponChannelConfig* WeaponChannel =
		GroupAbilityConfig.FindWeaponChannel(WeaponBinding);
	if (!GetWorld() || !CombatHealth || !GroupAbilityConfig.IsUsableByLeaseOwner()
		|| !WeaponChannel || !WeaponChannel->bEnabled
		|| WeaponChannel->Kind != EGuLiWingmanWeaponKind::Missile
		|| !WeaponChannel->Runtime.IsWellFormed())
	{
		return false;
	}

	TArray<FGuLiWingmanTargetObservation> Observations;
	TSet<FGuLiTargetHandle> AddedTargets;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		const UGuLiCombatHealthComponent* Health = Actor
			? Actor->FindComponentByClass<UGuLiCombatHealthComponent>() : nullptr;
		if (!Health || !Health->GetIsReplicated() || !Health->IsAlive()
			|| Health->GetCombatTeam() == EGuLiTeam::Unassigned
			|| Health->GetTargetHandle().Kind != EGuLiTargetKind::Ship
			|| !Health->GetTargetHandle().IsValid()
			|| AddedTargets.Contains(Health->GetTargetHandle()))
		{
			continue;
		}
		FGuLiWingmanTargetObservation& Observation = Observations.AddDefaulted_GetRef();
		Observation.Target = Health->GetTargetHandle();
		Observation.Team = Health->GetCombatTeam();
		Observation.Location = Actor->GetActorLocation();
		Observation.CollisionActor = Actor;
		Observation.Source = EGuLiWingmanTargetObservationSource::ReplicatedShip;
		Observation.bAlive = true;
		Observation.bFromAcceptedOrReliableState = !Observation.Location.ContainsNaN();
		if (Observation.bFromAcceptedOrReliableState)
		{
			AddedTargets.Add(Observation.Target);
		}
		else
		{
			Observations.Pop(EAllowShrinking::No);
		}
	}

	const AGuLiBattleGameState* BattleGameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	const uint32 MatchEpoch = BattleGameState ? BattleGameState->GetMatchEpoch() : 0u;
	const AGuLiSoldierStateReplicator* SoldierStates = nullptr;
	const AGuLiCommanderPresentationActor* CommanderPresentation = nullptr;
	for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It)
	{
		if (It->GetSnapshotMatchEpoch() == MatchEpoch)
		{
			SoldierStates = *It;
			break;
		}
	}
	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
	{
		CommanderPresentation = *It;
		break;
	}
	if (SoldierStates && CommanderPresentation)
	{
		FGuLiWingmanTargetAcquisition::AppendCommanderObservations(
			SoldierStates->GetItems(),
			SoldierStates->GetSnapshotMatchEpoch(),
			MatchEpoch,
			[CommanderPresentation](const FGuLiSoldierId SoldierId, FTransform& OutTransform)
			{
				return CommanderPresentation->TryGetAuthoritativeSoldierTransform(
					SoldierId, OutTransform);
			},
			Observations);
	}

	AGuLiWingmanPresentationActor* WingmanPresentation = nullptr;
	for (TActorIterator<AGuLiWingmanPresentationActor> It(GetWorld()); It; ++It)
	{
		WingmanPresentation = *It;
		break;
	}
	if (BattleGameState && WingmanPresentation)
	{
		const TArray<FGuLiCommanderRoleSlotState> RoleSlots = BattleGameState->GetRoleSlots();
		auto ResolveTeam = [&RoleSlots](const FGuid& PlayerGuid)
		{
			for (const FGuLiCommanderRoleSlotState& Slot : RoleSlots)
			{
				if (Slot.bOccupied && Slot.PlayerGuid == PlayerGuid)
				{
					return Slot.Team;
				}
			}
			return EGuLiTeam::Unassigned;
		};
		TArray<FGuLiWingmanAcceptedTargetPose> AcceptedWingmen;
		WingmanPresentation->GetFreshAcceptedTargetPoses(
			BattleGameState->GetServerWorldTimeSeconds(),
			FGuLiWingmanTargetAcquisition::MaximumAcceptedPoseAgeSeconds,
			AcceptedWingmen);
		for (const FGuLiWingmanAcceptedTargetPose& Pose : AcceptedWingmen)
		{
			const FGuLiTargetHandle Target = GuLiCombatTargets::MakeWingmanTargetHandle(Pose.Wingman);
			const EGuLiTeam Team = ResolveTeam(Pose.LeaseOwnerPlayerGuid);
			if (!Target.IsValid() || Team == EGuLiTeam::Unassigned || AddedTargets.Contains(Target))
			{
				continue;
			}
			FGuLiWingmanTargetObservation& Observation = Observations.AddDefaulted_GetRef();
			Observation.Target = Target;
			Observation.Team = Team;
			Observation.Location = Pose.Transform.GetLocation();
			Observation.Source = EGuLiWingmanTargetObservationSource::WingmanAcceptedPose;
			Observation.bAlive = true;
			Observation.bFromAcceptedOrReliableState = true;
			AddedTargets.Add(Target);
		}
	}

	FGuLiWingmanTargetObservation Selected;
	const bool bSelected = FGuLiWingmanTargetAcquisition::SelectBestTarget(
		AimOrigin,
		AimForward,
		CombatHealth->GetCombatTeam(),
		WeaponChannel->Runtime,
		Observations,
		[this, &AimOrigin](const FGuLiWingmanTargetObservation& Target)
		{
			FCollisionQueryParams QueryParams(
				SCENE_QUERY_STAT(GuLiShipPredictedMissileLock), false, this);
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(
				Hit, AimOrigin, Target.Location, ECC_Visibility, QueryParams))
			{
				return true;
			}
			return Target.CollisionActor.IsValid()
				&& Hit.GetActor() == Target.CollisionActor.Get();
		},
		Selected);
	if (bSelected)
	{
		OutTarget = Selected.Target;
	}
	return bSelected;
}

void AGuLiStrikeShip::HandleTriggeredShipWeaponAbility(
	const FGuLiWeaponBindingKey WeaponBinding,
	const FName SkillId,
	const FGameplayTag CatalogAbilityId,
	const uint32 AbilitySetRevision,
	const bool bLocallyPredicted)
{
	const FGuLiWingmanWeaponChannelConfig* Channel =
		GroupAbilityConfig.FindWeaponChannel(WeaponBinding);
	if (!Channel || !Channel->bEnabled
		|| Channel->Kind != EGuLiWingmanWeaponKind::Missile
		|| Channel->SkillId != SkillId || Channel->AbilityId != CatalogAbilityId)
	{
		return;
	}
	// Only the owning input path emits the authoritative, quantized command.
	if (!IsLocallyControlled() || (!HasAuthority() && !bLocallyPredicted))
	{
		return;
	}
	FVector AimOrigin;
	FVector AimForward;
	FIntVector AimDirectionMilli;
	if (!BuildLocalWingmanMissileAim(AimOrigin, AimForward)
		|| !GuLiWingmanMissileAim::Quantize(AimForward, AimDirectionMilli))
	{
		return;
	}

	const FGuid RequestId = FGuid::NewGuid();
	FGuLiTargetHandle PredictedTarget;
	const bool bPredictedLocked = SelectLocalPredictedWingmanTarget(
		AimOrigin, AimForward, WeaponBinding, PredictedTarget);
	OnWingmanMissileLockPredicted.Broadcast(
		RequestId, bPredictedLocked, PredictedTarget, AimOrigin, AimForward);

	const uint32 DefinitionRevision = Channel->DefinitionRevision;
	if (HasAuthority())
	{
		ExecuteServerWingmanMissileSalvo(
			RequestId, AimDirectionMilli, WeaponBinding, SkillId, AbilitySetRevision,
			GroupAbilityConfig.LoadoutRevision, Channel->ProfileRevision, DefinitionRevision);
	}
	else
	{
		ServerRequestWingmanMissileSalvo(
			RequestId, AimDirectionMilli, WeaponBinding, SkillId, AbilitySetRevision,
			GroupAbilityConfig.LoadoutRevision, Channel->ProfileRevision, DefinitionRevision);
	}
}

void AGuLiStrikeShip::ServerRequestWingmanMissileSalvo(
	const FGuid RequestId,
	const FIntVector AimDirectionMilli,
	const FGuLiWeaponBindingKey WeaponBinding,
	const FName SkillId,
	const uint32 AbilitySetRevision,
	const uint32 LoadoutRevision,
	const uint32 ProfileRevision,
	const uint32 MissileDefinitionRevision)
{
	ExecuteServerWingmanMissileSalvo(
		RequestId, AimDirectionMilli, WeaponBinding, SkillId, AbilitySetRevision,
		LoadoutRevision, ProfileRevision, MissileDefinitionRevision);
}

void AGuLiStrikeShip::ExecuteServerWingmanMissileSalvo(
	const FGuid& RequestId,
	const FIntVector& AimDirectionMilli,
	const FGuLiWeaponBindingKey& WeaponBinding,
	const FName SkillId,
	const uint32 AbilitySetRevision,
	const uint32 LoadoutRevision,
	const uint32 ProfileRevision,
	const uint32 MissileDefinitionRevision)
{
	FGuLiWingmanMissileSalvoResult Result;
	if (const FGuLiWingmanMissileSalvoResult* Previous =
		WingmanMissileRequestResults.Find(RequestId))
	{
		Result = *Previous;
		Result.RejectReason = EGuLiWingmanRejectReason::Duplicate;
		Result.LaunchedCount = 0;
		Result.bSharedCooldownStarted = false;
		BroadcastWingmanMissileResult(RequestId, Result);
		return;
	}
    FGuLiWingmanMissileSalvoRequest Request;Request.ActivationId=RequestId;Request.Binding=WeaponBinding;Request.SkillId=SkillId;
        Request.AbilitySetRevision=AbilitySetRevision;Request.LoadoutRevision=LoadoutRevision;Request.ProfileRevision=ProfileRevision;
        Request.MissileDefinitionRevision=MissileDefinitionRevision;Request.AimDirectionMilli=AimDirectionMilli;
        if (const auto* Channel=GroupAbilityConfig.FindWeaponChannel(WeaponBinding)) Request.MissileAbilityId=Channel->AbilityId;
        Result=ActivateHybridMissileSalvo(Request);RememberWingmanMissileRequestResult(RequestId,Result);BroadcastWingmanMissileResult(RequestId,Result);return;
}

void AGuLiStrikeShip::RememberWingmanMissileRequestResult(
	const FGuid& RequestId,
	const FGuLiWingmanMissileSalvoResult& Result)
{
	if (!RequestId.IsValid() || WingmanMissileRequestResults.Contains(RequestId))
	{
		return;
	}
	WingmanMissileRequestResults.Add(RequestId, Result);
	WingmanMissileRequestOrder.Add(RequestId);
	constexpr int32 MaximumRememberedRequests = 256;
	const int32 Overflow = WingmanMissileRequestOrder.Num() - MaximumRememberedRequests;
	if (Overflow <= 0)
	{
		return;
	}
	for (int32 Index = 0; Index < Overflow; ++Index)
	{
		WingmanMissileRequestResults.Remove(WingmanMissileRequestOrder[Index]);
	}
	WingmanMissileRequestOrder.RemoveAt(0, Overflow, EAllowShrinking::No);
}

void AGuLiStrikeShip::BroadcastWingmanMissileResult(
	const FGuid& RequestId,
	const FGuLiWingmanMissileSalvoResult& Result)
{
	if (IsLocallyControlled())
	{
		ClientResolveWingmanMissileSalvo(
			RequestId, Result.RejectReason, Result.SelectedTarget,
			Result.FlightIndex, Result.LaunchedCount);
	}
	else
	{
		ClientResolveWingmanMissileSalvo(
			RequestId, Result.RejectReason, Result.SelectedTarget,
			Result.FlightIndex, Result.LaunchedCount);
	}
}

void AGuLiStrikeShip::ClientResolveWingmanMissileSalvo(
	const FGuid RequestId,
	const EGuLiWingmanRejectReason RejectReason,
	const FGuLiTargetHandle ServerSelectedTarget,
	const uint8 FlightIndex,
	const int32 LaunchedCount)
{
	OnWingmanMissileSalvoResolved.Broadcast(
		RequestId, RejectReason, ServerSelectedTarget, FlightIndex, LaunchedCount);
}

void AGuLiStrikeShip::HandleShipDeath()
{
	if (bShipDeathHandled)
	{
		return;
	}
	bShipDeathHandled = true;
	WingmanMissileRequestResults.Reset();
	WingmanMissileRequestOrder.Reset();
	ActiveMissileInputBinding = FGuLiWeaponBindingKey{};
	if (ShipAim)
	{
		ShipAim->ResetForOwnerUnavailable();
	}
	if (HangarCapability)
	{
		HangarCapability->SetActiveAbilityInputEnabled(false);
		HangarCapability->ServerClearShipAbilities();
	}


	if (UGuLiShipMovementComponent* Movement = GetShipMovement())
	{
		Movement->ClearFlightInput();
		Movement->DisableMovement();
	}
}

void AGuLiStrikeShip::UnPossessed()
{
	ActiveMissileInputBinding = FGuLiWeaponBindingKey{};
	if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->ClearFlightInput(); }
	if (HangarCapability) { HangarCapability->SetActiveAbilityInputEnabled(false); }
	RemoveShipInputContext();
	Super::UnPossessed();
	if (ShipAim)
	{
		ShipAim->HandleOwnerControllerChanged();
	}
	if (ShipWorldHUD)
	{
		ShipWorldHUD->HandleOwnerControllerChanged();
	}
}

void AGuLiStrikeShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingShipPlay = true;
	ActiveMissileInputBinding = FGuLiWeaponBindingKey{};
	if (UGuLiShipMovementComponent* Movement = GetShipMovement()) { Movement->ClearFlightInput(); }
	if (HangarCapability)
	{
		HangarCapability->SetActiveAbilityInputEnabled(false);
		HangarCapability->ServerClearShipAbilities();
		HangarCapability->OnProjectionChanged().RemoveAll(this);
		HangarCapability->OnWeaponAbilityAuthorized().RemoveAll(this);
	}

	if (CombatHealth) { CombatHealth->OnDeath.RemoveDynamic(this, &AGuLiStrikeShip::HandleShipDeath); }
	RemoveShipInputContext();
	Super::EndPlay(EndPlayReason);
}

void AGuLiStrikeShip::SetWingmanAttackTarget(const FGuLiTargetHandle& Target)
{
	ServerSetWingmanAttackTarget(Target);
}
void AGuLiStrikeShip::ClearWingmanAttackTarget()
{
	ServerSetWingmanAttackTarget({});
}
void AGuLiStrikeShip::ServerSetWingmanAttackTarget(const FGuLiTargetHandle& Target)
{
    if (!UGuLiHybridActorComponent::HasSimulation(this)) return;
        auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
        FGuLiCombatTargetSnapshot Snapshot;FGuLiHybridObjectKey Key;
        if (Target.IsValid() && (!H || !Ledger || !H->ResolveTarget(Target,Key)
            || !Ledger->TryGetTargetSnapshot(Target,Snapshot) || !Snapshot.bAlive)) return;
        uint32 Revision=WingmanAttackTarget.Revision+1;if (!Revision) ++Revision;
        WingmanAttackTarget={};WingmanAttackTarget.Target=Target;WingmanAttackTarget.bSpecified=Target.IsValid();
        WingmanAttackTarget.Revision=Revision;WingmanAttackTarget.ServerTime=H->GetLogicalSeconds();
        if (Target.IsValid()) {WingmanAttackTarget.Location=Snapshot.Location;WingmanAttackTarget.Radius=Snapshot.CollisionRadius;}
        return;
}
