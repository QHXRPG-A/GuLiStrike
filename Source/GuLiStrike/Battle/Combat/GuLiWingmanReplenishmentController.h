// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Relay/GuLiWingmanRelayServer.h"

/** One authority-side replacement committed into the existing group and stable roster slot. */
struct GULISTRIKE_API FGuLiWingmanReplenishmentResult
{
	FGuLiWingmanHandle PreviousWingman;
	FGuLiWingmanHandle ReplenishedWingman;
	/** Stable authority schedule identity; emitted exactly once when the queued due slot is released. */
	uint64 ScheduleId = 0u;
};

/**
 * Server-only death/replenishment clock. It owns no transforms and mutates only the Relay lifecycle
 * records through ReplenishWingman after a continuously observed 15-second death interval.
 */
class GULISTRIKE_API FGuLiWingmanReplenishmentController
{
public:
	static constexpr double DefaultDelaySeconds = 15.0;

	void Reset();

	/**
	 * Observes the current stable slots and replenishes due entries only while the group is Active and
	 * no transfer is in progress. OutRosterStateChanged includes death, replacement and group changes.
	 */
	int32 Advance(
		FGuLiWingmanRelayServer& Relay,
		double NowSeconds,
		TArray<FGuLiWingmanReplenishmentResult>& OutReplenished,
		bool& OutRosterStateChanged,
		double DelaySeconds = DefaultDelaySeconds);

	int32 GetTrackedDeadSlotCount() const { return DeadSlots.Num(); }
	int32 GetQueuedDueSlotCount() const;
	/** Read-only diagnostics for the exact authoritative timer attached to a dead identity. */
	bool TryGetSchedule(
		const FGuLiWingmanHandle& Wingman,
		uint64& OutScheduleId,
		double& OutReplenishAtSeconds,
		bool& bOutDue) const;

private:
	struct FDeadSlotState
	{
		FGuLiWingmanHandle Wingman;
		double ReplenishAtSeconds = 0.0;
		uint64 ScheduleId = 0u;
		bool bDue = false;
	};

	static uint8 MakeStableSlotIndex(const FGuLiWingmanHandle& Wingman);
	static uint64 ComputeRosterFingerprint(const TArray<FGuLiWingmanRosterEntry>& Roster);

	FGuLiWingmanGroupHandle ObservedGroup;
	TMap<uint8, FDeadSlotState> DeadSlots;
	uint64 LastRosterFingerprint = 0u;
	uint64 NextScheduleId = 0u;
};
