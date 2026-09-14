// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceMapDefinition.h"

#include "Misc/SecureHash.h"
#include "Engine/StaticMesh.h"

namespace
{
	void HashBytes(FSHA1& Hash, const void* Data, const uint32 NumBytes)
	{
		Hash.Update(static_cast<const uint8*>(Data), NumBytes);
	}

	template <typename T>
	void HashValue(FSHA1& Hash, const T& Value)
	{
		HashBytes(Hash, &Value, sizeof(T));
	}

	void HashString(FSHA1& Hash, const FString& Value)
	{
		FTCHARToUTF8 Utf8(*Value);
		const int32 Length = Utf8.Length();
		HashValue(Hash, Length);
		if (Length > 0)
		{
			HashBytes(Hash, Utf8.Get(), static_cast<uint32>(Length));
		}
	}

	bool FailResourceDefinition(FString& OutError, const FString& Message)
	{
		OutError = Message;
		return false;
	}
}

bool FGuLiOreVisualAsset::IsWellFormed() const
{
	return FamilyIndex <= 3u && Stage != EGuLiOreVisualStage::Hidden && !Mesh.IsNull();
}

bool UGuLiResourceMapDefinition::ValidateDefinition(FString& OutError) const
{
	OutError.Reset();
	if (MapPackage.IsNone() || LayoutVersion != GULI_RESOURCE_LAYOUT_VERSION
		|| DeterministicSeed != GULI_RESOURCE_BAKE_SEED || SourceHash.IsEmpty()
		|| LayoutHash.IsEmpty() || !SpawnAnchors.IsWellFormed())
	{
		return FailResourceDefinition(OutError, TEXT("Bake metadata, fixed seed, or spawn anchors are missing/invalid."));
	}
	if (!PlayableMinimum.Equals(FVector2D(-GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM), 0.1f)
		|| !PlayableMaximum.Equals(FVector2D(GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM), 0.1f))
	{
		return FailResourceDefinition(OutError, TEXT("Playable bounds must be the canonical 560000 x 560000 cm square."));
	}
	if (Territories.Num() != GULI_RESOURCE_TERRITORY_COUNT
		|| Clusters.Num() != GULI_RESOURCE_CLUSTER_COUNT
		|| Nodes.Num() != GULI_RESOURCE_NODE_COUNT)
	{
		return FailResourceDefinition(OutError, FString::Printf(
			TEXT("Expected 25 territories, 240 clusters and 6240 nodes; got %d/%d/%d."),
			Territories.Num(), Clusters.Num(), Nodes.Num()));
	}

	int32 TotalBlueBudget = 0;
	int32 TotalRedBudget = 0;
	TSet<FName> TerritoryIds;
	for (int32 Index = 0; Index < Territories.Num(); ++Index)
	{
		const FGuLiTerritoryDefinition& Territory = Territories[Index];
		FString TerritoryError;
		if (!Territory.IsWellFormed(&TerritoryError)
			|| GuLiResources::ToTerritoryIndex(Territory.BoardRow, Territory.BoardColumn) != Index)
		{
			return FailResourceDefinition(OutError, FString::Printf(TEXT("Territory[%d]: %s"), Index, *TerritoryError));
		}
		if (TerritoryIds.Contains(Territory.TerritoryId))
		{
			return FailResourceDefinition(OutError, TEXT("Duplicate TerritoryId."));
		}
		TerritoryIds.Add(Territory.TerritoryId);
		const EGuLiTeam ExpectedOwner = Territory.BoardRow == 1 && Territory.BoardColumn == 3
			? EGuLiTeam::Red
			: (Territory.BoardRow == 5 && Territory.BoardColumn == 3
				? EGuLiTeam::Blue : EGuLiTeam::Unassigned);
		if (Territory.InitialOwner != ExpectedOwner
			|| !Territory.Center.Equals(GuLiResources::GetTerritoryCenter(
				Territory.BoardRow, Territory.BoardColumn), 1.0f))
		{
			return FailResourceDefinition(OutError, TEXT("Territory center or initial ownership differs from the canonical board."));
		}
		TotalBlueBudget += Territory.BlueClusterBudget;
		TotalRedBudget += Territory.RedClusterBudget;
	}
	if (TotalBlueBudget != GULI_RESOURCE_BLUE_CLUSTER_COUNT
		|| TotalRedBudget != GULI_RESOURCE_RED_CLUSTER_COUNT)
	{
		return FailResourceDefinition(OutError, TEXT("Territory budgets do not total 200 blue / 40 red clusters."));
	}

	TArray<int32> BluePerTerritory;
	TArray<int32> RedPerTerritory;
	BluePerTerritory.Init(0, GULI_RESOURCE_TERRITORY_COUNT);
	RedPerTerritory.Init(0, GULI_RESOURCE_TERRITORY_COUNT);
	TSet<uint16> ClusterIds;
	for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
	{
		const FGuLiResourceClusterDefinition& Cluster = Clusters[ClusterIndex];
		FString ClusterError;
		if (!Cluster.IsWellFormed(Nodes.Num(), &ClusterError)
			|| Cluster.ClusterId != static_cast<uint16>(ClusterIndex + 1)
			|| Cluster.FirstNodeIndex != static_cast<uint32>(ClusterIndex * GULI_RESOURCE_NODES_PER_CLUSTER))
		{
			return FailResourceDefinition(OutError, FString::Printf(TEXT("Cluster[%d]: %s"), ClusterIndex, *ClusterError));
		}
		if (ClusterIds.Contains(Cluster.ClusterId))
		{
			return FailResourceDefinition(OutError, TEXT("Duplicate ClusterId."));
		}
		ClusterIds.Add(Cluster.ClusterId);
		(Cluster.ResourceType == EGuLiResourceType::Blue
			? BluePerTerritory[Cluster.TerritoryIndex]
			: RedPerTerritory[Cluster.TerritoryIndex])++;

		const FGuLiTerritoryDefinition& Territory = Territories[Cluster.TerritoryIndex];
		const FVector Local = Cluster.Center - Territory.Center;
		const float LegalHalfExtent = GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - 3000.0f;
		if (FMath::Abs(Local.X) > LegalHalfExtent || FMath::Abs(Local.Y) > LegalHalfExtent)
		{
			return FailResourceDefinition(OutError, TEXT("Cluster violates the 30 m Territory boundary margin."));
		}

		int32 Capacity = 0;
		int32 FullCount = 0;
		int32 PartialCount = 0;
		int32 RemnantCount = 0;
		for (int32 LocalNodeIndex = 0; LocalNodeIndex < Cluster.NodeCount; ++LocalNodeIndex)
		{
			const int32 NodeIndex = static_cast<int32>(Cluster.FirstNodeIndex) + LocalNodeIndex;
			const FGuLiResourceNodeDefinition& Node = Nodes[NodeIndex];
			FString NodeError;
			if (!Node.IsWellFormed(Clusters.Num(), &NodeError)
				|| Node.NodeId != static_cast<uint32>(NodeIndex + 1)
				|| Node.ClusterId != Cluster.ClusterId || Node.ResourceType != Cluster.ResourceType)
			{
				return FailResourceDefinition(OutError, FString::Printf(TEXT("Node[%d]: %s"), NodeIndex, *NodeError));
			}
			Capacity += Node.InitialAmount;
			FullCount += Node.InitialAmount == 3u ? 1 : 0;
			PartialCount += Node.InitialAmount == 2u ? 1 : 0;
			RemnantCount += Node.InitialAmount == 1u ? 1 : 0;
		}
		if (Capacity != GULI_RESOURCE_RAW_PER_CLUSTER
			|| FullCount != 3 || PartialCount != 8 || RemnantCount != 15)
		{
			return FailResourceDefinition(OutError, TEXT("Each cluster must contain 3 Full, 8 Partial and 15 Remnant nodes (40 raw)."));
		}
	}
	for (int32 TerritoryIndex = 0; TerritoryIndex < Territories.Num(); ++TerritoryIndex)
	{
		if (BluePerTerritory[TerritoryIndex] != Territories[TerritoryIndex].BlueClusterBudget
			|| RedPerTerritory[TerritoryIndex] != Territories[TerritoryIndex].RedClusterBudget)
		{
			return FailResourceDefinition(OutError, TEXT("Baked cluster counts differ from a Territory budget."));
		}
	}
	for (int32 Left = 0; Left < Clusters.Num(); ++Left)
	{
		for (int32 Right = Left + 1; Right < Clusters.Num(); ++Right)
		{
			if (FVector::DistSquared2D(Clusters[Left].Center, Clusters[Right].Center)
				< FMath::Square(5500.0f))
			{
				return FailResourceDefinition(OutError, TEXT("Two cluster centers are closer than 55 m."));
			}
		}
	}
	for (const FGuLiResourceClusterDefinition& Cluster : Clusters)
	{
		const int32 MirrorTerritoryIndex = GULI_RESOURCE_TERRITORY_COUNT - 1 - Cluster.TerritoryIndex;
		const FGuLiResourceClusterDefinition* Mirror = Clusters.FindByPredicate(
			[&](const FGuLiResourceClusterDefinition& Candidate)
			{
				return Candidate.TerritoryIndex == MirrorTerritoryIndex
					&& Candidate.ResourceType == Cluster.ResourceType
					&& FVector2D(Candidate.Center).Equals(-FVector2D(Cluster.Center), 0.25);
			});
		if (!Mirror)
		{
			return FailResourceDefinition(OutError, TEXT("A cluster is missing its 180-degree rotational counterpart."));
		}
		if (!FMath::IsNearlyEqual(
			Cluster.ObstacleRadiusCentimeters, Mirror->ObstacleRadiusCentimeters, 0.1f))
		{
			return FailResourceDefinition(OutError, TEXT("Mirrored cluster obstacle radii differ."));
		}
		for (int32 LocalNodeIndex = 0; LocalNodeIndex < Cluster.NodeCount; ++LocalNodeIndex)
		{
			const FGuLiResourceNodeDefinition& Node =
				Nodes[static_cast<int32>(Cluster.FirstNodeIndex) + LocalNodeIndex];
			const FGuLiResourceNodeDefinition& MirrorNode =
				Nodes[static_cast<int32>(Mirror->FirstNodeIndex) + LocalNodeIndex];
			const FVector2D Local = FVector2D(Node.WorldTransform.GetLocation() - Cluster.Center);
			const FVector2D MirrorLocal = FVector2D(MirrorNode.WorldTransform.GetLocation() - Mirror->Center);
			const float ExpectedMirrorYaw = FRotator::NormalizeAxis(
				Node.WorldTransform.Rotator().Yaw + 180.0f);
			if (Node.FamilyIndex != MirrorNode.FamilyIndex
				|| Node.InitialAmount != MirrorNode.InitialAmount
				|| !Node.WorldTransform.GetScale3D().Equals(MirrorNode.WorldTransform.GetScale3D(), 0.0001)
				|| !MirrorLocal.Equals(-Local, 0.25)
				|| FMath::Abs(FMath::FindDeltaAngleDegrees(
					ExpectedMirrorYaw, MirrorNode.WorldTransform.Rotator().Yaw)) > 0.05f)
			{
				return FailResourceDefinition(OutError,
					TEXT("Mirrored cluster nodes must share family/scale/stage and rotate by 180 degrees."));
			}
		}
	}
	if (!LayoutHash.Equals(CalculateLayoutHash(), ESearchCase::IgnoreCase))
	{
		return FailResourceDefinition(OutError, TEXT("LayoutHash is stale; rebake before starting this map."));
	}
	return true;
}

FString UGuLiResourceMapDefinition::CalculateLayoutHash() const
{
	FSHA1 Hash;
	HashString(Hash, MapPackage.ToString());
	HashValue(Hash, LayoutVersion);
	HashValue(Hash, DeterministicSeed);
	HashString(Hash, SourceHash);
	HashValue(Hash, PlayableMinimum);
	HashValue(Hash, PlayableMaximum);
	HashValue(Hash, SpawnAnchors.RedFactory);
	HashValue(Hash, SpawnAnchors.RedAssembly);
	HashValue(Hash, SpawnAnchors.BlueFactory);
	HashValue(Hash, SpawnAnchors.BlueAssembly);
	for (const FGuLiTerritoryDefinition& Territory : Territories)
	{
		HashString(Hash, Territory.TerritoryId.ToString());
		HashValue(Hash, Territory.BoardRow);
		HashValue(Hash, Territory.BoardColumn);
		HashValue(Hash, Territory.InitialOwner);
		HashValue(Hash, Territory.BlueClusterBudget);
		HashValue(Hash, Territory.RedClusterBudget);
		HashValue(Hash, Territory.Center);
		for (const FVector2D& Point : Territory.LocalPolygon) HashValue(Hash, Point);
	}
	for (const FGuLiResourceClusterDefinition& Cluster : Clusters)
	{
		HashValue(Hash, Cluster.ClusterId);
		HashValue(Hash, Cluster.TerritoryIndex);
		HashValue(Hash, Cluster.ResourceType);
		HashValue(Hash, Cluster.Center);
		HashValue(Hash, Cluster.ObstacleRadiusCentimeters);
		HashValue(Hash, Cluster.FirstNodeIndex);
		HashValue(Hash, Cluster.NodeCount);
	}
	for (const FGuLiResourceNodeDefinition& Node : Nodes)
	{
		HashValue(Hash, Node.NodeId);
		HashValue(Hash, Node.ClusterId);
		HashValue(Hash, Node.ResourceType);
		HashValue(Hash, Node.FamilyIndex);
		HashValue(Hash, Node.InitialAmount);
		const FVector Location = Node.WorldTransform.GetLocation();
		const FQuat Rotation = Node.WorldTransform.GetRotation();
		const FVector Scale = Node.WorldTransform.GetScale3D();
		HashValue(Hash, Location);
		HashValue(Hash, Rotation);
		HashValue(Hash, Scale);
	}
	Hash.Final();
	uint8 Digest[FSHA1::DigestSize];
	Hash.GetHash(Digest);
	return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

const FGuLiTerritoryDefinition* UGuLiResourceMapDefinition::FindTerritory(const uint8 TerritoryIndex) const
{
	return Territories.IsValidIndex(TerritoryIndex) ? &Territories[TerritoryIndex] : nullptr;
}

const FGuLiResourceClusterDefinition* UGuLiResourceMapDefinition::FindCluster(const uint16 ClusterId) const
{
	const int32 Index = static_cast<int32>(ClusterId) - 1;
	return Clusters.IsValidIndex(Index) && Clusters[Index].ClusterId == ClusterId ? &Clusters[Index] : nullptr;
}

UGuLiResourceEconomyConfig::UGuLiResourceEconomyConfig()
{
	static const TCHAR* TypeNames[] = { TEXT("Blue"), TEXT("Red") };
	static const TCHAR* StageNames[] = { TEXT("Remnant"), TEXT("Partial"), TEXT("Full") };
	for (int32 TypeIndex = 0; TypeIndex < 2; ++TypeIndex)
	{
		for (int32 FamilyIndex = 0; FamilyIndex < 4; ++FamilyIndex)
		{
			for (int32 StageIndex = 0; StageIndex < 3; ++StageIndex)
			{
				FGuLiOreVisualAsset& Entry = OreVisuals.AddDefaulted_GetRef();
				Entry.ResourceType = static_cast<EGuLiResourceType>(TypeIndex);
				Entry.FamilyIndex = static_cast<uint8>(FamilyIndex);
				Entry.Stage = static_cast<EGuLiOreVisualStage>(StageIndex + 1);
				const FString AssetName = FString::Printf(TEXT("SM_Ore_%s_%02d_%s"),
					TypeNames[TypeIndex], FamilyIndex + 1, StageNames[StageIndex]);
				const FString Path = FString::Printf(TEXT("/Game/GuLiStrike/Resources/Ores/Meshes/%s/%s.%s"),
					TypeNames[TypeIndex], *AssetName, *AssetName);
				Entry.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path));
			}
		}
	}
	FactoryPresentationClass = TSoftClassPtr<AActor>(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory.BP_ResourceProcessingFactory_C")));
}

bool UGuLiResourceEconomyConfig::ValidateConfig(FString& OutError) const
{
	OutError.Reset();
	if (MiningVehicleUnitTypeId <= 0 || MiningVehicleUnitTypeId > MAX_uint16 || MiningDistanceCentimeters <= 0.0f
		|| MiningRatePerSecond <= 0.0f || CargoCapacity <= 0 || DockingSeconds < 0.0f
		|| FactoryDockOffsetCentimeters < GULI_RESOURCE_FACTORY_DOCK_MIN_OFFSET_CM
		|| FactoryProcessingRatePerSecond <= 0.0f || PlayerOrderGraceSeconds != 3.0f
		|| AutoRetrySeconds <= 0.0f || InitialBlueInventory < 0 || InitialRedInventory < 0
		|| FactoryPresentationClass.IsNull()
		|| FactoryManeuverSpeedCentimetersPerSecond <= 0.0f
		|| SentryTurretBlueCost != 10 || MissileTurretBlueCost != 20 || OutpostBlueCost != 40)
	{
		OutError = TEXT("Economy values violate the approved mining defaults and building costs.");
		return false;
	}
	if (OreVisuals.Num() != 24)
	{
		OutError = TEXT("Exactly 24 ore visual entries are required (2 colors x 4 families x 3 stages).");
		return false;
	}
	TSet<uint32> Keys;
	for (const FGuLiOreVisualAsset& Entry : OreVisuals)
	{
		const uint32 Key = static_cast<uint32>(Entry.ResourceType) * 12u
			+ static_cast<uint32>(Entry.FamilyIndex) * 3u
			+ static_cast<uint32>(Entry.Stage) - 1u;
		if (!Entry.IsWellFormed() || Keys.Contains(Key))
		{
			OutError = TEXT("Ore visual entries contain an invalid or duplicate color/family/stage key.");
			return false;
		}
		Keys.Add(Key);
	}
	return true;
}

const FGuLiOreVisualAsset* UGuLiResourceEconomyConfig::FindOreVisual(
	const EGuLiResourceType ResourceType,
	const uint8 FamilyIndex,
	const EGuLiOreVisualStage Stage) const
{
	return OreVisuals.FindByPredicate([=](const FGuLiOreVisualAsset& Entry)
	{
		return Entry.ResourceType == ResourceType && Entry.FamilyIndex == FamilyIndex
			&& Entry.Stage == Stage;
	});
}
