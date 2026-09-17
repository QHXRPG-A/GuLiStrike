#pragma once

#include "CoreMinimal.h"

struct FGuLiSoldierPoseChunk;

// Process-local, opt-in diagnostics. The codec does not depend on the stress driver.
namespace GuLiCommanderPoseMetrics
{
enum class EScope : uint8 { Send, Receive, Ack, Capture, WireRead, Count };
struct FSnapshot
{
	double Milliseconds[static_cast<uint8>(EScope::Count)]{};
	uint64 Calls[static_cast<uint8>(EScope::Count)]{};
	uint64 DecodedSamples = 0;
	uint64 DecodeFailures = 0;
	uint64 AbsoluteSamples = 0;
	uint64 DeltaSamples = 0;
	uint64 PayloadBytes = 0;
	uint64 SampleGapCount = 0;
	uint64 SampleGapTicks = 0;
	// Bins 0..19 represent 1..20 simulation steps. Bin 20 represents a larger gap.
	uint64 SampleGapHistogram[21]{};
};
GULISTRIKE_API void BeginCapture();
GULISTRIKE_API void EndCapture();
GULISTRIKE_API const FSnapshot& GetSnapshot();
GULISTRIKE_API void RecordDecoded(const FGuLiSoldierPoseChunk& Chunk);
GULISTRIKE_API void RecordFailure();
GULISTRIKE_API void RecordEncoded(uint32 Absolute, uint32 Delta, uint32 Bytes);
class GULISTRIKE_API FScope
{
public:
	explicit FScope(EScope InScope);
	~FScope();
private:
	EScope Scope;
	double Started;
};
}
