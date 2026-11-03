#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Network/GuLiHybridObjectKey.h"
#include "Gameplay/CombatEffects/GuLiPresentationSlotPool.h"
#include "GuLiNetBuffs.h"
#include "GuLiBuffPresentation.generated.h"
class UNiagaraComponent;
USTRUCT()
struct FGuLiBuffVisualSlot
{
    GENERATED_BODY()
    FGuLiPresentationSlotHandle Handle;
    FGuLiHybridObjectKey Key,Host;
    int32 Vfx=0;
    double Until=0,Offscreen=-1;
    UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
};
/** Reconstructable local visuals. No damage or buff timers run in this subsystem. */
UCLASS()
class GULISTRIKE_API UGuLiBuffPresentation final : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    void Apply(const FGuLiHybridObjectKey&,const GuLiNet::Buffs::State&,bool Bootstrap,bool SyncEvents=true);
    void Remove(const FGuLiHybridObjectKey&,bool ForgetEvents=false);
    FString GetDiagnostics() const;
private:
    void Stop(FGuLiBuffVisualSlot&);
    FGuLiPresentationSlotPool Pool;
    TMap<FGuLiHybridObjectKey,FGuLiPresentationSlotHandle> ByKey;
    UPROPERTY(Transient) TArray<FGuLiBuffVisualSlot> Slots;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Tails;
    uint32 Epoch=0;
    TSet<int32> PreparedVfx,MissingStatusVfx;
    // Read once from the prepared Niagara asset; the hot path never loads assets.
    TMap<int32,float> BurstLifetimes;
    // Event identity outlives the attached flame: final settlements may arrive after expiry.
    TMap<FGuLiHybridObjectKey,FGuid> LastBursts;
    uint64 BurstReceipts=0,BurstAccepted=0,BurstSuppressed=0,BurstRejected=0;
};
