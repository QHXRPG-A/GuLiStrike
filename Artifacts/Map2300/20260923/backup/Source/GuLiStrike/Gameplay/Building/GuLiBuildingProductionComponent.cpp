#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiBuildingProductionComponent::UGuLiBuildingProductionComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .25f;
}
void UGuLiBuildingProductionComponent::InitializeProduction()
{
	const auto& Life = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	ObservedTeam = Life.GetTeam();
	SetComponentTickEnabled(GetOwner()->HasAuthority() && Life.GetDefinition().Category == EGuLiBuildingCategory::Barracks);
}
void UGuLiBuildingProductionComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt,TickType,TickFunction);
	const auto& Life = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	const auto& Definition = Life.GetDefinition();
	auto& Resources = *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (ObservedTeam != Life.GetTeam()) { ObservedTeam = Life.GetTeam(); CycleSeconds = 0; }
	const bool bWasProducing = bProducing;
	bProducing = Life.IsCompleted() && GuLiResources::IsPlayableTeam(ObservedTeam)
		&& (Life.GetState().Origin != EGuLiBuildingOrigin::Gift || Resources.IsTerritorySupplied(Life.GetState().TerritoryIndex));
	if (!bProducing)
	{
		if (bWasProducing) { GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate(); }
		return;
	}
	CycleSeconds += Dt;
	if (CycleSeconds >= Definition.ProductionSeconds)
	{
		CycleSeconds = FMath::Fmod(CycleSeconds, Definition.ProductionSeconds);
		const auto* SoldierDefinition = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>()
			->FindSoldierDefinition(Definition.ProductionUnitId);
		const float SpawnSpacing = FMath::Max(320.0f,
			SoldierDefinition ? SoldierDefinition->GetMassAvoidanceRadius(150.0f) * 2.0f + 20.0f : 320.0f);
		TArray<FVector> Locations;
		for (int32 Index = 0; Index < Definition.ProductionCount; ++Index)
		{
			const FVector Local(Definition.CollisionExtent.X + 800 + (Index / 6) * SpawnSpacing, (Index % 6 - 2.5) * SpawnSpacing, 0);
			Locations.Add(Life.GetGroundLocation() + GetOwner()->GetActorRotation().RotateVector(Local));
		}
		TArray<FGuLiSoldierId> Spawned;
		GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->SpawnSoldierBatch(ObservedTeam, Definition.ProductionUnitId, Locations, Spawned);
		if (!Spawned.IsEmpty()) GetWorld()->GetSubsystem<UGuLiArmyAdvanceSubsystem>()->RegisterBatch(ObservedTeam, Spawned, Life.GetState().TerritoryIndex);
	}
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}
void UGuLiBuildingProductionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiBuildingProductionComponent, CycleSeconds);
	DOREPLIFETIME(UGuLiBuildingProductionComponent, bProducing);
}
