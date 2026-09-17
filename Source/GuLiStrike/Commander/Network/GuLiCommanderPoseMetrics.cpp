#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "Commander/Network/GuLiCommanderTypes.h"

namespace GuLiCommanderPoseMetrics
{
static bool bCapturing = false;
static FSnapshot Snapshot;
static TMap<FGuLiSoldierId, uint32> LastSampleTicks;
static uint32 SampleEpoch = 0;
void BeginCapture() { Snapshot = {}; LastSampleTicks.Empty(); SampleEpoch = 0; bCapturing = true; }
void EndCapture() { bCapturing = false; LastSampleTicks.Empty(); }
const FSnapshot& GetSnapshot() { return Snapshot; }
void RecordDecoded(const FGuLiSoldierPoseChunk& Chunk)
{
	if (!bCapturing) return;
	Snapshot.DecodedSamples += Chunk.Samples.Num();
	if (SampleEpoch != Chunk.AuthorityEpoch) { SampleEpoch = Chunk.AuthorityEpoch; LastSampleTicks.Empty(); }
	for (const auto& Pose : Chunk.Samples)
	{
		uint32* Last = LastSampleTicks.Find(Pose.SoldierId);
		if (!Last) { LastSampleTicks.Add(Pose.SoldierId, Chunk.ServerSimTick); continue; }
		const int32 Gap = int32(Chunk.ServerSimTick - *Last);
		if (Gap <= 0) continue;
		++Snapshot.SampleGapCount;
		Snapshot.SampleGapTicks += uint32(Gap);
		++Snapshot.SampleGapHistogram[FMath::Min(Gap - 1, 20)];
		*Last = Chunk.ServerSimTick;
	}
}
void RecordFailure() { if (bCapturing) ++Snapshot.DecodeFailures; }
void RecordEncoded(uint32 Absolute, uint32 Delta, uint32 Bytes)
{
	if (!bCapturing) return;
	Snapshot.AbsoluteSamples += Absolute;
	Snapshot.DeltaSamples += Delta;
	Snapshot.PayloadBytes += Bytes;
}
FScope::FScope(EScope InScope) : Scope(InScope), Started(bCapturing ? FPlatformTime::Seconds() : 0.0) {}
FScope::~FScope()
{
	if (Started == 0.0) return;
	const uint8 Index = static_cast<uint8>(Scope);
	Snapshot.Milliseconds[Index] += (FPlatformTime::Seconds() - Started) * 1000.0;
	++Snapshot.Calls[Index];
}
}
