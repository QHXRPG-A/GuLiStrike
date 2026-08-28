// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/GameMode.h"
#include "GuLiCommanderGameMode.generated.h"

class AGuLiCommanderPresentationActor;
class AGuLiCommanderPlayerController;
class AGuLiCommanderPlayerState;
class AGuLiSoldierStateReplicator;

/** Server-only 5v5 role assignment and commander match bootstrap. */
UCLASS()
class AGuLiCommanderGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AGuLiCommanderGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

private:
	void EnsureSoldierStateReplicator();
	void EnsurePresentationActor();
	void PublishSoldierSnapshotAndPoses();
	void AssignRoleAndBootstrap(AGuLiCommanderPlayerController& CommanderController);

	UPROPERTY(Transient)
	TObjectPtr<AGuLiSoldierStateReplicator> SoldierStateReplicator;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiCommanderPresentationActor> PresentationActor;

	uint32 LastPublishedPoseSimTick = 0u;
	uint32 LastPoseChunkDispatchSimTick = 0u;
	TArray<FGuLiSoldierPoseChunk> PendingPoseChunks;
	int32 PendingPoseChunkOffset = 0;
	int32 PendingPoseChunkStartIndex = 0;
};
