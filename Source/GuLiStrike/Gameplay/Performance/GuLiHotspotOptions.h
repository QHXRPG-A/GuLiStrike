#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiHotspotOptions.generated.h"

class FJsonObject;

enum class EGuLiHotspot : uint32
{
    HUDCache=1, SceneUICache=2, AuxiliaryPosition=4, ComponentCache=8, TeleportIdle=16,
    GameplayWorker=32, // 64 and 128 are retired; synchronization has one implementation.
    ParallelGameplay=256
};

/** Local development controls, latched once per world. Never serialized into gameplay state. */
UCLASS()
class GULISTRIKE_API UGuLiHotspotOptions final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    uint32 GetMask() const { return Mask; }
    uint32 GetGameplayWorkers() const { return Workers; }
    bool Has(EGuLiHotspot Option) const { return (Mask & uint32(Option))!=0; }
    static bool Enabled(const UWorld* World,EGuLiHotspot Option);
    static uint32 ResolveLaunchMask();
    static uint32 ResolveGameplayWorkers();
    TSharedRef<FJsonObject> GetSyncIdentity() const;
    // Game-thread counters; consumers take start/end deltas outside timed windows.
    uint64 ContractRebuilds=0, BindingRebuilds=0, BindingHits=0, PublicCaptures=0, PublicAbsent=0;
    uint64 BodyDecodes=0, BodyDecodeHits=0, BodyCallbacks=0, BodyApplications=0;
private:
    uint32 Mask=0;
    uint32 Workers=2;
    TSharedPtr<FJsonObject> SyncIdentity;
};
