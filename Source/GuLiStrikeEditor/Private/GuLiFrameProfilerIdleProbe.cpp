#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "Modules/ModuleManager.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Windows/WindowsHWrapper.h"
#include "Widgets/Docking/SDockTab.h"

// This probe belongs to the project editor module so it also works with the
// profiler unloaded. No Trace, Slate observer, screenshot or per-frame I/O.
namespace GuLiIdleProbe
{
TWeakObjectPtr<UWorld> World;
TSharedPtr<FJsonObject> Result;
TArray<double> Intervals;
FDelegateHandle FrameHandle;
double Started=0,Previous=0,Duration=5,CpuStart=0;
uint64 Foreground=0,Background=0;
FString Label;

double ProcessCpuSeconds()
{
    FILETIME Created,Exited,Kernel,User;
    if (!::GetProcessTimes(::GetCurrentProcess(),&Created,&Exited,&Kernel,&User)) return -1;
    ULARGE_INTEGER K,U;
    K.LowPart=Kernel.dwLowDateTime;K.HighPart=Kernel.dwHighDateTime;
    U.LowPart=User.dwLowDateTime;U.HighPart=User.dwHighDateTime;
    return double(K.QuadPart+U.QuadPart)*1.e-7;
}
FString Channels()
{
    TStringBuilder<2048> Value;FTraceAuxiliary::GetActiveChannelsString(Value);return FString(Value.ToView());
}
TSharedRef<FJsonObject> Census()
{
    if (const auto* Hybrid=World.IsValid()?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr)
        return Hybrid->GetSimulationCensus();
    return MakeShared<FJsonObject>();
}
void Cancel()
{
    FCoreDelegates::OnBeginFrame.Remove(FrameHandle);FrameHandle.Reset();
    Result.Reset();Intervals.Empty();World.Reset();Started=Previous=0;
}
void Frame()
{
    const double Now=FPlatformTime::Seconds();
    if (Started==0) {Started=Previous=Now;CpuStart=ProcessCpuSeconds();return;}
    Intervals.Add((Now-Previous)*1000);Previous=Now;
    if (FApp::HasFocus()) ++Foreground;else ++Background;
    if (Now-Started<Duration) return;
    const double CpuEnd=ProcessCpuSeconds();
    FCoreDelegates::OnBeginFrame.Remove(FrameHandle);FrameHandle.Reset();
    const double Elapsed=Now-Started;
    Result->SetNumberField(TEXT("elapsed_seconds"),Elapsed);
    Result->SetNumberField(TEXT("frames"),Intervals.Num());
    Result->SetNumberField(TEXT("foreground_frames"),double(Foreground));
    Result->SetNumberField(TEXT("background_frames"),double(Background));
    Result->SetNumberField(TEXT("frame_mean_ms"),Elapsed*1000/Intervals.Num());
    Intervals.Sort();
    Result->SetNumberField(TEXT("frame_p95_ms"),Intervals[FMath::Clamp(FMath::CeilToInt(Intervals.Num()*.95)-1,0,Intervals.Num()-1)]);
    Result->SetNumberField(TEXT("frame_peak_ms"),Intervals.Last());
    if (CpuStart>=0 && CpuEnd>=CpuStart)
    {
        Result->SetNumberField(TEXT("process_cpu_ms_per_frame"),(CpuEnd-CpuStart)*1000/Intervals.Num());
        Result->SetNumberField(TEXT("process_cpu_ms_per_second"),(CpuEnd-CpuStart)*1000/Elapsed);
    }
    Result->SetObjectField(TEXT("census_after"),Census());
    Result->SetStringField(TEXT("trace_channels_after"),Channels());
    Result->SetBoolField(TEXT("trace_connected_after"),FTraceAuxiliary::IsConnected());
    Result->SetBoolField(TEXT("world_alive_at_end"),World.IsValid());
    FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../PerformanceCaptures")/FApp::GetProjectName()/TEXT("idle-overhead"));
    FPaths::CollapseRelativeDirectories(Root);IFileManager::Get().MakeDirectory(*Root,true);
    const FString Path=Root/(FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+Label+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)+TEXT(".json"));
    FString Text;FJsonSerializer::Serialize(Result.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    const bool Saved=FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(Text,*(Root/TEXT("latest.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("[GULI_IDLE_PROBE] saved=%d path=%s frames=%d foreground=%llu background=%llu mean_ms=%.3f"),Saved,*Path,Intervals.Num(),Foreground,Background,Elapsed*1000/Intervals.Num());
    Cancel();
}
FAutoConsoleCommand Command(TEXT("gs.FrameProfiler.MeasureIdle"),TEXT("MeasureIdle <seconds 1..30> <label>: native frame/process CPU counters only; does not record Trace or capture RenderDoc."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (FrameHandle.IsValid() || Args.Num()!=2 || FTraceAuxiliary::IsConnected())
        {UE_LOG(LogTemp,Warning,TEXT("[GULI_IDLE_PROBE] Busy, active Trace connection, or expected <seconds> <label>."));return;}
        World=GEditor?GEditor->PlayWorld:nullptr;
        if (const auto* Perf=World.IsValid()?World->GetSubsystem<UGuLiPerformanceSubsystem>():nullptr;Perf && Perf->IsPerformanceCaptureBusy()) return;
        if (auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(FName(TEXT("GuLiFrameProfiler.Report")))) Tab->RequestCloseTab();
        Duration=FMath::Clamp(FCString::Atod(*Args[0]),1.,30.);Label=FPaths::MakeValidFileName(Args[1]);
        Result=MakeShared<FJsonObject>();Intervals.Reset();Intervals.Reserve(32768);
        Foreground=Background=0;Started=Previous=0;
        Result->SetStringField(TEXT("label"),Label);
        Result->SetStringField(TEXT("map"),World.IsValid()?World->GetPathName():TEXT("editor_only"));
        Result->SetStringField(TEXT("trace_channels_before"),Channels());
        Result->SetBoolField(TEXT("trace_connected_before"),FTraceAuxiliary::IsConnected());
        Result->SetBoolField(TEXT("profiler_module_loaded"),FModuleManager::Get().IsModuleLoaded(TEXT("GuLiFrameProfiler")));
        Result->SetBoolField(TEXT("renderdoc_dll_attached"),::GetModuleHandleW(L"renderdoc.dll")!=nullptr);
        Result->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());
        if (auto* Client=World.IsValid()?World->GetGameViewport():nullptr;Client && Client->Viewport)
        {
            const FIntPoint Size=Client->Viewport->GetSizeXY();
            Result->SetNumberField(TEXT("viewport_width"),Size.X);Result->SetNumberField(TEXT("viewport_height"),Size.Y);
        }
        Result->SetObjectField(TEXT("census_before"),Census());
        FrameHandle=FCoreDelegates::OnBeginFrame.AddStatic(&Frame);
        UE_LOG(LogTemp,Display,TEXT("[GULI_IDLE_PROBE] armed label=%s seconds=%.1f"),*Label,Duration);
    }));
}

void StopGuLiFrameProfilerIdleProbe() {GuLiIdleProbe::Cancel();}
