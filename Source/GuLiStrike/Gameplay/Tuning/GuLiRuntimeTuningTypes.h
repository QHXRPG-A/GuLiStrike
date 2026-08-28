// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** The layer currently supplying an effective runtime-tuning value. */
enum class EGuLiRuntimeTuningValueSource : uint8
{
	CppFallback,
	DataTable,
	GMOverride
};

/** Effective authoritative Soldier values shared by the data and GM layers. */
struct GULISTRIKE_API FGuLiSoldierRuntimeTuningValues
{
	float MovementSpeedCmPerSecond = 3600.0f;
	uint8 MaxHealth = 100u;
	float AttackPower = 0.0f;
	float Defense = 0.0f;
	float AttackRangeCentimeters = 0.0f;
};

/** Effective GM-only modifiers applied to the reserved Ship modifier layer. */
struct GULISTRIKE_API FGuLiShipRuntimeTuningValues
{
	float MaxSpeedMultiplier = 1.0f;
	float AccelerationMultiplier = 1.0f;

	bool IsIdentity() const
	{
		return FMath::IsNearlyEqual(MaxSpeedMultiplier, 1.0f)
			&& FMath::IsNearlyEqual(AccelerationMultiplier, 1.0f);
	}
};

/** Structured response returned by Get/Set/Reset and by the console adapter. */
struct GULISTRIKE_API FGuLiRuntimeTuningResult
{
	bool bSuccess = false;
	bool bChanged = false;
	FName Key = NAME_None;
	FString Error;
	double Baseline = 0.0;
	double PreviousEffective = 0.0;
	double Effective = 0.0;
	EGuLiRuntimeTuningValueSource Source = EGuLiRuntimeTuningValueSource::CppFallback;
	int32 AppliedInstanceCount = 0;
};

/** Read-only row returned by List. */
struct GULISTRIKE_API FGuLiRuntimeTuningEntryView
{
	FName Key = NAME_None;
	double Minimum = 0.0;
	double Maximum = 0.0;
	double Baseline = 0.0;
	double Effective = 0.0;
	EGuLiRuntimeTuningValueSource Source = EGuLiRuntimeTuningValueSource::CppFallback;
	bool bIntegral = false;
};

/**
 * World-agnostic whitelist and strict parser. The UWorldSubsystem owns one
 * registry instance per World and is responsible for applying accepted values.
 */
class GULISTRIKE_API FGuLiRuntimeTuningRegistry
{
public:
	FGuLiRuntimeTuningRegistry();

	bool SetBaseline(
		FName Key,
		double Value,
		EGuLiRuntimeTuningValueSource Source,
		FString* OutError = nullptr);

	FGuLiRuntimeTuningResult Get(const FString& Key) const;
	FGuLiRuntimeTuningResult Set(const FString& Key, const FString& ValueText);
	FGuLiRuntimeTuningResult Reset(const FString& Key);
	TArray<FGuLiRuntimeTuningResult> ResetAll();
	TArray<FGuLiRuntimeTuningEntryView> List(const FString& Prefix = FString()) const;

	static const TCHAR* LexToString(EGuLiRuntimeTuningValueSource Source);

private:
	struct FEntry
	{
		FName Key = NAME_None;
		double Minimum = 0.0;
		double Maximum = 0.0;
		double Baseline = 0.0;
		double Override = 0.0;
		EGuLiRuntimeTuningValueSource BaselineSource = EGuLiRuntimeTuningValueSource::CppFallback;
		bool bIntegral = false;
		bool bHasOverride = false;

		double GetEffective() const
		{
			return bHasOverride ? Override : Baseline;
		}

		EGuLiRuntimeTuningValueSource GetSource() const
		{
			return bHasOverride
				? EGuLiRuntimeTuningValueSource::GMOverride
				: BaselineSource;
		}
	};

	void Register(
		const TCHAR* Key,
		double Fallback,
		double Minimum,
		double Maximum,
		bool bIntegral = false);
	static bool IsValidValue(const FEntry& Entry, double Value, FString& OutError);
	static FGuLiRuntimeTuningResult MakeResult(const FEntry& Entry);

	TMap<FName, FEntry> Entries;
};

namespace GuLiRuntimeTuning
{
	/** Preserves the health ratio for living Soldiers; zero remains dead. */
	GULISTRIKE_API uint8 ScaleHealthPreservingRatio(
		uint8 CurrentHealth,
		uint8 PreviousMaximumHealth,
		uint8 NewMaximumHealth);

	/** Keeps local command prediction tied to the replicated effective speed. */
	GULISTRIKE_API float CalculatePredictionDistance(
		float EffectiveMoveSpeedCmPerSecond,
		float PredictionDurationSeconds,
		float FallbackDistanceCentimeters);
}
