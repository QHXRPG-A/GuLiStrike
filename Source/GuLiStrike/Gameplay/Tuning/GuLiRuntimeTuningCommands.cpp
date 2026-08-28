// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"

#include "Engine/World.h"
#include "GuLiStrike.h"

#if !UE_BUILD_SHIPPING

#include "HAL/IConsoleManager.h"

namespace GuLiRuntimeTuningCommands
{
	UGuLiRuntimeTuningSubsystem* ResolveSubsystem(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("gs.GM: no World was supplied."));
			return nullptr;
		}
		UGuLiRuntimeTuningSubsystem* Subsystem = World->GetSubsystem<UGuLiRuntimeTuningSubsystem>();
		if (!Subsystem)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("gs.GM: runtime tuning is unavailable in this World."));
		}
		return Subsystem;
	}

	void LogResult(const TCHAR* Operation, const FGuLiRuntimeTuningResult& Result)
	{
		if (!Result.bSuccess)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("gs.GM.%s rejected: %s"),
				Operation,
				*Result.Error);
			return;
		}

		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("gs.GM.%s key=%s baseline=%.17g old_effective=%.17g effective=%.17g source=%s applied=%d"),
			Operation,
			*Result.Key.ToString(),
			Result.Baseline,
			Result.PreviousEffective,
			Result.Effective,
			FGuLiRuntimeTuningRegistry::LexToString(Result.Source),
			Result.AppliedInstanceCount);
	}

	void List(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() > 1)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Usage: gs.GM.List [prefix]"));
			return;
		}
		UGuLiRuntimeTuningSubsystem* Subsystem = ResolveSubsystem(World);
		if (!Subsystem)
		{
			return;
		}

		const FString Prefix = Args.IsEmpty() ? FString() : Args[0];
		const TArray<FGuLiRuntimeTuningEntryView> Entries = Subsystem->ListValues(Prefix);
		for (const FGuLiRuntimeTuningEntryView& Entry : Entries)
		{
			UE_LOG(
				LogGuLiStrike,
				Display,
				TEXT("gs.GM key=%s baseline=%.17g effective=%.17g source=%s range=[%.17g,%.17g]%s"),
				*Entry.Key.ToString(),
				Entry.Baseline,
				Entry.Effective,
				FGuLiRuntimeTuningRegistry::LexToString(Entry.Source),
				Entry.Minimum,
				Entry.Maximum,
				Entry.bIntegral ? TEXT(" integer") : TEXT(""));
		}
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("gs.GM.List matched %d key(s)."),
			Entries.Num());
	}

	void Get(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Usage: gs.GM.Get <key>"));
			return;
		}
		if (UGuLiRuntimeTuningSubsystem* Subsystem = ResolveSubsystem(World))
		{
			LogResult(TEXT("Get"), Subsystem->GetValue(Args[0]));
		}
	}

	void Set(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 2)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Usage: gs.GM.Set <key> <value>"));
			return;
		}
		if (UGuLiRuntimeTuningSubsystem* Subsystem = ResolveSubsystem(World))
		{
			LogResult(TEXT("Set"), Subsystem->SetValue(Args[0], Args[1]));
		}
	}

	void Reset(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Usage: gs.GM.Reset <key|all>"));
			return;
		}
		if (UGuLiRuntimeTuningSubsystem* Subsystem = ResolveSubsystem(World))
		{
			for (const FGuLiRuntimeTuningResult& Result : Subsystem->ResetValues(Args[0]))
			{
				LogResult(TEXT("Reset"), Result);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ListCommand(
		TEXT("gs.GM.List"),
		TEXT("List runtime-tuning keys: gs.GM.List [prefix]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&List));

	FAutoConsoleCommandWithWorldAndArgs GetCommand(
		TEXT("gs.GM.Get"),
		TEXT("Read one runtime-tuning key: gs.GM.Get <key>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Get));

	FAutoConsoleCommandWithWorldAndArgs SetCommand(
		TEXT("gs.GM.Set"),
		TEXT("Set one World-session override: gs.GM.Set <key> <value>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Set));

	FAutoConsoleCommandWithWorldAndArgs ResetCommand(
		TEXT("gs.GM.Reset"),
		TEXT("Reset one or all World-session overrides: gs.GM.Reset <key|all>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Reset));
}

#endif // !UE_BUILD_SHIPPING
