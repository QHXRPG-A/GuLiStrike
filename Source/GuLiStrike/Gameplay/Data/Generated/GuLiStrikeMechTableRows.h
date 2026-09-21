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

};
