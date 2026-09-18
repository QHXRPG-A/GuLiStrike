// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikeCharacter.h"
#include "BlinkVFX.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "InputCoreTypes.h"
#include "InputAction.h"
#include "GuLiStrikeGameMode.h"
#include "GuLiStrikeAoEAttack.h"
#include "Kismet/KismetMathLibrary.h"
#include "GuLiStrikeProjectile.h"
#include "Engine/World.h"
#include "TimerManager.h"

AGuLiStrikeCharacter::AGuLiStrikeCharacter()
{
 	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(8.4f, 19.2f);
	GetMesh()->SetRelativeScale3D(FVector(0.2f));

	// create the spring arm
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArm->SetupAttachment(RootComponent);

	SpringArm->SetRelativeRotation(FRotator(-50.0f, 0.0f, 0.0f));

	SpringArm->TargetArmLength = 440.0f;
	SpringArm->ProbeSize = 2.4f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 0.5f;

	// create the camera
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);

	Camera->SetFieldOfView(75.0f);

	// configure the character movement
	GetCharacterMovement()->GravityScale = 0.3f;
	GetCharacterMovement()->MaxAcceleration = 200.0f;
	GetCharacterMovement()->MaxWalkSpeed = 120.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 409.6f;
	GetCharacterMovement()->MaxStepHeight = 9.0f;
	GetCharacterMovement()->PerchAdditionalHeight = 8.0f;
	GetCharacterMovement()->JumpZVelocity = 84.0f;
	GetCharacterMovement()->NetworkMaxSmoothUpdateDistance = 51.2f;
	GetCharacterMovement()->NetworkNoSmoothUpdateDistance = 76.8f;
	GetCharacterMovement()->BrakingFrictionFactor = 1.0f;
	GetCharacterMovement()->bCanWalkOffLedges = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 640.0f, 0.0f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;
}

void AGuLiStrikeCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	// update the items count
	UpdateItems();
}

void AGuLiStrikeCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	/** Clear the autofire timer */
	GetWorld()->GetTimerManager().ClearTimer(AutoFireTimer);
}

void AGuLiStrikeCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// set the player controller reference
	PlayerController = Cast<APlayerController>(GetController());
}

void AGuLiStrikeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// get the current rotation
	const FRotator OldRotation = GetActorRotation();

	// are we aiming with the mouse?
	if (bUsingMouse)
	{
		if (PlayerController)
		{
			// get the cursor world location
			FHitResult OutHit; 
			PlayerController->GetHitResultUnderCursorByChannel(MouseAimTraceChannel, true, OutHit);

			// find the aim rotation 
			const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(GetActorLocation(), OutHit.Location);

			// save the aim angle
			AimAngle = AimRot.Yaw;

			
			// update the yaw, reuse the pitch and roll
			SetActorRotation(FRotator(OldRotation.Pitch, AimAngle, OldRotation.Roll));

		}

	} else {

		// use quaternion interpolation to blend between our current rotation
		// and the desired aim rotation using the shortest path
		const FRotator TargetRot = FRotator(OldRotation.Pitch, AimAngle, OldRotation.Roll);

		SetActorRotation(TargetRot);
	}
}

void AGuLiStrikeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AGuLiStrikeCharacter::DoDash);

	// set up the enhanced input action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{

		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AGuLiStrikeCharacter::Move);
		EnhancedInputComponent->BindAction(StickAimAction, ETriggerEvent::Triggered, this, &AGuLiStrikeCharacter::StickAim);
		EnhancedInputComponent->BindAction(MouseAimAction, ETriggerEvent::Triggered, this, &AGuLiStrikeCharacter::MouseAim);
		EnhancedInputComponent->BindAction(DashAction, ETriggerEvent::Triggered, this, &AGuLiStrikeCharacter::Dash);
		EnhancedInputComponent->BindAction(ShootAction, ETriggerEvent::Triggered, this, &AGuLiStrikeCharacter::Shoot);
		EnhancedInputComponent->BindAction(AoEAction, ETriggerEvent::Triggered, this, &AGuLiStrikeCharacter::AoEAttack);

	}

}

void AGuLiStrikeCharacter::Move(const FInputActionValue& Value)
{
	// save the input vector
	FVector2D InputVector = Value.Get<FVector2D>();

	// route the input
	DoMove(InputVector.X, InputVector.Y);
}

void AGuLiStrikeCharacter::StickAim(const FInputActionValue& Value)
{
	// get the input vector
	FVector2D InputVector = Value.Get<FVector2D>();

	// route the input
	DoAim(InputVector.X, InputVector.Y);
}

void AGuLiStrikeCharacter::MouseAim(const FInputActionValue& Value)
{
	// raise the mouse controls flag
	bUsingMouse = true;

	// show the mouse cursor
	if (PlayerController)
	{
		PlayerController->SetShowMouseCursor(true);
	}
}

void AGuLiStrikeCharacter::Dash(const FInputActionValue& Value)
{
	// route the input
	DoDash();
}

void AGuLiStrikeCharacter::Shoot(const FInputActionValue& Value)
{
	// route the input
	DoShoot();
}

void AGuLiStrikeCharacter::AoEAttack(const FInputActionValue& Value)
{
	// route the input
	DoAoEAttack();
}

void AGuLiStrikeCharacter::DoMove(float AxisX, float AxisY)
{
	// save the input
	LastMoveInput.X = AxisX;
	LastMoveInput.Y = AxisY;

	// calculate the forward component of the input
	FRotator FlatRot = GetControlRotation();
	FlatRot.Pitch = 0.0f;

	// apply the forward input
	AddMovementInput(FlatRot.RotateVector(FVector::ForwardVector), AxisX);

	// apply the right input
	AddMovementInput(FlatRot.RotateVector(FVector::RightVector), AxisY);
}

void AGuLiStrikeCharacter::DoAim(float AxisX, float AxisY)
{
	// calculate the aim angle from the inputs
	AimAngle = FMath::RadiansToDegrees(FMath::Atan2(AxisY, -AxisX));

	// lower the mouse controls flag
	bUsingMouse = false;

	// hide the mouse cursor
	if (PlayerController)
	{
		PlayerController->SetShowMouseCursor(false);
	}

	// are we on autofire cooldown?
	if (!bAutoFireActive)
	{
		// set ourselves on cooldown
		bAutoFireActive = true;

		// fire a projectile
		DoShoot();

		// schedule autofire cooldown reset
		GetWorld()->GetTimerManager().SetTimer(AutoFireTimer, this, &AGuLiStrikeCharacter::ResetAutoFire, AutoFireDelay, false);
	}
}

void AGuLiStrikeCharacter::DoDash()
{
	// 角色朝向由鼠标瞄准更新；闪现只在水平面内沿该方向移动。
	const FVector BlinkDirection = GetActorForwardVector().GetSafeNormal2D();
	if (BlinkDirection.IsNearlyZero())
	{
		// 没有有效方向时不执行闪现。
		return;
	}

	// 先保存起点，传送后仍需要在这里生成角色残影。
	const FTransform BlinkStartTransform = GetActorTransform();
	// 使用可在蓝图中调节的闪现距离计算终点。
	const FVector BlinkTarget = GetActorLocation() + BlinkDirection * DashDistance;
	if (TeleportTo(BlinkTarget, GetActorRotation()))
	{
		// 闪现完成后清除原有速度，避免继续滑行或保留击退速度。
		GetCharacterMovement()->StopMovementImmediately();
		const FTransform BlinkEndTransform = GetActorTransform();

		// 纯视觉特效不参与碰撞，因此始终允许在起点和终点生成。
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (ABlinkVFX* StartVFX = GetWorld()->SpawnActor<ABlinkVFX>(ABlinkVFX::StaticClass(), BlinkStartTransform, SpawnParameters))
		{
			// 起点：显示冻结的角色残影和扩散热波。
			StartVFX->InitializeFromCharacter(
				this,
				true,
				true,
				DashTrailOpacity * 0.35f,
				DashTrailAfterimageLifetime * 0.75f);
		}

		const int32 TrailCount = FMath::Max(DashTrailAfterimageCount, 0);
		for (int32 TrailIndex = 1; TrailIndex <= TrailCount; ++TrailIndex)
		{
			const float TrailAlpha = static_cast<float>(TrailIndex) / static_cast<float>(TrailCount + 1);
			FTransform TrailTransform = BlinkStartTransform;
			TrailTransform.SetLocation(FMath::Lerp(BlinkStartTransform.GetLocation(), BlinkEndTransform.GetLocation(), TrailAlpha));

			const float TrailOpacity = DashTrailOpacity * FMath::Lerp(0.45f, 1.0f, TrailAlpha);
			const float TrailLifetime = DashTrailAfterimageLifetime * FMath::Lerp(0.8f, 1.0f, TrailAlpha);
			if (ABlinkVFX* TrailVFX = GetWorld()->SpawnActor<ABlinkVFX>(ABlinkVFX::StaticClass(), TrailTransform, SpawnParameters))
			{
				TrailVFX->InitializeFromCharacter(this, true, false, TrailOpacity, TrailLifetime);
			}
		}

		if (ABlinkVFX* EndVFX = GetWorld()->SpawnActor<ABlinkVFX>(ABlinkVFX::StaticClass(), BlinkEndTransform, SpawnParameters))
		{
			// 终点：只显示一次热波，强调空间被撕开的感觉。
			EndVFX->InitializeFromCharacter(this, false);
		}
	}
}

void AGuLiStrikeCharacter::DoShoot()
{
	// get the actor transform
	FTransform ProjectileTransform = GetActorTransform();

	// apply the projectile spawn offset
	FVector ProjectileLocation = ProjectileTransform.GetLocation() + ProjectileTransform.GetRotation().RotateVector(FVector::ForwardVector * ProjectileOffset);
	ProjectileTransform.SetLocation(ProjectileLocation);

	AGuLiStrikeProjectile* Projectile = GetWorld()->SpawnActor<AGuLiStrikeProjectile>(ProjectileClass, ProjectileTransform);
}

void AGuLiStrikeCharacter::DoAoEAttack()
{
	// do we have enough items to do an AoE attack?
	if (Items > 0)
	{
		// get the game time
		const float GameTime = GetWorld()->GetTimeSeconds();

		// are we off AoE cooldown?
		if (GameTime - LastAoETime > AoECooldownTime)
		{
			// save the new AoE time
			LastAoETime = GameTime;

			// spawn the AoE
			AGuLiStrikeAoEAttack* AoE = GetWorld()->SpawnActor<AGuLiStrikeAoEAttack>(AoEAttackClass, GetActorTransform());

			// decrease the number of items
			--Items;

			// update the items count
			UpdateItems();
		}
	}
}

void AGuLiStrikeCharacter::HandleDamage(float Damage, const FVector& DamageDirection)
{
	// calculate the knockback vector
	FVector LaunchVector = DamageDirection;
	LaunchVector.Z = 0.0f;

	// apply knockback to the character
	LaunchCharacter(LaunchVector * KnockbackStrength, true, true);

	// pass control to BP
	BP_Damaged();
}

void AGuLiStrikeCharacter::AddPickup()
{
	// increase the item count
	++Items;

	// update the items counter
	UpdateItems();
}

void AGuLiStrikeCharacter::UpdateItems()
{
	// update the game mode
	if (AGuLiStrikeGameMode* GM = Cast<AGuLiStrikeGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->ItemUsed(Items);
	}
}

void AGuLiStrikeCharacter::ResetAutoFire()
{
	// reset the autofire flag
	bAutoFireActive = false;
}
