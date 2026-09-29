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

/** GPU contract: 0 hit; 1..14 current pose; 15..28 previous; 29/30 current/previous missile visibility. */
struct GULISTRIKE_API FGuLiMechanicalAnimationFrame
{
	static constexpr int32 PoseFloats = 14;
	static constexpr int32 CustomDataFloats = 3 + 2 * PoseFloats;
	float Values[PoseFloats] = {};
	float MissilePodVisible = 0.0f;
};

namespace GuLiMechanicalAnimation
{
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
		FGuLiMechanicalAnimationState& State);
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
