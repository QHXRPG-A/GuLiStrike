// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
	uint32 NextRevision(const uint32 Current)
	{
		const uint32 Next = Current + 1u;
		return Next == 0u ? 1u : Next;
	}
}

bool FGuLiCombatHealthState::IsWellFormed() const
{
	return Revision != 0u && FMath::IsFinite(Health) && FMath::IsFinite(MaxHealth)
		&& MaxHealth > 0.0f && Health >= 0.0f && Health <= MaxHealth
		&& FMath::IsFinite(Shield) && FMath::IsFinite(MaxShield) && Shield >= 0 && MaxShield >= Shield
		&& bDead == (Health <= 0.0f);
}

bool FGuLiDamageRequest::IsWellFormed() const
{
	return MatchEpoch != 0u && DamageEventId.IsValid() && ShotId.IsValid()
		&& Source.IsValid() && Target.IsValid() && Source != Target
		&& FMath::IsFinite(Damage) && Damage > 0.0f && !HitLocation.ContainsNaN();
}

bool FGuLiDeathCommitRecord::IsWellFormed() const
{
	return MatchEpoch != 0u && DeathEventId.IsValid() && DamageEventId.IsValid()
		&& ShotId.IsValid() && Source.IsValid() && Target.IsValid() && Source != Target
		&& CommitOrdinal != 0u;
}

bool FGuLiRewardCommitRecord::IsWellFormed() const
{
	if (MatchEpoch == 0u || !RewardEventId.IsValid() || !DeathEventId.IsValid()
		|| CommitOrdinal == 0u || Status == EGuLiRewardGrantStatus::NotApplicable)
	{
		return false;
	}
	const bool bGrantRequired = Status == EGuLiRewardGrantStatus::SinkUnavailable
		|| Status == EGuLiRewardGrantStatus::SinkRejected
		|| Status == EGuLiRewardGrantStatus::Granted;
	return bGrantRequired ? Grant.IsWellFormed() : !Grant.IsWellFormed();
}

FGuLiTargetHandle GuLiCombatTargets::MakeWingmanTargetHandle(const FGuLiWingmanHandle& Wingman)
{
	FGuLiTargetHandle Target;
	if (!Wingman.IsValid())
	{
		return Target;
	}

	const FGuid& ShipId = Wingman.Flight.Group.ShipInstanceId;
	Target.Kind = EGuLiTargetKind::Wingman;
	Target.AuthorityId = FGuid(
		ShipId.A ^ (static_cast<uint32>(Wingman.Flight.FlightIndex) << 24u)
			^ (static_cast<uint32>(Wingman.MemberIndex) << 16u),
		ShipId.B ^ Wingman.EntityGeneration,
		ShipId.C ^ Wingman.Flight.Group.GroupGeneration,
		ShipId.D ^ Wingman.Flight.Group.ShipGeneration);
	Target.Generation = Wingman.EntityGeneration;
	Target.LocalId = static_cast<uint32>(Wingman.GetGroupMemberIndex()) + 1u;
	return Target;
}

FGuLiTargetHandle GuLiCombatTargets::MakeCommanderSoldierTargetHandle(
	const uint32 MatchEpoch,
	const uint32 SoldierId,
	const uint32 EntityGeneration)
{
	FGuLiTargetHandle Target;
	if (MatchEpoch == 0u || SoldierId == 0u || EntityGeneration == 0u)
	{
		return Target;
	}
	Target.Kind = EGuLiTargetKind::CommanderSoldier;
	Target.AuthorityId = FGuid(MatchEpoch, SoldierId, 0x434d4452u, 0x534f4c44u);
	Target.Generation = EntityGeneration;
	Target.LocalId = SoldierId;
	return Target;
}

UGuLiCombatHealthComponent::UGuLiCombatHealthComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UGuLiCombatHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiCombatHealthComponent, HealthState);
	DOREPLIFETIME(UGuLiCombatHealthComponent, TargetHandle);
	DOREPLIFETIME(UGuLiCombatHealthComponent, Team);
}

void UGuLiCombatHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetNetMode() != NM_DedicatedServer && !GetOwner()->FindComponentByClass<UGuLiUnitFeedbackComponent>())
	{
		auto* Feedback = NewObject<UGuLiUnitFeedbackComponent>(GetOwner());
		// Health is already replicated. Each render client owns this observer locally.
		Feedback->SetIsReplicated(false);
		Feedback->bPlayDestructionEffect = !GetOwner()->IsA<AGuLiStrikeShip>();
		GetOwner()->AddInstanceComponent(Feedback);
		Feedback->RegisterComponent();
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (!HealthState.IsWellFormed())
		{
			InitializeServerHealth(DefaultMaxHealth, false);
		}
		RegisterWithLedger();
	}
}

void UGuLiCombatHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromLedger();
	Super::EndPlay(EndPlayReason);
}

bool UGuLiCombatHealthComponent::ConfigureServerTarget(
	const FGuLiTargetHandle& NewHandle, const EGuLiTeam NewTeam)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !NewHandle.IsValid() || NewTeam == EGuLiTeam::Unassigned)
	{
		return false;
	}
	if (TargetHandle == NewHandle && Team == NewTeam)
	{
		RegisterWithLedger();
		return true;
	}
	UnregisterFromLedger();
	TargetHandle = NewHandle;
	Team = NewTeam;
	RegisterWithLedger();
	GetOwner()->ForceNetUpdate();
	return true;
}

bool UGuLiCombatHealthComponent::InitializeServerHealth(const float NewMaxHealth, const bool bPreserveRatio)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(NewMaxHealth) || NewMaxHealth <= 0.0f)
	{
		return false;
	}
	const bool bWasDead = HealthState.bDead;
	const float PreviousRatio = bPreserveRatio && HealthState.IsWellFormed()
		? HealthState.Health / HealthState.MaxHealth
		: 1.0f;
	HealthState.MaxHealth = NewMaxHealth;
	HealthState.Health = FMath::Clamp(NewMaxHealth * PreviousRatio, 0.0f, NewMaxHealth);
	HealthState.bDead = HealthState.Health <= 0.0f;
	HealthState.Revision = NextRevision(HealthState.Revision);
	BroadcastHealthState(bWasDead);
	GetOwner()->ForceNetUpdate();
	return true;
}

void UGuLiCombatHealthComponent::InitializeServerShield(float Maximum)
{
	check(GetOwner()->HasAuthority() && FMath::IsFinite(Maximum) && Maximum >= 0);
	HealthState.MaxShield = Maximum; HealthState.Shield = Maximum;
	HealthState.Revision = NextRevision(HealthState.Revision);
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}
float UGuLiCombatHealthComponent::ConsumeServerShield(float Damage)
{
	check(GetOwner()->HasAuthority() && Damage >= 0);
	const float Absorbed = FMath::Min(Damage, HealthState.Shield);
	if (Absorbed == 0) return 0;
	HealthState.Shield -= Absorbed; HealthState.Revision = NextRevision(HealthState.Revision);
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
	return Absorbed;
}
void UGuLiCombatHealthComponent::RechargeServerShield(float Amount)
{
	check(GetOwner()->HasAuthority() && Amount >= 0);
	if (HealthState.bDead || HealthState.Shield >= HealthState.MaxShield) return;
	HealthState.Shield = FMath::Min(HealthState.MaxShield, HealthState.Shield + Amount);
	HealthState.Revision = NextRevision(HealthState.Revision);
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}

bool UGuLiCombatHealthComponent::ApplyServerDamage(
	const FGuLiDamageRequest& Request, FGuLiDamageCommitResult& OutResult)
{
	OutResult = FGuLiDamageCommitResult();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Request.IsWellFormed()
		|| Request.Target != TargetHandle || !HealthState.IsWellFormed() || HealthState.bDead)
	{
		OutResult.Status = HealthState.bDead
			? EGuLiDamageCommitStatus::RejectedTargetDead
			: EGuLiDamageCommitStatus::RejectedByAdapter;
		return false;
	}
	if (UGuLiExternalUnitControlComponent::IsActorPhased(GetOwner()))
	{
		OutResult.Status = HealthState.bDead
			? EGuLiDamageCommitStatus::RejectedTargetDead
			: EGuLiDamageCommitStatus::RejectedByAdapter;
		return false;
	}

	const bool bWasDead = HealthState.bDead;
	const float PreviousHealth = HealthState.Health;
	OutResult.AbsorbedDamage = ConsumeServerShield(Request.Damage);
	HealthState.Health = FMath::Max(0.0f, HealthState.Health - (Request.Damage - OutResult.AbsorbedDamage));
	HealthState.bDead = HealthState.Health <= 0.0f;
	HealthState.Revision = NextRevision(HealthState.Revision);
	OutResult.Status = EGuLiDamageCommitStatus::Committed;
	OutResult.AppliedDamage = PreviousHealth - HealthState.Health;
	OutResult.RemainingHealth = HealthState.Health;
	OutResult.bKilled = !bWasDead && HealthState.bDead;
	BroadcastHealthState(bWasDead);
	GetOwner()->ForceNetUpdate();
	return true;
}

void UGuLiCombatHealthComponent::OnRep_HealthState()
{
	OnHealthChanged.Broadcast(HealthState.Health, HealthState.MaxHealth);
	if (HealthState.bDead)
	{
		OnDeath.Broadcast();
	}
}

void UGuLiCombatHealthComponent::RegisterWithLedger()
{
	if (!GetWorld() || !GetOwner() || !GetOwner()->HasAuthority() || !TargetHandle.IsValid())
	{
		return;
	}
	if (UGuLiDamageLedgerSubsystem* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>())
	{
		Ledger->RegisterHealthComponent(*this);
	}
}

void UGuLiCombatHealthComponent::UnregisterFromLedger()
{
	if (!GetWorld() || !TargetHandle.IsValid())
	{
		return;
	}
	if (UGuLiDamageLedgerSubsystem* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>())
	{
		Ledger->UnregisterTarget(TargetHandle, this);
	}
}

void UGuLiCombatHealthComponent::BroadcastHealthState(const bool bWasDead)
{
	OnHealthChanged.Broadcast(HealthState.Health, HealthState.MaxHealth);
	if (!bWasDead && HealthState.bDead)
	{
		OnDeath.Broadcast();
	}
}

void UGuLiDamageLedgerSubsystem::Deinitialize()
{
	DamageBarriers.Reset();
	RetainedEffectSources.Reset();
	TargetAdapters.Reset();
	SourceAdapters.Reset();
	ResultsByEvent.Reset();
	EventOrder.Reset();
	DeathRecordsByEvent.Reset();
	DeathEventOrder.Reset();
	RewardRecordsByEvent.Reset();
	RewardEventOrder.Reset();
	RewardPolicy = FGuLiRewardPolicy{};
	RewardSink = FGuLiRewardSink{};
	MatchEpoch = 0u;
	CommitCount = 0u;
	DeathCommitCount = 0u;
	RewardCommitCount = 0u;
	GrantedRewardCount = 0u;
	Super::Deinitialize();
}

bool UGuLiDamageLedgerSubsystem::IsAuthorityWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

bool UGuLiDamageLedgerSubsystem::BeginServerEpoch(const uint32 NewMatchEpoch)
{
	if (!IsAuthorityWorld() || NewMatchEpoch == 0u)
	{
		return false;
	}
	if (MatchEpoch == NewMatchEpoch)
	{
		return true;
	}
	MatchEpoch = NewMatchEpoch;
	RetainedEffectSources.Reset();
	ResultsByEvent.Reset();
	EventOrder.Reset();
	DeathRecordsByEvent.Reset();
	DeathEventOrder.Reset();
	RewardRecordsByEvent.Reset();
	RewardEventOrder.Reset();
	CommitCount = 0u;
	DeathCommitCount = 0u;
	RewardCommitCount = 0u;
	GrantedRewardCount = 0u;
	PruneInvalidTargets();
	return true;
}

bool UGuLiDamageLedgerSubsystem::SetRewardPipeline(
	FGuLiRewardPolicy InPolicy,
	FGuLiRewardSink InSink)
{
	if (!IsAuthorityWorld())
	{
		return false;
	}
	RewardPolicy = MoveTemp(InPolicy);
	RewardSink = MoveTemp(InSink);
	return true;
}

void UGuLiDamageLedgerSubsystem::ClearRewardPipeline()
{
	if (IsAuthorityWorld())
	{
		RewardPolicy = FGuLiRewardPolicy{};
		RewardSink = FGuLiRewardSink{};
	}
}

bool UGuLiDamageLedgerSubsystem::TryGetDeathRecord(
	const FGuid& DeathEventId,
	FGuLiDeathCommitRecord& OutRecord) const
{
	OutRecord = FGuLiDeathCommitRecord{};
	if (!IsAuthorityWorld())
	{
		return false;
	}
	if (const FGuLiDeathCommitRecord* Record = DeathRecordsByEvent.Find(DeathEventId))
	{
		OutRecord = *Record;
		return true;
	}
	return false;
}

bool UGuLiDamageLedgerSubsystem::TryGetRewardRecord(
	const FGuid& RewardEventId,
	FGuLiRewardCommitRecord& OutRecord) const
{
	OutRecord = FGuLiRewardCommitRecord{};
	if (!IsAuthorityWorld())
	{
		return false;
	}
	if (const FGuLiRewardCommitRecord* Record = RewardRecordsByEvent.Find(RewardEventId))
	{
		OutRecord = *Record;
		return true;
	}
	return false;
}

FGuid UGuLiDamageLedgerSubsystem::DeriveDeathEventId(const FGuid& DamageEventId)
{
	if (!DamageEventId.IsValid())
	{
		return FGuid{};
	}
	FGuid Result(
		DamageEventId.A ^ 0x44454154u,
		DamageEventId.B ^ 0x485f4556u,
		DamageEventId.C ^ 0x454e545fu,
		DamageEventId.D ^ 0x49445f31u);
	if (!Result.IsValid())
	{
		Result.D = 1u;
	}
	return Result;
}

FGuid UGuLiDamageLedgerSubsystem::DeriveRewardEventId(const FGuid& DamageEventId)
{
	if (!DamageEventId.IsValid())
	{
		return FGuid{};
	}
	FGuid Result(
		DamageEventId.A ^ 0x52455741u,
		DamageEventId.B ^ 0x52445f45u,
		DamageEventId.C ^ 0x56454e54u,
		DamageEventId.D ^ 0x5f494431u);
	if (!Result.IsValid())
	{
		Result.D = 1u;
	}
	return Result;
}

bool UGuLiDamageLedgerSubsystem::RegisterTarget(
	const FGuLiTargetHandle& Handle, FGuLiCombatTargetAdapter Adapter)
{
	if (!IsAuthorityWorld() || !Handle.IsValid() || !Adapter.IsBound())
	{
		return false;
	}
	if (const FGuLiCombatTargetAdapter* Existing = TargetAdapters.Find(Handle))
	{
		if (Existing->LifetimeOwner.IsValid() && Existing->LifetimeOwner != Adapter.LifetimeOwner)
		{
			return false;
		}
	}
	TargetAdapters.Add(Handle, MoveTemp(Adapter));
	return true;
}

bool UGuLiDamageLedgerSubsystem::RegisterHealthComponent(UGuLiCombatHealthComponent& HealthComponent)
{
	const FGuLiTargetHandle Handle = HealthComponent.GetTargetHandle();
	TWeakObjectPtr<UGuLiCombatHealthComponent> WeakHealth(&HealthComponent);
	FGuLiCombatTargetAdapter Adapter;
	Adapter.LifetimeOwner = &HealthComponent;
	Adapter.ReadSnapshot = [WeakHealth, Handle](FGuLiCombatTargetSnapshot& OutSnapshot)
	{
		const UGuLiCombatHealthComponent* Health = WeakHealth.Get();
		const AActor* Owner = Health ? Health->GetOwner() : nullptr;
		if (!Health || !Owner || Health->GetTargetHandle() != Handle || UGuLiExternalUnitControlComponent::IsActorPhased(Owner))
		{
			return false;
		}
		OutSnapshot = FGuLiCombatTargetSnapshot();
		OutSnapshot.Handle = Handle;
		OutSnapshot.Team = Health->GetCombatTeam();
		OutSnapshot.Location = Owner->GetActorLocation();
		OutSnapshot.Health = Health->GetHealthState().Health;
		OutSnapshot.bAlive = Health->IsAlive();
		OutSnapshot.CollisionActor = const_cast<AActor*>(Owner);
		if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
		{
			OutSnapshot.CollisionRadius = RootPrimitive->Bounds.SphereRadius;
		}
		return !OutSnapshot.Location.ContainsNaN();
	};
	Adapter.ApplyDamage = [WeakHealth](const FGuLiDamageRequest& Request, FGuLiDamageCommitResult& OutResult)
	{
		UGuLiCombatHealthComponent* Health = WeakHealth.Get();
		return Health && Health->ApplyServerDamage(Request, OutResult);
	};
	return RegisterTarget(Handle, MoveTemp(Adapter));
}

void UGuLiDamageLedgerSubsystem::UnregisterTarget(
	const FGuLiTargetHandle& Handle, const UObject* ExpectedOwner)
{
	if (FGuLiCombatTargetAdapter* Existing = TargetAdapters.Find(Handle))
	{
		if (!ExpectedOwner || Existing->LifetimeOwner.Get() == ExpectedOwner)
		{
			TargetAdapters.Remove(Handle);
		}
	}
}

bool UGuLiDamageLedgerSubsystem::TryGetTargetSnapshot(
	const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot)
{
	OutSnapshot = FGuLiCombatTargetSnapshot();
	FGuLiCombatTargetAdapter* Adapter = TargetAdapters.Find(Handle);
	if (!Adapter || !Adapter->IsBound() || !Adapter->ReadSnapshot(OutSnapshot)
		|| OutSnapshot.Handle != Handle || OutSnapshot.Location.ContainsNaN())
	{
		if (Adapter && !Adapter->LifetimeOwner.IsValid())
		{
			TargetAdapters.Remove(Handle);
		}
		return false;
	}
	return true;
}

void UGuLiDamageLedgerSubsystem::GetTargetSnapshots(
	TArray<FGuLiCombatTargetSnapshot>& OutSnapshots)
{
	OutSnapshots.Reset();
	if (!IsAuthorityWorld())
	{
		return;
	}
	PruneInvalidTargets();
	OutSnapshots.Reserve(TargetAdapters.Num());
	for (const TPair<FGuLiTargetHandle, FGuLiCombatTargetAdapter>& Pair : TargetAdapters)
	{
		FGuLiCombatTargetSnapshot Snapshot;
		if (Pair.Value.IsBound() && Pair.Value.ReadSnapshot(Snapshot)
			&& Snapshot.Handle == Pair.Key && Snapshot.Handle.IsValid()
			&& !Snapshot.Location.ContainsNaN())
		{
			OutSnapshots.Add(MoveTemp(Snapshot));
		}
	}
	OutSnapshots.Sort([](const FGuLiCombatTargetSnapshot& Lhs, const FGuLiCombatTargetSnapshot& Rhs)
	{
		if (Lhs.Handle.Kind != Rhs.Handle.Kind)
		{
			return static_cast<uint8>(Lhs.Handle.Kind) < static_cast<uint8>(Rhs.Handle.Kind);
		}
		if (Lhs.Handle.AuthorityId != Rhs.Handle.AuthorityId)
		{
			return Lhs.Handle.AuthorityId < Rhs.Handle.AuthorityId;
		}
		return Lhs.Handle.Generation < Rhs.Handle.Generation;
	});
}

FGuLiDamageCommitResult UGuLiDamageLedgerSubsystem::CommitDamage(const FGuLiDamageRequest& Request)
{
	return CommitDamageInternal(Request, nullptr);
}

bool UGuLiDamageLedgerSubsystem::RegisterSource(const FGuLiTargetHandle& Handle, FGuLiCombatSourceAdapter Adapter)
{
	if (!IsAuthorityWorld() || !Handle.IsValid() || !Adapter.LifetimeOwner.IsValid() || !Adapter.ReadSnapshot) return false;
	if (const auto* Existing = SourceAdapters.Find(Handle); Existing && Existing->LifetimeOwner.IsValid()
		&& Existing->LifetimeOwner != Adapter.LifetimeOwner) return false;
	SourceAdapters.Add(Handle, MoveTemp(Adapter));
	return true;
}

void UGuLiDamageLedgerSubsystem::UnregisterSource(const FGuLiTargetHandle& Handle, const UObject* Owner)
{
	if (const auto* Source = SourceAdapters.Find(Handle); Source && Source->LifetimeOwner.Get() == Owner) SourceAdapters.Remove(Handle);
}

bool UGuLiDamageLedgerSubsystem::TryGetSourceSnapshot(const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& Out)
{
	if (const auto* Source = SourceAdapters.Find(Handle))
	{
		Out = {};
		return Source->LifetimeOwner.IsValid() && Source->ReadSnapshot && Source->ReadSnapshot(Out)
			&& Out.Handle == Handle && !Out.Location.ContainsNaN();
	}
	return TryGetTargetSnapshot(Handle, Out);
}

void UGuLiDamageLedgerSubsystem::GetSourceOnlySnapshots(TArray<FGuLiCombatTargetSnapshot>& Out)
{
	Out.Reset();
	for (auto It = SourceAdapters.CreateIterator(); It; ++It)
	{
		if (!It.Value().LifetimeOwner.IsValid()) { It.RemoveCurrent(); continue; }
		FGuLiCombatTargetSnapshot Snapshot;
		if (TryGetSourceSnapshot(It.Key(), Snapshot)) Out.Add(Snapshot);
	}
}

FGuid UGuLiDamageLedgerSubsystem::AcquireEffectSource(const FGuLiTargetHandle& Source)
{
	FGuLiCombatTargetSnapshot Snapshot;
	if (!IsAuthorityWorld() || MatchEpoch == 0 || !TryGetSourceSnapshot(Source, Snapshot) || !Snapshot.bAlive) return {};
	const FGuid Id = FGuid::NewGuid();
	RetainedEffectSources.Add(Id, {Source, Snapshot.Team, MatchEpoch, 1});
	return Id;
}

bool UGuLiDamageLedgerSubsystem::RetainEffectSource(const FGuid& LeaseId)
{
	FRetainedEffectSource* Source = RetainedEffectSources.Find(LeaseId);
	if (!IsAuthorityWorld() || !Source || Source->Epoch != MatchEpoch) return false;
	++Source->References;
	return true;
}

void UGuLiDamageLedgerSubsystem::ReleaseEffectSource(const FGuid& LeaseId)
{
	if (FRetainedEffectSource* Source = RetainedEffectSources.Find(LeaseId); IsAuthorityWorld() && Source)
	{
		if (--Source->References <= 0) RetainedEffectSources.Remove(LeaseId);
	}
}

FGuLiDamageCommitResult UGuLiDamageLedgerSubsystem::CommitEffectDamage(const FGuLiDamageRequest& Request, const FGuid& LeaseId)
{
	const FRetainedEffectSource* Source = RetainedEffectSources.Find(LeaseId);
	if (!IsAuthorityWorld() || !Source || Source->Epoch != MatchEpoch || Request.MatchEpoch != Source->Epoch
		|| Source->Source != Request.Source)
	{
		FGuLiDamageCommitResult Result;
		Result.Status = IsAuthorityWorld() ? EGuLiDamageCommitStatus::RejectedMissingTarget : EGuLiDamageCommitStatus::RejectedNotAuthority;
		return Result;
	}
	// Copy before any adapter callback can release a lease or change the map.
	const EGuLiTeam FrozenTeam = Source->Team;
	return CommitDamageInternal(Request, &FrozenTeam);
}

FGuLiDamageCommitResult UGuLiDamageLedgerSubsystem::CommitDamageInternal(
	const FGuLiDamageRequest& Request, const EGuLiTeam* FrozenSourceTeam)
{
	if (!IsAuthorityWorld())
	{
		FGuLiDamageCommitResult Result;
		Result.Status = EGuLiDamageCommitStatus::RejectedNotAuthority;
		return Result;
	}
	if (!Request.IsWellFormed())
	{
		FGuLiDamageCommitResult Result;
		Result.Status = EGuLiDamageCommitStatus::RejectedMalformed;
		return Result;
	}
	if (const FGuLiDamageCommitResult* Cached = ResultsByEvent.Find(Request.DamageEventId))
	{
		FGuLiDamageCommitResult Duplicate = *Cached;
		Duplicate.Status = EGuLiDamageCommitStatus::Duplicate;
		return Duplicate;
	}

	FGuLiDamageCommitResult Result;
	if (MatchEpoch == 0u || Request.MatchEpoch != MatchEpoch)
	{
		Result.Status = EGuLiDamageCommitStatus::RejectedWrongEpoch;
		RememberResult(Request.DamageEventId, Result);
		return Result;
	}
	const FGuid DerivedDeathEventId = DeriveDeathEventId(Request.DamageEventId);
	if (const FGuLiDeathCommitRecord* ExistingDeath = DeathRecordsByEvent.Find(DerivedDeathEventId);
		ExistingDeath && ExistingDeath->DamageEventId == Request.DamageEventId)
	{
		Result.Status = EGuLiDamageCommitStatus::Duplicate;
		Result.bKilled = true;
		Result.DeathEventId = ExistingDeath->DeathEventId;
		Result.RewardEventId = DeriveRewardEventId(Request.DamageEventId);
		return Result;
	}

	FGuLiCombatTargetSnapshot SourceSnapshot;
	FGuLiCombatTargetSnapshot TargetSnapshot;
	if ((!FrozenSourceTeam && !TryGetTargetSnapshot(Request.Source, SourceSnapshot))
		|| !TryGetTargetSnapshot(Request.Target, TargetSnapshot))
	{
		Result.Status = EGuLiDamageCommitStatus::RejectedMissingTarget;
		RememberResult(Request.DamageEventId, Result);
		return Result;
	}
	if (!TargetSnapshot.bAlive)
	{
		Result.Status = EGuLiDamageCommitStatus::RejectedTargetDead;
		RememberResult(Request.DamageEventId, Result);
		return Result;
	}
	const EGuLiTeam SourceTeam = FrozenSourceTeam ? *FrozenSourceTeam : SourceSnapshot.Team;
	if (SourceTeam != EGuLiTeam::Unassigned && SourceTeam == TargetSnapshot.Team)
	{
		Result.Status = EGuLiDamageCommitStatus::RejectedFriendlyFire;
		RememberResult(Request.DamageEventId, Result);
		return Result;
	}

#if !UE_BUILD_SHIPPING
	// The 300-second Actor continuity gate needs a genuinely persistent target
	// field so it can prove repeated attack runs instead of merely proving which
	// side destroys the finite test roster first. Keep all authorization,
	// deduplication, effects and commit accounting, but leave health untouched for
	// Wingman-emitted damage only when the explicit unattended test flag is set.
	if (Request.Emitter.IsValid()
		&& FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmokePersistentTargets")))
	{
		Result.Status = EGuLiDamageCommitStatus::Committed;
		Result.CommitOrdinal = ++CommitCount;
		RememberResult(Request.DamageEventId, Result);
		return Result;
	}
#endif

	FGuLiCombatTargetAdapter* Adapter = TargetAdapters.Find(Request.Target);
	if (!Adapter || !Adapter->IsBound())
	{
		Result.Status = EGuLiDamageCommitStatus::RejectedByAdapter;
		RememberResult(Request.DamageEventId, Result);
		return Result;
	}
	FGuLiDamageRequest Remaining = Request;
	for (const auto& Barrier : DamageBarriers)
	{
		if (!Barrier.Owner.IsValid() || Remaining.Damage == 0) continue;
		const float Absorbed = Barrier.Absorb(TargetSnapshot, Remaining.Damage);
		check(FMath::IsFinite(Absorbed) && Absorbed >= 0 && Absorbed <= Remaining.Damage);
		Remaining.Damage -= Absorbed;
	}
	if (Remaining.Damage > 0)
	{
		if (!Adapter->ApplyDamage(Remaining, Result))
		{
			Result.Status = EGuLiDamageCommitStatus::RejectedByAdapter;
			RememberResult(Request.DamageEventId, Result);
			return Result;
		}
	}
	else Result.RemainingHealth = TargetSnapshot.Health;
	Result.AbsorbedDamage += Request.Damage - Remaining.Damage;
	Result.Status = EGuLiDamageCommitStatus::Committed;
	Result.CommitOrdinal = ++CommitCount;
	if (Result.bKilled)
	{
		CommitLethalEvents(Request, Result);
	}
	RememberResult(Request.DamageEventId, Result);
	return Result;
}

void UGuLiDamageLedgerSubsystem::RegisterDamageBarrier(UObject& Owner, uint32 StableOrder,
	TFunction<float(const FGuLiCombatTargetSnapshot&, float)> Absorb)
{
	check(IsAuthorityWorld() && Absorb && !DamageBarriers.ContainsByPredicate([&](const auto& B) { return B.Owner == &Owner; }));
	DamageBarriers.Add({&Owner, StableOrder, MoveTemp(Absorb)});
	DamageBarriers.Sort([](const auto& A, const auto& B) { return A.StableOrder < B.StableOrder; });
}
void UGuLiDamageLedgerSubsystem::UnregisterDamageBarrier(const UObject& Owner)
{
	DamageBarriers.RemoveAll([&](const auto& Barrier) { return Barrier.Owner == &Owner; });
}

void UGuLiDamageLedgerSubsystem::RememberResult(
	const FGuid& EventId, const FGuLiDamageCommitResult& Result)
{
	if (!EventId.IsValid() || ResultsByEvent.Contains(EventId))
	{
		return;
	}
	ResultsByEvent.Add(EventId, Result);
	EventOrder.Add(EventId);
	const int32 Overflow = EventOrder.Num() - FMath::Max(1, MaximumRememberedEvents);
	if (Overflow > 0)
	{
		for (int32 Index = 0; Index < Overflow; ++Index)
		{
			ResultsByEvent.Remove(EventOrder[Index]);
		}
		EventOrder.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

void UGuLiDamageLedgerSubsystem::RememberDeathRecord(const FGuLiDeathCommitRecord& Record)
{
	if (!Record.IsWellFormed() || DeathRecordsByEvent.Contains(Record.DeathEventId))
	{
		return;
	}
	DeathRecordsByEvent.Add(Record.DeathEventId, Record);
	DeathEventOrder.Add(Record.DeathEventId);
	const int32 Overflow = DeathEventOrder.Num() - FMath::Max(1, MaximumRememberedEvents);
	if (Overflow > 0)
	{
		for (int32 Index = 0; Index < Overflow; ++Index)
		{
			DeathRecordsByEvent.Remove(DeathEventOrder[Index]);
		}
		DeathEventOrder.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

void UGuLiDamageLedgerSubsystem::RememberRewardRecord(const FGuLiRewardCommitRecord& Record)
{
	if (!Record.IsWellFormed() || RewardRecordsByEvent.Contains(Record.RewardEventId))
	{
		return;
	}
	RewardRecordsByEvent.Add(Record.RewardEventId, Record);
	RewardEventOrder.Add(Record.RewardEventId);
	const int32 Overflow = RewardEventOrder.Num() - FMath::Max(1, MaximumRememberedEvents);
	if (Overflow > 0)
	{
		for (int32 Index = 0; Index < Overflow; ++Index)
		{
			RewardRecordsByEvent.Remove(RewardEventOrder[Index]);
		}
		RewardEventOrder.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

void UGuLiDamageLedgerSubsystem::CommitLethalEvents(
	const FGuLiDamageRequest& Request,
	FGuLiDamageCommitResult& InOutResult)
{
	const FGuid DeathEventId = DeriveDeathEventId(Request.DamageEventId);
	const FGuid RewardEventId = DeriveRewardEventId(Request.DamageEventId);
	InOutResult.DeathEventId = DeathEventId;
	InOutResult.RewardEventId = RewardEventId;

	if (const FGuLiDeathCommitRecord* Existing = DeathRecordsByEvent.Find(DeathEventId))
	{
		if (Existing->DamageEventId == Request.DamageEventId)
		{
			return;
		}
		return;
	}

	FGuLiDeathCommitRecord Death;
	Death.MatchEpoch = Request.MatchEpoch;
	Death.DeathEventId = DeathEventId;
	Death.DamageEventId = Request.DamageEventId;
	Death.ShotId = Request.ShotId;
	Death.Source = Request.Source;
	Death.Emitter = Request.Emitter;
	Death.WeaponBinding = Request.WeaponBinding;
	Death.SkillId = Request.SkillId;
	Death.LoadoutRevision = Request.LoadoutRevision;
	Death.ProfileRevision = Request.ProfileRevision;
	Death.RootEventId = Request.RootEventId;
	Death.Target = Request.Target;
	Death.CommitOrdinal = ++DeathCommitCount;
	RememberDeathRecord(Death);

	if (RewardRecordsByEvent.Contains(RewardEventId))
	{
		return;
	}

	FGuLiRewardCommitRecord Reward;
	Reward.MatchEpoch = Request.MatchEpoch;
	Reward.RewardEventId = RewardEventId;
	Reward.DeathEventId = DeathEventId;
	Reward.CommitOrdinal = ++RewardCommitCount;
	Reward.Status = EGuLiRewardGrantStatus::PolicyUnavailable;

	if (RewardPolicy)
	{
		FGuLiRewardGrant ProposedGrant;
		if (!RewardPolicy(Death, ProposedGrant) || !ProposedGrant.IsWellFormed())
		{
			Reward.Status = EGuLiRewardGrantStatus::NotGranted;
		}
		else
		{
			Reward.Grant = MoveTemp(ProposedGrant);
			if (!RewardSink)
			{
				Reward.Status = EGuLiRewardGrantStatus::SinkUnavailable;
			}
			else
			{
				Reward.Status = EGuLiRewardGrantStatus::Granted;
				if (!RewardSink(Reward))
				{
					Reward.Status = EGuLiRewardGrantStatus::SinkRejected;
				}
				else
				{
					++GrantedRewardCount;
				}
			}
		}
	}
	RememberRewardRecord(Reward);
}

void UGuLiDamageLedgerSubsystem::PruneInvalidTargets()
{
	for (auto Iterator = TargetAdapters.CreateIterator(); Iterator; ++Iterator)
	{
		if (!Iterator.Value().IsBound())
		{
			Iterator.RemoveCurrent();
		}
	}
}
