#include "Gameplay/CombatEffects/GuLiUnitFeedbackComponent.h"
#include "Network/GuLiNetworkSettings.h"
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
	SetIsReplicatedByDefault(false);
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
		ObservedTarget = HealthComponent->GetTargetHandle();
		Vitals.Observe(State, HealthComponent->GetCombatTeam(), ObservedTarget.IsValid() && HealthComponent->IsAlive(),
			GetWorld()->GetTimeSeconds());
		// A late-joining client must not replay an already dead Actor's explosion.
		bDestructionPlayed = State.IsWellFormed() && State.bDead;
		if (bDestructionPlayed) HideLivingMeshes();
		HealthComponent->OnHealthChanged.AddDynamic(this, &ThisClass::HandleHealth);
		HealthComponent->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
	}
}

void UGuLiUnitFeedbackComponent::HandleHealth(float Health, float Maximum)
{
	if (!HealthComponent.IsValid()) return;
	const auto& State = HealthComponent->GetHealthState();
	const auto& Target = HealthComponent->GetTargetHandle();
	const bool bNewTarget = ObservedTarget != Target;
	if (bNewTarget) Vitals = {};
	ObservedTarget = Target;
	const bool bHadBaseline = Vitals.Health.IsWellFormed();
	const float PreviousHealth = Vitals.Health.Health;
	const bool bChanged = Vitals.Observe(State, HealthComponent->GetCombatTeam(),
		Target.IsValid() && HealthComponent->IsAlive(), GetWorld()->GetTimeSeconds());
	if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
	{
		if (bChanged)
		{
			Feedback->RecordActorVitalChange(GetOwner(), Vitals);
			if (Health < PreviousHealth) Feedback->FlashActor(GetOwner());
		}
		else if (Vitals.LastChange < 0) Feedback->RemoveActorHealthBar(GetOwner());
	}
	if (Health > 0)
	{
		bDestructionPlayed = false;
		for (const auto& Mesh : HiddenLivingMeshes) if (Mesh.IsValid()) Mesh->SetVisibility(true);
		HiddenLivingMeshes.Reset();
		if (Wreck.IsValid()) Wreck->Destroy();
		Wreck.Reset();
	}
	else if (!bHadBaseline && !bDestructionPlayed) { bDestructionPlayed = true; HideLivingMeshes(); }
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
	if (!Owner || !bPlayDestructionEffect || !UGuLiNetworkSettings::CanPresent(GetWorld())) return;
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

void UGuLiUnitFeedbackComponent::MulticastHit()
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
		if (UWorld* World = GetWorld())
			if (auto* Feedback = World->GetSubsystem<UGuLiUnitFeedbackSubsystem>()) Feedback->ClearActorFlash(Owner);
	}
	if (HealthComponent.IsValid())
	{
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealth);
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::HandleDeath);
	}
	Super::EndPlay(Reason);
}
