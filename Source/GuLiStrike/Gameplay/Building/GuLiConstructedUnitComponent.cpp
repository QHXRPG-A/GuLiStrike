#include "Gameplay/Building/GuLiConstructedUnitComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavModifierComponent.h"
#include "Net/UnrealNetwork.h"

UGuLiConstructedUnitComponent::UGuLiConstructedUnitComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .1f;
}
void UGuLiConstructedUnitComponent::BeginPlay()
{
	Super::BeginPlay();
	Lifecycle = GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	if (auto* Life = Lifecycle.Get())
	{
		ConstructionChanged = Life->OnConstructionStateChanged.AddUObject(this, &ThisClass::OnConstructionChanged);
		OnConstructionChanged();
	}
}
void UGuLiConstructedUnitComponent::SetPopulationReservation(const FGuid& Token)
{
	check(GetOwner()->HasAuthority() && Token.IsValid() && !PopulationReservation.IsValid());
	PopulationReservation = Token;
	OnConstructionChanged();
}
void UGuLiConstructedUnitComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Life = Lifecycle.Get()) Life->OnConstructionStateChanged.Remove(ConstructionChanged);
	if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		Nav->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this, &ThisClass::HandleNavigationRebuilt);
	if (GetOwner()->HasAuthority() && PopulationReservation.IsValid())
		if (auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
			Authority->ReleaseConstructionUnit(PopulationReservation);
	PopulationReservation.Invalidate();
	Super::EndPlay(Reason);
}
void UGuLiConstructedUnitComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiConstructedUnitComponent, ConvertedSoldierId);
}
void UGuLiConstructedUnitComponent::OnConstructionChanged()
{
	auto* Life = Lifecycle.Get();
	if (!Life || Life->GetState().DefinitionId == 0) return;
	if (Life->GetState().Phase == EGuLiBuildingPhase::ConvertedToUnit)
	{
		GetOwner()->SetActorHiddenInGame(true);
		GetOwner()->SetActorEnableCollision(false);
		return;
	}
	if (Life->GetState().Phase == EGuLiBuildingPhase::Destroyed)
	{
		if (GetOwner()->HasAuthority() && PopulationReservation.IsValid())
			GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->ReleaseConstructionUnit(PopulationReservation);
		PopulationReservation.Invalidate(); SetComponentTickEnabled(false); return;
	}
	if (GetOwner()->HasAuthority() && Life->IsCompleted() && Life->GetDefinition().CompletionUnitTypeId > 0)
		BeginConversion();
}
void UGuLiConstructedUnitComponent::BeginConversion()
{
	if (bConversionStarted || ConvertedSoldierId.IsValid() || !PopulationReservation.IsValid()) return;
	auto* Building = Cast<AGuLiPlacedBuilding>(GetOwner());
	if (!Building) return;
	bConversionStarted = true;
	EarliestConversionTime = GetWorld()->GetTimeSeconds()+.2;
	// Preserve the physical site while its forbidden NavMesh footprint is rebuilt.
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Nav) Nav->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this, &ThisClass::HandleNavigationRebuilt);
	Building->GetBuildingCollision()->SetCanEverAffectNavigation(false);
	Building->GetNavigationModifier()->SetNavigationRelevancy(false);
	if (Nav)
	{
		Nav->UpdateActorInNavOctree(*Building);
		Nav->AddDirtyArea(Building->GetBuildingCollision()->Bounds.GetBox(), ENavigationDirtyFlag::All, TEXT("ConstructedUnitHandover"));
	}
	SetComponentTickEnabled(true);
}
void UGuLiConstructedUnitComponent::HandleNavigationRebuilt(ANavigationData* Data)
{
	if (Data && Data->GetConfig().Name == GuLiCommanderNavigationPolicy::GetRequiredAgentName())
		bNavigationRebuilt = true;
}
void UGuLiConstructedUnitComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, Type, Function);
	if (ConvertedSoldierId.IsValid()) { RefreshHandoverVisual(); return; }
	if (!GetOwner()->HasAuthority() || !bConversionStarted) return;
	if (!bNavigationRebuilt || GetWorld()->GetTimeSeconds() < EarliestConversionTime) return;
	if (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return;
	auto* Life = Lifecycle.Get();
	auto* Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>();
	if (!Life || !Life->IsCompleted() || !Health || !Health->IsAlive()) return;
	FGuLiSoldierId NewId;
	const FTransform Pose(FRotator(0, GetOwner()->GetActorRotation().Yaw, 0), Life->GetGroundLocation());
	if (!GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->CompleteConstructionUnit(
		PopulationReservation, Pose, Health->GetHealthState().Health, *GetOwner(), NewId)) return;
	ConvertedSoldierId = NewId; PopulationReservation.Invalidate();
	if (auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>())
		Ledger->UnregisterTarget(Health->GetTargetHandle(), Health);
	GetOwner()->SetCanBeDamaged(false); GetOwner()->SetActorEnableCollision(false);
	Life->MarkConvertedToUnit();
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
	// Retain the replicated identity briefly for handover; do not leave invisible buildings alive forever.
	GetOwner()->SetLifeSpan(10.0f);
	RefreshHandoverVisual();
}
void UGuLiConstructedUnitComponent::OnRep_ConvertedSoldier()
{
	if (!ConvertedSoldierId.IsValid()) return;
	GetOwner()->SetActorEnableCollision(false);
	if (bVisualRetired) return;
	SetComponentTickEnabled(true); RefreshHandoverVisual();
}
void UGuLiConstructedUnitComponent::RefreshHandoverVisual()
{
	if (bVisualRetired) return;
	bool bHasMassVisual = GetWorld()->GetNetMode() == NM_DedicatedServer;
	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); !bHasMassVisual && It; ++It)
	{
		FTransform Pose;
		bHasMassVisual = It->TryGetPresentedSoldierTransform(ConvertedSoldierId, Pose);
	}
	if (!bHasMassVisual) return;
	GetOwner()->SetActorHiddenInGame(true); bVisualRetired = true;
	SetComponentTickEnabled(false);
}
void UGuLiConstructedUnitComponent::RetireVisualForMass(FGuLiSoldierId Id)
{
	if (!Id.IsValid() || (ConvertedSoldierId.IsValid() && ConvertedSoldierId != Id)) return;
	GetOwner()->SetActorHiddenInGame(true);
	bVisualRetired=true;
	SetComponentTickEnabled(false);
}
