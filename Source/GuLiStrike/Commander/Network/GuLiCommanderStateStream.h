#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiCommanderStateStream.generated.h"

/** Bounded reliable transport. The two-byte length is part of the 1,000-byte limit. */
USTRUCT()
struct GULISTRIKE_API FGuLiEncodedStateBatch
{
	GENERATED_BODY()
	UPROPERTY() TArray<uint8> Data;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <> struct TStructOpsTypeTraits<FGuLiEncodedStateBatch> : TStructOpsTypeTraitsBase2<FGuLiEncodedStateBatch>
{
	enum { WithNetSerializer = true };
};

namespace GuLiCommanderStateStream
{
inline constexpr int32 MaxWireBytes = 1000;
inline constexpr int32 MaxInFlight = 4;
inline constexpr int32 MaxRecords = 256;
inline constexpr int32 RpcOverheadBytes = 128;

struct FValue
{
	FGuLiSoldierStateItem State;
	TOptional<FGuLiMoveEndpointItem> Endpoint;
	// Poses wait for births, command changes and teleport baselines to be applied.
	uint32 PublishedSequence = 0;
};

struct FPending
{
	FValue Value;
	double FirstWaitingTime = 0;
	bool bRemove = false;
	uint8 Priority = 3;
	uint64 QueueTicket = 0;
};

struct FRecord
{
	FValue Value;
	uint16 Mask = 0;
	bool bRemove = false;
};

struct FHeader
{
	uint16 Protocol = GULI_COMMANDER_PROTOCOL_VERSION;
	uint32 ConnectionGeneration = 0, Generation = 0, Epoch = 0, Sequence = 0;
	bool bStart = false, bComplete = false;
	uint32 RosterCount = 0;
	uint64 IdHash = 0;
};

struct FPrepared
{
	FHeader Header;
	FGuLiEncodedStateBatch Block;
	TArray<FRecord> Records;
	TArray<FGuLiSoldierId> CompletedIds;
	bool bBudgetLimited = false;
};

struct FInFlight
{
	uint32 Sequence = 0;
	// UE owns retransmission. Retain explicit removals until application ACK.
	TArray<FGuLiSoldierId> Removed;
};

GULISTRIKE_API uint64 HashIds(const TMap<FGuLiSoldierId, FValue>& Values);
GULISTRIKE_API bool ReadHeader(const FGuLiEncodedStateBatch& Block, FHeader& OutHeader);

/** One instance per owning connection, never the shared authority roster. */
struct GULISTRIKE_API FSender
{
	FHeader Session;
	TMap<FGuLiSoldierId, FValue> Published;
	TMap<FGuLiSoldierId, FPending> Pending;
	TSet<FGuLiSoldierId> RequiredBaseline;
	// Retirement may precede the next 10 Hz roster capture. Suppress it until absence is observed.
	TSet<FGuLiSoldierId> Retiring;
	TArray<FInFlight> InFlight;
	uint32 LastSequence = 0, AckedSequence = 0, CompleteSequence = 0;
	uint32 CompleteCount = 0;
	uint64 CompleteHash = 0, MergeCount = 0;
	bool bStarted = false;
	struct FQueueEntry { FGuLiSoldierId Id; uint64 Ticket; };
	TArray<FQueueEntry> Queues[4];
	int32 QueueHeads[4] = {0,0,0,0};
	uint64 NextQueueTicket = 1;
	TArray<FGuLiSoldierId> BaselineIds;
	int32 BaselineCursor = 0;
	void Queue(const FGuLiSoldierStateItem& State, const FGuLiMoveEndpointItem* Endpoint, double Now);
	void Enqueue(FGuLiSoldierId Id, FPending& Value);
	void Remove(FGuLiSoldierId Id, double Now);

	void Reset(uint32 Connection = 0, uint32 Generation = 0, uint32 Epoch = 0);
	void Begin(TConstArrayView<FGuLiSoldierStateItem> States, double Now);
	void Refresh(TConstArrayView<FGuLiSoldierStateItem> States,
		const FGuLiMoveEndpointFastArray& Endpoints, double Now);
	void Retire(const FGuLiSoldierStateItem& FinalState, double Now);
	bool Prepare(int32 MaxBytes, bool bUrgentOnly, double Now, FPrepared& Out);
	void Commit(const FPrepared& Batch);
	bool Confirm(uint32 Sequence);
	double OldestWait(double Now) const;
};

struct GULISTRIKE_API FReceiver
{
	FHeader Session;
	TMap<FGuLiSoldierId, FValue> Values;
	uint32 AppliedSequence = 0, AckSentSequence = 0, CompleteSequence = 0;
	uint32 CompleteCount = 0;
	uint64 CompleteHash = 0;
	bool bStarted = false;
	void Reset() { *this = FReceiver{}; }
	/** Decode and validate the entire batch before committing any state. */
	bool Apply(const FGuLiEncodedStateBatch& Block, TArray<FRecord>& OutRecords);
};

struct FPendingPose
{
	FGuLiQuantizedSoldierPose Sample;
	uint32 Frame = 0, SimTick = 0;
	float SampleTime = 0;
	double FirstWaitingTime = 0;
};

struct FDiagnostics
{
	uint64 StateBytes = 0, PoseBytes = 0, ChargedBytes = 0, ObservedBytes = 0;
	uint64 BudgetDeferrals = 0, WindowDeferrals = 0, PoseMerges = 0;
	uint64 ReceivedPoseBytes = 0, ReceivedPoseBlocks = 0;
	double NextLogTime = 0, MaxSampleGap = 0, MaxReceivedSampleGap = 0;
	TMap<FGuLiSoldierId, float> LastSentSampleTimes;
	TMap<FGuLiSoldierId, float> LastReceivedSampleTimes;
};
}
