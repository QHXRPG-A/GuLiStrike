#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

enum class EGuLiMechanicalModel : uint8 { None, WarMachine, Sweeper };

/** Authored static sockets and per-type tuning, resolved once with the unit definition.
 * All CPU points/distances are final gameplay centimeters; UV pivots remain mesh centimeters.
 */
struct GULISTRIKE_API FGuLiMechanicalAnimationConfig
{
	EGuLiMechanicalModel Model = EGuLiMechanicalModel::None;
	float ModelScale = 1.0f;
	FVector UpperPivot = FVector::ZeroVector;
	FVector GunPivots[2] = {};
	FVector WheelPivots[4] = {};
	float WheelRadii[4] = {};
	FVector DiscPivots[4] = {};
	FVector HoverNozzles[4] = {};
	float DiscDiameters[4] = {};
	bool bHasHoverNozzles = false;
	FTransform BasicMuzzles[2] = {FTransform::Identity, FTransform::Identity};
	FTransform MissileMuzzles[2] = {FTransform::Identity, FTransform::Identity};
	float BaseMoveSpeed = 720.0f;
	float UpperTurnRate = 180.0f;
	float LowerTurnRate = 90.0f;
	float PitchRate = 180.0f;
	float MinimumPitch = -15.0f;
	float MaximumPitch = 45.0f;
	float MinimumDiscTilt = 5.0f;
	float MaximumDiscTilt = 15.0f;
	float DiscTiltTransitionSeconds = 0.20f;
	float RecoilDistance = 35.0f;
	float RecoilKickSeconds = 0.04f;
	float RecoilReturnSeconds = 0.20f;
	float HoverHeight = 120.0f;
	float HoverBobAmplitude = 12.0f;
	float HoverPitchAmplitude = 1.2f;
	float HoverRollAmplitude = 1.2f;
	float HoverBobPeriod = 3.0f;
	float HoverPitchPeriod = 3.7f;
	float HoverRollPeriod = 4.3f;
	float HoverBlendSeconds = 0.35f;
	float HoverIdleEnterSpeed = 5.0f;
	float HoverIdleExitSpeed = 15.0f;
	float HoverIdleHoldSeconds = 0.2f;
	// Presentation-only turn rig. Socket positions use final game centimeters.
	FVector LegRoots[4] = {}, LegEnds[4] = {}, LegAxes[4] = {};
	bool bHasTurnRig = false;
	float VisualYawDamping = 3.0f;
	float VisualYawSnapDegrees = 0.25f;
	float TurnFullBankRate = 90.0f;
	float TurnRateDeadZone = 2.0f;
	float TurnDiscDegrees = 12.0f, TurnLegDegrees = 8.0f, TurnUpperDegrees = 15.0f;
	float TurnDiscEnterSeconds = 0.20f, TurnLegEnterSeconds = 0.30f, TurnUpperEnterSeconds = 0.45f;
	float TurnDiscReturnSeconds = 0.35f, TurnLegReturnSeconds = 0.45f, TurnUpperReturnSeconds = 0.70f;
	bool IsEnabled() const { return Model != EGuLiMechanicalModel::None; }
	static FGuLiMechanicalAnimationConfig FromStaticMesh(const UStaticMesh* Mesh, float Scale);
};

/** Authority supplies aim; rendering clients integrate wheels/tilt and accepted recoil cues. */
struct GULISTRIKE_API FGuLiMechanicalAnimationState
{
	float UpperYawDegrees = 0.0f;
	float GunPitchDegrees[2] = {};
	float RecoilStartTime[2] = {-1000.0f, -1000.0f};
	float RecoilFromCentimeters[2] = {};
	float WheelRadians[4] = {};
	FVector2D DiscTiltRadians = FVector2D::ZeroVector;
	FVector2D DiscTiltFrom = FVector2D::ZeroVector;
	FVector2D DiscTiltTarget = FVector2D::ZeroVector;
	float DiscTiltElapsed = 0.0f;
	// Only these three transition values are replicated. Waves use simulation time + stable ID.
	uint32 HoverBlendStartMilliseconds = 0;
	uint8 HoverBlendFromWeight = 0;
	bool bHoverIdleTarget = false;
	// Authority-only hysteresis; the evaluated pose is local to each simulation/render clock.
	float HoverStoppedSeconds = 0.0f;
	bool bHoverInitialized = false;
	float HoverBobCentimeters = 0.0f;
	float HoverPitchRadians = 0.0f;
	float HoverRollRadians = 0.0f;
	bool bInitialized = false;
};

/** Local rendering state only; never put this in the authority pose or network codec. */
struct GULISTRIKE_API FGuLiMechanicalVisualState
{
	FTransform Root = FTransform::Identity;
	float UpperYawDegrees = 0;
	float YawRate = 0;
	float DiscBankRadians = 0, LegBankRadians = 0, UpperBankRadians = 0;
	FQuat GunRotation[2] = {FQuat::Identity, FQuat::Identity};
	double PoseTime = 0;
	bool bInitialized = false;
};

/** GPU v4: legacy slots 0..30 stay fixed; new current pose 31..40, previous 41..50. */
struct GULISTRIKE_API FGuLiMechanicalAnimationFrame
{
	static constexpr int32 LegacyPoseFloats = 14;
	static constexpr int32 PoseFloats = 24;
	static constexpr int32 CustomDataFloats = 3 + 2 * PoseFloats;
	static constexpr int32 PoseIndex(int32 Index, bool bPrevious)
	{
		return Index < LegacyPoseFloats ? 1 + Index + (bPrevious ? LegacyPoseFloats : 0)
			: 31 + Index - LegacyPoseFloats + (bPrevious ? PoseFloats - LegacyPoseFloats : 0);
	}
	float Values[PoseFloats] = {};
	float MissilePodVisible = 0.0f;
};

namespace GuLiMechanicalAnimation
{
	/** Uses the monotonic presentation clock, including resets; does not mutate LogicalPose/Aim. */
	GULISTRIKE_API void StepVisualTurn(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& Aim, const FTransform& LogicalPose,
		double PoseTime, bool bReset, FGuLiMechanicalVisualState& Visual, float YawResponseScale = 1.0f);
	GULISTRIKE_API FGuLiMechanicalAnimationFrame BuildVisualFrame(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, const FGuLiMechanicalVisualState& Visual, float Time);
	GULISTRIKE_API bool ResolveVisualMuzzle(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, const FGuLiMechanicalVisualState& Visual,
		FName Slot, int32 Side, FTransform& Out);
	GULISTRIKE_API bool ResolveVisualNozzle(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, const FGuLiMechanicalVisualState& Visual,
		int32 Disc, FTransform& Out);
	GULISTRIKE_API float HoverWeight(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, double SimulationSeconds);
	GULISTRIKE_API void StepHover(const FGuLiMechanicalAnimationConfig& Config, uint32 StableId,
		float HorizontalSpeed, float DeltaSeconds, double SimulationSeconds, FGuLiMechanicalAnimationState& State);
	GULISTRIKE_API void EvaluateHover(const FGuLiMechanicalAnimationConfig& Config, uint32 StableId,
		double SimulationSeconds, FGuLiMechanicalAnimationState& State);
	/** Final game-centimeter transform, applied after every local mechanical part transform. */
	GULISTRIKE_API FTransform HoverBodyTransform(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State);
	/** +X of the result is exactly the negative animated disc normal. */
	GULISTRIKE_API bool ResolveHoverNozzle(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, const FTransform& LogicalPose, int32 Disc, FTransform& Out);
	/** One material instance per model batch, never per soldier. */
	GULISTRIKE_API void ConfigureOverlay(UInstancedStaticMeshComponent& Component, UMaterialInterface* Material);
	GULISTRIKE_API void StepAim(const FGuLiMechanicalAnimationConfig& Config,
		const FTransform& LogicalPose, const FVector* Target, float FinalMoveSpeed, float DeltaSeconds,
		FGuLiMechanicalAnimationState& State, float TurnRateScale = 1.0f);
	GULISTRIKE_API void StepLocomotion(const FGuLiMechanicalAnimationConfig& Config,
		const FTransform& PreviousPose, const FTransform& Pose, float DeltaSeconds, bool bReset,
		FGuLiMechanicalAnimationState& State);
	GULISTRIKE_API float RecoilAt(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, int32 Side, float ServerTime);
	GULISTRIKE_API void AcceptShot(const FGuLiMechanicalAnimationConfig& Config,
		FGuLiMechanicalAnimationState& State, int32 Side, float ServerTime);
	GULISTRIKE_API bool ResolveMuzzle(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, const FTransform& LogicalPose,
		FName Slot, int32 Side, float ServerTime, FTransform& OutTransform);
	GULISTRIKE_API FGuLiMechanicalAnimationFrame BuildFrame(const FGuLiMechanicalAnimationConfig& Config,
		const FGuLiMechanicalAnimationState& State, float LowerYawDegrees, float ServerTime);
	GULISTRIKE_API bool WriteInstance(UInstancedStaticMeshComponent& Component, int32 Index,
		const FGuLiMechanicalAnimationFrame& Current, const FGuLiMechanicalAnimationFrame& Previous,
		float HitTime = -1000.0f);
}
