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
    FGuid LastBurst;
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
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    void Apply(const FGuLiHybridObjectKey&,const GuLiNet::Buffs::State&,bool Bootstrap);
    void Remove(const FGuLiHybridObjectKey&);
private:
    void Stop(FGuLiBuffVisualSlot&);
    FGuLiPresentationSlotPool Pool;
    TMap<FGuLiHybridObjectKey,FGuLiPresentationSlotHandle> ByKey;
    UPROPERTY(Transient) TArray<FGuLiBuffVisualSlot> Slots;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Tails;
    uint32 Epoch=0;
};
