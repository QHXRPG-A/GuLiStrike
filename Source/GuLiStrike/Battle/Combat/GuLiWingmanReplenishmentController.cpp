// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiWingmanReplenishmentController.h"

namespace
{
	constexpr uint64 FnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	void AddHashBytes(uint64& Hash, const void* Data, const SIZE_T Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (SIZE_T Index = 0; Index < Size; ++Index)
		{
			Hash ^= Bytes[Index];
			Hash *= FnvPrime;
		}
	}

	template <typename TValue>
	void AddHashValue(uint64& Hash, const TValue& Value)
	{
		AddHashBytes(Hash, &Value, sizeof(TValue));
	}
}

void FGuLiWingmanReplenishmentController::Reset()
{
	ObservedGroup = FGuLiWingmanGroupHandle{};
	DeadSlots.Reset();
	LastRosterFingerprint = 0u;
	NextScheduleId = 0u;
}

int32 FGuLiWingmanReplenishmentController::Advance(
	FGuLiWingmanRelayServer& Relay,
	const double NowSeconds,
	TArray<FGuLiWingmanReplenishmentResult>& OutReplenished,
	bool& OutRosterStateChanged,
	const double DelaySeconds)
{
	OutReplenished.Reset();
	OutRosterStateChanged = false;
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| !FMath::IsFinite(DelaySeconds) || DelaySeconds <= 0.0)
	{
		return 0;
	}

	const FGuLiWingmanGroupHandle CurrentGroup = Relay.GetLeaseState().Group;
	if (!CurrentGroup.IsValid())
	{
		Reset();
		return 0;
	}
	if (ObservedGroup != CurrentGroup)
	{
		Reset();
		ObservedGroup = CurrentGroup;
		OutRosterStateChanged = true;
	}

	const TArray<FGuLiWingmanRosterEntry>& Roster = Relay.GetRoster();
	const uint64 BeforeFingerprint = ComputeRosterFingerprint(Roster);
	OutRosterStateChanged |= BeforeFingerprint != LastRosterFingerprint;
	LastRosterFingerprint = BeforeFingerprint;

	TSet<uint8> SeenSlots;
	TArray<uint8> DueSlots;
	for (const FGuLiWingmanRosterEntry& Entry : Roster)
	{
		if (!Entry.Wingman.IsValid() || Entry.Wingman.Flight.Group != CurrentGroup)
		{
			continue;
		}
		const uint8 SlotIndex = MakeStableSlotIndex(Entry.Wingman);
		SeenSlots.Add(SlotIndex);
		if (!Entry.bDead)
		{
			DeadSlots.Remove(SlotIndex);
			continue;
		}

		FDeadSlotState* DeadState = DeadSlots.Find(SlotIndex);
		if (!DeadState || DeadState->Wingman != Entry.Wingman)
		{
			FDeadSlotState NewState;
			NewState.Wingman = Entry.Wingman;
			NewState.ReplenishAtSeconds = NowSeconds + DelaySeconds;
			++NextScheduleId;
			if (NextScheduleId == 0u)
			{
				++NextScheduleId;
			}
			NewState.ScheduleId = NextScheduleId;
			DeadSlots.Add(SlotIndex, NewState);
			continue;
		}
		if (NowSeconds + UE_DOUBLE_SMALL_NUMBER >= DeadState->ReplenishAtSeconds)
		{
			DeadState->bDue = true;
			DueSlots.Add(SlotIndex);
		}
	}
	for (auto Iterator = DeadSlots.CreateIterator(); Iterator; ++Iterator)
	{
		if (!SeenSlots.Contains(Iterator.Key()))
		{
			Iterator.RemoveCurrent();
		}
	}

	if (Relay.GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Active
		|| Relay.IsTransferInProgress())
	{
		return 0;
	}

	DueSlots.Sort();
	for (const uint8 SlotIndex : DueSlots)
	{
		const FDeadSlotState* DeadState = DeadSlots.Find(SlotIndex);
		if (!DeadState || DeadState->Wingman.EntityGeneration == MAX_uint32)
		{
			continue;
		}
		const FGuLiWingmanHandle PreviousWingman = DeadState->Wingman;
		const uint32 NewGeneration = PreviousWingman.EntityGeneration + 1u;
		if (!Relay.ReplenishWingman(
			PreviousWingman.Flight.FlightIndex,
			PreviousWingman.MemberIndex,
			NewGeneration))
		{
			continue;
		}

		FGuLiWingmanReplenishmentResult& Result = OutReplenished.AddDefaulted_GetRef();
		Result.PreviousWingman = PreviousWingman;
		Result.ReplenishedWingman = PreviousWingman;
		Result.ReplenishedWingman.EntityGeneration = NewGeneration;
		Result.ScheduleId = DeadState->ScheduleId;
		DeadSlots.Remove(SlotIndex);
	}

	if (!OutReplenished.IsEmpty())
	{
		OutRosterStateChanged = true;
		LastRosterFingerprint = ComputeRosterFingerprint(Relay.GetRoster());
	}
	return OutReplenished.Num();
}

int32 FGuLiWingmanReplenishmentController::GetQueuedDueSlotCount() const
{
	int32 Count = 0;
	for (const TPair<uint8, FDeadSlotState>& Pair : DeadSlots)
	{
		Count += Pair.Value.bDue ? 1 : 0;
	}
	return Count;
}

bool FGuLiWingmanReplenishmentController::TryGetSchedule(
	const FGuLiWingmanHandle& Wingman,
	uint64& OutScheduleId,
	double& OutReplenishAtSeconds,
	bool& bOutDue) const
{
	OutScheduleId = 0u;
	OutReplenishAtSeconds = 0.0;
	bOutDue = false;
	if (!Wingman.IsValid())
	{
		return false;
	}
	const FDeadSlotState* State = DeadSlots.Find(MakeStableSlotIndex(Wingman));
	if (!State || State->Wingman != Wingman || State->ScheduleId == 0u
		|| !FMath::IsFinite(State->ReplenishAtSeconds) || State->ReplenishAtSeconds <= 0.0)
	{
		return false;
	}
	OutScheduleId = State->ScheduleId;
	OutReplenishAtSeconds = State->ReplenishAtSeconds;
	bOutDue = State->bDue;
	return true;
}

uint8 FGuLiWingmanReplenishmentController::MakeStableSlotIndex(const FGuLiWingmanHandle& Wingman)
{
	return static_cast<uint8>(
		Wingman.Flight.FlightIndex * GULI_WINGMAN_MEMBERS_PER_FLIGHT + Wingman.MemberIndex);
}

uint64 FGuLiWingmanReplenishmentController::ComputeRosterFingerprint(
	const TArray<FGuLiWingmanRosterEntry>& Roster)
{
	uint64 Hash = FnvOffsetBasis;
	for (const FGuLiWingmanRosterEntry& Entry : Roster)
	{
		const FGuLiWingmanHandle& Wingman = Entry.Wingman;
		AddHashValue(Hash, Wingman.Flight.Group.ShipInstanceId.A);
		AddHashValue(Hash, Wingman.Flight.Group.ShipInstanceId.B);
		AddHashValue(Hash, Wingman.Flight.Group.ShipInstanceId.C);
		AddHashValue(Hash, Wingman.Flight.Group.ShipInstanceId.D);
		AddHashValue(Hash, Wingman.Flight.Group.ShipGeneration);
		AddHashValue(Hash, Wingman.Flight.Group.GroupGeneration);
		AddHashValue(Hash, Wingman.Flight.FlightIndex);
		AddHashValue(Hash, Wingman.MemberIndex);
		AddHashValue(Hash, Wingman.EntityGeneration);
		AddHashValue(Hash, Entry.bDead);
	}
	return Hash == 0u ? 1u : Hash;
}
