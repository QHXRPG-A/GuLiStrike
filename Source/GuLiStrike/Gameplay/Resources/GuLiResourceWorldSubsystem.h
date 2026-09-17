// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Stronghold/GuLiStrongholdTopology.h"
#include "Gameplay/Stronghold/GuLiStrongholdTransportNetwork.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiResourceWorldSubsystem.generated.h"

struct FCollisionQueryParams;

class AGuLiMiningVehiclePawn;
class AGuLiConstructionVehiclePawn;
class AGuLiOreClusterObstacleActor;
class AGuLiOreFieldActor;
class AGuLiResourceFactoryActor;
class AGuLiResourceWorldState;
class AGuLiTerritoryOutpostActor;
class ARecastNavMesh;
class UGuLiResourceEconomyConfig;
class UGuLiResourceMapDefinition;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FGuLiTerritoryOwnershipChanged, int32, EGuLiTeam, EGuLiTeam);

/** Match-scoped authority for ore, territory and factories; economy is a separate service. */
UCLASS(Config = Game)
class GULISTRIKE_API UGuLiResourceWorldSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UGuLiResourceWorldSubsystem();
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	bool IsRuntimeReady() const { return bRuntimeReady; }
	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	bool IsResourceWorldActive() const { return bResourceWorldActive; }
	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	bool HasFatalInitializationError() const { return bFatalInitializationError; }
	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	FString GetInitializationError() const { return InitializationError; }
	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	const UGuLiResourceMapDefinition* GetMapDefinition() const { return MapDefinition; }
	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	const UGuLiResourceEconomyConfig* GetEconomyConfig() const { return EconomyConfig; }
	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	AGuLiResourceWorldState* GetResourceWorldState() const { return WorldState; }
	FBox2D GetPlayableBounds() const;
	int32 FindTerritoryIndex(const FVector& Location) const;
	const FGuLiStrongholdTopology& GetStrongholdTopology() const { return StrongholdTopology; }
	FVector GetTerritoryGroundLocation(int32 Index) const;
	bool IsTerritorySupplied(int32 Index) const;
	bool CanUseStrongholdTransit(int32 Index, EGuLiTeam Team) const;
	int32 FindTerritoryIndexById(FName TerritoryId) const;
	const FGuLiStrongholdTransportNetwork& GetTransportNetwork() const { return TransportNetwork; }
	FVector GetInitialBaseExit(EGuLiTeam Team) const;
	FGuLiTerritoryOwnershipChanged OnTerritoryOwnershipChanged;
	FGuLiControllableActorId AllocateControllableActorId() { return FGuLiControllableActorId(NextControllableActorId++); }
	AGuLiResourceFactoryActor* FindNearestFriendlyFactory(EGuLiTeam Team, const FVector& Location) const;
	void RegisterFactory(AGuLiResourceFactoryActor& Factory);
	AGuLiConstructionVehiclePawn* SpawnConstructionVehicle(EGuLiTeam Team, const FVector& GroundLocation);
	void CreditFactoryOutput(EGuLiTeam SettlementTeam, EGuLiResourceType Type, int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Resources|Territory")
	bool CanTeamMineAt(EGuLiTeam Team, int32 ClusterId) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resources|Territory")
	bool SetTerritoryOwner(uint8 TerritoryIndex, EGuLiTeam NewOwner);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resources|Territory")
	bool SetTerritoryOwnerByBoardCoordinate(int32 Row, int32 Column, EGuLiTeam NewOwner);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resources|Mining")
	bool MineOneRaw(EGuLiTeam Team, int32 ClusterId, EGuLiResourceType& OutResourceType);
	bool MineNodeRaw(EGuLiTeam Team, uint16 ClusterId, uint32 NodeId, EGuLiResourceType& OutResourceType);
	bool GetNodeMiningTarget(uint32 NodeId, FVector& Target) const;
	uint32 FindNearestMiningNode(uint16 ClusterId, const FVector& Position) const;
	void IgnoreMiningNavigationProxies(FCollisionQueryParams& Params) const;
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	bool IsClusterEmpty(int32 ClusterId) const;
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	int32 GetClusterRemainingRaw(int32 ClusterId) const;
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	int32 GetNodeRemainingRaw(int32 NodeId) const;

	UFUNCTION(BlueprintPure, Category = "Resources|Economy")
	FGuLiResourceAmounts GetTeamInventory(EGuLiTeam Team) const;

private:
	UPROPERTY(Config)
	FSoftObjectPath MapDefinitionAsset;

	UPROPERTY(Config)
	FSoftObjectPath EconomyConfigAsset;

	UPROPERTY(Transient)
	TObjectPtr<UGuLiResourceMapDefinition> MapDefinition;

	UPROPERTY(Transient)
	TObjectPtr<UGuLiResourceEconomyConfig> EconomyConfig;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiResourceWorldState> WorldState;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiOreFieldActor> OreField;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AGuLiOreClusterObstacleActor>> ClusterObstacles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AGuLiTerritoryOutpostActor>> Outposts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AGuLiResourceFactoryActor>> Factories;

	TArray<uint8> NodeRemaining;
	TArray<int32> ClusterRemaining;
	TArray<FGuLiDynamicObstacleHandle> ClusterObstacleHandles;
	bool bWorldBeginPlay = false;
	bool bResourceWorldActive = false;
	bool bRuntimeReady = false;
	bool bFatalInitializationError = false;
	bool bAuthorityActorsSpawned = false;
	double NavigationWaitStartSeconds = 0;
	// Runtime world copies only; restore fully asynchronous gathering when the startup gate opens.
	TArray<TWeakObjectPtr<ARecastNavMesh>> InitialSynchronousGatheringNavData;
	bool bOreFieldInitialized = false;
	bool bEconomyMatchStarted = false;
	FString InitializationError;
	uint32 LastAppliedWorldStateRevision = 0u;
	uint32 NextControllableActorId = 1000;
	bool bReplicatedStateDirty = true;
	FGuLiStrongholdTopology StrongholdTopology;
	FGuLiStrongholdTransportNetwork TransportNetwork;
	float CaptureAccumulator = 0;
	float MaintenanceAccumulator = 0;
	float ActiveMaintenancePeriod = 0;
	FGuid MaintenanceAccount;
	uint32 MaintenanceRequestId = 0;
	void TickStrongholds(float DeltaTime);
	void RefreshEncirclement();

	bool LoadAndValidateAssets();
	bool ValidateCurrentMap() const;
	bool SpawnAuthorityActors();
	void DiscoverReplicatedActors();
	void ApplyReplicatedState();
	void UpdateNavigationReadiness();
	FVector ProjectAnchorToGround(const FVector& Anchor) const;
	void HandleWorldStateChanged();
	void DisableDepletedCluster(uint16 ClusterId);
};
