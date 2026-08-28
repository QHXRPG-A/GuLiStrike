// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderGameMode.h"

#include "Commander/Framework/GuLiCommanderGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"

AGuLiCommanderGameMode::AGuLiCommanderGameMode()
{
	GameStateClass = AGuLiCommanderGameState::StaticClass();
	PlayerStateClass = AGuLiCommanderPlayerState::StaticClass();
	PlayerControllerClass = AGuLiCommanderPlayerController::StaticClass();
	DefaultPawnClass = AGuLiCommanderCameraPawn::StaticClass();
	SpectatorClass = ASpectatorPawn::StaticClass();
	HUDClass = AGuLiCommanderHUD::StaticClass();
	bUseSeamlessTravel = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.0f;
}

void AGuLiCommanderGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (AGuLiCommanderGameState* CommanderGameState = GetGameState<AGuLiCommanderGameState>())
	{
		CommanderGameState->InitializeServerMatchState();
	}

	EnsureSoldierStateReplicator();
	EnsurePresentationActor();
}

void AGuLiCommanderGameMode::EnsurePresentationActor()
{
	if (!HasAuthority() || IsValid(PresentationActor) || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
	{
		PresentationActor = *It;
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("GuLiCommanderPresentation");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PresentationActor = GetWorld()->SpawnActor<AGuLiCommanderPresentationActor>(
		AGuLiCommanderPresentationActor::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
}

void AGuLiCommanderGameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		PublishSoldierSnapshotAndPoses();
	}
}

void AGuLiCommanderGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (!HasAuthority())
	{
		return;
	}

	EnsureSoldierStateReplicator();
	if (AGuLiCommanderPlayerController* CommanderController = Cast<AGuLiCommanderPlayerController>(NewPlayer))
	{
		AssignRoleAndBootstrap(*CommanderController);
	}
}

void AGuLiCommanderGameMode::Logout(AController* Exiting)
{
	if (HasAuthority() && Exiting)
	{
		if (const AGuLiCommanderPlayerState* CommanderPlayerState = Exiting->GetPlayerState<AGuLiCommanderPlayerState>())
		{
			if (AGuLiCommanderGameState* CommanderGameState = GetGameState<AGuLiCommanderGameState>())
			{
				CommanderGameState->ReleaseRoleSlot(
					CommanderPlayerState->GetCommanderSlotIndex(),
					CommanderPlayerState->GetPlayerGuid());
			}
		}
	}

	Super::Logout(Exiting);
}

void AGuLiCommanderGameMode::EnsureSoldierStateReplicator()
{
	if (!HasAuthority() || IsValid(SoldierStateReplicator) || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It)
	{
		SoldierStateReplicator = *It;
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("GuLiSoldierStateReplicator");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SoldierStateReplicator = GetWorld()->SpawnActor<AGuLiSoldierStateReplicator>(
		AGuLiSoldierStateReplicator::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
}

void AGuLiCommanderGameMode::PublishSoldierSnapshotAndPoses()
{
	EnsureSoldierStateReplicator();
	if (!IsValid(SoldierStateReplicator) || !GetWorld())
	{
		return;
	}

	UGuLiBattleAuthoritySubsystem* Authority =
		GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	if (!Authority || !Authority->HasSpawnedAuthorityPopulation())
	{
		return;
	}
	const uint32 CurrentSimTick = Authority->GetServerSimTick();
	constexpr uint32 SimulationTicksPerPoseFrame = 30u / GULI_POSE_CAPTURE_RATE_HZ;
	if (CurrentSimTick == 0u || CurrentSimTick == LastPoseChunkDispatchSimTick)
	{
		return;
	}
	LastPoseChunkDispatchSimTick = CurrentSimTick;

	const AGuLiCommanderGameState* CommanderGameState = GetGameState<AGuLiCommanderGameState>();
	const uint32 MatchEpoch = CommanderGameState ? CommanderGameState->GetMatchEpoch() : 0u;
	if (MatchEpoch == 0u)
	{
		return;
	}

	const bool bCapturePoseFrame = PendingPoseChunks.IsEmpty()
		&& (LastPublishedPoseSimTick == 0u
			|| CurrentSimTick - LastPublishedPoseSimTick >= SimulationTicksPerPoseFrame);
	if (bCapturePoseFrame)
	{
		LastPublishedPoseSimTick = CurrentSimTick;
		TArray<FGuLiSoldierStateItem> SoldierStates;
		Authority->BuildSoldierStateSnapshot(SoldierStates);
		SoldierStateReplicator->ApplyAuthoritySnapshot(SoldierStates, MatchEpoch);

		PendingPoseChunks.Reset();
		Authority->CaptureSoldierPoseChunks(PendingPoseChunks, MatchEpoch);
		PendingPoseChunkOffset = 0;
		PendingPoseChunkStartIndex = PendingPoseChunks.IsEmpty()
			? 0
			: static_cast<int32>(
				PendingPoseChunks[0].FrameSequence
				% static_cast<uint32>(PendingPoseChunks.Num()));
	}

	if (PendingPoseChunks.IsEmpty()
		|| PendingPoseChunkOffset >= PendingPoseChunks.Num())
	{
		return;
	}

	const int32 ChunksPerDispatch = FMath::DivideAndRoundUp(
		PendingPoseChunks.Num(),
		static_cast<int32>(SimulationTicksPerPoseFrame));
	const int32 DispatchChunkCount = FMath::Min(
		ChunksPerDispatch,
		PendingPoseChunks.Num() - PendingPoseChunkOffset);
	for (TActorIterator<AGuLiCommanderPlayerController> It(GetWorld()); It; ++It)
	{
		if (UGuLiCommanderNetSyncComponent* NetSync = It->GetCommanderNetSyncComponent())
		{
			const AGuLiCommanderPlayerState* CommanderPlayerState =
				It->GetPlayerState<AGuLiCommanderPlayerState>();
			NetSync->EnsureServerBootstrapForMatch(MatchEpoch);
			if (!CommanderPlayerState || !CommanderPlayerState->IsSyncReady())
			{
				// Initial login can precede Mass creation, while seamless travel can retain
				// an old non-zero generation. EnsureServerBootstrapForMatch handles both
				// cases without repeatedly incrementing a current generation.
				continue;
			}

			NetSync->RefreshServerSelection();
			for (int32 ChunkOffset = 0; ChunkOffset < DispatchChunkCount; ++ChunkOffset)
			{
				const int32 OrderedChunkOffset = PendingPoseChunkOffset + ChunkOffset;
				const int32 ChunkIndex =
					(PendingPoseChunkStartIndex + OrderedChunkOffset) % PendingPoseChunks.Num();
				NetSync->SendPoseChunk(
					PendingPoseChunks[ChunkIndex]);
			}
		}
	}

	// Pose chunks are an unreliable, perishable snapshot. Advance this frame even
	// when there is no ready client; otherwise a server that has been running
	// empty can retain one old frame indefinitely and feed it to the next login.
	PendingPoseChunkOffset += DispatchChunkCount;
	if (PendingPoseChunkOffset >= PendingPoseChunks.Num())
	{
		PendingPoseChunks.Reset();
		PendingPoseChunkOffset = 0;
		PendingPoseChunkStartIndex = 0;
	}
}

void AGuLiCommanderGameMode::AssignRoleAndBootstrap(
	AGuLiCommanderPlayerController& CommanderController)
{
	AGuLiCommanderPlayerState* CommanderPlayerState =
		CommanderController.GetPlayerState<AGuLiCommanderPlayerState>();
	AGuLiCommanderGameState* CommanderGameState = GetGameState<AGuLiCommanderGameState>();
	if (!CommanderPlayerState || !CommanderGameState)
	{
		CommanderController.StartSpectatingOnly();
		return;
	}

	CommanderPlayerState->EnsureServerPlayerGuid();

	uint8 AssignedSlotIndex = AGuLiCommanderPlayerState::InvalidSlotIndex;
	EGuLiTeam AssignedTeam = EGuLiTeam::Unassigned;
	EGuLiCommanderRole AssignedRole = EGuLiCommanderRole::Observer;
	const bool bClaimedGameplaySlot = CommanderGameState->ClaimRoleSlot(
		CommanderPlayerState->GetPlayerGuid(),
		CommanderPlayerState->GetCommanderSlotIndex(),
		AssignedSlotIndex,
		AssignedTeam,
		AssignedRole);

	if (bClaimedGameplaySlot)
	{
		CommanderPlayerState->SetServerRoleAssignment(AssignedTeam, AssignedRole, AssignedSlotIndex);
	}
	else
	{
		// Connections beyond the ten unique 5v5 slots never receive command authority.
		CommanderPlayerState->SetServerObserver();
		CommanderController.StartSpectatingOnly();
	}

	if (UGuLiCommanderNetSyncComponent* NetSync = CommanderController.GetCommanderNetSyncComponent())
	{
		NetSync->StartServerBootstrap();
	}
}
