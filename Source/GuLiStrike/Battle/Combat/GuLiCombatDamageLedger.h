// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiCombatDamageLedger.generated.h"

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCombatHealthState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Health")
	float Health = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Health")
	float MaxHealth = 0.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Shield") float Shield = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Shield") float MaxShield = 0;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Health")
	uint32 Revision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Health")
	bool bDead = false;

	bool IsWellFormed() const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiDamageRequest
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Combat|Damage")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FGuid DamageEventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FGuid ShotId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FGuLiTargetHandle Source;

	/** Required by Wingman weapon validators before ledger submission. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FGuLiWingmanHandle Emitter;

	/** Optional weapon provenance. Wingman channel attacks always populate all fields. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FGuLiWeaponBindingKey WeaponBinding;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FName SkillId;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Damage")
	uint32 LoadoutRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Damage")
	uint32 ProfileRevision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FGuid RootEventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FGuLiTargetHandle Target;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float Damage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	FVector HitLocation = FVector::ZeroVector;

	bool IsWellFormed() const;
};

UENUM(BlueprintType)
enum class EGuLiDamageCommitStatus : uint8
{
	Committed = 0,
	Duplicate,
	RejectedNotAuthority,
	RejectedMalformed,
	RejectedWrongEpoch,
	RejectedMissingTarget,
	RejectedTargetDead,
	RejectedFriendlyFire,
	RejectedByAdapter,
	Pending
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiDamageCommitResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	EGuLiDamageCommitStatus Status = EGuLiDamageCommitStatus::RejectedMalformed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float AppliedDamage = 0.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Shield") float AbsorbedDamage = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float RemainingHealth = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	bool bKilled = false;

	/** Deterministically derived only for a lethal committed DamageEvent. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Death")
	FGuid DeathEventId;

	/** Deterministically derived alongside DeathEventId; an unavailable policy never fabricates a grant. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Reward")
	FGuid RewardEventId;

	uint64 CommitOrdinal = 0u;

	bool WasAccepted() const
	{
		return Status == EGuLiDamageCommitStatus::Committed || Status == EGuLiDamageCommitStatus::Duplicate;
	}
};

UENUM(BlueprintType)
enum class EGuLiRewardGrantStatus : uint8
{
	NotApplicable = 0,
	PolicyUnavailable,
	NotGranted,
	SinkUnavailable,
	SinkRejected,
	Granted
};

/** Opaque grant authored by an external battle reward policy; the ledger supplies no economy values. */
USTRUCT()
struct GULISTRIKE_API FGuLiRewardGrant
{
	GENERATED_BODY()

	FGuid BeneficiaryId;

	FName RewardDefinitionId;

	int64 Quantity = 0;

	bool IsWellFormed() const
	{
		return BeneficiaryId.IsValid() && !RewardDefinitionId.IsNone() && Quantity > 0;
	}
};

/** One authority-only death fact. Its EventId is a stable function of the lethal DamageEventId. */
USTRUCT()
struct GULISTRIKE_API FGuLiDeathCommitRecord
{
	GENERATED_BODY()

	uint32 MatchEpoch = 0u;

	FGuid DeathEventId;

	FGuid DamageEventId;

	FGuid ShotId;

	FGuLiTargetHandle Source;

	FGuLiWingmanHandle Emitter;

	FGuLiWeaponBindingKey WeaponBinding;

	FName SkillId;

	uint32 LoadoutRevision = 0u;

	uint32 ProfileRevision = 0u;

	FGuid RootEventId;

	FGuLiTargetHandle Target;

	uint64 CommitOrdinal = 0u;

	bool IsWellFormed() const;
};

/** One authority-only reward decision. A committed decision is not necessarily an economy grant. */
USTRUCT()
struct GULISTRIKE_API FGuLiRewardCommitRecord
{
	GENERATED_BODY()

	uint32 MatchEpoch = 0u;

	FGuid RewardEventId;

	FGuid DeathEventId;

	EGuLiRewardGrantStatus Status = EGuLiRewardGrantStatus::NotApplicable;

	FGuLiRewardGrant Grant;

	uint64 CommitOrdinal = 0u;

	bool WasGranted() const { return Status == EGuLiRewardGrantStatus::Granted; }
	bool IsWellFormed() const;
};

/** Optional battle-owned policy and sink. Neither callback may be supplied by a client. */
using FGuLiRewardPolicy = TFunction<bool(const FGuLiDeathCommitRecord&, FGuLiRewardGrant&)>;
using FGuLiRewardSink = TFunction<bool(const FGuLiRewardCommitRecord&)>;

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCombatTargetSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	FGuLiTargetHandle Handle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	FVector Location = FVector::ZeroVector;

	/** Optional logical pose orientation; Wingman fills this from the server-accepted sample. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	float CollisionRadius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	float Health = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	bool bAlive = false;

	TWeakObjectPtr<AActor> CollisionActor;
};

/**
 * Narrow target bridge. Commander Mass, Wingman lifecycle and Actor components
 * register callbacks without introducing reverse module/type dependencies.
 */
struct GULISTRIKE_API FGuLiCombatTargetAdapter
{
	TWeakObjectPtr<UObject> LifetimeOwner;
	TFunction<bool(FGuLiCombatTargetSnapshot&)> ReadSnapshot;

	bool IsBound() const
	{
		return LifetimeOwner.IsValid() && ReadSnapshot;
	}
};

/** An attack origin which is deliberately absent from the damage target directory. */
struct GULISTRIKE_API FGuLiCombatSourceAdapter
{
	TWeakObjectPtr<UObject> LifetimeOwner;
	TFunction<bool(FGuLiCombatTargetSnapshot&)> ReadSnapshot;
};

namespace GuLiCombatTargets
{
	/** Stable match-local Commander Mass identity; LocalId is the authoritative SoldierId. */
	GULISTRIKE_API FGuLiTargetHandle MakeCommanderSoldierTargetHandle(
		uint32 MatchEpoch,
		uint32 SoldierId,
		uint32 EntityGeneration = 1u);

	/** Stable across death/replenishment slot reuse; EntityGeneration still invalidates the old identity. */
	GULISTRIKE_API FGuLiTargetHandle MakeWingmanTargetHandle(const FGuLiWingmanHandle& Wingman);
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGuLiCombatHealthChanged, float, Health, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGuLiCombatDeath);

/** Custom server-owned Ship health. It intentionally is not an AttributeSet. */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiCombatHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiCombatHealthComponent();

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	const FGuLiCombatHealthState& GetHealthState() const { return HealthState; }
	float GetConfiguredMaxHealth() const { return DefaultMaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	bool IsAlive() const { return HealthState.IsWellFormed() && !HealthState.bDead; }

	UFUNCTION(BlueprintPure, Category = "Combat|Target")
	const FGuLiTargetHandle& GetTargetHandle() const { return TargetHandle; }

	UFUNCTION(BlueprintPure, Category = "Combat|Target")
	EGuLiTeam GetCombatTeam() const { return Team; }

	/** Apply a committed object-owner result to this local health view without publishing it again. */
	bool ApplyHybridHealth(const FGuLiTargetHandle& Handle, EGuLiTeam Faction, const FGuLiCombatHealthState& State,
		uint64 Epoch, uint64 LedgerRevision, bool bBusinessEffects);

	UPROPERTY(BlueprintAssignable, Category = "Combat|Health")
	FGuLiCombatHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Health")
	FGuLiCombatDeath OnDeath;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_HealthState(const FGuLiCombatHealthState& PreviousState);
	UFUNCTION() void OnRep_TargetIdentity();

	void RegisterWithLedger();
	void UnregisterFromLedger();
	void BroadcastHealthState(bool bWasDead);

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Health", meta = (ClampMin = "1.0"))
	float DefaultMaxHealth = 1000.0f;

	UPROPERTY()
	FGuLiCombatHealthState HealthState;

	UPROPERTY()
	FGuLiTargetHandle TargetHandle;

	UPROPERTY()
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint64 HybridEpoch = 0, HybridLedgerRevision = 0;
	bool bHybridObservedAlive = false, bHybridDeathDelivered = false;
};

/** Local combat query registry and asynchronous requests to the responsible peer. */
UCLASS()
class GULISTRIKE_API UGuLiDamageLedgerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	bool BindMatchEpoch(uint32 NewMatchEpoch);
	uint32 GetMatchEpoch() const { return MatchEpoch; }

	bool RegisterTarget(const FGuLiTargetHandle& Handle, FGuLiCombatTargetAdapter Adapter);
	bool RegisterHealthComponent(UGuLiCombatHealthComponent& HealthComponent);
	bool RegisterSource(const FGuLiTargetHandle& Handle, FGuLiCombatSourceAdapter Adapter);
	void UnregisterSource(const FGuLiTargetHandle& Handle, const UObject* Owner);
	bool TryGetSourceSnapshot(const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot);
	void GetSourceOnlySnapshots(TArray<FGuLiCombatTargetSnapshot>& OutSnapshots);
	void UnregisterTarget(const FGuLiTargetHandle& Handle, const UObject* ExpectedOwner = nullptr);

	bool TryGetTargetSnapshot(const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot);
	/** Authority-only stable snapshot of the current target directory for server target acquisition. */
	void GetTargetSnapshots(TArray<FGuLiCombatTargetSnapshot>& OutSnapshots, EGuLiTargetKind Kind = EGuLiTargetKind::None);
	/** Returns request identity; completion is reported by the responsible client. */
	FGuid SubmitDamage(const FGuLiDamageRequest& Request, UObject* CallbackOwner = nullptr,
		TFunction<void(const FGuLiDamageCommitResult&)> OnResolved = {});
	FGuid SubmitEffectDamage(const FGuLiDamageRequest& Request, const FGuid& LeaseId,
		UObject* CallbackOwner = nullptr, TFunction<void(const FGuLiDamageCommitResult&)> OnResolved = {});

	/** Authority-only cast provenance. A lease is not a target and survives its source Actor's destruction. */
	FGuid AcquireEffectSource(const FGuLiTargetHandle& Source);
	bool RetainEffectSource(const FGuid& LeaseId);
	void ReleaseEffectSource(const FGuid& LeaseId);

	uint64 GetCommitCount() const { return CommitCount; }

private:
	struct FRetainedEffectSource
	{
		FGuLiTargetHandle Source;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		uint32 Epoch = 0;
		int32 References = 1;
		uint64 StartedAt = 0;
	};
	struct FPendingSubmission
	{
		FGuid RetainedLease;
		TWeakObjectPtr<UObject> CallbackOwner;
		TFunction<void(const FGuLiDamageCommitResult&)> Callback;
	};
	TMap<FGuid, FPendingSubmission> PendingSubmissions;
	FGuid SubmitHybridDamage(const FGuLiDamageRequest& Request, const FGuid& Lease,
		UObject* CallbackOwner, TFunction<void(const FGuLiDamageCommitResult&)> Callback);
	TMap<FGuid, FRetainedEffectSource> RetainedEffectSources;
	bool IsAuthorityWorld() const;
	void RememberResult(const FGuid& EventId, const FGuLiDamageCommitResult& Result);
	void PruneInvalidTargets();

	TMap<FGuLiTargetHandle, FGuLiCombatTargetAdapter> TargetAdapters;
	/** Live directory membership by domain; snapshots are still read freshly for every caller. */
	TMap<EGuLiTargetKind, TSet<FGuLiTargetHandle>> TargetDomains;
	TMap<FGuLiTargetHandle, FGuLiCombatSourceAdapter> SourceAdapters;
	TMap<FGuid, FGuLiDamageCommitResult> ResultsByEvent;
	TArray<FGuid> EventOrder;
	uint32 MatchEpoch = 0u;
	uint64 CommitCount = 0u;
	int32 MaximumRememberedEvents = 8192;
};
