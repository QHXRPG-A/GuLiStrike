#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Ship/Capabilities/GuLiShipCapabilityComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#include "Battle/Combat/GuLiWingmanReplenishmentController.h"
#include "GuLiShipHangarCapabilityComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FGuLiShipAbilityProjectionChanged);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FGuLiShipTriggeredAbilityAuthorized, FGameplayTag, uint32, bool);
DECLARE_MULTICAST_DELEGATE_FiveParams(FGuLiShipWeaponAbilityAuthorized,
	FGuLiWeaponBindingKey, FName, FGameplayTag, uint32, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiShipAbilityPresentationChanged, FGameplayTag, bool);

/** Numeric combat definitions remain in the existing data assets/tables. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiShipHangarDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UGuLiShipAbilitySet> AbilitySet;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FGuLiShipAbilityLoadoutState Loadout;
};

/** Group-owned authorization and cooldowns. Existing Wingman execution is unchanged. */
UCLASS(ClassGroup=(Ship))
class GULISTRIKE_API UGuLiShipHangarCapabilityComponent : public UGuLiShipCapabilityComponent
{
	GENERATED_BODY()
public:
	UGuLiShipHangarCapabilityComponent();
	virtual bool ValidateConfiguration(const UDataAsset* Configuration, FString& Error) const override;
	// The unchanged v14 Wingman protocol represents one 25-member group per Ship.
	virtual FName GetExclusiveRuntimeDomain() const override { return TEXT("Wingman.ProtocolGroup"); }
	virtual void BuildRuntimeView(FGuLiShipCapabilityRuntimeView& View) const override;
	virtual void ApplyRuntimeView(const FGuLiShipCapabilityRuntimeView& View) override;
	virtual bool RequestActivation(FName ActionId, FString& Error) override;
	void InitializeShipActorInfo(AActor* ShipActor);
	bool IsOwnerActorAuthoritative() const;
	bool ServerApplyAbilitySet(UGuLiShipAbilitySet* AbilitySet, const FGuLiShipAbilityLoadoutState& Loadout, FString& Error);
	void ServerClearShipAbilities();
	void SetActiveAbilityInputEnabled(bool bEnabled);
	bool IsActiveAbilityInputEnabled() const { return bActiveAbilityInputEnabled; }
	void AbilityInputTagPressed(FGameplayTag InputTag);
	void AbilityInputTagReleased(FGameplayTag InputTag);
	bool AbilityWeaponBindingPressed(const FGuLiWeaponBindingKey& Binding);
	bool AbilityWeaponBindingReleased(const FGuLiWeaponBindingKey& Binding);
	bool SetProjectionContext(const FGuLiShipAbilityProjectionContext& Context);
	const FGuLiShipAbilityProjectionContext& GetProjectionContext() const { return ProjectionContext; }
	bool BuildGroupAbilityConfigSnapshot(FGuLiGroupAbilityConfigSnapshot& Snapshot) const;
	bool ObserveReplicatedGroupAbilityConfig(const FGuLiGroupAbilityConfigSnapshot& Snapshot);
	uint32 GetAbilitySetRevision() const { return AbilitySetRevision; }
	uint32 GetWeaponLoadoutRevision() const { return WeaponLoadoutRevision; }
	uint32 GetProjectionSnapshotRevision() const { return ProjectionSnapshotRevision; }
	uint32 GetObservedAbilitySetRevision() const { return ObservedAbilitySetRevision; }
	uint32 GetObservedSnapshotRevision() const { return ObservedSnapshotRevision; }
	const FGuLiShipAbilityLoadoutState& GetAppliedLoadout() const { return AppliedLoadout; }
	UFUNCTION(BlueprintPure, Category="Ship|Hangar")
	const UGuLiShipAbilitySet* GetAppliedAbilitySet() const { return AppliedAbilitySet; }
	bool IsAbilityGranted(FGameplayTag AbilityId) const;
	bool IsAbilityConfigurationCurrent(FGameplayTag AbilityId, uint32 Revision) const;
	const FGuLiShipAbilityGrant* FindConfiguredGrant(FGameplayTag AbilityId) const;
	const FGuLiShipAbilityGrant* FindConfiguredGrant(EGuLiShipAbilitySlot Slot) const;
	const FGuLiShipAbilityGrant* FindConfiguredGrant(const FGuLiWeaponBindingKey& Binding) const;
	bool IsWeaponConfigurationCurrent(const FGuLiWeaponBindingKey& Binding, FName SkillId, uint32 LoadoutRevision, uint32 ProfileRevision) const;
	const UGuLiWingmanFormationDefinition* GetActiveFormationDefinition() const;
	const UGuLiWingmanWeaponDefinition* GetBasicWeaponDefinition() const;
	const UGuLiWingmanWeaponDefinition* GetMissileDefinition() const;
	bool ServerTryStartMissileCooldown(float DurationSeconds);
	bool ServerTryReserveMissileCooldown(float DurationSeconds, const FGuid& ReservationId);
	bool ServerRollbackMissileCooldown(const FGuid& ReservationId);
	bool IsMissileCooldownActive() const;
	bool ServerTryReserveWeaponCooldown(FName GroupId, float DurationSeconds, const FGuid& ReservationId);
	bool ServerRollbackWeaponCooldown(FName GroupId, const FGuid& ReservationId);
	bool IsWeaponCooldownActive(FName GroupId) const;
	FGuLiShipAbilityProjectionChanged& OnProjectionChanged() { return ProjectionChanged; }
	FGuLiShipTriggeredAbilityAuthorized& OnTriggeredAbilityAuthorized() { return TriggeredAbility; }
	FGuLiShipWeaponAbilityAuthorized& OnWeaponAbilityAuthorized() { return WeaponAbility; }
	FGuLiShipAbilityPresentationChanged& OnAbilityPresentationChanged() { return PresentationChanged; }
	FGuLiWingmanCombatCoordinator* GetCombatCoordinator() const { return CombatCoordinator.Get(); }
	bool InitializeCombatCoordinator(const FGuLiWingmanCombatContext& Context, FString& Error);
	void ResetCombatCoordinator();
	FGuLiWingmanReplenishmentController& GetReplenishmentController() { return ReplenishmentController; }
	const FGuLiWingmanReplenishmentController& GetReplenishmentController() const { return ReplenishmentController; }
protected:
	virtual void OnPrepared() override;
	virtual void OnEnabled() override;
	virtual void OnSuspended() override;
	virtual void OnReleased() override;
private:
	FGuLiWeaponBindingKey BuildWeaponBinding(const FGuLiShipAbilityGrant& Grant) const;
	void RebuildWeaponBindingIndex();
	void MarkProjectionChanged();
	void PublishCooldowns();
	static uint32 AdvanceRevision(uint32 Revision);
	UPROPERTY(Transient) TObjectPtr<UGuLiShipAbilitySet> AppliedAbilitySet;
	UPROPERTY(Transient) FGuLiShipAbilityLoadoutState AppliedLoadout;
	UPROPERTY(Transient) TMap<FGameplayTag, FGuLiShipAbilityGrant> GrantsById;
	TMap<FGuLiWeaponBindingKey, FGameplayTag> IdsByWeaponBinding;
	TSet<FGameplayTag> PressedAbilities;
	FGuLiShipAbilityProjectionContext ProjectionContext;
	FGuLiGroupAbilityConfigSnapshot ObservedGroupAbilityConfig;
	uint64 AppliedLoadoutChecksum = 0;
	uint32 AbilitySetRevision = 0, WeaponLoadoutRevision = 0, ProjectionSnapshotRevision = 0;
	uint32 ObservedAbilitySetRevision = 0, ObservedSnapshotRevision = 0;
	bool bActiveAbilityInputEnabled = true;
	bool bInitializedOnAuthority = false;
	struct FCooldown { FTimerHandle Timer; FGuid Reservation; double EndsAt = 0; };
	TMap<FName, FCooldown> Cooldowns;
	TArray<FGuLiShipCapabilityTimerView> CooldownViews;
	FGuLiShipAbilityProjectionChanged ProjectionChanged;
	FGuLiShipTriggeredAbilityAuthorized TriggeredAbility;
	FGuLiShipWeaponAbilityAuthorized WeaponAbility;
	FGuLiShipAbilityPresentationChanged PresentationChanged;
	TUniquePtr<FGuLiWingmanCombatCoordinator> CombatCoordinator;
	FGuLiWingmanReplenishmentController ReplenishmentController;
};
