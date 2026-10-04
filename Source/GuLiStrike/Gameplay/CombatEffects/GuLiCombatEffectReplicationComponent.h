#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "Gameplay/Cards/GuLiRogueUpgradeTypes.h"
#include "GuLiCombatEffectReplicationComponent.generated.h"

class UGuLiCombatEffectRuntimeSubsystem;

USTRUCT()
struct FGuLiWingmanFeedbackCue
{
	GENERATED_BODY()
	UPROPERTY() FGuLiWingmanHandle Wingman;
	UPROPERTY() FVector_NetQuantize Location = FVector::ZeroVector;
	UPROPERTY() float ServerTime = 0;
	UPROPERTY() bool bDestroyed = false;
	/** Confirmed remaining health for the transient hit bar, not client-authoritative gameplay state. */
	UPROPERTY() uint16 HealthPermille = 1000;
};

/** Public GameState component. No OwnerOnly data and no client-to-server damage RPC. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiCombatEffectReplicationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiCombatEffectReplicationComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack) override;
	/** Authority-originated cosmetics; no damage or health is accepted from clients. */
	static void PublishWingmanFeedback(UWorld* World, const FGuLiWingmanHandle& Wingman, const FVector& Location, bool bDestroyed, uint16 HealthPermille);
	static void PublishRogueUpgrade(UWorld* World,const FGuLiRogueUpgradeCue& Cue);
	static bool PublishFlight(UWorld* World, const FGuLiFlightEvent& Event);
	/** Authority state refresh for one-time join bootstrap only; never sends an update. */
	static void UpdateFlight(UWorld* World, const FGuLiCombatEffectState& State);
	static void AttachFlightMuzzle(UWorld* World, const FGuLiCombatShotCue& Cue);
	static bool IsFlightActive(UWorld* World, const FGuid& Id);
	UFUNCTION(BlueprintPure, Category="Network|Flight") FString GetFlightDiagnostics() const;

private:
	UFUNCTION(NetMulticast, Reliable) void MulticastRogueUpgrade(const FGuLiRogueUpgradeCue& Cue);
	UFUNCTION() void OnRep_Epoch();
	UFUNCTION(NetMulticast, Reliable) void MulticastReliableStates(const TArray<FGuLiCombatEffectState>& States);
	/** Non-flight fields are reconstructed once on connection establishment. */
	UFUNCTION(NetMulticast, Reliable) void MulticastActiveSnapshot(const TArray<FGuLiCombatEffectState>& States);
	UFUNCTION(NetMulticast, Reliable) void MulticastFlightBatch(const TArray<uint8>& Payload);
	UFUNCTION(NetMulticast, Unreliable) void MulticastWingmanFeedback(const TArray<FGuLiWingmanFeedbackCue>& Cues);
	void HandleState(const FGuLiCombatEffectState& State, bool bReliable);
	void HandleShots(const TArray<FGuLiCombatShotCue>& Shots);
	void HandleEpoch(uint32 NewEpoch);
	bool IsCurrentEpoch(uint32 MatchEpoch) const;
	void ApplyStates(const TArray<FGuLiCombatEffectState>& States);
	void FlushFlightStreams();
	void SendToConnection(class UNetConnection* Connection, UFunction* Function, void* Parameters);

	UPROPERTY(ReplicatedUsing=OnRep_Epoch) uint32 Epoch = 0;
	TWeakObjectPtr<UGuLiCombatEffectRuntimeSubsystem> Runtime;
	TArray<FGuLiCombatEffectState> ReliableQueue;
	TArray<FGuLiWingmanFeedbackCue> WingmanFeedbackQueue;
	UPROPERTY(Transient) TMap<FGuid, FGuLiFlightEvent> ActiveFlights;
	UPROPERTY(Transient) TArray<FGuLiFlightEvent> PendingFlights;
	struct FPeerStream { TWeakObjectPtr<class UActorChannel> Channel; TArray<FGuLiFlightEvent> Queue; int32 Cursor = 0; };
	TMap<TWeakObjectPtr<class UNetConnection>, FPeerStream> FlightPeers;
	uint64 CreatedFlights = 0, EndedFlights = 0, SentFlightBytes = 0, SentFlightBatches = 0, BootstrapFlights = 0;
	double NextFlightDiagnosticTime = 0;
};
