// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeShip.h"
#include "GuLiStrikeShipParts.h"
#include "GuLiStrike.h"
#include "GuLiStrikeProjectile.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/World.h"

AGuLiStrikeShip::AGuLiStrikeShip()
{
	// the ship controls its own full 3D attitude
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// create the hull mesh; its sockets are the part hardpoints
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull Mesh"));
	HullMesh->SetupAttachment(GetRootComponent());
	HullMesh->SetCollisionProfileName(FName("NoCollision"));
	HullMesh->SetRelativeLocation(HullMeshOffset);

	// create the spring arm (chase camera behind and above the hull)
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArm->SetupAttachment(GetRootComponent());

	SpringArm->TargetArmLength = 3000.0f;
	SpringArm->SetRelativeRotation(FRotator(-18.0f, 0.0f, 0.0f));
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.0f;

	// create the camera
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);

	Camera->SetFieldOfView(90.0f);

	// configure free 6DOF flight
	GetCharacterMovement()->GravityScale = 0.0f;
	GetCharacterMovement()->bConstrainToPlane = false;
	GetCharacterMovement()->MaxFlySpeed = BaseMaxSpeed;
	GetCharacterMovement()->MaxAcceleration = BaseAcceleration;
	GetCharacterMovement()->BrakingDecelerationFlying = 200.0f;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 0.0f, 0.0f);
}

void AGuLiStrikeShip::BeginPlay()
{
	Super::BeginPlay();

	// switch to free flight
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);

	// install the spawn loadout
	for (const FGuLiStrikeShipDefaultPart& Entry : DefaultParts)
	{
		InstallPart(Entry.PartClass, Entry.SocketName);
	}

	// derive the initial flight stats
	RecomputeStats();
}

void AGuLiStrikeShip::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// add the ship mapping context so it does not fight the default character contexts
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

	// set up the enhanced input action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(ForwardAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustForward);
		EnhancedInputComponent->BindAction(BackwardAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustBackward);
		EnhancedInputComponent->BindAction(StrafeRightAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustRight);
		EnhancedInputComponent->BindAction(StrafeLeftAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustLeft);
		EnhancedInputComponent->BindAction(AscendAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustUp);
		EnhancedInputComponent->BindAction(DescendAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::ThrustDown);
		EnhancedInputComponent->BindAction(RollLeftAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::RollLeft);
		EnhancedInputComponent->BindAction(RollRightAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::RollRight);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::Look);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::BoostStart);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Completed, this, &AGuLiStrikeShip::BoostEnd);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &AGuLiStrikeShip::Fire);
		EnhancedInputComponent->BindAction(CycleEnginesAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::CycleEngines);
		EnhancedInputComponent->BindAction(CycleWeaponsAction, ETriggerEvent::Started, this, &AGuLiStrikeShip::CycleWeapons);
	}
}

void AGuLiStrikeShip::ThrustForward(const FInputActionValue& Value)
{
	// boost multiplies the thrust while held
	AddMovementInput(GetActorForwardVector(), bBoosting ? BoostThrustMultiplier : 1.0f);
}

void AGuLiStrikeShip::ThrustBackward(const FInputActionValue& Value)
{
	// boost multiplies the thrust while held
	AddMovementInput(GetActorForwardVector(), bBoosting ? -BoostThrustMultiplier : -1.0f);
}

void AGuLiStrikeShip::ThrustRight(const FInputActionValue& Value)
{
	// boost multiplies the thrust while held
	AddMovementInput(GetActorRightVector(), bBoosting ? BoostThrustMultiplier : 1.0f);
}

void AGuLiStrikeShip::ThrustLeft(const FInputActionValue& Value)
{
	// boost multiplies the thrust while held
	AddMovementInput(GetActorRightVector(), bBoosting ? -BoostThrustMultiplier : -1.0f);
}

void AGuLiStrikeShip::ThrustUp(const FInputActionValue& Value)
{
	// boost multiplies the thrust while held
	AddMovementInput(GetActorUpVector(), bBoosting ? BoostThrustMultiplier : 1.0f);
}

void AGuLiStrikeShip::ThrustDown(const FInputActionValue& Value)
{
	// boost multiplies the thrust while held
	AddMovementInput(GetActorUpVector(), bBoosting ? -BoostThrustMultiplier : -1.0f);
}

void AGuLiStrikeShip::RollLeft(const FInputActionValue& Value)
{
	// roll continuously while the input is held
	const float RollDelta = RollRate * GetWorld()->GetDeltaSeconds();
	AddActorLocalRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(RollDelta)), false, nullptr, ETeleportType::TeleportPhysics);
}

void AGuLiStrikeShip::RollRight(const FInputActionValue& Value)
{
	// roll continuously while the input is held
	const float RollDelta = RollRate * GetWorld()->GetDeltaSeconds();
	AddActorLocalRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-RollDelta)), false, nullptr, ETeleportType::TeleportPhysics);
}

void AGuLiStrikeShip::Look(const FInputActionValue& Value)
{
	// save the input vector
	const FVector2D InputVector = Value.Get<FVector2D>();

	// mouse right yaws right, mouse up pitches up (mouse Y delta is positive downwards)
	const FRotator LookDelta = FRotator(-InputVector.Y * MousePitchScale, InputVector.X * MouseYawScale, 0.0f);

	// rotate the ship around its own axes
	AddActorLocalRotation(LookDelta, false, nullptr, ETeleportType::TeleportPhysics);
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
	// validate the part class
	if (!PartClass)
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("InstallPart: null part class for socket %s"), *SocketName.ToString());
		return false;
	}

	// the socket must exist on the hull mesh
	if (!HullMesh || !HullMesh->DoesSocketExist(SocketName))
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("InstallPart: hull mesh has no socket %s"), *SocketName.ToString());
		return false;
	}

	// the part must list this socket as compatible
	const UGuLiStrikeShipPartComponent* PartCDO = Cast<UGuLiStrikeShipPartComponent>(PartClass->GetDefaultObject());
	if (!PartCDO || !PartCDO->CanAttachToSocket(SocketName))
	{
		UE_LOG(LogGuLiStrike, Warning, TEXT("InstallPart: %s is not compatible with socket %s"), *PartClass->GetName(), *SocketName.ToString());
		return false;
	}

	// replace whatever is installed on this socket
	UninstallPart(SocketName);

	// create the part component dynamically and attach it to the socket
	UGuLiStrikeShipPartComponent* Part = NewObject<UGuLiStrikeShipPartComponent>(this, PartClass);
	Part->RegisterComponent();
	Part->AttachToComponent(HullMesh, FAttachmentTransformRules::KeepRelativeTransform, SocketName);
	Part->SetRelativeTransform(PartCDO->PartRelativeTransform);

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
				Part->DestroyComponent();
			}

			InstalledParts.RemoveAt(Index);
			RecomputeStats();
			return true;
		}
	}

	return false;
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

	// gather the sockets that currently hold a part of this type
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
	// collect the catalogue entries of this type that may plug into the socket
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

	// find where the current part sits in the candidate list
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

	// advance to the next candidate (wraps around, so a single candidate reloads itself)
	const int32 NextIndex = (CurrentIndex + 1) % Candidates.Num();
	return InstallPart(Candidates[NextIndex], SocketName);
}

void AGuLiStrikeShip::FireInstalledWeapons()
{
	const float Now = GetWorld()->GetTimeSeconds();

	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		UGuLiStrikeWeaponPart* Weapon = Cast<UGuLiStrikeWeaponPart>(Entry.Part.Get());
		if (!Weapon || !Weapon->ProjectileClass)
		{
			continue;
		}

		// respect the part's fire rate
		const float FireInterval = 1.0f / FMath::Max(Weapon->FireRate, 0.01f);
		if (Now - Weapon->LastFireTime < FireInterval)
		{
			continue;
		}

		Weapon->LastFireTime = Now;

		// spawn the projectile from the part's muzzle
		FTransform MuzzleTransform = Weapon->GetComponentTransform();
		MuzzleTransform.SetLocation(MuzzleTransform.TransformPosition(Weapon->MuzzleOffset));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.Owner = this;

		GetWorld()->SpawnActor<AGuLiStrikeProjectile>(Weapon->ProjectileClass, MuzzleTransform, SpawnParameters);
	}
}

void AGuLiStrikeShip::RecomputeStats()
{
	// sum up thrust and mass over all installed parts
	float PartMassSum = 0.0f;
	float ThrustSum = 0.0f;
	for (const FGuLiStrikeInstalledPart& Entry : InstalledParts)
	{
		if (const UGuLiStrikeShipPartComponent* Part = Entry.Part.Get())
		{
			PartMassSum += Part->PartMass;

			if (const UGuLiStrikeEnginePart* Engine = Cast<UGuLiStrikeEnginePart>(Part))
			{
				ThrustSum += Engine->Thrust;
			}
		}
	}

	TotalThrust = ThrustSum;
	TotalMass = HullMass + PartMassSum;

	// thrust-to-mass ratio drives the speed multiplier around the nominal ratio
	const float ThrustRatio = TotalMass > 0.0f ? TotalThrust / TotalMass : 0.0f;
	const float SpeedMultiplier = FMath::Clamp(ThrustRatio / NominalThrustRatio, SpeedMultiplierMin, SpeedMultiplierMax);

	CurrentMaxSpeed = BaseMaxSpeed * SpeedMultiplier;

	// apply the stats to the movement component so hot-swaps take effect in flight
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxFlySpeed = CurrentMaxSpeed;
		Movement->MaxAcceleration = BaseAcceleration * SpeedMultiplier;
	}

	BP_OnStatsChanged();
}
