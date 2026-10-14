#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "Engine/DataTable.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/GroundMech/GuLiGroundMechAbilities.h"
#include "Gameplay/GroundMech/GuLiGroundMechRocketComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/GroundMech/Private/GuLiHybridGroundInput.h"

AGuLiGroundMechCharacter::AGuLiGroundMechCharacter(const FObjectInitializer& Initializer)
	: Super(Initializer.SetDefaultSubobjectClass<UGuLiGroundMechMovementComponent>(CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	bUseControllerRotationYaw = false;
	Weapon = CreateDefaultSubobject<UGuLiGroundMechWeaponComponent>(TEXT("Weapon"));
    CombatHealth = CreateDefaultSubobject<UGuLiCombatHealthComponent>(TEXT("CombatHealth"));
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("GroundMechAbilitySystem"));
	AbilitySystem->SetIsReplicated(false);
	FuelAttributes = CreateDefaultSubobject<UGuLiGroundMechAttributeSet>(TEXT("GroundMechFuelAttributes"));
	RocketJump = CreateDefaultSubobject<UGuLiGroundMechRocketComponent>(TEXT("RocketJump"));
	CreateDefaultSubobject<UGuLiExternalUnitControlComponent>(TEXT("ExternalUnitControl"))->bPlayerVehicle=true;
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

void AGuLiGroundMechCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	const auto* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Visuals.DT_GuLiStrikeMech_Visuals"));
	if (Table && Table->GetRowStruct() == FGuLiStrikeMechVisualsRow::StaticStruct())
		if (const auto* Row = Table->FindRow<FGuLiStrikeMechVisualsRow>(TEXT("Default"),TEXT("Mech visual"))) ModelId = Row->ModelId;
	if (GetWorld()) GetWorld()->GetSubsystem<UGuLiModelRegistrySubsystem>()->ApplyModelParts(this,ModelId);
}

void AGuLiGroundMechCharacter::BeginPlay()
{
	Super::BeginPlay();
    CombatHealth->OnDeath.AddUniqueDynamic(this,&ThisClass::HandleCommittedDeath);
	RocketJump->RefreshActorInfo();
	DesiredCameraDistance = StandingHeight * 4.f;
	DisplayAimYaw = GetActorRotation().Yaw;
	GetMesh()->AddTickPrerequisiteActor(this);
}

void AGuLiGroundMechCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	RocketJump->RefreshActorInfo();
	RemoveInputContext();
	const auto* PC = Cast<APlayerController>(GetController());
	ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	if (!Player) return;
	InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player);
	if (InputSubsystem.IsValid() && MappingContext) InputSubsystem->AddMappingContext(MappingContext,20);
}

void AGuLiGroundMechCharacter::RemoveInputContext()
{
	Weapon->SetFireHeld(false);
	RocketJump->Interrupt();
	if (InputSubsystem.IsValid()) InputSubsystem->RemoveMappingContext(MappingContext);
	InputSubsystem.Reset();
	bSprintHeld = false;
}

void AGuLiGroundMechCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	RocketJump->RefreshActorInfo();
	if (!IsLocallyControlled()) RemoveInputContext();
}

void AGuLiGroundMechCharacter::UnPossessed()
{
	RemoveInputContext();
	Super::UnPossessed();
}
void AGuLiGroundMechCharacter::SetCarrierDormant(bool Dormant)
{
    bCarrierDormant=Dormant; ApplyCarrierDormancy();
}
void AGuLiGroundMechCharacter::ApplyCarrierDormancy()
{
    if (bCarrierDormant)
    {
        RemoveInputContext(); HybridInput={}; SimulationInput={}; bSemanticInput=false; ConsumeMovementInputVector();
        GetCharacterMovement()->StopMovementImmediately();
    }
}

void AGuLiGroundMechCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    CombatHealth->OnDeath.RemoveDynamic(this,&ThisClass::HandleCommittedDeath);
	RemoveInputContext();
	Super::EndPlay(Reason);
}
void AGuLiGroundMechCharacter::HandleCommittedDeath()
{
    RemoveInputContext(); HybridInput={}; SimulationInput={}; bSemanticInput=false; ConsumeMovementInputVector();
    GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->DisableMovement();
}

void AGuLiGroundMechCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	auto& Enhanced = *CastChecked<UEnhancedInputComponent>(Input);
	Enhanced.BindAction(MoveAction,ETriggerEvent::Triggered,this,&ThisClass::Move);
	Enhanced.BindAction(MoveAction,ETriggerEvent::Completed,this,&ThisClass::StopMove);
	Enhanced.BindAction(MoveAction,ETriggerEvent::Canceled,this,&ThisClass::StopMove);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Triggered,this,&ThisClass::Sprint);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Completed,this,&ThisClass::StopSprint);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Canceled,this,&ThisClass::StopSprint);
	Enhanced.BindAction(ZoomAction,ETriggerEvent::Triggered,this,&ThisClass::Zoom);
	if (RocketJumpAction)
	{
		Enhanced.BindAction(RocketJumpAction,ETriggerEvent::Started,this,&ThisClass::StartRocketJump);
		Enhanced.BindAction(RocketJumpAction,ETriggerEvent::Completed,this,&ThisClass::StopRocketJump);
		Enhanced.BindAction(RocketJumpAction,ETriggerEvent::Canceled,this,&ThisClass::StopRocketJump);
	}
}

void AGuLiGroundMechCharacter::SetRocketJumpInput(bool bHeld)
{
	HybridInput.bRocket=bHeld && CanUseControls();
}
void AGuLiGroundMechCharacter::StartRocketJump(const FInputActionValue&) { SetRocketJumpInput(true); }
void AGuLiGroundMechCharacter::StopRocketJump(const FInputActionValue&) { SetRocketJumpInput(false); }

bool AGuLiGroundMechCharacter::CanUseControls() const
{
	if (!IsLocallyControlled() || bCarrierDormant || !CombatHealth->IsAlive()) return false;
	const auto* Identity=UGuLiHybridActorComponent::Find(this);
	const auto* PC=Cast<APlayerController>(Controller);
	return Identity && Identity->CanControl() && PC && !PC->IsMoveInputIgnored()
		&& !UGuLiExternalUnitControlComponent::AreActorActionsLocked(this);
}

void AGuLiGroundMechCharacter::Move(const FInputActionValue& Value)
{
	if (!CanUseControls()) return;
	HybridInput.Axis = Value.Get<FVector2D>().GetClampedToMaxSize(1.f);
}

void AGuLiGroundMechCharacter::StopMove(const FInputActionValue&) { HybridInput.Axis=FVector2D::ZeroVector; }
void AGuLiGroundMechCharacter::Sprint(const FInputActionValue&) { bSprintHeld=CanUseControls(); HybridInput.bSprint=bSprintHeld; }
void AGuLiGroundMechCharacter::StopSprint(const FInputActionValue&) { bSprintHeld=false; HybridInput.bSprint=false; }

void AGuLiGroundMechCharacter::Zoom(const FInputActionValue& Value)
{
	if (!CanUseControls()) return;
	DesiredCameraDistance=FMath::Clamp(DesiredCameraDistance-Value.Get<float>()*StandingHeight*.4f,StandingHeight*3.f,StandingHeight*8.f);
}

bool AGuLiGroundMechCharacter::GetCursorAimPoint(FVector& OutPoint) const
{
	if (bSemanticInput) { OutPoint=HybridInput.Aim; return HybridInput.bHasAim; }
	if (!UGuLiNetworkSettings::CanPresent(GetWorld())) return false;
	const auto* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLocallyControlled() || !Machinegun) return false;
	FVector Origin,Direction;
	if (!PC->DeprojectMousePositionToWorld(Origin,Direction) || Origin.ContainsNaN() || Direction.ContainsNaN()) return false;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GroundMechAim),true,this);
	const FVector End = Origin + Direction.GetSafeNormal() * 600000.f;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params);
	const FVector GunLocation = Machinegun->GetComponentLocation();
	const FVector Offset = ((bHit ? Hit.ImpactPoint : End) - GunLocation).GetClampedToMaxSize(599999.f);
	if (Offset.SizeSquared() < 1.f) return false;
	OutPoint = GunLocation + Offset;
	return true;
}

void AGuLiGroundMechCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (CanUseControls())
	{
		FVector Point;
		if (GetCursorAimPoint(Point)) { HybridInput.Aim=Point; HybridInput.bHasAim=true; }
	}
	else if (const auto* Identity=UGuLiHybridActorComponent::Find(this);
        !Identity || !GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->HasControl(Identity->GetObjectKey()))
    { HybridInput.Axis=FVector2D::ZeroVector; HybridInput.bFire=false; HybridInput.bRocket=false; }
	PumpHybridInput();
	if (UGuLiNetworkSettings::CanPresent(GetWorld()))
	{
		UpperBodyPivot->SetWorldRotation(FRotator(0,DisplayAimYaw-90.f,0));
		CameraBoom->TargetArmLength=FMath::FInterpTo(CameraBoom->TargetArmLength,DesiredCameraDistance,DeltaSeconds,10.f);
	}
}

bool AGuLiGroundMechCharacter::SubmitGameplayInput(FVector2D Axis,FVector AimPoint,bool bSprint,bool bFire,bool bRocket,bool bHasAim)
{
	if (!CanUseControls() || Axis.ContainsNaN() || AimPoint.ContainsNaN() || (bFire && !bHasAim)) return false;
	bSemanticInput=true; HybridInput={Axis.GetClampedToMaxSize(1.),AimPoint,bSprint,bFire,bRocket,bHasAim};
	return true;
}
void AGuLiGroundMechCharacter::PumpHybridInput()
{
	const auto* Identity=UGuLiHybridActorComponent::Find(this);
	auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	if (!Identity || !Identity->CanControl() || !Hybrid) return;
	const uint64 At=Hybrid->GetLogicalTick();
	if (bHybridInputSent && At==LastHybridInputTick) return;
	if (bHybridInputSent && HybridInput==LastHybridInput && At>=LastHybridInputTick && At-LastHybridInputTick<30) return;
	FGuLiHybridObjectState Current;
	if (!Hybrid->GetObject(Identity->GetObjectKey(),Current)) return;
	FGuLiHybridStateArchive A(256); auto Input=HybridInput; GuLiHybridGround::Input(A,Input);
	FGuLiHybridCommand Command; if (!A.Finish(Command.Arguments)) return;
	Command.Event=FGuid::NewGuid(); Command.LogicalTick=At; Command.Operation=TEXT("GroundInput");
	auto& Member=Command.Members.AddDefaulted_GetRef(); Member.Key=Current.Key;
	Member.ControlEpoch=Current.ControlEpoch; Member.OwnerEpoch=Current.OwnerEpoch; Member.Slot=Current.Ordinal;
	if (Hybrid->RouteCommand(Command)) { LastHybridInput=HybridInput; LastHybridInputTick=At; bHybridInputSent=true; }
}
void AGuLiGroundMechCharacter::AdvanceHybrid(float DeltaSeconds)
{
	if (!UGuLiHybridActorComponent::HasSimulation(this)) return;
	RocketJump->ApplyHybridInput(SimulationInput.bRocket);
	auto* Movement=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement());
	Movement->AdvanceHybrid(DeltaSeconds,SimulationInput.Axis,SimulationInput.bSprint);
	if (SimulationInput.bHasAim)
	{
		const auto Offset=SimulationInput.Aim-GetActorLocation();
		if (Offset.SizeSquared2D()>1) AimYaw=FRotator::CompressAxisToShort(Offset.Rotation().Yaw);
	}
	DisplayAimYaw=FMath::FixedTurn(DisplayAimYaw,FRotator::DecompressAxisFromShort(AimYaw),540.f*DeltaSeconds);
	UpperBodyPivot->SetWorldRotation(FRotator(0,DisplayAimYaw-90.f,0));
	// These skeletal poses contain gameplay mounting/muzzle sockets on logic-only endpoints too.
	GetMesh()->TickAnimation(DeltaSeconds,false); GetMesh()->RefreshBoneTransforms();
	Machinegun->TickAnimation(DeltaSeconds,false); Machinegun->RefreshBoneTransforms();
	Weapon->AdvanceHybrid(DeltaSeconds,SimulationInput.bFire,SimulationInput.Aim,SimulationInput.bHasAim);
}
