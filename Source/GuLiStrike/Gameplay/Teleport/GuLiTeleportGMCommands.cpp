#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#if !UE_BUILD_SHIPPING
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiTeleportGM, Log, All);
namespace
{
	void TeleportGM(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client || Args.Num()<2) { return; }
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		if (Args[1].Equals(TEXT("Red"),ESearchCase::IgnoreCase)) Team = EGuLiTeam::Red;
		if (Args[1].Equals(TEXT("Blue"),ESearchCase::IgnoreCase)) Team = EGuLiTeam::Blue;
		const auto* GS = World->GetGameState(); if (!GS) return;
		for (const APlayerState* Player : GS->PlayerArray)
		{
			const auto* PS = Cast<AGuLiBattlePlayerState>(Player);
			auto* PC = PS ? Cast<APlayerController>(PS->GetOwner()) : nullptr;
			auto* Input = PC ? PC->FindComponentByClass<UGuLiTeleportInputComponent>() : nullptr;
			if (!Input || !PS->IsCommander() || PS->GetTeam()!=Team) continue;
			if (Args[0].Equals(TEXT("Level"),ESearchCase::IgnoreCase))
			{
				int32 Level=0; const bool bValid=Args.Num()==3 && LexTryParseString(Level,*Args[2]) && Input->SetServerLevel(Level);
				UE_LOG(LogGuLiTeleportGM,Display,TEXT("Level accepted=%d level=%d"),bValid,Input->GetLevel()); return;
			}
			const auto State = Input->QueryState();
			if (Args[0].Equals(TEXT("Cancel"),ESearchCase::IgnoreCase)) Input->ServerSubmit(EGuLiTeleportCommand::Cancel,State.CastId,FVector::ZeroVector);
			else if (Args[0].Equals(TEXT("Source"),ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("Destination"),ESearchCase::IgnoreCase))
			{
				FVector Point;
				if (Args.Num()!=5 || !LexTryParseString(Point.X,*Args[2]) || !LexTryParseString(Point.Y,*Args[3]) || !LexTryParseString(Point.Z,*Args[4]) || Point.ContainsNaN()) return;
				Input->ServerSubmit(Args[0].Equals(TEXT("Source"),ESearchCase::IgnoreCase)?EGuLiTeleportCommand::Source:EGuLiTeleportCommand::Destination,State.CastId,Point);
			}
			const auto Result = Input->QueryState();
			UE_LOG(LogGuLiTeleportGM,Display,TEXT("cast=%s phase=%d level=%d units=%d source=%s destination=%s message=%s"),
				*Result.CastId.ToString(),int32(Result.Phase),Result.Config.Level,Result.ParticipantCount,*Result.Source.ToString(),*Result.Destination.ToString(),*Input->GetStatusText().ToString());
			return;
		}
	}
	FAutoConsoleCommandWithWorldAndArgs TeleportCommand(TEXT("gs.GM.Teleport"),
		TEXT("Server: gs.GM.Teleport <Level|Source|Destination|Cancel|Status> <Red|Blue> [1..4 | X Y Z]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TeleportGM));
}
#endif
