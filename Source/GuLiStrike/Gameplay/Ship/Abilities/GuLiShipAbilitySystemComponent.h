// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "GuLiShipAbilitySystemComponent.generated.h"

class AActor;

/** Raised only when the reliable group projection payload must be republished. */
DECLARE_MULTICAST_DELEGATE(FGuLiShipAbilityProjectionChanged);

/**
 * Local-only activation authorization. SpecHandle must never be placed in a
 * network request; consumers send AbilityId + AbilitySetRevision instead.
 */
DECLARE_MULTICAST_DELEGATE_FourParams(
	FGuLiShipTriggeredAbilityAuthorized,
	FGameplayAbilitySpecHandle /* LocalSpecHandle */,
	FGameplayTag /* StableAbilityId */,
	uint32 /* AbilitySetRevision */,
	bool /* bLocallyPredicted */);

/** Binding-exact authorization used by weapon commands; safe when GA classes are reused. */
DECLARE_MULTICAST_DELEGATE_SixParams(
	FGuLiShipWeaponAbilityAuthorized,
	FGameplayAbilitySpecHandle /* LocalSpecHandle */,
	FGuLiWeaponBindingKey /* Binding */,
	FName /* SkillId */,
	FGameplayTag /* CatalogAbilityId */,
	uint32 /* AbilitySetRevision */,
	bool /* bLocallyPredicted */);

/** Pawn-owned ASC for a Ship; no AttributeSet and no numeric GameplayEffects. */
UCLASS(ClassGroup = (Abilities), meta = (BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiShipAbilitySystemComponent final : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UGuLiShipAbilitySystemComponent();

	/** Idempotently establishes OwnerActor=AvatarActor=Ship on either network side. */
	void InitializeShipActorInfo(AActor* ShipActor);

	/**
	 * Authority-only diff/grant operation. Returns true only when the effective
	 * grant set changed. An unchanged repeat succeeds with false and empty error.
	 */
	bool ServerApplyAbilitySet(
		UGuLiShipAbilitySet* AbilitySet,
		const FGuLiShipAbilityLoadoutState& Loadout,
		FString& OutError);

	/** Authority-only death/EndPlay teardown; publishes an invalid tombstone. */
	void ServerClearShipAbilities();

	/** Ordinary UnPossess uses this instead of cancelling persistent group abilities. */
	void SetActiveAbilityInputEnabled(bool bEnabled);
	bool IsActiveAbilityInputEnabled() const { return bActiveAbilityInputEnabled; }

	void AbilityInputTagPressed(FGameplayTag InputTag);
	void AbilityInputTagReleased(FGameplayTag InputTag);
	/** Activates/releases exactly one configured binding; never fans out by shared GA class/tag. */
	bool AbilityWeaponBindingPressed(const FGuLiWeaponBindingKey& Binding);
	bool AbilityWeaponBindingReleased(const FGuLiWeaponBindingKey& Binding);

	/** Context changes are projection changes; exact repeats are no-ops. */
	bool SetProjectionContext(const FGuLiShipAbilityProjectionContext& NewContext);
	const FGuLiShipAbilityProjectionContext& GetProjectionContext() const { return ProjectionContext; }

	/** Builds either a complete usable config or an explicit, hashed invalid tombstone. */
	bool BuildGroupAbilityConfigSnapshot(FGuLiGroupAbilityConfigSnapshot& OutSnapshot) const;

	/** Owner-client bridge from the replicated config; backup owners still receive no ASC specs. */
	bool ObserveReplicatedGroupAbilityConfig(const FGuLiGroupAbilityConfigSnapshot& Snapshot);

	uint32 GetAbilitySetRevision() const { return AbilitySetRevision; }
	uint32 GetWeaponLoadoutRevision() const { return WeaponLoadoutRevision; }
	uint32 GetProjectionSnapshotRevision() const { return ProjectionSnapshotRevision; }
	uint32 GetObservedAbilitySetRevision() const { return ObservedAbilitySetRevision; }
	uint32 GetObservedSnapshotRevision() const { return ObservedSnapshotRevision; }
	const FGuLiShipAbilityLoadoutState& GetAppliedLoadout() const { return AppliedLoadout; }
	const UGuLiShipAbilitySet* GetAppliedAbilitySet() const { return AppliedAbilitySet; }

	bool IsAbilityGranted(FGameplayTag AbilityId) const;
	bool IsAbilityConfigurationCurrent(FGameplayTag AbilityId, uint32 ExpectedAbilitySetRevision) const;
	const FGuLiShipAbilityGrant* FindConfiguredGrant(FGameplayTag AbilityId) const;
	const FGuLiShipAbilityGrant* FindConfiguredGrant(EGuLiShipAbilitySlot Slot) const;
	const FGuLiShipAbilityGrant* FindConfiguredGrant(const FGuLiWeaponBindingKey& Binding) const;
	bool IsWeaponConfigurationCurrent(const FGuLiWeaponBindingKey& Binding,
		FName SkillId, uint32 ExpectedLoadoutRevision, uint32 ExpectedProfileRevision) const;
	const UGuLiWingmanFormationDefinition* GetActiveFormationDefinition() const;
	const UGuLiWingmanWeaponDefinition* GetBasicWeaponDefinition() const;
	const UGuLiWingmanWeaponDefinition* GetMissileDefinition() const;

	/** Authority-only shared missile cooldown represented by a replicated loose GAS tag. */
	bool ServerTryStartMissileCooldown(float DurationSeconds);
	/**
	 * Authority-only atomic-salvo reservation. The stable activation ID owns the
	 * cooldown until it expires; only that owner may roll it back after a failed batch.
	 */
	bool ServerTryReserveMissileCooldown(float DurationSeconds, const FGuid& ReservationId);
	bool ServerRollbackMissileCooldown(const FGuid& ReservationId);
	bool IsMissileCooldownActive() const;
	bool ServerTryReserveWeaponCooldown(
		FName CooldownGroupId, float DurationSeconds, const FGuid& ReservationId);
	bool ServerRollbackWeaponCooldown(FName CooldownGroupId, const FGuid& ReservationId);
	bool IsWeaponCooldownActive(FName CooldownGroupId) const;

	FGuLiShipAbilityProjectionChanged& OnProjectionChanged() { return ProjectionChangedDelegate; }
	FGuLiShipTriggeredAbilityAuthorized& OnTriggeredAbilityAuthorized() { return TriggeredAbilityDelegate; }
	FGuLiShipWeaponAbilityAuthorized& OnWeaponAbilityAuthorized() { return WeaponAbilityDelegate; }

	// Called only by UGuLiShipGameplayAbility instances owned by this ASC.
	bool CanActivateConfiguredAbility(
		FGameplayAbilitySpecHandle Handle,
		FGameplayTag AbilityId,
		EGuLiShipAbilitySlot Slot,
		EGuLiShipAbilityActivationPolicy ActivationPolicy) const;
	bool HandleConfiguredAbilityActivated(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActivationInfo& ActivationInfo);
	void HandleConfiguredAbilityEnded(FGameplayAbilitySpecHandle Handle);
	void BroadcastTriggeredAbilityAuthorization(
		FGameplayAbilitySpecHandle Handle,
		FGameplayTag AbilityId,
		const FGameplayAbilityActivationInfo& ActivationInfo);

private:
	bool HasIntactAppliedGrantState(uint64 ExpectedChecksum) const;
	void ClearGrantedAbilitiesInternal();
	void TryActivatePersistentAbilities();
	void MarkProjectionChanged();
	void BeginProjectionMutation();
	void EndProjectionMutation();
	FGuLiWeaponBindingKey BuildWeaponBinding(const FGuLiShipAbilityGrant& Grant) const;
	void RebuildWeaponBindingIndex();
	FGameplayAbilitySpecHandle FindSpecHandleForBinding(const FGuLiWeaponBindingKey& Binding) const;
	const FGuLiWingmanWeaponChannelConfig* FindObservedChannelForHandle(
		FGameplayAbilitySpecHandle Handle) const;
	static uint32 AdvanceRevision(uint32 Revision);

	UPROPERTY(Transient)
	TObjectPtr<UGuLiShipAbilitySet> AppliedAbilitySet;

	UPROPERTY(Transient)
	FGuLiShipAbilityLoadoutState AppliedLoadout;

	TMap<FGameplayTag, FGameplayAbilitySpecHandle> HandlesByAbilityId;
	TMap<FGameplayAbilitySpecHandle, FGuLiShipAbilityGrant> GrantsByHandle;
	TMap<FGuLiWeaponBindingKey, FGameplayAbilitySpecHandle> HandlesByWeaponBinding;
	TSet<FGameplayAbilitySpecHandle> PersistentActiveHandles;

	FGuLiShipAbilityProjectionContext ProjectionContext;
	uint64 AppliedLoadoutChecksum = 0u;
	uint32 AbilitySetRevision = 0u;
	uint32 WeaponLoadoutRevision = 0u;
	uint32 ProjectionSnapshotRevision = 0u;
	uint32 ObservedAbilitySetRevision = 0u;
	uint32 ObservedSnapshotRevision = 0u;
	FGuLiGroupAbilityConfigSnapshot ObservedGroupAbilityConfig;
	uint32 ProjectionMutationDepth = 0u;
	bool bProjectionMutationDirty = false;
	bool bActiveAbilityInputEnabled = true;
	/**
	 * One-way, local teardown capability established only by authoritative ActorInfo
	 * initialization. Actor role/ActorInfo can already be cleared when EndPlay runs;
	 * this is never replicated and therefore cannot authorize a remote client.
	 */
	UPROPERTY(Transient)
	bool bInitializedOnAuthority = false;
	struct FWeaponCooldownState
	{
		FTimerHandle TimerHandle;
		FGuid ReservationId;
	};
	TMap<FName, FWeaponCooldownState> WeaponCooldownsByGroup;

	FGuLiShipAbilityProjectionChanged ProjectionChangedDelegate;
	FGuLiShipTriggeredAbilityAuthorized TriggeredAbilityDelegate;
	FGuLiShipWeaponAbilityAuthorized WeaponAbilityDelegate;
};
