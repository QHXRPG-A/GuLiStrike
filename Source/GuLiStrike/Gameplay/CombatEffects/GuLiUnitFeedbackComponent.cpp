#include "Gameplay/CombatEffects/GuLiUnitFeedbackComponent.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/CombatEffects/GuLiUnitWreck.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

UGuLiUnitFeedbackComponent::UGuLiUnitFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UGuLiUnitFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!Owner) return;
	Owner->OnDestroyed.AddDynamic(this, &ThisClass::HandleDestroyed);
	Owner->OnTakeAnyDamage.AddDynamic(this, &ThisClass::HandleAnyDamage);
	HealthComponent = Owner->FindComponentByClass<UGuLiCombatHealthComponent>();
	if (HealthComponent.IsValid())
	{
		const auto& State = HealthComponent->GetHealthState();
		bHasHealthBaseline = State.IsWellFormed();
		PreviousHealth = State.Health;
		// A late-joining client must not replay an already dead Actor's explosion.
		bDestructionPlayed = bHasHealthBaseline && State.bDead;
		if (bDestructionPlayed) HideLivingMeshes();
		HealthComponent->OnHealthChanged.AddDynamic(this, &ThisClass::HandleHealth);
		HealthComponent->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
	}
}

void UGuLiUnitFeedbackComponent::HandleHealth(float Health, float Maximum)
{
	if (bHasHealthBaseline && Health < PreviousHealth)
		if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>()) Feedback->FlashActor(GetOwner());
	if (Health > 0)
	{
		bDestructionPlayed = false;
		for (const auto& Mesh : HiddenLivingMeshes) if (Mesh.IsValid()) Mesh->SetVisibility(true);
		HiddenLivingMeshes.Reset();
		if (Wreck.IsValid()) Wreck->Destroy();
		Wreck.Reset();
	}
	else if (!bHasHealthBaseline) { bDestructionPlayed = true; HideLivingMeshes(); }
	PreviousHealth = Health; bHasHealthBaseline = true;
}

void UGuLiUnitFeedbackComponent::HandleDeath()
{
	AActor* Owner = GetOwner();
	if (!Owner || bDestructionPlayed || !bPlayDestructionEffect || !GetWorld() || GetWorld()->bIsTearingDown) return;
	// Directly possessed player avatars never use the NPC destruction presentation.
	if (const APawn* Pawn = Cast<APawn>(Owner); Pawn && Pawn->IsPlayerControlled()) return;
	bDestructionPlayed = true;
	if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
	{
		FBox Bounds(ForceInit); float Size = 0.0f;
		UGuLiUnitFeedbackSubsystem::GetActorVisualBounds(Owner, Bounds, Size);
		Feedback->PlayDestruction(Bounds.IsValid ? Bounds.GetCenter() : Owner->GetActorLocation(), Size);
		Feedback->ClearActorFlash(Owner);
		Wreck = Feedback->SpawnActorWreck(Owner, FVector::ZeroVector, false);
		HideLivingMeshes();
	}
}

void UGuLiUnitFeedbackComponent::HideLivingMeshes()
{
	AActor* Owner = GetOwner();
	if (!Owner || !bPlayDestructionEffect || GetNetMode() == NM_DedicatedServer) return;
	if (const APawn* Pawn = Cast<APawn>(Owner); Pawn && Pawn->IsPlayerControlled()) return;
	TInlineComponentArray<UMeshComponent*> Meshes;
	Owner->GetComponents(Meshes, true);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->IsVisible()
			|| (!Cast<UStaticMeshComponent>(Mesh) && !Cast<USkeletalMeshComponent>(Mesh))) continue;
		HiddenLivingMeshes.AddUnique(Mesh);
		Mesh->SetVisibility(false);
	}
}

void UGuLiUnitFeedbackComponent::HandleDestroyed(AActor* Actor) { HandleDeath(); }

void UGuLiUnitFeedbackComponent::HandleAnyDamage(AActor* Actor, float Damage, const UDamageType* Type, AController* Instigator, AActor* Causer)
{
	if (Damage > 0 && Actor && Actor->HasAuthority() && !HealthComponent.IsValid()) MulticastHit();
}

void UGuLiUnitFeedbackComponent::MulticastHit_Implementation()
{
	if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>()) Feedback->FlashActor(GetOwner());
}

void UGuLiUnitFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// Actor::Destroyed routes component EndPlay before broadcasting OnDestroyed.
	if (Reason == EEndPlayReason::Destroyed) HandleDeath();
	if (AActor* Owner = GetOwner())
	{
		Owner->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleDestroyed);
		Owner->OnTakeAnyDamage.RemoveDynamic(this, &ThisClass::HandleAnyDamage);
		if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>()) Feedback->ClearActorFlash(Owner);
	}
	if (HealthComponent.IsValid())
	{
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealth);
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::HandleDeath);
	}
	Super::EndPlay(Reason);
}
