// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Stronghold/GuLiStrongholdNetworkPresentationComponent.h"
#include "Network/GuLiNetworkSettings.h"


AGuLiResourceWorldState::AGuLiResourceWorldState()
{
	bReplicates = false;
	SetReplicateMovement(false);
}

void AGuLiResourceWorldState::BeginPlay()
{
	Super::BeginPlay();
	RebuildOreIndex();
	if (UGuLiNetworkSettings::CanPresent(GetWorld()))
		NewObject<UGuLiStrongholdNetworkPresentationComponent>(this)->RegisterComponent();
}



void AGuLiResourceWorldState::ApplyTransportNetwork(const FGuLiTransportNetworkSnapshot& Snapshot)
{
	TransportNetwork = Snapshot;
	StateChanged.Broadcast();
}
void AGuLiResourceWorldState::ApplyTerritoryEncirclement(int32 Index, bool bEncircled)
{
	if (Territories[Index].bEncircled == bEncircled) return;
	Territories[Index].bEncircled = bEncircled;
	Territories[Index].Revision = NextStateRevision++;
	StateChanged.Broadcast();
}
bool AGuLiResourceWorldState::ApplyHybridDirectory(const FString& InLayoutHash,TConstArrayView<EGuLiTeam> Owners,
	TConstArrayView<uint8> Remaining,TConstArrayView<FVector> GroundLocations)
{
	if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || InLayoutHash.IsEmpty() || Owners.Num()!=GULI_RESOURCE_TERRITORY_COUNT
		|| GroundLocations.Num()!=Owners.Num() || Remaining.Num()!=GULI_RESOURCE_NODE_COUNT) return false;
	for (int32 I=0;I<Owners.Num();++I)
		if ((Owners[I]!=EGuLiTeam::Unassigned && !GuLiResources::IsPlayableTeam(Owners[I])) || GroundLocations[I].ContainsNaN()) return false;
	for (const auto Amount:Remaining) if (Amount>3) return false;
	SetReplicates(false); LayoutHash=InLayoutHash; bAuthorityReady=false;
	Territories.Reset(Owners.Num());
	for (int32 I=0;I<Owners.Num();++I)
	{
		auto& V=Territories.AddDefaulted_GetRef(); V.TerritoryIndex=uint8(I); V.Owner=Owners[I]; V.GroundLocation=GroundLocations[I];
		// Supply is a separate authoritative relationship, not inferred by this ore query projection.
		V.bSupplied=false; V.Revision=NextStateRevision++;
	}
	OreDeltas.Items.Reset(Remaining.Num()); OreDeltaIndexByNodeId.Reset();
	for (int32 I=0;I<Remaining.Num();++I)
	{
		auto& V=OreDeltas.Items.AddDefaulted_GetRef(); V.NodeId=uint32(I+1); V.RemainingAmount=Remaining[I]; V.Revision=NextStateRevision++;
		OreDeltaIndexByNodeId.Add(V.NodeId,I);
	}
	StateChanged.Broadcast(); return true;
}
bool AGuLiResourceWorldState::ApplyHybridTerritory(int32 Index,EGuLiTeam TerritoryOwner,bool Supplied)
{
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !Territories.IsValidIndex(Index)) return false;
    auto& T=Territories[Index];if (T.Owner==TerritoryOwner && T.bSupplied==Supplied) return true;
    T.Owner=TerritoryOwner;T.bSupplied=Supplied;T.Revision=NextStateRevision++;StateChanged.Broadcast();return true;
}
bool AGuLiResourceWorldState::ApplyHybridNodeRemaining(uint32 NodeId,uint8 RemainingAmount)
{
	if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || RemainingAmount>3) return false;
	const auto* Index=OreDeltaIndexByNodeId.Find(NodeId); if (!Index || !OreDeltas.Items.IsValidIndex(*Index)) return false;
	auto& V=OreDeltas.Items[*Index]; if (V.RemainingAmount==RemainingAmount) return true;
	V.RemainingAmount=RemainingAmount; V.Revision=NextStateRevision++;
	OreNodeChanged.Broadcast(NodeId,RemainingAmount); return true;
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
