#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
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
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiPooledProjectileStateEvent, const FGuLiCombatEffectState&, bool);

/** Server-only data pool. Wingmen/players step at 30 Hz; Commander soldiers sweep independently at 5 Hz. */
UCLASS()
class GULISTRIKE_API UGuLiProjectilePoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void BeginEpoch(uint32 NewEpoch);
	FGuLiProjectilePoolHandle Launch(const FGuLiPooledProjectileLaunch& Request);
	bool Release(FGuLiProjectilePoolHandle Handle, EGuLiCombatEffectEndReason Reason);
	bool Query(FGuLiProjectilePoolHandle Handle, FGuLiCombatEffectState& OutState) const;
	bool QueryById(const FGuid& Id, FGuLiCombatEffectState& OutState) const;
	/** None selects all pooled projectiles; other kinds select one source domain. */
	void AppendActiveSnapshot(TArray<FGuLiCombatEffectState>& OutStates, EGuLiTargetKind SourceKind = EGuLiTargetKind::None) const;
	void Step(float ServerTime);
	UFUNCTION(BlueprintPure, Category="Projectile Pool") FGuLiProjectilePoolStats GetStats() const { return Stats; }
	UFUNCTION(BlueprintPure, Category="Projectile Pool") int32 GetActiveCount() const { return ActiveSlots.Num(); }
	FGuLiPooledProjectileStateEvent OnState;

private:
	struct FSlot
	{
		FGuLiCombatEffectState State;
		FGuLiCombatEffectContext Context;
		FGuid SourceLease;
		uint32 Generation = 0;
		int32 ActiveIndex = INDEX_NONE;
		float SweepRadius = 0;
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
	void StepDomain(float Now, bool bGround, FSimulationHistory& History);
	bool Retire(FGuLiProjectilePoolHandle Handle, EGuLiCombatEffectEndReason Reason,
		const FVector& Position, float Time, const FGuLiTargetHandle* HitTarget = nullptr);
	UPROPERTY(Transient) TObjectPtr<UGuLiDamageLedgerSubsystem> Ledger;
	/** Keep launch references mapped even after the firing Pawn leaves the world. */
	TArray<FSlot> Slots;
	TArray<int32> FreeSlots;
	TArray<int32> ActiveSlots;
	TArray<FGuLiProjectilePoolHandle> StepHandles;
	TMap<FGuid, FGuLiProjectilePoolHandle> ById;
	FSimulationHistory WingmanHistory;
	FSimulationHistory GroundHistory;
	TArray<FGuLiCombatTargetSnapshot> Snapshots;
	TArray<FTargetMotion> Targets;
	TMap<FIntVector, TArray<int32>> SpatialGrid;
	TSet<int32> Candidates;
	FGuLiProjectilePoolStats Stats;
	uint32 Epoch = 0;
	float NextGroundStepTime = 0;
	bool bStepping = false;
};
