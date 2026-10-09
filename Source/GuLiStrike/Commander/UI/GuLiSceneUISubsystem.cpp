#include "Commander/UI/GuLiSceneUISubsystem.h"
#include "Commander/UI/GuLiSceneUIWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UGuLiSceneUIWidget* UGuLiSceneUISubsystem::ForController(APlayerController* PC)
{
	auto* Player=PC ? PC->GetLocalPlayer() : nullptr;
	return Player ? Player->GetSubsystem<UGuLiSceneUISubsystem>()->GetOrCreate(PC) : nullptr;
}
UGuLiSceneUIWidget* UGuLiSceneUISubsystem::GetOrCreate(APlayerController* PC)
{
	if (!PC || PC->IsActorBeingDestroyed())
	{
		if (Widget) Widget->RemoveFromParent(); Widget=nullptr; BoundController.Reset(); return nullptr;
	}
	if (!PC->IsLocalController() || PC->GetLocalPlayer()!=GetLocalPlayer()) return nullptr;
	if (BoundController.Get()!=PC || (Widget && Widget->GetWorld()!=PC->GetWorld()))
	{
		if (Widget) Widget->RemoveFromParent(); Widget=nullptr; BoundController=PC;
	}
	if (!Widget)
	{
		Widget=CreateWidget<UGuLiSceneUIWidget>(PC,UGuLiSceneUIWidget::StaticClass());
		if (Widget) { Widget->InitializeForController(PC); Widget->AddToPlayerScreen(-100); }
	}
	else if (!Widget->IsInViewport())
	{
		// NativeDestruct releases bindings when another UI removes the container.
		Widget->InitializeForController(PC);
		Widget->AddToPlayerScreen(-100);
	}
	return Widget;
}
bool UGuLiSceneUISubsystem::IsTickable() const
{ return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode()!=NM_DedicatedServer; }
TStatId UGuLiSceneUISubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiSceneUIOwner,STATGROUP_Tickables); }
void UGuLiSceneUISubsystem::Tick(float)
{ GetOrCreate(GetLocalPlayer()->GetPlayerController(GetWorld())); }
void UGuLiSceneUISubsystem::Deinitialize()
{ if (Widget) Widget->RemoveFromParent(); Widget=nullptr; BoundController.Reset(); Super::Deinitialize(); }
