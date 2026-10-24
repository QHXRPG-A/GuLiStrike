#include "Gameplay/Building/GuLiConstructedUnitComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
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
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"

UGuLiConstructedUnitComponent::UGuLiConstructedUnitComponent()
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .1f;
}
void UGuLiConstructedUnitComponent::BeginPlay()
{
    Super::BeginPlay();
    Lifecycle=GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
}

void UGuLiConstructedUnitComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        Nav->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this,&ThisClass::HandleNavigationRebuilt);
    Super::EndPlay(Reason);
}


void UGuLiConstructedUnitComponent::BeginConversion()
{
	if (bConversionStarted || ConvertedSoldierId.IsValid()) return;
	auto* Building = Cast<AGuLiPlacedBuilding>(GetOwner());
	if (!Building) return;
	bConversionStarted = true;
	if (!EarliestConversionTick)
    {
        const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
        if (!Binding) {bConversionStarted=false;return;}
        EarliestConversionTick=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjectLogicalTick(Binding->GetObjectKey())+12;
    }
    if (auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady())
    {
        // Keep the site reserved until the atomic building-to-unit ledger replacement.
        bNavigationRebuilt=true;SetComponentTickEnabled(false);return;
    }
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
	SetComponentTickEnabled(false);
}
void UGuLiConstructedUnitComponent::HandleNavigationRebuilt(ANavigationData* Data)
{
	if (Data && Data->GetConfig().Name == GuLiCommanderNavigationPolicy::GetRequiredAgentName())
		bNavigationRebuilt = true;
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
