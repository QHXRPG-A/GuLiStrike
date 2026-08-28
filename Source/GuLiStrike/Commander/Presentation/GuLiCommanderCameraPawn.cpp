// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "LandscapeProxy.h"

namespace GuLiCommanderCamera
{
	constexpr float DefaultArmLength = 80000.0f;
	constexpr float MinimumArmLength = 20000.0f;
	constexpr float MaximumArmLength = 180000.0f;
	constexpr float BattlefieldHalfExtent = 380000.0f;
	constexpr float GroundTraceHalfHeight = 600000.0f;
	constexpr float PivotHeightAboveGround = 150.0f;
	constexpr float YawDegreesPerInput = 70.0f;
	constexpr float ZoomStepMultiplier = 1.18f;
	constexpr int32 MaximumIgnoredGroundObstacles = 16;
}

AGuLiCommanderCameraPawn::AGuLiCommanderCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CommanderSpringArm"));
	SpringArm->SetupAttachment(SceneRoot);
	SpringArm->TargetArmLength = GuLiCommanderCamera::DefaultArmLength;
	SpringArm->SetRelativeRotation(FRotator(-55.0f, 0.0f, 0.0f));
	SpringArm->bDoCollisionTest = true;
	SpringArm->ProbeChannel = ECC_Visibility;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 8.0f;

	PerspectiveCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PerspectiveCamera"));
	PerspectiveCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	PerspectiveCamera->ProjectionMode = ECameraProjectionMode::Perspective;
	PerspectiveCamera->FieldOfView = 45.0f;
	PerspectiveCamera->bUsePawnControlRotation = false;

	DesiredArmLength = GuLiCommanderCamera::DefaultArmLength;
}

void AGuLiCommanderCameraPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocallyControlled())
	{
		return;
	}

	if (FMath::Abs(PendingYawInput) > KINDA_SMALL_NUMBER)
	{
		FRotator Rotation = GetActorRotation();
		Rotation.Pitch = 0.0f;
		Rotation.Roll = 0.0f;
		Rotation.Yaw = FRotator::NormalizeAxis(
			Rotation.Yaw + PendingYawInput * GuLiCommanderCamera::YawDegreesPerInput);
		SetActorRotation(Rotation);
	}

	if (FMath::Abs(PendingZoomInput) > KINDA_SMALL_NUMBER)
	{
		DesiredArmLength *= FMath::Pow(
			GuLiCommanderCamera::ZoomStepMultiplier,
			PendingZoomInput);
		DesiredArmLength = FMath::Clamp(
			DesiredArmLength,
			GuLiCommanderCamera::MinimumArmLength,
			GuLiCommanderCamera::MaximumArmLength);
	}

	SpringArm->TargetArmLength = FMath::FInterpTo(
		SpringArm->TargetArmLength,
		DesiredArmLength,
		DeltaSeconds,
		8.0f);

	FVector Location = GetActorLocation();
	const FRotator PlanarRotation(0.0f, GetActorRotation().Yaw, 0.0f);
	const FVector Forward = PlanarRotation.RotateVector(FVector::ForwardVector);
	const FVector Right = PlanarRotation.RotateVector(FVector::RightVector);
	const float CameraHeight = SpringArm->TargetArmLength
		* FMath::Abs(FMath::Sin(FMath::DegreesToRadians(SpringArm->GetRelativeRotation().Pitch)));
	const float MoveSpeed = FMath::Clamp(
		CameraHeight * 1.4f,
		30000.0f,
		230000.0f);
	Location += (Forward * PendingPlanarMovement.X + Right * PendingPlanarMovement.Y) * MoveSpeed;
	Location.X = FMath::Clamp(
		Location.X,
		-GuLiCommanderCamera::BattlefieldHalfExtent,
		GuLiCommanderCamera::BattlefieldHalfExtent);
	Location.Y = FMath::Clamp(
		Location.Y,
		-GuLiCommanderCamera::BattlefieldHalfExtent,
		GuLiCommanderCamera::BattlefieldHalfExtent);

	float GroundZ = 0.0f;
	if (FindLandscapeHeight(Location, GroundZ))
	{
		Location.Z = FMath::FInterpTo(
			Location.Z,
			GroundZ + GuLiCommanderCamera::PivotHeightAboveGround,
			DeltaSeconds,
			12.0f);
	}
	SetActorLocation(Location);

	PendingPlanarMovement = FVector2D::ZeroVector;
	PendingYawInput = 0.0f;
	PendingZoomInput = 0.0f;
}

void AGuLiCommanderCameraPawn::AddPlanarMovement(const FVector2D Movement)
{
	if (!Movement.ContainsNaN())
	{
		PendingPlanarMovement += Movement;
	}
}

void AGuLiCommanderCameraPawn::AddYawInput(const float YawInput)
{
	if (FMath::IsFinite(YawInput))
	{
		PendingYawInput += YawInput;
	}
}

void AGuLiCommanderCameraPawn::AddZoomInput(const float ZoomInput)
{
	if (FMath::IsFinite(ZoomInput))
	{
		PendingZoomInput += ZoomInput;
	}
}

void AGuLiCommanderCameraPawn::JumpToWorldLocation(FVector WorldLocation)
{
	if (!IsLocallyControlled() || WorldLocation.ContainsNaN())
	{
		return;
	}

	WorldLocation.X = FMath::Clamp(
		WorldLocation.X,
		-GuLiCommanderCamera::BattlefieldHalfExtent,
		GuLiCommanderCamera::BattlefieldHalfExtent);
	WorldLocation.Y = FMath::Clamp(
		WorldLocation.Y,
		-GuLiCommanderCamera::BattlefieldHalfExtent,
		GuLiCommanderCamera::BattlefieldHalfExtent);
	float GroundZ = 0.0f;
	if (FindLandscapeHeight(WorldLocation, GroundZ))
	{
		WorldLocation.Z = GroundZ + GuLiCommanderCamera::PivotHeightAboveGround;
	}
	else
	{
		WorldLocation.Z = GetActorLocation().Z;
	}

	PendingPlanarMovement = FVector2D::ZeroVector;
	SetActorLocation(WorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

bool AGuLiCommanderCameraPawn::FindLandscapeHeight(
	const FVector& AtLocation,
	float& OutGroundZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector TraceStart(
		AtLocation.X,
		AtLocation.Y,
		AtLocation.Z + GuLiCommanderCamera::GroundTraceHalfHeight);
	const FVector TraceEnd(
		AtLocation.X,
		AtLocation.Y,
		AtLocation.Z - GuLiCommanderCamera::GroundTraceHalfHeight);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiCommanderCameraGround), true, this);
	FHitResult FirstFallbackHit;
	bool bHasFallbackHit = false;

	for (int32 Attempt = 0; Attempt < GuLiCommanderCamera::MaximumIgnoredGroundObstacles; ++Attempt)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(
			Hit,
			TraceStart,
			TraceEnd,
			ECC_Visibility,
			QueryParams))
		{
			break;
		}

		if (!bHasFallbackHit)
		{
			FirstFallbackHit = Hit;
			bHasFallbackHit = true;
		}

		if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
		{
			OutGroundZ = Hit.ImpactPoint.Z;
			return FMath::IsFinite(OutGroundZ);
		}

		if (AActor* HitActor = Hit.GetActor())
		{
			QueryParams.AddIgnoredActor(HitActor);
			continue;
		}
		break;
	}

	if (bHasFallbackHit)
	{
		OutGroundZ = FirstFallbackHit.ImpactPoint.Z;
		return FMath::IsFinite(OutGroundZ);
	}
	return false;
}
