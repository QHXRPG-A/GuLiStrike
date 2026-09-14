// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Stronghold/GuLiStrongholdNetworkPresentationComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdTopology.h"

#include "Net/UnrealNetwork.h"

void FGuLiOreDeltaFastArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, const int32 FinalSize)
{
	(void)AddedIndices;
	(void)FinalSize;
	if (AGuLiResourceWorldState* State = Owner.Get()) State->NotifyFastArrayChanged();
}

void FGuLiOreDeltaFastArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, const int32 FinalSize)
{
	(void)ChangedIndices;
	(void)FinalSize;
	if (AGuLiResourceWorldState* State = Owner.Get()) State->NotifyFastArrayChanged();
}

void FGuLiOreDeltaFastArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, const int32 FinalSize)
{
	(void)RemovedIndices;
	(void)FinalSize;
	if (AGuLiResourceWorldState* State = Owner.Get()) State->NotifyFastArrayChanged();
}

AGuLiResourceWorldState::AGuLiResourceWorldState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(2.0f);
	SetReplicateMovement(false);
	OreDeltas.SetOwner(this);
}

void AGuLiResourceWorldState::BeginPlay()
{
	Super::BeginPlay();
	OreDeltas.SetOwner(this);
	RebuildOreIndex();
	if (GetNetMode() != NM_DedicatedServer)
		NewObject<UGuLiStrongholdNetworkPresentationComponent>(this)->RegisterComponent();
}

void AGuLiResourceWorldState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiResourceWorldState, LayoutHash);
	DOREPLIFETIME(AGuLiResourceWorldState, bAuthorityReady);
	DOREPLIFETIME(AGuLiResourceWorldState, Territories);
	DOREPLIFETIME(AGuLiResourceWorldState, OreDeltas);
	DOREPLIFETIME(AGuLiResourceWorldState, TransportEdges);
}

void AGuLiResourceWorldState::SetTransportEdgesAuthority(TConstArrayView<FGuLiStrongholdEdge> Edges)
{
	check(HasAuthority());
	TransportEdges.Reset();
	for (const auto& Edge : Edges) TransportEdges.Add(FIntPoint(Edge.A,Edge.B));
	ForceNetUpdate();
}

void AGuLiResourceWorldState::InitializeAuthority(
	const FString& InLayoutHash,
	const TConstArrayView<EGuLiTeam> InitialOwners)
{
	if (!HasAuthority() || InLayoutHash.IsEmpty()
		|| InitialOwners.Num() != GULI_RESOURCE_TERRITORY_COUNT)
	{
		return;
	}
	LayoutHash = InLayoutHash;
	Territories.Reset(InitialOwners.Num());
	for (int32 Index = 0; Index < InitialOwners.Num(); ++Index)
	{
		FGuLiTerritoryRuntimeState& State = Territories.AddDefaulted_GetRef();
		State.TerritoryIndex = static_cast<uint8>(Index);
		State.Owner = InitialOwners[Index];
		State.Revision = NextStateRevision++;
	}
	OreDeltas.Items.Reset();
	OreDeltaIndexByNodeId.Reset();
	bAuthorityReady = false;
	ForceNetUpdate();
	StateChanged.Broadcast();
}

void AGuLiResourceWorldState::SetAuthorityReady(const bool bReady)
{
	if (!HasAuthority() || bAuthorityReady == bReady)
	{
		return;
	}
	bAuthorityReady = bReady;
	ForceNetUpdate();
	StateChanged.Broadcast();
}

bool AGuLiResourceWorldState::SetTerritoryOwnerAuthority(
	const uint8 TerritoryIndex,
	const EGuLiTeam NewOwner)
{
	if (!HasAuthority() || !Territories.IsValidIndex(TerritoryIndex)
		|| (NewOwner != EGuLiTeam::Unassigned && !GuLiResources::IsPlayableTeam(NewOwner)))
	{
		return false;
	}
	FGuLiTerritoryRuntimeState& State = Territories[TerritoryIndex];
	if (State.Owner == NewOwner)
	{
		return true;
	}
	State.Owner = NewOwner;
	State.Revision = NextStateRevision++;
	ForceNetUpdate();
	StateChanged.Broadcast();
	return true;
}

void AGuLiResourceWorldState::SetTerritorySupplyAuthority(int32 Index, bool bSupplied)
{
	check(HasAuthority());
	if (Territories[Index].bSupplied == bSupplied) return;
	Territories[Index].bSupplied = bSupplied;
	Territories[Index].Revision = NextStateRevision++;
	ForceNetUpdate(); StateChanged.Broadcast();
}
void AGuLiResourceWorldState::SetTerritoryEncircledAuthority(int32 Index, bool bEncircled)
{
	check(HasAuthority());
	if (Territories[Index].bEncircled == bEncircled) return;
	Territories[Index].bEncircled = bEncircled;
	Territories[Index].Revision = NextStateRevision++;
	ForceNetUpdate(); StateChanged.Broadcast();
}
void AGuLiResourceWorldState::SetTerritoryGroundAuthority(int32 Index, const FVector& GroundLocation)
{
	check(HasAuthority());
	Territories[Index].GroundLocation = GroundLocation;
	ForceNetUpdate();
}

bool AGuLiResourceWorldState::SetNodeRemainingAuthority(
	const uint32 NodeId,
	const uint8 RemainingAmount)
{
	if (!HasAuthority() || NodeId == 0u || NodeId > GULI_RESOURCE_NODE_COUNT
		|| RemainingAmount > 3u)
	{
		return false;
	}
	int32* ExistingIndex = OreDeltaIndexByNodeId.Find(NodeId);
	if (!ExistingIndex)
	{
		FGuLiOreDeltaItem& Item = OreDeltas.Items.AddDefaulted_GetRef();
		Item.NodeId = NodeId;
		Item.RemainingAmount = RemainingAmount;
		Item.Revision = NextStateRevision++;
		const int32 Index = OreDeltas.Items.Num() - 1;
		OreDeltaIndexByNodeId.Add(NodeId, Index);
		OreDeltas.MarkItemDirty(Item);
	}
	else
	{
		FGuLiOreDeltaItem& Item = OreDeltas.Items[*ExistingIndex];
		if (Item.RemainingAmount == RemainingAmount)
		{
			return true;
		}
		Item.RemainingAmount = RemainingAmount;
		Item.Revision = NextStateRevision++;
		OreDeltas.MarkItemDirty(Item);
	}
	ForceNetUpdate();
	StateChanged.Broadcast();
	return true;
}

uint8 AGuLiResourceWorldState::GetNodeRemainingOr(
	const uint32 NodeId,
	const uint8 InitialAmount) const
{
	if (const int32* Index = OreDeltaIndexByNodeId.Find(NodeId))
	{
		return OreDeltas.Items.IsValidIndex(*Index)
			? OreDeltas.Items[*Index].RemainingAmount : InitialAmount;
	}
	return InitialAmount;
}

EGuLiTeam AGuLiResourceWorldState::GetTerritoryOwner(const uint8 TerritoryIndex) const
{
	return Territories.IsValidIndex(TerritoryIndex)
		? Territories[TerritoryIndex].Owner : EGuLiTeam::Unassigned;
}

void AGuLiResourceWorldState::NotifyFastArrayChanged()
{
	RebuildOreIndex();
	StateChanged.Broadcast();
}

void AGuLiResourceWorldState::OnRep_LayoutState()
{
	StateChanged.Broadcast();
}

void AGuLiResourceWorldState::OnRep_Territories()
{
	StateChanged.Broadcast();
}

void AGuLiResourceWorldState::RebuildOreIndex()
{
	OreDeltaIndexByNodeId.Reset();
	for (int32 Index = 0; Index < OreDeltas.Items.Num(); ++Index)
	{
		if (OreDeltas.Items[Index].NodeId != 0u)
		{
			OreDeltaIndexByNodeId.Add(OreDeltas.Items[Index].NodeId, Index);
		}
	}
}
