#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "GuLiCombatEffectReplicationComponent.generated.h"

/** Local query/presentation registry. Stable reflected class path; transport belongs to the session. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiCombatEffectReplicationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiCombatEffectReplicationComponent();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** Apply an authenticated owner state without publishing it again. */
    static bool ApplyHybridFlight(UWorld* World,const FGuLiFlightEvent& Event,bool Retired=false);
    /** Map an owner's object clock to this world's presentation clock, preserving durations. */
    static void RebaseReceivedState(UWorld* World,uint64 OwnerTick,FGuLiCombatEffectState& State);
    static void RebasePublishedState(UWorld* World,uint64 ObjectTick,FGuLiCombatEffectState& State);
    static void UpdateFlight(UWorld* World,const FGuLiCombatEffectState& State);
    static void AttachFlightMuzzle(UWorld* World,const FGuLiCombatShotCue& Cue);
    static bool IsFlightActive(UWorld* World,const FGuid& Id);
    UFUNCTION(BlueprintPure,Category="Network|Flight") FString GetFlightDiagnostics() const;
    uint64 GetObservedFlightCount() const {return ObservedFlights;}
    uint64 GetFinishedFlightCount() const {return FinishedFlights;}
    int32 GetActiveFlightCount() const;
    int32 GetAdditionalActiveFlightCount() const;
    bool QueryFlight(const FGuid& Id,FGuLiCombatEffectState& Out) const;
    void AppendAdditionalFlightSnapshot(TArray<FGuLiCombatEffectState>& Out) const;
    const TMap<FGuid,FIntPoint>& GetAdditionalFlightReceipts() const {return AdditionalReceipts;}
private:
    void HandleEpoch(uint32 NewEpoch);
    bool IsCurrentEpoch(uint32 MatchEpoch) const;
    UPROPERTY() uint32 Epoch=0;
    UPROPERTY(Transient) TMap<FGuid,FGuLiFlightEvent> ActiveFlights;
    uint64 ObservedFlights=0,FinishedFlights=0;
    bool bReviewReceipts=false;
    int32 NextReceipt=0;
    TArray<FGuid> ReceiptOrder;
    TMap<FGuid,FIntPoint> AdditionalReceipts;
};
