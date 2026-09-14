// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "GuLiResourceActors.generated.h"

class AGuLiResourceWorldState;
class UBoxComponent;
class UChildActorComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UNavModifierComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UGuLiResourceEconomyConfig;
class UGuLiResourceMapDefinition;
class UGuLiResourceWorldSubsystem;

/** One renderer for all 6240 logical nodes. It owns exactly 24 HISM components. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiOreFieldActor final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiOreFieldActor();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool InitializeField(
		const UGuLiResourceMapDefinition& InDefinition,
		const UGuLiResourceEconomyConfig& InConfig,
		AGuLiResourceWorldState& InWorldState,
		FString& OutError);
	void ApplyReplicatedState();
	void ApplyNodeAmount(uint32 NodeId, uint8 RemainingAmount);
	UFUNCTION(BlueprintPure, Category = "Resources|Rendering")
	int32 GetHISMComponentCount() const { return OreMeshes.Num(); }
	UFUNCTION(BlueprintPure, Category = "Resources|Rendering")
	int32 GetVisibleNodeCount() const;
	bool ValidateInstanceIndexMap(FString* OutError = nullptr) const;

private:
	struct FVisualInstanceRef
	{
		int32 ComponentIndex = INDEX_NONE;
		int32 InstanceIndex = INDEX_NONE;
	};

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> OreMeshes;

	TWeakObjectPtr<const UGuLiResourceMapDefinition> Definition;
	TWeakObjectPtr<AGuLiResourceWorldState> WorldState;
	TArray<uint8> AppliedNodeAmounts;
	TArray<FVisualInstanceRef> VisualRefs;
	TArray<TArray<uint32>> NodeIdsByComponentInstance;
	FDelegateHandle WorldStateChangedHandle;

	static int32 MakeComponentIndex(EGuLiResourceType Type, uint8 FamilyIndex, EGuLiOreVisualStage Stage);
	void RemoveVisual(uint32 NodeId);
	void AddVisual(uint32 NodeId, uint8 RemainingAmount);
};

/** Server-side collision, Recast NavArea_Null and Mass avoidance proxy for one whole cluster. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiOreClusterObstacleActor final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiOreClusterObstacleActor();
	void InitializeObstacle(uint16 InClusterId, float RadiusCentimeters);
	void SetObstacleEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "Resources|Navigation")
	int32 GetClusterId() const { return ClusterId; }
	UFUNCTION(BlueprintPure, Category = "Resources|Navigation")
	float GetObstacleRadius() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Navigation")
	bool IsObstacleEnabled() const { return bObstacleEnabled; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNavModifierComponent> NavModifier;

	uint16 ClusterId = 0u;
	bool bObstacleEnabled = true;
};

/** Public whitebox board landmark; owner color is replicated to both teams. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiTerritoryOutpostActor final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiTerritoryOutpostActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeOutpost(uint8 InTerritoryIndex, FName InTerritoryId, EGuLiTeam InOwner);
	void SetTerritoryOwnerAuthority(EGuLiTeam InOwner);
	uint8 GetTerritoryIndex() const { return TerritoryIndex; }
	EGuLiTeam GetTerritoryOwner() const { return TerritoryOwner; }

private:
	UFUNCTION()
	void OnRep_TerritoryOwner();
	void ApplyOwnerColor();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LandmarkMesh;

	UPROPERTY(Replicated)
	uint8 TerritoryIndex = 0u;

	UPROPERTY(Replicated)
	FName TerritoryId;

	UPROPERTY(ReplicatedUsing = OnRep_TerritoryOwner)
	EGuLiTeam TerritoryOwner = EGuLiTeam::Unassigned;
};

