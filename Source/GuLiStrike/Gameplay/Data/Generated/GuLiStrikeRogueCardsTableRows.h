// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeRogueCards.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeRogueCardsTableRows.generated.h"

/** DataTable DT_GuLiStrikeRogueCards_Cards 的行结构（源: GuLiStrikeRogueCards.xlsx / Cards）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsCardsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	FString Id;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	FString Note;

	/** Type (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 Type = 0;

	/** TextIds (str[], Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> TextIds;

	/** ImplementationClass (softclass, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftClassPtr<UObject> ImplementationClass;

	/** UnitTypeId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 UnitTypeId = 0;

	/** BonusPercent (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float BonusPercent = 0.0f;

	/** FrontMaterial (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> FrontMaterial;

	/** UpgradeVfx (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> UpgradeVfx;

	/** UpgradeVfxScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float UpgradeVfxScale = 0.0f;

	/** UpgradeVfxColor (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	FString UpgradeVfxColor;

	/** RequiredCardIds (str[], Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> RequiredCardIds;

	/** ExcludedCardIds (str[], Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> ExcludedCardIds;

	/** MaxAcquisitions (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 MaxAcquisitions = 0;

	/** BonusCount (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 BonusCount = 0;

	/** EffectIds (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> EffectIds;

	/** IconId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 IconId = 0;

	/** RequiredCardLevels (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> RequiredCardLevels;

	/** MaxRankUpgradeVfx (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> MaxRankUpgradeVfx;

	/** MaxRankUpgradeScale (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float MaxRankUpgradeScale = 0.0f;

	/** MaxRankStartRatio (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float MaxRankStartRatio = 0.0f;

	/** MaxRankGrowthSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float MaxRankGrowthSeconds = 0.0f;

	/** MaxRankDurationSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float MaxRankDurationSeconds = 0.0f;

	/** CastPose (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> CastPose;

	/** CastFlashVfx (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> CastFlashVfx;

	/** CastLightningVfx (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> CastLightningVfx;

	/** CastBodyScale (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastBodyScale = 0.0f;

	/** CastFoldSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastFoldSeconds = 0.0f;

	/** CastOpenSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastOpenSeconds = 0.0f;

	/** CastFlashSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastFlashSeconds = 0.0f;

	/** CastRecoverySeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastRecoverySeconds = 0.0f;

	/** UpgradeVfxHeightMultiplier (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float UpgradeVfxHeightMultiplier = 0.0f;

	/** UpgradeVfxPlaybackRate (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float UpgradeVfxPlaybackRate = 0.0f;

	/** CastCrossedHoldSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastCrossedHoldSeconds = 0.0f;

	/** CastChargeShakeDegrees (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastChargeShakeDegrees = 0.0f;

	/** CastReleaseShakeDegrees (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastReleaseShakeDegrees = 0.0f;

	/** CastShotRecoilCm (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastShotRecoilCm = 0.0f;

	/** CastShotRecoilSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastShotRecoilSeconds = 0.0f;

	/** CastCamera (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> CastCamera;

	/** CastCameraReturnSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastCameraReturnSeconds = 0.0f;

	/** CastLightningScaleMultiplier (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastLightningScaleMultiplier = 0.0f;

	/** CastCameraReturnCueSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float CastCameraReturnCueSeconds = 0.0f;

};

/** DataTable DT_GuLiStrikeRogueCards_GroundRicochet 的行结构（源: GuLiStrikeRogueCards.xlsx / GroundRicochet）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsGroundRicochetRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	FString Note;

	/** MinimumHops (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 MinimumHops = 0;

	/** MaximumHops (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 MaximumHops = 0;

	/** HopsPerStack (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 HopsPerStack = 0;

	/** RangeCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	float RangeCentimeters = 0.0f;

	/** DamageRetention (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	float DamageRetention = 0.0f;

};

/** DataTable DT_GuLiStrikeRogueCards_Effects 的行结构（源: GuLiStrikeRogueCards.xlsx / Effects）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsEffectsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	FString Note;

	/** Handler (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	FString Handler;

	/** Version (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 Version = 0;

	/** ConfigId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 ConfigId = 0;

	/** BuffId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 BuffId = 0;

};

/** DataTable DT_GuLiStrikeRogueCards_GroundWeaponEffects 的行结构（源: GuLiStrikeRogueCards.xlsx / GroundWeaponEffects）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsGroundWeaponEffectsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	FString Note;

	/** FireRateBonusPercent (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	float FireRateBonusPercent = 0.0f;

	/** ExplosiveIntervals (str[], Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> ExplosiveIntervals;

	/** ExplosiveDamageMultiplier (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	float ExplosiveDamageMultiplier = 0.0f;

	/** ExplosiveRadiusCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	float ExplosiveRadiusCentimeters = 0.0f;

	/** ExplosiveBulletVfxId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	int32 ExplosiveBulletVfxId = 0;

	/** ExplosiveImpactVfxId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	int32 ExplosiveImpactVfxId = 0;

	/** ExplosiveSpeedMultiplier (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	float ExplosiveSpeedMultiplier = 0.0f;

	/** ExplosiveOutwardOffsetCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	float ExplosiveOutwardOffsetCentimeters = 0.0f;

	/** ExplosiveRadiusBonuses (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> ExplosiveRadiusBonuses;

	/** ExplosiveDamageBonuses (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> ExplosiveDamageBonuses;

	/** BarrageIntervalsSeconds (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> BarrageIntervalsSeconds;

	/** BarrageShotCounts (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> BarrageShotCounts;

	/** BarrageDamageBonuses (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> BarrageDamageBonuses;

	/** BarrageConeHalfAnglesDegrees (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> BarrageConeHalfAnglesDegrees;

	/** LockSeconds (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> LockSeconds;

	/** LockRangeCentimeters (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> LockRangeCentimeters;

	/** BulletDamageMultipliers (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> BulletDamageMultipliers;

	/** BulletRateMultipliers (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> BulletRateMultipliers;

	/** BulletFlightVfxIds (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> BulletFlightVfxIds;

	/** LockAperturePixels (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	float LockAperturePixels = 0.0f;

	/** LockHexPixels (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	float LockHexPixels = 0.0f;

	/** LockTargetCounts (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> LockTargetCounts;

	/** SlowFractions (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> SlowFractions;

	/** SlowDurations (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> SlowDurations;

	/** ShieldDamageBonuses (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	TArray<FString> ShieldDamageBonuses;

	/** ArcVfxId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundWeaponEffects")
	int32 ArcVfxId = 0;

};

/** DataTable DT_GuLiStrikeRogueCards_BarrageCamera 的行结构（源: GuLiStrikeRogueCards.xlsx / BarrageCamera）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsBarrageCameraRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	FString Note;

	/** Phase (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	FString Phase;

	/** PhaseAlpha (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float PhaseAlpha = 0.0f;

	/** AzimuthDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float AzimuthDegrees = 0.0f;

	/** RadiusCm (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float RadiusCm = 0.0f;

	/** EyeHeightCm (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float EyeHeightCm = 0.0f;

	/** FocusForwardCm (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float FocusForwardCm = 0.0f;

	/** FocusRightCm (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float FocusRightCm = 0.0f;

	/** FocusHeightCm (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float FocusHeightCm = 0.0f;

	/** RollDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float RollDegrees = 0.0f;

	/** FieldOfViewDegrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float FieldOfViewDegrees = 0.0f;

	/** ShotWeight (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float ShotWeight = 0.0f;

	/** FlashAlpha (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float FlashAlpha = 0.0f;

	/** ChargeArcScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BarrageCamera")
	float ChargeArcScale = 0.0f;

};
