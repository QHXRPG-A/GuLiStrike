#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiBuildingLifecycleComponent::UGuLiBuildingLifecycleComponent()
{
	SetIsReplicatedByDefault(true);
}
void UGuLiBuildingLifecycleComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
		if (auto* Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>())
			Health->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
	if (State.InstanceId) RegisterInstance();
}
void UGuLiBuildingLifecycleComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Registry = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()) Registry->Unregister(*this);
	Super::EndPlay(Reason);
}
void UGuLiBuildingLifecycleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiBuildingLifecycleComponent, State);
}
const FGuLiBuildingDefinition& UGuLiBuildingLifecycleComponent::GetDefinition() const
{
	const auto* Definition = UGuLiBuildingCatalog::LoadDefaultCatalog()->FindById(State.DefinitionId);
	check(Definition);
	return *Definition;
}
EGuLiTeam UGuLiBuildingLifecycleComponent::GetTeam() const
{
	return CastChecked<IGuLiBuildingOwner>(GetOwner())->GetBuildingTeam();
}
FVector UGuLiBuildingLifecycleComponent::GetGroundLocation() const
{
	return CastChecked<IGuLiBuildingOwner>(GetOwner())->GetBuildingGroundLocation();
}
void UGuLiBuildingLifecycleComponent::InitializeBuilding(int32 DefinitionId, int32 TerritoryIndex,
	EGuLiBuildingOrigin Origin, bool bCompleted, const FGuid& Builder)
{
	check(GetOwner()->HasAuthority() && State.InstanceId == 0);
	State.InstanceId = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->AllocateInstanceId();
	State.DefinitionId = DefinitionId; State.TerritoryIndex = TerritoryIndex; State.Origin = Origin;
	State.BuilderGuid = Builder;
	State.Phase = bCompleted ? EGuLiBuildingPhase::Completed : EGuLiBuildingPhase::UnderConstruction;
	State.WorkDone = bCompleted ? GetDefinition().ConstructionWork : 0;
	if (auto* Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>())
	{
		FGuLiTargetHandle Target;
		Target.Kind = EGuLiTargetKind::GroundActor; Target.AuthorityId = FGuid::NewGuid();
		Target.Generation = 1; Target.LocalId = State.InstanceId;
		Health->ConfigureServerTarget(Target, GetTeam());
		Health->InitializeServerHealth(GetDefinition().MaxHealth);
		Health->InitializeServerShield(GetDefinition().MaxShield);
	}
	if (GetDefinition().Category == EGuLiBuildingCategory::Stronghold) GetOwner()->SetCanBeDamaged(false);
	if (HasBegunPlay()) RegisterInstance();
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}
void UGuLiBuildingLifecycleComponent::RegisterInstance()
{
	GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Register(*this);
}
void UGuLiBuildingLifecycleComponent::OnRep_State()
{
	if (!State.InstanceId) return; // Initial component replication precedes its authored identity.
	if (State.Phase == EGuLiBuildingPhase::Destroyed)
		GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Unregister(*this);
	else RegisterInstance();
}
void UGuLiBuildingLifecycleComponent::AddConstructionWork(float Work)
{
	check(GetOwner()->HasAuthority() && Work >= 0);
	if (State.Phase != EGuLiBuildingPhase::UnderConstruction) return;
	State.WorkDone = FMath::Min(State.WorkDone + Work, GetDefinition().ConstructionWork);
	if (State.WorkDone >= GetDefinition().ConstructionWork)
	{
		State.Phase = EGuLiBuildingPhase::Completed;
		GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->OnCompleted.Broadcast(*this);
	}
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}
void UGuLiBuildingLifecycleComponent::RefreshTeam()
{
	check(GetOwner()->HasAuthority());
	if (auto* Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>())
		Health->ConfigureServerTarget(Health->GetTargetHandle(), GetTeam());
	GetOwner()->ForceNetUpdate();
}
void UGuLiBuildingLifecycleComponent::HandleDeath()
{
	if (State.Phase == EGuLiBuildingPhase::Destroyed) return;
	State.Phase = EGuLiBuildingPhase::Destroyed;
	auto& Registry = *GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
	Registry.OnDestroyed.Broadcast(*this); Registry.Unregister(*this);
	GetOwner()->SetActorEnableCollision(false);
	GetOwner()->SetActorHiddenInGame(true);
	GetOwner()->SetLifeSpan(2);
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}
