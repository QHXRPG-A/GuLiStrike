#include "Gameplay/Stronghold/GuLiStrongholdCaptureComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiStrongholdCaptureComponent::UGuLiStrongholdCaptureComponent() { SetIsReplicatedByDefault(false); }
void UGuLiStrongholdCaptureComponent::InitializeCapture(int32 InTerritoryIndex, EGuLiTeam Owner)
{
	TerritoryIndex = InTerritoryIndex;
	SetOwnerEndpoint(Owner);
}
void UGuLiStrongholdCaptureComponent::SetOwnerEndpoint(EGuLiTeam Owner)
{
	check(GetOwner()->HasAuthority());
	Progress = Owner == EGuLiTeam::Red ? -1.f : Owner == EGuLiTeam::Blue ? 1.f : 0.f;

}
