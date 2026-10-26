#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
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
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"

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
	const float DimensionScale=GuLiGrid::FootprintFromDiameter(460.)*GuLiGrid::CellSize/460.f;
	GetCapsuleComponent()->InitCapsuleSize(230.f*DimensionScale, 374.1898f*DimensionScale);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetRelativeScale3D(FVector(2.0512617f*DimensionScale));
	GetMesh()->SetRelativeLocation(FVector(0,0,-373.1249f*DimensionScale));
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
	CameraBoom->SetRelativeRotation(FRotator(-14.85f,-3.90f,0));
	CameraBoom->TargetArmLength = DesiredCameraDistance;
	CameraBoom->SocketOffset = FVector(0,338.24235,135.74471);
	CameraBoom->bDoCollisionTest = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->SetFieldOfView(90.f);
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
	FString ConfigurationError;
	if (MobilityConfiguration.Load(ConfigurationError))
	{
		auto* Movement=GetCharacterMovement(); const auto& M=MobilityConfiguration.Mobility;
		WalkSpeed=M.WalkSpeed; Movement->MaxWalkSpeed=M.SprintSpeed;
		Movement->MaxAcceleration=M.Acceleration; Movement->BrakingDecelerationWalking=M.Braking;
		Movement->RotationRate=FRotator(0,M.MovingTurnRate,0);
		Movement->bOrientRotationToMovement=false;
	}
	else UE_LOG(LogTemp,Error,TEXT("Ground configuration: %s"),*ConfigurationError);
	const auto* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Visuals.DT_GuLiStrikeMech_Visuals"));
	if (Table && Table->GetRowStruct() == FGuLiStrikeMechVisualsRow::StaticStruct())
		if (const auto* Row = Table->FindRow<FGuLiStrikeMechVisualsRow>(TEXT("Default"),TEXT("Mech visual"))) ModelId = Row->ModelId;
	if (GetWorld())
	{
		auto* Registry = GetWorld()->GetSubsystem<UGuLiModelRegistrySubsystem>();
		if (bIntegratedBodyMesh)
		{
			if (auto* Body = Registry->LoadSkeletalModel(ModelId)) GetMesh()->SetSkeletalMeshAsset(Body);
			Armor->SetStaticMesh(nullptr); Shoulder->SetStaticMesh(nullptr); Machinegun->SetSkeletalMeshAsset(nullptr);
			UpperBodyPivot->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
		}
		else Registry->ApplyModelParts(this,ModelId);
	}
}

UMeshComponent* AGuLiGroundMechCharacter::GetJetAttachmentMesh() const
{
	return bIntegratedBodyMesh ? static_cast<UMeshComponent*>(GetMesh()) : static_cast<UMeshComponent*>(Armor.Get());
}

FRotator AGuLiGroundMechCharacter::GetIntegratedAimRotation() const
{
	const FVector Point = GetIntegratedAimPoint();
	const FVector Origin = GetMesh()->GetSocketLocation(TEXT("cockpit"));
	const float Pitch = Point.IsNearlyZero() ? 0.f : FMath::Clamp((Point-Origin).Rotation().Pitch,-80.f,80.f);
	// Imported mesh faces +Y. Component-space roll pitches +Y toward +Z.
	return FRotator(0.f,FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,DisplayAimYaw),-Pitch);
}

FVector AGuLiGroundMechCharacter::GetIntegratedAimPoint() const
{
	return UGuLiHybridActorComponent::HasSimulation(this) && SimulationInput.bHasAim
		? SimulationInput.Aim : FVector(Weapon->AimPoint);
}

bool AGuLiGroundMechCharacter::IsShoulderLookActive() const
{
	const auto* PC = Cast<AGuLiCommanderPlayerController>(GetController());
	return CanUseControls() && PC && PC->CanUseGroundMechFireInput() && !PC->IsLookInputIgnored();
}

void AGuLiGroundMechCharacter::SetShoulderAimHeld(bool bHeld)
{
	bShoulderAimHeld=bHeld && IsShoulderLookActive();
	HybridInput.bFaceAim=bShoulderAimHeld;
	if (!bSemanticInput)
	{
		HybridInput.WeaponAimMode=bShoulderAimHeld?EGuLiGroundWeaponAimMode::Target:EGuLiGroundWeaponAimMode::Forward;
		if (!bShoulderAimHeld) { HybridInput.bHasAim=false; HybridInput.Aim=FVector::ZeroVector; }
	}
}

void AGuLiGroundMechCharacter::BeginPlay()
{
	Super::BeginPlay();
    CombatHealth->OnDeath.AddUniqueDynamic(this,&ThisClass::HandleCommittedDeath);
	RocketJump->RefreshActorInfo();
	if (MobilityConfiguration.bReady)
	{
		const auto& C=MobilityConfiguration.FreeCamera;
		DesiredCameraDistance=C.ArmLength; CameraBoom->SocketOffset=C.SocketOffset;
		CameraBoom->SetWorldRotation(FRotator(C.PitchDegrees,C.YawDegrees,0));
		Camera->SetFieldOfView(C.FieldOfView);
		Camera->SetRelativeLocationAndRotation(FVector::ZeroVector,FRotator::ZeroRotator);
		CameraBoom->bDoCollisionTest=true;
	}
	CameraBoom->TargetArmLength=DesiredCameraDistance;
	if (bIntegratedBodyMesh) GetCharacterMovement()->bOrientRotationToMovement = false;
	CameraBoom->AddWorldRotation(FRotator(0,GetActorRotation().Yaw,0));
	DisplayAimYaw = GetActorRotation().Yaw;
	GetMesh()->AddTickPrerequisiteActor(this);
	CameraBoom->AddTickPrerequisiteActor(this);
	ResetBlinkCamera();
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
	ResetBlinkCamera();
	Weapon->SetFireHeld(false);
	RocketJump->Interrupt();
	if (InputSubsystem.IsValid()) InputSubsystem->RemoveMappingContext(MappingContext);
	InputSubsystem.Reset();
	bSprintHeld = false;
	HybridInput.BlinkRequest=SimulationInput.BlinkRequest; HybridInput.BlinkYaw=SimulationInput.BlinkYaw;
	SetShoulderAimHeld(false); TurnState.Direction=0;
	CameraMoveAxis = FVector2D::ZeroVector;
	HybridInput.bFaceCamera=false; HybridInput.CameraYaw=0;
	SimulationInput.bFaceCamera=false; SimulationInput.CameraYaw=0;
	HybridInput.MoveSpace=SimulationInput.MoveSpace=EGuLiGroundMoveSpace::World;
	WeaponAimState={};
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
	Enhanced.BindAction(SprintAction,ETriggerEvent::Started,this,&ThisClass::Sprint);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Completed,this,&ThisClass::StopSprint);
	Enhanced.BindAction(SprintAction,ETriggerEvent::Canceled,this,&ThisClass::StopSprint);
	Enhanced.BindAction(ZoomAction,ETriggerEvent::Triggered,this,&ThisClass::Zoom);
	if (LookAction) Enhanced.BindAction(LookAction,ETriggerEvent::Triggered,this,&ThisClass::Look);
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
	bSemanticInput = false;
	CameraMoveAxis = Value.Get<FVector2D>().GetClampedToMaxSize(1.f);
}

void AGuLiGroundMechCharacter::Look(const FInputActionValue& Value)
{
	if (!IsShoulderLookActive()) return;
	bSemanticInput = false;
	if (!MobilityConfiguration.bReady) return;
	const auto& C=bShoulderAimHeld?MobilityConfiguration.AimCamera:MobilityConfiguration.FreeCamera;
	const FVector2D Delta = Value.Get<FVector2D>() * C.Sensitivity;
	FRotator Rotation = CameraBoom->GetComponentRotation();
	Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + Delta.X);
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Delta.Y, C.MinPitchDegrees, C.MaxPitchDegrees);
	Rotation.Roll = 0.f;
	CameraBoom->SetWorldRotation(Rotation);
}

void AGuLiGroundMechCharacter::StopMove(const FInputActionValue&)
{
	CameraMoveAxis=FVector2D::ZeroVector; HybridInput.Axis=FVector2D::ZeroVector;
	HybridInput.bFaceCamera=false; HybridInput.CameraYaw=0;
}
void AGuLiGroundMechCharacter::Sprint(const FInputActionValue&) { RequestBlink(); }
void AGuLiGroundMechCharacter::StopSprint(const FInputActionValue&) { bSprintHeld=false; HybridInput.bSprint=false; }

void AGuLiGroundMechCharacter::Zoom(const FInputActionValue& Value)
{
	if (!CanUseControls() || !MobilityConfiguration.bReady || bShoulderAimHeld) return;
	const auto& C=MobilityConfiguration.FreeCamera;
	DesiredCameraDistance=FMath::Clamp(DesiredCameraDistance-Value.Get<float>()*C.ZoomStep,C.MinDistance,C.MaxDistance);
}

bool AGuLiGroundMechCharacter::GetCursorAimPoint(FVector& OutPoint) const
{
	if (bSemanticInput) { OutPoint=HybridInput.Aim; return HybridInput.bHasAim; }
	if (!bShoulderAimHeld || !IsShoulderLookActive()) return false;
	if (!UGuLiNetworkSettings::CanPresent(GetWorld())) return false;
	const auto* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLocallyControlled() || !GetMachinegun()) return false;
	FVector Origin,Direction;
	const ULocalPlayer* Player = PC->GetLocalPlayer();
	FSceneViewProjectionData Projection;
	if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport
		|| !Player->GetProjectionData(Player->ViewportClient->Viewport,Projection)) return false;
	const FVector2D Center = FVector2D(Projection.GetConstrainedViewRect().Min)
		+ FVector2D(Projection.GetConstrainedViewRect().Size()) * .5;
	if (!PC->DeprojectScreenPositionToWorld(Center.X,Center.Y,Origin,Direction)
		|| Origin.ContainsNaN() || Direction.ContainsNaN()) return false;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GroundMechAim),true,this);
	const FVector End = Origin + Direction.GetSafeNormal() * 600000.f;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params);
	const FVector GunLocation = bIntegratedBodyMesh ? GetMachinegun()->GetSocketLocation(TEXT("Muzzle")) : Machinegun->GetComponentLocation();
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
		if (!bSemanticInput)
		{
			const bool bLookActive=IsShoulderLookActive();
			if (!bLookActive) { SetShoulderAimHeld(false); CameraMoveAxis=FVector2D::ZeroVector; HybridInput.BlinkRequest=SimulationInput.BlinkRequest; HybridInput.BlinkYaw=SimulationInput.BlinkYaw; }
			HybridInput.bFaceAim=bShoulderAimHeld;
			HybridInput.bFaceCamera=bLookActive && CameraMoveAxis.Y>UE_KINDA_SMALL_NUMBER;
			HybridInput.CameraYaw=HybridInput.bFaceCamera
				? FRotator::CompressAxisToShort(CameraBoom->GetComponentRotation().Yaw) : 0;
			// Keyboard directions retain the same meaning across takeoff and landing.
			// Semantic callers still explicitly submit world-space input.
			HybridInput.MoveSpace=EGuLiGroundMoveSpace::Body;
			HybridInput.Axis=CameraMoveAxis.GetClampedToMaxSize(1.);
			HybridInput.WeaponAimMode=bShoulderAimHeld?EGuLiGroundWeaponAimMode::Target:EGuLiGroundWeaponAimMode::Forward;
			FVector Point;
			HybridInput.bHasAim=bShoulderAimHeld && Weapon->ResolveLocalWeaponAim(Point);
			HybridInput.Aim=HybridInput.bHasAim?Point:FVector::ZeroVector;
			if (!bLookActive) { HybridInput.bFire=false; Weapon->SetFireHeld(false); }
		}
	}
	else if (IsLocallyControlled())
	{
		CameraMoveAxis=FVector2D::ZeroVector; HybridInput.Axis=FVector2D::ZeroVector;
		HybridInput.bFire=false; HybridInput.bRocket=false; HybridInput.bSprint=false;
		HybridInput.BlinkRequest=SimulationInput.BlinkRequest; HybridInput.BlinkYaw=SimulationInput.BlinkYaw;
		HybridInput.bFaceCamera=false; HybridInput.CameraYaw=0;
		SetShoulderAimHeld(false);
		Weapon->SetFireHeld(false);
	}
	PumpHybridInput();
	if (UGuLiNetworkSettings::CanPresent(GetWorld()))
	{
		UpperBodyPivot->SetWorldRotation(FRotator(0,DisplayAimYaw-90.f,0));
		// Query replicas are not simulation owners; their pose still needs a real presentation clock.
		if (!UGuLiHybridActorComponent::HasSimulation(this) && !bCarrierDormant)
		{
			GetMesh()->TickAnimation(DeltaSeconds,false); GetMesh()->RefreshBoneTransforms();
			if (!bIntegratedBodyMesh) { Machinegun->TickAnimation(DeltaSeconds,false); Machinegun->RefreshBoneTransforms(); }
		}
		PresentBlink();
		UpdateMechCamera(DeltaSeconds);
	}
}

bool AGuLiGroundMechCharacter::SubmitGameplayInput(FVector2D Axis,FVector AimPoint,bool bSprint,bool bFire,bool bRocket,bool bHasAim,bool bFaceAim)
{
	if (!CanUseControls() || Axis.ContainsNaN() || AimPoint.ContainsNaN() || (bFire && !bHasAim)) return false;
	bSemanticInput=true; HybridInput={Axis.GetClampedToMaxSize(1.),AimPoint,bSprint,bFire,bRocket,bHasAim,bFaceAim};
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
	if (SimulationInput.bHasAim)
	{
		const auto Offset=SimulationInput.Aim-GetActorLocation();
		if (Offset.SizeSquared2D()>1) AimYaw=FRotator::CompressAxisToShort(Offset.Rotation().Yaw);
	}
	DisplayAimYaw=FRotator::DecompressAxisFromShort(AimYaw);
	const float PreviousBodyYaw=GetActorRotation().Yaw;
	AdvanceBlink(DeltaSeconds);
	if (!BlinkState.Phase) AdvanceFacing(DeltaSeconds);
	FVector2D WorldAxis=SimulationInput.Axis;
	if (SimulationInput.MoveSpace==EGuLiGroundMoveSpace::Body)
	{
		const FRotationMatrix Basis(FRotator(0,GetActorRotation().Yaw,0));
		const FVector WorldInput=Basis.GetUnitAxis(EAxis::X)*WorldAxis.Y+Basis.GetUnitAxis(EAxis::Y)*WorldAxis.X;
		WorldAxis=FVector2D(WorldInput.Y,WorldInput.X);
		if (Movement->IsGroundedForAnimation() && !RocketJump->IsThrusting() && HasMovementIntent())
		{
			// Transport the driven velocity in body space. Pending impulses, vertical
			// motion, platform displacement and subsequent collision response stay in CMC.
			const float DeltaYaw=FMath::FindDeltaAngleDegrees(PreviousBodyYaw,GetActorRotation().Yaw);
			Movement->Velocity=FRotator(0,DeltaYaw,0).RotateVector(Movement->Velocity);
		}
	}
	Movement->AdvanceHybrid(DeltaSeconds,BlinkState.Phase?FVector2D::ZeroVector:WorldAxis,SimulationInput.bSprint);
	AdvanceWeaponAim(DeltaSeconds);
	UpperBodyPivot->SetWorldRotation(FRotator(0,DisplayAimYaw-90.f,0));
	// These skeletal poses contain gameplay mounting/muzzle sockets on logic-only endpoints too.
	GetMesh()->TickAnimation(DeltaSeconds,false); GetMesh()->RefreshBoneTransforms();
	if (!bIntegratedBodyMesh) { Machinegun->TickAnimation(DeltaSeconds,false); Machinegun->RefreshBoneTransforms(); }
	const bool bForward=SimulationInput.WeaponAimMode==EGuLiGroundWeaponAimMode::Forward;
	const FVector FirePoint=bForward?GetMesh()->GetSocketLocation(TEXT("Gun_R"))+GetActorForwardVector()
		*(Weapon->ActiveSkill.ProjectileSpeed*Weapon->ActiveSkill.ProjectileLifetime):SimulationInput.Aim;
	Weapon->AdvanceHybrid(DeltaSeconds,SimulationInput.bFire && !BlinkState.Phase,FirePoint,bForward || SimulationInput.bHasAim);
	PresentBlink();
}

void AGuLiGroundMechCharacter::AdvanceWeaponAim(float DeltaSeconds)
{
	const bool bForward=SimulationInput.WeaponAimMode==EGuLiGroundWeaponAimMode::Forward;
	if (bForward && !WeaponAimState.bForward)
	{
		WeaponAimState.RightStart=GetMesh()->GetSocketTransform(TEXT("GunArm_R"),RTS_Component).GetRotation();
		WeaponAimState.LeftStart=GetMesh()->GetSocketTransform(TEXT("GunArm_L"),RTS_Component).GetRotation();
		WeaponAimState.ReturnAlpha=0.f;
	}
	WeaponAimState.bForward=bForward;
	WeaponAimState.ReturnAlpha=bForward?FMath::Min(1.f,WeaponAimState.ReturnAlpha+DeltaSeconds/MobilityConfiguration.Mobility.GunReturnSeconds):1.f;
}

void AGuLiGroundMechCharacter::AdvanceFacing(float DeltaSeconds)
{
	if (!MobilityConfiguration.bReady) return;
	const auto& M=MobilityConfiguration.Mobility;
	const auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement());
	if (FacingDisplacementRevision!=Move->GetDisplacementRevision())
	{ FacingDisplacementRevision=Move->GetDisplacementRevision(); TurnState.Direction=0; ++TurnState.Serial; }
	const bool bGrounded=Move->IsGroundedForAnimation() && !RocketJump->IsThrusting();
	const bool bFollowCamera=SimulationInput.bFaceCamera;
	const bool bFollowAim=SimulationInput.bFaceAim && SimulationInput.bHasAim;
	if (!bFollowCamera && !bFollowAim)
	{ TurnState.Direction=0; TurnState.Progress=0; return; }
	const float TargetYaw=bFollowCamera?FRotator::DecompressAxisFromShort(SimulationInput.CameraYaw):DisplayAimYaw;
	const float Error=FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,TargetYaw);
	const bool bContinuousTurn=!bGrounded || GetVelocity().SizeSquared2D()>FMath::Square(M.TurnMovementThreshold)
		|| !SimulationInput.Axis.IsNearlyZero();
	const float StopDegrees=bContinuousTurn?UE_KINDA_SMALL_NUMBER:M.TurnStopDegrees;
	if (TurnState.Direction && (FMath::Abs(Error)<=StopDegrees || Error*TurnState.Direction<0.f))
	{
		TurnState.Direction=0; TurnState.Progress=0;
	}
	if (!TurnState.Direction)
	{
		const float StartDegrees=bContinuousTurn?UE_KINDA_SMALL_NUMBER:(TurnState.Progress==1.f?M.TurnStopDegrees:M.TurnStartDegrees);
		if (FMath::Abs(Error)<=StartDegrees) return;
		const FName Name=Error<0?TEXT("TurnLeft"):TEXT("TurnRight");
		const auto& G=MobilityConfiguration.Gait(Name);
		const auto* Montage=MobilityConfiguration.Asset<UAnimMontage>(Name); if (!Montage) return;
		++TurnState.Serial; TurnState.Direction=Error<0?-1:1; TurnState.Time=TurnState.Progress=0;
		TurnState.StartYaw=GetActorRotation().Yaw; TurnState.Duration=Montage->GetPlayLength()/G.BasePlayRate;
		TurnState.TargetDegrees=FMath::Clamp(Error,-FMath::Abs(G.TurnDegrees),FMath::Abs(G.TurnDegrees));
	}
	const FName Name=TurnState.Direction<0?TEXT("TurnLeft"):TEXT("TurnRight");
	const auto& G=MobilityConfiguration.Gait(Name); const auto* Montage=MobilityConfiguration.Asset<UAnimMontage>(Name);
	const auto* Sequence=Cast<UAnimSequence>(Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference());
	TurnState.Time=FMath::Min(TurnState.Time+DeltaSeconds,TurnState.Duration);
	// W, right-button ground turns and airborne right-button turns share the
	// same Excel-selected root-yaw curve and play rate. Movement's rate is a
	// ceiling, not a separate 1080-deg/s shortcut around the authored step.
	const float PreviousProgress=TurnState.Progress;
	const float Curve=Sequence?Sequence->EvaluateCurveData(TEXT("GroundTurnProgress"),FAnimExtractContext(double(TurnState.Time*G.BasePlayRate))):0.f;
	TurnState.Progress=FMath::Clamp(Curve,TurnState.Progress,1.f);
	float DegreesThisTick=FMath::Abs(TurnState.TargetDegrees)*(TurnState.Progress-PreviousProgress);
	if (bContinuousTurn) DegreesThisTick=FMath::Min(DegreesThisTick,M.MovingTurnRate*DeltaSeconds);
	// Use this frame's requested heading: a moving target cannot leave the body
	// turning toward an obsolete start angle or snap it at the end of the step.
	SetActorRotation(FRotator(0,FMath::FixedTurn(GetActorRotation().Yaw,TargetYaw,DegreesThisTick),0));
	if (TurnState.Time>=TurnState.Duration)
	{
		TurnState.Direction=0;
		// A further step starts on the next fixed tick only if the residual exceeds the stop threshold.
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,TargetYaw))>StopDegrees)
		{
			// Retain hysteresis across consecutive steps (small residual finishes naturally).
			TurnState.Progress=1.f;
		}
		else TurnState.Progress=0.f;
	}
}
