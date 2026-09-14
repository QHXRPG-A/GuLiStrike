// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeSecondaryWeapons.xlsx, GuLiStrikeShip.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeShipTableRows.generated.h"

/** DataTable DT_GuLiStrikeShip_WingmanWeapons 的行结构（源: GuLiStrikeSecondaryWeapons.xlsx / WingmanWeapons）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipWingmanWeaponsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	FString Note;

	/** SkillId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	FString SkillId;

	/** AttackPattern (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	FString AttackPattern;

	/** ExecutorId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	FString ExecutorId;

	/** Damage (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float Damage = 0.0f;

	/** CooldownSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float CooldownSeconds = 0.0f;

	/** RangeCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float RangeCentimeters = 0.0f;

	/** FireConeHalfAngleDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float FireConeHalfAngleDegrees = 0.0f;

	/** FlightSpeedCentimetersPerSecond (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float FlightSpeedCentimetersPerSecond = 0.0f;

	/** DiveSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float DiveSeconds = 0.0f;

	/** MissileCount (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	int32 MissileCount = 0;

	/** StripLengthCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float StripLengthCentimeters = 0.0f;

	/** PullUpHeightCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float PullUpHeightCentimeters = 0.0f;

	/** AirFireStartDistanceCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float AirFireStartDistanceCentimeters = 0.0f;

	/** AirFireStopDistanceCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float AirFireStopDistanceCentimeters = 0.0f;

	/** AirBurstDurationSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float AirBurstDurationSeconds = 0.0f;

	/** AirOrbitCooldownSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float AirOrbitCooldownSeconds = 0.0f;

	/** ProjectileSpeedCentimetersPerSecond (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float ProjectileSpeedCentimetersPerSecond = 0.0f;

	/** ProjectileLifetimeSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float ProjectileLifetimeSeconds = 0.0f;

	/** SweepRadiusCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float SweepRadiusCentimeters = 0.0f;

	/** MuzzleX/MuzzleY/MuzzleZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	FVector Muzzle = FVector::ZeroVector;

	/** 产生的法术场 (Fields.id) -> EffectConfigId (Fields.name；导出时解析) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	FString EffectConfigId;

	/** WeaponAsset (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	TSoftObjectPtr<UObject> WeaponAsset;

	/** bRequiresLineOfSight (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	bool bRequiresLineOfSight = false;

	/** MaximumHomingTurnRateDegreesPerSecond (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	float MaximumHomingTurnRateDegreesPerSecond = 0.0f;

	/** AttackProjectile (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanWeapons")
	TSoftObjectPtr<UObject> AttackProjectile;

};

/** DataTable DT_GuLiStrikeShip_WingmanTargeting 的行结构（源: GuLiStrikeSecondaryWeapons.xlsx / WingmanTargeting）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipWingmanTargetingRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanTargeting")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanTargeting")
	FString Note;

	/** AcquireRadiusCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanTargeting")
	float AcquireRadiusCentimeters = 0.0f;

	/** ReleaseRadiusCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanTargeting")
	float ReleaseRadiusCentimeters = 0.0f;

	/** GuardRejoinFraction (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanTargeting")
	float GuardRejoinFraction = 0.0f;

	/** ScanIntervalSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WingmanTargeting")
	float ScanIntervalSeconds = 0.0f;

};

/** DataTable DT_GuLiStrikeShip_Parts 的行结构（源: GuLiStrikeShip.xlsx / Parts）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipPartsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	FString Note;

	/** PartId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	FString PartId;

	/** Type (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	FString Type;

	/** PartMass (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	float PartMass = 0.0f;

	/** Thrust (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	float Thrust = 0.0f;

	/** Damage (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	float Damage = 0.0f;

	/** FireRate (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	float FireRate = 0.0f;

	/** MuzzleX/MuzzleY/MuzzleZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	FVector Muzzle = FVector::ZeroVector;

	/** ProjectileClass (softclass, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	TSoftClassPtr<UObject> ProjectileClass;

};

/** DataTable DT_GuLiStrikeShip_Tuning 的行结构（源: GuLiStrikeShip.xlsx / Tuning）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipTuningRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	FString Note;

	/** HullMass (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float HullMass = 0.0f;

	/** BaseMaxSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float BaseMaxSpeed = 0.0f;

	/** BaseAcceleration (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float BaseAcceleration = 0.0f;

	/** NominalThrustRatio (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float NominalThrustRatio = 0.0f;

	/** SpeedMultiplierMin (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float SpeedMultiplierMin = 0.0f;

	/** SpeedMultiplierMax (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float SpeedMultiplierMax = 0.0f;

	/** BoostThrustMultiplier (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float BoostThrustMultiplier = 0.0f;

	/** MousePitchScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float MousePitchScale = 0.0f;

	/** MouseYawScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float MouseYawScale = 0.0f;

	/** YawRate (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float YawRate = 0.0f;

	/** YawResponseSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float YawResponseSpeed = 0.0f;

	/** YawStopDamping (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float YawStopDamping = 0.0f;

	/** MaxBankAngle (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float MaxBankAngle = 0.0f;

	/** BankInterpSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float BankInterpSpeed = 0.0f;

	/** OrientTurnSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float OrientTurnSpeed = 0.0f;

	/** OrientToMovement (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	bool OrientToMovement = false;

	/** OrientMinForwardDot (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	float OrientMinForwardDot = 0.0f;

	/** HullMeshOffsetX/HullMeshOffsetY/HullMeshOffsetZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tuning")
	FVector HullMeshOffset = FVector::ZeroVector;

};

/** DataTable DT_GuLiStrikeShip_Camera 的行结构（源: GuLiStrikeShip.xlsx / Camera）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipCameraRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	FString Note;

	/** CameraDefaultArmLength (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraDefaultArmLength = 0.0f;

	/** CameraZoomStep (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraZoomStep = 0.0f;

	/** CameraZoomMin (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraZoomMin = 0.0f;

	/** CameraZoomMax (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraZoomMax = 0.0f;

	/** CameraCollisionProbeRadius (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraCollisionProbeRadius = 0.0f;

	/** CameraCollisionMinArm (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraCollisionMinArm = 0.0f;

	/** CameraPitchMin (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraPitchMin = 0.0f;

	/** CameraPitchMax (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera")
	float CameraPitchMax = 0.0f;

};
