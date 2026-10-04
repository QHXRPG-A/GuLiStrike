// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "GuLiCommanderCameraPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;

UENUM(BlueprintType)
enum class EGuLiCommanderCameraTier : uint8 { Near, Tactical, Overview };
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiCommanderCameraTierChanged, EGuLiCommanderCameraTier);

/** Non-shipping camera solver snapshot exposed to the GM console command. */
struct FGuLiCommanderCameraDebugSnapshot
{
	FBox2D LandscapeBounds = FBox2D(ForceInit);
	FVector PivotLocation = FVector::ZeroVector;
	FVector CameraLocation = FVector::ZeroVector;
	float GroundHeight = 0.0f;
	float PivotClearance = 0.0f;
	float MinimumBoomClearance = 0.0f;
	float CameraClearance = 0.0f;
	float DesiredArmLength = 0.0f;
	float EffectiveArmLength = 0.0f;
	float RequestedPlanarDistance = 0.0f;
	float AppliedPlanarDistance = 0.0f;
	float AppliedPlanarRatio = 1.0f;
	float HardRequiredPivotZ = 0.0f;
	float CruiseTargetPivotZ = 0.0f;
	float HeldCruisePivotZ = 0.0f;
	float EmergencyLiftAmount = 0.0f;
	uint32 EmergencyLiftCount = 0u;
	bool bLandscapeValid = false;
	bool bFootprintClamped = false;
	bool bTerrainValid = false;
	bool bRequestedPoseValid = false;
	bool bHeightReanchoring = false;
	bool bEmergencyLift = false;
};

/** Lightweight perspective RTS camera used by the commander prototype. */
// 相机 Pawn 仅与拥有者相关；位置不通过 ReplicateMovement 同步，输入/视角由本地控制端维护。
UCLASS()
class GULISTRIKE_API AGuLiCommanderCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AGuLiCommanderCameraPawn();

	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	EGuLiCommanderCameraTier GetCameraTier() const { return CameraTier; }
	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	bool IsCameraTransitioning() const { return bTransitioning; }
	bool IsOverviewPresentation() const { return CameraTier == EGuLiCommanderCameraTier::Overview; }
	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	float GetTargetHeightMeters() const { return bOverviewTarget ? OverviewTargetHeightMeters : TargetHeightMeters; }
	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	float GetActualHeightMeters() const { return ActualHeightMeters; }
	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	float GetPitchDegrees() const { return -SpringPitchDegrees; }
	void SetRequestedHeightMeters(float HeightMeters);
	bool HasCameraConfig() const { return bConfigLoaded; }
	const FGuLiStrikeCommanderCameraRow& GetCameraConfig() const { return Config; }
	FGuLiCommanderCameraTierChanged OnCameraTierChanged;

	/** X moves along camera-planar forward and Y along camera-planar right. */
	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void AddPlanarMovement(FVector2D Movement);

	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void AddYawInput(float YawInput);

	/** Negative zooms in and positive zooms out. */
	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void AddZoomInput(float ZoomInput);

	/** Local presentation-only camera focus jump, used by the tactical map. */
	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void JumpToWorldLocation(FVector WorldLocation);

	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	USpringArmComponent* GetCommanderSpringArm() const { return SpringArm; }

	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	UCameraComponent* GetCommanderCamera() const { return PerspectiveCamera; }

#if !UE_BUILD_SHIPPING
	void SetCameraDebugEnabled(bool bEnabled) { bCameraDebugEnabled = bEnabled; }
	bool IsCameraDebugEnabled() const { return bCameraDebugEnabled; }
	const FGuLiCommanderCameraDebugSnapshot& GetCameraDebugSnapshot() const { return DebugSnapshot; }
#endif

private:
	bool LoadConfig();
	float PitchForHeight(float HeightMeters) const;
	void SetTier(EGuLiCommanderCameraTier Tier);
	bool SolveNormalPose(FVector Focus, float Yaw, float HeightMeters, float DeltaSeconds,
		bool bImmediate, FVector& OutPivot, float& OutArm, float PreviousTerrainZ = 0);
	bool SolveOverviewPose(FVector& OutLocation, FRotator& OutRotation) const;
	void StartOverviewTransition(bool bEnter);
	void TickOverview(float DeltaSeconds);
	void ApplyCameraPose(const FVector& Location, const FRotator& Rotation);
	FVector CurrentCameraLocation() const;
	FRotator CurrentCameraRotation() const;
	FBox2D GetUsableViewRect(FVector2D& OutSize) const;
	FGuLiStrikeCommanderCameraRow Config;
	bool bConfigLoaded = false;
	bool bConfigErrorLogged = false;
	EGuLiCommanderCameraTier CameraTier = EGuLiCommanderCameraTier::Near;
	float TargetHeightMeters = 0;
	float SmoothedHeightMeters = 0;
	float ActualHeightMeters = 0;
	float OverviewTargetHeightMeters = 0;
	float SpringPitchDegrees = -55;
	bool bTransitioning = false;
	bool bOverviewTarget = false;
	float TransitionElapsed = 0;
	FVector TransitionFromLocation = FVector::ZeroVector;
	FRotator TransitionFromRotation = FRotator::ZeroRotator;
	FVector TransitionToLocation = FVector::ZeroVector;
	FRotator TransitionToRotation = FRotator::ZeroRotator;
	FVector ReturnFocus = FVector::ZeroVector;
	float ReturnYaw = 0;
	FVector ReturnPivot = FVector::ZeroVector;
	float ReturnArm = 0;
	FBox2D LastUsableViewRect = FBox2D(ForceInit);
	FVector2D LastViewSize = FVector2D::ZeroVector;
	uint32 LastLandscapeRevision = 0;
	bool FindCameraGroundHeight(const FVector& AtLocation, float& OutGroundZ) const;
	void InitializeSolver();
	void SimulateCameraStep(float StepSeconds, const FVector2D& MovementSeconds, float YawSeconds);
	bool ConstrainFocusToBattlefield(FVector& InOutFocus) const;
	bool CalculateRequiredPivotHeight(
		const FVector2D& PivotXY,
		float YawDegrees,
		float ArmLength,
		float& OutRequiredPivotZ,
		float* OutPivotGroundZ = nullptr) const;
	float CalculatePredictedRequiredPivotHeight(
		const FVector& PivotLocation,
		float YawDegrees,
		float ArmLength,
		const FVector2D& PlanarVelocity,
		float HardRequiredPivotZ) const;
	FVector CalculateCameraOffset(float YawDegrees, float ArmLength) const;
	void RefreshDebugSnapshot(bool bFootprintClamped, bool bTerrainValid);
#if !UE_BUILD_SHIPPING
	void DrawCameraDebug() const;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Commander|Camera")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Camera")
	TObjectPtr<UCameraComponent> PerspectiveCamera;

	FVector2D PendingPlanarMovement = FVector2D::ZeroVector;
	float PendingYawInput = 0.0f;
	float PendingZoomInput = 0.0f;
	float DesiredArmLength = 16000.0f;
	float HeldCruisePivotZ = 0.0f;
	bool bSolverInitialized = false;
#if !UE_BUILD_SHIPPING
	bool bCameraDebugEnabled = false;
	float LastRequestedPlanarDistance = 0.0f;
	float LastAppliedPlanarDistance = 0.0f;
	float LastHardRequiredPivotZ = 0.0f;
	float LastCruiseTargetPivotZ = 0.0f;
	float LastEmergencyLiftAmount = 0.0f;
	uint32 EmergencyLiftCount = 0u;
	bool bLastRequestedPoseValid = false;
	bool bEmergencyLiftActive = false;
	bool bEmergencyLiftThisFrame = false;
	FGuLiCommanderCameraDebugSnapshot DebugSnapshot;
#endif
};
