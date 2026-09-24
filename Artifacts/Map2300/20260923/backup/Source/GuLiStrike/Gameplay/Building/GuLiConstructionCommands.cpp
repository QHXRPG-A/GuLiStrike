#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GuLiSpawnBuilderCommand(
	TEXT("guli.builder.Spawn"), TEXT("guli.builder.Spawn red|blue [X Y Z]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || (Args.Num() != 1 && Args.Num() != 4)) return;
		auto& Resources = *World->GetSubsystem<UGuLiResourceWorldSubsystem>();
		if (!Resources.IsRuntimeReady()) return;
		if (!Args[0].Equals(TEXT("red"),ESearchCase::IgnoreCase) && !Args[0].Equals(TEXT("blue"),ESearchCase::IgnoreCase))
		{ UE_LOG(LogTemp,Warning,TEXT("Expected red or blue.")); return; }
		const EGuLiTeam Team = Args[0].Equals(TEXT("red"), ESearchCase::IgnoreCase) ? EGuLiTeam::Red : EGuLiTeam::Blue;
		const auto& Anchors = Resources.GetMapDefinition()->SpawnAnchors;
		FVector Position = Team == EGuLiTeam::Red ? Anchors.RedAssembly : Anchors.BlueAssembly;
		Position.X += 8000;
		if (Args.Num() == 4 && (!LexTryParseString(Position.X,*Args[1]) || !LexTryParseString(Position.Y,*Args[2])
			|| !LexTryParseString(Position.Z,*Args[3]) || Position.ContainsNaN()))
		{ UE_LOG(LogTemp,Warning,TEXT("Expected finite X Y Z coordinates.")); return; }
		Resources.SpawnConstructionVehicle(Team, Position);
	}));
#endif
