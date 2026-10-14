// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Stronghold/GuLiStrongholdTransportNetwork.h"
#include "GuLiResourceWorldState.generated.h"

class AGuLiResourceWorldState;

USTRUCT()
struct GULISTRIKE_API FGuLiOreDeltaItem
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
struct GULISTRIKE_API FGuLiOreDeltaFastArray
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGuLiOreDeltaItem> Items;


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
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiOreNodeChanged, uint32, uint8);

/** Local resource query projection. Immutable layout remains in the cooked DataAsset. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiResourceWorldState final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiResourceWorldState();

	virtual void BeginPlay() override;

	void ApplyTransportNetwork(const FGuLiTransportNetworkSnapshot& Snapshot);
	UFUNCTION(BlueprintPure) FGuLiTransportNetworkSnapshot GetTransportNetworkSnapshot() const { return TransportNetwork; }
	const FGuLiTransportNetworkSnapshot& GetTransportNetwork() const { return TransportNetwork; }
	void ApplyTerritoryEncirclement(int32 Index, bool bEncircled);
	/** Apply the resource owner's committed directory without emitting business actions. */
	bool ApplyHybridDirectory(const FString& InLayoutHash,TConstArrayView<EGuLiTeam> Owners,
		TConstArrayView<uint8> Remaining,TConstArrayView<FVector> GroundLocations);
	bool ApplyHybridNodeRemaining(uint32 NodeId,uint8 RemainingAmount);
    bool ApplyHybridTerritory(int32 Index,EGuLiTeam Owner,bool Supplied);

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
	FGuLiOreNodeChanged OreNodeChanged;

private:


	UPROPERTY()
	FString LayoutHash;

	UPROPERTY()
	bool bAuthorityReady = false;

	UPROPERTY()
	TArray<FGuLiTerritoryRuntimeState> Territories;

	UPROPERTY()
	FGuLiOreDeltaFastArray OreDeltas;
	UPROPERTY() FGuLiTransportNetworkSnapshot TransportNetwork;

	TMap<uint32, int32> OreDeltaIndexByNodeId;
	FGuLiResourceWorldStateChanged StateChanged;
	uint32 NextStateRevision = 1u;

	void RebuildOreIndex();
};
