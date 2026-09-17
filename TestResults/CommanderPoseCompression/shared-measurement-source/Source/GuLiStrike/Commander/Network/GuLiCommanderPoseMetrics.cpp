#include "Commander/Network/GuLiCommanderPoseMetrics.h"

namespace GuLiCommanderPoseMetrics
{
static bool bCapturing = false;
static FSnapshot Snapshot;
void BeginCapture() { Snapshot = {}; bCapturing = true; }
void EndCapture() { bCapturing = false; }
const FSnapshot& GetSnapshot() { return Snapshot; }
void RecordDecoded(uint32 Count) { if (bCapturing) Snapshot.DecodedSamples += Count; }
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
