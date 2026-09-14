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

	/** Optional for a Ship projectile; required by Wingman weapon validators before ledger submission. */
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
	RejectedByAdapter
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiDamageCommitResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	EGuLiDamageCommitStatus Status = EGuLiDamageCommitStatus::RejectedMalformed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float AppliedDamage = 0.0f;

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
	TFunction<bool(const FGuLiDamageRequest&, FGuLiDamageCommitResult&)> ApplyDamage;

	bool IsBound() const
	{
		return LifetimeOwner.IsValid() && ReadSnapshot && ApplyDamage;
	}
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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	const FGuLiCombatHealthState& GetHealthState() const { return HealthState; }

	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	bool IsAlive() const { return HealthState.IsWellFormed() && !HealthState.bDead; }

	UFUNCTION(BlueprintPure, Category = "Combat|Target")
	const FGuLiTargetHandle& GetTargetHandle() const { return TargetHandle; }

	UFUNCTION(BlueprintPure, Category = "Combat|Target")
	EGuLiTeam GetCombatTeam() const { return Team; }

	/** Authority-only identity registration. Repeating the same values is a no-op. */
	bool ConfigureServerTarget(const FGuLiTargetHandle& NewHandle, EGuLiTeam NewTeam);

	/** Authority-only initialization; preserves the current health ratio when requested. */
	bool InitializeServerHealth(float NewMaxHealth, bool bPreserveRatio = false);

	/** Called only by the Damage Ledger's registered adapter. */
	bool ApplyServerDamage(const FGuLiDamageRequest& Request, FGuLiDamageCommitResult& OutResult);

	UPROPERTY(BlueprintAssignable, Category = "Combat|Health")
	FGuLiCombatHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Health")
	FGuLiCombatDeath OnDeath;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_HealthState();

	void RegisterWithLedger();
	void UnregisterFromLedger();
	void BroadcastHealthState(bool bWasDead);

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Health", meta = (ClampMin = "1.0"))
	float DefaultMaxHealth = 1000.0f;

	UPROPERTY(ReplicatedUsing = OnRep_HealthState)
	FGuLiCombatHealthState HealthState;

	UPROPERTY(Replicated)
	FGuLiTargetHandle TargetHandle;

	UPROPERTY(Replicated)
	EGuLiTeam Team = EGuLiTeam::Unassigned;
};

/** Server-only idempotent damage truth and target registry. */
UCLASS()
class GULISTRIKE_API UGuLiDamageLedgerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	bool BeginServerEpoch(uint32 NewMatchEpoch);
	uint32 GetMatchEpoch() const { return MatchEpoch; }

	bool RegisterTarget(const FGuLiTargetHandle& Handle, FGuLiCombatTargetAdapter Adapter);
	bool RegisterHealthComponent(UGuLiCombatHealthComponent& HealthComponent);
	void UnregisterTarget(const FGuLiTargetHandle& Handle, const UObject* ExpectedOwner = nullptr);

	bool TryGetTargetSnapshot(const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot);
	/** Authority-only stable snapshot of the current target directory for server target acquisition. */
	void GetTargetSnapshots(TArray<FGuLiCombatTargetSnapshot>& OutSnapshots);
	FGuLiDamageCommitResult CommitDamage(const FGuLiDamageRequest& Request);

	/** Authority-only cast provenance. A lease is not a target and survives its source Actor's destruction. */
	FGuid AcquireEffectSource(const FGuLiTargetHandle& Source);
	bool RetainEffectSource(const FGuid& LeaseId);
	void ReleaseEffectSource(const FGuid& LeaseId);
	FGuLiDamageCommitResult CommitEffectDamage(const FGuLiDamageRequest& Request, const FGuid& LeaseId);

	/** Authority-only optional integration point. Empty callbacks restore fail-closed Policy/SinkUnavailable results. */
	bool SetRewardPipeline(FGuLiRewardPolicy InPolicy, FGuLiRewardSink InSink);
	void ClearRewardPipeline();

	bool TryGetDeathRecord(const FGuid& DeathEventId, FGuLiDeathCommitRecord& OutRecord) const;
	bool TryGetRewardRecord(const FGuid& RewardEventId, FGuLiRewardCommitRecord& OutRecord) const;

	static FGuid DeriveDeathEventId(const FGuid& DamageEventId);
	static FGuid DeriveRewardEventId(const FGuid& DamageEventId);

	uint64 GetCommitCount() const { return CommitCount; }
	uint64 GetDeathCommitCount() const { return DeathCommitCount; }
	uint64 GetRewardCommitCount() const { return RewardCommitCount; }
	uint64 GetGrantedRewardCount() const { return GrantedRewardCount; }
	int32 GetRememberedEventCount() const { return ResultsByEvent.Num(); }
	int32 GetRememberedDeathEventCount() const { return DeathRecordsByEvent.Num(); }
	int32 GetRememberedRewardEventCount() const { return RewardRecordsByEvent.Num(); }

private:
	struct FRetainedEffectSource
	{
		FGuLiTargetHandle Source;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		uint32 Epoch = 0;
		int32 References = 1;
	};
	FGuLiDamageCommitResult CommitDamageInternal(const FGuLiDamageRequest& Request, const EGuLiTeam* FrozenSourceTeam);
	TMap<FGuid, FRetainedEffectSource> RetainedEffectSources;
	bool IsAuthorityWorld() const;
	void RememberResult(const FGuid& EventId, const FGuLiDamageCommitResult& Result);
	void RememberDeathRecord(const FGuLiDeathCommitRecord& Record);
	void RememberRewardRecord(const FGuLiRewardCommitRecord& Record);
	void CommitLethalEvents(const FGuLiDamageRequest& Request, FGuLiDamageCommitResult& InOutResult);
	void PruneInvalidTargets();

	TMap<FGuLiTargetHandle, FGuLiCombatTargetAdapter> TargetAdapters;
	TMap<FGuid, FGuLiDamageCommitResult> ResultsByEvent;
	TArray<FGuid> EventOrder;
	TMap<FGuid, FGuLiDeathCommitRecord> DeathRecordsByEvent;
	TArray<FGuid> DeathEventOrder;
	TMap<FGuid, FGuLiRewardCommitRecord> RewardRecordsByEvent;
	TArray<FGuid> RewardEventOrder;
	FGuLiRewardPolicy RewardPolicy;
	FGuLiRewardSink RewardSink;
	uint32 MatchEpoch = 0u;
	uint64 CommitCount = 0u;
	uint64 DeathCommitCount = 0u;
	uint64 RewardCommitCount = 0u;
	uint64 GrantedRewardCount = 0u;
	int32 MaximumRememberedEvents = 8192;
};
