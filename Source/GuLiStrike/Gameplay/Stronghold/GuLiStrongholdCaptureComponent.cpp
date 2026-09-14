#include "Gameplay/Stronghold/GuLiStrongholdCaptureComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiStrongholdCaptureComponent::UGuLiStrongholdCaptureComponent() { SetIsReplicatedByDefault(true); }
void UGuLiStrongholdCaptureComponent::InitializeCapture(int32 InTerritoryIndex, EGuLiTeam Owner)
{
	TerritoryIndex = InTerritoryIndex;
	SetOwnerEndpoint(Owner);
}
void UGuLiStrongholdCaptureComponent::SetOwnerEndpoint(EGuLiTeam Owner)
{
	check(GetOwner()->HasAuthority());
	Progress = Owner == EGuLiTeam::Red ? -1.f : Owner == EGuLiTeam::Blue ? 1.f : 0.f;
	GetOwner()->ForceNetUpdate();
}
void UGuLiStrongholdCaptureComponent::AdvanceCapture(int32 Red, int32 Blue, float Seconds, float NeutralCaptureSeconds)
{
	check(GetOwner()->HasAuthority() && NeutralCaptureSeconds > 0);
	RedCount = Red; BlueCount = Blue;
	if (Red != Blue)
	{
		Progress = FMath::Clamp(Progress + (Blue > Red ? 1.f : -1.f) * Seconds / NeutralCaptureSeconds, -1.f, 1.f);
		auto& Resources = *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
		const EGuLiTeam Owner = Resources.GetResourceWorldState()->GetTerritoryOwner(TerritoryIndex);
		const EGuLiTeam Captor = Progress == 1.f ? EGuLiTeam::Blue : Progress == -1.f ? EGuLiTeam::Red : EGuLiTeam::Unassigned;
		if (GuLiResources::IsPlayableTeam(Captor) && Captor != Owner && Resources.SetTerritoryOwner(TerritoryIndex,Captor))
			OnCaptureRewardRequested.Broadcast(TerritoryIndex,Captor);
	}
	GetOwner()->ForceNetUpdate();
}
void UGuLiStrongholdCaptureComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiStrongholdCaptureComponent, Progress);
	DOREPLIFETIME(UGuLiStrongholdCaptureComponent, RedCount);
	DOREPLIFETIME(UGuLiStrongholdCaptureComponent, BlueCount);
}
