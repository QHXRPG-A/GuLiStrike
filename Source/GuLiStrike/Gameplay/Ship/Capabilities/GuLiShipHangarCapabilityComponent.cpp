#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"

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

UGuLiShipHangarCapabilityComponent::UGuLiShipHangarCapabilityComponent() = default;

bool UGuLiShipHangarCapabilityComponent::RequestActivation(FName ActionId, FString& Error)
{
	for (const auto& Entry : GrantsById)
	{
		const auto& Grant = Entry.Value;
		if (Grant.GetEffectiveSkillId() != ActionId && Grant.AbilityId.GetTagName() != ActionId) continue;
		if (Grant.InputTag.IsValid() && AbilityWeaponBindingPressed(BuildWeaponBinding(Grant))) return true;
		Error = TEXT("Hangar action is automatic, disabled, or cooling down."); return false;
	}
	Error = TEXT("Hangar has no action with this identity."); return false;
}

bool UGuLiShipHangarCapabilityComponent::ValidateConfiguration(const UDataAsset* Configuration, FString& Error) const
{
	const auto* Definition = Cast<UGuLiShipHangarDefinition>(Configuration);
	if (!Definition || !Definition->AbilitySet)
	{
		Error = TEXT("Hangar requires a definition and an authored Wingman ability set.");
		return false;
	}
	TArray<FGuLiShipAbilityGrant> Grants;
	return Definition->AbilitySet->ResolveLoadout(Definition->Loadout, Grants, &Error);
}

void UGuLiShipHangarCapabilityComponent::OnPrepared()
{
	const auto& Definition = *CastChecked<UGuLiShipHangarDefinition>(CapabilityConfiguration);
	AppliedAbilitySet = Definition.AbilitySet;
	AppliedLoadout = Definition.Loadout;
	AppliedLoadout.Normalize();
	TArray<FGuLiShipAbilityGrant> Grants;
	FString Error;
	const bool bResolved = AppliedAbilitySet->ResolveLoadout(AppliedLoadout, Grants, &Error);
	checkf(bResolved, TEXT("Validated hangar configuration changed during preparation: %s"), *Error);
	for (const auto& Grant : Grants) GrantsById.Add(Grant.AbilityId, Grant);
	AppliedLoadoutChecksum = AppliedAbilitySet->ComputeLoadoutChecksum(AppliedLoadout);
	AbilitySetRevision = WeaponLoadoutRevision = ProjectionSnapshotRevision = 1;
}

void UGuLiShipHangarCapabilityComponent::OnEnabled()
{
	bActiveAbilityInputEnabled = true;
	MarkProjectionChanged();
}

void UGuLiShipHangarCapabilityComponent::OnSuspended()
{
	SetActiveAbilityInputEnabled(false);
	MarkProjectionChanged();
}

void UGuLiShipHangarCapabilityComponent::OnReleased()
{
	ServerClearShipAbilities();
	CombatCoordinator.Reset(); ReplenishmentController.Reset();
}

bool UGuLiShipHangarCapabilityComponent::InitializeCombatCoordinator(const FGuLiWingmanCombatContext& Context, FString& Error)
{
	CombatCoordinator = MakeUnique<FGuLiWingmanCombatCoordinator>();
	return CombatCoordinator->Initialize(Context, &Error);
}
void UGuLiShipHangarCapabilityComponent::ResetCombatCoordinator() { CombatCoordinator.Reset(); }

void UGuLiShipHangarCapabilityComponent::InitializeShipActorInfo(AActor* ShipActor)
{
	check(ShipActor == GetOwner());
	bInitializedOnAuthority |= ShipActor->HasAuthority();
}

bool UGuLiShipHangarCapabilityComponent::IsOwnerActorAuthoritative() const
{
	return bInitializedOnAuthority && GetOwner()->HasAuthority();
}

bool UGuLiShipHangarCapabilityComponent::ServerApplyAbilitySet(UGuLiShipAbilitySet* Set,
	const FGuLiShipAbilityLoadoutState& Loadout, FString& Error)
{
	if (!IsOwnerActorAuthoritative() || !Set)
	{
		Error = TEXT("Only authority may apply a valid Wingman configuration.");
		return false;
	}
	TArray<FGuLiShipAbilityGrant> Grants;
	if (!Set->ResolveLoadout(Loadout, Grants, &Error)) return false;
	const uint64 Checksum = Set->ComputeLoadoutChecksum(Loadout);
	if (AppliedAbilitySet == Set && AppliedLoadout == Loadout && AppliedLoadoutChecksum == Checksum) return false;
	SetActiveAbilityInputEnabled(false);
	GrantsById.Reset();
	for (const auto& Grant : Grants) GrantsById.Add(Grant.AbilityId, Grant);
	AppliedAbilitySet = Set; AppliedLoadout = Loadout; AppliedLoadoutChecksum = Checksum;
	AbilitySetRevision = AdvanceRevision(AbilitySetRevision);
	WeaponLoadoutRevision = AdvanceRevision(WeaponLoadoutRevision);
	RebuildWeaponBindingIndex();
	SetActiveAbilityInputEnabled(IsCapabilityEnabled());
	MarkProjectionChanged();
	return true;
}

void UGuLiShipHangarCapabilityComponent::ServerClearShipAbilities()
{
	SetActiveAbilityInputEnabled(false);
	for (auto& Pair : Cooldowns) GetWorld()->GetTimerManager().ClearTimer(Pair.Value.Timer);
	Cooldowns.Reset();
	CooldownViews.Reset();
	GrantsById.Reset();
	IdsByWeaponBinding.Reset();
	AppliedAbilitySet = nullptr;
	AppliedLoadout.AbilityIds.Reset();
	RuntimeChanged.Broadcast();
	MarkProjectionChanged();
}

void UGuLiShipHangarCapabilityComponent::SetActiveAbilityInputEnabled(bool bEnabled)
{
	bActiveAbilityInputEnabled = bEnabled;
	if (!bEnabled)
	{
		for (auto Id : PressedAbilities) PresentationChanged.Broadcast(Id, false);
		PressedAbilities.Reset();
	}
}

void UGuLiShipHangarCapabilityComponent::AbilityInputTagPressed(FGameplayTag InputTag)
{
	const FGuLiShipAbilityGrant* Match = nullptr;
	for (const auto& Pair : GrantsById)
	{
		if (Pair.Value.InputTag != InputTag || !InputTag.IsValid()) continue;
		if (Match) return; // Shared input metadata cannot select a unique weapon binding.
		Match = &Pair.Value;
	}
	if (Match) AbilityWeaponBindingPressed(BuildWeaponBinding(*Match));
}

void UGuLiShipHangarCapabilityComponent::AbilityInputTagReleased(FGameplayTag InputTag)
{
	for (const auto& Pair : GrantsById)
	{
		if (Pair.Value.InputTag == InputTag && InputTag.IsValid()) AbilityWeaponBindingReleased(BuildWeaponBinding(Pair.Value));
	}
}

bool UGuLiShipHangarCapabilityComponent::AbilityWeaponBindingPressed(const FGuLiWeaponBindingKey& Binding)
{
	const auto* Grant = FindConfiguredGrant(Binding);
	if (!IsCapabilityEnabled() || !bActiveAbilityInputEnabled || !Grant || !Grant->InputTag.IsValid()
		|| IsWeaponCooldownActive(Grant->GetEffectiveCooldownGroupId())) return false;
	const uint32 Revision = IsOwnerActorAuthoritative() ? AbilitySetRevision : ObservedAbilitySetRevision;
	if (!Revision) return false;
	if (!IsOwnerActorAuthoritative())
	{
		const auto* Channel = ObservedGroupAbilityConfig.FindWeaponChannel(Binding);
		if (!ObservedGroupAbilityConfig.IsUsableByLeaseOwner() || !Channel || !Channel->bEnabled) return false;
	}
	PressedAbilities.Add(Grant->AbilityId);
	PresentationChanged.Broadcast(Grant->AbilityId, true);
	TriggeredAbility.Broadcast(Grant->AbilityId, Revision, !IsOwnerActorAuthoritative());
	WeaponAbility.Broadcast(Binding, Grant->GetEffectiveSkillId(), Grant->AbilityId, Revision, !IsOwnerActorAuthoritative());
	return true;
}

bool UGuLiShipHangarCapabilityComponent::AbilityWeaponBindingReleased(const FGuLiWeaponBindingKey& Binding)
{
	const auto* Grant = FindConfiguredGrant(Binding);
	if (!Grant || !PressedAbilities.Remove(Grant->AbilityId)) return false;
	PresentationChanged.Broadcast(Grant->AbilityId, false);
	return true;
}

bool UGuLiShipHangarCapabilityComponent::SetProjectionContext(const FGuLiShipAbilityProjectionContext& Context)
{
	if (!Context.IsWellFormed() || ProjectionContext == Context) return false;
	ProjectionContext = Context;
	RebuildWeaponBindingIndex();
	MarkProjectionChanged();
	return true;
}

bool UGuLiShipHangarCapabilityComponent::ObserveReplicatedGroupAbilityConfig(const FGuLiGroupAbilityConfigSnapshot& Snapshot)
{
	if (IsOwnerActorAuthoritative() || !Snapshot.IsWellFormed() || Snapshot.SnapshotRevision <= ObservedSnapshotRevision) return false;
	ObservedSnapshotRevision = Snapshot.SnapshotRevision;
	ObservedAbilitySetRevision = Snapshot.AbilitySetRevision;
	ObservedGroupAbilityConfig = Snapshot;
	ProjectionContext.MatchEpoch = Snapshot.MatchEpoch;
	ProjectionContext.Team = Snapshot.Team;
	ProjectionContext.OwnerPlayerGuid = Snapshot.OwnerPlayerGuid;
	ProjectionContext.WingmanTypeId = Snapshot.WingmanTypeId;
	ProjectionContext.ShipInstanceId = Snapshot.ShipInstanceId;
	ProjectionContext.ShipGeneration = Snapshot.ShipGeneration;
	ProjectionContext.GroupGeneration = Snapshot.GroupGeneration;
	ProjectionContext.FormationCommandRevision = Snapshot.FormationCommandRevision;
	ProjectionContext.EffectiveClientSimTick = Snapshot.EffectiveClientSimTick;
	RebuildWeaponBindingIndex();
	return true;
}

bool UGuLiShipHangarCapabilityComponent::IsAbilityGranted(FGameplayTag Id) const { return IsCapabilityEnabled() && GrantsById.Contains(Id); }
bool UGuLiShipHangarCapabilityComponent::IsAbilityConfigurationCurrent(FGameplayTag Id, uint32 Revision) const
{
	return Revision != 0 && Revision == AbilitySetRevision && IsAbilityGranted(Id);
}
const FGuLiShipAbilityGrant* UGuLiShipHangarCapabilityComponent::FindConfiguredGrant(FGameplayTag Id) const { return GrantsById.Find(Id); }
const FGuLiShipAbilityGrant* UGuLiShipHangarCapabilityComponent::FindConfiguredGrant(EGuLiShipAbilitySlot Slot) const
{
	for (const auto& Pair : GrantsById) if (Pair.Value.Slot == Slot) return &Pair.Value;
	return nullptr;
}
const FGuLiShipAbilityGrant* UGuLiShipHangarCapabilityComponent::FindConfiguredGrant(const FGuLiWeaponBindingKey& Binding) const
{
	const auto* Id = IdsByWeaponBinding.Find(Binding);
	return Id ? GrantsById.Find(*Id) : nullptr;
}
bool UGuLiShipHangarCapabilityComponent::IsWeaponConfigurationCurrent(const FGuLiWeaponBindingKey& Binding,
	FName SkillId, uint32 LoadoutRevision, uint32 ProfileRevision) const
{
	const auto* Grant = FindConfiguredGrant(Binding);
	return IsCapabilityEnabled() && Grant && LoadoutRevision != 0 && LoadoutRevision == WeaponLoadoutRevision
		&& ProfileRevision == Grant->ProfileRevision && SkillId == Grant->GetEffectiveSkillId();
}
const UGuLiWingmanFormationDefinition* UGuLiShipHangarCapabilityComponent::GetActiveFormationDefinition() const
{
	const auto* Grant = FindConfiguredGrant(EGuLiShipAbilitySlot::Formation);
	return IsCapabilityEnabled() && Grant ? Grant->FormationDefinition.Get() : nullptr;
}
const UGuLiWingmanWeaponDefinition* UGuLiShipHangarCapabilityComponent::GetBasicWeaponDefinition() const
{
	for (const auto& Pair : GrantsById)
		if (IsCapabilityEnabled() && Pair.Value.Slot == EGuLiShipAbilitySlot::BasicWeapon
			&& Pair.Value.GetEffectiveWeaponSlotId() == GuLiGetDefaultWeaponSlotId(EGuLiShipAbilitySlot::BasicWeapon)) return Pair.Value.WeaponDefinition;
	return nullptr;
}
const UGuLiWingmanWeaponDefinition* UGuLiShipHangarCapabilityComponent::GetMissileDefinition() const
{
	for (const auto& Pair : GrantsById)
		if (Pair.Value.Slot == EGuLiShipAbilitySlot::Missile
			&& Pair.Value.GetEffectiveWeaponSlotId() == GuLiGetDefaultWeaponSlotId(EGuLiShipAbilitySlot::Missile)) return Pair.Value.WeaponDefinition;
	return nullptr;
}

FGuLiWeaponBindingKey UGuLiShipHangarCapabilityComponent::BuildWeaponBinding(const FGuLiShipAbilityGrant& Grant) const
{
	if (!ProjectionContext.IsWellFormed() || Grant.Slot == EGuLiShipAbilitySlot::Formation) return {};
	return FGuLiWeaponBindingKey::Wingman(ProjectionContext.MatchEpoch, ProjectionContext.Team,
		ProjectionContext.OwnerPlayerGuid, ProjectionContext.WingmanTypeId, Grant.GetEffectiveWeaponSlotId());
}
void UGuLiShipHangarCapabilityComponent::RebuildWeaponBindingIndex()
{
	IdsByWeaponBinding.Reset();
	for (const auto& Pair : GrantsById)
	{
		const auto Binding = BuildWeaponBinding(Pair.Value);
		if (Binding.IsWellFormed()) IdsByWeaponBinding.Add(Binding, Pair.Key);
	}
}
void UGuLiShipHangarCapabilityComponent::MarkProjectionChanged()
{
	ProjectionSnapshotRevision = AdvanceRevision(ProjectionSnapshotRevision);
	ProjectionChanged.Broadcast();
}
uint32 UGuLiShipHangarCapabilityComponent::AdvanceRevision(uint32 Revision) { return ++Revision == 0 ? 1 : Revision; }

bool UGuLiShipHangarCapabilityComponent::ServerTryStartMissileCooldown(float Seconds) { return ServerTryReserveMissileCooldown(Seconds, FGuid::NewGuid()); }
bool UGuLiShipHangarCapabilityComponent::ServerTryReserveMissileCooldown(float Seconds, const FGuid& Reservation)
{ return ServerTryReserveWeaponCooldown(DefaultMissileCooldownGroupId, Seconds, Reservation); }
bool UGuLiShipHangarCapabilityComponent::ServerRollbackMissileCooldown(const FGuid& Reservation)
{ return ServerRollbackWeaponCooldown(DefaultMissileCooldownGroupId, Reservation); }
bool UGuLiShipHangarCapabilityComponent::IsMissileCooldownActive() const { return IsWeaponCooldownActive(DefaultMissileCooldownGroupId); }

bool UGuLiShipHangarCapabilityComponent::ServerTryReserveWeaponCooldown(FName Group, float Seconds, const FGuid& Reservation)
{
	if (!IsOwnerActorAuthoritative() || !IsCapabilityEnabled() || Group.IsNone() || !Reservation.IsValid()
		|| !FMath::IsFinite(Seconds) || Seconds <= 0 || IsWeaponCooldownActive(Group)) return false;
	auto& Cooldown = Cooldowns.Add(Group);
	Cooldown.Reservation = Reservation;
	Cooldown.EndsAt = GetWorld()->GetTimeSeconds() + Seconds;
	GetWorld()->GetTimerManager().SetTimer(Cooldown.Timer, FTimerDelegate::CreateWeakLambda(this, [this, Group, Reservation]()
	{
		const auto* Current = Cooldowns.Find(Group);
		if (Current && Current->Reservation == Reservation) { Cooldowns.Remove(Group); PublishCooldowns(); }
	}), Seconds, false);
	PublishCooldowns();
	return true;
}
bool UGuLiShipHangarCapabilityComponent::ServerRollbackWeaponCooldown(FName Group, const FGuid& Reservation)
{
	if (!IsOwnerActorAuthoritative()) return false;
	const auto* Current = Cooldowns.Find(Group);
	if (!Current || Current->Reservation != Reservation) return false;
	GetWorld()->GetTimerManager().ClearTimer(Cooldowns.FindChecked(Group).Timer);
	Cooldowns.Remove(Group);
	PublishCooldowns();
	return true;
}
bool UGuLiShipHangarCapabilityComponent::IsWeaponCooldownActive(FName Group) const
{
	if (IsOwnerActorAuthoritative()) return Cooldowns.Contains(Group);
	const auto* View = CooldownViews.FindByPredicate([Group](const auto& Entry) { return Entry.TimerId == Group; });
	return View && View->EndsAtServerSeconds > GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
}
void UGuLiShipHangarCapabilityComponent::PublishCooldowns()
{
	CooldownViews.Reset();
	for (const auto& Pair : Cooldowns)
	{
		auto& View = CooldownViews.AddDefaulted_GetRef(); View.TimerId = Pair.Key; View.EndsAtServerSeconds = Pair.Value.EndsAt;
	}
	RuntimeChanged.Broadcast();
}
void UGuLiShipHangarCapabilityComponent::BuildRuntimeView(FGuLiShipCapabilityRuntimeView& View) const
{
	Super::BuildRuntimeView(View); View.Timers = CooldownViews;
}
void UGuLiShipHangarCapabilityComponent::ApplyRuntimeView(const FGuLiShipCapabilityRuntimeView& View) { CooldownViews = View.Timers; }


bool UGuLiShipHangarCapabilityComponent::BuildGroupAbilityConfigSnapshot(
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
	for (const auto& Pair : GrantsById)
	{
		if (Pair.Value.Slot == EGuLiShipAbilitySlot::Formation)
		{
			FormationGrant = &Pair.Value;
			break;
		}
	}
	const UGuLiWingmanFormationDefinition* FormationDefinition =
		FormationGrant ? FormationGrant->FormationDefinition.Get() : nullptr;

	OutSnapshot.bGroupAbilitiesValid = IsCapabilityEnabled() && AbilitySetRevision != 0u && WeaponLoadoutRevision != 0u
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
		for (const TPair<FGameplayTag, FGuLiShipAbilityGrant>& Pair : GrantsById)
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
			const bool bPersistentReady = IsCapabilityEnabled();
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
