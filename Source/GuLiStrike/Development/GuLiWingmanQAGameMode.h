// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Framework/GuLiCommanderGameMode.h"
#include "GuLiWingmanQAGameMode.generated.h"

class APlayerStart;

/**
 * Non-Shipping command-line fixture for the formal Wingman Dedicated campaign.
 *
 * It retains the production Commander publisher/simulation and normal Battle role,
 * spawn, possession, Ship, Relay and Wingman paths. The only fixture behavior is a
 * deterministic role priority and collision-free PlayerStart layout created at runtime.
 */
UCLASS(NotBlueprintable)
class GULISTRIKE_API AGuLiWingmanQAGameMode final : public AGuLiCommanderGameMode
{
	GENERATED_BODY()

public:
	AGuLiWingmanQAGameMode();

	virtual void BeginPlay() override;
	virtual AActor* FindPlayerStart_Implementation(
		AController* Player,
		const FString& IncomingName = TEXT("")) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

private:
	AActor* FindDeterministicPlayerStart(AController* Player) const;
	void CreateDeterministicPlayerStarts();
	bool SelectNavigableAirStarts(TArray<FTransform>& InOutSlotTransforms) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<APlayerStart>> QAPlayerStarts;
};
