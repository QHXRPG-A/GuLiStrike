// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderWorldReplicationComponent.h"
#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiCommanderPoseDispatch, true);

namespace
{
	static_assert(GuLiCommanderSimulationTiming::RateHz % GULI_POSE_CAPTURE_RATE_HZ == 0u);

	uint8 GetPoseRateDivisor(
		const FGuLiQuantizedSoldierPose& Pose,
		const FVector& WorldLocation,
		const FVector& ViewLocation,
		const float FullRateDistanceCentimeters,
		const uint8 FarMovingDivisor,
		const uint8 StationaryDivisor)
	{
		if (Pose.State != EGuLiSoldierPoseState::Moving)
		{
			return StationaryDivisor;
		}
		return FVector::DistSquared2D(WorldLocation, ViewLocation)
			> FMath::Square(static_cast<double>(FullRateDistanceCentimeters))
			? FarMovingDivisor
			: 1u;
	}

	bool BuildConnectionPoseChunk(
		const FGuLiSoldierPoseChunk& Source,
		const FVector& ViewLocation,
		const float FullRateDistanceCentimeters,
		const uint8 FarMovingDivisor,
		const uint8 StationaryDivisor,
		FGuLiSoldierPoseChunk& OutChunk)
	{
		OutChunk = Source;
		OutChunk.Samples.Reset(Source.Samples.Num());
		for (const FGuLiQuantizedSoldierPose& Pose : Source.Samples)
		{
			const FVector WorldLocation = Pose.GetWorldLocationCentimeters();
			const uint8 Divisor = GetPoseRateDivisor(
				Pose,
				WorldLocation,
				ViewLocation,
				FullRateDistanceCentimeters,
				FarMovingDivisor,
				StationaryDivisor);
			const bool bScheduledFrame = Source.FrameSequence % Divisor
				== Pose.SoldierId.Value % Divisor;
			if (Pose.IsTeleport() || bScheduledFrame)
			{
				OutChunk.Samples.Add(Pose);
			}
		}
		return !OutChunk.Samples.IsEmpty();
	}
}

UGuLiCommanderWorldReplicationComponent::UGuLiCommanderWorldReplicationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGuLiCommanderWorldReplicationComponent::BeginPlay()
{
	Super::BeginPlay();
	// 一个 World 只允许其权威 GameMode 上的首个发布组件调度，防止蓝图误重复添加后双倍发送。
	if (!GetWorld() || !GetOwner() || GetOwner() != GetWorld()->GetAuthGameMode()
		|| GetOwner()->FindComponentByClass<UGuLiCommanderWorldReplicationComponent>() != this)
	{
		SetComponentTickEnabled(false);
		return;
	}
	bOwnsSoldierSimulation = true;
	check(FullRatePoseDistanceCentimeters >= 0.0f);
	check(FarMovingPoseFrameDivisor >= 1u);
	check(StationaryPoseFrameDivisor >= 1u);
	EnsureSoldierStateReplicator();
	EnsurePresentationActor();
	if (auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
		Authority->OnSoldierRetiring.AddUObject(this, &ThisClass::OnSoldierRetiring);
}

void UGuLiCommanderWorldReplicationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld()) if (auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
		Authority->OnSoldierRetiring.RemoveAll(this);
	if (bOwnsSoldierSimulation && bSoldierSimulationStarted && GetWorld())
	{
		if (UGuLiBattleAuthoritySubsystem* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
		{
			Authority->SetSoldierSimulationEnabled(false);
		}
	}
	bOwnsSoldierSimulation = false;
	bSoldierSimulationStarted = false;
	Super::EndPlay(EndPlayReason);
}

void UGuLiCommanderWorldReplicationComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (!bSoldierSimulationStarted)
		{
			const UGuLiResourceWorldSubsystem* Resources =
				GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
			if (Resources && Resources->IsRuntimeReady())
			{
				if (UGuLiBattleAuthoritySubsystem* Authority =
					GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
				{
					Authority->SetSoldierSimulationEnabled(true);
					bSoldierSimulationStarted = true;
				}
			}
		}
		PublishSoldierSnapshotAndPoses();
	}
}

void UGuLiCommanderWorldReplicationComponent::EnsurePresentationActor()
{
	if ((!GetOwner() || !GetOwner()->HasAuthority()) || IsValid(PresentationActor) || !GetWorld())
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

void UGuLiCommanderWorldReplicationComponent::EnsureSoldierStateReplicator()
{
	if ((!GetOwner() || !GetOwner()->HasAuthority()) || IsValid(SoldierStateReplicator) || !GetWorld())
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

void UGuLiCommanderWorldReplicationComponent::OnSoldierRetiring(const FGuLiSoldierStateItem& FinalState)
{
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!GameState) return;
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
		if (auto* Sync = It->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
			Sync->QueueSoldierRetirement(FinalState, GameState->GetMatchEpoch());
}

void UGuLiCommanderWorldReplicationComponent::PublishSoldierSnapshotAndPoses()
{
	EnsureSoldierStateReplicator();
	if (!IsValid(SoldierStateReplicator) || !GetWorld()) return;
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!Authority || !Authority->HasSpawnedAuthorityPopulation() || !GameState || !GameState->GetMatchEpoch()) return;
	const uint32 MatchEpoch = GameState->GetMatchEpoch();
	if (PublishedMatchEpoch != MatchEpoch) { PublishedMatchEpoch = MatchEpoch; LastPublishedPoseSimTick = 0; }
	const uint32 CurrentSimTick = Authority->GetServerSimTick();
	constexpr uint32 SimulationTicksPerPoseFrame = GuLiCommanderSimulationTiming::RateHz / GULI_POSE_CAPTURE_RATE_HZ;
	if (!CurrentSimTick || (LastPublishedPoseSimTick && CurrentSimTick - LastPublishedPoseSimTick < SimulationTicksPerPoseFrame)) return;
	LastPublishedPoseSimTick = CurrentSimTick;
	TArray<FGuLiSoldierStateItem> States;
	Authority->BuildSoldierStateSnapshot(States);
	SoldierStateReplicator->ApplyAuthoritySnapshot(States, MatchEpoch);
	TArray<FGuLiSoldierPoseChunk> Chunks;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Capture);
		GuLiCommanderPoseMetrics::FScope Measure(GuLiCommanderPoseMetrics::EScope::Capture);
		Authority->CaptureSoldierPoseChunks(Chunks, MatchEpoch);
	}
	// Capture never waits for a connection to drain. Each connection keeps only its latest unsent sample per ID.
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		auto* Sync = It->FindComponentByClass<UGuLiCommanderNetSyncComponent>();
		if (!Sync) continue;
		Sync->EnsureServerBootstrapForMatch(MatchEpoch);
		if (!Sync->IsSoldierStreamReady()) continue;
		Sync->RefreshServerSelection();
		FVector ViewLocation; FRotator ViewRotation; It->GetPlayerViewPoint(ViewLocation, ViewRotation);
		for (const auto& Source : Chunks)
		{
			FGuLiSoldierPoseChunk Filtered;
			if (BuildConnectionPoseChunk(Source, ViewLocation, FullRatePoseDistanceCentimeters,
				FarMovingPoseFrameDivisor, StationaryPoseFrameDivisor, Filtered)) Sync->SendPoseChunk(Filtered);
		}
	}
}
