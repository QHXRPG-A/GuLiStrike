// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Relay/GuLiWingmanRelayServer.h"

/** Outcome of preserving a group when its current simulation owner disconnects. */
enum class EGuLiWingmanOwnerLossDisposition : uint8
{
	Ignored = 0,
	AwaitingConnectedOwner,
	OfferStarted,
	TakeoverCommitted,
	/** Deprecated source compatibility; production disconnects never skip Offer Ready. */
	TakeoverStarted = TakeoverCommitted
};

struct GULISTRIKE_API FGuLiWingmanOwnerLossAssignment
{
	FGuLiWingmanGroupHandle Group;
	FGuid NewOwnerPlayerGuid;
	FGuid NewBackupPlayerGuid;
	uint32 OfferRevision = 0u;
	double ReadyDeadlineSeconds = 0.0;
	double OverallDeadlineSeconds = 0.0;
	EGuLiWingmanOwnerLossDisposition Disposition = EGuLiWingmanOwnerLossDisposition::Ignored;
};

using FGuLiLatestCarrierSourceResolver = TFunction<bool(FGuLiCarrierSourceRef& OutSource)>;

/**
 * Server-only, GameState-lifetime registry for ClientValidatedRelay cores.
 *
 * PlayerController components are RPC transports and may disappear at any time. This registry is the
 * durable authority owner for Roster, Lease, Health, Dead, AcceptedSnapshot and GroupAbilityConfig.
 * It deliberately has no tick and therefore cannot create or advance Wingman movement.
 */
class GULISTRIKE_API FGuLiWingmanRelayAuthorityRegistry
{
public:
	FGuLiWingmanRelayServer* CreateGroup(
		uint32 MatchEpoch,
		const FGuLiWingmanGroupHandle& Group,
		const FGuid& InitialOwnerPlayerGuid,
		const FGuid& BackupPlayerGuid,
		const FGuLiGroupAbilityConfigSnapshot& InitialAbilityConfig,
		double NowSeconds,
		const FGuLiWingmanRelayTuning& Tuning = FGuLiWingmanRelayTuning{});

	FGuLiWingmanRelayServer* FindGroup(const FGuLiWingmanGroupHandle& Group);
	const FGuLiWingmanRelayServer* FindGroup(const FGuLiWingmanGroupHandle& Group) const;
	/** Revokes the retained core and clears every runtime callback while preserving its tombstone. */
	bool RevokeGroup(const FGuLiWingmanGroupHandle& Group, double NowSeconds);
	/** Final removal path; revokes first so no callback or authoritative scope survives destruction. */
	bool DestroyGroup(const FGuLiWingmanGroupHandle& Group, double NowSeconds);

	/** Stable list, sorted by group identity, used by disconnect/reconnect orchestration. */
	TArray<FGuLiWingmanGroupHandle> FindGroupsOwnedBy(const FGuid& OwnerPlayerGuid) const;
	TArray<FGuLiWingmanGroupHandle> GetAwaitingOwnerGroups() const;
	/**
	 * Groups whose still-connected owner went silent through the lease watchdog.
	 * They retain all six authoritative scopes but have not yet entered the
	 * connected-backup rotation used by socket-loss recovery.
	 */
	TArray<FGuLiWingmanGroupHandle> GetRecoverableLeaseLossGroups() const;

	/**
	 * Invalidates every lease owned by DisconnectedPlayerGuid without deleting its authoritative state.
	 * Preferred BackupPlayerGuid wins when connected; otherwise the lexicographically first candidate wins.
	 */
	TArray<FGuLiWingmanOwnerLossAssignment> HandleOwnerDisconnected(
		const FGuid& DisconnectedPlayerGuid,
		const TArray<FGuid>& ConnectedCandidatePlayerGuids,
		double NowSeconds);
	/** Starts the same Offer/Ready rotation after watchdog revocation, without pretending a socket closed. */
	bool BeginLeaseLossRecovery(
		const FGuLiWingmanGroupHandle& Group,
		const TArray<FGuid>& ConnectedCandidatePlayerGuids,
		double NowSeconds,
		FGuLiWingmanOwnerLossAssignment& OutAssignment);

	/** Assigns retained no-owner groups after a later connection becomes eligible. */
	TArray<FGuLiWingmanOwnerLossAssignment> AssignAwaitingGroups(
		const TArray<FGuid>& ConnectedCandidatePlayerGuids,
		double NowSeconds);
	bool AssignAwaitingGroup(
		const FGuLiWingmanGroupHandle& Group,
		const TArray<FGuid>& ConnectedCandidatePlayerGuids,
		double NowSeconds,
		FGuLiWingmanOwnerLossAssignment& OutAssignment);

	/** Exact 1 Hz group-only watchdog. Returns only newly-issued backup offers. */
	TArray<FGuLiWingmanOwnerLossAssignment> RunLeaseMaintenance(double NowSeconds);
	/** Ready from the exact proposed owner. Success commits but does not grant active simulation yet. */
	bool AcknowledgeLeaseOfferReady(
		const FGuLiWingmanGroupHandle& Group,
		const FGuid& SenderPlayerGuid,
		uint32 OfferRevision,
		double NowSeconds,
		FGuLiWingmanOwnerLossAssignment& OutAssignment);

	/** Opaque server-side cohort (battle team) prevents a retained group crossing team boundaries. */
	void SetGroupOwnerCohort(const FGuLiWingmanGroupHandle& Group, uint8 OwnerCohort);
	uint8 GetGroupOwnerCohort(const FGuLiWingmanGroupHandle& Group) const;

	void SetCarrierResolvers(
		const FGuLiWingmanGroupHandle& Group,
		FGuLiCarrierSourceResolver CarrierResolver,
		FGuLiLatestCarrierSourceResolver LatestSourceResolver);
	EGuLiRelayCarrierLookupResult ResolveCarrierSource(
		const FGuLiWingmanGroupHandle& Group,
		const FGuLiCarrierSourceRef& Source,
		FGuLiRelayCarrierState& OutState) const;
	bool GetLatestCarrierSource(
		const FGuLiWingmanGroupHandle& Group,
		FGuLiCarrierSourceRef& OutSource) const;

	bool SetCandidateWorldValidator(
		const FGuLiWingmanGroupHandle& Group,
		FGuLiCandidateWorldValidator Validator);
	const FGuLiCandidateWorldValidator* FindCandidateWorldValidator(
		const FGuLiWingmanGroupHandle& Group) const;
	void ClearCandidateWorldValidator(const FGuLiWingmanGroupHandle& Group);

	void SetFireIntentValidator(
		const FGuLiWingmanGroupHandle& Group,
		FGuLiFireIntentServerValidator Validator);
	const FGuLiFireIntentServerValidator* FindFireIntentValidator(
		const FGuLiWingmanGroupHandle& Group) const;
	FGuLiServerFireIntentAcceptedSignature* FindFireIntentAcceptedDelegate(
		const FGuLiWingmanGroupHandle& Group);
	void ClearFireHooks(const FGuLiWingmanGroupHandle& Group);

	int32 Num() const { return Groups.Num(); }

private:
	struct FGroupEntry
	{
		TSharedPtr<FGuLiWingmanRelayServer> Relay;
		FGuLiCarrierSourceResolver CarrierResolver;
		FGuLiLatestCarrierSourceResolver LatestSourceResolver;
		FGuLiCandidateWorldValidator CandidateWorldValidator;
		FGuLiFireIntentServerValidator FireIntentValidator;
		FGuLiServerFireIntentAcceptedSignature FireIntentAccepted;
		mutable FGuLiCarrierSourceRef LastKnownCarrierSource;
		uint8 OwnerCohort = MAX_uint8;
		bool bAwaitingConnectedOwner = false;
		TArray<FGuid> EligibleBackupCandidates;
		TSet<FGuid> AttemptedBackupCandidates;
	};

	static FGuid MakeNoOwnerSentinel(const FGuLiWingmanGroupHandle& Group);
	static void NormalizeCandidates(TArray<FGuid>& CandidatePlayerGuids);
	static bool SelectOwners(
		const FGuid& PreferredOwner,
		const TArray<FGuid>& CandidatePlayerGuids,
		FGuid& OutOwner,
		FGuid& OutBackup);
	static bool GroupLess(
		const FGuLiWingmanGroupHandle& Lhs,
		const FGuLiWingmanGroupHandle& Rhs);
	static void ClearRuntimeHooks(FGroupEntry& Entry);
	static bool StartNextOffer(
		const FGuLiWingmanGroupHandle& Group,
		FGroupEntry& Entry,
		const FGuid& PreferredOwner,
		double NowSeconds,
		FGuLiWingmanOwnerLossAssignment& OutAssignment);

	TMap<FGuLiWingmanGroupHandle, FGroupEntry> Groups;
};
