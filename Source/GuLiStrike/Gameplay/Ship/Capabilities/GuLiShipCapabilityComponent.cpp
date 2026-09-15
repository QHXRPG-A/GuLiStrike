#include "Gameplay/Ship/Capabilities/GuLiShipCapabilityComponent.h"
#include "Engine/DataAsset.h"

UGuLiShipCapabilityComponent::UGuLiShipCapabilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UGuLiShipCapabilityComponent::ValidateConfiguration(const UDataAsset*, FString&) const { return true; }

void UGuLiShipCapabilityComponent::BuildRuntimeView(FGuLiShipCapabilityRuntimeView& View) const
{
	View.GroupId = SourceGroupId;
	View.CapabilityId = CapabilityId;
	View.State = State;
}

void UGuLiShipCapabilityComponent::Prepare(FName InGroupId, FName InCapabilityId, UDataAsset* Configuration)
{
	check(State == EGuLiShipCapabilityState::Prepared && !InGroupId.IsNone() && !InCapabilityId.IsNone());
	SourceGroupId = InGroupId; CapabilityId = InCapabilityId; CapabilityConfiguration = Configuration;
	OnPrepared();
}

void UGuLiShipCapabilityComponent::SetCapabilityEnabled(bool bEnabled)
{
	check(State != EGuLiShipCapabilityState::Released);
	const auto Next = bEnabled ? EGuLiShipCapabilityState::Enabled : EGuLiShipCapabilityState::Suspended;
	if (State == Next) return;
	State = Next;
	if (bEnabled) OnEnabled(); else OnSuspended();
	StateChanged.Broadcast(this, State);
}

void UGuLiShipCapabilityComponent::ReleaseCapability()
{
	if (State == EGuLiShipCapabilityState::Released) return;
	if (State == EGuLiShipCapabilityState::Enabled) SetCapabilityEnabled(false);
	State = EGuLiShipCapabilityState::Released;
	OnReleased();
}

bool UGuLiShipCapabilityComponent::RequestActivation(FName, FString& Error)
{
	Error = TEXT("This capability has no requested action.");
	return false;
}

void UGuLiShipCapabilityComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseCapability();
	Super::EndPlay(Reason);
}
