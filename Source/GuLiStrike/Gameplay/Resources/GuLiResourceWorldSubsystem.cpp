// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Stronghold/GuLiStrongholdCaptureComponent.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"

#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "AI/NavigationSystemBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"
#include "Engine/StaticMesh.h"
#include "Subsystems/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiResources, Log, All);

namespace
{
	constexpr TCHAR CanonicalResourceMapPackage[] = TEXT("/Game/Maps/LVL_CommanderMassPrototype");

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarDrawResourceBoard(
		TEXT("gs.Resources.DrawBoard"),
		0,
		TEXT("Draw the canonical 5x5 board, Outpost_RxCy labels and public Territory ownership."),
		ECVF_Cheat);
#endif

}

int32 UGuLiResourceWorldSubsystem::FindTerritoryIndex(const FVector& Location) const
{
	if (!MapDefinition) return INDEX_NONE;
	for (int32 Index = 0; Index < MapDefinition->Territories.Num(); ++Index)
	{
		const auto& Center = MapDefinition->Territories[Index].Center;
		if (FMath::Abs(Location.X - Center.X) <= GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM
			&& FMath::Abs(Location.Y - Center.Y) <= GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM) return Index;
	}
	return INDEX_NONE;
}
void UGuLiResourceWorldSubsystem::RegisterFactory(AGuLiResourceFactoryActor& Factory)
{
	Factories.AddUnique(&Factory);
}
AGuLiResourceFactoryActor* UGuLiResourceWorldSubsystem::FindNearestFriendlyFactory(EGuLiTeam Team, const FVector& Location) const
{
	AGuLiResourceFactoryActor* Best = nullptr;
	double Distance = TNumericLimits<double>::Max();
	for (const auto& Factory : Factories)
		if (IsValid(Factory) && !Factory->IsActorBeingDestroyed() && Factory->GetTeam() == Team
			&& Factory->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->IsCompleted())
		{
			const double Candidate = FVector::DistSquared2D(Location, Factory->GetDockPoint());
			if (Candidate < Distance) { Distance = Candidate; Best = Factory; }
		}
	return Best;
}

AGuLiConstructionVehiclePawn* UGuLiResourceWorldSubsystem::SpawnConstructionVehicle(EGuLiTeam Team, const FVector& GroundLocation)
{
	check(GetWorld()->GetNetMode() != NM_Client);
	const auto* Unit = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>()->FindSoldierDefinition(EconomyConfig->ConstructionVehicleUnitTypeId);
	check(Unit);
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	auto* Vehicle = GetWorld()->SpawnActor<AGuLiConstructionVehiclePawn>(AGuLiConstructionVehiclePawn::StaticClass(),
		FTransform(FRotator::ZeroRotator, ProjectAnchorToGround(GroundLocation) + FVector(0,0,650)), Params);
	if (Vehicle) Vehicle->InitializeVehicle(Team, AllocateControllableActorId(), *Unit);
	return Vehicle;
}
void UGuLiResourceWorldSubsystem::CreditFactoryOutput(EGuLiTeam Team, EGuLiResourceType Type, int32 Amount)
{
	GetWorld()->GetSubsystem<UGuLiTeamEconomySubsystem>()->Credit(Team, Type, Amount);
}

UGuLiResourceWorldSubsystem::UGuLiResourceWorldSubsystem()
{
	MapDefinitionAsset = FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
	EconomyConfigAsset = FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy.DA_ResourceEconomy"));
}

bool UGuLiResourceWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiResourceWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiTeamEconomySubsystem>();
	Collection.InitializeDependency<UGuLiUnitDataSubsystem>();
	Collection.InitializeDependency<UGuLiMiningVehicleManager>();
	Collection.InitializeDependency<UGuLiDynamicObstacleRegistrySubsystem>();
}

void UGuLiResourceWorldSubsystem::Deinitialize()
{
	if (WorldState) WorldState->OnStateChanged().RemoveAll(this);
	bEconomyMatchStarted = false;
	ClusterObstacleHandles.Reset();
	Factories.Reset();
	Outposts.Reset();
	ClusterObstacles.Reset();
	OreField = nullptr;
	WorldState = nullptr;
	MapDefinition = nullptr;
	EconomyConfig = nullptr;
	Super::Deinitialize();
}

void UGuLiResourceWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldBeginPlay = true;
	const FString RuntimePackage = UWorld::RemovePIEPrefix(InWorld.GetOutermost()->GetName());
	if (RuntimePackage != CanonicalResourceMapPackage
		&& !RuntimePackage.EndsWith(CanonicalResourceMapPackage))
	{
		// Other QA/gameplay maps retain their pre-resource behavior. Downstream startup
		// treats this explicit no-op state as ready instead of waiting on unrelated assets.
		bRuntimeReady = true;
		return;
	}
	bResourceWorldActive = true;
	if (!LoadAndValidateAssets())
	{
		bFatalInitializationError = true;
		UE_LOG(LogGuLiResources, Error, TEXT("Resource startup blocked: %s"), *InitializationError);
		return;
	}
	StrongholdTopology.Initialize(MapDefinition->Territories);
	if (InWorld.GetNetMode() != NM_Client)
	{
		if (!SpawnAuthorityActors())
		{
			bFatalInitializationError = true;
			UE_LOG(LogGuLiResources, Error, TEXT("Resource startup blocked: %s"), *InitializationError);
		}
	}
}

void UGuLiResourceWorldSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	if (!bWorldBeginPlay || !bResourceWorldActive || bFatalInitializationError) return;
#if !UE_BUILD_SHIPPING
	if (CVarDrawResourceBoard.GetValueOnGameThread() != 0 && MapDefinition)
	{
		for (int32 Line = 0; Line <= GULI_RESOURCE_BOARD_DIMENSION; ++Line)
		{
			const float Coordinate = -GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM
				+ Line * GULI_RESOURCE_TERRITORY_SIZE_CM;
			for (int32 Segment = 0; Segment < GULI_RESOURCE_BOARD_DIMENSION; ++Segment)
			{
				const float Start = -GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM
					+ Segment * GULI_RESOURCE_TERRITORY_SIZE_CM;
				const float End = Start + GULI_RESOURCE_TERRITORY_SIZE_CM;
				FVector VerticalStart = ProjectAnchorToGround(FVector(Coordinate, Start, 0.0f));
				FVector VerticalEnd = ProjectAnchorToGround(FVector(Coordinate, End, 0.0f));
				FVector HorizontalStart = ProjectAnchorToGround(FVector(Start, Coordinate, 0.0f));
				FVector HorizontalEnd = ProjectAnchorToGround(FVector(End, Coordinate, 0.0f));
				VerticalStart.Z += 500.0f;
				VerticalEnd.Z += 500.0f;
				HorizontalStart.Z += 500.0f;
				HorizontalEnd.Z += 500.0f;
				DrawDebugLine(GetWorld(), VerticalStart, VerticalEnd, FColor::Cyan, false, 0.0f, 0, 60.0f);
				DrawDebugLine(GetWorld(), HorizontalStart, HorizontalEnd, FColor::Cyan, false, 0.0f, 0, 60.0f);
			}
		}
		for (int32 TerritoryIndex = 0; TerritoryIndex < MapDefinition->Territories.Num(); ++TerritoryIndex)
		{
			const FGuLiTerritoryDefinition& Territory = MapDefinition->Territories[TerritoryIndex];
			const EGuLiTeam Owner = WorldState
				? WorldState->GetTerritoryOwner(static_cast<uint8>(TerritoryIndex)) : Territory.InitialOwner;
			const FColor Color = Owner == EGuLiTeam::Red ? FColor::Red
				: (Owner == EGuLiTeam::Blue ? FColor::Blue : FColor::Silver);
			FVector LabelLocation = ProjectAnchorToGround(Territory.Center);
			LabelLocation.Z += 8000.0f;
			const TCHAR* OwnerText = Owner == EGuLiTeam::Red ? TEXT("Red")
				: (Owner == EGuLiTeam::Blue ? TEXT("Blue") : TEXT("Neutral"));
			DrawDebugString(GetWorld(), LabelLocation,
				FString::Printf(TEXT("%s [%s]"), *Territory.TerritoryId.ToString(), OwnerText),
				nullptr, Color, 0.0f, true, 2.0f);
		}
	}
#endif
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		DiscoverReplicatedActors();
		if (bReplicatedStateDirty) ApplyReplicatedState();
	}
	else
	{
		UpdateNavigationReadiness();
		if (bRuntimeReady) TickStrongholds(DeltaTime);
	}
}

TStatId UGuLiResourceWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiResourceWorldSubsystem, STATGROUP_Tickables);
}

bool UGuLiResourceWorldSubsystem::LoadAndValidateAssets()
{
	InitializationError.Reset();
	MapDefinition = Cast<UGuLiResourceMapDefinition>(MapDefinitionAsset.TryLoad());
	EconomyConfig = Cast<UGuLiResourceEconomyConfig>(EconomyConfigAsset.TryLoad());
	if (!MapDefinition || !EconomyConfig)
	{
		InitializationError = FString::Printf(TEXT("Missing cooked resource assets: map=%s economy=%s."),
			*MapDefinitionAsset.ToString(), *EconomyConfigAsset.ToString());
		return false;
	}
	if (!MapDefinition->ValidateDefinition(InitializationError)) return false;
	if (!EconomyConfig->ValidateConfig(InitializationError)) return false;
	if (!ValidateCurrentMap())
	{
		InitializationError = FString::Printf(TEXT("Baked MapPackage %s does not match loaded map %s."),
			*MapDefinition->MapPackage.ToString(), *GetWorld()->GetOutermost()->GetName());
		return false;
	}
	NodeRemaining.SetNum(MapDefinition->Nodes.Num());
	for (int32 Index = 0; Index < MapDefinition->Nodes.Num(); ++Index)
		NodeRemaining[Index] = MapDefinition->Nodes[Index].InitialAmount;
	ClusterRemaining.Init(GULI_RESOURCE_RAW_PER_CLUSTER, MapDefinition->Clusters.Num());
	return true;
}

bool UGuLiResourceWorldSubsystem::ValidateCurrentMap() const
{
	if (!MapDefinition || !GetWorld()) return false;
	const FString RuntimePackage = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	return RuntimePackage == MapDefinition->MapPackage.ToString()
		|| RuntimePackage.EndsWith(MapDefinition->MapPackage.ToString());
}

bool UGuLiResourceWorldSubsystem::SpawnAuthorityActors()
{
	UWorld* World = GetWorld();
	if (!World || !MapDefinition || !EconomyConfig || bAuthorityActorsSpawned) return false;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	WorldState = World->SpawnActor<AGuLiResourceWorldState>(
		AGuLiResourceWorldState::StaticClass(), FTransform::Identity, Params);
	if (!WorldState)
	{
		InitializationError = TEXT("Could not spawn the replicated resource world state.");
		return false;
	}
	TArray<EGuLiTeam> Owners;
	Owners.Reserve(MapDefinition->Territories.Num());
	for (const FGuLiTerritoryDefinition& Territory : MapDefinition->Territories)
		Owners.Add(Territory.InitialOwner);
	WorldState->InitializeAuthority(MapDefinition->LayoutHash, Owners);
	WorldState->SetTransportEdgesAuthority(StrongholdTopology.GetTransportEdges());
	MaintenanceAccount = FGuid::NewGuid();
	WorldState->OnStateChanged().AddUObject(this, &ThisClass::HandleWorldStateChanged);

	NodeRemaining.SetNum(MapDefinition->Nodes.Num());
	for (int32 Index = 0; Index < MapDefinition->Nodes.Num(); ++Index)
		NodeRemaining[Index] = MapDefinition->Nodes[Index].InitialAmount;
	ClusterRemaining.Init(GULI_RESOURCE_RAW_PER_CLUSTER, MapDefinition->Clusters.Num());
	FGuLiResourceAmounts StartingBalance;
	StartingBalance.Blue = EconomyConfig->InitialBlueInventory;
	StartingBalance.Red = EconomyConfig->InitialRedInventory;
	UGuLiTeamEconomySubsystem* Economy = World->GetSubsystem<UGuLiTeamEconomySubsystem>();
	check(Economy);
	Economy->BeginMatch(StartingBalance, StartingBalance);
	bEconomyMatchStarted = true;

	OreField = World->SpawnActor<AGuLiOreFieldActor>(
		AGuLiOreFieldActor::StaticClass(), FTransform::Identity, Params);
	FString FieldError;
	if (!OreField || !OreField->InitializeField(*MapDefinition, *EconomyConfig, *WorldState, FieldError))
	{
		InitializationError = FString::Printf(TEXT("Could not initialize ore HISM field: %s"), *FieldError);
		return false;
	}
	bOreFieldInitialized = true;

	for (int32 Index = 0; Index < MapDefinition->Territories.Num(); ++Index)
	{
		const FGuLiTerritoryDefinition& Territory = MapDefinition->Territories[Index];
		FVector Location = ProjectAnchorToGround(Territory.Center);
		Location.Z += 2500.0f;
		AGuLiTerritoryOutpostActor* Outpost = World->SpawnActor<AGuLiTerritoryOutpostActor>(
			AGuLiTerritoryOutpostActor::StaticClass(), FTransform(FRotator::ZeroRotator, Location), Params);
		if (!Outpost)
		{
			InitializationError = TEXT("Could not spawn all 25 Territory landmarks.");
			return false;
		}
		Outpost->InitializeOutpost(static_cast<uint8>(Index), Territory.TerritoryId, Territory.InitialOwner);
		Outposts.Add(Outpost);
		WorldState->SetTerritoryGroundAuthority(Index, Outpost->GetBuildingGroundLocation());
	}

	ClusterObstacles.Reserve(MapDefinition->Clusters.Num());
	ClusterObstacleHandles.Reserve(MapDefinition->Clusters.Num());
	UGuLiDynamicObstacleRegistrySubsystem* DynamicObstacles =
		World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	check(DynamicObstacles);
	for (const FGuLiResourceClusterDefinition& Cluster : MapDefinition->Clusters)
	{
		AGuLiOreClusterObstacleActor* Obstacle = World->SpawnActor<AGuLiOreClusterObstacleActor>(
			AGuLiOreClusterObstacleActor::StaticClass(),
			FTransform(FRotator::ZeroRotator, Cluster.Center), Params);
		if (!Obstacle)
		{
			InitializationError = TEXT("Could not spawn all 240 cluster obstacles.");
			return false;
		}
		Obstacle->InitializeObstacle(Cluster.ClusterId, Cluster.ObstacleRadiusCentimeters);
		ClusterObstacles.Add(Obstacle);
		ClusterObstacleHandles.Add(DynamicObstacles->RegisterObstacle(
			Cluster.Center, Cluster.ObstacleRadiusCentimeters));
	}

	auto SpawnFactoryAndVehicle = [&](const EGuLiTeam Team, const FVector& FactoryAnchor,
		const FVector& VehicleAnchor) -> bool
	{
		const FVector InteriorDirection = FVector(-FactoryAnchor.X, -FactoryAnchor.Y, 0.0f).GetSafeNormal();
		check(!InteriorDirection.IsNearlyZero());
		const FVector FactoryLocation = ProjectAnchorToGround(FactoryAnchor);
		const FVector DockPoint = ProjectAnchorToGround(
			FactoryAnchor + InteriorDirection * EconomyConfig->FactoryDockOffsetCentimeters);
		AGuLiResourceFactoryActor* Factory = World->SpawnActor<AGuLiResourceFactoryActor>(
			AGuLiResourceFactoryActor::StaticClass(),
			FTransform(FRotator(0.0f, Team == EGuLiTeam::Red ? -90.0f : 90.0f, 0.0f),
				FactoryLocation), Params);
		if (!Factory) return false;
		Factory->InitializeFactory(Team, *EconomyConfig, DockPoint);
		Factories.AddUnique(Factory);
		FVector VehicleLocation = ProjectAnchorToGround(VehicleAnchor);
		VehicleLocation.Z += 650.0f;
		return World->GetSubsystem<UGuLiMiningVehicleManager>()->SpawnMiningVehicle(Factory,
			FTransform(FRotator(0.0f, Team == EGuLiTeam::Red ? -90.0f : 90.0f, 0.0f), VehicleLocation)) != nullptr;
	};
	if (!SpawnFactoryAndVehicle(EGuLiTeam::Red, MapDefinition->SpawnAnchors.RedFactory,
			MapDefinition->SpawnAnchors.RedAssembly)
		|| !SpawnFactoryAndVehicle(EGuLiTeam::Blue, MapDefinition->SpawnAnchors.BlueFactory,
			MapDefinition->SpawnAnchors.BlueAssembly))
	{
		InitializationError = TEXT("Could not spawn both team factories and mining vehicles.");
		return false;
	}
	for (int32 Index = 0; Index < EconomyConfig->InitialConstructionVehiclesPerTeam; ++Index)
	{
		const FVector Offset(6000 + Index * 2000, 0, 0);
		SpawnConstructionVehicle(EGuLiTeam::Red, MapDefinition->SpawnAnchors.RedAssembly + Offset);
		SpawnConstructionVehicle(EGuLiTeam::Blue, MapDefinition->SpawnAnchors.BlueAssembly + Offset);
	}
	RefreshEncirclement();
	bAuthorityActorsSpawned = true;
	UE_LOG(LogGuLiResources, Display,
		TEXT("Initialized 25 territories, 240 ore clusters, 6240 nodes, 24 HISMs, 2 factories and 2 miners; waiting for dynamic navigation."));
	return true;
}

void UGuLiResourceWorldSubsystem::DiscoverReplicatedActors()
{
	if (!WorldState)
	{
		for (TActorIterator<AGuLiResourceWorldState> It(GetWorld()); It; ++It)
		{
			WorldState = *It;
			WorldState->OnStateChanged().AddUObject(this, &ThisClass::HandleWorldStateChanged);
			bReplicatedStateDirty = true;
			break;
		}
	}
	if (!OreField)
	{
		for (TActorIterator<AGuLiOreFieldActor> It(GetWorld()); It; ++It)
		{
			OreField = *It;
			bReplicatedStateDirty = true;
			break;
		}
	}
	if (Factories.IsEmpty())
		for (TActorIterator<AGuLiResourceFactoryActor> It(GetWorld()); It; ++It) Factories.Add(*It);
}

void UGuLiResourceWorldSubsystem::ApplyReplicatedState()
{
	if (!WorldState || !MapDefinition || !EconomyConfig || !OreField) return;
	if (!WorldState->GetLayoutHash().IsEmpty()
		&& !WorldState->GetLayoutHash().Equals(MapDefinition->LayoutHash, ESearchCase::CaseSensitive))
	{
		bFatalInitializationError = true;
		InitializationError = TEXT("Server/client resource layout hash mismatch; client will not enter Ready.");
		UE_LOG(LogGuLiResources, Error, TEXT("%s"), *InitializationError);
		return;
	}
	if (!bOreFieldInitialized)
	{
		FString Error;
		if (!OreField->InitializeField(*MapDefinition, *EconomyConfig, *WorldState, Error))
		{
			bFatalInitializationError = true;
			InitializationError = Error;
			return;
		}
		bOreFieldInitialized = true;
	}
	for (int32 Index = 0; Index < MapDefinition->Nodes.Num(); ++Index)
		NodeRemaining[Index] = MapDefinition->Nodes[Index].InitialAmount;
	for (const FGuLiOreDeltaItem& Delta : WorldState->GetOreDeltas())
	{
		const int32 NodeIndex = static_cast<int32>(Delta.NodeId) - 1;
		if (NodeRemaining.IsValidIndex(NodeIndex)) NodeRemaining[NodeIndex] = Delta.RemainingAmount;
	}
	ClusterRemaining.Init(0, MapDefinition->Clusters.Num());
	for (int32 NodeIndex = 0; NodeIndex < MapDefinition->Nodes.Num(); ++NodeIndex)
	{
		const int32 ClusterIndex = static_cast<int32>(MapDefinition->Nodes[NodeIndex].ClusterId) - 1;
		if (ClusterRemaining.IsValidIndex(ClusterIndex)) ClusterRemaining[ClusterIndex] += NodeRemaining[NodeIndex];
	}
	OreField->ApplyReplicatedState();
	bRuntimeReady = WorldState->IsAuthorityReady() && bOreFieldInitialized;
	bReplicatedStateDirty = false;
}

void UGuLiResourceWorldSubsystem::UpdateNavigationReadiness()
{
	if (bRuntimeReady || !bAuthorityActorsSpawned || !WorldState) return;
	if (!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))
	{
		UGuLiTeamEconomySubsystem* Economy = GetWorld()->GetSubsystem<UGuLiTeamEconomySubsystem>();
		check(Economy && bEconomyMatchStarted);
		Economy->OpenTransactions();
		bRuntimeReady = true;
		WorldState->SetAuthorityReady(true);
		UE_LOG(LogGuLiResources, Display,
			TEXT("Resource world and navigation are Ready; Mass spawning and Commander input may proceed."));
	}
}

FVector UGuLiResourceWorldSubsystem::ProjectAnchorToGround(const FVector& Anchor) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiResourceGroundProjection), false);
	const FVector Start(Anchor.X, Anchor.Y, Anchor.Z + 100000.0f);
	const FVector End(Anchor.X, Anchor.Y, Anchor.Z - 100000.0f);
	return GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params)
		? Hit.Location : Anchor;
}

FBox2D UGuLiResourceWorldSubsystem::GetPlayableBounds() const
{
	return MapDefinition
		? FBox2D(MapDefinition->PlayableMinimum, MapDefinition->PlayableMaximum)
		: FBox2D(FVector2D(-GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM),
			FVector2D(GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM));
}

bool UGuLiResourceWorldSubsystem::CanTeamMineAt(const EGuLiTeam Team, const int32 ClusterId) const
{
	const FGuLiResourceClusterDefinition* Cluster = MapDefinition && ClusterId > 0 && ClusterId <= MAX_uint16
		? MapDefinition->FindCluster(static_cast<uint16>(ClusterId)) : nullptr;
	return bRuntimeReady && Cluster && WorldState && GuLiResources::IsPlayableTeam(Team)
		&& WorldState->GetTerritoryOwner(Cluster->TerritoryIndex) == Team
		&& !IsClusterEmpty(ClusterId);
}

bool UGuLiResourceWorldSubsystem::SetTerritoryOwner(
	const uint8 TerritoryIndex,
	const EGuLiTeam NewOwner)
{
	const EGuLiTeam OldOwner = WorldState ? WorldState->GetTerritoryOwner(TerritoryIndex) : EGuLiTeam::Unassigned;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !WorldState
		|| !WorldState->SetTerritoryOwnerAuthority(TerritoryIndex, NewOwner))
	{
		return false;
	}
	if (OldOwner == NewOwner) return true;
	Outposts[TerritoryIndex]->SetTerritoryOwnerAuthority(NewOwner);
	Outposts[TerritoryIndex]->FindComponentByClass<UGuLiStrongholdCaptureComponent>()->SetOwnerEndpoint(NewOwner);
	RefreshEncirclement();
	OnTerritoryOwnershipChanged.Broadcast(TerritoryIndex, OldOwner, NewOwner);
	return true;
}

bool UGuLiResourceWorldSubsystem::SetTerritoryOwnerByBoardCoordinate(
	const int32 Row, const int32 Column, const EGuLiTeam NewOwner)
{
	const int32 Index = GuLiResources::ToTerritoryIndex(Row, Column);
	return Index != INDEX_NONE && SetTerritoryOwner(static_cast<uint8>(Index), NewOwner);
}

bool UGuLiResourceWorldSubsystem::MineOneRaw(
    const EGuLiTeam Team, const int32 ClusterId, EGuLiResourceType& OutResourceType)
{
    if (!CanTeamMineAt(Team, ClusterId)) return false;
    const FGuLiResourceClusterDefinition& Cluster = *MapDefinition->FindCluster(ClusterId);
    for (int32 LocalIndex = 0; LocalIndex < Cluster.NodeCount; ++LocalIndex)
    {
        const uint32 NodeId = MapDefinition->Nodes[Cluster.FirstNodeIndex + LocalIndex].NodeId;
        if (NodeRemaining[NodeId - 1] > 0) return MineNodeRaw(Team, ClusterId, NodeId, OutResourceType);
    }
    return false;
}

bool UGuLiResourceWorldSubsystem::MineNodeRaw(
    const EGuLiTeam Team, const uint16 ClusterId, const uint32 NodeId, EGuLiResourceType& OutResourceType)
{
    if (GetWorld()->GetNetMode() == NM_Client || !CanTeamMineAt(Team, ClusterId)
        || NodeId == 0 || !NodeRemaining.IsValidIndex(NodeId - 1) || NodeRemaining[NodeId - 1] == 0) return false;
    const FGuLiResourceNodeDefinition& Node = MapDefinition->Nodes[NodeId - 1];
    if (Node.ClusterId != ClusterId) return false;
    --NodeRemaining[NodeId - 1];
    --ClusterRemaining[ClusterId - 1];
    OutResourceType = Node.ResourceType;
    WorldState->SetNodeRemainingAuthority(NodeId, NodeRemaining[NodeId - 1]);
    if (ClusterRemaining[ClusterId - 1] == 0) DisableDepletedCluster(ClusterId);
    return true;
}

int32 UGuLiResourceWorldSubsystem::GetNodeRemainingRaw(const int32 NodeId) const
{
    return NodeRemaining.IsValidIndex(NodeId - 1) ? NodeRemaining[NodeId - 1] : 0;
}

bool UGuLiResourceWorldSubsystem::GetNodeMiningTarget(const uint32 NodeId, FVector& Target) const
{
    if (NodeId == 0 || !NodeRemaining.IsValidIndex(NodeId - 1) || NodeRemaining[NodeId - 1] == 0) return false;
    const FGuLiResourceNodeDefinition& Node = MapDefinition->Nodes[NodeId - 1];
    const FGuLiOreVisualAsset* Visual = EconomyConfig->FindOreVisual(Node.ResourceType, Node.FamilyIndex,
        static_cast<EGuLiOreVisualStage>(NodeRemaining[NodeId - 1]));
    check(Visual);
    const UStaticMesh* Mesh = Visual->Mesh.Get();
    check(Mesh);
    Target = Node.WorldTransform.TransformPosition(Mesh->GetBounds().Origin);
    return true;
}

uint32 UGuLiResourceWorldSubsystem::FindNearestMiningNode(const uint16 ClusterId, const FVector& Position) const
{
    const FGuLiResourceClusterDefinition* Cluster = MapDefinition->FindCluster(ClusterId);
    if (!Cluster) return 0;
    uint32 Best = 0;
    double BestDistance = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Cluster->NodeCount; ++Index)
    {
        const uint32 NodeId = MapDefinition->Nodes[Cluster->FirstNodeIndex + Index].NodeId;
        FVector Target;
        if (GetNodeMiningTarget(NodeId, Target))
        {
            const double Distance = FVector::DistSquared(Position, Target);
            if (Distance < BestDistance) { Best = NodeId; BestDistance = Distance; }
        }
    }
    return Best;
}

void UGuLiResourceWorldSubsystem::IgnoreMiningNavigationProxies(FCollisionQueryParams& Params) const
{
    for (const AGuLiOreClusterObstacleActor* Proxy : ClusterObstacles) Params.AddIgnoredActor(Proxy);
}

bool UGuLiResourceWorldSubsystem::IsClusterEmpty(const int32 ClusterId) const
{
	const int32 Index = static_cast<int32>(ClusterId) - 1;
	return !ClusterRemaining.IsValidIndex(Index) || ClusterRemaining[Index] <= 0;
}

int32 UGuLiResourceWorldSubsystem::GetClusterRemainingRaw(const int32 ClusterId) const
{
	const int32 Index = static_cast<int32>(ClusterId) - 1;
	return ClusterRemaining.IsValidIndex(Index) ? ClusterRemaining[Index] : 0;
}

void UGuLiResourceWorldSubsystem::DisableDepletedCluster(const uint16 ClusterId)
{
	const int32 Index = static_cast<int32>(ClusterId) - 1;
	check(ClusterObstacles.IsValidIndex(Index) && ClusterObstacles[Index]);
	check(ClusterObstacleHandles.IsValidIndex(Index) && ClusterObstacleHandles[Index].IsValid());
	ClusterObstacles[Index]->SetObstacleEnabled(false);
	UGuLiDynamicObstacleRegistrySubsystem* Obstacles =
		GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	check(Obstacles);
	Obstacles->UnregisterObstacle(ClusterObstacleHandles[Index]);
	ClusterObstacleHandles[Index].Reset();
}

FGuLiResourceAmounts UGuLiResourceWorldSubsystem::GetTeamInventory(const EGuLiTeam Team) const
{
	const UGuLiTeamEconomySubsystem* Economy =
		GetWorld()->GetSubsystem<UGuLiTeamEconomySubsystem>();
	check(Economy);
	return Economy->GetTeamBalance(Team);
}

void UGuLiResourceWorldSubsystem::HandleWorldStateChanged()
{
	bReplicatedStateDirty = true;
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Client && OreField)
		OreField->ApplyReplicatedState();
}
