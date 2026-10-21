#include "IGuLiFrameProfilerContext.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool IsGuLiSlateWindowCaptureActive();
bool IsGuLiFrameProfilerSlateActive();
bool BeginGuLiFrameProfilerSlate(UWorld* World);
TSharedRef<FJsonObject> EndGuLiFrameProfilerSlate();

namespace
{
class FProjectCaptureContext final : public IGuLiFrameProfilerContext
{
public:
    bool CanBegin(UWorld* World, FString& Reason) const override
    {
        if (Active || IsGuLiSlateWindowCaptureActive() || IsGuLiFrameProfilerSlateActive()) { Reason=TEXT("Native/Slate capture is already active."); return false; }
        if (World)
            if (const auto* Perf=World->GetSubsystem<UGuLiPerformanceSubsystem>(); Perf && Perf->IsPerformanceCaptureBusy())
            { Reason=TEXT("A native performance capture is already active."); return false; }
        return true;
    }
    bool Begin(UWorld* World, const FString& Directory, FString& Reason) override
    {
        if (!CanBegin(World,Reason)) return false;
        Active=true;Target=World;
        if (const auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr) ReadyAtStart=Hybrid->IsWorldReady();
        if (World && World->WorldType==EWorldType::PIE)
        {
            Native=World->GetSubsystem<UGuLiPerformanceSubsystem>();
            if (Native.IsValid()) { Native->BeginCapture(); OwnNative=Native->IsCapturing(); }
        }
        OwnSlate=BeginGuLiFrameProfilerSlate(World);
        return true;
    }
    FString End() override
    {
        auto Root=MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("adapter"),TEXT("GuLiStrikeEditor/v2"));
        const auto* Hybrid=Target.IsValid()?Target->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
        Root->SetBoolField(TEXT("runtime_ready_at_start"),ReadyAtStart);
        Root->SetBoolField(TEXT("runtime_ready_at_end"),Hybrid && Hybrid->IsWorldReady());
        Root->SetNumberField(TEXT("scope_contract_version"),1);
        auto Support=MakeShared<FJsonObject>();
        const bool Instrumented=CPUPROFILERTRACE_ENABLED && Hybrid && OwnNative;
        for (const TCHAR* Name:{TEXT("hybrid_complete"),TEXT("world_update"),TEXT("state_apply"),TEXT("receipts"),TEXT("state_tree"),TEXT("pathfinding"),TEXT("avoidance"),TEXT("snapshot")})
            Support->SetBoolField(Name,Instrumented);
        Root->SetObjectField(TEXT("metric_support"),Support);
        Root->SetBoolField(TEXT("native_available"),OwnNative && Native.IsValid());
        if (OwnNative && Native.IsValid())
        {
            Native->EndCapture();
            TSharedPtr<FJsonObject> Profile;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Native->GetCaptureJson()),Profile) && Profile)
                Root->SetObjectField(TEXT("native_profile"),Profile);
        }
        if (OwnSlate)
        {
            Root->SetObjectField(TEXT("slate_attribution"),EndGuLiFrameProfilerSlate());
        }
        else Root->SetStringField(TEXT("slate_attribution_note"),TEXT("Slate ownership hooks could not be installed; verify Slate debug support and target viewport/layer identity."));
        Root->SetBoolField(TEXT("slate_attribution_started"),OwnSlate);
        Native.Reset();Target.Reset();ReadyAtStart=false; OwnNative=OwnSlate=Active=false;
        FString Text; FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text)); return Text;
    }
    bool IsActive() const { return Active; }
private:
    TWeakObjectPtr<UGuLiPerformanceSubsystem> Native;
    TWeakObjectPtr<UWorld> Target;
    bool Active=false,OwnNative=false,OwnSlate=false,ReadyAtStart=false;
};
TUniquePtr<FProjectCaptureContext> GuLiFrameProfilerContext;
}

void RegisterGuLiFrameProfilerContext()
{
    GuLiFrameProfilerContext=MakeUnique<FProjectCaptureContext>();
    IModularFeatures::Get().RegisterModularFeature(IGuLiFrameProfilerContext::GetFeatureName(),GuLiFrameProfilerContext.Get());
}
void UnregisterGuLiFrameProfilerContext()
{
    if (!GuLiFrameProfilerContext) return;
    if (GuLiFrameProfilerContext->IsActive()) GuLiFrameProfilerContext->End();
    IModularFeatures::Get().UnregisterModularFeature(IGuLiFrameProfilerContext::GetFeatureName(),GuLiFrameProfilerContext.Get());
    GuLiFrameProfilerContext.Reset();
}
