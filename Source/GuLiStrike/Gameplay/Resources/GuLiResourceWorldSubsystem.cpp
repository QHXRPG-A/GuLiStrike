// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Stronghold/GuLiHybridControlPoint.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Development/GuLiHybridReviewSubsystem.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
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
#include "NavMesh/RecastNavMesh.h"
#include "Engine/StaticMesh.h"
#include "Subsystems/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiResources, Log, All);

namespace
{
	constexpr TCHAR CanonicalResourceMapPackage[] = TEXT("/Game/Maps/LVL_CommanderMassPrototype");


	/** Keep the individual dirty bounds, and let Recast coalesce overlapping tiles once spawning finishes. */
	class FResourceNavigationBuildBatch
	{
	public:
		explicit FResourceNavigationBuildBatch(UWorld* World)
			: Navigation(FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			bOwnsLock = Navigation && !Navigation->IsNavigationBuildingLocked(ENavigationBuildLock::Custom);
			if (bOwnsLock) Navigation->AddNavigationBuildLock(ENavigationBuildLock::Custom);
		}
		~FResourceNavigationBuildBatch()
		{
			if (bOwnsLock)
				Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::Custom,
					UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
		}
	private:
		UNavigationSystemV1* Navigation = nullptr;
		bool bOwnsLock = false;
	};

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarDrawResourceBoard(
		TEXT("gs.Resources.DrawBoard"),
		0,
		TEXT("Draw the canonical 7x7 board, Outpost_RxCy labels and public Territory ownership."),
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
	TArray<AGuLiResourceFactoryActor*> Candidates;
	QueryFriendlyFactories(Team, Location, Candidates);
	return Candidates.IsEmpty() ? nullptr : Candidates[0];
}
void UGuLiResourceWorldSubsystem::QueryFriendlyFactories(EGuLiTeam Team, const FVector& Location,
	TArray<AGuLiResourceFactoryActor*>& Out) const
{
	Out.Reset();
	for (const auto& Factory : Factories)
		if (IsValid(Factory) && !Factory->IsActorBeingDestroyed() && Factory->GetTeam() == Team
			&& Factory->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->IsCompleted())
			Out.Add(Factory);
	Out.StableSort([&](const auto& A, const auto& B) {
		return FVector::DistSquared2D(Location, A.GetActorLocation()) < FVector::DistSquared2D(Location, B.GetActorLocation());
	});
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
	if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
	{
		if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
		{
			Hybrid->OnResourceDirectoryChanged.AddUObject(this,&ThisClass::HandleHybridResourceDirectory);
			Hybrid->OnResourceNodeChanged.AddUObject(this,&ThisClass::HandleHybridResourceNode);
		}
	}
}

void UGuLiResourceWorldSubsystem::Deinitialize()
{
	OnResourceSettlement.Clear();
	if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
	{
		Hybrid->OnResourceDirectoryChanged.RemoveAll(this); Hybrid->OnResourceNodeChanged.RemoveAll(this);
	}
	if (WorldState) { WorldState->OnStateChanged().RemoveAll(this); WorldState->OreNodeChanged.RemoveAll(this); }
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

}

void UGuLiResourceWorldSubsystem::Tick(const float DeltaTime)
{
    if (!bWorldBeginPlay || !bResourceWorldActive || bFatalInitializationError || !bHybridDirectoryDirty) return;
    FString Layout; int32 Nodes=0; uint64 Revision=0;
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Hybrid || !Hybrid->GetResourceDirectoryMetadata(Layout,Nodes,Revision)) return;
    if (!RefreshHybridQueryScene())
    {
        bFatalInitializationError=true; Hybrid->Suspend(InitializationError);
        UE_LOG(LogGuLiResources,Error,TEXT("Resource query initialization failed: %s"),*InitializationError);
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



bool UGuLiResourceWorldSubsystem::RefreshHybridQueryScene()
{
	auto* World=GetWorld(); auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
	FString Layout; int32 Nodes=0; uint64 Revision=0;
	if (!Hybrid || !MapDefinition || !EconomyConfig || !Hybrid->GetResourceDirectoryMetadata(Layout,Nodes,Revision)
		|| Layout!=MapDefinition->LayoutHash || Nodes!=MapDefinition->Nodes.Num())
	{ InitializationError=TEXT("Committed resource catalog does not match the local baked layout"); return false; }
	TArray<uint8> Remaining; Remaining.SetNum(Nodes);
	for (const auto& Node:MapDefinition->Nodes)
		if (!Remaining.IsValidIndex(Node.NodeId-1) || !Hybrid->GetResourceNodeRemaining(Node.NodeId,Remaining[Node.NodeId-1])
			|| Remaining[Node.NodeId-1]>Node.InitialAmount)
		{ InitializationError=TEXT("Committed ore directory is incomplete or exceeds the baked amount"); return false; }
	TArray<EGuLiTeam> Owners; TArray<FVector> GroundLocations;
	for (int32 I=0;I<MapDefinition->Territories.Num();++I)
	{
		Owners.Add(Hybrid->GetResourceTerritoryOwner(I)); GroundLocations.Add(MapDefinition->Territories[I].OutpostGroundLocation);
	}
	const FResourceNavigationBuildBatch NavigationBatch(World);
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!WorldState)
	{
		WorldState=World->SpawnActor<AGuLiResourceWorldState>(AGuLiResourceWorldState::StaticClass(),FTransform::Identity,Params);
		if (!WorldState) { InitializationError=TEXT("Could not create the local resource query projection"); return false; }
		WorldState->SetReplicates(false);
		WorldState->OnStateChanged().AddUObject(this,&ThisClass::HandleWorldStateChanged);
		WorldState->OreNodeChanged.AddUObject(this,&ThisClass::HandleOreNodeChanged);
	}
	if (!WorldState->ApplyHybridDirectory(Layout,Owners,Remaining,GroundLocations))
	{ InitializationError=TEXT("Could not apply the committed resource query projection"); return false; }
	NodeRemaining=MoveTemp(Remaining); ClusterRemaining.Init(0,MapDefinition->Clusters.Num());
	for (int32 I=0;I<Nodes;++I) ClusterRemaining[MapDefinition->Nodes[I].ClusterId-1]+=NodeRemaining[I];
	if (UGuLiNetworkSettings::CanPresent(World))
	{
		if (!OreField)
		{
			OreField=World->SpawnActor<AGuLiOreFieldActor>(AGuLiOreFieldActor::StaticClass(),FTransform::Identity,Params);
			if (!OreField) { InitializationError=TEXT("Could not create the ore presentation field"); return false; }
			OreField->SetReplicates(false);
			if (!OreField->InitializeField(*MapDefinition,*EconomyConfig,*WorldState,InitializationError)) return false;
			bOreFieldInitialized=true;
		}
		else OreField->ApplyReplicatedState();
	}
	if (ClusterObstacles.IsEmpty())
	{
		auto* Obstacles=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
		if (!Obstacles) { InitializationError=TEXT("Missing resource collision/navigation registry"); return false; }
		ClusterObstacleHandles.SetNum(MapDefinition->Clusters.Num());
		for (const auto& Cluster:MapDefinition->Clusters)
		{
			const FTransform Transform(FRotator::ZeroRotator,Cluster.Center);
			auto* Proxy=World->SpawnActorDeferred<AGuLiOreClusterObstacleActor>(AGuLiOreClusterObstacleActor::StaticClass(),Transform,
				nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Proxy) { InitializationError=TEXT("Could not create all resource collision/navigation proxies"); return false; }
			Proxy->SetReplicates(false); Proxy->InitializeObstacle(Cluster.ClusterId,Cluster.ObstacleRadiusCentimeters);
			Proxy->FinishSpawning(Transform); ClusterObstacles.Add(Proxy);
		}
	}
	for (int32 I=0;I<ClusterObstacles.Num();++I) RefreshHybridClusterObstacle(I);
	bHybridQuerySceneReady=true; bHybridDirectoryDirty=false; bReplicatedStateDirty=false;
    TArray<FGuLiHybridObjectState> Objects;Hybrid->GetObjects(Objects);
    for (const auto& O:Objects) if (GuLiHybridControlPoint::Read(O).Index!=INDEX_NONE) ApplyHybridControlPoint(O,false);
	// Formal miners, factories, territory lifecycle and supply have their own grants.
	// An ore query projection alone must not open the full resource gameplay gate.
	UE_LOG(LogGuLiResources,Display,TEXT("Hybrid ore query applied nodes=%d collision_clusters=%d visual_components=%d revision=%llu"),
		Nodes,ClusterObstacles.Num(),OreField?OreField->GetHISMComponentCount():0,Revision);
	return true;
}
void UGuLiResourceWorldSubsystem::HandleHybridResourceDirectory() { bHybridDirectoryDirty=true; }
bool UGuLiResourceWorldSubsystem::IsMiningRuntimeReady() const
{
    if (bFatalInitializationError) return false;
    if (const auto* Review=GetWorld()->GetSubsystem<UGuLiHybridReviewSubsystem>();Review && !Review->IsAutonomousMiningEnabled()) return false;
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    // A rebuilt tile elsewhere does not revoke a valid mining order or stop
    // extraction. Slot preparation and travel defer their own navigation work.
    return bHybridMiningSimulationSupported && bHybridQuerySceneReady && H && H->IsWorldReady();
}
void UGuLiResourceWorldSubsystem::HandleHybridResourceNode(uint32 NodeId,uint8 Remaining)
{
	if (!bHybridQuerySceneReady || bHybridDirectoryDirty) return;
	if (!WorldState || !WorldState->ApplyHybridNodeRemaining(NodeId,Remaining))
	{
		bFatalInitializationError=true; InitializationError=TEXT("Committed ore result cannot be applied to the query scene");
		if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) Hybrid->Suspend(InitializationError);
	}
}
void UGuLiResourceWorldSubsystem::RefreshHybridClusterObstacle(int32 Index)
{
	if (!ClusterObstacles.IsValidIndex(Index) || !ClusterObstacles[Index] || !ClusterObstacleHandles.IsValidIndex(Index)) return;
	auto* Obstacles=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>(); if (!Obstacles) return;
	const bool Enabled=ClusterRemaining[Index]>0; ClusterObstacles[Index]->SetObstacleEnabled(Enabled);
	auto& Handle=ClusterObstacleHandles[Index];
	if (Enabled && !Handle.IsValid())
	{
		const auto& Cluster=MapDefinition->Clusters[Index]; Handle=Obstacles->RegisterObstacle(Cluster.Center,Cluster.ObstacleRadiusCentimeters);
	}
	else if (!Enabled && Handle.IsValid()) { Obstacles->UnregisterObstacle(Handle); Handle.Reset(); }
}
void UGuLiResourceWorldSubsystem::GetHybridQuerySceneCounts(int32& CollisionClusters,int32& EnabledClusters,int32& VisualComponents) const
{
	CollisionClusters=ClusterObstacles.Num(); EnabledClusters=0;
	for (const auto& Proxy:ClusterObstacles) if (Proxy && Proxy->IsObstacleEnabled()) ++EnabledClusters;
	VisualComponents=OreField?OreField->GetHISMComponentCount():0;
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
    return IsMiningRuntimeReady() && Cluster && WorldState && GuLiResources::IsPlayableTeam(Team)
		&& (WorldState->GetTerritoryOwner(Cluster->TerritoryIndex) == Team
			|| WorldState->GetTerritoryOwner(Cluster->TerritoryIndex) == EGuLiTeam::Unassigned)
		&& !IsClusterEmpty(ClusterId);
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
    const UStaticMesh* Mesh = GuLiModels::Load<UStaticMesh>(this,Visual->ModelId);
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
	if (const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) return Hybrid->GetTeamBalance(Team);
	return {};
}

void UGuLiResourceWorldSubsystem::HandleWorldStateChanged()
{
	bReplicatedStateDirty = true;

}

void UGuLiResourceWorldSubsystem::HandleOreNodeChanged(uint32 NodeId,uint8 Remaining)
{
    if (!MapDefinition || NodeId==0 || !NodeRemaining.IsValidIndex(NodeId-1)) return;
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {
        const int32 Cluster=MapDefinition->Nodes[NodeId-1].ClusterId-1;
        if (ClusterRemaining.IsValidIndex(Cluster)) ClusterRemaining[Cluster]+=int32(Remaining)-int32(NodeRemaining[NodeId-1]);
        NodeRemaining[NodeId-1]=Remaining;
        if (UGuLiNetworkSettings::IsHybrid(GetWorld())) RefreshHybridClusterObstacle(Cluster);
    }
    if (OreField) OreField->ApplyNodeAmount(NodeId,Remaining);
}
