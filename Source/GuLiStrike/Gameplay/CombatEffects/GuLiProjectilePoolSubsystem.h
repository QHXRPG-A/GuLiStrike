#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundAttackSnapshot.h"
#include "GuLiProjectilePoolSubsystem.generated.h"

/** A process-local slot identity. Never send this handle over the network. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiProjectilePoolHandle
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int32 Slot = INDEX_NONE;
	UPROPERTY() uint32 Generation = 0;
	UPROPERTY() uint32 MatchEpoch = 0;
	bool IsValid() const { return Slot >= 0 && Generation != 0 && MatchEpoch != 0; }
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiProjectilePoolStats
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int32 Capacity = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int32 Active = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int32 PeakActive = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int64 Launched = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int64 Recycled = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int64 Expansions = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int64 Hits = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int64 Blocked = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") int64 CandidateChecks = 0;
	UPROPERTY(BlueprintReadOnly, Category="Projectile Pool") double LastStepMilliseconds = 0;
};

/** Authority-resolved launch data, frozen independently of the firing burst. */
struct GULISTRIKE_API FGuLiPooledProjectileLaunch
{
	FGuLiCombatEffectContext Context;
	FVector Position = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	FVector MuzzleOffset = FVector::ZeroVector;
	float Speed = 16000.0f;
	float Lifetime = 1.875f;
	float MaximumDistance = 30000.0f;
	float SweepRadius = 9.0f;
	float ServerTime = 0.0f;
	/** Optional retained burst lease. Launch acquires its own reference. */
	FGuid SourceLease;
	/** Frozen player presentation reference, also usable after the firing Pawn disappears. */
	int32 PlayerBulletVfxId = 0;
    FGuLiGroundAttackSnapshot GroundAttack;
	bool bAcceptanceAdditional = false;
	/** Explicit load replenishment may prepare a replacement within one second. */
	float ScheduledActivationSeconds = 0;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiPooledProjectileStateEvent, const FGuLiCombatEffectState&, bool);

/** Server-only data pool. Wingmen/players step at 30 Hz; Commander soldiers sweep independently at 5 Hz. */
UCLASS()
class GULISTRIKE_API UGuLiProjectilePoolSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	TSharedPtr<class IGuLiHybridSimulationAdapter> CreateHybridAdapter(const struct FGuLiHybridObjectState& Object);
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	void BeginEpoch(uint32 NewEpoch);
	FGuLiProjectilePoolHandle Launch(const FGuLiPooledProjectileLaunch& Request);
	bool Release(FGuLiProjectilePoolHandle Handle, EGuLiCombatEffectEndReason Reason);
	bool Query(FGuLiProjectilePoolHandle Handle, FGuLiCombatEffectState& OutState) const;
	bool QueryById(const FGuid& Id, FGuLiCombatEffectState& OutState) const;
    /** True only after authoritative retirement or a final admission rejection. */
    bool HasTerminalFlightResult(const FGuid& Id) const;
	/** None selects all pooled projectiles; other kinds select one source domain. */
	void AppendActiveSnapshot(TArray<FGuLiCombatEffectState>& OutStates, EGuLiTargetKind SourceKind = EGuLiTargetKind::None) const;
	void Step(float ServerTime);
	UFUNCTION(BlueprintPure, Category="Projectile Pool") FGuLiProjectilePoolStats GetStats() const { return Stats; }
	UFUNCTION(BlueprintPure, Category="Projectile Pool") int32 GetActiveCount() const { return ActiveSlots.Num(); }
	FGuLiPooledProjectileStateEvent OnState;
    FGuLiGroundWeaponHitEvent OnGroundWeaponHit;
    static constexpr double SpatialCellCentimeters=20000.;

private:
	friend struct FGuLiHybridProjectileState;
	TSharedPtr<struct FGuLiHybridProjectileState> HybridState;
	void InitializeHybrid(FSubsystemCollectionBase& Collection);
	void ResetHybrid();
	FGuLiProjectilePoolHandle LaunchHybrid(const FGuLiPooledProjectileLaunch& Request,EGuLiTeam SourceTeam);
	bool IsPendingHybrid(FGuLiProjectilePoolHandle Handle) const;
	void RetireHybrid(const FGuLiCombatEffectState& State,const FGuLiCombatEffectContext& Context,
		EGuLiCombatEffectEndReason Reason,const FVector& Position,float Time,const FGuLiTargetHandle* Target);
	FGuLiProjectilePoolHandle ActivateLaunch(const FGuLiPooledProjectileLaunch& Request,EGuLiTeam SourceTeam,
		FGuid Lease=FGuid(),int32 ReservedSlot=INDEX_NONE);
	struct FSlot
	{
		FGuLiCombatEffectState State;
		FGuLiCombatEffectContext Context;
		FGuid SourceLease;
		uint32 Generation = 0;
		int32 ActiveIndex = INDEX_NONE;
		float SweepRadius = 0;
		FGuid HistoryGroup;
	};
	struct FTargetMotion
	{
		FGuLiCombatTargetSnapshot Snapshot;
		FVector Previous = FVector::ZeroVector;
	};
	struct FSimulationHistory
	{
		TMap<FGuLiTargetHandle, FVector> PreviousTargets;
		float PreviousTime = 0;
	};
	bool Matches(FGuLiProjectilePoolHandle Handle) const;
	void Grow(int32 Count);
	void Clear();
	void BuildSpatialIndex(float Now, const FSimulationHistory& History);
	void StepDomain(float Now, bool bGround, FSimulationHistory& History,const FGuid* Group=nullptr);
	bool Retire(FGuLiProjectilePoolHandle Handle, EGuLiCombatEffectEndReason Reason,
		const FVector& Position, float Time, const FGuLiTargetHandle* HitTarget = nullptr);
	UPROPERTY(Transient) TObjectPtr<UGuLiDamageLedgerSubsystem> Ledger;
    struct FAcceptedGroundHit {FGuLiGroundWeaponHit Hit;FGuLiGroundAttackSnapshot Snapshot;};
    TArray<FAcceptedGroundHit> PendingGroundHits;
	/** Keep launch references mapped even after the firing Pawn leaves the world. */
	TArray<FSlot> Slots;
	TArray<int32> FreeSlots;
	TArray<int32> ActiveSlots;
	TArray<int32> DomainSlots[2];
	TArray<FGuLiCombatTargetSnapshot> SourceOnly;
	TArray<FGuLiProjectilePoolHandle> StepHandles;
	TMap<FGuid, FGuLiProjectilePoolHandle> ById;
	FSimulationHistory WingmanHistory;
	FSimulationHistory GroundHistory;
	struct FScheduledLaunch { FGuLiPooledProjectileLaunch Recipe; EGuLiTeam Team; FGuid Lease; FGuLiProjectilePoolHandle Handle; };
	TMap<FGuid,FScheduledLaunch> ScheduledLaunches;
	TMap<FGuid,FSimulationHistory> HybridHistories[2];
	TArray<FGuLiCombatTargetSnapshot> Snapshots;
	TArray<FTargetMotion> Targets;
	TMap<FIntVector, TArray<int32>> SpatialGrid;
	TSet<int32> Candidates;
	TArray<int32> OrderedCandidates;
	TArray<uint32> CandidateVisits;
	uint32 CandidateGeneration = 0;
	FGuLiProjectilePoolStats Stats;
	uint32 Epoch = 0;
	float NextGroundStepTime = 0;
	bool bStepping = false;
};
