#include "UI/Cards/GuLiCardRevealViewportLibrary.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SWindow.h"

bool UGuLiCardRevealViewportLibrary::IsCardRevealViewportFocused(const APlayerController* PlayerController)
{
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController() || !FSlateApplication::IsInitialized())
	{
		return false;
	}

	const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return false;
	}

	const TSharedPtr<SWindow> Window = ViewportClient->GetWindow();
	// Keyboard focus can legitimately belong to the presentation UI. Window activation
	// is the boundary that must also invalidate a cached cursor position after Alt+Tab.
	return FSlateApplication::Get().IsActive() && Window.IsValid() && Window->IsActive();
}
