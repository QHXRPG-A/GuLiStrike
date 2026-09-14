// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattlePlayerController.h"

#include "Battle/Framework/GuLiBattleGameMode.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Gameplay/Presentation/GuLiTeamOutlineCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#if !UE_BUILD_SHIPPING
#include "Development/GM/SGuLiGMPanel.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"
#endif

const FName AGuLiBattlePlayerController::PlayerNetSyncComponentName(TEXT("CommanderNetSync"));

AGuLiBattlePlayerController::AGuLiBattlePlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 只有一个默认网络子对象。Ground/Air 不需要士兵名册也能完成这里的公共握手。
	PlayerNetSyncComponent = CreateDefaultSubobject<UGuLiPlayerNetSyncComponent>(PlayerNetSyncComponentName);
	WingmanRelayComponent = CreateDefaultSubobject<UGuLiWingmanRelayComponent>(TEXT("WingmanRelay"));
	PlayerCameraManagerClass = AGuLiTeamOutlineCameraManager::StaticClass();
}

void AGuLiBattlePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
#if !UE_BUILD_SHIPPING
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::F10, IE_Pressed, this, &ThisClass::ToggleGMPanel).bConsumeInput = true;
	}
#endif
}

#if !UE_BUILD_SHIPPING
void AGuLiBattlePlayerController::ToggleGMPanel()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const bool bHasViewportContext = World && World->IsGameWorld() && Viewport && LocalPlayer;
	const GuLiGMPanel::EToggleAction Action = GuLiGMPanel::ResolveToggleAction(
		IsLocalController(),
		bHasViewportContext,
		bGMPanelOpen);
	if (Action == GuLiGMPanel::EToggleAction::Close)
	{
		CloseGMPanel();
		return;
	}
	if (Action != GuLiGMPanel::EToggleAction::Open)
	{
		return;
	}
	if (!GMPanel.IsValid())
	{
		GMPanel = SNew(SGuLiGMPanel).Controller(this);
	}

	Viewport->AddViewportWidgetForPlayer(LocalPlayer, GMPanel.ToSharedRef(), 10000);
	bGMPanelOpen = true;
	GMPanel->HandlePanelOpened();

	FlushPressedKeys();
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(GMPanel);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}

void AGuLiBattlePlayerController::CloseGMPanel()
{
	RemoveGMPanel(true);
}

void AGuLiBattlePlayerController::RemoveGMPanel(const bool bRestoreGameplayInput)
{
	if (!bGMPanelOpen)
	{
		return;
	}
	if (GMPanel.IsValid())
	{
		GMPanel->HandlePanelClosed();
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
			{
				Viewport->RemoveViewportWidgetForPlayer(LocalPlayer, GMPanel.ToSharedRef());
			}
		}
	}
	bGMPanelOpen = false;
	if (bRestoreGameplayInput)
	{
		FlushPressedKeys();
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		RestoreGameplayInputAfterGMPanel();
	}
}

void AGuLiBattlePlayerController::RestoreGameplayInputAfterGMPanel()
{
	const GuLiGMPanel::FInputRestorePolicy Policy = GuLiGMPanel::ResolveInputRestorePolicy(false);
	bShowMouseCursor = Policy.bShowCursor;
	bEnableClickEvents = Policy.bEnableClickEvents;
	bEnableMouseOverEvents = Policy.bEnableMouseOverEvents;
	SetInputMode(FInputModeGameOnly());
}
#endif

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
#if !UE_BUILD_SHIPPING
	RemoveGMPanel(false);
	GMPanel.Reset();
#endif
	if (HasAuthority() && GetWorld())
	{
		if (AGuLiBattleGameMode* BattleMode = GetWorld()->GetAuthGameMode<AGuLiBattleGameMode>())
		{
			BattleMode->CancelPlayerRespawn(*this);
		}
	}
	Super::EndPlay(EndPlayReason);
}
