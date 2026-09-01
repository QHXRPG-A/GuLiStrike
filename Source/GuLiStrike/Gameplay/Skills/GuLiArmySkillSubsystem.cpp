#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillReplicationActor.h"
#include "Gameplay/Skills/GuLiSkillResolver.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GuLiStrike.h"

bool UGuLiArmySkillSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiArmySkillSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	Definitions = Data->GetSkillDefinitions();
	Configs = Data->GetUnitSkillConfigs();
	RegisteredExecutors.Add(TEXT("DirectSingleTarget"));
	if (GetWorld()->GetNetMode() != NM_Client)
	{
		FString Error;
		if (!StageTeam(EGuLiTeam::Red, {}, {}, Error) || !StageTeam(EGuLiTeam::Blue, {}, {}, Error))
			UE_LOG(LogGuLiStrike, Error, TEXT("Army skill initialization failed: %s"), *Error);
	}
}

void UGuLiArmySkillSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() != NM_Client)
	{
		ReplicationActor = InWorld.SpawnActor<AGuLiArmySkillReplicationActor>();
		SynchronizeMatchEpoch();
	}
}

void UGuLiArmySkillSubsystem::Deinitialize()
{
	TeamSources.Reset(); TeamOverrides.Reset(); LastRejectedChanges.Reset(); CommittedProfiles.Reset(); PendingProfiles.Reset();
	ProfileLookup.Reset(); EffectHooks.Reset(); RegisteredExecutors.Reset(); ReplicationActor.Reset();
	Super::Deinitialize();
}

void UGuLiArmySkillSubsystem::SynchronizeMatchEpoch()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
	const auto* State = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!State || State->GetMatchEpoch() == 0 || State->GetMatchEpoch() == CachedMatchEpoch) return;
	const bool bNewMatch = CachedMatchEpoch != 0;
	CachedMatchEpoch = State->GetMatchEpoch();
	if (bNewMatch)
	{
		TeamSources.Reset(); TeamOverrides.Reset(); LastRejectedChanges.Reset(); PendingProfiles.Reset();
		// Do not copy the previous match's profiles into either team's staged reset.
		bPendingChanges = true;
		FString Error;
		if (!StageTeam(EGuLiTeam::Red, {}, {}, Error) || !StageTeam(EGuLiTeam::Blue, {}, {}, Error))
			UE_LOG(LogGuLiStrike, Error, TEXT("Army skill new-match reset failed: %s"), *Error);
	}
	// Epoch publication is needed even if both matches have identical final numbers.
	bPendingChanges = true;
}

uint64 UGuLiArmySkillSubsystem::ProfileKey(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId)
{
	return (static_cast<uint64>(static_cast<uint8>(Team)) << 48) | (static_cast<uint64>(UnitTypeId) << 32) | GetTypeHash(SlotId);
}

void UGuLiArmySkillSubsystem::RebuildLookup()
{
	ProfileLookup.Reset();
	for (int32 Index = 0; Index < CommittedProfiles.Num(); ++Index)
	{
		const auto& Profile = CommittedProfiles[Index];
		ProfileLookup.FindOrAdd(ProfileKey(Profile.Team, Profile.UnitTypeId, Profile.SlotId)).Add(Index);
	}
}

const FGuLiResolvedSkillProfile* UGuLiArmySkillSubsystem::FindResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId) const
{
	if (!IsCurrentMatchSnapshot()) return nullptr;
	if (const auto* Bucket = ProfileLookup.Find(ProfileKey(Team, UnitTypeId, SlotId)))
	{
		for (int32 Index : *Bucket)
		{
			const auto& Profile = CommittedProfiles[Index];
			if (Profile.Team == Team && Profile.UnitTypeId == UnitTypeId && Profile.SlotId == SlotId) return &Profile;
		}
	}
	return nullptr;
}

bool UGuLiArmySkillSubsystem::IsCurrentMatchSnapshot() const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	return State && State->GetMatchEpoch() != 0 && CommittedMatchEpoch == State->GetMatchEpoch();
}

const TArray<FGuLiResolvedSkillProfile>& UGuLiArmySkillSubsystem::GetResolvedSkills() const
{
	static const TArray<FGuLiResolvedSkillProfile> Empty;
	return IsCurrentMatchSnapshot() ? CommittedProfiles : Empty;
}

TArray<FGuLiSkillSource> UGuLiArmySkillSubsystem::GetSources(EGuLiTeam Team) const
{
	const UWorld* World = GetWorld();
	const auto* State = World ? World->GetGameState<AGuLiBattleGameState>() : nullptr;
	if (!State || World->GetNetMode() == NM_Client || State->GetMatchEpoch() == 0 || State->GetMatchEpoch() != CachedMatchEpoch) return {};
	const auto* Sources = TeamSources.Find(Team);
	return Sources ? *Sources : TArray<FGuLiSkillSource>();
}

bool UGuLiArmySkillSubsystem::ValidateCommander(const AGuLiBattlePlayerState& Commander, FString& OutError) const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const auto* Controller = Cast<APlayerController>(Commander.GetOwner());
	if (!State || State->GetMatchEpoch() == 0 || !Commander.HasAuthority() || Commander.GetWorld() != GetWorld()
		|| !Controller || Controller->GetPlayerState<AGuLiBattlePlayerState>() != &Commander
		|| GetWorld()->GetNetMode() == NM_Client || !Commander.IsCommander() || !Commander.IsBattleReady() || !Commander.IsSoldierStreamReady())
	{ OutError = TEXT("Requires this world's authoritative, battle-ready and soldier-ready Commander."); return false; }
	for (const auto& Slot : State->GetRoleSlots())
	{
		if (Slot.SlotIndex == Commander.GetBattleSlotIndex() && Slot.bOccupied && Slot.bBattleReady && Slot.bSyncReady
			&& Slot.Role == EGuLiCommanderRole::Commander && Slot.Team == Commander.GetTeam()
			&& Slot.PlayerGuid.IsValid() && Slot.PlayerGuid == Commander.GetPlayerGuid()) return true;
	}
	OutError = TEXT("Commander does not own the current authoritative team seat."); return false;
}

bool UGuLiArmySkillSubsystem::StageTeam(EGuLiTeam Team, const TArray<FGuLiSkillSource>& Sources,
	const TArray<FGuLiSkillNumericOverride>& Overrides, FString& OutError,
	const TArray<FGuLiSkillSlotKey>* AffectedSlots)
{
	TArray<FGuLiResolvedSkillProfile> Profiles;
	const bool bResolved = AffectedSlots
		? FGuLiSkillResolver::ResolveSelected(Team, Definitions, Configs, Sources, Overrides, *AffectedSlots, Profiles, OutError)
		: FGuLiSkillResolver::Resolve(Team, Definitions, Configs, Sources, Overrides, Profiles, OutError);
	if (!bResolved) { LastRejectedChanges.Add(Team, OutError); return false; }
	for (const auto& Source : Sources)
		for (const auto& Replacement : Source.Replacements)
		{
			const auto* Definition = Definitions.FindByPredicate([&](const auto& Item) { return Item.SkillId == Replacement.SkillId; });
			if (!Definition || !RegisteredExecutors.Contains(Definition->ExecutorId))
			{ OutError = TEXT("Replacement references an unavailable executor, including hidden alternatives."); LastRejectedChanges.Add(Team, OutError); return false; }
		}
	for (const auto& Profile : Profiles)
	{
		if (!RegisteredExecutors.Contains(Profile.ExecutorId))
		{ OutError = FString::Printf(TEXT("Executor '%s' is not registered."), *Profile.ExecutorId.ToString()); LastRejectedChanges.Add(Team, OutError); return false; }
	}
	// All checks precede source/config mutation, including same-ID upserts.
	TArray<FGuLiSkillSource> SourceCopy = Sources;
	TArray<FGuLiSkillNumericOverride> OverrideCopy = Overrides;
	LastResolvedKeyCount = Profiles.Num();
	if (!Profiles.IsEmpty())
	{
		if (!bPendingChanges) PendingProfiles = CommittedProfiles;
		// Retain every unaffected committed or already-pending key, including this team's.
		for (auto& Profile : Profiles)
		{
			auto* Existing = PendingProfiles.FindByPredicate([&](const auto& Item)
			{ return Item.Team == Profile.Team && Item.UnitTypeId == Profile.UnitTypeId && Item.SlotId == Profile.SlotId; });
			if (Existing) *Existing = MoveTemp(Profile);
			else PendingProfiles.Add(MoveTemp(Profile));
		}
		bPendingChanges = true;
	}
	TeamSources.Add(Team, MoveTemp(SourceCopy)); TeamOverrides.Add(Team, MoveTemp(OverrideCopy));
	return true;
}

bool UGuLiArmySkillSubsystem::UpsertSource(const AGuLiBattlePlayerState& Commander, const FGuLiSkillSource& Source, FString& OutError)
{
	if (!ValidateCommander(Commander, OutError)) return false;
	SynchronizeMatchEpoch();
	auto Sources = TeamSources.FindOrAdd(Commander.GetTeam());
	TArray<FGuLiSkillSlotKey> AffectedSlots;
	FGuLiSkillResolver::GatherAffectedSlots(Configs, Source, AffectedSlots);
	if (auto* Existing = Sources.FindByPredicate([&](const auto& Item) { return Item.SourceInstanceId == Source.SourceInstanceId; }))
	{
		FGuLiSkillResolver::GatherAffectedSlots(Configs, *Existing, AffectedSlots);
		*Existing = Source;
	}
	else Sources.Add(Source);
	return StageTeam(Commander.GetTeam(), Sources, TeamOverrides.FindOrAdd(Commander.GetTeam()), OutError, &AffectedSlots);
}

bool UGuLiArmySkillSubsystem::RemoveSource(const AGuLiBattlePlayerState& Commander, FGuid SourceInstanceId, FString& OutError)
{
	if (!ValidateCommander(Commander, OutError)) return false;
	SynchronizeMatchEpoch();
	if (!SourceInstanceId.IsValid()) { OutError = TEXT("Source ID is invalid."); return false; }
	auto Sources = TeamSources.FindOrAdd(Commander.GetTeam());
	TArray<FGuLiSkillSlotKey> AffectedSlots;
	if (const auto* Existing = Sources.FindByPredicate([&](const auto& Source) { return Source.SourceInstanceId == SourceInstanceId; }))
		FGuLiSkillResolver::GatherAffectedSlots(Configs, *Existing, AffectedSlots);
	Sources.RemoveAll([&](const auto& Source) { return Source.SourceInstanceId == SourceInstanceId; });
	return StageTeam(Commander.GetTeam(), Sources, TeamOverrides.FindOrAdd(Commander.GetTeam()), OutError, &AffectedSlots);
}

bool UGuLiArmySkillSubsystem::SetNumericOverride(const AGuLiBattlePlayerState& Commander, const FGuLiSkillNumericOverride& Override, FString& OutError)
{
	if (!ValidateCommander(Commander, OutError)) return false;
	SynchronizeMatchEpoch();
	auto Overrides = TeamOverrides.FindOrAdd(Commander.GetTeam());
	auto* Existing = Overrides.FindByPredicate([&](const auto& Item) { return Item.UnitTypeId == Override.UnitTypeId && Item.SlotId == Override.SlotId; });
	if (!Existing) { Existing = &Overrides.AddDefaulted_GetRef(); Existing->UnitTypeId = Override.UnitTypeId; Existing->SlotId = Override.SlotId; }
	if (Override.bOverrideDamage) { Existing->bOverrideDamage = true; Existing->Damage = Override.Damage; }
	if (Override.bOverrideAttackRate) { Existing->bOverrideAttackRate = true; Existing->AttackRatePerSecond = Override.AttackRatePerSecond; }
	if (Override.bOverrideRange) { Existing->bOverrideRange = true; Existing->RangeCentimeters = Override.RangeCentimeters; }
	const TArray<FGuLiSkillSlotKey> AffectedSlots = {{Override.UnitTypeId, Override.SlotId}};
	return StageTeam(Commander.GetTeam(), TeamSources.FindOrAdd(Commander.GetTeam()), Overrides, OutError, &AffectedSlots);
}

bool UGuLiArmySkillSubsystem::ClearNumericOverride(const AGuLiBattlePlayerState& Commander, uint16 UnitTypeId, FName SlotId, FString& OutError)
{
	if (!ValidateCommander(Commander, OutError)) return false;
	SynchronizeMatchEpoch();
	if (!Configs.ContainsByPredicate([&](const auto& Item) { return Item.bDefault && Item.UnitTypeId == UnitTypeId && Item.SlotId == SlotId; }))
	{ OutError = TEXT("Cannot reset unknown unit/slot."); return false; }
	auto Overrides = TeamOverrides.FindOrAdd(Commander.GetTeam());
	TArray<FGuLiSkillSlotKey> AffectedSlots;
	if (Overrides.ContainsByPredicate([&](const auto& Item) { return Item.UnitTypeId == UnitTypeId && Item.SlotId == SlotId; }))
		AffectedSlots.Add({UnitTypeId, SlotId});
	Overrides.RemoveAll([&](const auto& Item) { return Item.UnitTypeId == UnitTypeId && Item.SlotId == SlotId; });
	return StageTeam(Commander.GetTeam(), TeamSources.FindOrAdd(Commander.GetTeam()), Overrides, OutError, &AffectedSlots);
}

bool UGuLiArmySkillSubsystem::ClearAll(const AGuLiBattlePlayerState& Commander, FString& OutError)
{
	if (!ValidateCommander(Commander, OutError)) return false;
	SynchronizeMatchEpoch();
	TArray<FGuLiSkillSlotKey> AffectedSlots;
	for (const auto& Source : TeamSources.FindOrAdd(Commander.GetTeam()))
		FGuLiSkillResolver::GatherAffectedSlots(Configs, Source, AffectedSlots);
	for (const auto& Override : TeamOverrides.FindOrAdd(Commander.GetTeam()))
		AffectedSlots.AddUnique({Override.UnitTypeId, Override.SlotId});
	return StageTeam(Commander.GetTeam(), {}, {}, OutError, &AffectedSlots);
}

bool UGuLiArmySkillSubsystem::ExecuteCommand(const AGuLiBattlePlayerState& Commander, const FGuLiArmySkillCommand& Command, FString& OutError)
{
	switch (Command.Command)
	{
	case EGuLiArmySkillCommand::UpsertSource: return UpsertSource(Commander, Command.Source, OutError);
	case EGuLiArmySkillCommand::RemoveSource: return RemoveSource(Commander, Command.SourceInstanceId, OutError);
	case EGuLiArmySkillCommand::SetNumericOverride: return SetNumericOverride(Commander, Command.NumericOverride, OutError);
	case EGuLiArmySkillCommand::ClearNumericOverride: return ClearNumericOverride(Commander, Command.NumericOverride.UnitTypeId, Command.NumericOverride.SlotId, OutError);
	case EGuLiArmySkillCommand::ClearAll: return ClearAll(Commander, OutError);
	default: OutError = TEXT("Unknown army skill command."); return false;
	}
}

void UGuLiArmySkillSubsystem::CommitPendingChanges()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
	const auto* State = GetWorld()->GetGameState<AGuLiBattleGameState>();
	// Keep initial pending defaults intact if an early fixed step precedes match setup.
	if (!State || State->GetMatchEpoch() == 0) return;
	SynchronizeMatchEpoch();
	if (!bPendingChanges) return;
	bool bConfigurationChanged = PendingProfiles.Num() != CommittedProfiles.Num();
	for (auto& Profile : PendingProfiles)
	{
		const auto* Old = FindResolvedSkill(Profile.Team, Profile.UnitTypeId, Profile.SlotId);
		const bool bSame = Old && Old->HasSameConfiguration(Profile);
		bConfigurationChanged |= !bSame;
		Profile.Revision = bSame ? Old->Revision : NextRevision++;
		if (NextRevision == 0) NextRevision = 1;
	}
	if (bConfigurationChanged)
	{
		CommittedProfiles = MoveTemp(PendingProfiles);
		RebuildLookup();
	}
	else PendingProfiles.Reset();
	CommittedMatchEpoch = CachedMatchEpoch;
	bPendingChanges = false;
	if (ReplicationActor.IsValid() && (bConfigurationChanged || LastPublishedEpoch != CachedMatchEpoch))
	{
		ReplicationActor->Publish(CachedMatchEpoch, CommittedProfiles);
		LastPublishedEpoch = CachedMatchEpoch;
	}
}

void UGuLiArmySkillSubsystem::ReceiveReplicatedProfiles(uint32 MatchEpoch, const TArray<FGuLiResolvedSkillProfile>& Profiles)
{
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Client) return;
	CachedMatchEpoch = MatchEpoch;
	CommittedMatchEpoch = MatchEpoch;
	CommittedProfiles = Profiles;
	RebuildLookup();
}

FString UGuLiArmySkillSubsystem::ExplainResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId) const
{
	const auto* Profile = FindResolvedSkill(Team, UnitTypeId, SlotId);
	if (!Profile)
	{
		FString Text = TEXT("No committed skill profile (catalog, executor, simulation, or current-match snapshot not ready).");
		if (const auto* Error = LastRejectedChanges.Find(Team)) Text += TEXT("\nLatest rejected team request (not applied): ") + *Error;
		return Text;
	}
	FString Text = FString::Printf(TEXT("Committed final: Team=%u Unit=%u Slot=%s Skill=%s Executor=%s Damage=%.6g Rate=%.6g/s Range=%.6gcm Revision=%u Pending=%s"),
		static_cast<uint8>(Team), UnitTypeId, *SlotId.ToString(), *Profile->SkillId.ToString(), *Profile->ExecutorId.ToString(),
		Profile->Damage, Profile->AttackRatePerSecond, Profile->RangeCentimeters, Profile->Revision, bPendingChanges ? TEXT("yes") : TEXT("no"));
	if (const auto* Base = Configs.FindByPredicate([&](const auto& Item) { return Item.UnitTypeId == UnitTypeId && Item.SlotId == SlotId && Item.SkillId == Profile->SkillId; }))
		Text += FString::Printf(TEXT("\nBase: Damage=%.6g Rate=%.6g Range=%.6g"), Base->Damage, Base->AttackRatePerSecond, Base->RangeCentimeters);
	if (const auto* Sources = TeamSources.Find(Team))
	{
		for (const auto& Source : *Sources)
		{
			for (const auto& Replacement : Source.Replacements)
				if (Replacement.Target.MatchesUnitSlot(UnitTypeId, SlotId)) Text += FString::Printf(TEXT("\nSource %s [%s]: replace=%s priority=%d"), *Source.SourceInstanceId.ToString(), *Source.DebugLabel, *Replacement.SkillId.ToString(), Replacement.Priority);
			for (const auto& Modifier : Source.Modifiers)
			{
				if (!Modifier.Target.MatchesUnitSlot(UnitTypeId, SlotId)) continue;
				const bool bApplicable = (Modifier.Target.RequiredSkillId.IsNone() || Modifier.Target.RequiredSkillId == Profile->SkillId) && Profile->Tags.HasAll(Modifier.Target.RequiredTags);
				static const TCHAR* AttributeNames[] = {TEXT("Damage"), TEXT("AttackRatePerSecond"), TEXT("RangeCentimeters")};
				Text += FString::Printf(TEXT("\nSource %s [%s]: %s %s %.6g required-skill=%s required-tags={%s} committed-final-filter=%s"),
					*Source.SourceInstanceId.ToString(), *Source.DebugLabel, AttributeNames[static_cast<uint8>(Modifier.Attribute)],
					Modifier.Operation == EGuLiSkillModifierOperation::AddPercent ? TEXT("AddPercent(fraction)") : TEXT("AddFlat"), Modifier.Magnitude,
					*Modifier.Target.RequiredSkillId.ToString(), *Modifier.Target.RequiredTags.ToStringSimple(), bApplicable ? TEXT("match") : TEXT("inactive"));
			}
		}
	}
	if (const auto* Overrides = TeamOverrides.Find(Team))
		for (const auto& Override : *Overrides)
			if (Override.UnitTypeId == UnitTypeId && Override.SlotId == SlotId) Text += FString::Printf(TEXT("\nGM final override: Damage=%s%.6g Rate=%s%.6g Range=%s%.6g"),
				Override.bOverrideDamage ? TEXT("") : TEXT("off/"), Override.Damage, Override.bOverrideAttackRate ? TEXT("") : TEXT("off/"), Override.AttackRatePerSecond,
				Override.bOverrideRange ? TEXT("") : TEXT("off/"), Override.RangeCentimeters);
	if (bPendingChanges)
	{
		if (const auto* Pending = PendingProfiles.FindByPredicate([&](const auto& Item) { return Item.Team == Team && Item.UnitTypeId == UnitTypeId && Item.SlotId == SlotId; }))
			Text += FString::Printf(TEXT("\nAccepted pending final (next authority step): Skill=%s Damage=%.6g Rate=%.6g Range=%.6g. Source ledger above includes pending changes."),
				*Pending->SkillId.ToString(), Pending->Damage, Pending->AttackRatePerSecond, Pending->RangeCentimeters);
	}
	Text += FString::Printf(TEXT("\nCalculation: (Base+SumFlat)*Product(1+PercentPerSource), then GM override. Last accepted resolution computed %d slot(s)."), LastResolvedKeyCount);
	Text += TEXT("\nLimits: finite Damage[0,1e9], Rate[0,30]/s, Range[0,1e6]cm. Out-of-range requests are rejected atomically; no clamp is applied.");
	if (const auto* Error = LastRejectedChanges.Find(Team)) Text += TEXT("\nLatest rejected team request (not applied): ") + *Error;
	return Text;
}

bool UGuLiArmySkillSubsystem::RegisterExecutor(FName ExecutorId)
{
	if (ExecutorId.IsNone() || RegisteredExecutors.Contains(ExecutorId)) return false;
	RegisteredExecutors.Add(ExecutorId); return true;
}

bool UGuLiArmySkillSubsystem::RegisterEffectHook(FName HookId, FGuLiSkillEffectHook Hook)
{
	if (HookId.IsNone() || !Hook || EffectHooks.Contains(HookId)) return false;
	EffectHooks.Add(HookId, MoveTemp(Hook)); return true;
}

void UGuLiArmySkillSubsystem::UnregisterEffectHook(FName HookId) { EffectHooks.Remove(HookId); }

void UGuLiArmySkillSubsystem::ExecuteEffectHook(FName HookId, const FGuLiSkillEffectContext& Context) const
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
	if (const auto* Hook = EffectHooks.Find(HookId))
	{
		// A hook may unregister itself while executing.
		const FGuLiSkillEffectHook Callback = *Hook;
		Callback(Context);
	}
}
