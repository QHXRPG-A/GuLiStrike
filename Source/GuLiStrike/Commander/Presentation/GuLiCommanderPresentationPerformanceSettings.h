// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Identifies which layer currently supplies one client-local presentation value. */
enum class EGuLiCommanderPresentationSettingSource : uint8
{
	CppDefault,
	Config,
	LocalOverride
};

/**
 * Typed, C++-only snapshot of the Commander Soldier presentation cost controls.
 * These values are deliberately local-only and never enter replication.
 */
struct GULISTRIKE_API FGuLiCommanderPresentationPerformanceSettings
{
	static constexpr int32 DefaultUnitCullDistanceCentimeters = 20000;
	static constexpr int32 DefaultRingCullDistanceCentimeters = 0;
	static constexpr int32 MaximumCullDistanceCentimeters = 10000000;

	int32 UnitCullDistanceCentimeters = DefaultUnitCullDistanceCentimeters;
	int32 RingCullDistanceCentimeters = DefaultRingCullDistanceCentimeters;
	bool bUnitCastShadow = false;
	bool bUnitAffectDistanceFieldLighting = false;
	bool bUnitAffectDynamicIndirectLighting = false;
	bool bUnitVisibleInRayTracing = false;

	static FGuLiCommanderPresentationPerformanceSettings CompiledDefaults();
	bool operator==(const FGuLiCommanderPresentationPerformanceSettings& Other) const;
	bool operator!=(const FGuLiCommanderPresentationPerformanceSettings& Other) const
	{
		return !(*this == Other);
	}
};

/** Presence-preserving raw values read from the active class Config section. */
struct GULISTRIKE_API FGuLiCommanderPresentationRawConfigSettings
{
	TOptional<FString> UnitCullDistanceCentimeters;
	TOptional<FString> RingCullDistanceCentimeters;
	TOptional<FString> bUnitCastShadow;
	TOptional<FString> bUnitAffectDistanceFieldLighting;
	TOptional<FString> bUnitAffectDynamicIndirectLighting;
	TOptional<FString> bUnitVisibleInRayTracing;
};

struct GULISTRIKE_API FGuLiCommanderPresentationSettingView
{
	FName Key;
	FString Baseline;
	FString Effective;
	FString AcceptedValues;
	EGuLiCommanderPresentationSettingSource Source =
		EGuLiCommanderPresentationSettingSource::CppDefault;
};

/** Structured result shared by native callers and the local console command adapter. */
struct GULISTRIKE_API FGuLiCommanderPresentationSettingResult
{
	FName Key;
	FString Baseline;
	FString PreviousEffective;
	FString Effective;
	FString Error;
	EGuLiCommanderPresentationSettingSource Source =
		EGuLiCommanderPresentationSettingSource::CppDefault;
	int32 AppliedActorCount = 0;
	bool bSuccess = false;
	bool bChanged = false;
};

/**
 * One registry is owned by each presentation actor, which naturally scopes overrides to a World.
 * It accepts only the explicit six-key whitelist below.
 */
class GULISTRIKE_API FGuLiCommanderPresentationPerformanceRegistry
{
public:
	FGuLiCommanderPresentationPerformanceRegistry();

	/** Missing keys retain C++ defaults; explicit invalid keys are diagnosed and fall back independently. */
	void InitializeFromRawConfig(
		const FGuLiCommanderPresentationRawConfigSettings& ConfigSettings,
		TArray<FString>& OutValidationErrors);

	const FGuLiCommanderPresentationPerformanceSettings& GetBaselineSettings() const
	{
		return BaselineSettings;
	}
	FGuLiCommanderPresentationPerformanceSettings GetEffectiveSettings() const;

	TArray<FGuLiCommanderPresentationSettingView> List(const FString& Prefix = FString()) const;
	FGuLiCommanderPresentationSettingResult Get(const FString& Key) const;
	FGuLiCommanderPresentationSettingResult Set(const FString& Key, const FString& Value);
	TArray<FGuLiCommanderPresentationSettingResult> Reset(const FString& KeyOrAll);

	static const TCHAR* LexToString(EGuLiCommanderPresentationSettingSource Source);

private:
	struct FOverrides
	{
		TOptional<int32> UnitCullDistanceCentimeters;
		TOptional<int32> RingCullDistanceCentimeters;
		TOptional<bool> bUnitCastShadow;
		TOptional<bool> bUnitAffectDistanceFieldLighting;
		TOptional<bool> bUnitAffectDynamicIndirectLighting;
		TOptional<bool> bUnitVisibleInRayTracing;
	};

	FGuLiCommanderPresentationSettingResult MakeUnknownKeyResult(const FString& Key) const;
	FGuLiCommanderPresentationSettingResult MakeResultForKnownKey(FName Key) const;
	bool ResetKnownKey(FName Key, FGuLiCommanderPresentationSettingResult& OutResult);
	EGuLiCommanderPresentationSettingSource GetBaselineSource(FName Key) const;
	bool HasOverride(FName Key) const;

	FGuLiCommanderPresentationPerformanceSettings BaselineSettings;
	FOverrides Overrides;
	TSet<FName> ConfigBaselineKeys;
};
