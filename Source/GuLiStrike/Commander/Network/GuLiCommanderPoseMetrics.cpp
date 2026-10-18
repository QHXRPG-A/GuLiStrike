#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include <atomic>

namespace GuLiCommanderPoseMetrics
{
static std::atomic<bool> bCapturing = false;
static FCriticalSection MetricsMutex;
static uint64 CaptureGeneration=0;
static FSnapshot Snapshot;
static TMap<FGuLiSoldierId, uint32> LastSampleTicks;
static uint32 SampleEpoch = 0;
void BeginCapture() {FScopeLock Lock(&MetricsMutex);++CaptureGeneration;Snapshot = {}; LastSampleTicks.Empty(); SampleEpoch = 0; bCapturing = true; }
void EndCapture() {FScopeLock Lock(&MetricsMutex);bCapturing = false; LastSampleTicks.Empty(); }
FSnapshot GetSnapshot() {FScopeLock Lock(&MetricsMutex);return Snapshot; }
void RecordDecoded(const FGuLiSoldierPoseChunk& Chunk)
{
	if (!bCapturing) return;
    FScopeLock Lock(&MetricsMutex);if (!bCapturing) return;
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
void RecordFailure() {if (!bCapturing) return;FScopeLock Lock(&MetricsMutex);if (bCapturing) ++Snapshot.DecodeFailures; }
void RecordEncoded(uint32 Absolute, uint32 Delta, uint32 Bytes)
{
	if (!bCapturing) return;
    FScopeLock Lock(&MetricsMutex);if (!bCapturing) return;
	Snapshot.AbsoluteSamples += Absolute;
	Snapshot.DeltaSamples += Delta;
	Snapshot.PayloadBytes += Bytes;
}
FScope::FScope(EScope InScope) : Scope(InScope), Started(0)
{if (bCapturing) {FScopeLock Lock(&MetricsMutex);if (bCapturing) {Started=FPlatformTime::Seconds();Generation=CaptureGeneration;}}}
FScope::~FScope()
{
	if (Started == 0.0) return;
    FScopeLock Lock(&MetricsMutex);if (!bCapturing||Generation!=CaptureGeneration) return;
	const uint8 Index = static_cast<uint8>(Scope);
	Snapshot.Milliseconds[Index] += (FPlatformTime::Seconds() - Started) * 1000.0;
	++Snapshot.Calls[Index];
}
}
