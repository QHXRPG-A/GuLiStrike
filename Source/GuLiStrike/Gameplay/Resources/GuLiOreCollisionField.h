#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavRelevantComponent.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "GuLiOreCollisionField.generated.h"

class UInstancedStaticMeshComponent;
class UGuLiResourceMapDefinition;
class UGuLiResourceEconomyConfig;

/** One navigation exporter per cluster; the cluster bounds never become a solid modifier. */
UCLASS()
class GULISTRIKE_API UGuLiOreNavigationComponent final : public UNavRelevantComponent
{
    GENERATED_BODY()
public:
    UGuLiOreNavigationComponent();
    void InitializeNodes(TConstArrayView<FGuLiOreNodeObstacle> InNodes,TConstArrayView<uint8> Remaining);
    void RemoveNodes(TConstArrayView<uint32> NodeIds);
    void SetNodesEnabled(bool bEnabled);
    int32 GetAliveCount() const;
    virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
    virtual void CalcAndCacheBounds() const override;
private:
    TArray<FGuLiOreNodeObstacle> Nodes;
    uint32 AliveMask=0;
    void Publish();
};

/** Hidden simple collision, grouped by initial ModelId. No per-node actors or navigation export. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiOreCollisionFieldActor final : public AActor
{
    GENERATED_BODY()
public:
    AGuLiOreCollisionFieldActor();
    bool InitializeNodes(const UGuLiResourceMapDefinition& Definition,const UGuLiResourceEconomyConfig& Config,
        TConstArrayView<uint8> Remaining,TArray<FGuLiOreNodeObstacle>& OutShapes,FString& OutError);
    bool RemoveNodes(TConstArrayView<uint32> NodeIds);
    int32 GetAliveCount() const { return AliveCount; }
    bool ValidateInstanceIndexMap() const;
private:
    struct FInstanceRef { int32 Component=INDEX_NONE; int32 Instance=INDEX_NONE; };
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> CollisionMeshes;
    TMap<int32,int32> ComponentByModel;
    TArray<FInstanceRef> NodeRefs;
    TArray<TArray<uint32>> NodeIdsByInstance;
    int32 AliveCount=0;
};
