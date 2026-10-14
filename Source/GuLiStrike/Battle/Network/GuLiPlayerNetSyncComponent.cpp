// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Network/GuLiPlayerNetSyncComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool FGuLiNetworkRequestWindow::Consume(
	const double NowSeconds,
	const int32 MaximumRequests,
	const double WindowSeconds)
{
	if (!FMath::IsFinite(NowSeconds) || !FMath::IsFinite(WindowSeconds)
		|| MaximumRequests <= 0 || WindowSeconds <= 0.0)
	{
		return false;
	}
	RequestTimes.RemoveAll([NowSeconds, WindowSeconds](const double RequestTime)
	{
		return NowSeconds < RequestTime || NowSeconds - RequestTime >= WindowSeconds;
	});
	if (RequestTimes.Num() >= MaximumRequests)
	{
		return false;
	}
	RequestTimes.Add(NowSeconds);
	return true;
}

UGuLiPlayerNetSyncComponent::UGuLiPlayerNetSyncComponent()
{
    SetIsReplicatedByDefault(false);
    PrimaryComponentTick.bCanEverTick=false;
    PrimaryComponentTick.bStartWithTickEnabled=false;
}
APlayerController* UGuLiPlayerNetSyncComponent::GetOwningPlayerController() const
{ return Cast<APlayerController>(GetOwner()); }
AGuLiBattlePlayerState* UGuLiPlayerNetSyncComponent::GetBattlePlayerState() const
{
    const auto* Controller=GetOwningPlayerController();
    return Controller?Controller->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
}
AGuLiBattleGameState* UGuLiPlayerNetSyncComponent::GetBattleGameState() const
{ return GetWorld()?GetWorld()->GetGameState<AGuLiBattleGameState>():nullptr; }
bool UGuLiPlayerNetSyncComponent::IsConnectionReady() const
{
    const auto* Hybrid=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    const auto* Player=GetBattlePlayerState();
    return Hybrid && Hybrid->IsWorldReady() && Player && Player->IsBattleReady();
}
uint32 UGuLiPlayerNetSyncComponent::GetConnectionGeneration() const
{
    const auto* Battle=GetBattleGameState();
    return IsConnectionReady() && Battle?Battle->GetMatchEpoch():0;
}
