// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattlePlayerController.h"

#include "Battle/Framework/GuLiBattleGameMode.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

const FName AGuLiBattlePlayerController::PlayerNetSyncComponentName(TEXT("CommanderNetSync"));

AGuLiBattlePlayerController::AGuLiBattlePlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 只有一个默认网络子对象。Ground/Air 不需要士兵名册也能完成这里的公共握手。
	PlayerNetSyncComponent = CreateDefaultSubobject<UGuLiPlayerNetSyncComponent>(PlayerNetSyncComponentName);
	WingmanRelayComponent = CreateDefaultSubobject<UGuLiWingmanRelayComponent>(TEXT("WingmanRelay"));
}

void AGuLiBattlePlayerController::PawnPendingDestroy(APawn* InPawn)
{
	// 普通 UnPossess/世界切换不是战斗死亡。先保存判断，再让 Super 完成解除占有和 Inactive 状态。
	const bool bNotifyDeath = HasAuthority() && !bEndingPlay && !IsActorBeingDestroyed()
		&& InPawn && InPawn == GetPawn() && InPawn->IsActorBeingDestroyed();
	Super::PawnPendingDestroy(InPawn);

	if (bNotifyDeath && IsValid(this) && !IsActorBeingDestroyed() && GetWorld())
	{
		if (AGuLiBattleGameMode* BattleMode = GetWorld()->GetAuthGameMode<AGuLiBattleGameMode>())
		{
			BattleMode->SchedulePlayerRespawn(*this);
		}
	}
}

void AGuLiBattlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (HasAuthority() && GetWorld())
	{
		if (AGuLiBattleGameMode* BattleMode = GetWorld()->GetAuthGameMode<AGuLiBattleGameMode>())
		{
			BattleMode->CancelPlayerRespawn(*this);
		}
	}
	Super::EndPlay(EndPlayReason);
}
