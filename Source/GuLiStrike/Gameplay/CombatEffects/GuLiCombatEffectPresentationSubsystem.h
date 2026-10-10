#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Commander/Presentation/GuLiCommanderLODPolicy.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Cards/GuLiRogueUpgradeTypes.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "Gameplay/CombatEffects/GuLiClientFlightPool.h"
#include "GuLiCombatEffectPresentationSubsystem.generated.h"

class UNiagaraComponent;
class UGuLiImpactBatchPresentation;
class UGuLiMuzzleBatchPresentation;

USTRUCT()
struct FGuLiLocalCombatEffect
{
	GENERATED_BODY()
	UPROPERTY() FGuLiCombatEffectState State;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Flight;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Waiting;
	UPROPERTY() TObjectPtr<UNiagaraComponent> ActiveLoop;
	UPROPERTY() TObjectPtr<class AGuLiFlightVisualActor> FlightActor;
	UPROPERTY() FGuLiFlightEvent FlightRecipe;
	UPROPERTY() TObjectPtr<UGuLiProjectileEffectDefinition> LoadedDefinition;
	UPROPERTY() TObjectPtr<UNiagaraSystem> LoadedFlightSystem;
	UPROPERTY() TObjectPtr<class UGuLiProjectileFlightPresentationProfile> LoadedFlightProfile;
	FGuLiClientFlightHandle FlightHandle;
	FGuLiCombatEffectState Prediction;
	bool bHasPrediction = false;
	bool bUsesMissileCluster = false;
	bool bFlightConfigurationPending = false;
	float NextFlightConfigurationRetry = 0;
	FVector RenderLocation = FVector::ZeroVector;
	FVector LaunchVisualOffset = FVector::ZeroVector;
	bool bLaunchVisualOffsetResolved = false;
	int32 NextGunShotOrdinal = 0;
	int32 LaserSlot = INDEX_NONE;
	float LaserMuzzleUntil = 0;
	float LaserFadeUntil = 0;
	bool bActivationPlayed = false;
	bool bSuppressOldBurst = false;
	EGuLiCommanderLODLevel FlightDetailLevel=EGuLiCommanderLODLevel::Full;
	double FlightDetailChangedAt=-1;
};

USTRUCT()
struct FGuLiRetiringCombatEffect
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	float ReleaseTime = 0.0f;
	FBox Bounds=FBox(ForceInit);
	float OffscreenSince=-1;
	int32 VfxId=0;
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
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 ImpactActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 ImpactComponents = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 ImpactBatchPublished = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 ImpactBatchFallbacks = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 MuzzleActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 MuzzleComponents = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleAccepted = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleBorn = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleDuplicates = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleExpired = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleOffscreenRecycled = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleBatchPublished = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleBatchFallbacks = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzlePoseQueries = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzleLifeUploads = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 MuzzlePoseUploads = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 WingmanFlightActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 WingmanFlightComponents = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 WingmanFlightUploads = 0;
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
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 ClientFlightDataCapacity = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int32 ClientFlightDataActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 ClientFlightDataReuses = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 ClientFlightDataReleases = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 ClientFlightDataEpoch = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 PoseCacheHits = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 PoseCacheMisses = 0;
	UPROPERTY(BlueprintReadOnly, Category="Combat Effects") int64 NiagaraArrayUploads = 0;
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
	float OffscreenSince=-1;
	FBox EmissionBounds = FBox(ForceInit);
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

struct FGuLiLaserUploadedRow
{
	FVector Position=FVector::ZeroVector, Direction=FVector::ForwardVector, MuzzlePosition=FVector::ZeroVector, LightPosition=FVector::ZeroVector;
	FVector2D Size=FVector2D::ZeroVector, MuzzleSize=FVector2D::ZeroVector;
	FLinearColor Color=FLinearColor::Transparent, MuzzleColor=FLinearColor::Transparent, LightColor=FLinearColor::Transparent;
	float LightRadius = 0;
	bool bLight = false;
	bool operator==(const FGuLiLaserUploadedRow& R) const
	{ return Position==R.Position && Direction==R.Direction && MuzzlePosition==R.MuzzlePosition && LightPosition==R.LightPosition
		&& Size==R.Size && MuzzleSize==R.MuzzleSize && Color==R.Color && MuzzleColor==R.MuzzleColor && LightColor==R.LightColor
		&& LightRadius==R.LightRadius && bLight==R.bLight; }
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
	TArray<int32> UsedSlots, PreviousUsedSlots;
	TArray<FGuLiLaserUploadedRow> UploadedRows;
	FBox UploadedBounds = FBox(ForceInit);
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
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool SetReviewImpactChannel(class UNiagaraDataChannelAsset* Channel);
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool SetReviewMuzzleChannel(class UNiagaraDataChannelAsset* Channel);
	/** Client-only visual input for the saved opt-in comparison; no weapon or replication events. */
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool EmitReviewMuzzleInput(int32 Identity,FVector Location,FRotator Rotation,bool Heavy=false,int32 Mode=2,FVector FollowVelocity=FVector::ZeroVector);
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool ResetReviewMuzzles();
	/** Exercise loss of a cosmetic pose provider, only in an explicitly started PIE review. */
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool RemoveReviewMuzzleSource(int32 Identity);
	/** Client-local epoch regression. Never updates authoritative or replicated battle state. */
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool AdvanceReviewEffectEpoch();
	UFUNCTION(BlueprintPure,Category="Combat Effects|Acceptance")
	FString GetMuzzleProtocolSnapshot() const;
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool SetReviewFlightProfile(UGuLiProjectileEffectDefinition* Definition,class UGuLiProjectileFlightPresentationProfile* Profile);
	/** Opt-in client-only catalog acceptance fixture; never creates gameplay or network events. */
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	int32 EmitReviewImpacts(FVector Location,int32 Count=1,int32 Seed=1);
	/** Exercise the saved batch protocol with a stable terminal identity and explicit inputs. PIE only. */
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool EmitReviewImpactInput(int32 Identity,FVector Location,FRotator Rotation,FVector Scale,FLinearColor Tint,
		int32 Seed=1,float Lifetime=3.f,int32 ReviewEpoch=1,int32 TerminalSequence=1);
	UFUNCTION(BlueprintCallable,Category="Combat Effects|Acceptance")
	bool ResetReviewImpacts();
#endif
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
	UPROPERTY(Transient) TObjectPtr<UGuLiImpactBatchPresentation> ImpactBatches;
	UPROPERTY(Transient) TObjectPtr<UGuLiMuzzleBatchPresentation> MuzzleBatches;
#if WITH_EDITOR
	struct FReviewMuzzlePose { FTransform Pose; FVector Velocity; double Start=0; };
	TMap<FGuid,FReviewMuzzlePose> ReviewMuzzlePoses;
#endif
	UPROPERTY(Transient) TObjectPtr<class UGuLiWingmanProjectilePresentation> WingmanFlights;
	UPROPERTY(Transient) TMap<TObjectPtr<UGuLiProjectileEffectDefinition>,TObjectPtr<class UGuLiProjectileFlightPresentationProfile>> ReviewFlightProfiles;
	void ResolveFlightConfiguration(FGuLiLocalCombatEffect& Visual);
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
	bool IsVisibleBounds(const FBox& Bounds) const;
	bool IsVisibleSystemBounds(UNiagaraSystem* System, const FTransform& Transform) const;
	bool GetSystemWorldBounds(UNiagaraSystem* System, const FTransform& Transform, FBox& OutBounds) const;
	TMap<TWeakObjectPtr<UNiagaraComponent>,int32> SpawnedEffectIds;
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
	int32 LaserBlockSize = 256;
	UPROPERTY(Transient) TArray<FGuLiRogueUpgradeBlock> UpgradeBlocks;
	TMap<FGuLiSoldierId,int32> UpgradeSlots;
	struct FUpgradeReceipt { float Expires=0.f; TSet<uint16> Batches; };
	TMap<FGuid,FUpgradeReceipt> UpgradeReceipts;
	TWeakObjectPtr<class AGuLiSoldierStateReplicator> UpgradeRoster;
	TMap<int32, TArray<int32>> FreeLaserSlots;
	TSet<FGuid> LaserVisualIds;
	TMap<EGuLiTargetKind, FPoseProvider> PoseProviders;
	TMap<EGuLiTargetKind, FMuzzleProvider> MuzzleProviders;
	mutable TMap<FGuLiTargetHandle, TWeakObjectPtr<AActor>> ShipPoseCache;
	struct FResolvedPose { FTransform Transform; int32 UnitTypeId = 0; bool bSuccess = false; };
	struct FMuzzleCacheKey
	{
		FGuLiCombatShotCue Cue;
		friend uint32 GetTypeHash(const FMuzzleCacheKey& K)
		{ return HashCombine(GetTypeHash(K.Cue.Source), HashCombine(GetTypeHash(K.Cue.ShotId), GetTypeHash(K.Cue.SlotId))); }
		friend bool operator==(const FMuzzleCacheKey& A, const FMuzzleCacheKey& B)
		{
			const auto& L = A.Cue; const auto& R = B.Cue;
			return L.MatchEpoch == R.MatchEpoch && L.Source == R.Source && L.Target == R.Target
				&& L.ShotId == R.ShotId && L.SlotId == R.SlotId && L.MuzzleIndex == R.MuzzleIndex
				&& L.UnitTypeId == R.UnitTypeId && L.ServerTime == R.ServerTime
				&& L.MechanicalPoseTimeSeconds == R.MechanicalPoseTimeSeconds && L.RecoilFromCentimeters == R.RecoilFromCentimeters
				&& L.bMechanicalShot == R.bMechanicalShot && L.MuzzleOffset == R.MuzzleOffset
				&& L.MuzzleDirection == R.MuzzleDirection && L.Start == R.Start && L.End == R.End;
		}
	};
	struct FResolvedMuzzle { FTransform Transform; float RenderTime = 0; bool bSuccess = false; };
	mutable TMap<FGuLiTargetHandle, FResolvedPose> BatchPoses;
	mutable TMap<FMuzzleCacheKey, FResolvedMuzzle> BatchMuzzles;
	bool bResolvingPresentationBatch = false;
	FGuLiClientFlightPool ClientFlights;
	TMap<FGuid, uint32> Tombstones;
	/** IDs only; projectile Visuals own the frozen launch payload and lifetime. */
	TMap<FGuid, TSet<FGuid>> GuidanceMembers;
	TArray<FGuid> TombstoneOrder;
	TSet<FGuid> SeenShots;
	TArray<FGuid> ShotOrder;
	TArray<FGuLiCombatShotCue> PendingShots;
	TMap<FActiveMuzzleKey, FActiveMuzzleVisual> ActiveMuzzles;
	mutable FGuLiCombatEffectVisualCounters Counters;
	FBox GunfireBounds = FBox(ForceInit);
	FBox PreviousGunfireBounds = FBox(ForceInit);
	float GunfireBoundsResetTime = 0;
	float NextMuzzleRefreshTime = 0;
	uint32 Epoch = 0;
	TSet<uint32> RetiredEpochs;
	bool bChannelWarning = false;
};
