#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

AGuLiGroundMechCharacter::AGuLiGroundMechCharacter(const FObjectInitializer& Initializer)
	: Super(Initializer.SetDefaultSubobjectClass<UGuLiExternalCharacterMovementComponent>(CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	bUseControllerRotationYaw = false;
	CreateDefaultSubobject<UGuLiExternalUnitControlComponent>(TEXT("ExternalUnitControl"));
	GetCapsuleComponent()->InitCapsuleSize(230.f, 374.1898f);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetRelativeScale3D(FVector(2.0512617f));
	GetMesh()->SetRelativeLocation(FVector(0,0,-373.1249f));
	// The source model faces +Y; the Character's movement forward is +X.
	GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
	UpperBodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("UpperBodyPivot"));
	UpperBodyPivot->SetupAttachment(GetMesh(),TEXT("Mount_Top"));
	UpperBodyPivot->SetUsingAbsoluteRotation(true);
	Armor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Armor"));
	Armor->SetupAttachment(UpperBodyPivot);
	Armor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shoulder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shoulder"));
	Shoulder->SetupAttachment(Armor,TEXT("Mount_Weapon_L"));
	Shoulder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Machinegun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Machinegun"));
	Machinegun->SetupAttachment(Armor,TEXT("Mount_Weapon_R"));
	Machinegun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-55,0,0));
	CameraBoom->TargetArmLength = DesiredCameraDistance;
	CameraBoom->bDoCollisionTest = false;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom);
	Camera->SetFieldOfView(60.f);
	auto* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0,540,0);
	Movement->MaxWalkSpeed = 1440.f;
	Movement->MinAnalogWalkSpeed = 0.f;
	Movement->MaxAcceleration = 2880.f;
	Movement->BrakingDecelerationWalking = 2880.f;
	Movement->MaxStepHeight = 75.f;
	Movement->SetWalkableFloorAngle(45.f);
	Movement->GravityScale = 1.f;
}

void AGuLiGroundMechCharacter::BeginPlay()
{
	Super::BeginPlay();
	DesiredCameraDistance = StandingHeight * 4.f;
	DisplayAimYaw = GetActorRotation().Yaw;
	GetMesh()->AddTickPrerequisiteActor(this);
}

void AGuLiGroundMechCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	RemoveInputContext();
	auto* PC = CastChecked<APlayerController>(GetController());
	InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	InputSubsystem->AddMappingContext(MappingContext,20);
}

void AGuLiGroundMechCharacter::RemoveInputContext()
{
	if (InputSubsystem.IsValid()) InputSubsystem->RemoveMappingContext(MappingContext);
	InputSubsystem.Reset();
	bSprintHeld = false;
}

void AGuLiGroundMechCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	if (!IsLocallyControlled()) RemoveInputContext();
}

void AGuLiGroundMechCharacter::UnPossessed()
{
	RemoveInputContext();
	Super::UnPossessed();
}

void AGuLiGroundMechCharacter::EndPlay(EEndPlayReason::Type Reason)
{
	RemoveInputContext();
	Super::EndPlay(Reason);
}

void AGuLiGroundMechCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	auto& Enhanced = *CastChecked<UEnhancedInputComponent>(Input);
	Enhanced.BindAction(MoveAction,ETriggerEvent::Triggered,this,&ThisClass::Move);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Triggered,this,&ThisClass::Sprint);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Completed,this,&ThisClass::StopSprint);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Canceled,this,&ThisClass::StopSprint);
	Enhanced.BindAction(ZoomAction,ETriggerEvent::Triggered,this,&ThisClass::Zoom);
}

bool AGuLiGroundMechCharacter::CanUseControls() const
{
	if (!IsLocallyControlled()) return false;
	const auto* PC = Cast<AGuLiBattlePlayerController>(GetController());
	const auto* State = GetPlayerState<AGuLiBattlePlayerState>();
	// Ownership and PlayerState arrive independently during initial possession.
	return PC && State && State->GetBattleRole()==EGuLiCommanderRole::Ground
		&& State->IsBattleReady() && PC->GetPlayerNetSyncComponent()->IsConnectionReady()
		&& !PC->IsMoveInputIgnored() && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(this);
}

void AGuLiGroundMechCharacter::Move(const FInputActionValue& Value)
{
	if (!CanUseControls()) return;
	const FVector2D Axis = Value.Get<FVector2D>().GetClampedToMaxSize(1.f);
	const float Magnitude = bSprintHeld ? 1.f : WalkSpeed/GetCharacterMovement()->MaxWalkSpeed;
	// CMC transmits this acceleration magnitude, so walk/run share its existing prediction.
	AddMovementInput(FVector::ForwardVector,Axis.Y*Magnitude);
	AddMovementInput(FVector::RightVector,Axis.X*Magnitude);
}

void AGuLiGroundMechCharacter::Sprint(const FInputActionValue&) { bSprintHeld=CanUseControls(); }
void AGuLiGroundMechCharacter::StopSprint(const FInputActionValue&) { bSprintHeld=false; }

void AGuLiGroundMechCharacter::Zoom(const FInputActionValue& Value)
{
	if (!CanUseControls()) return;
	DesiredCameraDistance=FMath::Clamp(DesiredCameraDistance-Value.Get<float>()*StandingHeight*.4f,StandingHeight*3.f,StandingHeight*8.f);
}

void AGuLiGroundMechCharacter::UpdateAim()
{
	auto& PC = *CastChecked<APlayerController>(GetController());
	FVector Origin,Direction;
	if (!PC.DeprojectMousePositionToWorld(Origin,Direction)) return;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GroundMechAim),true,this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*600000.f,ECC_Visibility,Params)) return;
	const FVector ToTarget=Hit.ImpactPoint-GetActorLocation();
	if (ToTarget.SizeSquared2D()<1.f) return;
	PC.SetControlRotation(FRotator(0,ToTarget.Rotation().Yaw,0));
}

void AGuLiGroundMechCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (CanUseControls()) UpdateAim();
	else bSprintHeld=false;
	if (HasAuthority() && Controller) AimYaw=FRotator::CompressAxisToShort(GetControlRotation().Yaw);
	const float TargetYaw=IsLocallyControlled()?GetControlRotation().Yaw:FRotator::DecompressAxisFromShort(AimYaw);
	DisplayAimYaw=FMath::FixedTurn(DisplayAimYaw,TargetYaw,540.f*DeltaSeconds);
	UpperBodyPivot->SetWorldRotation(FRotator(0,DisplayAimYaw-90.f,0));
	CameraBoom->TargetArmLength=FMath::FInterpTo(CameraBoom->TargetArmLength,DesiredCameraDistance,DeltaSeconds,10.f);
}

void AGuLiGroundMechCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AGuLiGroundMechCharacter,AimYaw,COND_SkipOwner);
}
