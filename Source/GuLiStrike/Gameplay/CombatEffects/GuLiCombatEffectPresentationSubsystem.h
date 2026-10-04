#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Cards/GuLiRogueUpgradeTypes.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "GuLiCombatEffectPresentationSubsystem.generated.h"

class UNiagaraComponent;

USTRUCT()
struct FGuLiLocalCombatEffect
{
	GENERATED_BODY()
	UPROPERTY() FGuLiCombatEffectState State;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Flight;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Waiting;
	UPROPERTY() TObjectPtr<UNiagaraComponent> ActiveLoop;
	UPROPERTY() TObjectPtr<class AGuLiFlightVisualActor> FlightActor;
	FVector RenderLocation = FVector::ZeroVector;
	FVector LaunchVisualOffset = FVector::ZeroVector;
	bool bLaunchVisualOffsetResolved = false;
	int32 NextGunShotOrdinal = 0;
	int32 LaserSlot = INDEX_NONE;
	float LaserMuzzleUntil = 0;
	float LaserFadeUntil = 0;
	bool bActivationPlayed = false;
	bool bSuppressOldBurst = false;
};

USTRUCT()
struct FGuLiRetiringCombatEffect
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	float ReleaseTime = 0.0f;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCombatEffectVisualCounters
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 ReceivedShots = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 WrittenShots = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 DroppedShots = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 ReceivedStates = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 RejectedStates = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 BurstsPlayed = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MachineGunImpactsPlayed = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 SynthesizedGunShots = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 ComponentCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 LaserActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 LaserCapacity = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 LaserVisible = 0;
	/** Ground projectile light slots uploaded this frame, capped across all render blocks. */
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 GroundMachineGunLights = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") double LastUpdateMilliseconds = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 ClientFlightActorCapacity = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 ClientFlightActorActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 UpgradeActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 UpgradeVisible = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 UpgradeComponents = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 UpgradeVisibleParticles = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") double UpgradeUpdateMilliseconds = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 MissileClusterComponents = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 MissileParticleCapacity = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 MissileFullTrails = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") double MissileClusterUpdateMilliseconds = 0;
};

/** One accepted shot, played when the interpolated unit reaches its shot time. */
USTRUCT()
struct FGuLiMechanicalMuzzleVisual
{
	GENERATED_BODY()
	UPROPERTY() FGuLiCombatShotCue Cue;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	bool bStarted = false;
};

struct FGuLiRogueUpgradeSlot
{
	FGuLiSoldierId Soldier;
	EGuLiTeam Team=EGuLiTeam::Unassigned;
	uint16 UnitTypeId=0;
	float StartTime=0.f, Scale=1.f;
	FLinearColor Color=FLinearColor::Transparent;
	bool bActive=false;
};

USTRUCT()
struct FGuLiRogueUpgradeBlock
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	UPROPERTY() TObjectPtr<UNiagaraSystem> System;
	TArray<FGuLiRogueUpgradeSlot> Slots;
	TArray<int32> Free;
	TArray<FVector> Positions, Parameters;
	TArray<FLinearColor> Colors;
};

/** A fixed block of persistent particle slots. Empty rows have zero alpha. */
USTRUCT()
struct FGuLiLaserRenderBlock
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	int32 VfxId = 0;
	FVector BaseScale = FVector::OneVector;
	TArray<FVector> Positions, Directions, MuzzlePositions;
	TArray<FVector2D> Sizes, MuzzleSizes;
	TArray<FLinearColor> Colors, MuzzleColors;
	TArray<FVector> LightPositions;
	TArray<FLinearColor> LightColors;
	TArray<float> LightRadii;
	TArray<bool> LightEnabled;
	int32 LightCount = 0;
	FBox Bounds = FBox(ForceInit);
	bool bVisible = false;
};

/** Render-client only. Game code supplies stable-handle pose resolvers without reverse Mass dependencies. */
UCLASS()
class GULISTRIKE_API UGuLiCombatEffectPresentationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	void BeginEpoch(uint32 NewEpoch);
	void ApplyState(const FGuLiCombatEffectState& State, bool bFromSnapshot = false);
	void ApplyFlightEvent(const FGuLiFlightEvent& Event);
	void ApplyCorrection(const FGuLiCombatEffectCorrection& Correction);
	void ApplyShots(const TArray<FGuLiCombatShotCue>& Cues);
	void ApplyRogueUpgrade(const FGuLiRogueUpgradeCue& Cue);
	using FPoseResolver = TFunction<bool(const FGuLiTargetHandle&, FTransform&, int32&)>;
	void RegisterPoseResolver(EGuLiTargetKind Kind, UObject* Owner, FPoseResolver Resolver);
	void UnregisterPoseResolver(EGuLiTargetKind Kind, const UObject* Owner);
	using FMuzzleResolver = TFunction<bool(const FGuLiCombatShotCue&, FTransform&, float&)>;
	using FShotObserver = TFunction<void(const FGuLiCombatShotCue&)>;
	using FLaunchOffsetResolver = TFunction<bool(const FGuLiTargetHandle&, FName, const FVector&, FVector&)>;
	void RegisterMuzzleResolver(EGuLiTargetKind Kind, UObject* Owner, FMuzzleResolver Resolver,
		FShotObserver Observer = {}, FLaunchOffsetResolver LaunchOffset = {});
	void UnregisterMuzzleResolver(EGuLiTargetKind Kind, const UObject* Owner);
	/** Existing accepted shot cues supply cosmetic aim; no authority or network state is added. */
	bool TryGetWeaponAim(const FGuLiTargetHandle& Source, FName SlotId, FVector& Target) const;

	UFUNCTION(BlueprintPure, Category="Combat Effects") FGuLiCombatEffectVisualCounters GetCounters() const;
	UFUNCTION(BlueprintPure, Category="Combat Effects") TArray<FGuLiCombatEffectState> GetEffectStates() const;
	UFUNCTION(BlueprintPure, Category="Combat Effects") int32 GetActiveVisualCount() const { return Visuals.Num(); }

private:
	friend class UGuLiRogueCardQALibrary;
	struct FPoseProvider { TWeakObjectPtr<UObject> Owner; FPoseResolver Resolve; };
	struct FMuzzleProvider { TWeakObjectPtr<UObject> Owner; FMuzzleResolver Resolve; FShotObserver Observe; FLaunchOffsetResolver LaunchOffset; };
	struct FActiveMuzzleKey
	{
		FGuLiTargetHandle Source;
		FName SlotId;
		uint8 MuzzleIndex = 0;
		friend bool operator==(const FActiveMuzzleKey& Lhs, const FActiveMuzzleKey& Rhs)
		{ return Lhs.Source == Rhs.Source && Lhs.SlotId == Rhs.SlotId && Lhs.MuzzleIndex == Rhs.MuzzleIndex; }
		friend uint32 GetTypeHash(const FActiveMuzzleKey& Key)
		{ return HashCombine(GetTypeHash(Key.Source), HashCombine(GetTypeHash(Key.SlotId), static_cast<uint32>(Key.MuzzleIndex))); }
	};
	struct FActiveMuzzleVisual
	{
		FGuLiCombatShotCue Cue;
		FVector LastDirection = FVector::ForwardVector;
		float ExpireServerTime = 0.0f;
	};
	float ServerTime() const;
	UGuLiCombatEffectCatalog* GetCatalog();
	UNiagaraComponent* SpawnPooled(int32 VfxId, FVector Location, float DynamicScale,
		float Radius = 0, FRotator Rotation = FRotator::ZeroRotator, FName ScaleParameterName = NAME_None);
	void Retire(UNiagaraComponent* Component, float Seconds, bool bDeactivate = true);
	void RemoveVisual(const FGuid& Id, bool bImmediate);
	void QueueSustainedGunfire(float Now, bool bEnabled);
	void FlushGunfire();
	int32 AllocateLaserSlot(int32 VfxId);
	void FreeLaserSlot(int32 Slot);
	void UpdateLaserPool(float Now, bool bEnabled);
	void ResetLaserPool();
	void PlayMachineGunImpact(const FGuLiCombatEffectState& State);
	void UpdateRogueUpgradePool(float Now,bool bEnabled);
	void ResetRogueUpgradePool();
	void UpdateMechanicalMuzzles(float Now, bool bEnabled);
	void ResetMechanicalMuzzles();
	FVector EvaluateLaunchVisualOffset(FGuLiLocalCombatEffect& Visual, FName Slot, float RenderTime);
	bool ResolvePose(const FGuLiTargetHandle& Target, FTransform& Transform, int32& UnitTypeId) const;
	bool ResolveMuzzleTransform(const FGuLiCombatShotCue& Cue, FTransform& Transform, float& RenderTime) const;
	bool ResolveMuzzlePosition(const FGuLiCombatShotCue& Cue, FVector& Position) const;
	bool ResolveTargetPosition(const FGuLiCombatShotCue& Cue, FVector& Position) const;
	void ResolveShotEndpoints(const FGuLiCombatShotCue& Cue, FVector& Start, FVector& End) const;
	double ClosestLocalCameraDistanceSquared(FVector Location) const;
	bool IsVisibleLocation(FVector Location) const;
	void UpdateField(FGuLiLocalCombatEffect& Visual, float Now);
	void UpdateGroundWarning(const FGuLiCombatEffectState& State, bool bEnabled);
	void RemoveGuidanceMember(const FGuLiCombatEffectState& State);
	void RefreshGuidanceWarning(const FGuid& BatchId, bool bEnabled);
	void ResetVisuals();
	AGuLiFlightVisualActor* AcquireFlightActor(const FGuLiFlightEvent& Event);
	void ReleaseFlightActor(AGuLiFlightVisualActor* Actor);

	UPROPERTY(Transient) TObjectPtr<UGuLiCombatEffectCatalog> Catalog;
	UPROPERTY(Transient) TObjectPtr<class UGuLiCommanderDataSubsystem> CommanderData;
	UPROPERTY(Transient) TObjectPtr<class UGuLiGroundWarningSubsystem> GroundWarnings;
	UPROPERTY(Transient) TObjectPtr<class UGuLiMissileClusterPresentation> MissileClusters;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Gunfire;
	UPROPERTY(Transient) TMap<FGuid, FGuLiLocalCombatEffect> Visuals;
	UPROPERTY(Transient) TArray<TObjectPtr<AGuLiFlightVisualActor>> FlightActors;
	UPROPERTY(Transient) TArray<TObjectPtr<AGuLiFlightVisualActor>> FreeFlightActors;
	UPROPERTY(Transient) TArray<FGuLiRetiringCombatEffect> Retiring;
	UPROPERTY(Transient) TArray<FGuLiMechanicalMuzzleVisual> MechanicalMuzzles;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> LoadedMechanicalMuzzleSystem;
	UPROPERTY(Transient) TArray<FGuLiLaserRenderBlock> LaserBlocks;
	UPROPERTY(Transient) TArray<FGuLiRogueUpgradeBlock> UpgradeBlocks;
	TMap<FGuLiSoldierId,int32> UpgradeSlots;
	struct FUpgradeReceipt { float Expires=0.f; TSet<uint16> Batches; };
	TMap<FGuid,FUpgradeReceipt> UpgradeReceipts;
	TWeakObjectPtr<class AGuLiSoldierStateReplicator> UpgradeRoster;
	TMap<int32, TArray<int32>> FreeLaserSlots;
	TMap<EGuLiTargetKind, FPoseProvider> PoseProviders;
	TMap<EGuLiTargetKind, FMuzzleProvider> MuzzleProviders;
	mutable TMap<FGuLiTargetHandle, TWeakObjectPtr<AActor>> ShipPoseCache;
	TMap<FGuid, uint32> Tombstones;
	/** IDs only; projectile Visuals own the frozen launch payload and lifetime. */
	TMap<FGuid, TSet<FGuid>> GuidanceMembers;
	TArray<FGuid> TombstoneOrder;
	TSet<FGuid> SeenShots;
	TArray<FGuid> ShotOrder;
	TArray<FGuLiCombatShotCue> PendingShots;
	TMap<FActiveMuzzleKey, FActiveMuzzleVisual> ActiveMuzzles;
	FGuLiCombatEffectVisualCounters Counters;
	FBox GunfireBounds = FBox(ForceInit);
	FBox PreviousGunfireBounds = FBox(ForceInit);
	float GunfireBoundsResetTime = 0;
	float NextMuzzleRefreshTime = 0;
	uint32 Epoch = 0;
	TSet<uint32> RetiredEpochs;
	bool bChannelWarning = false;
};
