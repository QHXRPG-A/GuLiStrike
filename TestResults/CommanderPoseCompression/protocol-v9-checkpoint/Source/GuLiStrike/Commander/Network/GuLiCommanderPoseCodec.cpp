#include "Commander/Network/GuLiCommanderPoseCodec.h"

#include "Algo/BinarySearch.h"
#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

namespace GuLiCommanderPoseCodec
{
namespace
{
constexpr uint16 AllFields = (1u << 10u) - 1u;
bool Newer(uint32 A, uint32 B) { return int32(A - B) > 0; }

// Half steps round away from zero, including negative world coordinates and predictions.
bool QuantizeAxis(double Value, double Step, int64 Minimum, int64 Maximum, int64& Out)
{
	if (!FMath::IsFinite(Value)) return false;
	const double Rounded = FMath::FloorToDouble(FMath::Abs(Value / Step) + 0.5);
	const double Signed = Value < 0.0 ? -Rounded : Rounded;
	if (Signed < double(Minimum) || Signed > double(Maximum)) return false;
	Out = int64(Signed);
	return true;
}

int64 RoundTenths(int64 Numerator)
{
	return Numerator < 0 ? -((-Numerator + 5) / 10) : (Numerator + 5) / 10;
}

void Predict(const FGuLiCompressedSoldierPose& Pose, uint32 Steps, int64 (&Values)[6])
{
	static_assert(GuLiCommanderSimulationTiming::RateHz == 10u);
	Values[0] = int64(Pose.WorldXMeters) + RoundTenths(int64(Pose.VelocityXMetersPerSecond) * Steps);
	Values[1] = int64(Pose.WorldYMeters) + RoundTenths(int64(Pose.VelocityYMetersPerSecond) * Steps);
	// One m/s for one 0.1-second step is exactly one Z quantization unit.
	Values[2] = int64(Pose.WorldZDecimeters) + int64(Pose.VelocityZMetersPerSecond) * Steps;
	Values[3] = Pose.VelocityXMetersPerSecond;
	Values[4] = Pose.VelocityYMetersPerSecond;
	Values[5] = Pose.VelocityZMetersPerSecond;
}

void WriteUnsigned(FBitWriter& Writer, uint64 Value)
{
	do
	{
		uint8 Byte = uint8(Value & 0x7fu);
		Value >>= 7u;
		if (Value) Byte |= 0x80u;
		Writer.SerializeBits(&Byte, 8);
	} while (Value);
}

void WriteSigned(FBitWriter& Writer, int64 Value)
{
	const uint64 ZigZag = Value < 0 ? uint64(-(Value + 1)) * 2u + 1u : uint64(Value) * 2u;
	WriteUnsigned(Writer, ZigZag);
}

// All untrusted reads are bounded here, before reaching the cache or complete-pose consumer.
struct FReader
{
	FBitReader Bits;
	bool bValid = true;
	explicit FReader(const TArray<uint8>& Data) : Bits(Data.GetData(), Data.Num() * 8) {}
	uint64 ReadBits(int32 Count)
	{
		uint64 Value = 0;
		if (!bValid || Bits.GetBitsLeft() < Count) { bValid = false; return 0; }
		Bits.SerializeBits(&Value, Count);
		return Value;
	}
	uint64 ReadUnsigned(uint64 Maximum)
	{
		uint64 Value = 0;
		for (int32 Index = 0; Index < 10 && bValid; ++Index)
		{
			const uint8 Byte = uint8(ReadBits(8));
			if (Index == 9 && Byte > 1) break;
			Value |= uint64(Byte & 0x7fu) << (Index * 7);
			if (!(Byte & 0x80u))
			{
				bValid &= Value <= Maximum && (Index == 0 || Byte != 0);
				return Value;
			}
		}
		bValid = false;
		return 0;
	}
	uint32 ReadId() { return uint32(ReadUnsigned(MAX_uint32)); }
	int64 ReadSigned()
	{
		// Covers the full int32 position difference plus the bounded velocity prediction.
		const uint64 Value = ReadUnsigned((uint64(1) << 34u) - 1u);
		return (Value & 1u) ? -int64(Value >> 1u) - 1 : int64(Value >> 1u);
	}
};

bool IsPoseValid(const FGuLiCompressedSoldierPose& Pose)
{
	return Pose.SoldierId.IsValid() && uint8(Pose.State) <= uint8(EGuLiSoldierPoseState::Destroyed)
		&& (Pose.Flags & ~GULI_VALID_SOLDIER_POSE_FLAGS) == 0;
}

const FGuLiCompressedSoldierPose* FindSample(const FHistoryBlock& Block, FGuLiSoldierId Id)
{
	const int32 Index = Algo::LowerBoundBy(Block.Samples, Id, &FGuLiCompressedSoldierPose::SoldierId);
	return Block.Samples.IsValidIndex(Index) && Block.Samples[Index].SoldierId == Id ? &Block.Samples[Index] : nullptr;
}

void WriteRecord(FBitWriter& Writer, const FGuLiCompressedSoldierPose& Pose,
	uint32 PreviousId, const FBaseline* Baseline, uint32 Sequence, uint32 SimTick)
{
	WriteUnsigned(Writer, Pose.SoldierId.Value - PreviousId);
	WriteUnsigned(Writer, Baseline ? Sequence - Baseline->Sequence : 0u);
	int64 Predicted[6] = {};
	if (Baseline) Predict(Baseline->Pose, SimTick - Baseline->SimTick, Predicted);
	const int64 Current[6] = { Pose.WorldXMeters, Pose.WorldYMeters, Pose.WorldZDecimeters,
		Pose.VelocityXMetersPerSecond, Pose.VelocityYMetersPerSecond, Pose.VelocityZMetersPerSecond };
	uint16 Mask = 0;
	for (int32 Axis = 0; Axis < 6; ++Axis)
		if (!Baseline || Current[Axis] != Predicted[Axis]) Mask |= 1u << Axis;
	const int32 YawDelta = Baseline ? ((int32(Pose.FacingYaw) - Baseline->Pose.FacingYaw + 128) & 255) - 128 : 0;
	if (!Baseline || YawDelta != 0) Mask |= 1u << 6;
	if (!Baseline || Pose.ActiveOrderId != Baseline->Pose.ActiveOrderId) Mask |= 1u << 7;
	if (!Baseline || Pose.State != Baseline->Pose.State) Mask |= 1u << 8;
	if (!Baseline || Pose.Flags != Baseline->Pose.Flags) Mask |= 1u << 9;
	Writer.SerializeBits(&Mask, 10);
	for (int32 Axis = 0; Axis < 6; ++Axis)
		if (Mask & (1u << Axis)) WriteSigned(Writer, Current[Axis] - Predicted[Axis]);
	if (Mask & (1u << 6))
	{
		if (Baseline) WriteSigned(Writer, YawDelta);
		else { uint8 Yaw = Pose.FacingYaw; Writer.SerializeBits(&Yaw, 8); }
	}
	if (Mask & (1u << 7)) WriteUnsigned(Writer, Pose.ActiveOrderId);
	if (Mask & (1u << 8)) { uint8 State = uint8(Pose.State); Writer.SerializeBits(&State, 2); }
	if (Mask & (1u << 9)) { uint8 Flags = Pose.Flags; Writer.SerializeBits(&Flags, 1); }
}
}

bool Quantize(const FVector& Position, const FVector& Velocity, float Yaw, FGuLiCompressedSoldierPose& OutPose)
{
	if (!FMath::IsFinite(Yaw)) return false;
	if (!OutPose.SetWorldLocationCentimeters(Position) || !OutPose.SetVelocityCentimetersPerSecond(Velocity)) return false;
	OutPose.FacingYaw = GuLiCommanderProtocol::QuantizeYawDegrees(Yaw);
	return true;
}

void FHistory::Reset() { Blocks.Empty(); }

void FHistory::Store(uint32 Sequence, uint32 SimTick, TConstArrayView<FGuLiCompressedSoldierPose> Samples)
{
	if (Blocks.IsEmpty()) Blocks.SetNum(HistoryCapacity);
	FHistoryBlock& Block = Blocks[Sequence % HistoryCapacity];
	Block.Sequence = Sequence;
	Block.SimTick = SimTick;
	Block.bAcknowledged = false;
	Block.Samples.Reset(Samples.Num());
	Block.Samples.Append(Samples.GetData(), Samples.Num());
}

FHistoryBlock* FHistory::Find(uint32 Sequence)
{
	if (Sequence == 0 || Blocks.IsEmpty()) return nullptr;
	FHistoryBlock& Block = Blocks[Sequence % HistoryCapacity];
	return Block.Sequence == Sequence ? &Block : nullptr;
}

const FHistoryBlock* FHistory::Find(uint32 Sequence) const
{
	return const_cast<FHistory*>(this)->Find(Sequence);
}

void FHistory::Forget(TConstArrayView<FGuLiSoldierId> Removed)
{
	if (Removed.IsEmpty()) return;
	const TSet<FGuLiSoldierId> Ids(Removed);
	for (FHistoryBlock& Block : Blocks)
		Block.Samples.RemoveAll([&Ids](const auto& Sample) { return Ids.Contains(Sample.SoldierId); });
}

uint64 FHistory::GetAllocatedBytes() const
{
	uint64 Bytes = Blocks.GetAllocatedSize();
	for (const FHistoryBlock& Block : Blocks) Bytes += Block.Samples.GetAllocatedSize();
	return Bytes;
}

void FSender::Reset(uint32 Generation, uint32 Epoch)
{
	SyncGeneration = Generation; MatchEpoch = Epoch; LastSequence = 0;
	History.Reset(); Confirmed.Empty();
}

void FSender::Encode(const FGuLiSoldierPoseChunk& Chunk, TArray<FGuLiEncodedPoseBlock>& OutBlocks)
{
	check(SyncGeneration != 0 && MatchEpoch == Chunk.AuthorityEpoch);
	check(Chunk.ProtocolVersion == GULI_COMMANDER_PROTOCOL_VERSION && Chunk.FrameSequence != 0);
	check(Chunk.ChunkCount > 0 && Chunk.ChunkCount <= GULI_MAX_POSE_CHUNKS_PER_FRAME && Chunk.ChunkIndex < Chunk.ChunkCount);
	check(FMath::IsFinite(Chunk.ServerTimeSeconds) && Chunk.ServerTimeSeconds >= 0);
	OutBlocks.Reset();
	TArray<FGuLiCompressedSoldierPose, TInlineAllocator<GULI_MAX_POSE_SAMPLES_PER_CHUNK>> Sorted(Chunk.Samples);
	Sorted.Sort([](const auto& A, const auto& B) { return A.SoldierId < B.SoldierId; });
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
		check(IsPoseValid(Sorted[Index]) && (Index == 0 || Sorted[Index - 1].SoldierId < Sorted[Index].SoldierId));
	int32 Start = 0;
	while (Start < Sorted.Num())
	{
		if (++LastSequence == 0) ++LastSequence;
		FBitWriter Writer(MaxDataBytes * 8, true);
		uint16 Version = GULI_COMMANDER_PROTOCOL_VERSION;
		Writer.SerializeBits(&Version, 16);
		WriteUnsigned(Writer, SyncGeneration); WriteUnsigned(Writer, MatchEpoch);
		WriteUnsigned(Writer, LastSequence); WriteUnsigned(Writer, Chunk.FrameSequence);
		WriteUnsigned(Writer, Chunk.ServerSimTick);
		float Time = Chunk.ServerTimeSeconds;
		Writer.SerializeBits(&Time, 32);
		WriteUnsigned(Writer, Chunk.ChunkIndex); WriteUnsigned(Writer, Chunk.ChunkCount);
		const int32 CountBitOffset = int32(Writer.GetNumBits());
		uint8 Count = 0;
		Writer.SerializeBits(&Count, 6);
		uint32 PreviousId = 0;
		uint32 AbsoluteCount = 0;
		while (Start + Count < Sorted.Num() && Count < GULI_MAX_POSE_SAMPLES_PER_CHUNK)
		{
			const auto& Pose = Sorted[Start + Count];
			const FBaseline* Baseline = Confirmed.Find(Pose.SoldierId);
			if (Baseline && (Pose.IsTeleport() || Chunk.ServerSimTick - Baseline->SimTick > MaxBaselineSteps
				|| LastSequence - Baseline->Sequence > MaxBaselineBlocks)) Baseline = nullptr;
			FBitWriter Record(512, true);
			WriteRecord(Record, Pose, PreviousId, Baseline, LastSequence, Chunk.ServerSimTick);
			if (Writer.GetNumBits() + Record.GetNumBits() > MaxDataBytes * 8) break;
			Writer.SerializeBits(Record.GetData(), Record.GetNumBits());
			PreviousId = Pose.SoldierId.Value;
			AbsoluteCount += Baseline == nullptr;
			++Count;
		}
		check(Count > 0);
		appBitsCpy(Writer.GetData(), CountBitOffset, &Count, 0, 6);
		auto& Block = OutBlocks.AddDefaulted_GetRef();
		Block.Data.Append(Writer.GetData(), int32(Writer.GetNumBytes()));
		History.Store(LastSequence, Chunk.ServerSimTick, MakeArrayView(Sorted.GetData() + Start, Count));
		GuLiCommanderPoseMetrics::RecordEncoded(AbsoluteCount, Count - AbsoluteCount, Block.Data.Num() + 2);
		Start += Count;
	}
}

bool FSender::Confirm(const FGuLiPoseAcknowledgment& Ack)
{
	if (Ack.ProtocolVersion != GULI_COMMANDER_PROTOCOL_VERSION || SyncGeneration == 0
		|| Ack.SyncGeneration != SyncGeneration || Ack.MatchEpoch != MatchEpoch || Ack.LatestSequence == 0
		|| !(Ack.ReceivedBits[0] & 1u) || LastSequence == 0 || Newer(Ack.LatestSequence, LastSequence)
		|| LastSequence - Ack.LatestSequence >= HistoryCapacity) return false;
	// Validate the bitmap before committing: every retained bit must refer to a block we actually sent.
	for (uint32 Bit = 0; Bit < AckBitCount; ++Bit)
	{
		if (!(Ack.ReceivedBits[Bit / 64] & (uint64(1) << (Bit % 64)))) continue;
		const uint32 Sequence = Ack.LatestSequence - Bit;
		if (LastSequence - Sequence < HistoryCapacity && !History.Find(Sequence)) return false;
	}
	for (uint32 Bit = 0; Bit < AckBitCount; ++Bit)
	{
		if (!(Ack.ReceivedBits[Bit / 64] & (uint64(1) << (Bit % 64)))) continue;
		FHistoryBlock* Block = History.Find(Ack.LatestSequence - Bit);
		if (!Block || Block->bAcknowledged) continue;
		for (const auto& Pose : Block->Samples)
		{
			FBaseline* Existing = Confirmed.Find(Pose.SoldierId);
			if (!Existing || Newer(Block->Sequence, Existing->Sequence))
				Confirmed.Add(Pose.SoldierId, { Pose, Block->Sequence, Block->SimTick });
		}
		Block->bAcknowledged = true;
	}
	return true;
}

void FSender::Forget(TConstArrayView<FGuLiSoldierId> Removed)
{
	for (FGuLiSoldierId Id : Removed) Confirmed.Remove(Id);
	History.Forget(Removed);
}

uint64 FSender::GetAllocatedBytes() const { return History.GetAllocatedBytes() + Confirmed.GetAllocatedSize(); }

void FReceiver::Reset(uint32 Generation, uint32 Epoch)
{
	SyncGeneration = Generation; MatchEpoch = Epoch; LatestSequence = 0;
	History.Reset();
}

EDecodeResult FReceiver::Decode(const FGuLiEncodedPoseBlock& Block, FGuLiSoldierPoseChunk& OutChunk)
{
	if (Block.Data.IsEmpty() || Block.Data.Num() > MaxDataBytes) return EDecodeResult::InvalidPayload;
	FReader Reader(Block.Data);
	const uint16 Version = uint16(Reader.ReadBits(16));
	const uint32 Generation = Reader.ReadId();
	const uint32 Epoch = Reader.ReadId();
	if (!Reader.bValid) return EDecodeResult::InvalidPayload;
	if (SyncGeneration == 0 || Version != GULI_COMMANDER_PROTOCOL_VERSION || Generation != SyncGeneration || Epoch != MatchEpoch)
		return EDecodeResult::WrongSession;
	const uint32 Sequence = Reader.ReadId();
	FGuLiSoldierPoseChunk Chunk;
	Chunk.AuthorityEpoch = Epoch;
	Chunk.FrameSequence = Reader.ReadId();
	Chunk.ServerSimTick = Reader.ReadId();
	const uint32 TimeBits = uint32(Reader.ReadBits(32));
	FMemory::Memcpy(&Chunk.ServerTimeSeconds, &TimeBits, sizeof(TimeBits));
	const uint32 ChunkIndex = Reader.ReadId(), ChunkCount = Reader.ReadId();
	const uint8 Count = uint8(Reader.ReadBits(6));
	if (!Reader.bValid || Sequence == 0 || Chunk.FrameSequence == 0 || ChunkCount == 0
		|| ChunkCount > GULI_MAX_POSE_CHUNKS_PER_FRAME || ChunkIndex >= ChunkCount
		|| Count == 0 || Count > GULI_MAX_POSE_SAMPLES_PER_CHUNK
		|| !FMath::IsFinite(Chunk.ServerTimeSeconds) || Chunk.ServerTimeSeconds < 0) return EDecodeResult::InvalidPayload;
	if (History.Find(Sequence)) return EDecodeResult::Duplicate;
	if (LatestSequence != 0 && !Newer(Sequence, LatestSequence) && LatestSequence - Sequence >= HistoryCapacity)
		return EDecodeResult::TooOld;
	Chunk.ChunkIndex = uint16(ChunkIndex); Chunk.ChunkCount = uint16(ChunkCount);
	Chunk.Samples.Reserve(Count);
	uint32 PreviousId = 0;
	for (uint8 Index = 0; Index < Count; ++Index)
	{
		const uint32 IdDelta = Reader.ReadId();
		if (!Reader.bValid || IdDelta == 0 || uint64(PreviousId) + IdDelta > MAX_uint32) return EDecodeResult::InvalidPayload;
		const FGuLiSoldierId Id(PreviousId + IdDelta);
		PreviousId = Id.Value;
		const uint32 BaselineDistance = Reader.ReadId();
		if (!Reader.bValid || BaselineDistance > MaxBaselineBlocks) return EDecodeResult::InvalidPayload;
		FGuLiCompressedSoldierPose Pose;
		int64 Values[6] = {};
		if (BaselineDistance)
		{
			const FHistoryBlock* Base = History.Find(Sequence - BaselineDistance);
			const auto* Sample = Base ? FindSample(*Base, Id) : nullptr;
			if (!Sample) return EDecodeResult::MissingBaseline;
			if (Chunk.ServerSimTick - Base->SimTick > MaxBaselineSteps) return EDecodeResult::InvalidPayload;
			Pose = *Sample;
			Predict(Pose, Chunk.ServerSimTick - Base->SimTick, Values);
		}
		Pose.SoldierId = Id;
		const uint16 Mask = uint16(Reader.ReadBits(10));
		if (!BaselineDistance && Mask != AllFields) return EDecodeResult::InvalidPayload;
		for (int32 Axis = 0; Axis < 6; ++Axis)
		{
			if (Mask & (1u << Axis)) Values[Axis] += Reader.ReadSigned();
			if (Values[Axis] < (Axis < 3 ? MIN_int32 : MIN_int16)
				|| Values[Axis] > (Axis < 3 ? MAX_int32 : MAX_int16)) return EDecodeResult::InvalidPayload;
		}
		Pose.WorldXMeters = int32(Values[0]); Pose.WorldYMeters = int32(Values[1]); Pose.WorldZDecimeters = int32(Values[2]);
		Pose.VelocityXMetersPerSecond = int16(Values[3]); Pose.VelocityYMetersPerSecond = int16(Values[4]); Pose.VelocityZMetersPerSecond = int16(Values[5]);
		if (Mask & (1u << 6))
		{
			if (BaselineDistance)
			{
				const int64 Difference = Reader.ReadSigned();
				if (Difference < -128 || Difference > 127) return EDecodeResult::InvalidPayload;
				Pose.FacingYaw = uint8(int32(Pose.FacingYaw) + Difference);
			}
			else Pose.FacingYaw = uint8(Reader.ReadBits(8));
		}
		if (Mask & (1u << 7)) Pose.ActiveOrderId = Reader.ReadId();
		if (Mask & (1u << 8)) Pose.State = EGuLiSoldierPoseState(Reader.ReadBits(2));
		if (Mask & (1u << 9)) Pose.Flags = uint8(Reader.ReadBits(1));
		if (!Reader.bValid || !IsPoseValid(Pose) || (BaselineDistance && Pose.IsTeleport())) return EDecodeResult::InvalidPayload;
		Chunk.Samples.Add(Pose);
	}
	// Only zero byte padding is allowed; no partially decoded block enters history or the ACK window.
	const int64 Remaining = Reader.Bits.GetBitsLeft();
	if (!Reader.bValid || Remaining >= 8 || Reader.ReadBits(int32(Remaining)) != 0) return EDecodeResult::InvalidPayload;
	History.Store(Sequence, Chunk.ServerSimTick, Chunk.Samples);
	if (LatestSequence == 0 || Newer(Sequence, LatestSequence)) LatestSequence = Sequence;
	OutChunk = MoveTemp(Chunk);
	return EDecodeResult::Decoded;
}

bool FReceiver::BuildAcknowledgment(FGuLiPoseAcknowledgment& OutAck) const
{
	if (LatestSequence == 0) return false;
	OutAck = {};
	OutAck.SyncGeneration = SyncGeneration; OutAck.MatchEpoch = MatchEpoch; OutAck.LatestSequence = LatestSequence;
	for (uint32 Bit = 0; Bit < AckBitCount; ++Bit)
		if (History.Find(LatestSequence - Bit)) OutAck.ReceivedBits[Bit / 64] |= uint64(1) << (Bit % 64);
	return true;
}

void FReceiver::Forget(TConstArrayView<FGuLiSoldierId> Removed) { History.Forget(Removed); }
}

bool FGuLiCompressedSoldierPose::SetWorldLocationCentimeters(const FVector& WorldLocation)
{
	int64 X, Y, Z;
	using namespace GuLiCommanderPoseCodec;
	if (!QuantizeAxis(WorldLocation.X, GULI_POSE_XY_STEP_CENTIMETERS, MIN_int32, MAX_int32, X)
		|| !QuantizeAxis(WorldLocation.Y, GULI_POSE_XY_STEP_CENTIMETERS, MIN_int32, MAX_int32, Y)
		|| !QuantizeAxis(WorldLocation.Z, GULI_POSE_Z_STEP_CENTIMETERS, MIN_int32, MAX_int32, Z)) return false;
	WorldXMeters = int32(X); WorldYMeters = int32(Y); WorldZDecimeters = int32(Z);
	return true;
}

FVector FGuLiCompressedSoldierPose::GetWorldLocationCentimeters() const
{
	return FVector(WorldXMeters * GULI_POSE_XY_STEP_CENTIMETERS, WorldYMeters * GULI_POSE_XY_STEP_CENTIMETERS,
		WorldZDecimeters * GULI_POSE_Z_STEP_CENTIMETERS);
}

bool FGuLiCompressedSoldierPose::SetVelocityCentimetersPerSecond(const FVector& Velocity)
{
	int64 X, Y, Z;
	using namespace GuLiCommanderPoseCodec;
	if (!QuantizeAxis(Velocity.X, GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND, MIN_int16, MAX_int16, X)
		|| !QuantizeAxis(Velocity.Y, GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND, MIN_int16, MAX_int16, Y)
		|| !QuantizeAxis(Velocity.Z, GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND, MIN_int16, MAX_int16, Z)) return false;
	VelocityXMetersPerSecond = int16(X); VelocityYMetersPerSecond = int16(Y); VelocityZMetersPerSecond = int16(Z);
	return true;
}

FVector FGuLiCompressedSoldierPose::GetVelocityCentimetersPerSecond() const
{
	return FVector(VelocityXMetersPerSecond, VelocityYMetersPerSecond, VelocityZMetersPerSecond)
		* GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND;
}

uint8 GuLiCommanderProtocol::QuantizeYawDegrees(float YawDegrees)
{
	check(FMath::IsFinite(YawDegrees));
	return uint8(FMath::RoundToInt(FRotator::ClampAxis(YawDegrees) * (256.0f / 360.0f)) & 255);
}

float GuLiCommanderProtocol::DequantizeYawDegrees(uint8 QuantizedYaw) { return float(QuantizedYaw) * (360.0f / 256.0f); }

bool FGuLiEncodedPoseBlock::NetSerialize(FArchive& Ar, UPackageMap*, bool& bOutSuccess)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Wire);
	TOptional<GuLiCommanderPoseMetrics::FScope> ReadMeasure;
	if (Ar.IsLoading()) ReadMeasure.Emplace(GuLiCommanderPoseMetrics::EScope::WireRead);
	uint16 Bytes = Ar.IsSaving() ? uint16(Data.Num()) : 0;
	if (Ar.IsSaving() && (Data.IsEmpty() || Data.Num() > GuLiCommanderPoseCodec::MaxDataBytes))
	{
		bOutSuccess = false; Ar.SetError(); return true;
	}
	Ar.SerializeBits(&Bytes, 10);
	if (Ar.IsError() || Bytes == 0 || Bytes > GuLiCommanderPoseCodec::MaxDataBytes)
	{
		bOutSuccess = false; Ar.SetError(); return true;
	}
	if (Ar.IsLoading()) Data.SetNumUninitialized(Bytes);
	Ar.SerializeBits(Data.GetData(), Bytes * 8);
	bOutSuccess = !Ar.IsError();
	return true;
}

bool FGuLiPoseAcknowledgment::NetSerialize(FArchive& Ar, UPackageMap*, bool& bOutSuccess)
{
	Ar << ProtocolVersion;
	Ar.SerializeIntPacked(SyncGeneration); Ar.SerializeIntPacked(MatchEpoch); Ar.SerializeIntPacked(LatestSequence);
	Ar.SerializeBits(ReceivedBits, 512);
	bOutSuccess = !Ar.IsError() && ProtocolVersion == GULI_COMMANDER_PROTOCOL_VERSION
		&& SyncGeneration != 0 && MatchEpoch != 0 && LatestSequence != 0 && (ReceivedBits[0] & 1u);
	return true;
}
