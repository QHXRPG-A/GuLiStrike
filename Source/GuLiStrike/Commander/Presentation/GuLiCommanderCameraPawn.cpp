// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCommanderCamera, Log, All);

namespace GuLiCommanderCamera
{
	constexpr float DefaultArmLength = 80000.0f;
	constexpr float MinimumArmLength = 20000.0f;
	constexpr float EmergencyMinimumArmLength = 1000.0f;
	constexpr float MaximumArmLength = 180000.0f;
	constexpr float PivotHeightAboveGround = 150.0f;
	constexpr float BoomHeightAboveGround = 200.0f;
	constexpr float CameraHeightAboveGround = 500.0f;
	constexpr float BoundaryPadding = 5000.0f;
	constexpr float BoomSampleSpacing = 10000.0f;
	constexpr float MaximumRiseSpeed = 30000.0f;
	constexpr float MaximumDescentSpeed = 15000.0f;
	constexpr float MaximumSubstepSeconds = 1.0f / 60.0f;
	constexpr int32 MaximumSubsteps = 8;
	constexpr int32 BinarySearchIterations = 8;
	constexpr int32 ArmFitIterations = 12;
	constexpr float YawDegreesPerInput = 70.0f;
	constexpr float ZoomStepMultiplier = 1.18f;
	constexpr float ZoomInterpolationSpeed = 8.0f;
	constexpr float CameraPitchDegrees = -55.0f;

	float InterpolateYaw(const float From, const float To, const float Alpha)
	{
		return FRotator::NormalizeAxis(From + FRotator::NormalizeAxis(To - From) * Alpha);
	}
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
	SpringArm->SetRelativeRotation(FRotator(GuLiCommanderCamera::CameraPitchDegrees, 0.0f, 0.0f));
	// Terrain clearance is solved explicitly. Collision tests would retract on props and soldiers.
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bEnableCameraLag = false;

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
	if (!bSolverInitialized)
	{
		InitializeSolver();
	}

	if (FMath::Abs(PendingZoomInput) > KINDA_SMALL_NUMBER)
	{
		DesiredArmLength = FMath::Clamp(
			DesiredArmLength * FMath::Pow(GuLiCommanderCamera::ZoomStepMultiplier, PendingZoomInput),
			GuLiCommanderCamera::MinimumArmLength,
			GuLiCommanderCamera::MaximumArmLength);
	}

	const float SafeDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
	const float SimulatedSeconds = FMath::Min(
		SafeDeltaSeconds,
		GuLiCommanderCamera::MaximumSubstepSeconds * static_cast<float>(GuLiCommanderCamera::MaximumSubsteps));
	if (SimulatedSeconds > UE_SMALL_NUMBER)
	{
		const int32 SubstepCount = FMath::Clamp(
			FMath::CeilToInt(SimulatedSeconds / GuLiCommanderCamera::MaximumSubstepSeconds),
			1,
			GuLiCommanderCamera::MaximumSubsteps);
		const float InputScale = SafeDeltaSeconds > UE_SMALL_NUMBER ? SimulatedSeconds / SafeDeltaSeconds : 0.0f;
		const FVector2D SimulatedMovement = PendingPlanarMovement * InputScale;
		const float SimulatedYaw = PendingYawInput * InputScale;
		const float StepSeconds = SimulatedSeconds / static_cast<float>(SubstepCount);
		for (int32 StepIndex = 0; StepIndex < SubstepCount; ++StepIndex)
		{
			SimulateCameraStep(
				StepSeconds,
				SimulatedMovement / static_cast<float>(SubstepCount),
				SimulatedYaw / static_cast<float>(SubstepCount));
		}
	}

	PendingPlanarMovement = FVector2D::ZeroVector;
	PendingYawInput = 0.0f;
	PendingZoomInput = 0.0f;
#if !UE_BUILD_SHIPPING
	if (bCameraDebugEnabled)
	{
		DrawCameraDebug();
	}
#endif
}

void AGuLiCommanderCameraPawn::InitializeSolver()
{
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw);
	FVector Pivot = GetActorLocation();
	float EffectiveArmLength = FMath::Clamp(
		SpringArm ? SpringArm->TargetArmLength : DesiredArmLength,
		GuLiCommanderCamera::EmergencyMinimumArmLength,
		GuLiCommanderCamera::MaximumArmLength);
	bool bFootprintClamped = false;
	const bool bLandscapeValid = ConstrainStateToLandscape(
		Pivot, Rotation.Yaw, EffectiveArmLength, EffectiveArmLength, bFootprintClamped);
	float RequiredPivotZ = Pivot.Z;
	const bool bTerrainValid = bLandscapeValid && CalculateRequiredPivotHeight(
		FVector2D(Pivot.X, Pivot.Y), Rotation.Yaw, EffectiveArmLength, RequiredPivotZ);
	if (bTerrainValid)
	{
		Pivot.Z = RequiredPivotZ;
	}
	SetActorLocationAndRotation(Pivot, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	SpringArm->TargetArmLength = EffectiveArmLength;
	bSolverInitialized = true;
#if !UE_BUILD_SHIPPING
	if (bCameraDebugEnabled)
	{
		RefreshDebugSnapshot(bFootprintClamped, bTerrainValid);
	}
#endif
}

void AGuLiCommanderCameraPawn::SimulateCameraStep(
	const float StepSeconds,
	const FVector2D& MovementSeconds,
	const float YawSeconds)
{
	const FVector CurrentPivot = GetActorLocation();
	const float CurrentYaw = FRotator::NormalizeAxis(GetActorRotation().Yaw);
	const float CurrentArmLength = SpringArm->TargetArmLength;
	const float RequestedYaw = FRotator::NormalizeAxis(
		CurrentYaw + YawSeconds * GuLiCommanderCamera::YawDegreesPerInput);
	const float RequestedArmLength = FMath::FInterpTo(
		CurrentArmLength, DesiredArmLength, StepSeconds, GuLiCommanderCamera::ZoomInterpolationSpeed);

	FVector RequestedPivot = CurrentPivot;
	const FRotator PlanarRotation(0.0f, RequestedYaw, 0.0f);
	const FVector Forward = PlanarRotation.RotateVector(FVector::ForwardVector);
	const FVector Right = PlanarRotation.RotateVector(FVector::RightVector);
	const float CameraHeight = CurrentArmLength * FMath::Abs(
		FMath::Sin(FMath::DegreesToRadians(GuLiCommanderCamera::CameraPitchDegrees)));
	const float MoveSpeed = FMath::Clamp(CameraHeight * 1.4f, 30000.0f, 230000.0f);
	RequestedPivot += (Forward * MovementSeconds.X + Right * MovementSeconds.Y) * MoveSpeed;

	FVector CandidatePivot = RequestedPivot;
	float CandidateArmLength = RequestedArmLength;
	bool bFootprintClamped = false;
	bool bCandidateValid = ConstrainStateToLandscape(
		CandidatePivot, RequestedYaw, RequestedArmLength, CandidateArmLength, bFootprintClamped);
	float CandidateRequiredZ = CurrentPivot.Z;
	bCandidateValid = bCandidateValid && CalculateRequiredPivotHeight(
		FVector2D(CandidatePivot.X, CandidatePivot.Y),
		RequestedYaw,
		CandidateArmLength,
		CandidateRequiredZ);

	const float MaximumAllowedZ = CurrentPivot.Z + GuLiCommanderCamera::MaximumRiseSpeed * StepSeconds;
	float ChosenYaw = RequestedYaw;
	if (!bCandidateValid || CandidateRequiredZ > MaximumAllowedZ)
	{
		// Shorten the whole pan/turn/zoom transition when its terrain rise cannot be paid this step.
		float LowerAlpha = 0.0f;
		float UpperAlpha = 1.0f;
		FVector BestPivot = CurrentPivot;
		float BestYaw = CurrentYaw;
		float BestArmLength = CurrentArmLength;
		float BestRequiredZ = CurrentPivot.Z;
		bool bFoundSafeTransition = CalculateRequiredPivotHeight(
			FVector2D(CurrentPivot.X, CurrentPivot.Y),
			CurrentYaw,
			CurrentArmLength,
			BestRequiredZ)
			&& BestRequiredZ <= MaximumAllowedZ;

		for (int32 SearchIndex = 0; SearchIndex < GuLiCommanderCamera::BinarySearchIterations; ++SearchIndex)
		{
			const float Alpha = (LowerAlpha + UpperAlpha) * 0.5f;
			FVector TestPivot = FMath::Lerp(CurrentPivot, RequestedPivot, Alpha);
			const float TestYaw = GuLiCommanderCamera::InterpolateYaw(CurrentYaw, RequestedYaw, Alpha);
			float TestArmLength = FMath::Lerp(CurrentArmLength, RequestedArmLength, Alpha);
			bool bTestClamped = false;
			float TestRequiredZ = CurrentPivot.Z;
			const bool bTestValid = ConstrainStateToLandscape(
				TestPivot, TestYaw, TestArmLength, TestArmLength, bTestClamped)
				&& CalculateRequiredPivotHeight(
					FVector2D(TestPivot.X, TestPivot.Y), TestYaw, TestArmLength, TestRequiredZ)
				&& TestRequiredZ <= MaximumAllowedZ;
			if (bTestValid)
			{
				LowerAlpha = Alpha;
				BestPivot = TestPivot;
				BestYaw = TestYaw;
				BestArmLength = TestArmLength;
				BestRequiredZ = TestRequiredZ;
				bFoundSafeTransition = true;
				bFootprintClamped |= bTestClamped;
			}
			else
			{
				UpperAlpha = Alpha;
			}
		}

		if (bFoundSafeTransition)
		{
			CandidatePivot = BestPivot;
			ChosenYaw = BestYaw;
			CandidateArmLength = BestArmLength;
			CandidateRequiredZ = BestRequiredZ;
			bCandidateValid = true;
		}
		else
		{
			CandidatePivot = CurrentPivot;
			ChosenYaw = CurrentYaw;
			CandidateArmLength = CurrentArmLength;
			bCandidateValid = CalculateRequiredPivotHeight(
				FVector2D(CurrentPivot.X, CurrentPivot.Y),
				CurrentYaw,
				CurrentArmLength,
				CandidateRequiredZ);
		}
	}

	if (bCandidateValid)
	{
		CandidatePivot.Z = CandidateRequiredZ >= CurrentPivot.Z
			? FMath::Min(CandidateRequiredZ, MaximumAllowedZ)
			: FMath::Max(
				CandidateRequiredZ,
				CurrentPivot.Z - GuLiCommanderCamera::MaximumDescentSpeed * StepSeconds);
	}
	else
	{
		CandidatePivot = CurrentPivot;
		ChosenYaw = CurrentYaw;
		CandidateArmLength = CurrentArmLength;
	}

	SetActorLocationAndRotation(
		CandidatePivot,
		FRotator(0.0f, ChosenYaw, 0.0f),
		false,
		nullptr,
		ETeleportType::None);
	SpringArm->TargetArmLength = CandidateArmLength;
#if !UE_BUILD_SHIPPING
	if (bCameraDebugEnabled)
	{
		RefreshDebugSnapshot(bFootprintClamped, bCandidateValid);
	}
#endif
}

bool AGuLiCommanderCameraPawn::ConstrainStateToLandscape(
	FVector& InOutPivot,
	const float YawDegrees,
	const float RequestedArmLength,
	float& OutArmLength,
	bool& OutClamped) const
{
	OutClamped = false;
	const UWorld* World = GetWorld();
	const UGuLiCommanderLandscapeQuerySubsystem* LandscapeQuery = World
		? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()
		: nullptr;
	FBox2D LandscapeBounds(ForceInit);
	if (!LandscapeQuery || !LandscapeQuery->TryGetBounds(LandscapeBounds))
	{
		return false;
	}
	const FBox2D InnerBounds(
		LandscapeBounds.Min + FVector2D(GuLiCommanderCamera::BoundaryPadding),
		LandscapeBounds.Max - FVector2D(GuLiCommanderCamera::BoundaryPadding));
	if (!InnerBounds.bIsValid || InnerBounds.GetSize().GetMin() <= 1.0)
	{
		return false;
	}

	const auto TryFootprint = [this, YawDegrees, &InnerBounds](const float ArmLength, FBox2D& OutOffsets)
	{
		return CalculateFootprintOffsets(YawDegrees, ArmLength, OutOffsets)
			&& OutOffsets.GetSize().X <= InnerBounds.GetSize().X
			&& OutOffsets.GetSize().Y <= InnerBounds.GetSize().Y;
	};
	OutArmLength = FMath::Clamp(
		RequestedArmLength,
		GuLiCommanderCamera::EmergencyMinimumArmLength,
		GuLiCommanderCamera::MaximumArmLength);
	FBox2D FootprintOffsets(ForceInit);
	if (!TryFootprint(OutArmLength, FootprintOffsets))
	{
		float LowerArm = GuLiCommanderCamera::EmergencyMinimumArmLength;
		float UpperArm = OutArmLength;
		if (!TryFootprint(LowerArm, FootprintOffsets))
		{
			return false;
		}
		for (int32 Index = 0; Index < GuLiCommanderCamera::ArmFitIterations; ++Index)
		{
			const float TestArm = (LowerArm + UpperArm) * 0.5f;
			FBox2D TestOffsets(ForceInit);
			if (TryFootprint(TestArm, TestOffsets))
			{
				LowerArm = TestArm;
				FootprintOffsets = TestOffsets;
			}
			else
			{
				UpperArm = TestArm;
			}
		}
		OutArmLength = LowerArm;
		OutClamped = true;
	}

	const FVector2D AllowedMinimum = InnerBounds.Min - FootprintOffsets.Min;
	const FVector2D AllowedMaximum = InnerBounds.Max - FootprintOffsets.Max;
	if (AllowedMinimum.X > AllowedMaximum.X || AllowedMinimum.Y > AllowedMaximum.Y)
	{
		return false;
	}
	const FVector2D OriginalPivot(InOutPivot.X, InOutPivot.Y);
	const FVector2D ClampedPivot(
		FMath::Clamp(OriginalPivot.X, AllowedMinimum.X, AllowedMaximum.X),
		FMath::Clamp(OriginalPivot.Y, AllowedMinimum.Y, AllowedMaximum.Y));
	InOutPivot.X = ClampedPivot.X;
	InOutPivot.Y = ClampedPivot.Y;
	OutClamped |= !OriginalPivot.Equals(ClampedPivot, 0.01);
	return true;
}

bool AGuLiCommanderCameraPawn::CalculateRequiredPivotHeight(
	const FVector2D& PivotXY,
	const float YawDegrees,
	const float ArmLength,
	float& OutRequiredPivotZ,
	float* OutPivotGroundZ) const
{
	float PivotGroundZ = 0.0f;
	if (!FindLandscapeHeight(FVector(PivotXY.X, PivotXY.Y, 0.0), PivotGroundZ))
	{
		return false;
	}
	if (OutPivotGroundZ)
	{
		*OutPivotGroundZ = PivotGroundZ;
	}
	OutRequiredPivotZ = PivotGroundZ + GuLiCommanderCamera::PivotHeightAboveGround;
	const FVector CameraOffset = CalculateCameraOffset(YawDegrees, ArmLength);
	const int32 SegmentCount = FMath::Max(
		1, FMath::CeilToInt(ArmLength / GuLiCommanderCamera::BoomSampleSpacing));
	for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
	{
		const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
		const FVector RelativeSample = CameraOffset * Alpha;
		float SampleGroundZ = 0.0f;
		if (!FindLandscapeHeight(
			FVector(PivotXY.X + RelativeSample.X, PivotXY.Y + RelativeSample.Y, 0.0),
			SampleGroundZ))
		{
			return false;
		}
		const float RequiredClearance = SegmentIndex == SegmentCount
			? GuLiCommanderCamera::CameraHeightAboveGround
			: GuLiCommanderCamera::BoomHeightAboveGround;
		OutRequiredPivotZ = FMath::Max(
			OutRequiredPivotZ,
			SampleGroundZ + RequiredClearance - RelativeSample.Z);
	}
	return FMath::IsFinite(OutRequiredPivotZ);
}

bool AGuLiCommanderCameraPawn::CalculateFootprintOffsets(
	const float YawDegrees,
	const float ArmLength,
	FBox2D& OutOffsets) const
{
	OutOffsets = FBox2D(ForceInit);
	if (!FMath::IsFinite(YawDegrees) || !FMath::IsFinite(ArmLength) || ArmLength <= 0.0f)
	{
		return false;
	}
	double AspectRatio = 16.0 / 9.0;
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
		if (ViewportWidth > 0 && ViewportHeight > 0)
		{
			AspectRatio = static_cast<double>(ViewportWidth) / static_cast<double>(ViewportHeight);
		}
	}
	AspectRatio = FMath::Clamp(AspectRatio, 0.25, 8.0);
	const double HorizontalHalfFov = FMath::DegreesToRadians(
		PerspectiveCamera ? PerspectiveCamera->FieldOfView * 0.5 : 22.5);
	const double HorizontalTangent = FMath::Tan(HorizontalHalfFov);
	const double VerticalTangent = HorizontalTangent / AspectRatio;
	const FRotationMatrix Rotation(FRotator(GuLiCommanderCamera::CameraPitchDegrees, YawDegrees, 0.0f));
	const FVector Forward = Rotation.GetScaledAxis(EAxis::X);
	const FVector Right = Rotation.GetScaledAxis(EAxis::Y);
	const FVector Up = Rotation.GetScaledAxis(EAxis::Z);
	const FVector CameraOffset = -Forward * ArmLength;
	const double FocusPlaneZ = -static_cast<double>(GuLiCommanderCamera::PivotHeightAboveGround);
	OutOffsets += FVector2D::ZeroVector;
	OutOffsets += FVector2D(CameraOffset.X, CameraOffset.Y);
	for (int32 HorizontalSign = -1; HorizontalSign <= 1; HorizontalSign += 2)
	{
		for (int32 VerticalSign = -1; VerticalSign <= 1; VerticalSign += 2)
		{
			const FVector RayDirection = (
				Forward
				+ Right * (HorizontalTangent * static_cast<double>(HorizontalSign))
				+ Up * (VerticalTangent * static_cast<double>(VerticalSign))).GetSafeNormal();
			if (RayDirection.Z >= -UE_SMALL_NUMBER)
			{
				return false;
			}
			const double Distance = (FocusPlaneZ - CameraOffset.Z) / RayDirection.Z;
			if (Distance <= 0.0 || !FMath::IsFinite(Distance))
			{
				return false;
			}
			const FVector GroundPoint = CameraOffset + RayDirection * Distance;
			if (GroundPoint.ContainsNaN())
			{
				return false;
			}
			OutOffsets += FVector2D(GroundPoint.X, GroundPoint.Y);
		}
	}
	return OutOffsets.bIsValid;
}

FVector AGuLiCommanderCameraPawn::CalculateCameraOffset(
	const float YawDegrees,
	const float ArmLength) const
{
	return -FRotator(GuLiCommanderCamera::CameraPitchDegrees, YawDegrees, 0.0f).Vector() * ArmLength;
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
	if (!bSolverInitialized)
	{
		InitializeSolver();
	}
	const float Yaw = FRotator::NormalizeAxis(GetActorRotation().Yaw);
	float EffectiveArmLength = SpringArm->TargetArmLength;
	bool bFootprintClamped = false;
	const bool bLandscapeValid = ConstrainStateToLandscape(
		WorldLocation, Yaw, EffectiveArmLength, EffectiveArmLength, bFootprintClamped);
	float RequiredPivotZ = GetActorLocation().Z;
	const bool bTerrainValid = bLandscapeValid && CalculateRequiredPivotHeight(
		FVector2D(WorldLocation.X, WorldLocation.Y), Yaw, EffectiveArmLength, RequiredPivotZ);
	if (!bTerrainValid)
	{
		return;
	}
	WorldLocation.Z = RequiredPivotZ;
	PendingPlanarMovement = FVector2D::ZeroVector;
	PendingYawInput = 0.0f;
	PendingZoomInput = 0.0f;
	SpringArm->TargetArmLength = EffectiveArmLength;
	SetActorLocation(WorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
#if !UE_BUILD_SHIPPING
	if (bCameraDebugEnabled)
	{
		RefreshDebugSnapshot(bFootprintClamped, true);
	}
#endif
}

bool AGuLiCommanderCameraPawn::FindLandscapeHeight(
	const FVector& AtLocation,
	float& OutGroundZ) const
{
	const UWorld* World = GetWorld();
	const UGuLiCommanderLandscapeQuerySubsystem* LandscapeQuery = World
		? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()
		: nullptr;
	return LandscapeQuery
		&& LandscapeQuery->TryGetLandscapeHeight(FVector2D(AtLocation.X, AtLocation.Y), OutGroundZ);
}

void AGuLiCommanderCameraPawn::RefreshDebugSnapshot(
	const bool bFootprintClamped,
	const bool bTerrainValid)
{
#if !UE_BUILD_SHIPPING
	DebugSnapshot = FGuLiCommanderCameraDebugSnapshot();
	DebugSnapshot.PivotLocation = GetActorLocation();
	DebugSnapshot.DesiredArmLength = DesiredArmLength;
	DebugSnapshot.EffectiveArmLength = SpringArm ? SpringArm->TargetArmLength : 0.0f;
	DebugSnapshot.bFootprintClamped = bFootprintClamped;
	DebugSnapshot.bTerrainValid = bTerrainValid;
	const UWorld* World = GetWorld();
	const UGuLiCommanderLandscapeQuerySubsystem* LandscapeQuery = World
		? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()
		: nullptr;
	DebugSnapshot.bLandscapeValid = LandscapeQuery
		&& LandscapeQuery->TryGetBounds(DebugSnapshot.LandscapeBounds);
	if (!LandscapeQuery)
	{
		return;
	}
	const float Yaw = GetActorRotation().Yaw;
	const FVector CameraOffset = CalculateCameraOffset(Yaw, DebugSnapshot.EffectiveArmLength);
	DebugSnapshot.CameraLocation = DebugSnapshot.PivotLocation + CameraOffset;
	float GroundZ = 0.0f;
	if (LandscapeQuery->TryGetLandscapeHeight(
		FVector2D(DebugSnapshot.PivotLocation.X, DebugSnapshot.PivotLocation.Y), GroundZ))
	{
		DebugSnapshot.GroundHeight = GroundZ;
		DebugSnapshot.PivotClearance = DebugSnapshot.PivotLocation.Z - GroundZ;
	}
	DebugSnapshot.MinimumBoomClearance = TNumericLimits<float>::Max();
	const int32 SegmentCount = FMath::Max(
		1, FMath::CeilToInt(DebugSnapshot.EffectiveArmLength / GuLiCommanderCamera::BoomSampleSpacing));
	for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
	{
		const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
		const FVector Sample = DebugSnapshot.PivotLocation + CameraOffset * Alpha;
		if (!LandscapeQuery->TryGetLandscapeHeight(FVector2D(Sample.X, Sample.Y), GroundZ))
		{
			continue;
		}
		const float Clearance = Sample.Z - GroundZ;
		if (SegmentIndex == SegmentCount)
		{
			DebugSnapshot.CameraClearance = Clearance;
		}
		else
		{
			DebugSnapshot.MinimumBoomClearance = FMath::Min(DebugSnapshot.MinimumBoomClearance, Clearance);
		}
	}
	if (DebugSnapshot.MinimumBoomClearance == TNumericLimits<float>::Max())
	{
		DebugSnapshot.MinimumBoomClearance = DebugSnapshot.CameraClearance;
	}
#endif
}

#if !UE_BUILD_SHIPPING
void AGuLiCommanderCameraPawn::DrawCameraDebug() const
{
	const FString Text = FString::Printf(
		TEXT("CommanderCamera valid=%d terrain=%d clamped=%d\n")
		TEXT("bounds=[%.0f %.0f]-[%.0f %.0f]\n")
		TEXT("pivot=(%.0f %.0f %.0f) ground=%.0f clear=%.0f\n")
		TEXT("camera=(%.0f %.0f %.0f) boomMin=%.0f cameraClear=%.0f arm=%.0f/%.0f"),
		DebugSnapshot.bLandscapeValid ? 1 : 0,
		DebugSnapshot.bTerrainValid ? 1 : 0,
		DebugSnapshot.bFootprintClamped ? 1 : 0,
		DebugSnapshot.LandscapeBounds.Min.X,
		DebugSnapshot.LandscapeBounds.Min.Y,
		DebugSnapshot.LandscapeBounds.Max.X,
		DebugSnapshot.LandscapeBounds.Max.Y,
		DebugSnapshot.PivotLocation.X,
		DebugSnapshot.PivotLocation.Y,
		DebugSnapshot.PivotLocation.Z,
		DebugSnapshot.GroundHeight,
		DebugSnapshot.PivotClearance,
		DebugSnapshot.CameraLocation.X,
		DebugSnapshot.CameraLocation.Y,
		DebugSnapshot.CameraLocation.Z,
		DebugSnapshot.MinimumBoomClearance,
		DebugSnapshot.CameraClearance,
		DebugSnapshot.EffectiveArmLength,
		DebugSnapshot.DesiredArmLength);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-static_cast<int32>(GetUniqueID()), 0.0f, FColor::Cyan, Text);
	}
	if (UWorld* World = GetWorld(); DebugSnapshot.LandscapeBounds.bIsValid)
	{
		const FVector2D Min = DebugSnapshot.LandscapeBounds.Min;
		const FVector2D Max = DebugSnapshot.LandscapeBounds.Max;
		const float Z = DebugSnapshot.PivotLocation.Z;
		DrawDebugLine(World, FVector(Min.X, Min.Y, Z), FVector(Max.X, Min.Y, Z), FColor::Cyan, false, 0.0f, 0, 20.0f);
		DrawDebugLine(World, FVector(Max.X, Min.Y, Z), FVector(Max.X, Max.Y, Z), FColor::Cyan, false, 0.0f, 0, 20.0f);
		DrawDebugLine(World, FVector(Max.X, Max.Y, Z), FVector(Min.X, Max.Y, Z), FColor::Cyan, false, 0.0f, 0, 20.0f);
		DrawDebugLine(World, FVector(Min.X, Max.Y, Z), FVector(Min.X, Min.Y, Z), FColor::Cyan, false, 0.0f, 0, 20.0f);
		DrawDebugLine(World, DebugSnapshot.PivotLocation, DebugSnapshot.CameraLocation, FColor::Yellow, false, 0.0f, 0, 12.0f);
	}
}

namespace GuLiCommanderCameraDebugCommand
{
	void ToggleDebug(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
		{
			UE_LOG(LogGuLiCommanderCamera, Warning, TEXT("Usage: gs.GM.Commander.Camera.Debug <0|1>"));
			return;
		}
		const bool bEnabled = Args[0] == TEXT("1");
		int32 UpdatedCount = 0;
		for (TActorIterator<AGuLiCommanderCameraPawn> It(World); It; ++It)
		{
			if (It->IsLocallyControlled())
			{
				It->SetCameraDebugEnabled(bEnabled);
				++UpdatedCount;
			}
		}
		UE_LOG(LogGuLiCommanderCamera, Display, TEXT("Commander camera debug %s for %d local camera(s)."),
			bEnabled ? TEXT("enabled") : TEXT("disabled"), UpdatedCount);
	}

	FAutoConsoleCommandWithWorldAndArgs CameraDebugCommand(
		TEXT("gs.GM.Commander.Camera.Debug"),
		TEXT("Toggle commander camera bounds and terrain-clearance debug: <0|1>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ToggleDebug));
}
#endif
