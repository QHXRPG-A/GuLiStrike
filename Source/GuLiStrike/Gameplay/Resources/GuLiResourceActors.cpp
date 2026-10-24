// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Resources/GuLiOreCollisionField.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Stronghold/GuLiStrongholdCaptureComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdFacilitiesComponent.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AGuLiOreFieldActor::AGuLiOreFieldActor()
{
	bReplicates = false;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	OreMeshes.Reserve(24);
	for (int32 Index = 0; Index < 24; ++Index)
	{
		const FName Name(*FString::Printf(TEXT("OreHISM_%02d"), Index));
		UHierarchicalInstancedStaticMeshComponent* Component =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(SceneRoot);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetRemoveSwap();
		Component->SetMobility(EComponentMobility::Movable);
		OreMeshes.Add(Component);
	}
}

void AGuLiOreFieldActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AGuLiResourceWorldState* State = WorldState.Get())
	{
		State->OnStateChanged().RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

int32 AGuLiOreFieldActor::MakeComponentIndex(
	const EGuLiResourceType Type,
	const uint8 FamilyIndex,
	const EGuLiOreVisualStage Stage)
{
	if (FamilyIndex > 3u || Stage == EGuLiOreVisualStage::Hidden)
	{
		return INDEX_NONE;
	}
	return static_cast<int32>(Type) * 12 + static_cast<int32>(FamilyIndex) * 3
		+ static_cast<int32>(Stage) - 1;
}

bool AGuLiOreFieldActor::InitializeField(
	const UGuLiResourceMapDefinition& InDefinition,
	const UGuLiResourceEconomyConfig& InConfig,
	AGuLiResourceWorldState& InWorldState,
	FString& OutError)
{
	OutError.Reset();
	if (OreMeshes.Num() != 24 || InDefinition.Nodes.Num() != GULI_RESOURCE_NODE_COUNT)
	{
		OutError = TEXT("Ore field requires exactly 24 HISM components and 6240 baked nodes.");
		return false;
	}
	for (UHierarchicalInstancedStaticMeshComponent* Component : OreMeshes)
	{
		Component->ClearInstances();
		Component->SetVisibility(UGuLiNetworkSettings::CanPresent(GetWorld()));
		if (!UGuLiNetworkSettings::CanPresent(GetWorld())) Component->SetStaticMesh(nullptr);
	}
	if (UGuLiNetworkSettings::CanPresent(GetWorld()))
	for (const FGuLiOreVisualAsset& Asset : InConfig.OreVisuals)
	{
		const int32 ComponentIndex = MakeComponentIndex(
			Asset.ResourceType, Asset.FamilyIndex, Asset.Stage);
		UStaticMesh* Mesh = GuLiModels::Load<UStaticMesh>(this, Asset.ModelId);
		if (!OreMeshes.IsValidIndex(ComponentIndex) || !Mesh)
		{
			OutError = FString::Printf(TEXT("Missing ore mesh for visual key %d/%d/%d."),
				static_cast<int32>(Asset.ResourceType), Asset.FamilyIndex, static_cast<int32>(Asset.Stage));
			return false;
		}
		OreMeshes[ComponentIndex]->SetStaticMesh(Mesh);
	}

	Definition = &InDefinition;
	WorldState = &InWorldState;
	AppliedNodeAmounts.Init(0u, InDefinition.Nodes.Num());
	VisualRefs.SetNum(InDefinition.Nodes.Num());
	NodeIdsByComponentInstance.SetNum(24);
	for (const FGuLiResourceNodeDefinition& Node : InDefinition.Nodes)
	{
		const uint8 Remaining = InWorldState.GetNodeRemainingOr(Node.NodeId, Node.InitialAmount);
		AppliedNodeAmounts[Node.NodeId - 1u] = Remaining;
		AddVisual(Node.NodeId, Remaining);
	}
	InWorldState.OnStateChanged().RemoveAll(this);
	// ResourceWorld forwards only changed nodes; full snapshots are applied on initial synchronization.
	return true;
}

void AGuLiOreFieldActor::ApplyReplicatedState()
{
	AGuLiResourceWorldState* State = WorldState.Get();
	if (!State)
	{
		return;
	}
	if (!Definition.IsValid()) return;
	for (const auto& Node : Definition->Nodes)
		ApplyNodeAmount(Node.NodeId,State->GetNodeRemainingOr(Node.NodeId,Node.InitialAmount));
}

void AGuLiOreFieldActor::ApplyNodeAmount(const uint32 NodeId, const uint8 RemainingAmount)
{
	const int32 NodeIndex = static_cast<int32>(NodeId) - 1;
	if (!AppliedNodeAmounts.IsValidIndex(NodeIndex) || RemainingAmount > 3u
		|| AppliedNodeAmounts[NodeIndex] == RemainingAmount)
	{
		return;
	}
	RemoveVisual(NodeId);
	AppliedNodeAmounts[NodeIndex] = RemainingAmount;
	AddVisual(NodeId, RemainingAmount);
}

int32 AGuLiOreFieldActor::GetVisibleNodeCount() const
{
	int32 Count = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* Component : OreMeshes)
	{
		if (Component)
		{
			Count += Component->GetInstanceCount();
		}
	}
	return Count;
}

bool AGuLiOreFieldActor::ValidateInstanceIndexMap(FString* OutError) const
{
	auto Fail = [OutError](const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
		return false;
	};
	if (OreMeshes.Num() != 24 || NodeIdsByComponentInstance.Num() != OreMeshes.Num()
		|| VisualRefs.Num() != AppliedNodeAmounts.Num())
	{
		return Fail(TEXT("Ore HISM/index-map array sizes differ."));
	}
	for (int32 ComponentIndex = 0; ComponentIndex < OreMeshes.Num(); ++ComponentIndex)
	{
		const UHierarchicalInstancedStaticMeshComponent* Component = OreMeshes[ComponentIndex];
		const TArray<uint32>& NodeIds = NodeIdsByComponentInstance[ComponentIndex];
		if (!Component || Component->GetInstanceCount() != NodeIds.Num())
		{
			return Fail(FString::Printf(TEXT("Ore HISM %d instance count differs from its NodeId map."),
				ComponentIndex));
		}
		for (int32 InstanceIndex = 0; InstanceIndex < NodeIds.Num(); ++InstanceIndex)
		{
			const int32 NodeIndex = static_cast<int32>(NodeIds[InstanceIndex]) - 1;
			if (!VisualRefs.IsValidIndex(NodeIndex)
				|| VisualRefs[NodeIndex].ComponentIndex != ComponentIndex
				|| VisualRefs[NodeIndex].InstanceIndex != InstanceIndex)
			{
				return Fail(FString::Printf(
					TEXT("Ore HISM %d instance %d has a stale reverse NodeId mapping."),
					ComponentIndex, InstanceIndex));
			}
		}
	}
	for (int32 NodeIndex = 0; NodeIndex < VisualRefs.Num(); ++NodeIndex)
	{
		const FVisualInstanceRef& Ref = VisualRefs[NodeIndex];
		const bool bShouldBeVisible = AppliedNodeAmounts.IsValidIndex(NodeIndex)
			&& AppliedNodeAmounts[NodeIndex] > 0u;
		const bool bHasValidRef = OreMeshes.IsValidIndex(Ref.ComponentIndex)
			&& NodeIdsByComponentInstance.IsValidIndex(Ref.ComponentIndex)
			&& NodeIdsByComponentInstance[Ref.ComponentIndex].IsValidIndex(Ref.InstanceIndex)
			&& NodeIdsByComponentInstance[Ref.ComponentIndex][Ref.InstanceIndex]
				== static_cast<uint32>(NodeIndex + 1);
		if (bShouldBeVisible != bHasValidRef)
		{
			return Fail(FString::Printf(TEXT("Ore node %d has inconsistent visibility/index state."),
				NodeIndex + 1));
		}
	}
	if (OutError)
	{
		OutError->Reset();
	}
	return true;
}

void AGuLiOreFieldActor::RemoveVisual(const uint32 NodeId)
{
	const int32 NodeIndex = static_cast<int32>(NodeId) - 1;
	if (!VisualRefs.IsValidIndex(NodeIndex))
	{
		return;
	}
	FVisualInstanceRef& Ref = VisualRefs[NodeIndex];
	if (!OreMeshes.IsValidIndex(Ref.ComponentIndex)
		|| !NodeIdsByComponentInstance.IsValidIndex(Ref.ComponentIndex)
		|| !NodeIdsByComponentInstance[Ref.ComponentIndex].IsValidIndex(Ref.InstanceIndex))
	{
		Ref = FVisualInstanceRef{};
		return;
	}
	TArray<uint32>& NodeIds = NodeIdsByComponentInstance[Ref.ComponentIndex];
	const int32 LastIndex = NodeIds.Num() - 1;
	const uint32 SwappedNodeId = NodeIds[LastIndex];
	if (OreMeshes[Ref.ComponentIndex]->RemoveInstance(Ref.InstanceIndex))
	{
		if (Ref.InstanceIndex != LastIndex)
		{
			NodeIds[Ref.InstanceIndex] = SwappedNodeId;
			const int32 SwappedNodeIndex = static_cast<int32>(SwappedNodeId) - 1;
			if (VisualRefs.IsValidIndex(SwappedNodeIndex))
			{
				VisualRefs[SwappedNodeIndex].InstanceIndex = Ref.InstanceIndex;
			}
		}
		NodeIds.Pop(EAllowShrinking::No);
	}
	Ref = FVisualInstanceRef{};
}

void AGuLiOreFieldActor::AddVisual(const uint32 NodeId, const uint8 RemainingAmount)
{
	if (!UGuLiNetworkSettings::CanPresent(GetWorld())) return;
	const UGuLiResourceMapDefinition* Map = Definition.Get();
	const int32 NodeIndex = static_cast<int32>(NodeId) - 1;
	if (!Map || !Map->Nodes.IsValidIndex(NodeIndex) || !VisualRefs.IsValidIndex(NodeIndex))
	{
		return;
	}
	const FGuLiResourceNodeDefinition& Node = Map->Nodes[NodeIndex];
	const int32 ComponentIndex = MakeComponentIndex(
		Node.ResourceType, Node.FamilyIndex, GuLiResources::AmountToVisualStage(RemainingAmount));
	if (!OreMeshes.IsValidIndex(ComponentIndex))
	{
		return;
	}
	const int32 InstanceIndex = OreMeshes[ComponentIndex]->AddInstance(Node.WorldTransform, true);
	if (InstanceIndex != INDEX_NONE)
	{
		TArray<uint32>& NodeIds = NodeIdsByComponentInstance[ComponentIndex];
		check(InstanceIndex == NodeIds.Num());
		NodeIds.Add(NodeId);
		VisualRefs[NodeIndex].ComponentIndex = ComponentIndex;
		VisualRefs[NodeIndex].InstanceIndex = InstanceIndex;
	}
}

AGuLiOreClusterObstacleActor::AGuLiOreClusterObstacleActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	NavModifier = CreateDefaultSubobject<UGuLiOreNavigationComponent>(TEXT("OreNodeNavAreas"));
}

void AGuLiOreClusterObstacleActor::InitializeObstacle(
	const uint16 InClusterId,
	const float RadiusCentimeters)
{
	// Compatibility for synthetic single-obstacle callers. Resource startup uses InitializeNodes.
	FGuLiOreNodeObstacle Node; Node.NodeId=1; Node.ClusterId=InClusterId;
	Node.Obstacle.Location=GetActorLocation(); Node.Obstacle.RadiusCentimeters=FMath::Max(1.f,RadiusCentimeters);
	Node.HalfHeight=Node.Obstacle.RadiusCentimeters;
	const uint8 Remaining=1;
	InitializeNodes(InClusterId,MakeArrayView(&Node,1),MakeArrayView(&Remaining,1));
}

void AGuLiOreClusterObstacleActor::SetObstacleEnabled(const bool bEnabled)
{
	NavModifier->SetNodesEnabled(bEnabled);
}

float AGuLiOreClusterObstacleActor::GetObstacleRadius() const
{
	return ObstacleRadius;
}

bool AGuLiOreClusterObstacleActor::IsObstacleEnabled() const { return NavModifier->GetAliveCount()>0; }
void AGuLiOreClusterObstacleActor::InitializeNodes(uint16 InClusterId,
	TConstArrayView<FGuLiOreNodeObstacle> Nodes,TConstArrayView<uint8> Remaining)
{
	ClusterId=InClusterId; ObstacleRadius=0;
	for (const auto& Node:Nodes)
		ObstacleRadius=FMath::Max(ObstacleRadius,float(FVector::Dist2D(GetActorLocation(),Node.Obstacle.Location))+Node.Obstacle.RadiusCentimeters);
	NavModifier->InitializeNodes(Nodes,Remaining);
}
void AGuLiOreClusterObstacleActor::RemoveNodes(TConstArrayView<uint32> NodeIds) { NavModifier->RemoveNodes(NodeIds); }


AGuLiTerritoryOutpostActor::AGuLiTerritoryOutpostActor()
{
	CreateDefaultSubobject<UGuLiBuildingLifecycleComponent>(TEXT("Lifecycle"));
	CreateDefaultSubobject<UGuLiStrongholdCaptureComponent>(TEXT("Capture"));
	CreateDefaultSubobject<UGuLiStrongholdFacilitiesComponent>(TEXT("Facilities"));
	SetCanBeDamaged(false);
	bReplicates = false;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	GroundCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("GroundCollision"));
	RootComponent = GroundCollision;
	GroundCollision->SetBoxExtent(FVector(300.0f, 300.0f, 500.0f));
	GroundCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GroundCollision->SetCollisionObjectType(ECC_WorldStatic);
	GroundCollision->SetCollisionResponseToAllChannels(ECR_Block);
	GroundCollision->SetCanEverAffectNavigation(true);
	Presentation = CreateDefaultSubobject<UGuLiOutpostPresentationComponent>(TEXT("Presentation"));
}



void AGuLiTerritoryOutpostActor::InitializeOutpost(
	const uint8 InTerritoryIndex,
	const FName InTerritoryId,
	const EGuLiTeam InOwner,const int32 DefinitionId)
{
	TerritoryIndex = InTerritoryIndex;
	TerritoryId = InTerritoryId;
	OwnerState.Team = InOwner;
	OwnerState.FloatEpochServerTime = UGuLiNetworkSettings::IsHybrid(GetWorld())?UGuLiHybridActorComponent::BattleSeconds(this):GetWorld()->GetTimeSeconds();
	OwnerState.ChangedServerTime = OwnerState.FloatEpochServerTime;
	Presentation->ApplyOwnerState(OwnerState);
	FindComponentByClass<UGuLiBuildingLifecycleComponent>()->InitializeBuilding(DefinitionId, InTerritoryIndex, EGuLiBuildingOrigin::Map, true);
	FindComponentByClass<UGuLiStrongholdCaptureComponent>()->InitializeCapture(InTerritoryIndex, InOwner);
	FindComponentByClass<UGuLiStrongholdFacilitiesComponent>()->InitializeFacilities();

}

void AGuLiTerritoryOutpostActor::SetBuildingTeamAuthority(EGuLiTeam NewTeam)
{
    SetTerritoryOwnerAuthority(NewTeam);
}

void AGuLiTerritoryOutpostActor::SetTerritoryOwnerAuthority(const EGuLiTeam InOwner)
{
	if (HasAuthority() && OwnerState.Team != InOwner)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (OwnerState.Team == EGuLiTeam::Unassigned && InOwner != EGuLiTeam::Unassigned)
			OwnerState.FloatEpochServerTime = Now;
		OwnerState.Team = InOwner;
		OwnerState.ChangedServerTime = Now;
		Presentation->ApplyOwnerState(OwnerState);

	}
}

void AGuLiTerritoryOutpostActor::OnRep_OwnerState()
{
	Presentation->ApplyOwnerState(OwnerState);
}
void AGuLiTerritoryOutpostActor::ApplyHybridOwner(EGuLiTeam InOwner,double LogicalSeconds)
{
    if (OwnerState.Team==InOwner) return;
    if (OwnerState.Team==EGuLiTeam::Unassigned) OwnerState.FloatEpochServerTime=LogicalSeconds;
    OwnerState.Team=InOwner;OwnerState.ChangedServerTime=LogicalSeconds;Presentation->ApplyOwnerState(OwnerState);
}
