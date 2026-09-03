// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"

namespace
{
	bool GuidLess(const FGuid& Lhs, const FGuid& Rhs)
	{
		if (Lhs.A != Rhs.A) return Lhs.A < Rhs.A;
		if (Lhs.B != Rhs.B) return Lhs.B < Rhs.B;
		if (Lhs.C != Rhs.C) return Lhs.C < Rhs.C;
		return Lhs.D < Rhs.D;
	}
}

FGuLiWingmanRelayServer* FGuLiWingmanRelayAuthorityRegistry::CreateGroup(
	const uint32 MatchEpoch,
	const FGuLiWingmanGroupHandle& Group,
	const FGuid& InitialOwnerPlayerGuid,
	const FGuid& BackupPlayerGuid,
	const FGuLiGroupAbilityConfigSnapshot& InitialAbilityConfig,
	const double NowSeconds,
	const FGuLiWingmanRelayTuning& Tuning)
{
	if (Groups.Contains(Group))
	{
		return nullptr;
	}

	FGroupEntry Entry;
	Entry.Relay = MakeShared<FGuLiWingmanRelayServer>();
	if (!Entry.Relay->InitializeGroup(
		MatchEpoch,
		Group,
		InitialOwnerPlayerGuid,
		BackupPlayerGuid,
		InitialAbilityConfig,
		NowSeconds,
		Tuning))
	{
		return nullptr;
	}
	FGroupEntry& Stored = Groups.Add(Group, MoveTemp(Entry));
	return Stored.Relay.Get();
}

FGuLiWingmanRelayServer* FGuLiWingmanRelayAuthorityRegistry::FindGroup(
	const FGuLiWingmanGroupHandle& Group)
{
	FGroupEntry* Entry = Groups.Find(Group);
	return Entry ? Entry->Relay.Get() : nullptr;
}

const FGuLiWingmanRelayServer* FGuLiWingmanRelayAuthorityRegistry::FindGroup(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGroupEntry* Entry = Groups.Find(Group);
	return Entry ? Entry->Relay.Get() : nullptr;
}

bool FGuLiWingmanRelayAuthorityRegistry::RevokeGroup(
	const FGuLiWingmanGroupHandle& Group, const double NowSeconds)
{
	FGroupEntry* Entry = Groups.Find(Group);
	if (!Entry || !Entry->Relay || !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return false;
	}
	if (Entry->Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Revoked)
	{
		Entry->Relay->Revoke(NowSeconds);
	}
	Entry->bAwaitingConnectedOwner = false;
	ClearRuntimeHooks(*Entry);
	return true;
}

bool FGuLiWingmanRelayAuthorityRegistry::DestroyGroup(
	const FGuLiWingmanGroupHandle& Group, const double NowSeconds)
{
	if (!RevokeGroup(Group, NowSeconds))
	{
		return false;
	}
	return Groups.Remove(Group) == 1;
}

TArray<FGuLiWingmanGroupHandle> FGuLiWingmanRelayAuthorityRegistry::FindGroupsOwnedBy(
	const FGuid& OwnerPlayerGuid) const
{
	TArray<FGuLiWingmanGroupHandle> Result;
	if (!OwnerPlayerGuid.IsValid())
	{
		return Result;
	}
	for (const TPair<FGuLiWingmanGroupHandle, FGroupEntry>& Pair : Groups)
	{
		if (Pair.Value.Relay && Pair.Value.Relay->GetLeaseState().OwnerPlayerGuid == OwnerPlayerGuid)
		{
			Result.Add(Pair.Key);
		}
	}
	Result.Sort(&FGuLiWingmanRelayAuthorityRegistry::GroupLess);
	return Result;
}

TArray<FGuLiWingmanGroupHandle> FGuLiWingmanRelayAuthorityRegistry::GetAwaitingOwnerGroups() const
{
	TArray<FGuLiWingmanGroupHandle> Result;
	for (const TPair<FGuLiWingmanGroupHandle, FGroupEntry>& Pair : Groups)
	{
		if (Pair.Value.bAwaitingConnectedOwner && Pair.Value.Relay)
		{
			Result.Add(Pair.Key);
		}
	}
	Result.Sort(&FGuLiWingmanRelayAuthorityRegistry::GroupLess);
	return Result;
}

TArray<FGuLiWingmanOwnerLossAssignment> FGuLiWingmanRelayAuthorityRegistry::HandleOwnerDisconnected(
	const FGuid& DisconnectedPlayerGuid,
	const TArray<FGuid>& ConnectedCandidatePlayerGuids,
	const double NowSeconds)
{
	TArray<FGuLiWingmanOwnerLossAssignment> Assignments;
	if (!DisconnectedPlayerGuid.IsValid() || !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return Assignments;
	}

	TArray<FGuid> Candidates = ConnectedCandidatePlayerGuids;
	Candidates.Remove(DisconnectedPlayerGuid);
	NormalizeCandidates(Candidates);
	for (const FGuLiWingmanGroupHandle& Group : FindGroupsOwnedBy(DisconnectedPlayerGuid))
	{
		FGroupEntry* Entry = Groups.Find(Group);
		if (!Entry || !Entry->Relay)
		{
			continue;
		}

		const FGuid PreferredBackup = Entry->Relay->GetLeaseState().BackupPlayerGuid;
		Entry->EligibleBackupCandidates = Candidates;
		Entry->AttemptedBackupCandidates.Reset();
		Entry->bAwaitingConnectedOwner = true;
		FGuLiWingmanOwnerLossAssignment Assignment;
		if (StartNextOffer(Group, *Entry, PreferredBackup, NowSeconds, Assignment))
		{
			Assignments.Add(Assignment);
		}
		else if (Entry->Relay->SuspendForOwnerLoss(MakeNoOwnerSentinel(Group), NowSeconds))
		{
			Assignment.Group = Group;
			Assignment.Disposition = EGuLiWingmanOwnerLossDisposition::AwaitingConnectedOwner;
			Assignments.Add(Assignment);
		}
	}
	return Assignments;
}

TArray<FGuLiWingmanOwnerLossAssignment> FGuLiWingmanRelayAuthorityRegistry::AssignAwaitingGroups(
	const TArray<FGuid>& ConnectedCandidatePlayerGuids,
	const double NowSeconds)
{
	TArray<FGuLiWingmanOwnerLossAssignment> Assignments;
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return Assignments;
	}
	TArray<FGuid> Candidates = ConnectedCandidatePlayerGuids;
	NormalizeCandidates(Candidates);
	for (const FGuLiWingmanGroupHandle& Group : GetAwaitingOwnerGroups())
	{
		FGroupEntry* Entry = Groups.Find(Group);
		if (!Entry || !Entry->Relay)
		{
			continue;
		}
		for (const FGuid& Candidate : Candidates)
		{
			Entry->EligibleBackupCandidates.AddUnique(Candidate);
		}
		NormalizeCandidates(Entry->EligibleBackupCandidates);
		FGuLiWingmanOwnerLossAssignment Assignment;
		if (StartNextOffer(Group, *Entry, FGuid{}, NowSeconds, Assignment))
		{
			Assignments.Add(Assignment);
			Candidates.Remove(Assignment.NewOwnerPlayerGuid);
			if (Candidates.IsEmpty())
			{
				break;
			}
		}
	}
	return Assignments;
}

bool FGuLiWingmanRelayAuthorityRegistry::AssignAwaitingGroup(
	const FGuLiWingmanGroupHandle& Group,
	const TArray<FGuid>& ConnectedCandidatePlayerGuids,
	const double NowSeconds,
	FGuLiWingmanOwnerLossAssignment& OutAssignment)
{
	OutAssignment = FGuLiWingmanOwnerLossAssignment{};
	FGroupEntry* Entry = Groups.Find(Group);
	if (!Entry || !Entry->Relay || !Entry->bAwaitingConnectedOwner
		|| !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return false;
	}
	TArray<FGuid> Candidates = ConnectedCandidatePlayerGuids;
	NormalizeCandidates(Candidates);
	for (const FGuid& Candidate : Candidates)
	{
		Entry->EligibleBackupCandidates.AddUnique(Candidate);
	}
	NormalizeCandidates(Entry->EligibleBackupCandidates);
	if (!StartNextOffer(Group, *Entry, FGuid{}, NowSeconds, OutAssignment))
	{
		OutAssignment = FGuLiWingmanOwnerLossAssignment{};
		return false;
	}
	return true;
}

TArray<FGuLiWingmanOwnerLossAssignment>
FGuLiWingmanRelayAuthorityRegistry::RunLeaseMaintenance(const double NowSeconds)
{
	TArray<FGuLiWingmanOwnerLossAssignment> NewOffers;
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return NewOffers;
	}
	TArray<FGuLiWingmanGroupHandle> OrderedGroups;
	Groups.GetKeys(OrderedGroups);
	OrderedGroups.Sort(&FGuLiWingmanRelayAuthorityRegistry::GroupLess);
	for (const FGuLiWingmanGroupHandle& Group : OrderedGroups)
	{
		FGroupEntry* Entry = Groups.Find(Group);
		if (!Entry || !Entry->Relay)
		{
			continue;
		}
		const uint32 PreviousOfferRevision = Entry->Relay->GetPendingLeaseOffer().OfferRevision;
		if (!Entry->Relay->RunLeaseMaintenance(NowSeconds))
		{
			continue;
		}
		if (Entry->Relay->GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active
			&& !Entry->Relay->IsTransferInProgress()
			&& !Entry->Relay->GetPendingLeaseOffer().IsPending())
		{
			Entry->bAwaitingConnectedOwner = false;
			Entry->EligibleBackupCandidates.Reset();
			Entry->AttemptedBackupCandidates.Reset();
			continue;
		}
		const bool bOfferExpired = PreviousOfferRevision != 0u
			&& !Entry->Relay->GetPendingLeaseOffer().IsPending();
		const bool bNeedsOwner = Entry->bAwaitingConnectedOwner
			&& !Entry->Relay->GetPendingLeaseOffer().IsPending()
			&& (bOfferExpired
				|| Entry->Relay->GetActiveLeaseTransaction().State
					== EGuLiWingmanActiveLeaseTransactionState::NoOwner);
		if (bNeedsOwner)
		{
			FGuLiWingmanOwnerLossAssignment Assignment;
			if (StartNextOffer(Group, *Entry, FGuid{}, NowSeconds, Assignment))
			{
				NewOffers.Add(Assignment);
			}
			else
			{
				Entry->Relay->EnterNoOwner(NowSeconds);
			}
		}
	}
	return NewOffers;
}

bool FGuLiWingmanRelayAuthorityRegistry::AcknowledgeLeaseOfferReady(
	const FGuLiWingmanGroupHandle& Group,
	const FGuid& SenderPlayerGuid,
	const uint32 OfferRevision,
	const double NowSeconds,
	FGuLiWingmanOwnerLossAssignment& OutAssignment)
{
	OutAssignment = FGuLiWingmanOwnerLossAssignment{};
	FGroupEntry* Entry = Groups.Find(Group);
	if (!Entry || !Entry->Relay
		|| !Entry->Relay->AcknowledgeLeaseOfferReady(SenderPlayerGuid, OfferRevision, NowSeconds))
	{
		return false;
	}
	OutAssignment.Group = Group;
	OutAssignment.NewOwnerPlayerGuid = Entry->Relay->GetLeaseState().OwnerPlayerGuid;
	OutAssignment.NewBackupPlayerGuid = Entry->Relay->GetLeaseState().BackupPlayerGuid;
	OutAssignment.OfferRevision = OfferRevision;
	OutAssignment.Disposition = EGuLiWingmanOwnerLossDisposition::TakeoverCommitted;
	return true;
}

void FGuLiWingmanRelayAuthorityRegistry::SetGroupOwnerCohort(
	const FGuLiWingmanGroupHandle& Group, const uint8 OwnerCohort)
{
	if (FGroupEntry* Entry = Groups.Find(Group))
	{
		Entry->OwnerCohort = OwnerCohort;
	}
}

uint8 FGuLiWingmanRelayAuthorityRegistry::GetGroupOwnerCohort(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGroupEntry* Entry = Groups.Find(Group);
	return Entry ? Entry->OwnerCohort : MAX_uint8;
}

void FGuLiWingmanRelayAuthorityRegistry::SetCarrierResolvers(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiCarrierSourceResolver CarrierResolver,
	FGuLiLatestCarrierSourceResolver LatestSourceResolver)
{
	if (FGroupEntry* Entry = Groups.Find(Group))
	{
		Entry->CarrierResolver = MoveTemp(CarrierResolver);
		Entry->LatestSourceResolver = MoveTemp(LatestSourceResolver);
		FGuLiCarrierSourceRef Latest;
		if (Entry->LatestSourceResolver && Entry->LatestSourceResolver(Latest) && Latest.IsValid())
		{
			Entry->LastKnownCarrierSource = Latest;
		}
	}
}

EGuLiRelayCarrierLookupResult FGuLiWingmanRelayAuthorityRegistry::ResolveCarrierSource(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiCarrierSourceRef& Source,
	FGuLiRelayCarrierState& OutState) const
{
	OutState = FGuLiRelayCarrierState{};
	const FGroupEntry* Entry = Groups.Find(Group);
	return Entry && Entry->CarrierResolver
		? Entry->CarrierResolver(Source, OutState)
		: EGuLiRelayCarrierLookupResult::Rejected;
}

bool FGuLiWingmanRelayAuthorityRegistry::GetLatestCarrierSource(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiCarrierSourceRef& OutSource) const
{
	OutSource = FGuLiCarrierSourceRef{};
	const FGroupEntry* Entry = Groups.Find(Group);
	if (!Entry)
	{
		return false;
	}
	if (Entry->LatestSourceResolver && Entry->LatestSourceResolver(OutSource) && OutSource.IsValid())
	{
		Entry->LastKnownCarrierSource = OutSource;
		return true;
	}
	OutSource = Entry->LastKnownCarrierSource;
	return OutSource.IsValid();
}

bool FGuLiWingmanRelayAuthorityRegistry::SetCandidateWorldValidator(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiCandidateWorldValidator Validator)
{
	if (FGroupEntry* Entry = Groups.Find(Group))
	{
		Entry->CandidateWorldValidator = MoveTemp(Validator);
		return static_cast<bool>(Entry->CandidateWorldValidator);
	}
	return false;
}

const FGuLiCandidateWorldValidator*
FGuLiWingmanRelayAuthorityRegistry::FindCandidateWorldValidator(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGroupEntry* Entry = Groups.Find(Group);
	return Entry && Entry->CandidateWorldValidator ? &Entry->CandidateWorldValidator : nullptr;
}

void FGuLiWingmanRelayAuthorityRegistry::ClearCandidateWorldValidator(
	const FGuLiWingmanGroupHandle& Group)
{
	if (FGroupEntry* Entry = Groups.Find(Group))
	{
		Entry->CandidateWorldValidator = FGuLiCandidateWorldValidator{};
	}
}

void FGuLiWingmanRelayAuthorityRegistry::SetFireIntentValidator(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiFireIntentServerValidator Validator)
{
	if (FGroupEntry* Entry = Groups.Find(Group))
	{
		Entry->FireIntentValidator = MoveTemp(Validator);
	}
}

const FGuLiFireIntentServerValidator* FGuLiWingmanRelayAuthorityRegistry::FindFireIntentValidator(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGroupEntry* Entry = Groups.Find(Group);
	return Entry && Entry->FireIntentValidator ? &Entry->FireIntentValidator : nullptr;
}

FGuLiServerFireIntentAcceptedSignature*
FGuLiWingmanRelayAuthorityRegistry::FindFireIntentAcceptedDelegate(
	const FGuLiWingmanGroupHandle& Group)
{
	FGroupEntry* Entry = Groups.Find(Group);
	return Entry ? &Entry->FireIntentAccepted : nullptr;
}

void FGuLiWingmanRelayAuthorityRegistry::ClearFireHooks(const FGuLiWingmanGroupHandle& Group)
{
	if (FGroupEntry* Entry = Groups.Find(Group))
	{
		Entry->FireIntentValidator = FGuLiFireIntentServerValidator{};
		Entry->FireIntentAccepted.Clear();
	}
}

FGuid FGuLiWingmanRelayAuthorityRegistry::MakeNoOwnerSentinel(
	const FGuLiWingmanGroupHandle& Group)
{
	FGuid Sentinel(
		Group.ShipInstanceId.A ^ 0x4e4f4f57u,
		Group.ShipInstanceId.B ^ Group.ShipGeneration ^ 0x4e455230u,
		Group.ShipInstanceId.C ^ Group.GroupGeneration ^ 0x4c454153u,
		Group.ShipInstanceId.D ^ 0xffffffffu);
	if (!Sentinel.IsValid())
	{
		Sentinel = FGuid(0x4e4f4f57u, 0x4e455230u, 0x4c454153u, 0x45000001u);
	}
	return Sentinel;
}

void FGuLiWingmanRelayAuthorityRegistry::NormalizeCandidates(
	TArray<FGuid>& CandidatePlayerGuids)
{
	CandidatePlayerGuids.RemoveAll([](const FGuid& Guid) { return !Guid.IsValid(); });
	CandidatePlayerGuids.Sort(GuidLess);
	for (int32 Index = CandidatePlayerGuids.Num() - 1; Index > 0; --Index)
	{
		if (CandidatePlayerGuids[Index] == CandidatePlayerGuids[Index - 1])
		{
			CandidatePlayerGuids.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
}

bool FGuLiWingmanRelayAuthorityRegistry::SelectOwners(
	const FGuid& PreferredOwner,
	const TArray<FGuid>& CandidatePlayerGuids,
	FGuid& OutOwner,
	FGuid& OutBackup)
{
	OutOwner.Invalidate();
	OutBackup.Invalidate();
	if (CandidatePlayerGuids.IsEmpty())
	{
		return false;
	}
	OutOwner = PreferredOwner.IsValid() && CandidatePlayerGuids.Contains(PreferredOwner)
		? PreferredOwner : CandidatePlayerGuids[0];
	for (const FGuid& Candidate : CandidatePlayerGuids)
	{
		if (Candidate != OutOwner)
		{
			OutBackup = Candidate;
			break;
		}
	}
	return OutOwner.IsValid();
}

bool FGuLiWingmanRelayAuthorityRegistry::StartNextOffer(
	const FGuLiWingmanGroupHandle& Group,
	FGroupEntry& Entry,
	const FGuid& PreferredOwner,
	const double NowSeconds,
	FGuLiWingmanOwnerLossAssignment& OutAssignment)
{
	OutAssignment = FGuLiWingmanOwnerLossAssignment{};
	if (!Entry.Relay || Entry.Relay->GetPendingLeaseOffer().IsPending())
	{
		return false;
	}
	TArray<FGuid> Remaining;
	for (const FGuid& Candidate : Entry.EligibleBackupCandidates)
	{
		if (Candidate.IsValid() && !Entry.AttemptedBackupCandidates.Contains(Candidate)
			&& Candidate != Entry.Relay->GetLeaseState().OwnerPlayerGuid)
		{
			Remaining.Add(Candidate);
		}
	}
	NormalizeCandidates(Remaining);
	FGuid Owner;
	FGuid Backup;
	if (!SelectOwners(PreferredOwner, Remaining, Owner, Backup))
	{
		return false;
	}
	FGuLiWingmanPendingLeaseOffer Offer;
	if (!Entry.Relay->BeginLeaseOffer(Owner, Backup, NowSeconds, Offer))
	{
		return false;
	}
	Entry.AttemptedBackupCandidates.Add(Owner);
	Entry.bAwaitingConnectedOwner = true;
	OutAssignment.Group = Group;
	OutAssignment.NewOwnerPlayerGuid = Owner;
	OutAssignment.NewBackupPlayerGuid = Backup;
	OutAssignment.OfferRevision = Offer.OfferRevision;
	OutAssignment.ReadyDeadlineSeconds = Offer.ReadyDeadlineSeconds;
	OutAssignment.OverallDeadlineSeconds = Offer.OverallDeadlineSeconds;
	OutAssignment.Disposition = EGuLiWingmanOwnerLossDisposition::OfferStarted;
	return true;
}

bool FGuLiWingmanRelayAuthorityRegistry::GroupLess(
	const FGuLiWingmanGroupHandle& Lhs,
	const FGuLiWingmanGroupHandle& Rhs)
{
	if (Lhs.ShipInstanceId != Rhs.ShipInstanceId)
	{
		return GuidLess(Lhs.ShipInstanceId, Rhs.ShipInstanceId);
	}
	if (Lhs.ShipGeneration != Rhs.ShipGeneration)
	{
		return Lhs.ShipGeneration < Rhs.ShipGeneration;
	}
	return Lhs.GroupGeneration < Rhs.GroupGeneration;
}

void FGuLiWingmanRelayAuthorityRegistry::ClearRuntimeHooks(FGroupEntry& Entry)
{
	Entry.CarrierResolver = FGuLiCarrierSourceResolver{};
	Entry.LatestSourceResolver = FGuLiLatestCarrierSourceResolver{};
	Entry.CandidateWorldValidator = FGuLiCandidateWorldValidator{};
	Entry.FireIntentValidator = FGuLiFireIntentServerValidator{};
	Entry.FireIntentAccepted.Clear();
	Entry.LastKnownCarrierSource = FGuLiCarrierSourceRef{};
}
