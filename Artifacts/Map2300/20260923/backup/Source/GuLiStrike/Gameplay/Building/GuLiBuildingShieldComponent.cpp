#include "Gameplay/Building/GuLiBuildingShieldComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiBuildingShieldComponent::UGuLiBuildingShieldComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .25f;
}
void UGuLiBuildingShieldComponent::InitializeShield()
{
	check(GetOwner()->HasAuthority());
	const auto& Life = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	bGenerator = Life.GetDefinition().Category == EGuLiBuildingCategory::ShieldGenerator;
	SetComponentTickEnabled(bGenerator);
	if (HasBegunPlay() && bGenerator) RegisterBarrier();
}
void UGuLiBuildingShieldComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority() && bGenerator) RegisterBarrier();
}
bool UGuLiBuildingShieldComponent::HasSupply() const
{
	const auto& Life = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	return Life.IsCompleted() && GuLiResources::IsPlayableTeam(Life.GetTeam())
		&& (Life.GetState().Origin != EGuLiBuildingOrigin::Gift
			|| GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->IsTerritorySupplied(Life.GetState().TerritoryIndex));
}
void UGuLiBuildingShieldComponent::RegisterBarrier()
{
	const auto& Life = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->RegisterDamageBarrier(*this, Life.GetState().InstanceId,
		[this](const FGuLiCombatTargetSnapshot& Target, float Damage)
	{
		const auto& Building = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
		if (!HasSupply() || Target.Team != Building.GetTeam()
			|| FVector::DistSquared2D(Target.Location, Building.GetGroundLocation()) > FMath::Square(Building.GetDefinition().ShieldRadius)) return 0.f;
		return GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()->ConsumeServerShield(Damage);
	});
}
void UGuLiBuildingShieldComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt,TickType,TickFunction);
	bEnabled = HasSupply();
	if (bEnabled)
		GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()->RechargeServerShield(
			GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->GetDefinition().ShieldRechargePerSecond * Dt);
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}
void UGuLiBuildingShieldComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetOwner()->HasAuthority() && bGenerator) GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->UnregisterDamageBarrier(*this);
	Super::EndPlay(Reason);
}
void UGuLiBuildingShieldComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiBuildingShieldComponent,bEnabled);
}
