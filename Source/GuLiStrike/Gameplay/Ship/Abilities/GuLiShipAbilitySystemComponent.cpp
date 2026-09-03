// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Ship/Abilities/GuLiShipGameplayAbility.h"

namespace
{
	bool Fail(FString& OutError, const FString& Message)
	{
		OutError = Message;
		return false;
	}
}

UGuLiShipAbilitySystemComponent::UGuLiShipAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
	SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void UGuLiShipAbilitySystemComponent::InitializeShipActorInfo(AActor* ShipActor)
{
	if (!ShipActor)
	{
		return;
	}
	if (ShipActor->HasAuthority())
	{
		// EndPlay may clear network role/ActorInfo before the Ship asks its ASC to
		// tear down. Remember that this local component was initialized by the
		// authority; the flag is deliberately one-way and never replicated.
		bInitializedOnAuthority = true;
	}

	// Re-running InitAbilityActorInfo refreshes controller/avatar caches after
	// possession without granting a second copy of any ability.
	InitAbilityActorInfo(ShipActor, ShipActor);
	if (IsOwnerActorAuthoritative())
	{
		TryActivatePersistentAbilities();
	}
}

bool UGuLiShipAbilitySystemComponent::ServerApplyAbilitySet(
	UGuLiShipAbilitySet* AbilitySet,
	const FGuLiShipAbilityLoadoutState& Loadout,
	FString& OutError)
{
	OutError.Reset();
	if (!IsOwnerActorAuthoritative())
	{
		return Fail(OutError, TEXT("Only the authoritative Ship may grant or clear Ship abilities."));
	}
	if (!AbilitySet)
	{
		return Fail(OutError, TEXT("Ship ability set is null."));
	}

	FGuLiShipAbilityLoadoutState NormalizedLoadout = Loadout;
	NormalizedLoadout.Normalize();
	TArray<FGuLiShipAbilityGrant> OrderedGrants;
	if (!AbilitySet->ResolveLoadout(NormalizedLoadout, OrderedGrants, &OutError))
	{
		return false;
	}

	const uint64 NewChecksum = AbilitySet->ComputeLoadoutChecksum(NormalizedLoadout);
	if (NewChecksum == 0u)
	{
		return Fail(OutError, TEXT("Ship ability set produced an invalid zero loadout checksum."));
	}
	if (AppliedAbilitySet == AbilitySet
		&& AppliedLoadout == NormalizedLoadout
		&& AppliedLoadoutChecksum == NewChecksum
		&& HasIntactAppliedGrantState(NewChecksum))
	{
		return false;
	}

	BeginProjectionMutation();
	ClearGrantedAbilitiesInternal();
	AppliedAbilitySet = AbilitySet;
	AppliedLoadout = NormalizedLoadout;
	AppliedLoadoutChecksum = NewChecksum;
	AbilitySetRevision = AdvanceRevision(AbilitySetRevision);

	for (const FGuLiShipAbilityGrant& Grant : OrderedGrants)
	{
		FGameplayAbilitySpec Spec(Grant.AbilityClass, Grant.AbilityLevel, INDEX_NONE, AbilitySet);
		if (Grant.InputTag.IsValid())
		{
			Spec.GetDynamicSpecSourceTags().AddTag(Grant.InputTag);
		}
		const FGameplayAbilitySpecHandle Handle = GiveAbility(Spec);
		if (!Handle.IsValid())
		{
			ClearGrantedAbilitiesInternal();
			AppliedAbilitySet = nullptr;
			AppliedLoadout = FGuLiShipAbilityLoadoutState();
			AppliedLoadout.AbilityIds.Reset();
			AppliedLoadoutChecksum = 0u;
			MarkProjectionChanged();
			EndProjectionMutation();
			return Fail(OutError, FString::Printf(
				TEXT("GAS failed to grant Ship ability %s."), *Grant.AbilityId.ToString()));
		}
		HandlesByAbilityId.Add(Grant.AbilityId, Handle);
		GrantsByHandle.Add(Handle, Grant);
	}

	MarkProjectionChanged();
	TryActivatePersistentAbilities();
	EndProjectionMutation();
	return true;
}

void UGuLiShipAbilitySystemComponent::ServerClearShipAbilities()
{
	// During Actor EndPlay both the cached AbilityActorInfo and the Actor's network
	// role may already be in teardown. Only a component that was initialized by
	// authority earlier in this exact Ship lifetime may use the fallback path.
	// Do not use IsOwnerActorAuthoritative() alone here: vanilla ASC reports true
	// before ActorInfo initialization because bCachedIsNetSimulated defaults false.
	const AActor* CurrentAbilityOwner = GetOwnerActor();
	const bool bCurrentOwnerIsAuthority = CurrentAbilityOwner && CurrentAbilityOwner->HasAuthority();
	if (!bCurrentOwnerIsAuthority && !bInitializedOnAuthority)
	{
		return;
	}
	// Cooldown is independent of an ability instance. Never let an earlier ability
	// cancellation make this function return while a loose cooldown tag/timer is live.
	if (HandlesByAbilityId.IsEmpty() && !AppliedAbilitySet
		&& !IsMissileCooldownActive() && !MissileCooldownReservationId.IsValid())
	{
		return;
	}

	BeginProjectionMutation();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MissileCooldownTimerHandle);
	}
	MissileCooldownTimerHandle.Invalidate();
	SetLooseGameplayTagCount(
		TAG_GuLi_ShipAbility_State_MissileCooldown,
		0,
		EGameplayTagReplicationState::TagOnly);
	MissileCooldownReservationId.Invalidate();
	ClearGrantedAbilitiesInternal();
	AppliedAbilitySet = nullptr;
	AppliedLoadout = FGuLiShipAbilityLoadoutState();
	AppliedLoadout.AbilityIds.Reset();
	AppliedLoadoutChecksum = 0u;
	MarkProjectionChanged();
	EndProjectionMutation();
}

void UGuLiShipAbilitySystemComponent::SetActiveAbilityInputEnabled(const bool bEnabled)
{
	if (bActiveAbilityInputEnabled == bEnabled)
	{
		return;
	}
	bActiveAbilityInputEnabled = bEnabled;
	if (!bEnabled)
	{
		for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
		{
			if (!Spec.InputPressed)
			{
				continue;
			}
			Spec.InputPressed = false;
			AbilitySpecInputReleased(Spec);
		}
	}
}

void UGuLiShipAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	if (!bActiveAbilityInputEnabled || !InputTag.IsValid())
	{
		return;
	}

	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}
		Spec.InputPressed = true;
		AbilitySpecInputPressed(Spec);
		if (!Spec.IsActive())
		{
			TryActivateAbility(Spec.Handle);
		}
	}
}

void UGuLiShipAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}
		Spec.InputPressed = false;
		AbilitySpecInputReleased(Spec);
	}
}

bool UGuLiShipAbilitySystemComponent::SetProjectionContext(
	const FGuLiShipAbilityProjectionContext& NewContext)
{
	if (!NewContext.IsWellFormed() || ProjectionContext == NewContext)
	{
		return false;
	}
	ProjectionContext = NewContext;
	MarkProjectionChanged();
	return true;
}

bool UGuLiShipAbilitySystemComponent::BuildGroupAbilityConfigSnapshot(
	FGuLiGroupAbilityConfigSnapshot& OutSnapshot) const
{
	OutSnapshot = FGuLiGroupAbilityConfigSnapshot();
	if (!ProjectionContext.IsWellFormed() || ProjectionSnapshotRevision == 0u)
	{
		return false;
	}

	OutSnapshot.ShipInstanceId = ProjectionContext.ShipInstanceId;
	OutSnapshot.ShipGeneration = ProjectionContext.ShipGeneration;
	OutSnapshot.GroupGeneration = ProjectionContext.GroupGeneration;
	OutSnapshot.AbilitySetRevision = AbilitySetRevision;
	OutSnapshot.SnapshotRevision = ProjectionSnapshotRevision;
	OutSnapshot.FormationCommandRevision = ProjectionContext.FormationCommandRevision;
	OutSnapshot.EffectiveClientSimTick = ProjectionContext.EffectiveClientSimTick;

	const FGameplayAbilitySpecHandle* FormationHandle =
		PersistentActiveHandles.Find(EGuLiShipAbilitySlot::Formation);
	const FGameplayAbilitySpecHandle* BasicHandle =
		PersistentActiveHandles.Find(EGuLiShipAbilitySlot::BasicWeapon);
	const FGuLiShipAbilityGrant* FormationGrant = FormationHandle ? GrantsByHandle.Find(*FormationHandle) : nullptr;
	const FGuLiShipAbilityGrant* BasicGrant = BasicHandle ? GrantsByHandle.Find(*BasicHandle) : nullptr;
	const FGuLiShipAbilityGrant* MissileGrant = FindConfiguredGrant(EGuLiShipAbilitySlot::Missile);
	const UGuLiWingmanFormationDefinition* FormationDefinition =
		FormationGrant ? FormationGrant->FormationDefinition.Get() : nullptr;
	const UGuLiWingmanWeaponDefinition* BasicDefinition =
		BasicGrant ? BasicGrant->WeaponDefinition.Get() : nullptr;
	const UGuLiWingmanWeaponDefinition* MissileDefinition =
		MissileGrant ? MissileGrant->WeaponDefinition.Get() : nullptr;

	OutSnapshot.bGroupAbilitiesValid = FormationGrant && BasicGrant && MissileGrant
		&& FormationDefinition && BasicDefinition && MissileDefinition
		&& FormationGrant->Slot == EGuLiShipAbilitySlot::Formation
		&& BasicGrant->Slot == EGuLiShipAbilitySlot::BasicWeapon
		&& MissileGrant->Slot == EGuLiShipAbilitySlot::Missile;
	if (OutSnapshot.bGroupAbilitiesValid)
	{
		OutSnapshot.FormationAbilityId = FormationGrant->AbilityId;
		OutSnapshot.BasicWeaponAbilityId = BasicGrant->AbilityId;
		OutSnapshot.MissileAbilityId = MissileGrant->AbilityId;
		OutSnapshot.FormationDefinitionRevision = FormationGrant->GetDefinitionRevision();
		OutSnapshot.FormationDefinitionChecksum = FormationGrant->GetDefinitionChecksum();
		OutSnapshot.BasicWeaponDefinitionRevision = BasicGrant->GetDefinitionRevision();
		OutSnapshot.BasicWeaponDefinitionChecksum = BasicGrant->GetDefinitionChecksum();
		OutSnapshot.MissileDefinitionRevision = MissileGrant->GetDefinitionRevision();
		OutSnapshot.MissileDefinitionChecksum = MissileGrant->GetDefinitionChecksum();

		FGuLiWingmanFormationRuntimeConfig& Runtime = OutSnapshot.FormationRuntime;
		Runtime.InnerRingSlots = FormationDefinition->InnerRingSlots;
		Runtime.OuterRingSlots = FormationDefinition->OuterRingSlots;
		Runtime.InnerRingRadiusCentimeters = FormationDefinition->InnerRingRadiusCentimeters;
		Runtime.OuterRingRadiusCentimeters = FormationDefinition->OuterRingRadiusCentimeters;
		Runtime.InnerRingHeightCentimeters = FormationDefinition->InnerRingHeightCentimeters;
		Runtime.OuterRingHeightCentimeters = FormationDefinition->OuterRingHeightCentimeters;
		Runtime.InnerAngularSpeedRadiansPerSecond = FormationDefinition->InnerAngularSpeedRadiansPerSecond;
		Runtime.OuterAngularSpeedRadiansPerSecond = FormationDefinition->OuterAngularSpeedRadiansPerSecond;
		Runtime.MinimumSpeedCentimetersPerSecond = FormationDefinition->MinimumFlightSpeedCentimetersPerSecond;
		Runtime.CruiseSpeedCentimetersPerSecond = FormationDefinition->CruiseFlightSpeedCentimetersPerSecond;
		Runtime.CatchUpSpeedCentimetersPerSecond = FormationDefinition->CatchUpFlightSpeedCentimetersPerSecond;
		Runtime.MaximumAccelerationCentimetersPerSecondSquared =
			FormationDefinition->MaximumAccelerationCentimetersPerSecondSquared;
		Runtime.MaximumDecelerationCentimetersPerSecondSquared =
			FormationDefinition->MaximumDecelerationCentimetersPerSecondSquared;
		Runtime.MaximumTurnRateDegreesPerSecond = FormationDefinition->MaximumTurnRateDegreesPerSecond;
		Runtime.MaximumBankDegrees = FormationDefinition->MaximumBankDegrees;
		Runtime.AgentRadiusCentimeters = FormationDefinition->AgentRadiusCentimeters;
		Runtime.SeparationRadiusCentimeters = FormationDefinition->SeparationRadiusCentimeters;
		Runtime.ObstacleLookAheadCentimeters = FormationDefinition->ObstacleLookAheadCentimeters;
		Runtime.CatchUpDistanceCentimeters = FormationDefinition->CatchUpDistanceCentimeters;
		Runtime.RecoveryDistanceCentimeters = FormationDefinition->RecoveryDistanceCentimeters;

		OutSnapshot.BasicWeaponRuntime.RangeCentimeters = BasicDefinition->RangeCentimeters;
		OutSnapshot.BasicWeaponRuntime.CooldownSeconds = BasicDefinition->CooldownSeconds;
		OutSnapshot.BasicWeaponRuntime.TargetConeHalfAngleDegrees = BasicDefinition->TargetConeHalfAngleDegrees;
		OutSnapshot.BasicWeaponRuntime.bRequiresLineOfSight = BasicDefinition->bRequiresLineOfSight;
		OutSnapshot.MissileRuntime.RangeCentimeters = MissileDefinition->RangeCentimeters;
		OutSnapshot.MissileRuntime.CooldownSeconds = MissileDefinition->CooldownSeconds;
		OutSnapshot.MissileRuntime.TargetConeHalfAngleDegrees = MissileDefinition->TargetConeHalfAngleDegrees;
		OutSnapshot.MissileRuntime.bRequiresLineOfSight = MissileDefinition->bRequiresLineOfSight;
	}

	OutSnapshot.RefreshHash();
	return OutSnapshot.IsWellFormed();
}

bool UGuLiShipAbilitySystemComponent::ObserveReplicatedGroupAbilityConfig(
	const FGuLiGroupAbilityConfigSnapshot& Snapshot)
{
	if (IsOwnerActorAuthoritative() || !Snapshot.IsWellFormed()
		|| Snapshot.SnapshotRevision <= ObservedSnapshotRevision)
	{
		return false;
	}
	ObservedSnapshotRevision = Snapshot.SnapshotRevision;
	ObservedAbilitySetRevision = Snapshot.AbilitySetRevision;
	return true;
}

bool UGuLiShipAbilitySystemComponent::ServerTryStartMissileCooldown(const float DurationSeconds)
{
	return ServerTryReserveMissileCooldown(DurationSeconds, FGuid::NewGuid());
}

bool UGuLiShipAbilitySystemComponent::ServerTryReserveMissileCooldown(
	const float DurationSeconds,
	const FGuid& ReservationId)
{
	if (!IsOwnerActorAuthoritative() || !ReservationId.IsValid()
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f
		|| IsMissileCooldownActive())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	SetLooseGameplayTagCount(
		TAG_GuLi_ShipAbility_State_MissileCooldown,
		1,
		EGameplayTagReplicationState::TagOnly);
	MissileCooldownReservationId = ReservationId;
	World->GetTimerManager().SetTimer(
		MissileCooldownTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			MissileCooldownReservationId.Invalidate();
			SetLooseGameplayTagCount(
				TAG_GuLi_ShipAbility_State_MissileCooldown,
				0,
				EGameplayTagReplicationState::TagOnly);
		}),
		DurationSeconds,
		false);
	return true;
}

bool UGuLiShipAbilitySystemComponent::ServerRollbackMissileCooldown(
	const FGuid& ReservationId)
{
	if (!IsOwnerActorAuthoritative() || !ReservationId.IsValid()
		|| MissileCooldownReservationId != ReservationId)
	{
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MissileCooldownTimerHandle);
	}
	MissileCooldownReservationId.Invalidate();
	SetLooseGameplayTagCount(
		TAG_GuLi_ShipAbility_State_MissileCooldown,
		0,
		EGameplayTagReplicationState::TagOnly);
	return true;
}

bool UGuLiShipAbilitySystemComponent::IsMissileCooldownActive() const
{
	return HasMatchingGameplayTag(TAG_GuLi_ShipAbility_State_MissileCooldown);
}

bool UGuLiShipAbilitySystemComponent::IsAbilityGranted(const FGameplayTag AbilityId) const
{
	if (const FGameplayAbilitySpecHandle* Handle = HandlesByAbilityId.Find(AbilityId))
	{
		return FindAbilitySpecFromHandle(*Handle) != nullptr;
	}
	return false;
}

bool UGuLiShipAbilitySystemComponent::IsAbilityConfigurationCurrent(
	const FGameplayTag AbilityId,
	const uint32 ExpectedAbilitySetRevision) const
{
	return ExpectedAbilitySetRevision != 0u
		&& ExpectedAbilitySetRevision == AbilitySetRevision
		&& IsAbilityGranted(AbilityId);
}

const FGuLiShipAbilityGrant* UGuLiShipAbilitySystemComponent::FindConfiguredGrant(
	const FGameplayTag AbilityId) const
{
	const FGameplayAbilitySpecHandle* Handle = HandlesByAbilityId.Find(AbilityId);
	return Handle ? GrantsByHandle.Find(*Handle) : nullptr;
}

const FGuLiShipAbilityGrant* UGuLiShipAbilitySystemComponent::FindConfiguredGrant(
	const EGuLiShipAbilitySlot Slot) const
{
	for (const TPair<FGameplayAbilitySpecHandle, FGuLiShipAbilityGrant>& Pair : GrantsByHandle)
	{
		if (Pair.Value.Slot == Slot && FindAbilitySpecFromHandle(Pair.Key))
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

const UGuLiWingmanFormationDefinition* UGuLiShipAbilitySystemComponent::GetActiveFormationDefinition() const
{
	const FGameplayAbilitySpecHandle* Handle = PersistentActiveHandles.Find(EGuLiShipAbilitySlot::Formation);
	const FGuLiShipAbilityGrant* Grant = Handle ? GrantsByHandle.Find(*Handle) : nullptr;
	return Grant ? Grant->FormationDefinition.Get() : nullptr;
}

const UGuLiWingmanWeaponDefinition* UGuLiShipAbilitySystemComponent::GetBasicWeaponDefinition() const
{
	const FGameplayAbilitySpecHandle* Handle = PersistentActiveHandles.Find(EGuLiShipAbilitySlot::BasicWeapon);
	const FGuLiShipAbilityGrant* Grant = Handle ? GrantsByHandle.Find(*Handle) : nullptr;
	return Grant ? Grant->WeaponDefinition.Get() : nullptr;
}

const UGuLiWingmanWeaponDefinition* UGuLiShipAbilitySystemComponent::GetMissileDefinition() const
{
	const FGuLiShipAbilityGrant* Grant = FindConfiguredGrant(EGuLiShipAbilitySlot::Missile);
	return Grant ? Grant->WeaponDefinition.Get() : nullptr;
}

bool UGuLiShipAbilitySystemComponent::CanActivateConfiguredAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayTag AbilityId,
	const EGuLiShipAbilitySlot Slot,
	const EGuLiShipAbilityActivationPolicy ActivationPolicy) const
{
	if (!Handle.IsValid() || !AbilityId.IsValid() || Slot == EGuLiShipAbilitySlot::None)
	{
		return false;
	}
	if (Slot == EGuLiShipAbilitySlot::Missile && IsMissileCooldownActive())
	{
		return false;
	}

	if (const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle))
	{
		if (Grant->AbilityId != AbilityId || Grant->Slot != Slot)
		{
			return false;
		}
	}
	else
	{
		// Owning clients reconstruct input authorization from the replicated spec;
		// the reliable GroupAbilityConfig remains the network version authority.
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		const UGuLiShipGameplayAbility* ShipAbility = Spec && Spec->Ability
			? Cast<UGuLiShipGameplayAbility>(Spec->Ability)
			: nullptr;
		if (IsOwnerActorAuthoritative() || !ShipAbility
			|| ShipAbility->GetStableAbilityId() != AbilityId
			|| ShipAbility->GetShipAbilitySlot() != Slot)
		{
			return false;
		}
	}

	if (ActivationPolicy == EGuLiShipAbilityActivationPolicy::OnInputTriggered)
	{
		return bActiveAbilityInputEnabled;
	}
	if (const FGameplayAbilitySpecHandle* ActiveHandle = PersistentActiveHandles.Find(Slot))
	{
		return *ActiveHandle == Handle;
	}
	return IsOwnerActorAuthoritative();
}

bool UGuLiShipAbilitySystemComponent::HandleConfiguredAbilityActivated(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActivationInfo& ActivationInfo)
{
	const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle);
	if (!Grant)
	{
		// Client-side triggered abilities are authorized from their replicated spec.
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		const UGuLiShipGameplayAbility* Ability = Spec && Spec->Ability
			? Cast<UGuLiShipGameplayAbility>(Spec->Ability)
			: nullptr;
		return !IsOwnerActorAuthoritative()
			&& bActiveAbilityInputEnabled
			&& Ability
			&& Ability->GetShipActivationPolicy() == EGuLiShipAbilityActivationPolicy::OnInputTriggered;
	}

	if (Grant->Slot == EGuLiShipAbilitySlot::Missile)
	{
		return bActiveAbilityInputEnabled && !IsMissileCooldownActive()
			&& GetActiveFormationDefinition() && GetBasicWeaponDefinition();
	}

	if (!GuLiIsPersistentShipAbilitySlot(Grant->Slot))
	{
		return false;
	}
	if (const FGameplayAbilitySpecHandle* ActiveHandle = PersistentActiveHandles.Find(Grant->Slot))
	{
		return *ActiveHandle == Handle;
	}
	PersistentActiveHandles.Add(Grant->Slot, Handle);
	MarkProjectionChanged();
	return true;
}

void UGuLiShipAbilitySystemComponent::HandleConfiguredAbilityEnded(const FGameplayAbilitySpecHandle Handle)
{
	const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle);
	if (!Grant)
	{
		return;
	}
	const FGameplayAbilitySpecHandle* ActiveHandle = PersistentActiveHandles.Find(Grant->Slot);
	if (ActiveHandle && *ActiveHandle == Handle)
	{
		PersistentActiveHandles.Remove(Grant->Slot);
		MarkProjectionChanged();
	}
}

void UGuLiShipAbilitySystemComponent::BroadcastTriggeredAbilityAuthorization(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayTag AbilityId,
	const FGameplayAbilityActivationInfo& ActivationInfo)
{
	const uint32 EffectiveRevision = IsOwnerActorAuthoritative()
		? AbilitySetRevision
		: ObservedAbilitySetRevision;
	if (EffectiveRevision == 0u)
	{
		return;
	}
	const bool bLocallyPredicted =
		ActivationInfo.ActivationMode == EGameplayAbilityActivationMode::Predicting;
	TriggeredAbilityDelegate.Broadcast(Handle, AbilityId, EffectiveRevision, bLocallyPredicted);
}

bool UGuLiShipAbilitySystemComponent::HasIntactAppliedGrantState(const uint64 ExpectedChecksum) const
{
	if (ExpectedChecksum == 0u || HandlesByAbilityId.Num() != 3 || GrantsByHandle.Num() != 3)
	{
		return false;
	}
	for (const TPair<FGameplayTag, FGameplayAbilitySpecHandle>& Pair : HandlesByAbilityId)
	{
		if (!Pair.Value.IsValid() || !GrantsByHandle.Contains(Pair.Value) || !FindAbilitySpecFromHandle(Pair.Value))
		{
			return false;
		}
	}
	return true;
}

void UGuLiShipAbilitySystemComponent::ClearGrantedAbilitiesInternal()
{
	TArray<FGameplayAbilitySpecHandle> Handles;
	HandlesByAbilityId.GenerateValueArray(Handles);
	for (const FGameplayAbilitySpecHandle Handle : Handles)
	{
		if (Handle.IsValid())
		{
			CancelAbilityHandle(Handle);
			ClearAbility(Handle);
		}
	}
	PersistentActiveHandles.Reset();
	GrantsByHandle.Reset();
	HandlesByAbilityId.Reset();
}

void UGuLiShipAbilitySystemComponent::TryActivatePersistentAbilities()
{
	if (!IsOwnerActorAuthoritative())
	{
		return;
	}
	for (const TPair<FGameplayAbilitySpecHandle, FGuLiShipAbilityGrant>& Pair : GrantsByHandle)
	{
		if (GuLiIsPersistentShipAbilitySlot(Pair.Value.Slot)
			&& !PersistentActiveHandles.Contains(Pair.Value.Slot))
		{
			TryActivateAbility(Pair.Key, false);
		}
	}
}

void UGuLiShipAbilitySystemComponent::MarkProjectionChanged()
{
	if (ProjectionMutationDepth > 0u)
	{
		bProjectionMutationDirty = true;
		return;
	}
	ProjectionSnapshotRevision = AdvanceRevision(ProjectionSnapshotRevision);
	ProjectionChangedDelegate.Broadcast();
}

void UGuLiShipAbilitySystemComponent::BeginProjectionMutation()
{
	++ProjectionMutationDepth;
}

void UGuLiShipAbilitySystemComponent::EndProjectionMutation()
{
	check(ProjectionMutationDepth > 0u);
	--ProjectionMutationDepth;
	if (ProjectionMutationDepth == 0u && bProjectionMutationDirty)
	{
		bProjectionMutationDirty = false;
		ProjectionSnapshotRevision = AdvanceRevision(ProjectionSnapshotRevision);
		ProjectionChangedDelegate.Broadcast();
	}
}

uint32 UGuLiShipAbilitySystemComponent::AdvanceRevision(uint32 Revision)
{
	++Revision;
	return Revision == 0u ? 1u : Revision;
}
