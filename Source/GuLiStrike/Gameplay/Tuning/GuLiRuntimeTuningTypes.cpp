// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"

#include "String/LexFromString.h"

#include <limits>

namespace GuLiRuntimeTuningKeys
{
	constexpr TCHAR SoldierMoveSpeed[] = TEXT("soldier.move_speed_cm_s");
	constexpr TCHAR SoldierMaxHealth[] = TEXT("soldier.max_health");
	constexpr TCHAR SoldierDefense[] = TEXT("soldier.defense");
	constexpr TCHAR ShipMaxSpeedMultiplier[] = TEXT("ship.max_speed_multiplier");
	constexpr TCHAR ShipAccelerationMultiplier[] = TEXT("ship.acceleration_multiplier");

	FString UnknownKeyError(const FString& Key)
	{
		if (Key.Equals(TEXT("soldier.attack_power"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("soldier.attack_range_cm"), ESearchCase::IgnoreCase))
		{
			return TEXT("Soldier attack tuning moved to gs.GM.Skill.Set <Red|Blue> <UnitTypeId> <damage|rate|range> <value>; use gs.GM.Skill.Reset to restore that type's skill baseline");
		}
		return FString::Printf(TEXT("unknown key '%s'"), *Key);
	}

	bool IsStrictNumber(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return false;
		}

		int32 Index = 0;
		if (Text[Index] == TCHAR('+') || Text[Index] == TCHAR('-'))
		{
			++Index;
		}
		bool bHasMantissaDigit = false;
		while (Index < Text.Len() && FChar::IsDigit(Text[Index]))
		{
			bHasMantissaDigit = true;
			++Index;
		}
		if (Index < Text.Len() && Text[Index] == TCHAR('.'))
		{
			++Index;
			while (Index < Text.Len() && FChar::IsDigit(Text[Index]))
			{
				bHasMantissaDigit = true;
				++Index;
			}
		}
		if (!bHasMantissaDigit)
		{
			return false;
		}
		if (Index < Text.Len() && (Text[Index] == TCHAR('e') || Text[Index] == TCHAR('E')))
		{
			++Index;
			if (Index < Text.Len() && (Text[Index] == TCHAR('+') || Text[Index] == TCHAR('-')))
			{
				++Index;
			}
			const int32 ExponentStart = Index;
			while (Index < Text.Len() && FChar::IsDigit(Text[Index]))
			{
				++Index;
			}
			if (Index == ExponentStart)
			{
				return false;
			}
		}
		return Index == Text.Len();
	}
}

FGuLiRuntimeTuningRegistry::FGuLiRuntimeTuningRegistry()
{
	// FMassMoveTargetFragment stores desired speed in FMassInt16Real (1 cm precision).
	Register(GuLiRuntimeTuningKeys::SoldierMoveSpeed, 3600.0, 1.0, MAX_int16);
	Register(GuLiRuntimeTuningKeys::SoldierMaxHealth, 100.0,
		static_cast<double>(std::numeric_limits<float>::denorm_min()), 1000000000.0);
	Register(GuLiRuntimeTuningKeys::SoldierDefense, 0.0, 0.0, 1000000.0);
	Register(GuLiRuntimeTuningKeys::ShipMaxSpeedMultiplier, 1.0, 0.0, 100.0);
	Register(GuLiRuntimeTuningKeys::ShipAccelerationMultiplier, 1.0, 0.0, 100.0);
}

bool FGuLiRuntimeTuningRegistry::SetBaseline(
	const FName Key,
	const double Value,
	const EGuLiRuntimeTuningValueSource Source,
	FString* OutError)
{
	FEntry* Entry = Entries.Find(Key);
	if (!Entry)
	{
		if (OutError)
		{
			*OutError = GuLiRuntimeTuningKeys::UnknownKeyError(Key.ToString());
		}
		return false;
	}
	if (Source == EGuLiRuntimeTuningValueSource::GMOverride)
	{
		if (OutError)
		{
			*OutError = TEXT("a baseline cannot use the GMOverride source");
		}
		return false;
	}

	FString Error;
	if (!IsValidValue(*Entry, Value, Error))
	{
		if (OutError)
		{
			*OutError = MoveTemp(Error);
		}
		return false;
	}

	Entry->Baseline = Value;
	Entry->BaselineSource = Source;
	return true;
}

FGuLiRuntimeTuningResult FGuLiRuntimeTuningRegistry::Get(const FString& Key) const
{
	const FString TrimmedKey = Key.TrimStartAndEnd();
	const FEntry* Entry = Entries.Find(FName(*TrimmedKey));
	if (!Entry)
	{
		FGuLiRuntimeTuningResult Result;
		Result.Error = GuLiRuntimeTuningKeys::UnknownKeyError(TrimmedKey);
		return Result;
	}
	return MakeResult(*Entry);
}

FGuLiRuntimeTuningResult FGuLiRuntimeTuningRegistry::Set(
	const FString& Key,
	const FString& ValueText)
{
	const FString TrimmedKey = Key.TrimStartAndEnd();
	FEntry* Entry = Entries.Find(FName(*TrimmedKey));
	if (!Entry)
	{
		FGuLiRuntimeTuningResult Result;
		Result.Error = GuLiRuntimeTuningKeys::UnknownKeyError(TrimmedKey);
		return Result;
	}

	double ParsedValue = 0.0;
	if (!GuLiRuntimeTuning::TryParseFiniteNumber(ValueText, ParsedValue))
	{
		FGuLiRuntimeTuningResult Result = MakeResult(*Entry);
		Result.bSuccess = false;
		Result.Error = FString::Printf(TEXT("'%s' is not a finite number"), *ValueText);
		return Result;
	}

	FString Error;
	if (!IsValidValue(*Entry, ParsedValue, Error))
	{
		FGuLiRuntimeTuningResult Result = MakeResult(*Entry);
		Result.bSuccess = false;
		Result.Error = MoveTemp(Error);
		return Result;
	}

	const double PreviousEffective = Entry->GetEffective();
	const bool bHadOverride = Entry->bHasOverride;
	Entry->Override = ParsedValue;
	Entry->bHasOverride = true;
	FGuLiRuntimeTuningResult Result = MakeResult(*Entry);
	Result.PreviousEffective = PreviousEffective;
	// An equal-valued override still changes all other types that have their own baselines.
	Result.bChanged = !bHadOverride || PreviousEffective != Result.Effective;
	return Result;
}

FGuLiRuntimeTuningResult FGuLiRuntimeTuningRegistry::Reset(const FString& Key)
{
	const FString TrimmedKey = Key.TrimStartAndEnd();
	FEntry* Entry = Entries.Find(FName(*TrimmedKey));
	if (!Entry)
	{
		FGuLiRuntimeTuningResult Result;
		Result.Error = GuLiRuntimeTuningKeys::UnknownKeyError(TrimmedKey);
		return Result;
	}

	const double PreviousEffective = Entry->GetEffective();
	const bool bHadOverride = Entry->bHasOverride;
	Entry->bHasOverride = false;
	FGuLiRuntimeTuningResult Result = MakeResult(*Entry);
	Result.PreviousEffective = PreviousEffective;
	Result.bChanged = bHadOverride;
	return Result;
}

TArray<FGuLiRuntimeTuningResult> FGuLiRuntimeTuningRegistry::ResetAll()
{
	TArray<FGuLiRuntimeTuningResult> Results;
	TArray<FName> Keys;
	Entries.GenerateKeyArray(Keys);
	Keys.Sort(FNameLexicalLess());
	Results.Reserve(Keys.Num());
	for (const FName Key : Keys)
	{
		Results.Add(Reset(Key.ToString()));
	}
	return Results;
}

TArray<FGuLiRuntimeTuningEntryView> FGuLiRuntimeTuningRegistry::List(
	const FString& Prefix) const
{
	const FString TrimmedPrefix = Prefix.TrimStartAndEnd();
	TArray<FGuLiRuntimeTuningEntryView> Result;
	for (const TPair<FName, FEntry>& Pair : Entries)
	{
		const FEntry& Entry = Pair.Value;
		if (!TrimmedPrefix.IsEmpty()
			&& !Entry.Key.ToString().StartsWith(TrimmedPrefix, ESearchCase::IgnoreCase))
		{
			continue;
		}

		FGuLiRuntimeTuningEntryView& View = Result.AddDefaulted_GetRef();
		View.Key = Entry.Key;
		View.Minimum = Entry.Minimum;
		View.Maximum = Entry.Maximum;
		View.Baseline = Entry.Baseline;
		View.Effective = Entry.GetEffective();
		View.Source = Entry.GetSource();
		View.bIntegral = Entry.bIntegral;
	}
	Result.Sort([](const FGuLiRuntimeTuningEntryView& Left, const FGuLiRuntimeTuningEntryView& Right)
	{
		return Left.Key.LexicalLess(Right.Key);
	});
	return Result;
}

const TCHAR* FGuLiRuntimeTuningRegistry::LexToString(
	const EGuLiRuntimeTuningValueSource Source)
{
	switch (Source)
	{
	case EGuLiRuntimeTuningValueSource::CppFallback:
		return TEXT("C++Fallback");
	case EGuLiRuntimeTuningValueSource::DataTable:
		return TEXT("DataTable");
	case EGuLiRuntimeTuningValueSource::GMOverride:
		return TEXT("GMOverride");
	default:
		return TEXT("Unknown");
	}
}

void FGuLiRuntimeTuningRegistry::Register(
	const TCHAR* Key,
	const double Fallback,
	const double Minimum,
	const double Maximum,
	const bool bIntegral)
{
	FEntry Entry;
	Entry.Key = FName(Key);
	Entry.Minimum = Minimum;
	Entry.Maximum = Maximum;
	Entry.Baseline = Fallback;
	Entry.BaselineSource = EGuLiRuntimeTuningValueSource::CppFallback;
	Entry.bIntegral = bIntegral;
	Entries.Add(Entry.Key, Entry);
}

bool FGuLiRuntimeTuningRegistry::IsValidValue(
	const FEntry& Entry,
	const double Value,
	FString& OutError)
{
	if (!FMath::IsFinite(Value))
	{
		OutError = TEXT("value must be finite");
		return false;
	}
	if (Entry.Key == FName(GuLiRuntimeTuningKeys::SoldierMaxHealth)
		&& (Value <= 0.0 || static_cast<float>(Value) <= 0.0f))
	{
		OutError = TEXT("maximum health must be a positive representable float, at most 1e9");
		return false;
	}
	if (Value < Entry.Minimum || Value > Entry.Maximum)
	{
		OutError = FString::Printf(
			TEXT("value %.17g is outside [%.17g, %.17g]"),
			Value,
			Entry.Minimum,
			Entry.Maximum);
		return false;
	}
	if (Entry.bIntegral && Value != FMath::RoundToDouble(Value))
	{
		OutError = TEXT("value must be an integer");
		return false;
	}
	return true;
}

FGuLiRuntimeTuningResult FGuLiRuntimeTuningRegistry::MakeResult(const FEntry& Entry)
{
	FGuLiRuntimeTuningResult Result;
	Result.bSuccess = true;
	Result.Key = Entry.Key;
	Result.Baseline = Entry.Baseline;
	Result.PreviousEffective = Entry.GetEffective();
	Result.Effective = Entry.GetEffective();
	Result.Source = Entry.GetSource();
	return Result;
}

bool GuLiRuntimeTuning::TryParseFiniteNumber(const FString& ValueText, double& OutValue)
{
	const FString TrimmedValue = ValueText.TrimStartAndEnd();
	double ParsedValue = 0.0;
	if (!GuLiRuntimeTuningKeys::IsStrictNumber(TrimmedValue)
		|| !LexTryParseString(ParsedValue, *TrimmedValue) || !FMath::IsFinite(ParsedValue))
	{
		return false;
	}
	OutValue = ParsedValue;
	return true;
}

float GuLiRuntimeTuning::ScaleHealthPreservingRatio(
	const float CurrentHealth,
	const float PreviousMaximumHealth,
	const float NewMaximumHealth)
{
	if (!FMath::IsFinite(CurrentHealth) || CurrentHealth <= 0.0f
		|| !FMath::IsFinite(NewMaximumHealth) || NewMaximumHealth <= 0.0f)
	{
		return 0.0f;
	}
	if (!FMath::IsFinite(PreviousMaximumHealth) || PreviousMaximumHealth <= 0.0f)
	{
		return FMath::Min(CurrentHealth, NewMaximumHealth);
	}

	const double HealthRatio = FMath::Clamp(static_cast<double>(CurrentHealth)
		/ static_cast<double>(PreviousMaximumHealth), 0.0, 1.0);
	const float ScaledHealth = static_cast<float>(HealthRatio * static_cast<double>(NewMaximumHealth));
	return FMath::Min(NewMaximumHealth,
		FMath::Max(ScaledHealth, std::numeric_limits<float>::denorm_min()));
}

float GuLiRuntimeTuning::CalculatePredictionDistance(
	const float EffectiveMoveSpeedCmPerSecond,
	const float PredictionDurationSeconds,
	const float FallbackDistanceCentimeters)
{
	if (FMath::IsFinite(EffectiveMoveSpeedCmPerSecond)
		&& EffectiveMoveSpeedCmPerSecond > 0.0f
		&& FMath::IsFinite(PredictionDurationSeconds)
		&& PredictionDurationSeconds >= 0.0f)
	{
		return EffectiveMoveSpeedCmPerSecond * PredictionDurationSeconds;
	}
	return FMath::IsFinite(FallbackDistanceCentimeters)
		? FMath::Max(0.0f, FallbackDistanceCentimeters)
		: 0.0f;
}
