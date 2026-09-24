// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_EDITOR

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "MeshUtilities.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"

namespace GuLiCommanderEditorCommands
{
	constexpr TCHAR SourceMeshPath[] =
		TEXT("/Game/Assets/Arma/FourFRobot/cannon_war_machine/SkeletalMeshes/cannon_war_machine.cannon_war_machine");
	constexpr TCHAR DestinationPackagePath[] =
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot");

	void BuildFourFRobotStaticProxy(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		if (!World || !World->IsEditorWorld())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander proxy build requires an editor world."));
			return;
		}

		if (LoadObject<UStaticMesh>(nullptr, DestinationPackagePath) != nullptr)
		{
			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("Commander static proxy already exists at %s; refusing to overwrite."),
				DestinationPackagePath);
			return;
		}

		USkeletalMesh* SourceMesh = LoadObject<USkeletalMesh>(nullptr, SourceMeshPath);
		if (!SourceMesh)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not load Commander source mesh %s."), SourceMeshPath);
			return;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("GuLiCommanderProxyBuildSource");
		SpawnParameters.ObjectFlags = RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* TemporaryActor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (!TemporaryActor)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not create the temporary Commander proxy actor."));
			return;
		}

		USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(
			TemporaryActor,
			TEXT("ProxySourceMesh"),
			RF_Transient);
		TemporaryActor->SetRootComponent(MeshComponent);
		TemporaryActor->AddInstanceComponent(MeshComponent);
		MeshComponent->SetSkeletalMeshAsset(SourceMesh);
		MeshComponent->SetVisibility(true);
		MeshComponent->SetHiddenInGame(false);
		MeshComponent->RegisterComponentWithWorld(World);
		MeshComponent->RefreshBoneTransforms();
		MeshComponent->MarkRenderStateDirty();
		FlushRenderingCommands();

		TArray<UMeshComponent*> MeshComponents;
		MeshComponents.Add(MeshComponent);
		IMeshUtilities& MeshUtilities =
			FModuleManager::LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
		UStaticMesh* StaticMesh = MeshUtilities.ConvertMeshesToStaticMesh(
			MeshComponents,
			MeshComponent->GetComponentTransform(),
			DestinationPackagePath);

		TemporaryActor->Destroy();
		if (!StaticMesh)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("MeshUtilities failed to build the Commander static proxy."));
			return;
		}

		StaticMesh->MarkPackageDirty();
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Built Commander FourFRobot static proxy: %s"),
			*StaticMesh->GetPathName());
	}

	FAutoConsoleCommandWithWorldAndArgs BuildFourFRobotStaticProxyCommand(
		TEXT("gs.Commander.BuildFourFRobotProxy"),
		TEXT("Builds /Game/Commander/Units/SM_CommanderFourFRobot from the approved skeletal source asset."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuildFourFRobotStaticProxy));
}

#endif // WITH_EDITOR
