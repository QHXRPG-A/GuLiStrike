// Copyright Epic Games, Inc. All Rights Reserved.
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

UGuLiTeamOutlineComponent::UGuLiTeamOutlineComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.2f;
}

void UGuLiTeamOutlineComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) return;
	HealthSource = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>();
	SetComponentTickEnabled(HealthSource != nullptr);
	if (HealthSource) OutlineTeam = HealthSource->GetCombatTeam();
	RefreshMeshes();
}

void UGuLiTeamOutlineComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	OutlineTeam = HealthSource->GetCombatTeam();
	// Ship parts can be installed after BeginPlay; stencil setters only dirty on change.
	RefreshMeshes();
}

void UGuLiTeamOutlineComponent::SetOutlineTeam(EGuLiTeam NewTeam)
{
	if (OutlineTeam == NewTeam) return;
	OutlineTeam = NewTeam;
	RefreshMeshes();
}

void UGuLiTeamOutlineComponent::RefreshMeshes()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	TInlineComponentArray<UPrimitiveComponent*> Meshes(GetOwner());
	for (UPrimitiveComponent* Mesh : Meshes)
	{
		if (!Mesh->IsA<UStaticMeshComponent>() && !Mesh->IsA<USkinnedMeshComponent>()) continue;
		Mesh->SetCustomDepthStencilValue(static_cast<uint8>(OutlineTeam));
		Mesh->SetRenderCustomDepth(OutlineTeam != EGuLiTeam::Unassigned);
	}
}
