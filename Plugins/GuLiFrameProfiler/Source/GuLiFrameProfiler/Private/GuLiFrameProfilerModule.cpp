#include "GuLiFrameProfilerAnalysis.h"
#include "IGuLiFrameProfilerContext.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "IRenderCaptureProvider.h"
#include "Layout/WidgetPath.h"
#include "LevelEditor.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "RenderCommandFence.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UnrealClient.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "SceneView.h"

#define LOCTEXT_NAMESPACE "GuLiFrameProfiler"
DEFINE_LOG_CATEGORY_STATIC(LogGuLiFrameProfiler,Log,All);

namespace
{
const FName ReportTab(TEXT("GuLiFrameProfiler.Report"));
constexpr double CpuSeconds=5.0;
TAutoConsoleVariable<int32> KeepDetailedGPUEvents(
    TEXT("gs.FrameProfiler.KeepDetailedGPUEvents"),0,
    TEXT("Keep RenderDoc's startup-wide detailed draw/material/RDG markers. Default 0: enable detailed markers only for a GPU capture; does not change rendering quality."));
bool HasRenderDoc()
{
    return FModuleManager::Get().IsModuleLoaded(TEXT("RenderDocPlugin")) && IRenderCaptureProvider::IsAvailable();
}
void CaptureBookmark(bool Begin,const FString& Id)
{
    // Re-emit the specification for every file. An earlier cancelled recording
    // must not leave this recording dependent on a cached format declaration.
    static uint8 BeginPoint=0,EndPoint=0;
    const void* Point=Begin?&BeginPoint:&EndPoint;
    FMiscTrace::OutputBookmarkSpec(Point,__FILE__,__LINE__,Begin?TEXT("GuLiFrameProfiler_Begin %s"):TEXT("GuLiFrameProfiler_End %s"));
    FMiscTrace::OutputBookmark(uint32(0),Point,*Id);
}
FString OutputRoot()
{
    FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../PerformanceCaptures")/FApp::GetProjectName());
    FPaths::CollapseRelativeDirectories(Path); return Path;
}
class FProfilerCommands final : public TCommands<FProfilerCommands>
{
public:
    FProfilerCommands():TCommands(TEXT("GuLiFrameProfiler"),LOCTEXT("CommandContext","GuLi Frame Profiler"),NAME_None,FAppStyle::GetAppStyleSetName()) {}
    void RegisterCommands() override
    {
        UI_COMMAND(Capture,"Capture and Analyze","Analyze five seconds of CPU trace. Also capture the viewport when started with -AttachRenderDoc.",EUserInterfaceActionType::Button,FInputChord(EModifierKey::Alt,EKeys::Slash));
        UI_COMMAND(OpenReport,"Open Last CPU Report","Open the most recent capture report.",EUserInterfaceActionType::Button,FInputChord());
    }
    TSharedPtr<FUICommandInfo> Capture,OpenReport;
};

bool AcceptInput(FSlateApplication& App)
{
    if (App.GetActiveModalWindow()) return false;
    if (const auto Focus=App.GetKeyboardFocusedWidget())
    {
        FWidgetPath Path; App.GeneratePathToWidgetUnchecked(Focus.ToSharedRef(),Path);
        for (int32 I=0;I<Path.Widgets.Num();++I)
        {
            const FString Type=Path.Widgets[I].Widget->GetTypeAsString();
            if (Type.Contains(TEXT("EditableText")) || Type.Contains(TEXT("SearchBox"))) return false;
        }
    }
    return true;
}
class FProfilerInput final : public IInputProcessor
{
public:
    explicit FProfilerInput(TSharedRef<FUICommandList> InCommands):Commands(InCommands) {}
    void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
    bool HandleKeyDownEvent(FSlateApplication& App,const FKeyEvent& Event) override
    {
        return !Event.IsRepeat() && AcceptInput(App) && Commands->ProcessCommandBindings(Event);
    }
    const TCHAR* GetDebugName() const override { return TEXT("GuLiFrameProfiler"); }
private:
    TSharedRef<FUICommandList> Commands;
};

struct FJob
{
    FString Id,Directory,TracePath,RdcPath,OriginalChannels,Error;
    TWeakObjectPtr<UWorld> World;
    FViewport* Viewport=nullptr;
    bool PIE=false,OwnTrace=false,OwnAdapter=false,WindowComplete=false,Interrupted=false,StopWarning=false,CaptureRenderDoc=true;
    FGuid TraceGuid;
    IGuLiFrameProfilerContext* Adapter=nullptr;
    TSharedRef<FJsonObject> Context=MakeShared<FJsonObject>();
    double Started=0,Deadline=0;
    uint64 ForegroundFrames=0,BackgroundFrames=0,StopAfterFrame=0;
    FRenderCommandFence GpuFence;
    struct FCVarState { IConsoleVariable* Variable; int32 Value; EConsoleVariableFlags SetBy; FString Name; };
    TArray<FCVarState> RenderDocCVars;
    TSharedRef<FThreadSafeBool> Cancel=MakeShared<FThreadSafeBool>(false);
    TFuture<GuLiFrameProfiler::FAnalysisResult> Analysis;
};
}

class FGuLiFrameProfilerModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        if (IsRunningCommandlet() || FApp::IsUnattended()) return;
        if (GEditor && FSlateApplication::IsInitialized()) InitializeUI();
        else PostEngine=FCoreDelegates::OnPostEngineInit.AddRaw(this,&FGuLiFrameProfilerModule::InitializeUI);
    }
    void ShutdownModule() override
    {
        FCoreDelegates::OnPostEngineInit.Remove(PostEngine);
        if (!Initialized) return;
        if (Job)
        {
            *Job->Cancel=true;
            if (State==EState::Recording || State==EState::Armed) FinishCPU(false,TEXT("Editor/plugin shutdown."));
            const double StopDeadline=FPlatformTime::Seconds()+5;
            while (Job->OwnTrace && !StopOwnedTrace() && FPlatformTime::Seconds()<StopDeadline)
                FPlatformProcess::Sleep(.005f);
            if (Job->OwnTrace) UE_LOG(LogGuLiFrameProfiler,Error,TEXT("Trace stop did not settle before module shutdown: %s"),*Job->TracePath);
            if (Job->Analysis.IsValid()) Job->Analysis.Wait();
            if (State==EState::WaitingGpu) Job->GpuFence.Wait();
            RestoreRenderDoc();
            Job->Context->SetStringField(TEXT("shutdown_note"),TEXT("Editor closed before report delivery."));
            GuLiFrameProfiler::SaveJson(Job->Directory/TEXT("context.json"),Job->Context);
        }
        RemoveCaptureDelegates();
        if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        for (IConsoleObject* C:ConsoleCommands) IConsoleManager::Get().UnregisterConsoleObject(C);
        ConsoleCommands.Empty();
        UToolMenus::UnRegisterStartupCallback(this); UToolMenus::UnregisterOwner(this);
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Input);
        Input.Reset();
        if (auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(ReportTab)) Tab->RequestCloseTab();
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ReportTab);
        Commands.Reset(); FProfilerCommands::Unregister(); Job.Reset();
    }
private:
    enum class EState {Idle,Armed,Recording,ClosingTrace,WaitingGpu,Analyzing};
    EState State=EState::Idle;
    bool Initialized=false;
    TSharedPtr<FUICommandList> Commands;
    TSharedPtr<FProfilerInput> Input;
    TUniquePtr<FJob> Job;
    FDelegateHandle PostEngine,BeginFrame,EndPIE;
    FTSTicker::FDelegateHandle TickHandle;
    TArray<IConsoleObject*> ConsoleCommands;
    FString Status=TEXT("待命：Alt+/ 采 CPU 5 秒并分析"),Report=TEXT("按 Alt+/ 开始采样。需要 GPU 截帧时，以 -AttachRenderDoc 启动编辑器。"),LastDirectory;

    FString StateName() const
    {
        switch(State)
        {
        case EState::Armed:return TEXT("armed"); case EState::Recording:return TEXT("recording_cpu");
        case EState::ClosingTrace:return TEXT("closing_trace"); case EState::WaitingGpu:return TEXT("capturing_renderdoc");
        case EState::Analyzing:return TEXT("analyzing"); default:return TEXT("idle");
        }
    }
    void SetStatus(const FString& Text)
    {
        Status=Text; UE_LOG(LogGuLiFrameProfiler,Display,TEXT("%s"),*Text);
        auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("state"),StateName());
        J->SetStringField(TEXT("message"),Text); J->SetStringField(TEXT("directory"),Job?Job->Directory:LastDirectory);
        IFileManager::Get().MakeDirectory(*OutputRoot(),true);
        GuLiFrameProfiler::SaveJson(OutputRoot()/TEXT("latest-status.json"),J);
    }
    void InitializeUI()
    {
        if (Initialized || !FSlateApplication::IsInitialized() || !GEditor) return;
        Initialized=true; FProfilerCommands::Register(); Commands=MakeShared<FUICommandList>();
        Commands->MapAction(FProfilerCommands::Get().Capture,FExecuteAction::CreateRaw(this,&FGuLiFrameProfilerModule::Trigger),FCanExecuteAction::CreateLambda([]{return !FSlateApplication::Get().GetActiveModalWindow().IsValid();}));
        Commands->MapAction(FProfilerCommands::Get().OpenReport,FExecuteAction::CreateRaw(this,&FGuLiFrameProfilerModule::OpenReport));
        Input=MakeShared<FProfilerInput>(Commands.ToSharedRef()); FSlateApplication::Get().RegisterInputPreProcessor(Input,0);
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ReportTab,FOnSpawnTab::CreateRaw(this,&FGuLiFrameProfilerModule::SpawnReport))
            .SetDisplayName(LOCTEXT("ReportTitle","GuLi Frame Profiler")).SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FGuLiFrameProfilerModule::RegisterMenus));
        auto& Console=IConsoleManager::Get();
        ConsoleCommands.Add(Console.RegisterConsoleCommand(TEXT("gs.FrameProfiler.Capture"),TEXT("Analyze CPU for 5 seconds; also capture the viewport if RenderDoc was attached at startup."),FConsoleCommandDelegate::CreateRaw(this,&FGuLiFrameProfilerModule::Trigger)));
        ConsoleCommands.Add(Console.RegisterConsoleCommand(TEXT("gs.FrameProfiler.CaptureCPU"),TEXT("Capture and analyze CPU for 5 seconds without a RenderDoc capture."),FConsoleCommandDelegate::CreateRaw(this,&FGuLiFrameProfilerModule::TriggerCPU)));
        ConsoleCommands.Add(Console.RegisterConsoleCommand(TEXT("gs.FrameProfiler.Cancel"),TEXT("End this profiler's capture early; preserve partial evidence."),FConsoleCommandDelegate::CreateRaw(this,&FGuLiFrameProfilerModule::CancelCapture)));
        ConsoleCommands.Add(Console.RegisterConsoleCommand(TEXT("gs.FrameProfiler.Status"),TEXT("Print current capture state and directory."),FConsoleCommandDelegate::CreateLambda([this]{UE_LOG(LogGuLiFrameProfiler,Display,TEXT("state=%s directory=%s message=%s"),*StateName(),Job?*Job->Directory:*LastDirectory,*Status);} )));
        // D3D12 enables expensive diagnostic markers for the entire session
        // when RenderDoc attaches. Keep attachment/capture available, but pay
        // for detailed material/pass names only outside the CPU timing window.
        if (!KeepDetailedGPUEvents.GetValueOnGameThread() && GDynamicRHI &&
            HasRenderDoc())
            GDynamicRHI->EnableIdealGPUCaptureOptions(false);
        SetStatus(HasRenderDoc()?TEXT("GuLiFrameProfiler ready — Alt+/；CPU 5 秒，随后截帧并自动分析。")
            :TEXT("GuLiFrameProfiler ready — Alt+/ 分析 CPU 5 秒；GPU 截帧需以 -AttachRenderDoc 启动。"));
    }
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        auto& Section=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"))->AddSection(TEXT("GuLiFrameProfiler"),LOCTEXT("MenuTitle","GuLi Frame Profiler"));
        Section.AddMenuEntryWithCommandList(FProfilerCommands::Get().Capture,Commands);
        Section.AddMenuEntryWithCommandList(FProfilerCommands::Get().OpenReport,Commands);
    }
    void OpenReport() { FGlobalTabmanager::Get()->TryInvokeTab(ReportTab); }
    TSharedRef<SDockTab> SpawnReport(const FSpawnTabArgs&)
    {
        return SNew(SDockTab).TabRole(ETabRole::NomadTab)
        [SNew(SBorder).Padding(8)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SButton).Text(LOCTEXT("CaptureButton","采样 + 截帧")).OnClicked_Lambda([this]{Trigger();return FReply::Handled();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SButton).Text(LOCTEXT("FolderButton","打开目录")).OnClicked_Lambda([this]{FPlatformProcess::ExploreFolder(*(LastDirectory.IsEmpty()?OutputRoot():LastDirectory));return FReply::Handled();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SButton).Text(LOCTEXT("RenderDocButton","RenderDoc")).OnClicked_Lambda([this]{OpenArtifact(true);return FReply::Handled();})]
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("InsightsButton","Unreal Insights")).OnClicked_Lambda([this]{OpenArtifact(false);return FReply::Handled();})]
                ]
                +SVerticalBox::Slot().FillHeight(1)
                [SNew(SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(false).Font(FAppStyle::GetFontStyle(TEXT("Mono")))
                    .Text_Lambda([this]{return FText::FromString(Report);})]
            ]
        ];
    }
    void OpenArtifact(bool RenderDoc)
    {
        if (LastDirectory.IsEmpty()) return;
        FString Exe,Artifact=LastDirectory/(RenderDoc?TEXT("frame.rdc"):TEXT("cpu.utrace"));
        if (!IFileManager::Get().FileExists(*Artifact)) {SetStatus(TEXT("对应采集文件不存在；请查看报告中的失败原因。"));return;}
        if (RenderDoc)
        {
            FString Folder; GConfig->GetString(TEXT("/Script/RenderDocPlugin.RenderDocPluginSettings"),TEXT("renderdoc.BinaryPath"),Folder,GEngineIni);
            Exe=Folder/TEXT("qrenderdoc.exe");
        }
        else Exe=FPaths::ConvertRelativePathToFull(FPaths::EngineDir()/TEXT("Binaries/Win64/UnrealInsights.exe"));
        if (!IFileManager::Get().FileExists(*Exe)) {SetStatus(TEXT("查看器不存在；采集文件已保留，可从目录手动打开。"));return;}
        const FString Args=RenderDoc?FString::Printf(TEXT("\"%s\""),*Artifact):FString::Printf(TEXT("-OpenTraceFile=\"%s\""),*Artifact);
        auto Handle=FPlatformProcess::CreateProc(*Exe,*Args,true,false,false,nullptr,0,nullptr,nullptr);
        if (Handle.IsValid()) FPlatformProcess::CloseProc(Handle); else SetStatus(TEXT("无法启动查看器；采集文件已保留。"));
    }
    FViewport* ResolveViewport(UWorld*& World,bool& PIE) const
    {
        World=nullptr; PIE=false;
        if (!GEditor || !GEngine) return nullptr;
        FViewport* Active=GEditor->GetActiveViewport();
        UWorld* Candidate=nullptr; int32 RenderedWorlds=0;
        for (const auto& Context:GEngine->GetWorldContexts())
        {
            auto* W=Context.World();
            if (W && W->WorldType==EWorldType::PIE && W->GetGameViewport() && W->GetGameViewport()->Viewport)
            {
                auto* VP=W->GetGameViewport()->Viewport; ++RenderedWorlds; Candidate=W;
                if (VP==Active || VP->HasFocus()) {World=W;PIE=true;return VP;}
            }
        }
        if (RenderedWorlds==1) {World=Candidate;PIE=true;return Candidate->GetGameViewport()->Viewport;}
        if (RenderedWorlds>1) return nullptr;
        World=GEditor->GetEditorWorldContext().World(); return Active;
    }
    bool StillOwnAdapter() const
    {
        if (!Job || !Job->OwnAdapter || !Job->Adapter) return false;
        const auto Providers=IModularFeatures::Get().GetModularFeatureImplementations<IGuLiFrameProfilerContext>(IGuLiFrameProfilerContext::GetFeatureName());
        return Providers.Contains(Job->Adapter);
    }
    void Trigger() { BeginCapture(HasRenderDoc()); }
    void TriggerCPU() { BeginCapture(false); }
    void BeginCapture(bool CaptureRenderDoc)
    {
        if (State!=EState::Idle) {UE_LOG(LogGuLiFrameProfiler,Display,TEXT("Capture already active: %s"),*StateName());return;}
        if (FTraceAuxiliary::IsConnected() || FTraceAuxiliary::IsPaused()) {SetStatus(TEXT("已有 Trace 运行或暂停中；请结束原采样后再使用 Alt+/。"));return;}
        UWorld* World=nullptr; bool PIE=false; FViewport* Viewport=ResolveViewport(World,PIE);
        if (!Viewport || !World) {SetStatus(TEXT("没有唯一可捕获视口；请聚焦目标游戏或关卡视口。"));return;}
        IGuLiFrameProfilerContext* Adapter=nullptr;
        auto Providers=IModularFeatures::Get().GetModularFeatureImplementations<IGuLiFrameProfilerContext>(IGuLiFrameProfilerContext::GetFeatureName());
        if (Providers.Num()>1) {SetStatus(TEXT("存在多个项目采样适配器，无法确定采样归属。"));return;}
        if (!Providers.IsEmpty()) Adapter=Providers[0];
        FString Reason;
        if (Adapter && !Adapter->CanBegin(World,Reason)) {SetStatus(Reason);return;}
        Job=MakeUnique<FJob>(); Job->Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Job->CaptureRenderDoc=CaptureRenderDoc;
        Job->Directory=OutputRoot()/(FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-"))+Job->Id.Left(8));
        Job->TracePath=Job->Directory/TEXT("cpu.utrace"); Job->RdcPath=Job->Directory/TEXT("frame.rdc");
        Job->World=World; Job->Viewport=Viewport; Job->PIE=PIE; Job->Adapter=Adapter;
        if (!IFileManager::Get().MakeDirectory(*Job->Directory,true)) {Job.Reset();SetStatus(TEXT("无法创建性能采集目录。"));return;}
        auto C=Job->Context; C->SetStringField(TEXT("capture_id"),Job->Id); C->SetStringField(TEXT("directory"),Job->Directory);
        C->SetStringField(TEXT("mode"),PIE?TEXT("PIE rendering client process"):TEXT("Editor viewport"));
        C->SetStringField(TEXT("map"),UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
        C->SetNumberField(TEXT("pid"),FPlatformProcess::GetCurrentProcessId()); C->SetStringField(TEXT("engine"),FEngineVersion::Current().ToString());
        C->SetStringField(TEXT("plugin_module"),FModuleManager::Get().GetModuleFilename(TEXT("GuLiFrameProfiler")));
        C->SetStringField(TEXT("plugin_build_stamp"),UTF8_TO_TCHAR(__DATE__ " " __TIME__));
        C->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601()); C->SetNumberField(TEXT("requested_cpu_seconds"),CpuSeconds);
        const FIntPoint Size=Viewport->GetSizeXY(); C->SetNumberField(TEXT("viewport_width"),Size.X);C->SetNumberField(TEXT("viewport_height"),Size.Y);
        C->SetBoolField(TEXT("cpu_window_complete"),false); C->SetStringField(TEXT("renderdoc_status"),TEXT("pending"));
        C->SetBoolField(TEXT("renderdoc_requested"),CaptureRenderDoc);
        C->SetBoolField(TEXT("renderdoc_attached_at_start"),HasRenderDoc());
        FString Manifest;TSharedPtr<FJsonObject> Modules;
        if (FFileHelper::LoadFileToString(Manifest,*(FPaths::ProjectDir()/TEXT("Binaries/Win64/UnrealEditor.modules"))) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Manifest),Modules) && Modules)
            C->SetStringField(TEXT("build_id"),Modules->GetStringField(TEXT("BuildId")));
        else C->SetStringField(TEXT("build_id"),TEXT("unavailable"));
        if (Adapter)
        {
            if (!Adapter->Begin(World,Job->Directory,Reason)) {Job.Reset();SetStatus(Reason);return;}
            Job->OwnAdapter=true;
        }
        TStringBuilder<1024> Channels; FTraceAuxiliary::GetActiveChannelsString(Channels); Job->OriginalChannels=Channels.ToString();
        C->SetStringField(TEXT("trace_channels_before"),Job->OriginalChannels);
        FTraceAuxiliary::FOptions Options; Options.bExcludeTail=true;
        if (!GuLiFrameProfiler::SaveJson(Job->Directory/TEXT("context.json"),C) ||
            !FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File,*Job->TracePath,TEXT("cpu,frame,bookmark"),&Options))
        {
            if (StillOwnAdapter()) Job->Adapter->End();
            RestoreTraceChannels(); Job.Reset();SetStatus(TEXT("无法启动 CPU Trace，已清理本次原生采样。"));return;
        }
        Job->OwnTrace=true;
        Job->Deadline=FPlatformTime::Seconds()+5;
        State=EState::Armed;
        BeginFrame=FCoreDelegates::OnBeginFrame.AddRaw(this,&FGuLiFrameProfilerModule::OnBeginFrame);
        EndPIE=FEditorDelegates::PrePIEEnded.AddLambda([this](bool){if(Job && Job->PIE && (State==EState::Armed || State==EState::Recording)) FinishCPU(false,TEXT("PIE ended before CPU window completed."));});
        TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FGuLiFrameProfilerModule::Tick),.05f);
        SetStatus(CaptureRenderDoc?TEXT("开始采样：CPU 5 秒，随后截帧。采样期间可继续操作。"):TEXT("开始采样：仅采 CPU 5 秒。采样期间可继续操作。"));
    }
    bool ValidViewport() const
    {
        if (!Job || !Job->World.IsValid()) return false;
        if (Job->PIE) return Job->World->GetGameViewport() && Job->World->GetGameViewport()->Viewport==Job->Viewport;
        return GEditor && GEditor->GetActiveViewport()==Job->Viewport;
    }
    void OnBeginFrame()
    {
        if (!Job) return;
        if (!ValidViewport()) {FinishCPU(false,TEXT("Target world/viewport changed."));return;}
        if (State==EState::Armed)
        {
            // Start() queues a writer connection. Its trace GUID is not valid
            // until the connection is established; never sample/own a zero GUID.
            FGuid Session,Guid;
            if (!FTraceAuxiliary::IsConnected(Session,Guid) || !Guid.IsValid() || IFileManager::Get().FileSize(*Job->TracePath)<=0)
            {
                if (FPlatformTime::Seconds()>Job->Deadline) FinishCPU(false,TEXT("Trace writer connection timed out."));
                return;
            }
            const FString Destination=FPaths::ConvertRelativePathToFull(FTraceAuxiliary::GetTraceDestinationString());
            if (!FPaths::IsSamePath(Destination,Job->TracePath))
            {FinishCPU(false,TEXT("An external trace replaced the pending connection."));return;}
            Job->TraceGuid=Guid;
            State=EState::Recording;Job->Started=FPlatformTime::Seconds();
            CaptureBookmark(true,Job->Id);
        }
        else if (State==EState::Recording)
        {
            if (FApp::HasFocus()) ++Job->ForegroundFrames;else ++Job->BackgroundFrames;
            if (FPlatformTime::Seconds()-Job->Started>=CpuSeconds) FinishCPU(true,FString());
        }
    }
    void RemoveCaptureDelegates()
    {
        FCoreDelegates::OnBeginFrame.Remove(BeginFrame);BeginFrame.Reset();
        FEditorDelegates::PrePIEEnded.Remove(EndPIE);EndPIE.Reset();
    }
    void RestoreTraceChannels()
    {
        if (!Job) return;
        FTraceAuxiliary::DisableChannels(TEXT("cpu,frame,bookmark"));
        if (!Job->OriginalChannels.IsEmpty()) FTraceAuxiliary::EnableChannels(*Job->OriginalChannels);
    }
    bool StopOwnedTrace()
    {
        if (!Job || !Job->OwnTrace) return true;
        FGuid Session,Guid;
        const bool Connected=FTraceAuxiliary::IsConnected(Session,Guid);
        const bool SameFile=FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(FTraceAuxiliary::GetTraceDestinationString()),Job->TracePath);
        if (SameFile && (!Connected || !Job->TraceGuid.IsValid() || Guid==Job->TraceGuid))
        {
            // IsConnected() also includes a pending writer. Stop() rejects that
            // state; retain ownership and retry on the ticker instead of leaking
            // a file recording after an immediate cancel/PIE exit.
            if (!Connected || !FTraceAuxiliary::Stop()) return false;
            Job->OwnTrace=false;RestoreTraceChannels();return true;
        }
        Job->OwnTrace=false;Job->WindowComplete=false;Job->Interrupted=true;
        Job->Error=TEXT("Trace ownership changed; external trace was left untouched.");
        return true;
    }
    void FinishCPU(bool Complete,const FString& Error)
    {
        if (!Job || (State!=EState::Armed && State!=EState::Recording)) return;
        if (State==EState::Armed) CaptureBookmark(true,Job->Id);
        CaptureBookmark(false,Job->Id);
        Job->WindowComplete=Complete; Job->Interrupted=!Complete;Job->Error=Error;
        RemoveCaptureDelegates();
        // Finish this frame before requesting writer shutdown. Boundary events
        // must reach the trace stream; the additional frame is outside End.
        Job->StopAfterFrame=GFrameCounter+1;
        if (StillOwnAdapter())
        {
            TSharedPtr<FJsonObject> Project;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Job->Adapter->End()),Project) && Project) Job->Context->SetObjectField(TEXT("project"),Project);
        }
        Job->OwnAdapter=false;
        Job->Context->SetBoolField(TEXT("cpu_window_complete"),Job->WindowComplete);
        Job->Context->SetNumberField(TEXT("foreground_frames"),double(Job->ForegroundFrames));
        Job->Context->SetNumberField(TEXT("background_frames"),double(Job->BackgroundFrames));
        TStringBuilder<1024> AfterChannels;FTraceAuxiliary::GetActiveChannelsString(AfterChannels);
        Job->Context->SetStringField(TEXT("trace_channels_after"),AfterChannels.ToString());
        Job->Context->SetStringField(TEXT("interruption"),Job->Error);
        State=EState::ClosingTrace; Job->Deadline=FPlatformTime::Seconds()+15;
        SetStatus(Complete?(Job->CaptureRenderDoc?TEXT("CPU 采样结束，等待落盘后截帧。"):TEXT("CPU 采样结束，等待落盘后分析。")):TEXT("采样提前结束；保留不完整证据并生成报告。"));
    }
    void CancelCapture()
    {
        if (State==EState::Armed || State==EState::Recording) FinishCPU(false,TEXT("Cancelled by user."));
        else if (State==EState::Analyzing && Job) *Job->Cancel=true;
    }
    void SetRenderDocCVar(const TCHAR* Name,int32 Value)
    {
        if (auto* C=IConsoleManager::Get().FindConsoleVariable(Name))
        {
            Job->RenderDocCVars.Add({C,C->GetInt(),EConsoleVariableFlags(C->GetFlags()&ECVF_SetByMask),Name});
            C->Set(Value,ECVF_SetByConsole);
        }
    }
    bool RestoreRenderDoc()
    {
        if (!Job || Job->RenderDocCVars.IsEmpty()) return true;
        bool Restored=true;
        TArray<TSharedPtr<FJsonValue>> Settings;
        for (const auto& Item:Job->RenderDocCVars)
        {
            // SetOnly still checks priority. Remove our console override to
            // reveal the previous history entry; preserve an existing console
            // value by writing it back at the same priority instead.
            if (Item.SetBy==ECVF_SetByConsole) Item.Variable->Set(Item.Value,ECVF_SetByConsole);
            else Item.Variable->Unset(ECVF_SetByConsole);
            const auto AfterSetBy=EConsoleVariableFlags(Item.Variable->GetFlags()&ECVF_SetByMask);
            const bool Exact=Item.Variable->GetInt()==Item.Value && AfterSetBy==Item.SetBy;
            Restored &= Exact;
            auto Setting=MakeShared<FJsonObject>();
            Setting->SetStringField(TEXT("name"),Item.Name);
            Setting->SetNumberField(TEXT("before"),Item.Value);
            Setting->SetNumberField(TEXT("after"),Item.Variable->GetInt());
            Setting->SetStringField(TEXT("set_by_before"),GetConsoleVariableSetByName(Item.SetBy));
            Setting->SetStringField(TEXT("set_by_after"),GetConsoleVariableSetByName(AfterSetBy));
            Setting->SetBoolField(TEXT("restored"),Exact);
            Settings.Add(MakeShared<FJsonValueObject>(Setting));
            if (!Exact) UE_LOG(LogGuLiFrameProfiler,Error,TEXT("Capture setting was not restored: %s"),*Item.Name);
        }
        Job->Context->SetBoolField(TEXT("renderdoc_cvars_restored"),Restored);
        Job->Context->SetArrayField(TEXT("renderdoc_cvar_restore"),Settings);
        Job->RenderDocCVars.Empty();
        return Restored;
    }
    void CaptureGPU()
    {
        if (!Job->CaptureRenderDoc)
        {Job->Context->SetStringField(TEXT("renderdoc_status"),TEXT("not_requested: CPU-only capture"));StartAnalysis();return;}
        if (Job->Interrupted || !ValidViewport())
        {Job->Context->SetStringField(TEXT("renderdoc_status"),TEXT("skipped: interrupted capture or viewport destroyed"));StartAnalysis();return;}
        if (!FModuleManager::Get().IsModuleLoaded(TEXT("RenderDocPlugin")) || !IRenderCaptureProvider::IsAvailable())
        {Job->Context->SetStringField(TEXT("renderdoc_status"),TEXT("unavailable: RenderDoc was not ready at startup"));StartAnalysis();return;}
        SetRenderDocCVar(TEXT("renderdoc.CaptureAllActivity"),0);
        SetRenderDocCVar(TEXT("renderdoc.CaptureDelay"),0);
        SetRenderDocCVar(TEXT("renderdoc.CaptureFrameCount"),1);
        // RenderDoc itself scopes draw events around the captured frame.
        // Add detailed names here, then restore the exact values/priorities.
        SetRenderDocCVar(TEXT("r.ShowMaterialDrawEvents"),-1);
        SetRenderDocCVar(TEXT("r.RDG.Events"),3);
        State=EState::WaitingGpu;SetStatus(TEXT("正在截取目标视口；CPU 统计窗口已关闭。"));
        IRenderCaptureProvider::Get().CaptureFrame(Job->Viewport,0,Job->RdcPath);
        Job->GpuFence.BeginFence(); Job->Deadline=FPlatformTime::Seconds()+120;
    }
    void StartAnalysis()
    {
        GuLiFrameProfiler::SaveJson(Job->Directory/TEXT("context.json"),Job->Context);
        const auto Service=FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices")).GetAnalysisService();
        State=EState::Analyzing;SetStatus(TEXT("采集已完成，正在后台分析 CPU 热点。"));
        const FString TracePath=Job->TracePath,Id=Job->Id,Directory=Job->Directory;
        const auto Context=Job->Context;
        const auto Cancel=Job->Cancel;
        // Provider startup can invoke editor modules (including ChaosVD) that
        // require the game thread. The trace parser and report work stay async.
        const auto Session=Service->StartAnalysis(*TracePath);
        Job->Analysis=Async(EAsyncExecution::Thread,[TracePath,Id,Directory,Context,Cancel,Session]
        {
            auto Result=GuLiFrameProfiler::Analyze(TracePath,Id,Context,Session,Cancel);
            if (!Result.Json || !GuLiFrameProfiler::SaveJson(Directory/TEXT("report.json"),Result.Json.ToSharedRef()) ||
                !GuLiFrameProfiler::SaveText(Directory/TEXT("report.md"),Result.Markdown))
            {Result.Success=false;Result.Markdown+=TEXT("\n\n报告写入失败，请检查磁盘和目录权限。\n");}
            return Result;
        });
    }
    bool Tick(float)
    {
        if (!Job) return false;
        if (State==EState::ClosingTrace)
        {
            if (GFrameCounter<Job->StopAfterFrame) return true;
            if (!StopOwnedTrace())
            {
                if (!Job->StopWarning && FPlatformTime::Seconds()>Job->Deadline)
                {
                    Job->StopWarning=true;Job->WindowComplete=false;Job->Interrupted=true;
                    Job->Error=TEXT("Owned Trace writer is still stopping; no new capture will start until it settles.");
                    SetStatus(TEXT("本次 Trace 仍在停止，继续收尾；不会转为待机或启动第二次采样。"));
                }
                return true;
            }
            Job->Context->SetBoolField(TEXT("cpu_window_complete"),Job->WindowComplete);
            Job->Context->SetStringField(TEXT("interruption"),Job->Error);
            TStringBuilder<1024> Channels;FTraceAuxiliary::GetActiveChannelsString(Channels);
            Job->Context->SetStringField(TEXT("trace_channels_after"),Channels.ToString());
            // The default reader does not share write access. It succeeds only
            // once the trace writer has closed its file; size polling alone is insufficient.
            TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Job->TracePath,FILEREAD_Silent));
            const bool Closed=Reader && Reader->TotalSize()>0; Reader.Reset();
            if (Closed) CaptureGPU();
            else if (FPlatformTime::Seconds()>Job->Deadline)
            {
                Job->Context->SetStringField(TEXT("renderdoc_status"),TEXT("skipped: CPU trace did not finalize"));
                Job->Context->SetBoolField(TEXT("cpu_window_complete"),false);StartAnalysis();
            }
        }
        else if (State==EState::WaitingGpu)
        {
            if (Job->GpuFence.IsFenceComplete())
            {
                const bool Restored=RestoreRenderDoc();
                const bool Captured=IFileManager::Get().FileSize(*Job->RdcPath)>0;
                Job->Context->SetStringField(TEXT("renderdoc_status"),Captured?
                    (Restored?TEXT("captured"):TEXT("captured; failed to restore capture settings")):
                    TEXT("failed: RenderDoc did not produce the requested file"));
                StartAnalysis();
            }
            else if (FPlatformTime::Seconds()>Job->Deadline)
            {
                // Keep this job occupied until its render command finishes;
                // never create a second capture or unload a live capture resource.
                Job->Context->SetStringField(TEXT("renderdoc_status"),TEXT("capture delayed beyond 120 seconds"));
            }
        }
        else if (State==EState::Analyzing && Job->Analysis.IsReady())
        {
            auto Result=Job->Analysis.Get(); Report=MoveTemp(Result.Markdown); LastDirectory=Job->Directory;
            const bool Complete=Result.Success && Job->WindowComplete && (!Job->CaptureRenderDoc || Job->Context->GetStringField(TEXT("renderdoc_status"))==TEXT("captured"));
            const bool CpuOnly=!Job->CaptureRenderDoc;
            Job.Reset();State=EState::Idle;TickHandle.Reset();
            SetStatus(Complete?(CpuOnly?TEXT("完成：CPU 报告已保存。"):TEXT("完成：CPU 报告和 RenderDoc 截帧已保存。")):TEXT("报告已生成，部分采集未完成；请查看报告中的状态。"));
            OpenReport(); return false;
        }
        return true;
    }
};

IMPLEMENT_MODULE(FGuLiFrameProfilerModule,GuLiFrameProfiler)
#undef LOCTEXT_NAMESPACE
