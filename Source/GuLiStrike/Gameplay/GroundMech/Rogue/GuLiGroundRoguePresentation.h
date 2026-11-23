#pragma once

#include "Components/ActorComponent.h"
#include "Gameplay/GroundMech/Camera/GuLiGroundBarrageCameraPath.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "GuLiGroundRoguePresentation.generated.h"

struct FStreamableHandle;
class UAnimSequence;
class UNiagaraComponent;
class UGuLiRogueCardPresentation;

UCLASS()
class GULISTRIKE_API UGuLiGroundRoguePresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiGroundRoguePresentation();
    void SetCast(uint64 FirstShotTick);
    bool IsCasting() const { return CastFirstShotTick!=0; }
    void RecordShot(uint64 Tick, bool bLeft);
    void ClearShotRecoil() { LeftShotTick = RightShotTick = 0; }
    uint64 LatestShotTick() const { return FMath::Max(LeftShotTick, RightShotTick); }
    bool LatestShotWasLeft() const { return LeftShotTick > RightShotTick; }
    void ReadShotRecoil(FVector& Left, FVector& Right) const;
    void PlayUpgrade(const FString& CardId, int32 Rank);
    void ResetEffects();
    void ReadPose(UAnimSequence*& Sequence, float& Time, float& Weight, float Delta) const;
    bool ReadCastCamera(FGuLiGroundBarrageCameraShot& Shot) const;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
    struct FUpgrade
    {
        FString CardId;
        bool bUseMaxRankVfx = false;
        double Started = -1.;
        TWeakObjectPtr<UNiagaraComponent> Component;
        TWeakObjectPtr<UGuLiRogueCardPresentation> LocalSelection;
    };
    FString BarrageCardId;
    TMap<FString,FGuLiStrikeRogueCardsCardsRow> Rows;
    TArray<FUpgrade> Upgrades;
    TSharedPtr<FStreamableHandle> Assets;
    TWeakObjectPtr<UNiagaraComponent> Flash;
    TWeakObjectPtr<UNiagaraComponent> Lightning;
    FGuLiGroundBarrageCameraPath CameraPath;
    float LastChargeArcScale = 0.f;
    uint64 CastFirstShotTick = 0;
    uint64 LeftShotTick = 0, RightShotTick = 0;
    bool bFlashPassed = false;
    bool bCanPresent = false;
    bool CanShowBody() const;
    float CastAge(const FGuLiStrikeRogueCardsCardsRow& Row) const;
    void UpdateCast();
    bool UpdateUpgrade(FUpgrade& Cue, double Now);
    UNiagaraComponent* Spawn(const TSoftObjectPtr<UObject>& Asset, float Size, bool bAtFeet,
        float HeightMultiplier = 1.f, float PlaybackRate = 1.f);
    static void Release(TWeakObjectPtr<UNiagaraComponent>& Component);
};
