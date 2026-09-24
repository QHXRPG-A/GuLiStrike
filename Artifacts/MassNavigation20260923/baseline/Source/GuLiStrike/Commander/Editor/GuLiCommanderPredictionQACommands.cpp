// Copyright Epic Games, Inc. All Rights Reserved.
#include "GuLiStrike.h"

#if !UE_BUILD_SHIPPING
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

namespace GuLiCommanderPredictionQA
{
AGuLiCommanderPresentationActor* FindPresentation(UWorld* World)
{
#if WITH_EDITOR
	if ((!World || !World->IsGameWorld()) && GEditor) World = GEditor->PlayWorld;
#endif
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It) return *It;
	return nullptr;
}

void Start(const TArray<FString>& Args, UWorld* World)
{
	const bool bNoOffset = !Args.IsEmpty() && Args[0].Equals(TEXT("no_offset"), ESearchCase::IgnoreCase);
	uint32 SoldierId = 0;
	float DurationSeconds = 12.0f;
	if (Args.IsEmpty() || Args.Num() > 3
		|| (!bNoOffset && !Args[0].Equals(TEXT("baseline"), ESearchCase::IgnoreCase))
		|| (Args.Num() > 1 && !LexTryParseString(SoldierId, *Args[1]))
		|| (Args.Num() > 2 && !LexTryParseString(DurationSeconds, *Args[2])))
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Usage: gs.Commander.PredictionTrace.Start baseline|no_offset [soldier_id=0] [seconds=12, range 1..120]. Zero binds to the first soldier in an actual move input."));
		return;
	}
	AGuLiCommanderPresentationActor* Presentation = FindPresentation(World);
	if (!Presentation || !Presentation->StartPredictionTrace(bNoOffset, SoldierId, DurationSeconds))
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("PredictionTrace could not start: requires a local game world, valid duration and no active capture. No movement was issued."));
	}
}

void Stop(const TArray<FString>& Args, UWorld* World)
{
	AGuLiCommanderPresentationActor* Presentation = FindPresentation(World);
	FString Output;
	if (!Presentation || !Presentation->IsPredictionTraceActive())
	{
		UE_LOG(LogGuLiStrike, Display, TEXT("PredictionTrace is inactive; default prediction behavior is unchanged."));
		return;
	}
	Presentation->StopPredictionTrace(Output);
}

FAutoConsoleCommandWithWorldAndArgs StartCommand(
	TEXT("gs.Commander.PredictionTrace.Start"),
	TEXT("Record real-input prediction: baseline|no_offset [soldier_id=0] [seconds=12]. Captures one soldier; no_offset temporarily disables displacement only. Auto-restores and exports Project/outputs/commander-selection-20260831/diagnostics/*.csv."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
FAutoConsoleCommandWithWorldAndArgs StopCommand(
	TEXT("gs.Commander.PredictionTrace.Stop"),
	TEXT("Stop prediction recording, restore the temporary displacement override, export CSV. Does not issue movement or alter persistent settings."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Stop));
}
#endif
