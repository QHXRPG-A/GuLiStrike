// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiBuildingShieldComponent.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "Net/UnrealNetwork.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiActorDamageReceiverComponent.h"

AGuLiPlacedBuilding::AGuLiPlacedBuilding()
{
	PrimaryActorTick.bCanEverTick = false;
	CreateDefaultSubobject<UGuLiCombatHealthComponent>(TEXT("CombatHealth"));
	Lifecycle = CreateDefaultSubobject<UGuLiBuildingLifecycleComponent>(TEXT("Lifecycle"));
	CreateDefaultSubobject<UGuLiBuildingProductionComponent>(TEXT("Production"));
	CreateDefaultSubobject<UGuLiBuildingShieldComponent>(TEXT("ShieldSupply"));
	CreateDefaultSubobject<UGuLiActorDamageReceiverComponent>(TEXT("DamageReceiver"))->bDestroyOwnerOnDeath = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = false;
	SetReplicateMovement(false);
	NetDormancy = DORM_Initial;

	CollisionRoot = CreateDefaultSubobject<UBoxComponent>(TEXT("BuildingCollision"));
	SetRootComponent(CollisionRoot);
	CollisionRoot->SetMobility(EComponentMobility::Static);
	CollisionRoot->SetBoxExtent(FVector(100.0f));
	CollisionRoot->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	CollisionRoot->SetGenerateOverlapEvents(false);
	// Mass Soldiers do not own physics bodies. Runtime building avoidance is deferred,
	// so this physical blocker must not invalidate the global Commander NavMesh and
	// briefly stop every active formation when a building is placed.
	CollisionRoot->SetCanEverAffectNavigation(false);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuildingVisual"));
	VisualMesh->SetupAttachment(CollisionRoot);
	// Runtime and replicated initialization assign the catalog mesh once. Start
	// movable so SetStaticMesh is legal on registered client components, then
	// ApplyDefinition locks the finished visual back to Static.
	VisualMesh->SetMobility(EComponentMobility::Movable);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCanEverAffectNavigation(false);

	NavigationModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavigationModifier"));
	NavigationModifier->SetAreaClass(UNavArea_Null::StaticClass());
	NavigationModifier->SetNavigationRelevancy(false);
}

void AGuLiPlacedBuilding::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiPlacedBuilding, BuildingType);
	DOREPLIFETIME(AGuLiPlacedBuilding, Team);
	DOREPLIFETIME(AGuLiPlacedBuilding, BuilderPlayerGuid);
	DOREPLIFETIME(AGuLiPlacedBuilding, DefinitionId);
}

bool AGuLiPlacedBuilding::InitializeBuilding(
	const EGuLiBuildingType InBuildingType,
	const EGuLiTeam InTeam,
	const FGuid& InBuilderPlayerGuid,
	const FGuLiBuildingDefinition& Definition)
{
	if (!HasAuthority() || InBuildingType != Definition.Type || !Definition.IsUsable()
		|| InTeam == EGuLiTeam::Unassigned || !InBuilderPlayerGuid.IsValid())
	{
		return false;
	}

	BuildingType = InBuildingType;
	Team = InTeam;
	BuilderPlayerGuid = InBuilderPlayerGuid;
	DefinitionId = UGuLiBuildingCatalog::LoadDefaultCatalog()->FindDefinition(InBuildingType)->DefinitionId;
	SetActorScale3D(FVector::OneVector);
	ApplyDefinition(Definition);
	Lifecycle->InitializeBuilding(DefinitionId, GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->FindTerritoryIndex(GetBuildingGroundLocation()),
		EGuLiBuildingOrigin::Manual, false, InBuilderPlayerGuid);
	FindComponentByClass<UGuLiBuildingProductionComponent>()->InitializeProduction();
	FindComponentByClass<UGuLiBuildingShieldComponent>()->InitializeShield();
	return true;
}

bool AGuLiPlacedBuilding::InitializeFromDefinition(int32 InDefinitionId, EGuLiTeam InTeam,
	const FGuid& BuilderGuid, int32 TerritoryIndex, EGuLiBuildingOrigin Origin, bool bCompleted)
{
	check(HasAuthority());
	const auto& D = *UGuLiBuildingCatalog::LoadDefaultCatalog()->FindById(InDefinitionId);
	DefinitionId = InDefinitionId; BuildingType = D.Type; Team = InTeam; BuilderPlayerGuid = BuilderGuid;
	ApplyDefinition(D);
	Lifecycle->InitializeBuilding(DefinitionId, TerritoryIndex, Origin, bCompleted, BuilderGuid);
	FindComponentByClass<UGuLiBuildingProductionComponent>()->InitializeProduction();
	FindComponentByClass<UGuLiBuildingShieldComponent>()->InitializeShield();
	return true;
}
void AGuLiPlacedBuilding::SetBuildingTeamAuthority(EGuLiTeam NewTeam)
{
	check(HasAuthority()); Team = NewTeam; Lifecycle->RefreshTeam(); FlushNetDormancy(); ForceNetUpdate();
}
FVector AGuLiPlacedBuilding::GetBuildingGroundLocation() const
{
	return GetActorLocation() - FVector(0, 0, CollisionRoot->GetUnscaledBoxExtent().Z);
}

void AGuLiPlacedBuilding::ApplyDefinition(const FGuLiBuildingDefinition& Definition)
{
	if (!CollisionRoot || !VisualMesh || !Definition.IsUsable())
	{
		return;
	}

	CollisionRoot->SetBoxExtent(Definition.CollisionExtent, false);
	const bool bShouldDisallowNanite = Definition.Type == EGuLiBuildingType::MissileTurret;
	if (VisualMesh->bDisallowNanite != bShouldDisallowNanite)
	{
		// The production missile asset keeps Nanite data, but one of its 32 slots is
		// translucent glass. Rendering that slot through Nanite emits a warning every
		// frame, so this component deliberately uses the mesh fallback representation.
		VisualMesh->bDisallowNanite = bShouldDisallowNanite;
		VisualMesh->MarkRenderStateDirty();
	}
	const FVector DesiredLocation = Definition.VisualOffset
		- FVector(0.0f, 0.0f, Definition.CollisionExtent.Z);
	const bool bNeedsVisualUpdate = VisualMesh->GetStaticMesh() != Definition.Mesh
		|| !VisualMesh->GetRelativeLocation().Equals(DesiredLocation, UE_KINDA_SMALL_NUMBER)
		|| !VisualMesh->GetRelativeRotation().IsNearlyZero()
		|| !VisualMesh->GetRelativeScale3D().Equals(Definition.MeshScale, UE_KINDA_SMALL_NUMBER);
	if (bNeedsVisualUpdate)
	{
		if (VisualMesh->Mobility != EComponentMobility::Movable)
		{
			VisualMesh->SetMobility(EComponentMobility::Movable);
		}
		VisualMesh->SetStaticMesh(Definition.Mesh);
		VisualMesh->SetRelativeLocation(DesiredLocation);
		VisualMesh->SetRelativeRotation(FRotator::ZeroRotator);
		VisualMesh->SetRelativeScale3D(Definition.MeshScale);
	}
	if (VisualMesh->Mobility != EComponentMobility::Static)
	{
		VisualMesh->SetMobility(EComponentMobility::Static);
	}
}

void AGuLiPlacedBuilding::OnRep_BuildingState()
{
	UGuLiBuildingCatalog* Catalog = UGuLiBuildingCatalog::LoadDefaultCatalog();
	const FGuLiBuildingDefinition* Definition = Catalog
		? (DefinitionId ? Catalog->FindById(DefinitionId) : Catalog->FindDefinition(BuildingType))
		: nullptr;
	if (Definition && Definition->IsUsable())
	{
		ApplyDefinition(*Definition);
	}
}
