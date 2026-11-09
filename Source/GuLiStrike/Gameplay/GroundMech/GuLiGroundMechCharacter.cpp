#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkComponent.h"
#include "Gameplay/GroundMech/Camera/GuLiGroundMechCameraComponent.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/GroundMech/Abilities/Shared/GuLiGroundMechAttributes.h"
#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGroundMechRocketComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/GroundMech/Network/Private/GuLiHybridGroundInput.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Gameplay/GroundMech/Animation/GuLiGroundMechAnimInstance.h"

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
    Blink = CreateDefaultSubobject<UGuLiGroundMechBlinkComponent>(TEXT("Blink"));
    MechCameraController = CreateDefaultSubobject<UGuLiGroundMechCameraComponent>(TEXT("MechCameraController"));
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
	CameraBoom->TargetArmLength = 1084.4236f;
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
	const auto* Viewport=PC && PC->GetLocalPlayer()?PC->GetLocalPlayer()->ViewportClient.Get():nullptr;
	return CanUseControls() && PC && PC->CanUseGroundMechFireInput() && !PC->IsLookInputIgnored()
		&& Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus();
}

void AGuLiGroundMechCharacter::SetShoulderZoomHeld(bool bHeld)
{
	const bool WasHeld=bShoulderZoomHeld;
	bShoulderZoomHeld=bHeld && IsShoulderLookActive();
    if(WasHeld!=bShoulderZoomHeld)NotifyLocalPresentation({EGuLiGroundPresentationAction::Aim,
        bShoulderZoomHeld?EGuLiGroundPresentationPhase::Started:EGuLiGroundPresentationPhase::Ended});
}

void AGuLiGroundMechCharacter::ClearLocalControlInput()
{
	bSemanticInput=false; bSprintHeld=false;
	CameraMoveAxis=FVector2D::ZeroVector;
	HybridInput={};
	HybridInput.BlinkRequest=SimulationInput.BlinkRequest;
	HybridInput.BlinkYaw=SimulationInput.BlinkYaw;
	UpperAimPresentation.Stop();
	SetShoulderZoomHeld(false);
	Weapon->SetFireHeld(false);
}

void AGuLiGroundMechCharacter::NotifyLocalPresentation(const FGuLiGroundPresentationEvent& Event)
{
    if(IsLocallyControlled() && UGuLiNetworkSettings::CanPresent(GetWorld()))OnLocalPresentation.Broadcast(Event);
}

void AGuLiGroundMechCharacter::BeginPlay()
{
	Super::BeginPlay();
    CombatHealth->OnDeath.AddUniqueDynamic(this,&ThisClass::HandleCommittedDeath);
	RocketJump->RefreshActorInfo();
	Blink->Initialize();
    if (bIntegratedBodyMesh) GetCharacterMovement()->bOrientRotationToMovement = false;
    MechCameraController->Initialize(CameraBoom,Camera);
	DisplayAimYaw = GetActorRotation().Yaw;
	ResetUpperAim(GetActorRotation().Yaw);
	GetMesh()->AddTickPrerequisiteActor(this);
	CameraBoom->AddTickPrerequisiteActor(this);
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
	Blink->Interrupt();
	Weapon->SetFireHeld(false);
	RocketJump->Interrupt();
	if (InputSubsystem.IsValid()) InputSubsystem->RemoveMappingContext(MappingContext);
	InputSubsystem.Reset();
	ClearLocalControlInput(); TurnState.Direction=0;
	SimulationInput.Axis=FVector2D::ZeroVector;
	SimulationInput.bFaceAim=SimulationInput.bHasAim=false;
	SimulationInput.Aim=FVector::ZeroVector;
	SimulationInput.bFire=SimulationInput.bRocket=SimulationInput.bSprint=false;
	SimulationInput.MoveSpace=EGuLiGroundMoveSpace::World;
	WeaponAimState={};
	UpperAimState.Stop(); UpperAimPresentation.Stop();
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
    Blink->Shutdown();
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
    MechCameraController->AddLookInput(Value.Get<FVector2D>());
}

void AGuLiGroundMechCharacter::StopMove(const FInputActionValue&)
{
	CameraMoveAxis=FVector2D::ZeroVector; HybridInput.Axis=FVector2D::ZeroVector;
}
void AGuLiGroundMechCharacter::Sprint(const FInputActionValue&) { RequestBlink(); }
void AGuLiGroundMechCharacter::StopSprint(const FInputActionValue&) { bSprintHeld=false; HybridInput.bSprint=false; }

void AGuLiGroundMechCharacter::Zoom(const FInputActionValue& Value)
{
	if (!CanUseControls() || !MobilityConfiguration.bReady || bShoulderZoomHeld) return;
    MechCameraController->AddZoomInput(Value.Get<float>());
}

bool AGuLiGroundMechCharacter::GetCursorAimPoint(FVector& OutPoint) const
{
	if (bSemanticInput) { OutPoint=HybridInput.Aim; return HybridInput.bHasAim; }
	if (!IsShoulderLookActive()) return false;
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
			if (!bLookActive) ClearLocalControlInput();
			// Keyboard directions retain the same meaning across takeoff and landing.
			// Semantic callers still explicitly submit world-space input.
			HybridInput.MoveSpace=EGuLiGroundMoveSpace::Body;
			HybridInput.Axis=CameraMoveAxis.GetClampedToMaxSize(1.);
			HybridInput.WeaponAimMode=EGuLiGroundWeaponAimMode::Target;
			FVector Point;
			HybridInput.bHasAim=bLookActive && Weapon->ResolveLocalWeaponAim(Point);
			HybridInput.bFaceAim=HybridInput.bHasAim;
			HybridInput.Aim=HybridInput.bHasAim?Point:FVector::ZeroVector;
		}
	}
	else if (IsLocallyControlled())
	{
		ClearLocalControlInput();
	}
	PumpHybridInput();
	if (UGuLiNetworkSettings::CanPresent(GetWorld()))
	{
		UpperBodyPivot->SetWorldRotation(FRotator(0,DisplayAimYaw-90.f,0));
		const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
		const bool bPoseActive=!bCarrierDormant && CombatHealth->IsAlive() && (!Hybrid || !Hybrid->IsBattlePaused());
		if (bPoseActive) UpdateUpperAimPresentation(DeltaSeconds);
		// Query replicas are not simulation owners; their pose still needs a real presentation clock.
		if (!UGuLiHybridActorComponent::HasSimulation(this) && bPoseActive)
		{
			if (auto* Anim=Cast<UGuLiGroundMechAnimInstance>(GetMesh()->GetAnimInstance())) Anim->SetUsePresentedUpperAim(true);
			GetMesh()->TickAnimation(DeltaSeconds,false); GetMesh()->RefreshBoneTransforms();
			if (auto* Anim=Cast<UGuLiGroundMechAnimInstance>(GetMesh()->GetAnimInstance())) Anim->SetUsePresentedUpperAim(false);
			if (!bIntegratedBodyMesh) { Machinegun->TickAnimation(DeltaSeconds,false); Machinegun->RefreshBoneTransforms(); }
		}
		else if (bPoseActive) RefreshUpperAimPresentationPose();
		Blink->Present();
		MechCameraController->Update(DeltaSeconds);
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
	Blink->AdvanceSimulation(DeltaSeconds);
	if (!GetBlinkState().Phase) AdvanceFacing(DeltaSeconds);
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
	Movement->AdvanceHybrid(DeltaSeconds,GetBlinkState().Phase?FVector2D::ZeroVector:WorldAxis,SimulationInput.bSprint);
	AdvanceUpperAim(DeltaSeconds);
	AdvanceWeaponAim(DeltaSeconds);
	UpperBodyPivot->SetWorldRotation(FRotator(0,DisplayAimYaw-90.f,0));
    const bool bForward=SimulationInput.WeaponAimMode==EGuLiGroundWeaponAimMode::Forward;
    const FVector FirePoint=bForward?GetMesh()->GetSocketLocation(TEXT("Gun_R"))+GetActorForwardVector()
        *(Weapon->ActiveSkill.ProjectileSpeed*Weapon->ActiveSkill.ProjectileLifetime):SimulationInput.Aim;
    Weapon->AdvanceHybrid(DeltaSeconds,SimulationInput.bFire && !GetBlinkState().Phase,FirePoint,bForward || SimulationInput.bHasAim);
    // Input precedes animation; only real Fire notifications enqueue shots. Read
    // the updated sockets after the pose, outside the montage's callback stack.
    GetMesh()->TickAnimation(DeltaSeconds,false); GetMesh()->RefreshBoneTransforms();
    if (!bIntegratedBodyMesh) { Machinegun->TickAnimation(DeltaSeconds,false); Machinegun->RefreshBoneTransforms(); }
    Weapon->TryFire();
	Blink->Present();
	// A logical step can run after the display tick. Restore only the display
	// overlay after firing; the next logical step always evaluates authority first.
	if (UGuLiNetworkSettings::CanPresent(GetWorld())) RefreshUpperAimPresentationPose();
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

void AGuLiGroundMechCharacter::ResetUpperAim(float Yaw)
{
	UpperAimState.Reset(Yaw); UpperAimPresentation=UpperAimState;
	const auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement());
	UpperAimDisplacementRevision=Move->GetDisplacementRevision();
}

void AGuLiGroundMechCharacter::RestoreUpperAim(const FGuLiGroundMechUpperAimState& State,double SampleSeconds)
{
	UpperAimState=State; UpperAimPresentation=State; UpperAimSampleSeconds=SampleSeconds;
	UpperAimDisplacementRevision=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement())->GetDisplacementRevision();
	UpperAimReceiptWorldSeconds=GetWorld()->GetTimeSeconds();
	const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	UpperAimReceiptAge=Hybrid?FMath::Max(0.,Hybrid->GetLogicalSeconds()-SampleSeconds):0.;
}

void AGuLiGroundMechCharacter::AdvanceUpperAim(float DeltaSeconds)
{
	if (!MobilityConfiguration.bReady) return;
	const auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement());
	if (!UpperAimState.bInitialized || UpperAimDisplacementRevision!=Move->GetDisplacementRevision())
		ResetUpperAim(GetActorRotation().Yaw);
	if (GetBlinkState().Phase)
	{
		UpperAimState.WorldYaw=FRotator::NormalizeAxis(GetActorRotation().Yaw); UpperAimState.Stop();
		UpperAimPresentation=UpperAimState; return;
	}
	const bool bForward=SimulationInput.WeaponAimMode==EGuLiGroundWeaponAimMode::Forward;
	const auto& M=MobilityConfiguration.Mobility;
	UpperAimState.Advance(DeltaSeconds,GetActorLocation(),bForward?GetActorLocation()+GetActorForwardVector():SimulationInput.Aim,
		bForward || SimulationInput.bHasAim,M.UpperBodyAimResponseSeconds,M.UpperBodyAimSnapToleranceDegrees);
}

void AGuLiGroundMechCharacter::UpdateUpperAimPresentation(float DeltaSeconds)
{
	if (!MobilityConfiguration.bReady || !UpperAimState.bInitialized) return;
	const auto& M=MobilityConfiguration.Mobility;
	if (GetBlinkState().Phase)
	{
		UpperAimPresentation=UpperAimState;
		UpperAimPresentation.WorldYaw=FRotator::NormalizeAxis(GetActorRotation().Yaw); UpperAimPresentation.Stop(); return;
	}
	if (UpperAimPresentation.Revision!=UpperAimState.Revision || !UpperAimPresentation.bInitialized)
		UpperAimPresentation=UpperAimState;
	if (IsLocallyControlled())
	{
		const bool bForward=HybridInput.WeaponAimMode==EGuLiGroundWeaponAimMode::Forward;
		UpperAimPresentation.Advance(DeltaSeconds,GetActorLocation(),bForward?GetActorLocation()+GetActorForwardVector():HybridInput.Aim,
			CanUseControls() && (bForward || HybridInput.bHasAim),M.UpperBodyAimResponseSeconds,M.UpperBodyAimSnapToleranceDegrees);
	}
	else
	{
		// Evaluate once from the received sample and its age, never filter a
		// filtered result a second time when a motion packet is applied.
		UpperAimPresentation=UpperAimState;
		const float Age=UGuLiHybridActorComponent::HasSimulation(this)?0.f:
			float(UpperAimReceiptAge+FMath::Max(0.,double(GetWorld()->GetTimeSeconds())-UpperAimReceiptWorldSeconds));
		UpperAimPresentation.Advance(Age,GetActorLocation(),UpperAimState.TargetPoint,UpperAimState.bHasTarget,
			M.UpperBodyAimResponseSeconds,M.UpperBodyAimSnapToleranceDegrees);
	}
}

void AGuLiGroundMechCharacter::RefreshUpperAimPresentationPose()
{
	if (!bIntegratedBodyMesh || bCarrierDormant || !CombatHealth->IsAlive()) return;
	auto* Anim=Cast<UGuLiGroundMechAnimInstance>(GetMesh()->GetAnimInstance());
	if (!Anim || !Anim->BeginUpperAimPresentation()) return;
	GetMesh()->TickAnimation(0.f,false); GetMesh()->RefreshBoneTransforms();
	Anim->EndUpperAimPresentation();
}

void AGuLiGroundMechCharacter::AdvanceFacing(float DeltaSeconds)
{
	if (!MobilityConfiguration.bReady) return;
	const auto& M=MobilityConfiguration.Mobility;
	const auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement());
	if (FacingDisplacementRevision!=Move->GetDisplacementRevision())
	{ FacingDisplacementRevision=Move->GetDisplacementRevision(); TurnState.Direction=0; ++TurnState.Serial; }
	const bool bFollowAim=SimulationInput.bFaceAim && SimulationInput.bHasAim;
	if (!bFollowAim)
	{ TurnState.Direction=0; TurnState.Progress=0; return; }
	const float TargetYaw=DisplayAimYaw;
	const float Error=FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,TargetYaw);
	const float StopDegrees=M.TurnStopDegrees;
	if (TurnState.Direction && (FMath::Abs(Error)<=StopDegrees || Error*TurnState.Direction<0.f))
	{
		TurnState.Direction=0; TurnState.Progress=0;
	}
	if (!TurnState.Direction)
	{
		const float StartDegrees=TurnState.Progress==1.f?M.TurnStopDegrees:M.TurnStartDegrees;
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
	// Standing, moving and airborne aim use one authored yaw curve and clock.
	// The movement rate caps that curve; camera zoom never changes the heading.
	const float PreviousProgress=TurnState.Progress;
	const float Curve=Sequence?Sequence->EvaluateCurveData(TEXT("GroundTurnProgress"),FAnimExtractContext(double(TurnState.Time*G.BasePlayRate))):0.f;
	TurnState.Progress=FMath::Clamp(Curve,TurnState.Progress,1.f);
	float DegreesThisTick=FMath::Abs(TurnState.TargetDegrees)*(TurnState.Progress-PreviousProgress);
	DegreesThisTick=FMath::Min(DegreesThisTick,M.MovingTurnRate*DeltaSeconds);
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

// Stable Character API used by Blueprints, animation and existing gameplay consumers.
bool AGuLiGroundMechCharacter::RequestBlink()
{
    if (!CanUseControls() || !IsShoulderLookActive() || !MobilityConfiguration.bReady) return false;
    bSprintHeld=false;
    // Enhanced Input has evaluated every action before dispatching Started callbacks.
    // Reading the action here avoids depending on the Move callback's order this frame.
    FVector2D Axis=FVector2D::ZeroVector;
    if (const auto* PC=Cast<APlayerController>(GetController()))
        if (const auto* Input=Cast<UEnhancedPlayerInput>(PC->PlayerInput); Input && MoveAction)
            Axis=Input->GetActionValue(MoveAction).Get<FVector2D>().GetClampedToMaxSize(1.f);
    const float CameraYaw=MechCameraController->GetViewYaw();
    const float DirectionYaw=Axis.IsNearlyZero()?CameraYaw:CameraYaw+FMath::RadiansToDegrees(FMath::Atan2(Axis.X,Axis.Y));
    return Blink->QueueRequest(HybridInput,FRotator::CompressAxisToShort(DirectionYaw));
}
const FGuLiGroundMechBlinkState& AGuLiGroundMechCharacter::GetBlinkState() const { return Blink->GetState(); }
uint8 AGuLiGroundMechCharacter::GetBlinkPhase() const { return Blink->GetState().Phase; }
float AGuLiGroundMechCharacter::GetBlinkPoseAlpha() const { return Blink->GetPoseAlpha(); }
bool AGuLiGroundMechCharacter::CanActivateBlink() const { return Blink->CanActivate(); }
float AGuLiGroundMechCharacter::GetBlinkCooldownRemaining() const { return Blink->GetCooldownRemaining(); }
float AGuLiGroundMechCharacter::GetBlinkCooldownDuration() const { return Blink->GetCooldownDuration(); }
void AGuLiGroundMechCharacter::ResetFacingForBlink(float Yaw)
{
	ResetUpperAim(Yaw);
    TurnState.Direction=0; TurnState.Time=TurnState.Progress=0.f;
    TurnState.StartYaw=Yaw; TurnState.TargetDegrees=0.f; ++TurnState.Serial;
    auto* Movement=GetCharacterMovement(); Movement->Velocity.X=Movement->Velocity.Y=0.f;
}
