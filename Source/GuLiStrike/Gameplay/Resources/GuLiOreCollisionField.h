#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavRelevantComponent.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "GuLiOreCollisionField.generated.h"

class UInstancedStaticMeshComponent;
class UGuLiResourceMapDefinition;
class UGuLiResourceEconomyConfig;

/** Hidden simple collision, grouped by initial ModelId. No per-node actors or navigation export. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiOreCollisionFieldActor final : public AActor
{
    GENERATED_BODY()
public:
    AGuLiOreCollisionFieldActor();
    bool InitializeNodes(const UGuLiResourceMapDefinition& Definition,const UGuLiResourceEconomyConfig& Config,
        TConstArrayView<uint8> Remaining,FString& OutError);
    bool RemoveNodes(TConstArrayView<uint32> NodeIds);
    bool RestoreNodes(TConstArrayView<uint32> NodeIds,const UGuLiResourceMapDefinition& Definition);
    int32 GetAliveCount() const { return AliveCount; }
    bool ValidateInstanceIndexMap() const;
private:
    struct FInstanceRef { int32 Component=INDEX_NONE; int32 Instance=INDEX_NONE; };
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> CollisionMeshes;
    TMap<int32,int32> ComponentByModel;
    TArray<FInstanceRef> NodeRefs;
    TArray<int32> InitialComponentByNode;
    TArray<TArray<uint32>> NodeIdsByInstance;
    int32 AliveCount=0;
};
