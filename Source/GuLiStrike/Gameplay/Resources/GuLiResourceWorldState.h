// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiResourceWorldState.generated.h"

class AGuLiResourceWorldState;
struct FGuLiStrongholdEdge;

USTRUCT()
struct GULISTRIKE_API FGuLiOreDeltaItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 NodeId = 0u;

	UPROPERTY()
	uint8 RemainingAmount = 0u;

	UPROPERTY()
	uint32 Revision = 0u;
};

USTRUCT()
struct GULISTRIKE_API FGuLiOreDeltaFastArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGuLiOreDeltaItem> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FGuLiOreDeltaItem, FGuLiOreDeltaFastArray>(
			Items, DeltaParams, *this);
	}

	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
	void SetOwner(AGuLiResourceWorldState* InOwner) { Owner = InOwner; }

private:
	TWeakObjectPtr<AGuLiResourceWorldState> Owner;
};

template <>
struct TStructOpsTypeTraits<FGuLiOreDeltaFastArray>
	: public TStructOpsTypeTraitsBase2<FGuLiOreDeltaFastArray>
{
	enum { WithNetDeltaSerializer = true };
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiTerritoryRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resources|Territory")
	uint8 TerritoryIndex = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resources|Territory")
	EGuLiTeam Owner = EGuLiTeam::Unassigned;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resources|Territory") bool bSupplied = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resources|Territory") bool bEncircled = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resources|Territory") FVector GroundLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Resources|Territory")
	uint32 Revision = 0u;
};

DECLARE_MULTICAST_DELEGATE(FGuLiResourceWorldStateChanged);

/** Public replicated session state. The full immutable layout remains in the cooked DataAsset. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiResourceWorldState final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiResourceWorldState();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeAuthority(const FString& InLayoutHash, TConstArrayView<EGuLiTeam> InitialOwners);
	void SetAuthorityReady(bool bReady);
	void SetTransportEdgesAuthority(TConstArrayView<FGuLiStrongholdEdge> Edges);
	const TArray<FIntPoint>& GetTransportEdges() const { return TransportEdges; }
	bool SetTerritoryOwnerAuthority(uint8 TerritoryIndex, EGuLiTeam NewOwner);
	void SetTerritorySupplyAuthority(int32 Index, bool bSupplied);
	void SetTerritoryEncircledAuthority(int32 Index, bool bEncircled);
	void SetTerritoryGroundAuthority(int32 Index, const FVector& GroundLocation);
	bool SetNodeRemainingAuthority(uint32 NodeId, uint8 RemainingAmount);

	UFUNCTION(BlueprintPure, Category = "Resources|Network")
	FString GetLayoutHash() const { return LayoutHash; }
	UFUNCTION(BlueprintPure, Category = "Resources|Network")
	bool IsAuthorityReady() const { return bAuthorityReady; }
	const TArray<FGuLiTerritoryRuntimeState>& GetTerritories() const { return Territories; }
	const TArray<FGuLiOreDeltaItem>& GetOreDeltas() const { return OreDeltas.Items; }
	uint8 GetNodeRemainingOr(uint32 NodeId, uint8 InitialAmount) const;
	UFUNCTION(BlueprintPure, Category = "Resources|Territory")
	EGuLiTeam GetTerritoryOwner(uint8 TerritoryIndex) const;

	FGuLiResourceWorldStateChanged& OnStateChanged() { return StateChanged; }
	void NotifyFastArrayChanged();

private:
	UFUNCTION()
	void OnRep_LayoutState();

	UFUNCTION()
	void OnRep_Territories();

	UPROPERTY(ReplicatedUsing = OnRep_LayoutState)
	FString LayoutHash;

	UPROPERTY(ReplicatedUsing = OnRep_LayoutState)
	bool bAuthorityReady = false;

	UPROPERTY(ReplicatedUsing = OnRep_Territories)
	TArray<FGuLiTerritoryRuntimeState> Territories;

	UPROPERTY(Replicated)
	FGuLiOreDeltaFastArray OreDeltas;
	UPROPERTY(Replicated) TArray<FIntPoint> TransportEdges;

	TMap<uint32, int32> OreDeltaIndexByNodeId;
	FGuLiResourceWorldStateChanged StateChanged;
	uint32 NextStateRevision = 1u;

	void RebuildOreIndex();
};
