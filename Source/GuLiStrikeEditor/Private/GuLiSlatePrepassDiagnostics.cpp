// Explicit editor-only layout profiling. No delegates are bound until Start.
#include "CoreMinimal.h"
#include "Debugging/SlateDebugging.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#if WITH_SLATE_DEBUGGING && CPUPROFILERTRACE_ENABLED
namespace GuLiSlatePrepassDiagnostics
{
struct FFrameSample { uint64 Frame = 0; double Milliseconds = 0; int32 Calls = 0; };
struct FTarget
{
	TWeakPtr<SWidget> Widget;
	FString Scope, Kind, Title, World, GameInstance;
	int32 WindowIndex = INDEX_NONE;
	uint64 Started = 0;
	bool bTracing = false;
	TArray<FFrameSample> Samples;
};

class FCapture
{
public:
	TMap<const SWidget*, FTarget> Targets;
	TArray<const SWidget*> TargetOrder;
	FString Output;
	FDelegateHandle BeginHandle, EndHandle, EndPIEHandle;
	uint64 FirstFrame = 0, LastFrame = 0, CallbackCount = 0;
	int32 InvalidPairs = 0;
	bool bStopped = false;

	~FCapture() { Stop(); }

	void AddTarget(const TSharedRef<SWidget>& Widget, FTarget&& Target)
	{
		if (Targets.Contains(&Widget.Get())) return;
		Target.Widget = Widget;
		Target.Samples.Reserve(4096);
		TargetOrder.Add(&Widget.Get());
		Targets.Add(&Widget.Get(), MoveTemp(Target));
	}

	void AddWindow(const TSharedRef<SWindow>& Window)
	{
		if (Targets.Contains(&Window.Get())) return;
		if (Window->IsVisible() && !Window->IsWindowMinimized())
		{
			FTarget Target;
			Target.WindowIndex = TargetOrder.Num();
			Target.Kind = TEXT("window");
			Target.Title = Window->GetTitle().ToString();
			Target.Scope = FString::Printf(TEXT("GuLiSlatePrepass_Window_%d"), Target.WindowIndex);
			AddTarget(StaticCastSharedRef<SWidget>(Window), MoveTemp(Target));
		}
		for (const auto& Child : Window->GetChildWindows()) AddWindow(Child);
	}

	void Start(const FString& InOutput)
	{
		Output = InOutput;
		FirstFrame = GFrameCounter;
		for (const auto& Window : FSlateApplication::Get().GetTopLevelWindows()) AddWindow(Window);
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || World->WorldType != EWorldType::PIE || !World->GetGameViewport() || !World->GetGameInstance()) continue;
			const auto Viewport = World->GetGameViewport()->GetGameViewportWidget();
			if (!Viewport) continue;
			const auto Window = FSlateApplication::Get().FindWidgetWindow(Viewport.ToSharedRef());
			if (!Window) continue;
			AddWindow(Window.ToSharedRef());
			const FTarget* WindowTarget = Targets.Find(Window.Get());
			if (!WindowTarget) continue;
			FTarget Target;
			Target.WindowIndex = WindowTarget->WindowIndex;
			Target.Kind = TEXT("game_viewport");
			Target.World = World->GetPathName();
			Target.GameInstance = World->GetGameInstance()->GetPathName();
			Target.Title = Window->GetTitle().ToString();
			Target.Scope = FString::Printf(TEXT("GuLiSlatePrepass_Viewport_%d"), Context.PIEInstance);
			AddTarget(StaticCastSharedRef<SWidget>(Viewport.ToSharedRef()), MoveTemp(Target));
		}
		BeginHandle = FSlateDebugging::BeginWidgetPrepass.AddRaw(this, &FCapture::Begin);
		EndHandle = FSlateDebugging::EndWidgetPrepass.AddRaw(this, &FCapture::End);
		EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([this](bool) { Stop(); });
		WriteReport(false);
	}

	void Begin(const SWidget* Widget)
	{
		++CallbackCount;
		FTarget* Target = Targets.Find(Widget);
		if (!Target || Target->Widget.Pin().Get() != Widget) return;
		if (Target->Started) { ++InvalidPairs; return; }
		Target->Started = FPlatformTime::Cycles64();
		Target->bTracing = UE_TRACE_CHANNELEXPR_IS_ENABLED(CpuChannel);
		if (Target->bTracing) FCpuProfilerTrace::OutputBeginDynamicEvent(*Target->Scope);
	}

	void End(const SWidget* Widget)
	{
		++CallbackCount;
		FTarget* Target = Targets.Find(Widget);
		if (!Target || Target->Widget.Pin().Get() != Widget) return;
		if (!Target->Started) { ++InvalidPairs; return; }
		const double Ms = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Target->Started);
		if (Target->bTracing) FCpuProfilerTrace::OutputEndEvent();
		Target->bTracing = false;
		Target->Started = 0;
		LastFrame = GFrameCounter;
		if (!Target->Samples.IsEmpty() && Target->Samples.Last().Frame == GFrameCounter)
		{
			Target->Samples.Last().Milliseconds += Ms;
			++Target->Samples.Last().Calls;
		}
		else if (Target->Samples.Num() < 4096)
		{
			Target->Samples.Add({GFrameCounter, Ms, 1});
		}
	}

	void Stop()
	{
		if (bStopped) return;
		bStopped = true;
		FSlateDebugging::BeginWidgetPrepass.Remove(BeginHandle);
		FSlateDebugging::EndWidgetPrepass.Remove(EndHandle);
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		for (const auto& Entry : Targets) if (Entry.Value.Started) ++InvalidPairs;
		WriteReport(true);
	}

	void WriteReport(bool bComplete) const
	{
		auto Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("active"), !bComplete);
		Root->SetBoolField(TEXT("complete"), bComplete);
		Root->SetNumberField(TEXT("invalid_pairs"), InvalidPairs);
		Root->SetNumberField(TEXT("callback_count"), static_cast<double>(CallbackCount));
		Root->SetNumberField(TEXT("first_frame"), static_cast<double>(FirstFrame));
		Root->SetNumberField(TEXT("last_frame"), static_cast<double>(LastFrame));
		Root->SetStringField(TEXT("scope_note"), TEXT("Window and game viewport root Prepass scopes include child probe callback overhead. Nested viewport times must be subtracted from their owning window, not added."));
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const SWidget* Widget : TargetOrder)
		{
			const FTarget& Target = Targets.FindChecked(Widget);
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("scope"), Target.Scope);
			Row->SetStringField(TEXT("kind"), Target.Kind);
			Row->SetStringField(TEXT("title"), Target.Title);
			Row->SetStringField(TEXT("world"), Target.World);
			Row->SetStringField(TEXT("game_instance"), Target.GameInstance);
			Row->SetNumberField(TEXT("window_index"), Target.WindowIndex);
			TArray<TSharedPtr<FJsonValue>> Samples;
			for (const auto& Sample : Target.Samples)
			{
				auto Value = MakeShared<FJsonObject>();
				Value->SetNumberField(TEXT("frame"), static_cast<double>(Sample.Frame));
				Value->SetNumberField(TEXT("ms"), Sample.Milliseconds);
				Value->SetNumberField(TEXT("calls"), Sample.Calls);
				Samples.Add(MakeShared<FJsonValueObject>(Value));
			}
			Row->SetArrayField(TEXT("samples"), Samples);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		Root->SetArrayField(TEXT("targets"), Rows);
		FString Json;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
		if (!FFileHelper::SaveStringToFile(Json, *Output)) UE_LOG(LogTemp, Error, TEXT("Slate Prepass diagnostics could not write %s"), *Output);
	}
};

TUniquePtr<FCapture> Active;
FAutoConsoleCommand StartCommand(TEXT("gs.Slate.Prepass.Start"), TEXT("Opt-in editor PIE window/root Prepass capture. Argument: JSON path under outputs/performance."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Active && !Active->bStopped) { UE_LOG(LogTemp, Warning, TEXT("Slate Prepass capture is already active.")); return; }
		if (!GEditor || !GEditor->IsPlaySessionInProgress() || Args.Num() != 1) return;
		FString Output = FPaths::ConvertRelativePathToFull(Args[0].TrimQuotes());
		FPaths::CollapseRelativeDirectories(Output);
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("outputs/performance"));
		if (!FPaths::IsUnderDirectory(Output, Directory) || FPaths::GetExtension(Output) != TEXT("json")) return;
		Active = MakeUnique<FCapture>();
		Active->Start(Output);
	}));
FAutoConsoleCommand StopCommand(TEXT("gs.Slate.Prepass.Stop"), TEXT("Unbind the Prepass diagnostic callbacks and save the report."),
	FConsoleCommandDelegate::CreateLambda([] { if (Active) Active->Stop(); }));
}
#endif
