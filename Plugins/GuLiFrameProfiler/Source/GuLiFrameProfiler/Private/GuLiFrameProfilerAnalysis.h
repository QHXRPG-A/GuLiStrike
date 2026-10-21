#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"

class FJsonObject;
namespace TraceServices { class IAnalysisSession; }

namespace GuLiFrameProfiler
{
struct FAnalysisResult
{
    TSharedPtr<FJsonObject> Json;
    FString Markdown;
    bool Success = false;
};

FAnalysisResult Analyze(const FString& TracePath, const FString& CaptureId,
    const TSharedRef<FJsonObject>& Context, const TSharedPtr<const TraceServices::IAnalysisSession>& Session,
    const TSharedRef<FThreadSafeBool>& Cancel);

bool SaveJson(const FString& Path, const TSharedRef<FJsonObject>& Object);
bool SaveText(const FString& Path, const FString& Text);
}
