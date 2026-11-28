#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiNetConversions.h"
#include "Commander/UI/GuLiSceneUISourceRegistry.h"
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"

#include "Components/PrimitiveComponent.h"
#include "HierarchicalHashGrid2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

// Each target occupies one size-appropriate cell. Large and off-centre bodies remain
// discoverable without expanding a local query by the largest unit on the map.
struct FGuLiCombatBodyIndex
{
	using FGrid = THierarchicalHashGrid2D<5, 4, int32>;
	struct FEntry { int32 Slot; FGrid::FCellLocation Cell; float Reach; };
	FGrid Grid{1000.f};
	TSparseArray<FGuLiTargetHandle> Handles;
	TMap<FGuLiTargetHandle, FEntry> Entries;

	void Set(const FGuLiTargetHandle& Handle, const FVector& Anchor, float Reach)
	{
		const FBox Bounds = FBox::BuildAABB(Anchor, FVector(Reach));
		if (FEntry* Entry = Entries.Find(Handle))
		{
			Entry->Cell = Grid.Move(Entry->Slot, Entry->Cell, Bounds);
			Entry->Reach = Reach;
			return;
		}
		const int32 Slot = Handles.Add(Handle);
		Entries.Add(Handle, {Slot, Grid.Add(Slot, Bounds), Reach});
	}

	void Remove(const FGuLiTargetHandle& Handle)
	{
		FEntry Entry;
		if (Entries.RemoveAndCopyValue(Handle, Entry))
		{
			Grid.Remove(Entry.Slot, Entry.Cell);
			Handles.RemoveAt(Entry.Slot);
		}
	}
};

namespace
{
void ReadActorBody(const AActor& Owner, FGuLiCombatTargetSnapshot& Snapshot)
{
	Snapshot.Location = Owner.GetActorLocation();
	if (const auto* Root = Cast<UPrimitiveComponent>(Owner.GetRootComponent()))
	{
		Snapshot.CollisionRadius = Root->Bounds.SphereRadius;
		Snapshot.HitCenter = Root->Bounds.Origin;
		Snapshot.bHasHitCenter = true;
	}
	else if (const FBox Bounds = Owner.GetComponentsBoundingBox(false); Bounds.IsValid)
	{
		Snapshot.HitCenter = Bounds.GetCenter();
		Snapshot.bHasHitCenter = true;
	}
}

float PlanarBodyReach(const FGuLiCombatTargetSnapshot& Snapshot)
{
	return Snapshot.CollisionRadius + float(FVector::Dist2D(Snapshot.Location, Snapshot.GetHitCenter()));
}

bool IsCurrentSoldierHandle(const FGuLiTargetHandle& Handle,uint32 Epoch)
{
    return Handle.Kind!=EGuLiTargetKind::CommanderSoldier || (Epoch
        && Handle.AuthorityId==GuLiCombatTargets::MakeCommanderSoldierTargetHandle(Epoch,Handle.LocalId).AuthorityId);
}
	uint32 NextRevision(const uint32 Current)
	{
		const uint32 Next = Current + 1u;
		return Next == 0u ? 1u : Next;
	}
}

bool FGuLiCombatHealthState::IsWellFormed() const
{
	return GuLiNet::ValidHealth(GuLiNetBridge::ToCore(*this));
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
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick = false;
}



void UGuLiCombatHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && UGuLiNetworkSettings::CanPresent(GetWorld()) && !GetOwner()->FindComponentByClass<UGuLiUnitFeedbackComponent>())
	{
		auto* Feedback = NewObject<UGuLiUnitFeedbackComponent>(GetOwner());
		// Committed health is applied locally; each rendered client owns its feedback observer.
		Feedback->SetIsReplicated(false);
		Feedback->bPlayDestructionEffect = !GetOwner()->IsA<AGuLiStrikeShip>();
		GetOwner()->AddInstanceComponent(Feedback);
		Feedback->RegisterComponent();
	}

}

void UGuLiCombatHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromLedger();
	Super::EndPlay(EndPlayReason);
}

void UGuLiCombatHealthComponent::OnRep_TargetIdentity()
{
	UGuLiSceneUISourceRegistry::Notify(GetOwner(), EGuLiSceneUIChange::Membership);
}

bool UGuLiCombatHealthComponent::ApplyHybridHealth(const FGuLiTargetHandle& Handle, EGuLiTeam Faction,
	const FGuLiCombatHealthState& State, uint64 Epoch, uint64 LedgerRevision, bool bBusinessEffects)
{
	if (!GetOwner() || !UGuLiNetworkSettings::IsHybrid(GetWorld()) || !Handle.IsValid()
		|| Faction == EGuLiTeam::Unassigned || !State.IsWellFormed() || !Epoch || Epoch < HybridEpoch
		|| (Epoch == HybridEpoch && LedgerRevision < HybridLedgerRevision)) return false;
	// Register the same life handle used by collision queries and damage. Every
	// actor adapter applies health, including miners and buildings which have no
	// weapon component to install this mapping on their behalf. Keep the mapping
	// after death for admitted effects and pending receipts.
	if (const auto* Identity = UGuLiHybridActorComponent::Find(GetOwner()))
	{
		const auto& Key = Identity->GetObjectKey();
		auto* Hybrid = GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
		if (!Hybrid) return false;
        // Gameplay handles can be derived (members, Mass, etc.). The registered
        // adapter mapping is authoritative; their GUID layout is not the object key.
        FGuLiHybridObjectKey Bound;
        if (Hybrid->ResolveTarget(Handle,Bound))
        { if (Bound!=Key) return false; }
        else
        {
            FGuLiTargetHandle Expected;
            if (!Hybrid->ResolveObjectTarget(Key,Expected) || Expected!=Handle || !Hybrid->BindTarget(Handle,Key)) return false;
        }
	}
	const bool bNewLife = TargetHandle != Handle;
	const bool bIdentityChanged = bNewLife || Team != Faction;
	const bool bStateChanged = Epoch != HybridEpoch || LedgerRevision != HybridLedgerRevision || !HealthState.IsWellFormed();
	if (bNewLife) { bHybridObservedAlive = false; bHybridDeathDelivered = false; }
	if (!State.bDead) bHybridObservedAlive = true;
	if (bStateChanged || bIdentityChanged)
	{
		const bool bWasDead = HealthState.bDead;
		if (bIdentityChanged) { UnregisterFromLedger(); TargetHandle = Handle; Team = Faction; OnRep_TargetIdentity(); }
		HealthState = State; HybridEpoch = Epoch; HybridLedgerRevision = LedgerRevision;
		RegisterWithLedger();
		UGuLiSceneUISourceRegistry::Notify(GetOwner(), bWasDead != State.bDead ? EGuLiSceneUIChange::Membership : EGuLiSceneUIChange::Content);
		OnHealthChanged.Broadcast(State.Health, State.MaxHealth);
	}
	// Numeric fragments arrive before the commit notification. Equal numeric
	// revisions must still deliver that event once; importing an already dead
	// life must not replay its death effects.
	if (bBusinessEffects && State.bDead && bHybridObservedAlive && !bHybridDeathDelivered)
	{ bHybridDeathDelivered = true; OnDeath.Broadcast(); }
	return true;
}

void UGuLiCombatHealthComponent::OnRep_HealthState(const FGuLiCombatHealthState& PreviousState)
{
	OnHealthChanged.Broadcast(HealthState.Health, HealthState.MaxHealth);
	if (HealthState.bDead && !PreviousState.bDead)
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
		if (!Ledger->RegisterHealthComponent(*this)) return;
		USceneComponent* Root = GetOwner()->GetRootComponent();
		if (FootprintRoot.Get() != Root)
		{
			if (auto* Old = FootprintRoot.Get()) Old->TransformUpdated.Remove(FootprintTransformHandle);
			FootprintRoot = Root;
			if (Root) FootprintTransformHandle = Root->TransformUpdated.AddWeakLambda(this,
				[this](USceneComponent*, EUpdateTransformFlags, ETeleportType) { RefreshActorFootprint(); });
		}
		RefreshActorFootprint();
	}
}

void UGuLiCombatHealthComponent::RefreshActorFootprint()
{
	if (auto* Ledger = GetWorld() ? GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>() : nullptr)
	{
		FGuLiCombatTargetSnapshot Body;
		ReadActorBody(*GetOwner(), Body);
		Ledger->SetTargetFootprint(TargetHandle, Body.Location, PlanarBodyReach(Body));
	}
}

void UGuLiCombatHealthComponent::UnregisterFromLedger()
{
	if (auto* Root = FootprintRoot.Get()) Root->TransformUpdated.Remove(FootprintTransformHandle);
	FootprintRoot.Reset();
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
	UGuLiSceneUISourceRegistry::Notify(GetOwner(), bWasDead != HealthState.bDead ? EGuLiSceneUIChange::Membership : EGuLiSceneUIChange::Content);
	OnHealthChanged.Broadcast(HealthState.Health, HealthState.MaxHealth);
	if (!bWasDead && HealthState.bDead)
	{
		OnDeath.Broadcast();
	}
}

void UGuLiDamageLedgerSubsystem::Deinitialize()
{
	PendingSubmissions.Reset();
	RetainedEffectSources.Reset();
	TargetAdapters.Reset();
	BodyIndex.Reset();
	TargetDomains.Reset();
	SourceAdapters.Reset();
	ResultsByEvent.Reset();
	EventOrder.Reset();
	MatchEpoch = 0u;
	CommitCount = 0u;
	Super::Deinitialize();
}

bool UGuLiDamageLedgerSubsystem::IsAuthorityWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

bool UGuLiDamageLedgerSubsystem::BindMatchEpoch(const uint32 NewMatchEpoch)
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
	PendingSubmissions.Reset();
	RetainedEffectSources.Reset();
	ResultsByEvent.Reset();
	EventOrder.Reset();
	CommitCount = 0u;
	PruneInvalidTargets();
	return true;
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
	TargetDomains.FindOrAdd(Handle.Kind).Add(Handle);
	if (!BodyIndex) BodyIndex = MakeShared<FGuLiCombatBodyIndex>();
	FGuLiCombatTargetSnapshot Snapshot;
	if (TargetAdapters.FindChecked(Handle).ReadSnapshot(Snapshot) && Snapshot.Handle == Handle)
		SetTargetFootprint(Handle, Snapshot.Location, PlanarBodyReach(Snapshot));
	return true;
}

void UGuLiDamageLedgerSubsystem::SetTargetFootprint(const FGuLiTargetHandle& Handle, const FVector& Anchor, float Reach)
{
	if (BodyIndex && TargetAdapters.Contains(Handle) && !Anchor.ContainsNaN() && FMath::IsFinite(Reach) && Reach >= 0)
		BodyIndex->Set(Handle, Anchor, Reach);
}

void UGuLiDamageLedgerSubsystem::MoveTargetFootprint(const FGuLiTargetHandle& Handle, const FVector& Anchor)
{
	if (!BodyIndex || Anchor.ContainsNaN()) return;
	if (auto* Entry = BodyIndex->Entries.Find(Handle))
		Entry->Cell = BodyIndex->Grid.Move(Entry->Slot, Entry->Cell, FBox::BuildAABB(Anchor, FVector(Entry->Reach)));
	else
	{
		// A body can first register while phased and become hittable on relocation.
		FGuLiCombatTargetSnapshot Snapshot;
		if (TryGetTargetSnapshot(Handle, Snapshot)) SetTargetFootprint(Handle, Anchor, PlanarBodyReach(Snapshot));
	}
}

void UGuLiDamageLedgerSubsystem::GetNearbyTargetSnapshots(const FBox& Bounds, TArray<FGuLiCombatTargetSnapshot>& Out)
{
	Out.Reset();
	if (!BodyIndex || !Bounds.IsValid) return;
	TArray<int32, TInlineAllocator<32>> Slots;
	BodyIndex->Grid.QuerySmall(Bounds, Slots);
	for (int32 Slot : Slots)
	{
		const FGuLiTargetHandle Handle = BodyIndex->Handles[Slot];
		FGuLiCombatTargetSnapshot Snapshot;
		if (TryGetTargetSnapshot(Handle, Snapshot) && Snapshot.bAlive && FMath::IsFinite(Snapshot.CollisionRadius)
			&& Snapshot.CollisionRadius >= 0 && Bounds.ComputeSquaredDistanceToPoint(Snapshot.GetHitCenter()) <= FMath::Square(double(Snapshot.CollisionRadius)))
			Out.Add(MoveTemp(Snapshot));
	}
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
		ReadActorBody(*Owner, OutSnapshot);
		const auto& Vitals = Health->GetHealthState();
		OutSnapshot.Health = Vitals.Health;
		OutSnapshot.MaxHealth = Vitals.MaxHealth;
		OutSnapshot.Shield = Vitals.Shield;
		OutSnapshot.MaxShield = Vitals.MaxShield;
		OutSnapshot.bAlive = Health->IsAlive();
		OutSnapshot.CollisionActor = const_cast<AActor*>(Owner);
		return !OutSnapshot.Location.ContainsNaN();
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
			if (BodyIndex) BodyIndex->Remove(Handle);
			if (auto* Domain = TargetDomains.Find(Handle.Kind)) Domain->Remove(Handle);
		}
	}
}

bool UGuLiDamageLedgerSubsystem::TryGetTargetSnapshot(
	const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot)
{
	OutSnapshot = FGuLiCombatTargetSnapshot();
	if (!IsCurrentSoldierHandle(Handle,MatchEpoch)) return false;
	FGuLiCombatTargetAdapter* Adapter = TargetAdapters.Find(Handle);
	if (!Adapter || !Adapter->IsBound() || !Adapter->ReadSnapshot(OutSnapshot)
		|| OutSnapshot.Handle != Handle || OutSnapshot.Location.ContainsNaN())
	{
		if (Adapter && !Adapter->LifetimeOwner.IsValid())
		{
			TargetAdapters.Remove(Handle);
			if (BodyIndex) BodyIndex->Remove(Handle);
			if (auto* Domain = TargetDomains.Find(Handle.Kind)) Domain->Remove(Handle);
		}
		return false;
	}
	return true;
}

void UGuLiDamageLedgerSubsystem::GetTargetSnapshots(
	TArray<FGuLiCombatTargetSnapshot>& OutSnapshots, const EGuLiTargetKind Kind)
{
	OutSnapshots.Reset();
	if (!IsAuthorityWorld())
	{
		return;
	}
	if (Kind != EGuLiTargetKind::None)
	{
		if (auto* Domain = TargetDomains.Find(Kind))
		{
			OutSnapshots.Reserve(Domain->Num());
			for (auto It = Domain->CreateIterator(); It; ++It)
			{
				const FGuLiTargetHandle Handle = *It;
				if (!IsCurrentSoldierHandle(Handle,MatchEpoch)) { TargetAdapters.Remove(Handle); if (BodyIndex) BodyIndex->Remove(Handle); It.RemoveCurrent(); continue; }
				const auto* Adapter = TargetAdapters.Find(Handle);
				if (!Adapter || !Adapter->IsBound())
				{ TargetAdapters.Remove(Handle); if (BodyIndex) BodyIndex->Remove(Handle); It.RemoveCurrent(); continue; }
				FGuLiCombatTargetSnapshot Snapshot;
				if (Adapter->ReadSnapshot(Snapshot) && Snapshot.Handle == Handle && Snapshot.Handle.IsValid()
					&& !Snapshot.Location.ContainsNaN()) OutSnapshots.Add(MoveTemp(Snapshot));
			}
		}
	}
	else
	{
		PruneInvalidTargets();
		OutSnapshots.Reserve(TargetAdapters.Num());
		for (const TPair<FGuLiTargetHandle, FGuLiCombatTargetAdapter>& Pair : TargetAdapters)
		{
			FGuLiCombatTargetSnapshot Snapshot;
			if (IsCurrentSoldierHandle(Pair.Key,MatchEpoch) && Pair.Value.IsBound() && Pair.Value.ReadSnapshot(Snapshot)
				&& Snapshot.Handle == Pair.Key && Snapshot.Handle.IsValid()
				&& !Snapshot.Location.ContainsNaN()) OutSnapshots.Add(MoveTemp(Snapshot));
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

FGuid UGuLiDamageLedgerSubsystem::SubmitDamage(const FGuLiDamageRequest& Request,UObject* Owner,TFunction<void(const FGuLiDamageCommitResult&)> Callback)
{
    return SubmitHybridDamage(Request,{},Owner,MoveTemp(Callback));
}
FGuid UGuLiDamageLedgerSubsystem::SubmitEffectDamage(const FGuLiDamageRequest& Request,const FGuid& Lease,UObject* Owner,TFunction<void(const FGuLiDamageCommitResult&)> Callback)
{
    return SubmitHybridDamage(Request,Lease,Owner,MoveTemp(Callback));
}
FGuid UGuLiDamageLedgerSubsystem::SubmitHybridDamage(const FGuLiDamageRequest& Request,const FGuid& Lease,UObject* Owner,TFunction<void(const FGuLiDamageCommitResult&)> Callback)
{
	auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	if (!Hybrid || !Request.IsWellFormed() || Request.MatchEpoch!=MatchEpoch) return {};
	if (PendingSubmissions.Contains(Request.DamageEventId)) return Request.DamageEventId;
	uint64 Started=Hybrid->GetLogicalTick();
	if (Lease.IsValid())
	{
		const auto* Source=RetainedEffectSources.Find(Lease);
		if (!Source || Source->Source!=Request.Source || Source->Epoch!=MatchEpoch) return {};
		Started=Source->StartedAt;
		if (!RetainEffectSource(Lease)) return {};
	}
	FPendingSubmission Pending; Pending.RetainedLease=Lease; Pending.CallbackOwner=Owner; Pending.Callback=MoveTemp(Callback);
	PendingSubmissions.Add(Request.DamageEventId,MoveTemp(Pending));
	const TWeakObjectPtr<UGuLiDamageLedgerSubsystem> Weak(this); const FGuid Event=Request.DamageEventId; const uint32 SubmittedEpoch=MatchEpoch;
	const FGuid Submitted=Hybrid->SubmitDamage(Request,Started,[Weak,Event,SubmittedEpoch](const FGuLiDamageCommitResult& Result)
	{
		auto* Self=Weak.Get(); if (!Self || Self->MatchEpoch!=SubmittedEpoch) return;
		auto* P=Self->PendingSubmissions.Find(Event); if (!P) return;
		auto Callback=MoveTemp(P->Callback); auto Owner=P->CallbackOwner; const auto Lease=P->RetainedLease;
		Self->PendingSubmissions.Remove(Event);
		if (!Self->ResultsByEvent.Contains(Event))
		{
			if (Result.Status==EGuLiDamageCommitStatus::Committed) ++Self->CommitCount;
			Self->RememberResult(Event,Result);
		}
		if (Lease.IsValid()) Self->ReleaseEffectSource(Lease);
		if (Owner.IsValid() && Callback) Callback(Result);
	});
	if (!Submitted.IsValid())
	{
		PendingSubmissions.Remove(Event); if (Lease.IsValid()) ReleaseEffectSource(Lease);
	}
	return Submitted;
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
	if (!IsCurrentSoldierHandle(Handle,MatchEpoch)) { Out={}; return false; }
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
		if (!It.Value().LifetimeOwner.IsValid() || !IsCurrentSoldierHandle(It.Key(),MatchEpoch)) { It.RemoveCurrent(); continue; }
		FGuLiCombatTargetSnapshot Snapshot;
		if (TryGetSourceSnapshot(It.Key(), Snapshot)) Out.Add(Snapshot);
	}
}

FGuid UGuLiDamageLedgerSubsystem::AcquireEffectSource(const FGuLiTargetHandle& Source)
{
	FGuLiCombatTargetSnapshot Snapshot;
	if (!IsAuthorityWorld() || MatchEpoch == 0 || !TryGetSourceSnapshot(Source, Snapshot) || !Snapshot.bAlive) return {};
	const FGuid Id = FGuid::NewGuid();
	const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	RetainedEffectSources.Add(Id, {Source, Snapshot.Team, MatchEpoch, 1, Hybrid ? Hybrid->GetLogicalTick() : 0});
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

void UGuLiDamageLedgerSubsystem::PruneInvalidTargets()
{
	for (auto Iterator = TargetAdapters.CreateIterator(); Iterator; ++Iterator)
	{
		if (!Iterator.Value().IsBound() || (MatchEpoch && !IsCurrentSoldierHandle(Iterator.Key(), MatchEpoch)))
		{
			if (BodyIndex) BodyIndex->Remove(Iterator.Key());
			if (auto* Domain = TargetDomains.Find(Iterator.Key().Kind)) Domain->Remove(Iterator.Key());
			Iterator.RemoveCurrent();
		}
	}
}
