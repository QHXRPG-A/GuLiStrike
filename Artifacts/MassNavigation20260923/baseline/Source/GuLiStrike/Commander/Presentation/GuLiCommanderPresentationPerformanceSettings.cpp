// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderPresentationPerformanceSettings.h"

namespace GuLiCommanderPresentationPerformance
{
	const FName UnitCullDistanceKey(TEXT("unit_cull_distance_cm"));
	const FName RingCullDistanceKey(TEXT("ring_cull_distance_cm"));
	const FName UnitCastShadowKey(TEXT("unit.cast_shadow"));
	const FName UnitAffectDistanceFieldLightingKey(TEXT("unit.affect_distance_field_lighting"));
	const FName UnitAffectDynamicIndirectLightingKey(TEXT("unit.affect_dynamic_indirect_lighting"));
	const FName UnitVisibleInRayTracingKey(TEXT("unit.visible_in_ray_tracing"));

	const TArray<FName>& GetSortedKeys()
	{
		static const TArray<FName> Keys = {
			RingCullDistanceKey,
			UnitAffectDistanceFieldLightingKey,
			UnitAffectDynamicIndirectLightingKey,
			UnitCastShadowKey,
			UnitVisibleInRayTracingKey,
			UnitCullDistanceKey
		};
		return Keys;
	}

	FName FindCanonicalKey(const FString& Candidate)
	{
		for (const FName Key : GetSortedKeys())
		{
			if (Candidate.Equals(Key.ToString(), ESearchCase::IgnoreCase))
			{
				return Key;
			}
		}
		return NAME_None;
	}

	FString BoolToString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	bool ParseStrictInteger(const FString& Text, int32& OutValue)
	{
		if (Text.IsEmpty() || Text.TrimStartAndEnd() != Text)
		{
			return false;
		}

		TCHAR* End = nullptr;
		const int64 Parsed = FCString::Strtoi64(*Text, &End, 10);
		if (!End || End == *Text || *End != TEXT('\0')
			|| Parsed < MIN_int32 || Parsed > MAX_int32)
		{
			return false;
		}
		OutValue = static_cast<int32>(Parsed);
		return true;
	}

	bool ParseStrictBool(const FString& Text, bool& bOutValue)
	{
		if (Text.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Text == TEXT("1"))
		{
			bOutValue = true;
			return true;
		}
		if (Text.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Text == TEXT("0"))
		{
			bOutValue = false;
			return true;
		}
		return false;
	}

	bool IsValidUnitCullDistance(const int32 Value)
	{
		return Value >= 1
			&& Value <= FGuLiCommanderPresentationPerformanceSettings::MaximumCullDistanceCentimeters;
	}

	bool IsValidRingCullDistance(const int32 Value)
	{
		return Value >= 0
			&& Value <= FGuLiCommanderPresentationPerformanceSettings::MaximumCullDistanceCentimeters;
	}
}

FGuLiCommanderPresentationPerformanceSettings
FGuLiCommanderPresentationPerformanceSettings::CompiledDefaults()
{
	return FGuLiCommanderPresentationPerformanceSettings();
}

bool FGuLiCommanderPresentationPerformanceSettings::operator==(
	const FGuLiCommanderPresentationPerformanceSettings& Other) const
{
	return UnitCullDistanceCentimeters == Other.UnitCullDistanceCentimeters
		&& RingCullDistanceCentimeters == Other.RingCullDistanceCentimeters
		&& bUnitCastShadow == Other.bUnitCastShadow
		&& bUnitAffectDistanceFieldLighting == Other.bUnitAffectDistanceFieldLighting
		&& bUnitAffectDynamicIndirectLighting == Other.bUnitAffectDynamicIndirectLighting
		&& bUnitVisibleInRayTracing == Other.bUnitVisibleInRayTracing;
}

FGuLiCommanderPresentationPerformanceRegistry::FGuLiCommanderPresentationPerformanceRegistry()
	: BaselineSettings(FGuLiCommanderPresentationPerformanceSettings::CompiledDefaults())
{
}

void FGuLiCommanderPresentationPerformanceRegistry::InitializeFromRawConfig(
	const FGuLiCommanderPresentationRawConfigSettings& ConfigSettings,
	TArray<FString>& OutValidationErrors)
{
	using namespace GuLiCommanderPresentationPerformance;

	BaselineSettings = FGuLiCommanderPresentationPerformanceSettings::CompiledDefaults();
	Overrides = FOverrides();
	ConfigBaselineKeys.Reset();
	OutValidationErrors.Reset();

	if (ConfigSettings.UnitCullDistanceCentimeters.IsSet())
	{
		int32 ParsedValue = 0;
		const FString& RawValue = ConfigSettings.UnitCullDistanceCentimeters.GetValue();
		if (ParseStrictInteger(RawValue, ParsedValue) && IsValidUnitCullDistance(ParsedValue))
		{
			BaselineSettings.UnitCullDistanceCentimeters = ParsedValue;
			ConfigBaselineKeys.Add(UnitCullDistanceKey);
		}
		else
		{
			OutValidationErrors.Add(FString::Printf(
				TEXT("UnitCullDistanceCentimeters='%s' is not an integer in [1,%d]"),
				*RawValue,
				FGuLiCommanderPresentationPerformanceSettings::MaximumCullDistanceCentimeters));
		}
	}

	if (ConfigSettings.RingCullDistanceCentimeters.IsSet())
	{
		int32 ParsedValue = 0;
		const FString& RawValue = ConfigSettings.RingCullDistanceCentimeters.GetValue();
		if (ParseStrictInteger(RawValue, ParsedValue) && IsValidRingCullDistance(ParsedValue))
		{
			BaselineSettings.RingCullDistanceCentimeters = ParsedValue;
			ConfigBaselineKeys.Add(RingCullDistanceKey);
		}
		else
		{
			OutValidationErrors.Add(FString::Printf(
				TEXT("RingCullDistanceCentimeters='%s' is not an integer in [0,%d]"),
				*RawValue,
				FGuLiCommanderPresentationPerformanceSettings::MaximumCullDistanceCentimeters));
		}
	}

	const auto ApplyBool = [this, &OutValidationErrors](
		const TCHAR* ConfigPropertyName,
		const TOptional<FString>& RawValue,
		const FName RegistryKey,
		bool& OutBaselineValue)
	{
		if (!RawValue.IsSet())
		{
			return;
		}

		bool bParsedValue = false;
		if (!GuLiCommanderPresentationPerformance::ParseStrictBool(
			RawValue.GetValue(),
			bParsedValue))
		{
			OutValidationErrors.Add(FString::Printf(
				TEXT("%s='%s' is not one of true, false, 1, or 0"),
				ConfigPropertyName,
				*RawValue.GetValue()));
			return;
		}

		OutBaselineValue = bParsedValue;
		ConfigBaselineKeys.Add(RegistryKey);
	};

	ApplyBool(
		TEXT("bUnitCastShadow"),
		ConfigSettings.bUnitCastShadow,
		UnitCastShadowKey,
		BaselineSettings.bUnitCastShadow);
	ApplyBool(
		TEXT("bUnitAffectDistanceFieldLighting"),
		ConfigSettings.bUnitAffectDistanceFieldLighting,
		UnitAffectDistanceFieldLightingKey,
		BaselineSettings.bUnitAffectDistanceFieldLighting);
	ApplyBool(
		TEXT("bUnitAffectDynamicIndirectLighting"),
		ConfigSettings.bUnitAffectDynamicIndirectLighting,
		UnitAffectDynamicIndirectLightingKey,
		BaselineSettings.bUnitAffectDynamicIndirectLighting);
	ApplyBool(
		TEXT("bUnitVisibleInRayTracing"),
		ConfigSettings.bUnitVisibleInRayTracing,
		UnitVisibleInRayTracingKey,
		BaselineSettings.bUnitVisibleInRayTracing);
}

FGuLiCommanderPresentationPerformanceSettings
FGuLiCommanderPresentationPerformanceRegistry::GetEffectiveSettings() const
{
	FGuLiCommanderPresentationPerformanceSettings Effective = BaselineSettings;
	if (Overrides.UnitCullDistanceCentimeters.IsSet())
	{
		Effective.UnitCullDistanceCentimeters = Overrides.UnitCullDistanceCentimeters.GetValue();
	}
	if (Overrides.RingCullDistanceCentimeters.IsSet())
	{
		Effective.RingCullDistanceCentimeters = Overrides.RingCullDistanceCentimeters.GetValue();
	}
	if (Overrides.bUnitCastShadow.IsSet())
	{
		Effective.bUnitCastShadow = Overrides.bUnitCastShadow.GetValue();
	}
	if (Overrides.bUnitAffectDistanceFieldLighting.IsSet())
	{
		Effective.bUnitAffectDistanceFieldLighting =
			Overrides.bUnitAffectDistanceFieldLighting.GetValue();
	}
	if (Overrides.bUnitAffectDynamicIndirectLighting.IsSet())
	{
		Effective.bUnitAffectDynamicIndirectLighting =
			Overrides.bUnitAffectDynamicIndirectLighting.GetValue();
	}
	if (Overrides.bUnitVisibleInRayTracing.IsSet())
	{
		Effective.bUnitVisibleInRayTracing = Overrides.bUnitVisibleInRayTracing.GetValue();
	}
	return Effective;
}

TArray<FGuLiCommanderPresentationSettingView>
FGuLiCommanderPresentationPerformanceRegistry::List(const FString& Prefix) const
{
	TArray<FGuLiCommanderPresentationSettingView> Result;
	for (const FName Key : GuLiCommanderPresentationPerformance::GetSortedKeys())
	{
		if (!Prefix.IsEmpty()
			&& !Key.ToString().StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FGuLiCommanderPresentationSettingResult Value = MakeResultForKnownKey(Key);
		FGuLiCommanderPresentationSettingView& View = Result.AddDefaulted_GetRef();
		View.Key = Key;
		View.Baseline = Value.Baseline;
		View.Effective = Value.Effective;
		View.Source = Value.Source;
		View.AcceptedValues = (Key == GuLiCommanderPresentationPerformance::UnitCullDistanceKey)
			? FString::Printf(
				TEXT("integer [1,%d]"),
				FGuLiCommanderPresentationPerformanceSettings::MaximumCullDistanceCentimeters)
			: (Key == GuLiCommanderPresentationPerformance::RingCullDistanceKey)
				? FString::Printf(
					TEXT("integer [0,%d]; 0 disables distance culling"),
					FGuLiCommanderPresentationPerformanceSettings::MaximumCullDistanceCentimeters)
				: TEXT("true|false|1|0");
	}
	return Result;
}

FGuLiCommanderPresentationSettingResult
FGuLiCommanderPresentationPerformanceRegistry::Get(const FString& Key) const
{
	const FName CanonicalKey = GuLiCommanderPresentationPerformance::FindCanonicalKey(Key);
	return CanonicalKey.IsNone()
		? MakeUnknownKeyResult(Key)
		: MakeResultForKnownKey(CanonicalKey);
}

FGuLiCommanderPresentationSettingResult
FGuLiCommanderPresentationPerformanceRegistry::Set(const FString& Key, const FString& Value)
{
	using namespace GuLiCommanderPresentationPerformance;

	const FName CanonicalKey = FindCanonicalKey(Key);
	if (CanonicalKey.IsNone())
	{
		return MakeUnknownKeyResult(Key);
	}

	FGuLiCommanderPresentationSettingResult Result = MakeResultForKnownKey(CanonicalKey);
	const FString PreviousEffective = Result.Effective;
	if (CanonicalKey == UnitCullDistanceKey || CanonicalKey == RingCullDistanceKey)
	{
		int32 ParsedValue = 0;
		const bool bParsed = ParseStrictInteger(Value, ParsedValue);
		const bool bInRange = CanonicalKey == UnitCullDistanceKey
			? IsValidUnitCullDistance(ParsedValue)
			: IsValidRingCullDistance(ParsedValue);
		if (!bParsed || !bInRange)
		{
			Result.bSuccess = false;
			Result.Error = FString::Printf(
				TEXT("value '%s' is not a valid %s"),
				*Value,
				CanonicalKey == UnitCullDistanceKey
					? TEXT("integer cull distance in [1,10000000]")
					: TEXT("integer cull distance in [0,10000000]"));
			return Result;
		}

		if (CanonicalKey == UnitCullDistanceKey)
		{
			Overrides.UnitCullDistanceCentimeters = ParsedValue;
		}
		else
		{
			Overrides.RingCullDistanceCentimeters = ParsedValue;
		}
	}
	else
	{
		bool bParsedValue = false;
		if (!ParseStrictBool(Value, bParsedValue))
		{
			Result.bSuccess = false;
			Result.Error = FString::Printf(
				TEXT("value '%s' is not one of true, false, 1, or 0"),
				*Value);
			return Result;
		}

		if (CanonicalKey == UnitCastShadowKey)
		{
			Overrides.bUnitCastShadow = bParsedValue;
		}
		else if (CanonicalKey == UnitAffectDistanceFieldLightingKey)
		{
			Overrides.bUnitAffectDistanceFieldLighting = bParsedValue;
		}
		else if (CanonicalKey == UnitAffectDynamicIndirectLightingKey)
		{
			Overrides.bUnitAffectDynamicIndirectLighting = bParsedValue;
		}
		else
		{
			Overrides.bUnitVisibleInRayTracing = bParsedValue;
		}
	}

	Result = MakeResultForKnownKey(CanonicalKey);
	Result.PreviousEffective = PreviousEffective;
	Result.Source = EGuLiCommanderPresentationSettingSource::LocalOverride;
	Result.bSuccess = true;
	Result.bChanged = PreviousEffective != Result.Effective;
	return Result;
}

TArray<FGuLiCommanderPresentationSettingResult>
FGuLiCommanderPresentationPerformanceRegistry::Reset(const FString& KeyOrAll)
{
	TArray<FGuLiCommanderPresentationSettingResult> Results;
	if (KeyOrAll.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		for (const FName Key : GuLiCommanderPresentationPerformance::GetSortedKeys())
		{
			FGuLiCommanderPresentationSettingResult& Result = Results.AddDefaulted_GetRef();
			ResetKnownKey(Key, Result);
		}
		return Results;
	}

	const FName CanonicalKey = GuLiCommanderPresentationPerformance::FindCanonicalKey(KeyOrAll);
	if (CanonicalKey.IsNone())
	{
		Results.Add(MakeUnknownKeyResult(KeyOrAll));
		return Results;
	}

	FGuLiCommanderPresentationSettingResult& Result = Results.AddDefaulted_GetRef();
	ResetKnownKey(CanonicalKey, Result);
	return Results;
}

const TCHAR* FGuLiCommanderPresentationPerformanceRegistry::LexToString(
	const EGuLiCommanderPresentationSettingSource Source)
{
	switch (Source)
	{
	case EGuLiCommanderPresentationSettingSource::CppDefault:
		return TEXT("cpp_default");
	case EGuLiCommanderPresentationSettingSource::Config:
		return TEXT("config");
	case EGuLiCommanderPresentationSettingSource::LocalOverride:
		return TEXT("local_override");
	default:
		return TEXT("unknown");
	}
}

FGuLiCommanderPresentationSettingResult
FGuLiCommanderPresentationPerformanceRegistry::MakeUnknownKeyResult(const FString& Key) const
{
	FGuLiCommanderPresentationSettingResult Result;
	Result.Key = FName(*Key);
	Result.Error = FString::Printf(TEXT("unknown presentation key '%s'"), *Key);
	return Result;
}

FGuLiCommanderPresentationSettingResult
FGuLiCommanderPresentationPerformanceRegistry::MakeResultForKnownKey(const FName Key) const
{
	using namespace GuLiCommanderPresentationPerformance;

	const FGuLiCommanderPresentationPerformanceSettings Effective = GetEffectiveSettings();
	FGuLiCommanderPresentationSettingResult Result;
	Result.Key = Key;
	Result.bSuccess = true;
	Result.Source = HasOverride(Key)
		? EGuLiCommanderPresentationSettingSource::LocalOverride
		: GetBaselineSource(Key);

	if (Key == UnitCullDistanceKey)
	{
		Result.Baseline = FString::FromInt(BaselineSettings.UnitCullDistanceCentimeters);
		Result.Effective = FString::FromInt(Effective.UnitCullDistanceCentimeters);
	}
	else if (Key == RingCullDistanceKey)
	{
		Result.Baseline = FString::FromInt(BaselineSettings.RingCullDistanceCentimeters);
		Result.Effective = FString::FromInt(Effective.RingCullDistanceCentimeters);
	}
	else if (Key == UnitCastShadowKey)
	{
		Result.Baseline = BoolToString(BaselineSettings.bUnitCastShadow);
		Result.Effective = BoolToString(Effective.bUnitCastShadow);
	}
	else if (Key == UnitAffectDistanceFieldLightingKey)
	{
		Result.Baseline = BoolToString(BaselineSettings.bUnitAffectDistanceFieldLighting);
		Result.Effective = BoolToString(Effective.bUnitAffectDistanceFieldLighting);
	}
	else if (Key == UnitAffectDynamicIndirectLightingKey)
	{
		Result.Baseline = BoolToString(BaselineSettings.bUnitAffectDynamicIndirectLighting);
		Result.Effective = BoolToString(Effective.bUnitAffectDynamicIndirectLighting);
	}
	else
	{
		Result.Baseline = BoolToString(BaselineSettings.bUnitVisibleInRayTracing);
		Result.Effective = BoolToString(Effective.bUnitVisibleInRayTracing);
	}
	Result.PreviousEffective = Result.Effective;
	return Result;
}

bool FGuLiCommanderPresentationPerformanceRegistry::ResetKnownKey(
	const FName Key,
	FGuLiCommanderPresentationSettingResult& OutResult)
{
	using namespace GuLiCommanderPresentationPerformance;

	OutResult = MakeResultForKnownKey(Key);
	OutResult.PreviousEffective = OutResult.Effective;
	const bool bHadOverride = HasOverride(Key);
	if (Key == UnitCullDistanceKey)
	{
		Overrides.UnitCullDistanceCentimeters.Reset();
	}
	else if (Key == RingCullDistanceKey)
	{
		Overrides.RingCullDistanceCentimeters.Reset();
	}
	else if (Key == UnitCastShadowKey)
	{
		Overrides.bUnitCastShadow.Reset();
	}
	else if (Key == UnitAffectDistanceFieldLightingKey)
	{
		Overrides.bUnitAffectDistanceFieldLighting.Reset();
	}
	else if (Key == UnitAffectDynamicIndirectLightingKey)
	{
		Overrides.bUnitAffectDynamicIndirectLighting.Reset();
	}
	else if (Key == UnitVisibleInRayTracingKey)
	{
		Overrides.bUnitVisibleInRayTracing.Reset();
	}
	else
	{
		OutResult = MakeUnknownKeyResult(Key.ToString());
		return false;
	}

	const FString PreviousEffective = OutResult.PreviousEffective;
	OutResult = MakeResultForKnownKey(Key);
	OutResult.PreviousEffective = PreviousEffective;
	OutResult.bChanged = bHadOverride && PreviousEffective != OutResult.Effective;
	return true;
}

EGuLiCommanderPresentationSettingSource
FGuLiCommanderPresentationPerformanceRegistry::GetBaselineSource(const FName Key) const
{
	return ConfigBaselineKeys.Contains(Key)
		? EGuLiCommanderPresentationSettingSource::Config
		: EGuLiCommanderPresentationSettingSource::CppDefault;
}

bool FGuLiCommanderPresentationPerformanceRegistry::HasOverride(const FName Key) const
{
	using namespace GuLiCommanderPresentationPerformance;
	if (Key == UnitCullDistanceKey)
	{
		return Overrides.UnitCullDistanceCentimeters.IsSet();
	}
	if (Key == RingCullDistanceKey)
	{
		return Overrides.RingCullDistanceCentimeters.IsSet();
	}
	if (Key == UnitCastShadowKey)
	{
		return Overrides.bUnitCastShadow.IsSet();
	}
	if (Key == UnitAffectDistanceFieldLightingKey)
	{
		return Overrides.bUnitAffectDistanceFieldLighting.IsSet();
	}
	if (Key == UnitAffectDynamicIndirectLightingKey)
	{
		return Overrides.bUnitAffectDynamicIndirectLighting.IsSet();
	}
	return Key == UnitVisibleInRayTracingKey
		&& Overrides.bUnitVisibleInRayTracing.IsSet();
}
