// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderPresentationActor.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GuLiStrike.h"

#if !UE_BUILD_SHIPPING

#include "HAL/IConsoleManager.h"

namespace GuLiCommanderPresentationCommands
{
	TArray<AGuLiCommanderPresentationActor*> ResolveActors(UWorld* World)
	{
		TArray<AGuLiCommanderPresentationActor*> Actors;
		if (!World)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("gs.Commander.Presentation: no World was supplied."));
			return Actors;
		}
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("gs.Commander.Presentation: unavailable on a Dedicated Server."));
			return Actors;
		}

		for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				Actors.Add(*It);
			}
		}
		if (Actors.IsEmpty())
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("gs.Commander.Presentation: no presentation actor exists in this World."));
		}
		return Actors;
	}

	void LogResult(const TCHAR* Operation, const FGuLiCommanderPresentationSettingResult& Result)
	{
		if (!Result.bSuccess)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("gs.Commander.Presentation.%s rejected: %s"),
				Operation,
				*Result.Error);
			return;
		}

		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("gs.Commander.Presentation.%s key=%s baseline=%s old_effective=%s effective=%s source=%s applied_actors=%d"),
			Operation,
			*Result.Key.ToString(),
			*Result.Baseline,
			*Result.PreviousEffective,
			*Result.Effective,
			FGuLiCommanderPresentationPerformanceRegistry::LexToString(Result.Source),
			Result.AppliedActorCount);
	}

	void List(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() > 1)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("Usage: gs.Commander.Presentation.List [prefix]"));
			return;
		}

		const TArray<AGuLiCommanderPresentationActor*> Actors = ResolveActors(World);
		if (Actors.IsEmpty())
		{
			return;
		}
		const FString Prefix = Args.IsEmpty() ? FString() : Args[0];
		const TArray<FGuLiCommanderPresentationSettingView> Entries =
			Actors[0]->ListPresentationPerformanceSettings(Prefix);
		for (const FGuLiCommanderPresentationSettingView& Entry : Entries)
		{
			UE_LOG(
				LogGuLiStrike,
				Display,
				TEXT("gs.Commander.Presentation key=%s baseline=%s effective=%s source=%s accepted=%s"),
				*Entry.Key.ToString(),
				*Entry.Baseline,
				*Entry.Effective,
				FGuLiCommanderPresentationPerformanceRegistry::LexToString(Entry.Source),
				*Entry.AcceptedValues);
		}
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("gs.Commander.Presentation.List matched %d key(s) across %d actor(s)."),
			Entries.Num(),
			Actors.Num());
	}

	void Get(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("Usage: gs.Commander.Presentation.Get <key>"));
			return;
		}

		const TArray<AGuLiCommanderPresentationActor*> Actors = ResolveActors(World);
		if (!Actors.IsEmpty())
		{
			LogResult(TEXT("Get"), Actors[0]->GetPresentationPerformanceSetting(Args[0]));
		}
	}

	void Set(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 2)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("Usage: gs.Commander.Presentation.Set <key> <value>"));
			return;
		}

		const TArray<AGuLiCommanderPresentationActor*> Actors = ResolveActors(World);
		if (Actors.IsEmpty())
		{
			return;
		}

		FGuLiCommanderPresentationSettingResult Combined =
			Actors[0]->ApplyLocalPresentationPerformanceOverride(Args[0], Args[1]);
		if (Combined.bSuccess)
		{
			for (int32 Index = 1; Index < Actors.Num(); ++Index)
			{
				const FGuLiCommanderPresentationSettingResult Result =
					Actors[Index]->ApplyLocalPresentationPerformanceOverride(Args[0], Args[1]);
				if (Result.bSuccess)
				{
					Combined.AppliedActorCount += Result.AppliedActorCount;
				}
			}
		}
		LogResult(TEXT("Set"), Combined);
	}

	void Reset(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("Usage: gs.Commander.Presentation.Reset <key|all>"));
			return;
		}

		const TArray<AGuLiCommanderPresentationActor*> Actors = ResolveActors(World);
		if (Actors.IsEmpty())
		{
			return;
		}

		TArray<FGuLiCommanderPresentationSettingResult> Combined =
			Actors[0]->ClearLocalPresentationPerformanceOverrides(Args[0]);
		for (int32 ActorIndex = 1; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			const TArray<FGuLiCommanderPresentationSettingResult> ActorResults =
				Actors[ActorIndex]->ClearLocalPresentationPerformanceOverrides(Args[0]);
			for (int32 ResultIndex = 0;
				ResultIndex < Combined.Num() && ResultIndex < ActorResults.Num();
				++ResultIndex)
			{
				if (Combined[ResultIndex].bSuccess && ActorResults[ResultIndex].bSuccess)
				{
					Combined[ResultIndex].AppliedActorCount +=
						ActorResults[ResultIndex].AppliedActorCount;
				}
			}
		}

		for (const FGuLiCommanderPresentationSettingResult& Result : Combined)
		{
			LogResult(TEXT("Reset"), Result);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ListCommand(
		TEXT("gs.Commander.Presentation.List"),
		TEXT("List local presentation keys: gs.Commander.Presentation.List [prefix]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&List));

	FAutoConsoleCommandWithWorldAndArgs GetCommand(
		TEXT("gs.Commander.Presentation.Get"),
		TEXT("Read one local presentation key: gs.Commander.Presentation.Get <key>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Get));

	FAutoConsoleCommandWithWorldAndArgs SetCommand(
		TEXT("gs.Commander.Presentation.Set"),
		TEXT("Set one World-local presentation override: gs.Commander.Presentation.Set <key> <value>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Set));

	FAutoConsoleCommandWithWorldAndArgs ResetCommand(
		TEXT("gs.Commander.Presentation.Reset"),
		TEXT("Reset local presentation overrides: gs.Commander.Presentation.Reset <key|all>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Reset));
}

#endif // !UE_BUILD_SHIPPING
