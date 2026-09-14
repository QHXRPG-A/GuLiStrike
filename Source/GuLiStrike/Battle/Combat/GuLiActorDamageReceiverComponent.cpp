#include "Battle/Combat/GuLiActorDamageReceiverComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Engine/World.h"

UGuLiActorDamageReceiverComponent::UGuLiActorDamageReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGuLiActorDamageReceiverComponent::BeginPlay()
{
	Super::BeginPlay();
	Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>();
	checkf(Health, TEXT("DamageReceiver requires a CombatHealth component on its owner."));
	if (GetOwner()->HasAuthority())
	{
		GetOwner()->OnTakeAnyDamage.AddDynamic(this, &ThisClass::ReceiveDamage);
		Health->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
	}
}

void UGuLiActorDamageReceiverComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetOwner()->OnTakeAnyDamage.RemoveDynamic(this, &ThisClass::ReceiveDamage);
	Health->OnDeath.RemoveDynamic(this, &ThisClass::HandleDeath);
	Super::EndPlay(Reason);
}

void UGuLiActorDamageReceiverComponent::ReceiveDamage(
	AActor* Actor, const float Damage, const UDamageType* Type, AController* Instigator, AActor* Causer)
{
	AActor* SourceActor = Instigator ? Instigator->GetPawn() : Causer;
	const UGuLiCombatHealthComponent* Source = SourceActor ? SourceActor->FindComponentByClass<UGuLiCombatHealthComponent>() : nullptr;
	// Standard damage needs a registered combat source, just like the ledger's other entry points.
	if (!Source) return;
	UGuLiDamageLedgerSubsystem* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	FGuLiDamageRequest Request;
	Request.MatchEpoch = Ledger->GetMatchEpoch();
	Request.DamageEventId = FGuid::NewGuid();
	Request.ShotId = Request.DamageEventId;
	Request.Source = Source->GetTargetHandle();
	Request.Target = Health->GetTargetHandle();
	Request.Damage = Damage;
	Request.HitLocation = Actor->GetActorLocation();
	Ledger->CommitDamage(Request);
}

void UGuLiActorDamageReceiverComponent::HandleDeath()
{
	if (!bDestroyOwnerOnDeath) return;
	AActor* Actor = GetOwner();
	Actor->SetActorTickEnabled(false);
	Actor->SetActorEnableCollision(false);
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
			Movement->Deactivate();
		}
	}
	// Let the lethal health update reach observers before replicated destruction removes the model.
	Actor->SetLifeSpan(0.1f);
}
