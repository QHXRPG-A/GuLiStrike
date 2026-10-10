#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool UGuLiPerformanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}
void UGuLiPerformanceSubsystem::BeginCapture() { Counters.Reset(); FirstFrame = LastFrame = GFrameCounter; bCapturing = true; }
void UGuLiPerformanceSubsystem::EndCapture() { LastFrame=GFrameCounter; bCapturing=false; }
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
	FString Json; const auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Root, Writer); return Json;
}
FGuLiPerformanceScope::FGuLiPerformanceScope(UWorld* World, FName InName, const UObject* Player) : Name(InName), LocalPlayer(Player)
{
	if (World) Capture = World->GetSubsystem<UGuLiPerformanceSubsystem>();
	if (Capture && Capture->IsCapturing()) Started = FPlatformTime::Seconds(); else Capture = nullptr;
}
FGuLiPerformanceScope::~FGuLiPerformanceScope()
{ if (Capture) Capture->Record(Name, (FPlatformTime::Seconds() - Started) * 1000.0, LocalPlayer); }
