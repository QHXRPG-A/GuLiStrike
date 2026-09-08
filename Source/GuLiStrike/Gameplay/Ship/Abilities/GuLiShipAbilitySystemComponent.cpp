// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Ship/Abilities/GuLiShipGameplayAbility.h"

namespace
{
	const FName DefaultMissileCooldownGroupId(TEXT("WingmanMissileSalvo"));

	bool Fail(FString& OutError, const FString& Message)
	{
		OutError = Message;
		return false;
	}

	uint32 DeriveFormationSeed(
		const FGuLiShipAbilityProjectionContext& Context,
		const FGameplayTag FormationAbilityId)
	{
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddString(Hash, TEXT("GuLi.Wingman.FormationSeed.v1"));
		GuLiShipAbilityHash::AddUInt32(Hash, Context.ShipInstanceId.A);
		GuLiShipAbilityHash::AddUInt32(Hash, Context.ShipInstanceId.B);
		GuLiShipAbilityHash::AddUInt32(Hash, Context.ShipInstanceId.C);
		GuLiShipAbilityHash::AddUInt32(Hash, Context.ShipInstanceId.D);
		GuLiShipAbilityHash::AddUInt32(Hash, Context.ShipGeneration);
		GuLiShipAbilityHash::AddUInt32(Hash, Context.GroupGeneration);
		GuLiShipAbilityHash::AddTag(Hash, FormationAbilityId);
		Hash = GuLiShipAbilityHash::Finish(Hash);
		const uint32 Folded = static_cast<uint32>(Hash) ^ static_cast<uint32>(Hash >> 32u);
		return Folded == 0u ? 1u : Folded;
	}

	void CopyWeaponRuntime(
		const UGuLiWingmanWeaponDefinition& Definition,
		FGuLiWingmanWeaponRuntimeConfig& OutRuntime)
	{
		if (!Definition.BuildRuntimeConfig(OutRuntime)) OutRuntime.Damage = 0.0f;
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
	WeaponLoadoutRevision = AdvanceRevision(WeaponLoadoutRevision);

	for (const FGuLiShipAbilityGrant& Grant : OrderedGrants)
	{
		FGameplayAbilitySpec Spec(Grant.AbilityClass, Grant.AbilityLevel, INDEX_NONE, AbilitySet);
		// A grant's catalog identity may differ from its reusable GA class CDO ID.
		Spec.GetDynamicSpecSourceTags().AddTag(Grant.AbilityId);
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
	RebuildWeaponBindingIndex();

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
		&& WeaponCooldownsByGroup.IsEmpty() && !IsMissileCooldownActive())
	{
		return;
	}

	BeginProjectionMutation();
	if (UWorld* World = GetWorld())
	{
		for (TPair<FName, FWeaponCooldownState>& Pair : WeaponCooldownsByGroup)
		{
			World->GetTimerManager().ClearTimer(Pair.Value.TimerHandle);
		}
	}
	WeaponCooldownsByGroup.Reset();
	SetLooseGameplayTagCount(
		TAG_GuLi_ShipAbility_State_MissileCooldown,
		0,
		EGameplayTagReplicationState::TagOnly);
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

	FGameplayAbilitySpec* MatchedSpec = nullptr;
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}
		if (MatchedSpec)
		{
			// Shared tags are legal catalog metadata, but are not an implicit
			// multi-channel command. Call AbilityWeaponBindingPressed instead.
			return;
		}
		MatchedSpec = &Spec;
	}
	if (MatchedSpec)
	{
		MatchedSpec->InputPressed = true;
		AbilitySpecInputPressed(*MatchedSpec);
		if (!MatchedSpec->IsActive())
		{
			TryActivateAbility(MatchedSpec->Handle);
		}
	}
}

void UGuLiShipAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}
	FGameplayAbilitySpec* MatchedSpec = nullptr;
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}
		if (MatchedSpec)
		{
			return;
		}
		MatchedSpec = &Spec;
	}
	if (MatchedSpec)
	{
		MatchedSpec->InputPressed = false;
		AbilitySpecInputReleased(*MatchedSpec);
	}
}

bool UGuLiShipAbilitySystemComponent::AbilityWeaponBindingPressed(
	const FGuLiWeaponBindingKey& Binding)
{
	if (!bActiveAbilityInputEnabled || !Binding.IsWellFormed())
	{
		return false;
	}
	const FGameplayAbilitySpecHandle Handle = FindSpecHandleForBinding(Binding);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? FindAbilitySpecFromHandle(Handle) : nullptr;
	if (!Spec || !Spec->Ability)
	{
		return false;
	}
	Spec->InputPressed = true;
	AbilitySpecInputPressed(*Spec);
	return Spec->IsActive() || TryActivateAbility(Handle);
}

bool UGuLiShipAbilitySystemComponent::AbilityWeaponBindingReleased(
	const FGuLiWeaponBindingKey& Binding)
{
	const FGameplayAbilitySpecHandle Handle = FindSpecHandleForBinding(Binding);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? FindAbilitySpecFromHandle(Handle) : nullptr;
	if (!Spec || !Spec->Ability)
	{
		return false;
	}
	Spec->InputPressed = false;
	AbilitySpecInputReleased(*Spec);
	return true;
}

bool UGuLiShipAbilitySystemComponent::SetProjectionContext(
	const FGuLiShipAbilityProjectionContext& NewContext)
{
	if (!NewContext.IsWellFormed() || ProjectionContext == NewContext)
	{
		return false;
	}
	ProjectionContext = NewContext;
	RebuildWeaponBindingIndex();
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
	OutSnapshot.MatchEpoch = ProjectionContext.MatchEpoch;
	OutSnapshot.Team = ProjectionContext.Team;
	OutSnapshot.OwnerPlayerGuid = ProjectionContext.OwnerPlayerGuid;
	OutSnapshot.WingmanTypeId = ProjectionContext.WingmanTypeId;
	OutSnapshot.ShipGeneration = ProjectionContext.ShipGeneration;
	OutSnapshot.GroupGeneration = ProjectionContext.GroupGeneration;
	OutSnapshot.AbilitySetRevision = AbilitySetRevision;
	OutSnapshot.LoadoutRevision = WeaponLoadoutRevision;
	OutSnapshot.SnapshotRevision = ProjectionSnapshotRevision;
	OutSnapshot.FormationCommandRevision = ProjectionContext.FormationCommandRevision;
	OutSnapshot.EffectiveClientSimTick = ProjectionContext.EffectiveClientSimTick;

	const FGuLiShipAbilityGrant* FormationGrant = nullptr;
	for (const FGameplayAbilitySpecHandle Handle : PersistentActiveHandles)
	{
		const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle);
		if (Grant && Grant->Slot == EGuLiShipAbilitySlot::Formation)
		{
			FormationGrant = Grant;
			break;
		}
	}
	const UGuLiWingmanFormationDefinition* FormationDefinition =
		FormationGrant ? FormationGrant->FormationDefinition.Get() : nullptr;

	OutSnapshot.bGroupAbilitiesValid = AbilitySetRevision != 0u && WeaponLoadoutRevision != 0u
		&& FormationGrant && FormationDefinition
		&& FormationGrant->Slot == EGuLiShipAbilitySlot::Formation;
	if (OutSnapshot.bGroupAbilitiesValid)
	{
		const uint32 FormationSeed = DeriveFormationSeed(
			ProjectionContext, FormationGrant->AbilityId);
		OutSnapshot.bGroupAbilitiesValid = FormationDefinition->BuildRuntimeConfig(
			FormationSeed, OutSnapshot.FormationRuntime);
	}
	if (OutSnapshot.bGroupAbilitiesValid)
	{
		OutSnapshot.FormationAbilityId = FormationGrant->AbilityId;
		OutSnapshot.FormationDefinitionRevision = FormationGrant->GetDefinitionRevision();
		OutSnapshot.FormationDefinitionChecksum = FormationGrant->GetDefinitionChecksum();

		TArray<const FGuLiShipAbilityGrant*> WeaponGrants;
		for (const TPair<FGameplayAbilitySpecHandle, FGuLiShipAbilityGrant>& Pair : GrantsByHandle)
		{
			if (Pair.Value.Slot != EGuLiShipAbilitySlot::Formation)
			{
				WeaponGrants.Add(&Pair.Value);
			}
		}
		WeaponGrants.Sort([](const FGuLiShipAbilityGrant& Lhs, const FGuLiShipAbilityGrant& Rhs)
		{
			const FName LhsSlot = Lhs.GetEffectiveWeaponSlotId();
			const FName RhsSlot = Rhs.GetEffectiveWeaponSlotId();
			return LhsSlot != RhsSlot ? LhsSlot.LexicalLess(RhsSlot)
				: Lhs.AbilityId.GetTagName().LexicalLess(Rhs.AbilityId.GetTagName());
		});
		for (const FGuLiShipAbilityGrant* Grant : WeaponGrants)
		{
			const UGuLiWingmanWeaponDefinition* Definition = Grant ? Grant->WeaponDefinition.Get() : nullptr;
			const FGuLiWeaponBindingKey Binding = Grant ? BuildWeaponBinding(*Grant) : FGuLiWeaponBindingKey{};
			const bool bPersistentReady = Grant
				&& (!GuLiIsPersistentShipAbilitySlot(Grant->Slot)
					|| (HandlesByAbilityId.Contains(Grant->AbilityId)
						&& PersistentActiveHandles.Contains(HandlesByAbilityId.FindChecked(Grant->AbilityId))));
			if (!Grant || !Definition || !Binding.IsWellFormed() || !bPersistentReady)
			{
				OutSnapshot.bGroupAbilitiesValid = false;
				break;
			}
			FGuLiWingmanWeaponChannelConfig& Channel = OutSnapshot.WeaponChannels.AddDefaulted_GetRef();
			Channel.Binding = Binding;
			Channel.SkillId = Grant->GetEffectiveSkillId();
			Channel.AbilityId = Grant->AbilityId;
			Channel.Kind = Definition->Kind;
			Channel.CooldownGroupId = Grant->GetEffectiveCooldownGroupId();
			Channel.bEnabled = true;
			Channel.ProfileRevision = Grant->ProfileRevision;
			Channel.DefinitionRevision = Grant->GetDefinitionRevision();
			Channel.DefinitionChecksum = Grant->GetDefinitionChecksum();
			CopyWeaponRuntime(*Definition, Channel.Runtime);
			if (!Channel.IsWellFormed())
			{
				OutSnapshot.bGroupAbilitiesValid = false;
				break;
			}

			// Transitional mirrors keep current HUD/presentation consumers working;
			// validators and combat truth use WeaponChannels exclusively in v9.
			if (Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic
				&& !OutSnapshot.BasicWeaponAbilityId.IsValid())
			{
				OutSnapshot.BasicWeaponAbilityId = Channel.AbilityId;
				OutSnapshot.BasicWeaponDefinitionRevision = Channel.DefinitionRevision;
				OutSnapshot.BasicWeaponDefinitionChecksum = Channel.DefinitionChecksum;
				OutSnapshot.BasicWeaponRuntime = Channel.Runtime;
			}
			else if (Channel.Kind == EGuLiWingmanWeaponKind::Missile
				&& !OutSnapshot.MissileAbilityId.IsValid())
			{
				OutSnapshot.MissileAbilityId = Channel.AbilityId;
				OutSnapshot.MissileDefinitionRevision = Channel.DefinitionRevision;
				OutSnapshot.MissileDefinitionChecksum = Channel.DefinitionChecksum;
				OutSnapshot.MissileRuntime = Channel.Runtime;
			}
		}
	}
	if (!OutSnapshot.bGroupAbilitiesValid)
	{
		OutSnapshot.AbilitySetRevision = AbilitySetRevision;
		OutSnapshot.LoadoutRevision = 0u;
		OutSnapshot.FormationAbilityId = FGameplayTag();
		OutSnapshot.BasicWeaponAbilityId = FGameplayTag();
		OutSnapshot.MissileAbilityId = FGameplayTag();
		OutSnapshot.WeaponChannels.Reset();
		OutSnapshot.FormationDefinitionRevision = 0u;
		OutSnapshot.FormationDefinitionChecksum = 0u;
		OutSnapshot.BasicWeaponDefinitionRevision = 0u;
		OutSnapshot.BasicWeaponDefinitionChecksum = 0u;
		OutSnapshot.MissileDefinitionRevision = 0u;
		OutSnapshot.MissileDefinitionChecksum = 0u;
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
	ObservedGroupAbilityConfig = Snapshot;
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
	return ServerTryReserveWeaponCooldown(
		DefaultMissileCooldownGroupId, DurationSeconds, ReservationId);
}

bool UGuLiShipAbilitySystemComponent::ServerRollbackMissileCooldown(
	const FGuid& ReservationId)
{
	return ServerRollbackWeaponCooldown(DefaultMissileCooldownGroupId, ReservationId);
}

bool UGuLiShipAbilitySystemComponent::IsMissileCooldownActive() const
{
	return IsWeaponCooldownActive(DefaultMissileCooldownGroupId);
}

bool UGuLiShipAbilitySystemComponent::ServerTryReserveWeaponCooldown(
	const FName CooldownGroupId,
	const float DurationSeconds,
	const FGuid& ReservationId)
{
	if (!IsOwnerActorAuthoritative() || CooldownGroupId.IsNone() || !ReservationId.IsValid()
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f
		|| IsWeaponCooldownActive(CooldownGroupId))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FWeaponCooldownState& State = WeaponCooldownsByGroup.FindOrAdd(CooldownGroupId);
	State.ReservationId = ReservationId;
	if (CooldownGroupId == DefaultMissileCooldownGroupId)
	{
		// Preserve the original replicated HUD/owner feedback for the built-in salvo.
		SetLooseGameplayTagCount(
			TAG_GuLi_ShipAbility_State_MissileCooldown,
			1,
			EGameplayTagReplicationState::TagOnly);
	}
	World->GetTimerManager().SetTimer(
		State.TimerHandle,
		FTimerDelegate::CreateWeakLambda(
			this,
			[this, CooldownGroupId, ReservationId]()
			{
				const FWeaponCooldownState* Current = WeaponCooldownsByGroup.Find(CooldownGroupId);
				if (!Current || Current->ReservationId != ReservationId)
				{
					return;
				}
				WeaponCooldownsByGroup.Remove(CooldownGroupId);
				if (CooldownGroupId == DefaultMissileCooldownGroupId)
				{
					SetLooseGameplayTagCount(
						TAG_GuLi_ShipAbility_State_MissileCooldown,
						0,
						EGameplayTagReplicationState::TagOnly);
				}
			}),
		DurationSeconds,
		false);
	return true;
}

bool UGuLiShipAbilitySystemComponent::ServerRollbackWeaponCooldown(
	const FName CooldownGroupId,
	const FGuid& ReservationId)
{
	if (!IsOwnerActorAuthoritative() || CooldownGroupId.IsNone() || !ReservationId.IsValid())
	{
		return false;
	}
	FWeaponCooldownState* State = WeaponCooldownsByGroup.Find(CooldownGroupId);
	if (!State || State->ReservationId != ReservationId)
	{
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(State->TimerHandle);
	}
	WeaponCooldownsByGroup.Remove(CooldownGroupId);
	if (CooldownGroupId == DefaultMissileCooldownGroupId)
	{
		SetLooseGameplayTagCount(
			TAG_GuLi_ShipAbility_State_MissileCooldown,
			0,
			EGameplayTagReplicationState::TagOnly);
	}
	return true;
}

bool UGuLiShipAbilitySystemComponent::IsWeaponCooldownActive(const FName CooldownGroupId) const
{
	if (CooldownGroupId.IsNone())
	{
		return false;
	}
	if (const FWeaponCooldownState* State = WeaponCooldownsByGroup.Find(CooldownGroupId))
	{
		if (State->ReservationId.IsValid())
		{
			return true;
		}
	}
	// Only the built-in group has a legacy replicated tag. Custom groups remain
	// server-authoritative until a later UI projection consumes the channel ledger.
	return CooldownGroupId == DefaultMissileCooldownGroupId
		&& HasMatchingGameplayTag(TAG_GuLi_ShipAbility_State_MissileCooldown);
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

const FGuLiShipAbilityGrant* UGuLiShipAbilitySystemComponent::FindConfiguredGrant(
	const FGuLiWeaponBindingKey& Binding) const
{
	const FGameplayAbilitySpecHandle* Handle = HandlesByWeaponBinding.Find(Binding);
	return Handle && FindAbilitySpecFromHandle(*Handle) ? GrantsByHandle.Find(*Handle) : nullptr;
}

bool UGuLiShipAbilitySystemComponent::IsWeaponConfigurationCurrent(
	const FGuLiWeaponBindingKey& Binding,
	const FName SkillId,
	const uint32 ExpectedLoadoutRevision,
	const uint32 ExpectedProfileRevision) const
{
	const FGuLiShipAbilityGrant* Grant = FindConfiguredGrant(Binding);
	return Grant && ExpectedLoadoutRevision != 0u
		&& ExpectedLoadoutRevision == WeaponLoadoutRevision
		&& ExpectedProfileRevision != 0u
		&& ExpectedProfileRevision == Grant->ProfileRevision
		&& SkillId == Grant->GetEffectiveSkillId();
}

const UGuLiWingmanFormationDefinition* UGuLiShipAbilitySystemComponent::GetActiveFormationDefinition() const
{
	for (const FGameplayAbilitySpecHandle Handle : PersistentActiveHandles)
	{
		const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle);
		if (Grant && Grant->Slot == EGuLiShipAbilitySlot::Formation)
		{
			return Grant->FormationDefinition.Get();
		}
	}
	return nullptr;
}

const UGuLiWingmanWeaponDefinition* UGuLiShipAbilitySystemComponent::GetBasicWeaponDefinition() const
{
	for (const FGameplayAbilitySpecHandle Handle : PersistentActiveHandles)
	{
		const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle);
		if (Grant && Grant->Slot == EGuLiShipAbilitySlot::BasicWeapon
			&& Grant->GetEffectiveWeaponSlotId() == GuLiGetDefaultWeaponSlotId(EGuLiShipAbilitySlot::BasicWeapon))
		{
			return Grant->WeaponDefinition.Get();
		}
	}
	return nullptr;
}

const UGuLiWingmanWeaponDefinition* UGuLiShipAbilitySystemComponent::GetMissileDefinition() const
{
	for (const TPair<FGameplayAbilitySpecHandle, FGuLiShipAbilityGrant>& Pair : GrantsByHandle)
	{
		if (Pair.Value.Slot == EGuLiShipAbilitySlot::Missile
			&& Pair.Value.GetEffectiveWeaponSlotId() == GuLiGetDefaultWeaponSlotId(EGuLiShipAbilitySlot::Missile)
			&& FindAbilitySpecFromHandle(Pair.Key))
		{
			return Pair.Value.WeaponDefinition.Get();
		}
	}
	return nullptr;
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
	if (const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle))
	{
		if (Grant->Slot != Slot)
		{
			return false;
		}
		if (Slot == EGuLiShipAbilitySlot::Missile
			&& IsWeaponCooldownActive(Grant->GetEffectiveCooldownGroupId()))
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
		const FGuLiWingmanWeaponChannelConfig* ObservedChannel = FindObservedChannelForHandle(Handle);
		if (IsOwnerActorAuthoritative() || !ShipAbility
		|| ShipAbility->GetShipAbilitySlot() != Slot
		|| (Slot != EGuLiShipAbilitySlot::Formation && !ObservedChannel))
		{
			return false;
		}
		if (Slot == EGuLiShipAbilitySlot::Missile
			&& IsWeaponCooldownActive(ObservedChannel->CooldownGroupId))
		{
			return false;
		}
	}

	if (ActivationPolicy == EGuLiShipAbilityActivationPolicy::OnInputTriggered)
	{
		return bActiveAbilityInputEnabled;
	}
	if (PersistentActiveHandles.Contains(Handle)) return true;
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
		return bActiveAbilityInputEnabled
			&& !IsWeaponCooldownActive(Grant->GetEffectiveCooldownGroupId())
			&& GetActiveFormationDefinition() && BuildWeaponBinding(*Grant).IsWellFormed();
	}

	if (!GuLiIsPersistentShipAbilitySlot(Grant->Slot))
	{
		return false;
	}
	if (PersistentActiveHandles.Contains(Handle)) return true;
	PersistentActiveHandles.Add(Handle);
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
	if (PersistentActiveHandles.Remove(Handle) > 0)
	{
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
	FGameplayTag EffectiveAbilityId = AbilityId;
	FGuLiWeaponBindingKey Binding;
	FName SkillId;
	if (const FGuLiShipAbilityGrant* Grant = GrantsByHandle.Find(Handle))
	{
		EffectiveAbilityId = Grant->AbilityId;
		Binding = BuildWeaponBinding(*Grant);
		SkillId = Grant->GetEffectiveSkillId();
	}
	else if (const FGuLiWingmanWeaponChannelConfig* Channel = FindObservedChannelForHandle(Handle))
	{
		EffectiveAbilityId = Channel->AbilityId;
		Binding = Channel->Binding;
		SkillId = Channel->SkillId;
	}
	TriggeredAbilityDelegate.Broadcast(Handle, EffectiveAbilityId, EffectiveRevision, bLocallyPredicted);
	if (Binding.IsWellFormed() && !SkillId.IsNone())
	{
		WeaponAbilityDelegate.Broadcast(
			Handle, Binding, SkillId, EffectiveAbilityId, EffectiveRevision, bLocallyPredicted);
	}
}

bool UGuLiShipAbilitySystemComponent::HasIntactAppliedGrantState(const uint64 ExpectedChecksum) const
{
	if (ExpectedChecksum == 0u || HandlesByAbilityId.Num() != AppliedLoadout.AbilityIds.Num()
		|| GrantsByHandle.Num() != AppliedLoadout.AbilityIds.Num())
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
	HandlesByWeaponBinding.Reset();
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
			&& !PersistentActiveHandles.Contains(Pair.Key))
		{
			TryActivateAbility(Pair.Key, false);
		}
	}
}

FGuLiWeaponBindingKey UGuLiShipAbilitySystemComponent::BuildWeaponBinding(
	const FGuLiShipAbilityGrant& Grant) const
{
	if (!ProjectionContext.IsWellFormed() || Grant.Slot == EGuLiShipAbilitySlot::Formation)
	{
		return FGuLiWeaponBindingKey{};
	}
	return FGuLiWeaponBindingKey::Wingman(
		ProjectionContext.MatchEpoch,
		ProjectionContext.Team,
		ProjectionContext.OwnerPlayerGuid,
		ProjectionContext.WingmanTypeId,
		Grant.GetEffectiveWeaponSlotId());
}

void UGuLiShipAbilitySystemComponent::RebuildWeaponBindingIndex()
{
	HandlesByWeaponBinding.Reset();
	if (!ProjectionContext.IsWellFormed())
	{
		return;
	}
	for (const TPair<FGameplayAbilitySpecHandle, FGuLiShipAbilityGrant>& Pair : GrantsByHandle)
	{
		const FGuLiWeaponBindingKey Binding = BuildWeaponBinding(Pair.Value);
		if (Binding.IsWellFormed())
		{
			HandlesByWeaponBinding.Add(Binding, Pair.Key);
		}
	}
}

FGameplayAbilitySpecHandle UGuLiShipAbilitySystemComponent::FindSpecHandleForBinding(
	const FGuLiWeaponBindingKey& Binding) const
{
	if (const FGameplayAbilitySpecHandle* Handle = HandlesByWeaponBinding.Find(Binding))
	{
		return *Handle;
	}
	const FGuLiWingmanWeaponChannelConfig* Channel =
		ObservedGroupAbilityConfig.FindWeaponChannel(Binding);
	if (!Channel || !Channel->bEnabled)
	{
		return FGameplayAbilitySpecHandle{};
	}
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(Channel->AbilityId))
		{
			return Spec.Handle;
		}
	}
	return FGameplayAbilitySpecHandle{};
}

const FGuLiWingmanWeaponChannelConfig*
UGuLiShipAbilitySystemComponent::FindObservedChannelForHandle(
	const FGameplayAbilitySpecHandle Handle) const
{
	const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
	if (!Spec || !Spec->Ability || !ObservedGroupAbilityConfig.IsUsableByLeaseOwner())
	{
		return nullptr;
	}
	return ObservedGroupAbilityConfig.WeaponChannels.FindByPredicate(
		[Spec](const FGuLiWingmanWeaponChannelConfig& Channel)
		{
			return Channel.bEnabled
				&& Spec->GetDynamicSpecSourceTags().HasTagExact(Channel.AbilityId);
		});
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
