// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeMech.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeMechTableRows.generated.h"

/** DataTable DT_GuLiStrikeMech_Upgrades 的行结构（源: GuLiStrikeMech.xlsx / 升级表）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechUpgradesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	FString Id;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	FString Note;

	/** SkillId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	int32 SkillId = 0;

	/** Level (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	int32 Level = 0;

	/** FireRate (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	float FireRate = 0.0f;

	/** Damage (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	float Damage = 0.0f;

};

/** DataTable DT_GuLiStrikeMech_Skills 的行结构（源: GuLiStrikeMech.xlsx / 技能表）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechSkillsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString Note;

	/** ProjectileSpeed (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float ProjectileSpeed = 0.0f;

	/** ProjectileLifetime (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float ProjectileLifetime = 0.0f;

	/** SweepRadius (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float SweepRadius = 0.0f;

	/** MuzzleSocket (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString MuzzleSocket;

	/** RecoilBone (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString RecoilBone;

	/** RecoilTargetLocalZCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float RecoilTargetLocalZCentimeters = 0.0f;

	/** RecoilDuration (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float RecoilDuration = 0.0f;

	/** RecoilCurve (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftObjectPtr<UObject> RecoilCurve;

	/** WeaponAnimation (softclass, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftClassPtr<UObject> WeaponAnimation;

	/** BulletVfxId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 BulletVfxId = 0;

	/** MuzzleVfxId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 MuzzleVfxId = 0;

	/** ExecutionType (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString ExecutionType;

	/** AbilityClass (softclass, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftClassPtr<UObject> AbilityClass;

	/** MaxFuel (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float MaxFuel = 0.0f;

	/** InitialFuel (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float InitialFuel = 0.0f;

	/** FuelDrainPerSecond (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float FuelDrainPerSecond = 0.0f;

	/** FuelRecoveryPerSecond (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float FuelRecoveryPerSecond = 0.0f;

	/** FuelRecoveryDelay (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float FuelRecoveryDelay = 0.0f;

	/** ThrustAcceleration (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float ThrustAcceleration = 0.0f;

	/** MaxRiseSpeed (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float MaxRiseSpeed = 0.0f;

	/** JetVfxId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 JetVfxId = 0;

	/** JetSocketLeft (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString JetSocketLeft;

	/** JetSocketRight (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString JetSocketRight;

	/** JetPitchDegrees (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float JetPitchDegrees = 0.0f;

	/** FuelBarVfxId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 FuelBarVfxId = 0;

	/** FuelBarHeight (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float FuelBarHeight = 0.0f;

	/** FuelBarRightOffset (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float FuelBarRightOffset = 0.0f;

	/** FuelBarFadeSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float FuelBarFadeSeconds = 0.0f;

	/** AirSpeedMultiplier (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float AirSpeedMultiplier = 0.0f;

	/** JetMaxTiltDegrees (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float JetMaxTiltDegrees = 0.0f;

	/** FallGravityMultiplier (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float FallGravityMultiplier = 0.0f;

	/** AimAssistEnabled (bool, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	bool AimAssistEnabled = false;

	/** AimAssistRadiusCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float AimAssistRadiusCentimeters = 0.0f;

	/** JetForwardSpeedMultiplier (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float JetForwardSpeedMultiplier = 0.0f;

};

/** DataTable DT_GuLiStrikeMech_Visuals 的行结构（源: GuLiStrikeMech.xlsx / Visuals）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechVisualsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visuals")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visuals")
	FString Note;

	/** ModelId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visuals")
	int32 ModelId = 0;

};

/** DataTable DT_GuLiStrikeMech_Mobility 的行结构（源: GuLiStrikeMech.xlsx / Mobility）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechMobilityRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	FString Note;

	/** WalkSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float WalkSpeed = 0.0f;

	/** SprintSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float SprintSpeed = 0.0f;

	/** Acceleration (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float Acceleration = 0.0f;

	/** Braking (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float Braking = 0.0f;

	/** MovingTurnRate (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float MovingTurnRate = 0.0f;

	/** TurnStartDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float TurnStartDegrees = 0.0f;

	/** TurnStopDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float TurnStopDegrees = 0.0f;

	/** TurnMovementThreshold (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float TurnMovementThreshold = 0.0f;

	/** GunYawLimit (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float GunYawLimit = 0.0f;

	/** GunPitchUpLimit (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float GunPitchUpLimit = 0.0f;

	/** GunPitchDownLimit (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float GunPitchDownLimit = 0.0f;

	/** GunReturnSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float GunReturnSeconds = 0.0f;

	/** BlinkDistance (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkDistance = 0.0f;

	/** BlinkCooldownSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkCooldownSeconds = 0.0f;

	/** BlinkWindupSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkWindupSeconds = 0.0f;

	/** BlinkRecoverySeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkRecoverySeconds = 0.0f;

	/** BlinkArmBackDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkArmBackDegrees = 0.0f;

	/** BlinkBodyLeanDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkBodyLeanDegrees = 0.0f;

	/** BlinkAfterimageCount (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	int32 BlinkAfterimageCount = 0;

	/** BlinkAfterimageSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkAfterimageSeconds = 0.0f;

	/** BlinkAfterimageOpacity (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkAfterimageOpacity = 0.0f;

	/** BlinkLoopSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkLoopSeconds = 0.0f;

	/** BlinkLoopSwayDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mobility")
	float BlinkLoopSwayDegrees = 0.0f;

};

/** DataTable DT_GuLiStrikeMech_Gait 的行结构（源: GuLiStrikeMech.xlsx / Gait）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechGaitRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	FString Note;

	/** Asset (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	TSoftObjectPtr<UObject> Asset;

	/** ReferenceSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float ReferenceSpeed = 0.0f;

	/** MinStrideScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float MinStrideScale = 0.0f;

	/** MaxStrideScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float MaxStrideScale = 0.0f;

	/** MinPlayRate (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float MinPlayRate = 0.0f;

	/** MaxPlayRate (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float MaxPlayRate = 0.0f;

	/** BasePlayRate (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float BasePlayRate = 0.0f;

	/** TransitionSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float TransitionSeconds = 0.0f;

	/** SpeedSmoothingSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float SpeedSmoothingSeconds = 0.0f;

	/** TurnDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float TurnDegrees = 0.0f;

	/** RecoilCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float RecoilCentimeters = 0.0f;

	/** HorizontalMotionScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float HorizontalMotionScale = 0.0f;

	/** SwingLiftScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float SwingLiftScale = 0.0f;

	/** PelvisMotionScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float PelvisMotionScale = 0.0f;

	/** SafeMaxStrideScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float SafeMaxStrideScale = 0.0f;

	/** MovingTurnMaxWeight (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float MovingTurnMaxWeight = 0.0f;

	/** MovingTurnAsset (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	TSoftObjectPtr<UObject> MovingTurnAsset;

	/** ArmRecoilDegrees (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float ArmRecoilDegrees = 0.0f;

	/** ArmRecoilAttackSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float ArmRecoilAttackSeconds = 0.0f;

	/** ArmRecoilHoldSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float ArmRecoilHoldSeconds = 0.0f;

	/** ArmRecoilRecoverySeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait")
	float ArmRecoilRecoverySeconds = 0.0f;

};

/** DataTable DT_GuLiStrikeMech_GroundIK 的行结构（源: GuLiStrikeMech.xlsx / GroundIK）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechGroundIKRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	FString Note;

	/** Enabled (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	bool Enabled = false;

	/** TraceUp (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float TraceUp = 0.0f;

	/** TraceDown (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float TraceDown = 0.0f;

	/** MaxFootCorrection (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxFootCorrection = 0.0f;

	/** PelvisDown (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float PelvisDown = 0.0f;

	/** PelvisUp (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float PelvisUp = 0.0f;

	/** MaxTiltDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxTiltDegrees = 0.0f;

	/** SoleOffset (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float SoleOffset = 0.0f;

	/** PlantThreshold (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float PlantThreshold = 0.0f;

	/** ReleaseThreshold (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float ReleaseThreshold = 0.0f;

	/** BlendInSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float BlendInSeconds = 0.0f;

	/** BlendOutSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float BlendOutSeconds = 0.0f;

	/** InterpSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float InterpSpeed = 0.0f;

	/** LockReleaseDistance (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float LockReleaseDistance = 0.0f;

	/** ContactHeightTolerance (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float ContactHeightTolerance = 0.0f;

	/** ContactBlendHeight (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float ContactBlendHeight = 0.0f;

	/** HeelLeftX/HeelLeftY/HeelLeftZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	FVector HeelLeft = FVector::ZeroVector;

	/** ToeLeftX/ToeLeftY/ToeLeftZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	FVector ToeLeft = FVector::ZeroVector;

	/** HeelRightX/HeelRightY/HeelRightZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	FVector HeelRight = FVector::ZeroVector;

	/** ToeRightX/ToeRightY/ToeRightZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	FVector ToeRight = FVector::ZeroVector;

	/** MinKneeAngle (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MinKneeAngle = 0.0f;

	/** MaxKneeAngle (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxKneeAngle = 0.0f;

	/** MaxLegSwingDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxLegSwingDegrees = 0.0f;

	/** MaxKneeTwistDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxKneeTwistDegrees = 0.0f;

	/** MinFootSeparation (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MinFootSeparation = 0.0f;

	/** LockMaxYawDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float LockMaxYawDegrees = 0.0f;

	/** LockReleaseSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float LockReleaseSeconds = 0.0f;

	/** MaxPelvisSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxPelvisSpeed = 0.0f;

	/** MaxCorrectionSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxCorrectionSpeed = 0.0f;

	/** MaxFootAngularSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundIK")
	float MaxFootAngularSpeed = 0.0f;

};

/** DataTable DT_GuLiStrikeMech_Camera 的行结构（源: GuLiStrikeMech.xlsx / Camera）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechCameraRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	FString Note;

	/** ArmLength (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float ArmLength = 0.0f;

	/** SocketOffsetX/SocketOffsetY/SocketOffsetZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	FVector SocketOffset = FVector::ZeroVector;

	/** PitchDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float PitchDegrees = 0.0f;

	/** YawDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float YawDegrees = 0.0f;

	/** FieldOfView (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float FieldOfView = 0.0f;

	/** TransitionSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float TransitionSeconds = 0.0f;

	/** Sensitivity (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float Sensitivity = 0.0f;

	/** MinPitchDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float MinPitchDegrees = 0.0f;

	/** MaxPitchDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float MaxPitchDegrees = 0.0f;

	/** MinDistance (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float MinDistance = 0.0f;

	/** MaxDistance (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float MaxDistance = 0.0f;

	/** ZoomStep (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float ZoomStep = 0.0f;

	/** BlinkHoldSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float BlinkHoldSeconds = 0.0f;

	/** BlinkCatchupSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float BlinkCatchupSeconds = 0.0f;

};
