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
constexpr uint16 AllFields = (1u << 13u) - 1u;
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

void Predict(const FGuLiQuantizedSoldierPose& Pose, uint32 Steps, int64 (&Values)[6])
{
	static_assert(GuLiCommanderSimulationTiming::RateHz == 10u);
	Values[0] = int64(Pose.WorldXUnits) + RoundTenths(int64(Pose.VelocityXUnits) * Steps);
	Values[1] = int64(Pose.WorldYUnits) + RoundTenths(int64(Pose.VelocityYUnits) * Steps);
	// One 20 cm/s velocity unit for one 0.1-second step is one 2 cm Z unit.
	Values[2] = int64(Pose.WorldZUnits) + int64(Pose.VelocityZUnits) * Steps;
	Values[3] = Pose.VelocityXUnits;
	Values[4] = Pose.VelocityYUnits;
	Values[5] = Pose.VelocityZUnits;
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
	explicit FReader(TConstArrayView<uint8> Data) : Bits(Data.GetData(), Data.Num() * 8) {}
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

bool IsPoseValid(const FGuLiQuantizedSoldierPose& Pose)
{
	return Pose.SoldierId.IsValid() && uint8(Pose.State) <= uint8(EGuLiSoldierPoseState::Destroyed)
		&& (Pose.Flags & ~GULI_VALID_SOLDIER_POSE_FLAGS) == 0
		&& FMath::Abs(int32(Pose.LeftGunPitch)) <= 8900 && FMath::Abs(int32(Pose.RightGunPitch)) <= 8900
		&& Pose.VATClip < 7;
}

const FGuLiQuantizedSoldierPose* FindSample(const FHistoryBlock& Block, FGuLiSoldierId Id)
{
	const int32 Index = Algo::LowerBoundBy(Block.Samples, Id, &FGuLiQuantizedSoldierPose::SoldierId);
	return Block.Samples.IsValidIndex(Index) && Block.Samples[Index].SoldierId == Id ? &Block.Samples[Index] : nullptr;
}

void WriteRecord(FBitWriter& Writer, const FGuLiQuantizedSoldierPose& Pose,
	uint32 PreviousId, const FBaseline* Baseline, uint32 Sequence, uint32 SimTick,bool SingleMass)
{
	if (!SingleMass) WriteUnsigned(Writer, Pose.SoldierId.Value - PreviousId);
	WriteUnsigned(Writer, Baseline ? Sequence - Baseline->Sequence : 0u);
	int64 Predicted[6] = {};
	if (Baseline) Predict(Baseline->Pose, SimTick - Baseline->SimTick, Predicted);
	const int64 Current[6] = { Pose.WorldXUnits, Pose.WorldYUnits, Pose.WorldZUnits,
		Pose.VelocityXUnits, Pose.VelocityYUnits, Pose.VelocityZUnits };
	uint16 Mask = 0;
	for (int32 Axis = 0; Axis < 6; ++Axis)
		if (!Baseline || Current[Axis] != Predicted[Axis]) Mask |= 1u << Axis;
	const int32 YawDelta = Baseline ? ((int32(Pose.FacingYaw) - Baseline->Pose.FacingYaw + 128) & 255) - 128 : 0;
	if (!Baseline || YawDelta != 0) Mask |= 1u << 6;
	if (!Baseline || Pose.ActiveOrderId != Baseline->Pose.ActiveOrderId) Mask |= 1u << 7;
	if (!Baseline || Pose.State != Baseline->Pose.State) Mask |= 1u << 8;
	if (!Baseline || Pose.Flags != Baseline->Pose.Flags) Mask |= 1u << 9;
	if (!Baseline || Pose.UpperYaw != Baseline->Pose.UpperYaw
		|| Pose.LeftGunPitch != Baseline->Pose.LeftGunPitch || Pose.RightGunPitch != Baseline->Pose.RightGunPitch) Mask |= 1u << 10;
	if (!Baseline || Pose.HoverBlendStartMilliseconds != Baseline->Pose.HoverBlendStartMilliseconds
		|| Pose.HoverBlendFromWeight != Baseline->Pose.HoverBlendFromWeight || Pose.bHoverIdleTarget != Baseline->Pose.bHoverIdleTarget) Mask |= 1u << 11;
	if (!Baseline || Pose.VATClip != Baseline->Pose.VATClip || Pose.VATPhase != Baseline->Pose.VATPhase
		|| Pose.VATRate != Baseline->Pose.VATRate) Mask |= 1u << 12;
	Writer.SerializeBits(&Mask, 13);
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
	if (Mask & (1u << 10))
	{
		uint16 Yaw = Pose.UpperYaw;
		int16 Left = Pose.LeftGunPitch, Right = Pose.RightGunPitch;
		Writer.SerializeBits(&Yaw, 16); Writer.SerializeBits(&Left, 16); Writer.SerializeBits(&Right, 16);
	}
	if (Mask & (1u << 11))
	{
		WriteUnsigned(Writer, Pose.HoverBlendStartMilliseconds);
		uint8 From = Pose.HoverBlendFromWeight, Target = Pose.bHoverIdleTarget ? 1 : 0;
		Writer.SerializeBits(&From, 8); Writer.SerializeBits(&Target, 1);
	}
	if (Mask & (1u << 12))
	{
		uint8 Clip = Pose.VATClip; uint16 Phase = Pose.VATPhase, Rate = Pose.VATRate;
		Writer.SerializeBits(&Clip, 3); Writer.SerializeBits(&Phase, 16); Writer.SerializeBits(&Rate, 16);
	}
}
}

bool Quantize(const FVector& Position, const FVector& Velocity, float Yaw, FGuLiQuantizedSoldierPose& OutPose)
{
	if (!FMath::IsFinite(Yaw)) return false;
	if (!OutPose.SetWorldLocationCentimeters(Position) || !OutPose.SetVelocityCentimetersPerSecond(Velocity)) return false;
	OutPose.FacingYaw = GuLiCommanderProtocol::QuantizeYawDegrees(Yaw);
	return true;
}

void FHistory::Reset() { Blocks.Empty(); }

void FHistory::Store(uint32 Sequence, uint32 SimTick, TConstArrayView<FGuLiQuantizedSoldierPose> Samples)
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
	check((Generation == 0) == (Epoch == 0));
	SyncGeneration = Generation; MatchEpoch = Epoch; LastSequence = 0;
	History.Reset(); Confirmed.Empty();
	MassConnection.Invalidate(); MassControlEpoch=0; MassSessionFloor=MassStateFirstSequence=0;
	MassDelivery.reset(); bMassStateConfirmed=bLastMassIdleLoop=false; LastMassSentAt=-1; LastMassSampleTime=0;
}

bool IsStationaryPose(const FGuLiQuantizedSoldierPose& Pose)
{
    return !Pose.IsTeleport() && !Pose.VelocityXUnits && !Pose.VelocityYUnits && !Pose.VelocityZUnits;
}

static bool SameMassFields(const FGuLiQuantizedSoldierPose& P,const FGuLiQuantizedSoldierPose& B)
{
	return P.SoldierId==B.SoldierId && P.WorldXUnits==B.WorldXUnits && P.WorldYUnits==B.WorldYUnits && P.WorldZUnits==B.WorldZUnits
		&& P.VelocityXUnits==B.VelocityXUnits && P.VelocityYUnits==B.VelocityYUnits && P.VelocityZUnits==B.VelocityZUnits
		&& P.FacingYaw==B.FacingYaw && P.ActiveOrderId==B.ActiveOrderId && P.State==B.State && P.Flags==B.Flags
		&& P.UpperYaw==B.UpperYaw && P.LeftGunPitch==B.LeftGunPitch && P.RightGunPitch==B.RightGunPitch
		&& P.HoverBlendStartMilliseconds==B.HoverBlendStartMilliseconds && P.HoverBlendFromWeight==B.HoverBlendFromWeight
		&& P.bHoverIdleTarget==B.bHoverIdleTarget && P.VATClip==B.VATClip && P.VATRate==B.VATRate;
}

static bool NaturalIdlePhase(const FGuLiQuantizedSoldierPose& Pose,const FGuLiQuantizedSoldierPose& Anchor,double Seconds)
{
	if (Seconds<0) return false;
	const double Expected=double(Anchor.VATPhase)/65535.0 + Seconds*Anchor.VATRate/256.0;
	const double Error=double(Pose.VATPhase)/65535.0-Expected;
	// Two rounded phase samples and half a step of rate quantization per second.
	return FMath::Abs(Error-FMath::RoundToDouble(Error))<=1.0/65535.0+Seconds/512.0+1.e-6;
}

EMassPosePreparation FSender::PrepareMass(const FGuLiSoldierPoseChunk& Chunk,const FGuid& Connection,
	uint64 ControlEpoch,bool bIdleLoop,double Now,FPreparedBlock& Out)
{
	using EReason=GuLiNet::MassPoseReason;
	using FDelivery=GuLiNet::MotionDelivery;
	if (Chunk.Samples.Num()!=1 || !Connection.IsValid() || Chunk.AuthorityEpoch!=MatchEpoch)
		return EMassPosePreparation::Invalid;
	if (Connection!=MassConnection || ControlEpoch!=MassControlEpoch)
	{
		// A surviving receiver may retain its sequence window across reconnect.
		// Keep numbering monotonic, but bootstrap this channel from an absolute pose.
		MassConnection=Connection; MassControlEpoch=ControlEpoch;
		MassSessionFloor=LastSequence==MAX_uint32 ? 1u : LastSequence+1u;
		Confirmed.Reset(); History.Reset(); MassDelivery.reset(); MassStateFirstSequence=0;
		bMassStateConfirmed=false; LastMassSentAt=-1;
	}
	const auto& P=Chunk.Samples[0];
	const auto* Last=History.Find(LastSequence);
	const auto* B=Last ? FindSample(*Last,P.SoldierId) : nullptr;
	const bool Stationary=IsStationaryPose(P);
	const bool PredictPhase=Stationary && bIdleLoop && bLastMassIdleLoop;
	const bool SameFields=B && SameMassFields(P,*B) && bIdleLoop==bLastMassIdleLoop;
	const double SampleTime=Chunk.HybridSampleTime();
	const bool SamePhase=SameFields && (PredictPhase ? NaturalIdlePhase(P,*B,SampleTime-LastMassSampleTime)
		: P.VATPhase==B->VATPhase);
	const bool NewState=!Stationary || !SamePhase || !MassDelivery;
	EReason Reason=EReason::Fields;
	if (!B) Reason=EReason::First;
	else if (SameFields && PredictPhase && !SamePhase) Reason=EReason::Animation;
	if (!NewState)
	{
		const double SentAt=MassDelivery->SentAt.load();
		const bool Pending=SentAt==FDelivery::Pending;
		const double Reference=SentAt>=0 ? SentAt : LastMassSentAt;
		const bool Due=Reference<0 || Now-Reference >= (bMassStateConfirmed ? .5 : .2);
		if (Pending || (!Due && (SentAt>=0 || bMassStateConfirmed)))
			return P.VATPhase==B->VATPhase ? EMassPosePreparation::SkipExact : EMassPosePreparation::SkipNaturalPhase;
		if (bMassStateConfirmed) Reason=EReason::Heartbeat;
		else if (SentAt==FDelivery::Discarded) Reason=MassDelivery->Reason;
		else Reason=EReason::Retry;
	}
	if (!PrepareBlock(Chunk,700,Out,true)) return EMassPosePreparation::Invalid;
	Out.bNewMassState=NewState; Out.bMassIdleLoop=bIdleLoop; Out.MassSampleTime=SampleTime;
	if (Stationary) Out.MassDelivery=std::make_shared<FDelivery>(Reason);
	return EMassPosePreparation::Ready;
}

bool EncodeAcknowledgment(const FGuLiPoseAcknowledgment& Ack,TArray<uint8>& Out)
{
	Out.Reset();
	if (Ack.ProtocolVersion!=GULI_COMMANDER_PROTOCOL_VERSION || !Ack.SyncGeneration || !Ack.MatchEpoch
		|| !Ack.LatestSequence || !(Ack.ReceivedBits[0]&1u)) return false;
	uint8 Bitmap[64]{};int32 ByteCount=64;
	for (int32 I=0;I<64;++I) Bitmap[I]=uint8(Ack.ReceivedBits[I/8]>>((I%8)*8));
	while (ByteCount>1 && !Bitmap[ByteCount-1]) --ByteCount;
	FBitWriter Runs(512,true);bool Set=true;uint32 Start=0;
	for (uint32 Bit=1;Bit<=AckBitCount;++Bit)
	{
		const bool Next=Bit<AckBitCount && (Ack.ReceivedBits[Bit/64]&(uint64(1)<<(Bit%64)))!=0;
		if (Bit==AckBitCount || Next!=Set) {WriteUnsigned(Runs,Bit-Start);Start=Bit;Set=Next;}
	}
	FBitWriter Writer(1024,true);uint16 Version=Ack.ProtocolVersion;Writer.SerializeBits(&Version,16);
	WriteUnsigned(Writer,Ack.SyncGeneration);WriteUnsigned(Writer,Ack.MatchEpoch);WriteUnsigned(Writer,Ack.LatestSequence);
	uint8 Mode=Runs.GetNumBytes()<ByteCount+1?1:0;Writer.SerializeBits(&Mode,8);
	if (Mode) Writer.SerializeBits(Runs.GetData(),Runs.GetNumBits());
	else {WriteUnsigned(Writer,ByteCount);Writer.SerializeBits(Bitmap,ByteCount*8);}
	Out.Append(Writer.GetData(),int32(Writer.GetNumBytes()));return true;
}

bool DecodeAcknowledgment(TConstArrayView<uint8> Bytes,FGuLiPoseAcknowledgment& Out)
{
	if (Bytes.IsEmpty() || Bytes.Num()>83) return false;
	FReader R(Bytes);FGuLiPoseAcknowledgment Ack;
	Ack.ProtocolVersion=uint16(R.ReadBits(16));Ack.SyncGeneration=R.ReadId();Ack.MatchEpoch=R.ReadId();Ack.LatestSequence=R.ReadId();
	const auto Mode=R.ReadBits(8);
	if (Mode==0)
	{
		const auto Count=R.ReadUnsigned(64);if (!Count) return false;
		uint8 Last=0;
		for (uint32 I=0;I<Count && R.bValid;++I) {Last=uint8(R.ReadBits(8));Ack.ReceivedBits[I/8]|=uint64(Last)<<((I%8)*8);}
		if (!Last) return false;
	}
	else if (Mode==1)
	{
		uint32 Bit=0;bool Set=true;
		while (Bit<AckBitCount && R.bValid)
		{
			const auto Count=R.ReadUnsigned(AckBitCount-Bit);if (!Count) return false;
			for (uint32 End=Bit+uint32(Count);Bit<End;++Bit) if (Set) Ack.ReceivedBits[Bit/64]|=uint64(1)<<(Bit%64);
			Set=!Set;
		}
	}
	else return false;
	if (!R.bValid || R.Bits.GetBitsLeft()!=0 || Ack.ProtocolVersion!=GULI_COMMANDER_PROTOCOL_VERSION
		|| !Ack.SyncGeneration || !Ack.MatchEpoch || !Ack.LatestSequence || !(Ack.ReceivedBits[0]&1u)) return false;
	Out=Ack;return true;
}

bool FSender::Prepare(const FGuLiSoldierPoseChunk& Chunk, int32 MaxBytes, FPreparedBlock& Out) const
{
	return PrepareBlock(Chunk,MaxBytes,Out,false);
}

bool FSender::PrepareBlock(const FGuLiSoldierPoseChunk& Chunk,int32 MaxBytes,FPreparedBlock& Out,bool SingleMass) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Encode);
	Out = FPreparedBlock{};
	if (!SyncGeneration || MatchEpoch != Chunk.AuthorityEpoch || !Chunk.FrameSequence
		|| !Chunk.ChunkCount || Chunk.ChunkCount > GULI_MAX_POSE_CHUNKS_PER_FRAME || Chunk.ChunkIndex >= Chunk.ChunkCount
		|| !FMath::IsFinite(Chunk.ServerTimeSeconds) || Chunk.ServerTimeSeconds < 0 || Chunk.Samples.IsEmpty()) return false;
	const int32 DataBudget = FMath::Min(MaxBytes, MaxWireBytes) - 2;
	if (DataBudget <= 0) return false;
	TArray<FGuLiQuantizedSoldierPose> Sorted = Chunk.Samples;
	Sorted.Sort([](const auto& A, const auto& B) { return A.SoldierId < B.SoldierId; });
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
		if (!IsPoseValid(Sorted[Index]) || (Index && Sorted[Index - 1].SoldierId == Sorted[Index].SoldierId)) return false;
	Out.Sequence = LastSequence == MAX_uint32 ? 1u : LastSequence + 1u;
	Out.SimTick = Chunk.ServerSimTick;
	FBitWriter Writer(DataBudget * 8, true);
	// The authenticated hybrid envelope already identifies the wire version,
	// soldier lifetime, owner generation and capture tick.
	if (!SingleMass)
	{
		uint16 Version=GULI_COMMANDER_PROTOCOL_VERSION;
		Writer.SerializeBits(&Version,16); WriteUnsigned(Writer,SyncGeneration);
	}
	WriteUnsigned(Writer,MatchEpoch); WriteUnsigned(Writer,Out.Sequence);
	if (!SingleMass) WriteUnsigned(Writer,Chunk.FrameSequence);
	WriteUnsigned(Writer, Chunk.ServerSimTick);
	int32 CountBitOffset=0;
	uint8 Count = 0;
	if (!SingleMass)
	{
		float Time=Chunk.ServerTimeSeconds; Writer.SerializeBits(&Time,32);
		WriteUnsigned(Writer,Chunk.ChunkIndex); WriteUnsigned(Writer,Chunk.ChunkCount);
		CountBitOffset=Writer.GetNumBits(); Writer.SerializeBits(&Count,6);
	}
	uint32 PreviousId = 0;
	for (const auto& Pose : Sorted)
	{
		if (Count == GULI_MAX_POSE_SAMPLES_PER_CHUNK) break;
		const FBaseline* Baseline = Confirmed.Find(Pose.SoldierId);
		if (Baseline && (Pose.IsTeleport() || Chunk.ServerSimTick - Baseline->SimTick > MaxBaselineSteps
			|| Out.Sequence - Baseline->Sequence > MaxBaselineBlocks)) Baseline = nullptr;
		FBitWriter Record(512, true);
		WriteRecord(Record, Pose, PreviousId, Baseline, Out.Sequence, Chunk.ServerSimTick,SingleMass);
		if (Writer.GetNumBits() + Record.GetNumBits() > DataBudget * 8) break;
		Writer.SerializeBits(Record.GetData(), Record.GetNumBits());
		PreviousId = Pose.SoldierId.Value;
		Out.AbsoluteCount += Baseline == nullptr;
		Out.Samples.Add(Pose);
		++Count;
	}
	if (!Count) return false;
	if (!SingleMass) appBitsCpy(Writer.GetData(), CountBitOffset, &Count, 0, 6);
	Out.Block.Data.Append(Writer.GetData(), int32(Writer.GetNumBytes()));
	return true;
}

void FSender::Commit(const FPreparedBlock& Prepared)
{
	check(Prepared.Sequence == (LastSequence == MAX_uint32 ? 1u : LastSequence + 1u) && !Prepared.Samples.IsEmpty());
	LastSequence = Prepared.Sequence;
	History.Store(LastSequence, Prepared.SimTick, Prepared.Samples);
	if (Prepared.bNewMassState)
	{
		MassStateFirstSequence=LastSequence; bMassStateConfirmed=false; LastMassSentAt=-1;
	}
	else if (MassDelivery && MassDelivery->SentAt.load()>=0) LastMassSentAt=MassDelivery->SentAt.load();
	MassDelivery=Prepared.MassDelivery; LastMassSampleTime=Prepared.MassSampleTime; bLastMassIdleLoop=Prepared.bMassIdleLoop;
	// An ACK can arrive between worker preparation and the admission callback.
	if (bMassStateConfirmed && MassDelivery && MassDelivery->Reason==GuLiNet::MassPoseReason::Retry)
		MassDelivery->DiscardPending();
	GuLiCommanderPoseMetrics::RecordEncoded(Prepared.AbsoluteCount, Prepared.Samples.Num() - Prepared.AbsoluteCount,
		Prepared.Block.Data.Num() + 2);
}

void FSender::Encode(const FGuLiSoldierPoseChunk& Chunk, TArray<FGuLiEncodedPoseBlock>& OutBlocks)
{
	OutBlocks.Reset();
	FGuLiSoldierPoseChunk Remaining = Chunk;
	FPreparedBlock Prepared;
	while (Prepare(Remaining, MaxWireBytes, Prepared))
	{
		Commit(Prepared);
		TSet<FGuLiSoldierId> Sent;
		for (const auto& Sample : Prepared.Samples) Sent.Add(Sample.SoldierId);
		Remaining.Samples.RemoveAll([&](const auto& Sample) { return Sent.Contains(Sample.SoldierId); });
		OutBlocks.Add(MoveTemp(Prepared.Block));
	}
}

bool FSender::Confirm(const FGuLiPoseAcknowledgment& Ack)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Confirm);
	if (Ack.ProtocolVersion != GULI_COMMANDER_PROTOCOL_VERSION || SyncGeneration == 0
		|| Ack.SyncGeneration != SyncGeneration || Ack.MatchEpoch != MatchEpoch || Ack.LatestSequence == 0
		|| !(Ack.ReceivedBits[0] & 1u) || LastSequence == 0 || Newer(Ack.LatestSequence, LastSequence)
		|| LastSequence - Ack.LatestSequence >= HistoryCapacity) return false;
	// Validate the bitmap before committing: every retained bit must refer to a block we actually sent.
	for (uint32 Bit = 0; Bit < AckBitCount; ++Bit)
	{
		if (!(Ack.ReceivedBits[Bit / 64] & (uint64(1) << (Bit % 64)))) continue;
		const uint32 Sequence = Ack.LatestSequence - Bit;
		if (MassSessionFloor && Newer(MassSessionFloor,Sequence)) continue;
		if (LastSequence - Sequence < HistoryCapacity && !History.Find(Sequence)) return false;
	}
	for (uint32 Bit = 0; Bit < AckBitCount; ++Bit)
	{
		if (!(Ack.ReceivedBits[Bit / 64] & (uint64(1) << (Bit % 64)))) continue;
		if (MassSessionFloor && Newer(MassSessionFloor,Ack.LatestSequence-Bit)) continue;
		FHistoryBlock* Block = History.Find(Ack.LatestSequence - Bit);
		if (!Block || Block->bAcknowledged) continue;
		for (const auto& Pose : Block->Samples)
		{
			FBaseline* Existing = Confirmed.Find(Pose.SoldierId);
			if (!Existing || Newer(Block->Sequence, Existing->Sequence))
				Confirmed.Add(Pose.SoldierId, { Pose, Block->Sequence, Block->SimTick });
		}
		Block->bAcknowledged = true;
		if (MassStateFirstSequence && !Newer(MassStateFirstSequence,Block->Sequence) && !Block->Samples.IsEmpty())
		{
			bMassStateConfirmed=true;
			if (MassDelivery && MassDelivery->Reason==GuLiNet::MassPoseReason::Retry && Block->Sequence!=LastSequence)
				MassDelivery->DiscardPending();
		}
	}
	return true;
}

void FSender::Forget(TConstArrayView<FGuLiSoldierId> Removed)
{
	if (MassSessionFloor && !Removed.IsEmpty())
	{
		MassStateFirstSequence=0; bMassStateConfirmed=false; LastMassSentAt=-1; MassDelivery.reset();
	}
	for (FGuLiSoldierId Id : Removed) Confirmed.Remove(Id);
	History.Forget(Removed);
}

uint64 FSender::GetAllocatedBytes() const { return History.GetAllocatedBytes() + Confirmed.GetAllocatedSize(); }

void FReceiver::Reset(uint32 Generation, uint32 Epoch)
{
	check((Generation == 0) == (Epoch == 0));
	SyncGeneration = Generation; MatchEpoch = Epoch; LatestSequence = 0;
	History.Reset();
}

EDecodeResult FReceiver::Decode(const FGuLiEncodedPoseBlock& Block, FGuLiSoldierPoseChunk& OutChunk)
{
	return DecodeBlock(Block,OutChunk,FGuLiSoldierId{},0);
}

EDecodeResult FReceiver::DecodeMass(const FGuLiEncodedPoseBlock& Block,FGuLiSoldierPoseChunk& OutChunk,FGuLiSoldierId Id,uint64 At)
{
	if (!Id.IsValid() || !At || At%6 || At/6>MAX_uint32) return EDecodeResult::InvalidPayload;
	return DecodeBlock(Block,OutChunk,Id,At);
}

EDecodeResult FReceiver::DecodeBlock(const FGuLiEncodedPoseBlock& Block,FGuLiSoldierPoseChunk& OutChunk,FGuLiSoldierId BoundId,uint64 At)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Decode);
	if (Block.Data.IsEmpty() || Block.Data.Num() > MaxDataBytes) return EDecodeResult::InvalidPayload;
	FReader Reader(Block.Data);
	const bool SingleMass=BoundId.IsValid();
	const uint16 Version = SingleMass ? GULI_COMMANDER_PROTOCOL_VERSION : uint16(Reader.ReadBits(16));
	const uint32 Generation = SingleMass ? SyncGeneration : Reader.ReadId();
	const uint32 Epoch = Reader.ReadId();
	if (!Reader.bValid) return EDecodeResult::InvalidPayload;
	if (SyncGeneration == 0 || Version != GULI_COMMANDER_PROTOCOL_VERSION || Generation != SyncGeneration || Epoch != MatchEpoch)
		return EDecodeResult::WrongSession;
	const uint32 Sequence = Reader.ReadId();
	FGuLiSoldierPoseChunk Chunk;
	Chunk.AuthorityEpoch = Epoch;
	Chunk.FrameSequence = SingleMass ? uint32(At/6) : Reader.ReadId();
	Chunk.ServerSimTick = Reader.ReadId();
	uint32 ChunkIndex=0,ChunkCount=1; uint8 Count=1;
	if (SingleMass) Chunk.ServerTimeSeconds=float(Chunk.HybridSampleTime());
	else
	{
		const uint32 TimeBits=uint32(Reader.ReadBits(32)); FMemory::Memcpy(&Chunk.ServerTimeSeconds,&TimeBits,sizeof(TimeBits));
		ChunkIndex=Reader.ReadId(); ChunkCount=Reader.ReadId(); Count=uint8(Reader.ReadBits(6));
	}
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
		const uint32 IdDelta = SingleMass ? BoundId.Value : Reader.ReadId();
		if (!Reader.bValid || IdDelta == 0 || uint64(PreviousId) + IdDelta > MAX_uint32) return EDecodeResult::InvalidPayload;
		const FGuLiSoldierId Id(PreviousId + IdDelta);
		PreviousId = Id.Value;
		const uint32 BaselineDistance = Reader.ReadId();
		if (!Reader.bValid || BaselineDistance > MaxBaselineBlocks) return EDecodeResult::InvalidPayload;
		FGuLiQuantizedSoldierPose Pose;
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
		const uint16 Mask = uint16(Reader.ReadBits(13));
		if (!BaselineDistance && Mask != AllFields) return EDecodeResult::InvalidPayload;
		for (int32 Axis = 0; Axis < 6; ++Axis)
		{
			if (Mask & (1u << Axis)) Values[Axis] += Reader.ReadSigned();
			if (Values[Axis] < (Axis < 3 ? MIN_int32 : MIN_int16)
				|| Values[Axis] > (Axis < 3 ? MAX_int32 : MAX_int16)) return EDecodeResult::InvalidPayload;
		}
		Pose.WorldXUnits = int32(Values[0]); Pose.WorldYUnits = int32(Values[1]); Pose.WorldZUnits = int32(Values[2]);
		Pose.VelocityXUnits = int16(Values[3]); Pose.VelocityYUnits = int16(Values[4]); Pose.VelocityZUnits = int16(Values[5]);
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
		if (Mask & (1u << 10))
		{
			Pose.UpperYaw = uint16(Reader.ReadBits(16));
			Pose.LeftGunPitch = int16(Reader.ReadBits(16));
			Pose.RightGunPitch = int16(Reader.ReadBits(16));
		}
		if (Mask & (1u << 11))
		{
			Pose.HoverBlendStartMilliseconds = Reader.ReadId();
			Pose.HoverBlendFromWeight = uint8(Reader.ReadBits(8));
			Pose.bHoverIdleTarget = Reader.ReadBits(1) != 0;
		}
		if (Mask & (1u << 12))
		{
			Pose.VATClip = uint8(Reader.ReadBits(3));
			Pose.VATPhase = uint16(Reader.ReadBits(16));
			Pose.VATRate = uint16(Reader.ReadBits(16));
		}
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

bool FGuLiQuantizedSoldierPose::SetWorldLocationCentimeters(const FVector& WorldLocation)
{
	int64 X, Y, Z;
	using namespace GuLiCommanderPoseCodec;
	if (!QuantizeAxis(WorldLocation.X, GULI_POSE_XY_STEP_CENTIMETERS, MIN_int32, MAX_int32, X)
		|| !QuantizeAxis(WorldLocation.Y, GULI_POSE_XY_STEP_CENTIMETERS, MIN_int32, MAX_int32, Y)
		|| !QuantizeAxis(WorldLocation.Z, GULI_POSE_Z_STEP_CENTIMETERS, MIN_int32, MAX_int32, Z)) return false;
	WorldXUnits = int32(X); WorldYUnits = int32(Y); WorldZUnits = int32(Z);
	return true;
}

FVector FGuLiQuantizedSoldierPose::GetWorldLocationCentimeters() const
{
	return FVector(WorldXUnits * GULI_POSE_XY_STEP_CENTIMETERS, WorldYUnits * GULI_POSE_XY_STEP_CENTIMETERS,
		WorldZUnits * GULI_POSE_Z_STEP_CENTIMETERS);
}

bool FGuLiQuantizedSoldierPose::SetVelocityCentimetersPerSecond(const FVector& Velocity)
{
	int64 X, Y, Z;
	using namespace GuLiCommanderPoseCodec;
	if (!QuantizeAxis(Velocity.X, GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND, MIN_int16, MAX_int16, X)
		|| !QuantizeAxis(Velocity.Y, GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND, MIN_int16, MAX_int16, Y)
		|| !QuantizeAxis(Velocity.Z, GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND, MIN_int16, MAX_int16, Z)) return false;
	VelocityXUnits = int16(X); VelocityYUnits = int16(Y); VelocityZUnits = int16(Z);
	return true;
}

FVector FGuLiQuantizedSoldierPose::GetVelocityCentimetersPerSecond() const
{
	return FVector(VelocityXUnits, VelocityYUnits, VelocityZUnits)
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
