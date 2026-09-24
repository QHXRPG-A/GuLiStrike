#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiCommanderPoseCodec.generated.h"

/** The only pose wire payload. The 10-bit byte count is included in the 1,000-byte budget. */
USTRUCT()
struct GULISTRIKE_API FGuLiEncodedPoseBlock
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<uint8> Data;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <> struct TStructOpsTypeTraits<FGuLiEncodedPoseBlock> : TStructOpsTypeTraitsBase2<FGuLiEncodedPoseBlock>
{
	enum { WithNetSerializer = true };
};

/** Bit 0 acknowledges LatestSequence; bit N acknowledges LatestSequence - N. */
USTRUCT()
struct GULISTRIKE_API FGuLiPoseAcknowledgment
{
	GENERATED_BODY()
	uint16 ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION;
	uint32 SyncGeneration = 0;
	uint32 MatchEpoch = 0;
	uint32 LatestSequence = 0;
	uint64 ReceivedBits[8] = {};
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <> struct TStructOpsTypeTraits<FGuLiPoseAcknowledgment> : TStructOpsTypeTraitsBase2<FGuLiPoseAcknowledgment>
{
	enum { WithNetSerializer = true };
};

/** Connection-local protocol state. No Actors, gameplay mutations or presentation dependencies. */
namespace GuLiCommanderPoseCodec
{
inline constexpr int32 MaxWireBytes = 1000;
inline constexpr int32 MaxDataBytes = MaxWireBytes - 2;
inline constexpr uint32 HistoryCapacity = 2048;
inline constexpr uint32 AckBitCount = 512;
inline constexpr uint32 MaxBaselineSteps = 20;
inline constexpr uint32 MaxBaselineBlocks = 1024;

GULISTRIKE_API bool Quantize(const FVector& Position, const FVector& Velocity, float Yaw,
	FGuLiQuantizedSoldierPose& OutPose);

struct FHistoryBlock
{
	uint32 Sequence = 0;
	uint32 SimTick = 0;
	bool bAcknowledged = false;
	TArray<FGuLiQuantizedSoldierPose> Samples;
};

class GULISTRIKE_API FHistory
{
public:
	void Reset();
	void Store(uint32 Sequence, uint32 SimTick, TConstArrayView<FGuLiQuantizedSoldierPose> Samples);
	FHistoryBlock* Find(uint32 Sequence);
	const FHistoryBlock* Find(uint32 Sequence) const;
	void Forget(TConstArrayView<FGuLiSoldierId> Removed);
	uint64 GetAllocatedBytes() const;
private:
	TArray<FHistoryBlock> Blocks;
};

struct FBaseline
{
	FGuLiQuantizedSoldierPose Pose;
	uint32 Sequence = 0;
	uint32 SimTick = 0;
};

struct FPreparedBlock
{
	FGuLiEncodedPoseBlock Block;
	TArray<FGuLiQuantizedSoldierPose> Samples;
	uint32 Sequence = 0, SimTick = 0, AbsoluteCount = 0;
};

class GULISTRIKE_API FSender
{
public:
	void Reset(uint32 Generation = 0, uint32 Epoch = 0);
	void Encode(const FGuLiSoldierPoseChunk& Chunk, TArray<FGuLiEncodedPoseBlock>& OutBlocks);
	/** No sequence/history mutation until the caller has admitted the encoded bytes. */
	bool Prepare(const FGuLiSoldierPoseChunk& Chunk, int32 MaxBytes, FPreparedBlock& Out) const;
	void Commit(const FPreparedBlock& Prepared);
	/** Validates the complete acknowledgment before promoting any baseline. */
	bool Confirm(const FGuLiPoseAcknowledgment& Ack);
	void Forget(TConstArrayView<FGuLiSoldierId> Removed);
	uint64 GetAllocatedBytes() const;
	uint32 GetLastSequence() const { return LastSequence; }
private:
	FHistory History;
	TMap<FGuLiSoldierId, FBaseline> Confirmed;
	uint32 SyncGeneration = 0;
	uint32 MatchEpoch = 0;
	uint32 LastSequence = 0;
};

enum class EDecodeResult : uint8 { Decoded, Duplicate, TooOld, WrongSession, MissingBaseline, InvalidPayload };

class GULISTRIKE_API FReceiver
{
public:
	void Reset(uint32 Generation = 0, uint32 Epoch = 0);
	EDecodeResult Decode(const FGuLiEncodedPoseBlock& Block, FGuLiSoldierPoseChunk& OutChunk);
	bool BuildAcknowledgment(FGuLiPoseAcknowledgment& OutAck) const;
	void Forget(TConstArrayView<FGuLiSoldierId> Removed);
	uint64 GetAllocatedBytes() const { return History.GetAllocatedBytes(); }
private:
	FHistory History;
	uint32 SyncGeneration = 0;
	uint32 MatchEpoch = 0;
	uint32 LatestSequence = 0;
};
}
