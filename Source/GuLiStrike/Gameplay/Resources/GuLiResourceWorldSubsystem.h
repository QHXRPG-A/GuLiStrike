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
struct FGuLiHybridObjectKey;
struct FGuLiHybridResourceRequest;
struct FGuLiHybridTransactionResult;
struct FGuLiHybridObjectState;

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
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiResourceSettlement, const FGuLiHybridTransactionResult&);

/** Match-scoped authority for ore, territory and factories; economy is a separate service. */
UCLASS(Config = Game)
class GULISTRIKE_API UGuLiResourceWorldSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UGuLiResourceWorldSubsystem();
	virtual ~UGuLiResourceWorldSubsystem() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	bool IsRuntimeReady() const { return bRuntimeReady; }
	UFUNCTION(BlueprintPure, Category = "Resources|Runtime")
	bool IsHybridQuerySceneReady() const { return bHybridQuerySceneReady; }
	bool IsMiningRuntimeReady() const;
	void EnableHybridMiningSimulation() { bHybridMiningSimulationSupported=true; }
	void GetHybridQuerySceneCounts(int32& CollisionClusters,int32& EnabledClusters,int32& VisualComponents) const;
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
	int32 GetTransportRegion(EGuLiTeam Team, int32 TerritoryIndex) const;
	FVector GetTerritoryGroundLocation(int32 Index) const;
	bool IsTerritorySupplied(int32 Index) const;
	bool CanUseStrongholdTransit(int32 Index, EGuLiTeam Team) const;
	int32 FindTerritoryIndexById(FName TerritoryId) const;
	const FGuLiStrongholdTransportNetwork& GetTransportNetwork() const { return TransportNetwork; }
	FVector GetInitialBaseExit(EGuLiTeam Team) const;
	FGuLiTerritoryOwnershipChanged OnTerritoryOwnershipChanged;
	FGuLiControllableActorId AllocateControllableActorId() { return FGuLiControllableActorId(NextControllableActorId++); }
	AGuLiResourceFactoryActor* FindNearestFriendlyFactory(EGuLiTeam Team, const FVector& Location) const;
	void QueryFriendlyFactories(EGuLiTeam Team, const FVector& Location, TArray<AGuLiResourceFactoryActor*>& Out) const;
	void RegisterFactory(AGuLiResourceFactoryActor& Factory);
    bool RegisterHybridOutpost(AGuLiTerritoryOutpostActor& Outpost,int32 Index);
    void UnregisterHybridOutpost(AGuLiTerritoryOutpostActor& Outpost,int32 Index);
    void ApplyHybridControlPoint(const FGuLiHybridObjectState& State,bool Effects);
    FIntPoint GetHybridCaptureCounts(uint64 LogicalTick,int32 Territory);
    bool TryGetHybridCaptureCounts(uint64 LogicalTick,uint64 AfterTick,int32 Territory,FIntPoint& Out);
	FGuLiHybridObjectKey GetSettlementKey(const AActor* Actor) const;
	uint64 GetSettlementTick() const;
	FGuid SubmitResourceSettlement(const FGuLiHybridResourceRequest& Request);
	FGuLiResourceSettlement OnResourceSettlement;

	UFUNCTION(BlueprintPure, Category = "Resources|Territory")
	bool CanTeamMineAt(EGuLiTeam Team, int32 ClusterId) const;
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
	bool bHybridMiningSimulationSupported=false;
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
	bool bHybridQuerySceneReady = false;
	bool bHybridDirectoryDirty = true;
	double NavigationWaitStartSeconds = 0;
	// Runtime world copies only; restore fully asynchronous gathering when the startup gate opens.
	TArray<TWeakObjectPtr<ARecastNavMesh>> InitialSynchronousGatheringNavData;
	bool bOreFieldInitialized = false;
	FString InitializationError;
	uint32 LastAppliedWorldStateRevision = 0u;
	uint32 NextControllableActorId = 1000;
	bool bReplicatedStateDirty = true;
	FGuLiStrongholdTopology StrongholdTopology;
	TMap<EGuLiTeam,TArray<int32>> TransportRegions;
	FGuLiStrongholdTransportNetwork TransportNetwork;
	float CaptureAccumulator = 0;
    uint64 HybridCaptureTick=UINT64_MAX;
    uint32 HybridCaptureEpoch=0;
    TArray<FIntPoint> HybridCaptureCounts;
    uint64 BackgroundCaptureTick=0;
    uint32 BackgroundCaptureEpoch=0;
    TArray<FIntPoint> BackgroundCaptureCounts;
	float MaintenanceAccumulator = 0;
	float ActiveMaintenancePeriod = 0;
	FGuid MaintenanceAccount;
	uint32 MaintenanceRequestId = 0;

	void RefreshEncirclement();

	bool LoadAndValidateAssets();
	bool ValidateCurrentMap() const;

	bool RefreshHybridQueryScene();
	void HandleHybridResourceDirectory();
	void HandleHybridResourceNode(uint32 NodeId,uint8 Remaining);
	void RefreshHybridClusterObstacle(int32 Index);

	FVector ProjectAnchorToGround(const FVector& Anchor) const;
	void HandleWorldStateChanged();
	void HandleOreNodeChanged(uint32 NodeId, uint8 Remaining);
	void DisableDepletedCluster(uint16 ClusterId);
};
