// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/WarMachine/GuLiWarMachinePlaceholderPawn.h"

#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

AGuLiWarMachinePlaceholderPawn::AGuLiWarMachinePlaceholderPawn()
{
	bReplicates = true;
	// 战局最多十个玩家 Pawn，允许空中远距相机同样观察 Ground；士兵仍走独立姿态流。
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	GetCapsuleComponent()->InitCapsuleSize(60.0f, 90.0f);

	// 使用引擎已有形状，不新增模型或蓝图资产；碰撞和移动仍由 Character 的胶囊体承担。
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	// 静态占位外观随 CharacterMesh0 的 CMC 网络平滑移动，胶囊仍承担权威碰撞。
	BodyMesh->SetupAttachment(GetMesh());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetGenerateOverlapEvents(false);
	BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -30.0f));
	BodyMesh->SetRelativeScale3D(FVector(1.2f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMeshAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (BodyMeshAsset.Succeeded())
	{
		BodyMesh->SetStaticMesh(BodyMeshAsset.Object);
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 450.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 60.0f);
	CameraBoom->bUsePawnControlRotation = true;
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// 复用 CMC 行走/预测/校正，不另外写位置 RPC 或把指挥官姿态协议套到玩家 Pawn 上。
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 600.0f;
}

void AGuLiWarMachinePlaceholderPawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (APlayerController* OwningPlayerController = Cast<APlayerController>(GetController()))
	{
		if (OwningPlayerController->IsLocalController())
		{
			OwningPlayerController->SetInputMode(FInputModeGameOnly());
			OwningPlayerController->SetShowMouseCursor(false);
		}
	}
}

void AGuLiWarMachinePlaceholderPawn::SetupPlayerInputComponent(UInputComponent* IncomingInputComponent)
{
	Super::SetupPlayerInputComponent(IncomingInputComponent);
	if (!IncomingInputComponent)
	{
		return;
	}

	// 直接绑定已有键，不创建输入资产；派生角色输入与指挥官 Controller 的快捷键分离。
	IncomingInputComponent->BindAxisKey(EKeys::W, this, &AGuLiWarMachinePlaceholderPawn::HandleMoveForward);
	IncomingInputComponent->BindAxisKey(EKeys::S, this, &AGuLiWarMachinePlaceholderPawn::HandleMoveBackward);
	IncomingInputComponent->BindAxisKey(EKeys::D, this, &AGuLiWarMachinePlaceholderPawn::HandleMoveRight);
	IncomingInputComponent->BindAxisKey(EKeys::A, this, &AGuLiWarMachinePlaceholderPawn::HandleMoveLeft);
	IncomingInputComponent->BindAxisKey(EKeys::MouseX, this, &AGuLiWarMachinePlaceholderPawn::HandleLookYaw);
	IncomingInputComponent->BindAxisKey(EKeys::MouseY, this, &AGuLiWarMachinePlaceholderPawn::HandleLookPitch);
}

bool AGuLiWarMachinePlaceholderPawn::CanAcceptGroundInput() const
{
	// 这是正常玩家的本地输入门；它不替代 CMC 的服务器移动处理，也不授予武器权限。
	const AGuLiBattlePlayerController* OwningPlayerController = Cast<AGuLiBattlePlayerController>(GetController());
	const AGuLiBattlePlayerState* BattlePlayerState = GetPlayerState<AGuLiBattlePlayerState>();
	const UGuLiPlayerNetSyncComponent* ConnectionSync = OwningPlayerController
		? OwningPlayerController->GetPlayerNetSyncComponent() : nullptr;
	return IsLocallyControlled() && OwningPlayerController && OwningPlayerController->IsLocalController()
		&& BattlePlayerState && BattlePlayerState->GetBattleRole() == EGuLiCommanderRole::Ground
		&& BattlePlayerState->IsBattleReady() && ConnectionSync && ConnectionSync->IsConnectionReady();
}

void AGuLiWarMachinePlaceholderPawn::ApplyMoveInput(const float AxisValue, const bool bForwardAxis)
{
	if (!CanAcceptGroundInput() || !FMath::IsFinite(AxisValue) || FMath::IsNearlyZero(AxisValue))
	{
		return;
	}

	const FRotator ViewYaw(0.0f, GetControlRotation().Yaw, 0.0f);
	const FVector MoveDirection = FRotationMatrix(ViewYaw).GetUnitAxis(bForwardAxis ? EAxis::X : EAxis::Y);
	AddMovementInput(MoveDirection, AxisValue);
}

void AGuLiWarMachinePlaceholderPawn::HandleMoveForward(const float AxisValue)
{
	ApplyMoveInput(AxisValue, true);
}

void AGuLiWarMachinePlaceholderPawn::HandleMoveBackward(const float AxisValue)
{
	ApplyMoveInput(-AxisValue, true);
}

void AGuLiWarMachinePlaceholderPawn::HandleMoveRight(const float AxisValue)
{
	ApplyMoveInput(AxisValue, false);
}

void AGuLiWarMachinePlaceholderPawn::HandleMoveLeft(const float AxisValue)
{
	ApplyMoveInput(-AxisValue, false);
}

void AGuLiWarMachinePlaceholderPawn::HandleLookYaw(const float AxisValue)
{
	if (CanAcceptGroundInput() && FMath::IsFinite(AxisValue))
	{
		AddControllerYawInput(AxisValue);
	}
}

void AGuLiWarMachinePlaceholderPawn::HandleLookPitch(const float AxisValue)
{
	if (CanAcceptGroundInput() && FMath::IsFinite(AxisValue))
	{
		AddControllerPitchInput(-AxisValue);
	}
}

bool AGuLiWarMachinePlaceholderPawn::CanFire() const
{
	return false;
}

bool AGuLiWarMachinePlaceholderPawn::RequestFire()
{
	return false;
}
