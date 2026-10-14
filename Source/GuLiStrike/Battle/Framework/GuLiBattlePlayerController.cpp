// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattlePlayerController.h"

#include "Battle/Framework/GuLiBattleGameMode.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Gameplay/Presentation/GuLiTeamOutlineCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/GameInstance.h"

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

void AGuLiBattlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if !UE_BUILD_SHIPPING
	RemoveGMPanel(false);
	GMPanel.Reset();
#endif
	Super::EndPlay(EndPlayReason);
}
