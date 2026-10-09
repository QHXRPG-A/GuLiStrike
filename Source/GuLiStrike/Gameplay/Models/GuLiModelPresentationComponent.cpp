#include "Gameplay/Models/GuLiModelPresentationComponent.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/Models/GuLiLocalTeamColorSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"

void UGuLiModelPresentationComponent::InstallForPresentationActor(AActor* Actor)
{
	if (!Actor || !Actor->GetWorld()) return;
	auto* Registry=Actor->GetWorld()->GetSubsystem<UGuLiModelRegistrySubsystem>();
	if (!Registry) return;
	const int32 Id=Registry->FindModelIdForResource(Actor->GetClass());
	if (!Id) return;
	auto* Component=Actor->FindComponentByClass<UGuLiModelPresentationComponent>();
	if (!Component)
	{
		Component=NewObject<UGuLiModelPresentationComponent>(Actor,NAME_None,RF_Transient);
		Actor->AddInstanceComponent(Component); Component->ModelId=Id; Component->RegisterComponent();
	}
	Component->SetModelId(Id);
}

UGuLiModelPresentationComponent::UGuLiModelPresentationComponent() { PrimaryComponentTick.bCanEverTick=false; }
void UGuLiModelPresentationComponent::BeginPlay() { Super::BeginPlay(); RefreshModel(); }
bool UGuLiModelPresentationComponent::SetModelId(int32 Id) { if (Id<=0) return false; ModelId=Id; return RefreshModel(); }
bool UGuLiModelPresentationComponent::RefreshModel()
{
	if (!GetWorld() || !GetOwner() || ModelId<=0) return false;
	auto* Registry=GetWorld()->GetSubsystem<UGuLiModelRegistrySubsystem>();
	if (!Registry) return false;
	const bool Applied=Registry->ApplyModelParts(GetOwner(),ModelId);
	for (const auto& P : Registry->GetModelParts(ModelId))
		if (auto* C=Registry->FindPart(GetOwner(),P.ComponentPath)) UGuLiLocalTeamColorSubsystem::Register(this,C,ModelId,P.PartKey);
	return Applied;
}
void UGuLiModelPresentationComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (GetWorld() && GetWorld()->GetGameInstance())
		for (auto* Player : GetWorld()->GetGameInstance()->GetLocalPlayers())
			if (auto* S=Player->GetSubsystem<UGuLiLocalTeamColorSubsystem>())
			{
				TArray<UMeshComponent*> Meshes; GetOwner()->GetComponents(Meshes,true);
				for (auto* M : Meshes) S->UnregisterModelComponent(M);
			}
	Super::EndPlay(Reason);
}
