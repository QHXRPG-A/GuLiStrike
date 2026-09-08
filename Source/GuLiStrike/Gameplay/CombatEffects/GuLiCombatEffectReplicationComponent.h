#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "GuLiCombatEffectReplicationComponent.generated.h"

class UGuLiCombatEffectRuntimeSubsystem;

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

private:
	UFUNCTION() void OnRep_Epoch();
	UFUNCTION(NetMulticast, Reliable) void MulticastReliableStates(const TArray<FGuLiCombatEffectState>& States);
	/** Bounded one-Hz live snapshots: new peers rebuild without replaying old one-shots. */
	UFUNCTION(NetMulticast, Reliable) void MulticastActiveSnapshot(const TArray<FGuLiCombatEffectState>& States);
	UFUNCTION(NetMulticast, Unreliable) void MulticastCorrections(const TArray<FGuLiCombatEffectCorrection>& InCorrections);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShots(const TArray<FGuLiCombatShotCue>& Shots);
	void HandleState(const FGuLiCombatEffectState& State, bool bReliable);
	void HandleShots(const TArray<FGuLiCombatShotCue>& Shots);
	void HandleEpoch(uint32 NewEpoch);
	void ApplyStates(const TArray<FGuLiCombatEffectState>& States);

	UPROPERTY(ReplicatedUsing=OnRep_Epoch) uint32 Epoch = 0;
	TWeakObjectPtr<UGuLiCombatEffectRuntimeSubsystem> Runtime;
	TArray<FGuLiCombatEffectState> ReliableQueue;
	TMap<FGuid, FGuLiCombatEffectState> Corrections;
	TArray<FGuLiCombatShotCue> ShotQueue;
	TArray<FGuLiCombatEffectState> SnapshotQueue;
	int32 SnapshotCursor = 0;
	float SnapshotAccumulator = 0;
};
