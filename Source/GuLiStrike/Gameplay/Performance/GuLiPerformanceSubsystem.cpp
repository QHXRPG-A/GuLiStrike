#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool UGuLiPerformanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}
void UGuLiPerformanceSubsystem::BeginCapture()
{
	Counters.Reset(); ConnectionSamples.Reset(); NetworkSamples.Reset(); DroppedNetworkSamples = 0;
	FirstFrame = LastFrame = GFrameCounter; bCapturing = true; CaptureNetworkSample();
}
void UGuLiPerformanceSubsystem::EndCapture()
{
	if (bCapturing) CaptureNetworkSample();
	LastFrame=GFrameCounter; bCapturing=false;
}
void UGuLiPerformanceSubsystem::Tick(float)
{ if (bCapturing && FPlatformTime::Seconds() - LastNetworkSample >= 1.0) CaptureNetworkSample(); }
TStatId UGuLiPerformanceSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiPerformanceCapture, STATGROUP_Tickables); }
void UGuLiPerformanceSubsystem::Record(const FName Name, const double Value, const UObject* LocalPlayer)
{
	if (!bCapturing || !FMath::IsFinite(Value)) return;
	const FString Key = LocalPlayer ? LocalPlayer->GetPathName() + TEXT("/") + Name.ToString() : Name.ToString();
	auto& C = Counters.FindOrAdd(Key);
	if (C.Calls && C.Frame != GFrameCounter)
	{
		// Bounded diagnostic storage. Missing frames count as zero in the mean.
		if (C.FrameSamples.Num() < 4096) C.FrameSamples.Add(C.FrameTotal);
		C.FrameTotal = 0;
	}
	C.Frame = GFrameCounter; C.FrameTotal += Value; C.Total += Value; ++C.Calls;
}
FString UGuLiPerformanceSubsystem::GetCaptureJson() const
{
	auto Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("world"), GetWorld()->GetPathName());
	const uint64 Frames = FMath::Max(uint64(1), (bCapturing ? GFrameCounter : LastFrame) - FirstFrame + 1);
	Root->SetNumberField(TEXT("engine_frames"), double(Frames));
	Root->SetBoolField(TEXT("capturing"), bCapturing);
	auto Values = MakeShared<FJsonObject>();
	for (const auto& Pair : Counters)
	{
		const auto& C = Pair.Value; auto V = MakeShared<FJsonObject>();
		TArray<double> Samples = C.FrameSamples; Samples.Add(C.FrameTotal); Samples.Sort();
		V->SetNumberField(TEXT("total"), C.Total); V->SetNumberField(TEXT("calls"), double(C.Calls));
		V->SetNumberField(TEXT("mean_per_engine_frame"), C.Total / Frames);
		V->SetNumberField(TEXT("p95_active_frame"), Samples[FMath::Clamp(FMath::CeilToInt(Samples.Num() * .95) - 1, 0, Samples.Num() - 1)]);
		V->SetNumberField(TEXT("stored_active_frames"), Samples.Num());
		Values->SetObjectField(Pair.Key, V);
	}
	Root->SetObjectField(TEXT("counters"), Values);
	Root->SetArrayField(TEXT("network_samples"), NetworkSamples);
	Root->SetNumberField(TEXT("network_samples_dropped"), DroppedNetworkSamples);
	Root->SetStringField(TEXT("network_counter_basis"), TEXT("Cumulative application counters; UE connection deltas per wall second; decimal KB. Never add both ends of a connection."));
	Root->SetStringField(TEXT("flight_record_byte_basis"), TEXT("Record body + 2-byte length, excluding 1-byte batch header. Produced/enqueued record bytes are measured only while capturing, after muzzle attachment; RPC bytes count submission, not delivery."));
#if PLATFORM_WINDOWS
	Root->SetNumberField(TEXT("network_wall_seconds_qpc_offset"), 16777216.0);
#endif
	FString Json; const auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Root, Writer); return Json;
}
FGuLiPerformanceScope::FGuLiPerformanceScope(UWorld* World, FName InName, const UObject* Player) : Name(InName), LocalPlayer(Player)
{
	if (World) Capture = World->GetSubsystem<UGuLiPerformanceSubsystem>();
	if (Capture && Capture->IsCapturing()) Started = FPlatformTime::Seconds(); else Capture = nullptr;
}
FGuLiPerformanceScope::~FGuLiPerformanceScope()
{ if (Capture) Capture->Record(Name, (FPlatformTime::Seconds() - Started) * 1000.0, LocalPlayer); }
