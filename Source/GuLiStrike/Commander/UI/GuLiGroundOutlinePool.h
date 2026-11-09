#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiHybridObjectKey.h"
#include "GuLiGroundOutlinePool.generated.h"

class APlayerController;
class UInstancedStaticMeshComponent;
class UMeshComponent;
class UMaterialInterface;
class USceneCaptureComponent2D;
struct FGuLiGroundOutlineStorage;

/** Capture-only geometry. Original mesh materials, overlays and LOD are never modified. */
UCLASS()
class GULISTRIKE_API UGuLiGroundOutlinePool final : public UObject
{
    GENERATED_BODY()
public:
    UGuLiGroundOutlinePool();
    virtual ~UGuLiGroundOutlinePool() override;
    void Initialize(APlayerController* Controller);
    void Shutdown();
    void Begin(USceneCaptureComponent2D& Capture);
    void SubmitInstance(const FGuLiHybridObjectKey& Key,UInstancedStaticMeshComponent& Source,int32 Index,float Opacity);
    void SubmitActor(const FGuLiHybridObjectKey& Key,AActor& Source,float Opacity);
    void End();
    FString GetStatsJson() const;
private:
    void Configure(UMeshComponent& Component);
    bool SyncMaterials(UMeshComponent& Source,UMeshComponent& Proxy,bool Instanced,float Opacity);
    UPROPERTY(Transient) TObjectPtr<AActor> Owner;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UMaterialInterface>> Masks;
    TSharedPtr<FGuLiGroundOutlineStorage> Storage;
    TWeakObjectPtr<USceneCaptureComponent2D> ActiveCapture;
    TSet<FName> MissingMaterials;
    uint64 Frame=0,TransformWrites=0,CustomDataWrites=0;
    int32 ActiveInstances=0,ActiveActorMeshes=0;
};
