// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikeSpawner.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GuLiStrikeNPC.h"
#include "GuLiStrikeGameMode.h"

AGuLiStrikeSpawner::AGuLiStrikeSpawner()
{
 	PrimaryActorTick.bCanEverTick = true;

}

void AGuLiStrikeSpawner::BeginPlay()
{
	Super::BeginPlay();
	
	// find the recast navmesh actor on the level
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARecastNavMesh::StaticClass(), ActorList);

	if (ActorList.Num() > 0)
	{
		NavData = Cast<ARecastNavMesh>(ActorList[0]);
	} else {

		UE_LOG(LogTemp, Log, TEXT("Could not find recast navmesh"));

	}

	// set up the spawn timer
	GetWorld()->GetTimerManager().SetTimer(SpawnGroupTimer, this, &AGuLiStrikeSpawner::SpawnNPCGroup, SpawnGroupDelay, true);

	// spawn the first group of NPCs
	SpawnNPCGroup();
}

void AGuLiStrikeSpawner::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the spawn timers
	GetWorld()->GetTimerManager().ClearTimer(SpawnGroupTimer);
	GetWorld()->GetTimerManager().ClearTimer(SpawnNPCTimer);
}

void AGuLiStrikeSpawner::SpawnNPCGroup()
{
	// reset the group spawn counter
	SpawnCount = 0;

	// check if we're still under the max NPC cap
	if (AGuLiStrikeGameMode* GM = Cast<AGuLiStrikeGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (GM->CanSpawnNPCs())
		{
			SpawnNPC();
		}
	}
}

void AGuLiStrikeSpawner::SpawnNPC()
{
	FTransform SpawnTransform;

	// find a random point around the spawner
	FVector SpawnLoc;
	if (UNavigationSystemV1::K2_GetRandomReachablePointInRadius(GetWorld(), GetActorLocation(), SpawnLoc, SpawnRadius, NavData))
	{
		SpawnTransform.SetLocation(SpawnLoc);

		// spawn the NPC
		AGuLiStrikeNPC* NPC = GetWorld()->SpawnActor<AGuLiStrikeNPC>(NPCClass, SpawnTransform);
	}

	// increase the spawn counter
	++SpawnCount;

	// do we still have enemies left to spawn?
	if (SpawnCount < SpawnGroupSize)
	{
		GetWorld()->GetTimerManager().SetTimer(SpawnNPCTimer, this, &AGuLiStrikeSpawner::SpawnNPC, FMath::RandRange(MinSpawnDelay, MaxSpawnDelay), false);
	}

}
