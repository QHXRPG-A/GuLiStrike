#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Engine/World.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool UGuLiPerformanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && UGuLiNetworkSettings::CanCapturePerformance(World);
}
void UGuLiPerformanceSubsystem::BeginCapture()
{
    CaptureDeadline=0;
	if (!UGuLiNetworkSettings::CanCapturePerformance(GetWorld())) return;
	Counters.Reset(); NetworkSamples.Reset(); DroppedNetworkSamples = 0;
    SampleCounters.Reset();DroppedIndividualSamples=0;
    DroppedFrameSamples=0;
	FirstFrame = LastFrame = GFrameCounter; bCapturing = true; CaptureNetworkSample();
}
bool UGuLiPerformanceSubsystem::BeginTimedCapture(float Seconds)
{
    if (bCapturing || bHotspotPending || !FMath::IsFinite(Seconds) || Seconds<1 || Seconds>60) return false;
    BeginCapture();if (!bCapturing) return false;CaptureDeadline=FPlatformTime::Seconds()+Seconds;return true;
}
bool UGuLiPerformanceSubsystem::BeginLongObservation(float Seconds)
{
#if !UE_BUILD_SHIPPING
    if (ObservationDeadline>0 || !FMath::IsFinite(Seconds) || Seconds<1 || Seconds>3600) return false;
    ObservationSamples.Reset();DroppedObservationSamples=0;ObservationRequested=Seconds;
    ObservationStarted=FPlatformTime::Seconds();ObservationDeadline=ObservationStarted+Seconds;
    CaptureNetworkSample(true);return true;
#else
    return false;
#endif
}
FString UGuLiPerformanceSubsystem::GetLongObservationJson() const
{
    auto Root=MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("complete"),ObservationStarted>0 && ObservationDeadline==0);
    Root->SetNumberField(TEXT("requested_seconds"),ObservationRequested);
    Root->SetNumberField(TEXT("started_seconds"),ObservationStarted);
    Root->SetNumberField(TEXT("sampled_seconds"),FMath::Max(0.,LastObservationSample-ObservationStarted));
    Root->SetNumberField(TEXT("dropped_samples"),DroppedObservationSamples);
    Root->SetArrayField(TEXT("network_samples"),ObservationSamples);
    AppendContinuousHotspot(Root);
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
void UGuLiPerformanceSubsystem::EndCapture()
{
	if (bCapturing) CaptureNetworkSample();
	LastFrame=GFrameCounter; bCapturing=false;
    CaptureDeadline=0;
}
void UGuLiPerformanceSubsystem::Tick(float DeltaTime)
{
    if (ObservationDeadline>0 && FPlatformTime::Seconds()>=ObservationDeadline)
    {CaptureNetworkSample(true);ObservationDeadline=0;}
    if (bCapturing && CaptureDeadline>0 && FPlatformTime::Seconds()>=CaptureDeadline) EndCapture();
    if (bCapturing && FPlatformTime::Seconds() - LastNetworkSample >= 1.0) CaptureNetworkSample();
    if (ObservationDeadline>0 && FPlatformTime::Seconds()-LastObservationSample>=1.0) CaptureNetworkSample(true);
    TickHotspot(DeltaTime);
    TickContinuousHotspot();
}
TStatId UGuLiPerformanceSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiPerformanceCapture, STATGROUP_Tickables); }
void UGuLiPerformanceSubsystem::Record(const FName Name, const double Value, const UObject* LocalPlayer)
{
	if (!bCapturing || !FMath::IsFinite(Value)) return;
	const FString Key = LocalPlayer ? LocalPlayer->GetPathName() + TEXT("/") + Name.ToString() : Name.ToString();
	auto& C = Counters.FindOrAdd(Key);
	if (C.Calls && C.Frame != GFrameCounter)
	{
        // Retain the whole acceptance window. Overflow invalidates the round;
        // it must never turn a prefix into an apparently complete distribution.
        if (C.FrameSamples.Num()<262144)
        { C.FrameSamples.Add(C.FrameTotal); C.FrameTimes.Add(C.FrameQpc); C.FrameIds.Add(C.Frame); }
        else ++DroppedFrameSamples;
		C.FrameTotal = 0;
	}
	if (!C.Calls || C.Frame != GFrameCounter) C.FrameQpc = FPlatformTime::Cycles64();
	C.Frame = GFrameCounter; C.FrameTotal += Value; C.Total += Value; ++C.Calls;
}
void UGuLiPerformanceSubsystem::RecordSample(FName Name,double Value)
{
    check(IsInGameThread());if (!bCapturing || !FMath::IsFinite(Value)) return;
    auto& C=SampleCounters.FindOrAdd(Name);
    if (C.Values.Num()>=1048576) {++DroppedIndividualSamples;return;}
    C.Values.Add(Value);C.Total+=Value;
}
FString UGuLiPerformanceSubsystem::GetCaptureJson() const
{
	auto Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("world"), GetWorld()->GetPathName());
    // Readback is requested outside the native timing window. This census uses
    // simulation authority and life, never render instances or faction alone.
    if (const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
        Root->SetObjectField(TEXT("simulation_census"),Hybrid->GetSimulationCensus());
    if (const auto* H=GetWorld()->GetSubsystem<UGuLiHotspotOptions>())
    {
        Root->SetNumberField(TEXT("hotspot_mask"),H->GetMask());
        Root->SetNumberField(TEXT("gameplay_workers"),H->GetGameplayWorkers());
        auto Counts=MakeShared<FJsonObject>();
        Counts->SetNumberField(TEXT("contract_rebuilds"),double(H->ContractRebuilds));
        Counts->SetNumberField(TEXT("binding_rebuilds"),double(H->BindingRebuilds));
        Counts->SetNumberField(TEXT("binding_hits"),double(H->BindingHits));
        Counts->SetNumberField(TEXT("public_captures"),double(H->PublicCaptures));
        Counts->SetNumberField(TEXT("public_absent"),double(H->PublicAbsent));
        Counts->SetNumberField(TEXT("body_decodes"),double(H->BodyDecodes));
        Counts->SetNumberField(TEXT("body_decode_hits"),double(H->BodyDecodeHits));
        Counts->SetNumberField(TEXT("body_callbacks"),double(H->BodyCallbacks));
        Counts->SetNumberField(TEXT("body_applications"),double(H->BodyApplications));
        Root->SetObjectField(TEXT("hotspot_cumulative"),Counts);
    }
	const uint64 Frames = FMath::Max(uint64(1), (bCapturing ? GFrameCounter : LastFrame) - FirstFrame + 1);
	Root->SetNumberField(TEXT("engine_frames"), double(Frames));
	Root->SetBoolField(TEXT("capturing"), bCapturing);
    AppendHotspotCapture(Root);
    Root->SetNumberField(TEXT("qpc_frequency"),1.0/FPlatformTime::GetSecondsPerCycle64());
    Root->SetNumberField(TEXT("frame_samples_dropped"),double(DroppedFrameSamples));
    Root->SetBoolField(TEXT("capture_valid"),IsCaptureValid());
	auto Values = MakeShared<FJsonObject>();
	for (const auto& Pair : Counters)
	{
		const auto& C = Pair.Value; auto V = MakeShared<FJsonObject>();
		TArray<double> Samples = C.FrameSamples; Samples.Add(C.FrameTotal); Samples.Sort();
		V->SetNumberField(TEXT("total"), C.Total); V->SetNumberField(TEXT("calls"), double(C.Calls));
		V->SetNumberField(TEXT("mean_per_engine_frame"), C.Total / Frames);
		V->SetNumberField(TEXT("p95_active_frame"), Samples[FMath::Clamp(FMath::CeilToInt(Samples.Num() * .95) - 1, 0, Samples.Num() - 1)]);
        const auto Percentile=[&](double P) { return Samples[FMath::Clamp(FMath::CeilToInt(Samples.Num()*P)-1,0,Samples.Num()-1)]; };
        V->SetNumberField(TEXT("p50_active_frame"),Percentile(.5)); V->SetNumberField(TEXT("p99_active_frame"),Percentile(.99));
        V->SetNumberField(TEXT("max_active_frame"),Samples.Last());
        TArray<TSharedPtr<FJsonValue>> Timed;
        for (int32 I=0;I<=C.FrameSamples.Num();++I)
        {
            const bool Last=I==C.FrameSamples.Num(); auto Row=MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("qpc"),double(Last?C.FrameQpc:C.FrameTimes[I]));
            Row->SetNumberField(TEXT("frame"),double(Last?C.Frame:C.FrameIds[I]));
            Row->SetNumberField(TEXT("value"),Last?C.FrameTotal:C.FrameSamples[I]);
            Timed.Add(MakeShared<FJsonValueObject>(Row));
        }
        V->SetArrayField(TEXT("timed_frames"),Timed);
        V->SetStringField(TEXT("percentile_method"),TEXT("nearest rank over every retained active frame in the full window"));
		V->SetNumberField(TEXT("stored_active_frames"), Samples.Num());
		Values->SetObjectField(Pair.Key, V);
	}
	Root->SetObjectField(TEXT("counters"), Values);
    auto Individual=MakeShared<FJsonObject>();
    for (const auto& [Name,C]:SampleCounters)
    {
        if (C.Values.IsEmpty()) continue;auto Ordered=C.Values;Ordered.Sort();auto J=MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("samples"),Ordered.Num());J->SetNumberField(TEXT("mean"),C.Total/Ordered.Num());
        J->SetNumberField(TEXT("p95"),Ordered[FMath::Clamp(FMath::CeilToInt(Ordered.Num()*.95)-1,0,Ordered.Num()-1)]);
        J->SetNumberField(TEXT("peak"),Ordered.Last());Individual->SetObjectField(Name.ToString(),J);
    }
    Root->SetObjectField(TEXT("sample_metrics"),Individual);Root->SetNumberField(TEXT("individual_samples_dropped"),double(DroppedIndividualSamples));
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
