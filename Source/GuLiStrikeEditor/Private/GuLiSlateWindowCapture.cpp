// Opt-in native window attribution. Unassigned Slate work remains in the client upper bound.
#include "CoreMinimal.h"
#include "Debugging/SlateDebugging.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#if WITH_SLATE_DEBUGGING && CPUPROFILERTRACE_ENABLED
namespace GuLiSlateWindowCapture
{
struct FWindow
{
    TWeakPtr<SWindow> Weak;
    const SWidget* Raw=nullptr;
    FString Title, Kind;
    bool EditorOnly=false, Game=false;
    uint64 Prepasses=0, Paints=0;
};
struct FActive { const SWidget* Widget=nullptr; bool Emitted=false; };
class FCapture
{
public:
    TArray<FWindow,TInlineAllocator<8>> Windows;
    TArray<FActive,TInlineAllocator<8>> Prepasses, Paints;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakPtr<SViewport> ViewportWidget;
    TWeakPtr<SWindow> GameWindow;
    FDelegateHandle PreBegin,PreEnd,PaintBegin,PaintEnd,BeginFrame,EndPIE;
    FString Output;
    uint64 First=0, Last=0, CheckedFrames=0, PrepassCallbacks=0, UnknownPaints=0;
    uint32 GamePreId=0, GamePaintId=0, EditorPreId=0, EditorPaintId=0;
    int32 InvalidPairs=0, BindingChanges=0;
    bool Stopped=true;
    ~FCapture() { Stop(); }

    void AddWindow(TSharedRef<SWindow> W, const SWindow* EditorMain, const SWindow* MessageLog)
    {
        if (!Windows.ContainsByPredicate([&](const FWindow& Item){return Item.Raw==&W.Get();}))
        {
            FWindow Item; Item.Weak=W; Item.Raw=&W.Get(); Item.Title=W->GetTitle().ToString();
            Item.Game=&W.Get()==GameWindow.Pin().Get();
            Item.EditorOnly=!Item.Game && (&W.Get()==EditorMain || &W.Get()==MessageLog);
            Item.Kind=Item.Game?TEXT("game"):(&W.Get()==EditorMain?TEXT("editor_main"):
                (&W.Get()==MessageLog?TEXT("editor_message_log"):TEXT("unassigned")));
            Windows.Add(MoveTemp(Item));
        }
        for (const auto& Child:W->GetChildWindows()) AddWindow(Child,EditorMain,MessageLog);
    }
    bool Start(const FString& Path)
    {
        // A detached, uniquely identified rendered PIE viewport is required for editor subtraction.
        int32 Count=0;
        for (const auto& Context:GEngine->GetWorldContexts())
            if (auto* W=Context.World(); W && W->WorldType==EWorldType::PIE && W->GetGameViewport())
            { Viewport=W->GetGameViewport(); ++Count; }
        if (Count!=1 || !Viewport.IsValid()) return false;
        auto Widget=Viewport->GetGameViewportWidget();
        if (!Widget) return false;
        auto Window=FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef());
        auto EditorMain=FGlobalTabmanager::Get()->GetRootWindow();
        if (!Window || !EditorMain || Window==EditorMain) return false;
        ViewportWidget=Widget; GameWindow=Window;
        TSharedPtr<SWindow> MessageLog;
        if (auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(TEXT("MessageLog"))))
            MessageLog=FSlateApplication::Get().FindWidgetWindow(Tab.ToSharedRef());
        for (const auto& W:FSlateApplication::Get().GetTopLevelWindows())
            AddWindow(W,EditorMain.Get(),MessageLog.Get());
        if (!Windows.ContainsByPredicate([](const FWindow& Item){return Item.Game;})) return false;
        Output=Path; First=Last=GFrameCounter; Stopped=false;
        PreBegin=FSlateDebugging::BeginWidgetPrepass.AddRaw(this,&FCapture::BeginPrepass);
        PreEnd=FSlateDebugging::EndWidgetPrepass.AddRaw(this,&FCapture::EndPrepass);
        PaintBegin=FSlateDebugging::BeginWindow.AddLambda([this](const FSlateWindowElementList& List){Begin(List.GetPaintWindow(),true);});
        PaintEnd=FSlateDebugging::EndWindow.AddLambda([this](const FSlateWindowElementList& List){End(List.GetPaintWindow(),true);});
        BeginFrame=FCoreDelegates::OnBeginFrame.AddRaw(this,&FCapture::CheckBindings);
        EndPIE=FEditorDelegates::EndPIE.AddLambda([this](bool){Stop();});
        CheckBindings();
        return true;
    }
    void CheckBindings()
    {
        ++CheckedFrames; Last=GFrameCounter;
        auto Widget=Viewport.IsValid()?Viewport->GetGameViewportWidget():nullptr;
        if (!Widget || Widget!=ViewportWidget.Pin() || !GameWindow.IsValid() ||
            FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef())!=GameWindow.Pin())
        { ++BindingChanges; return; }
        int32 Count=0;
        for (const auto& Context:GEngine->GetWorldContexts())
            if (auto* W=Context.World(); W && W->WorldType==EWorldType::PIE && W->GetGameViewport())
            { ++Count; if (W->GetGameViewport()!=Viewport.Get()) ++BindingChanges; }
        if (Count!=1) ++BindingChanges;
    }
    void BeginPrepass(const SWidget* W) { ++PrepassCallbacks; Begin(W,false); }
    void EndPrepass(const SWidget* W) { ++PrepassCallbacks; End(W,false); }
    FWindow* Find(const SWidget* W)
    {
        // A few pointer comparisons, no widget traversal, allocation, timing or trace per leaf.
        for (auto& Item:Windows) if (Item.Raw==W) return &Item;
        return nullptr;
    }
    void Begin(const SWidget* W,bool Paint)
    {
        if (!IsInGameThread()) return;
        auto* Item=Find(W);
        if (!Item) { if (Paint) ++UnknownPaints; return; }
        if (Item->Weak.Pin().Get()!=W) { ++BindingChanges; return; }
        const bool Emit=(Item->Game || Item->EditorOnly) && bool(CpuChannel);
        if (Emit)
        {
            uint32& Id=Item->Game?(Paint?GamePaintId:GamePreId):(Paint?EditorPaintId:EditorPreId);
            const ANSICHAR* Name=Item->Game?(Paint?"GuLiSlate_GameWindow_Paint":"GuLiSlate_GameWindow_Prepass"):
                (Paint?"GuLiSlate_EditorOnly_Paint":"GuLiSlate_EditorOnly_Prepass");
            FCpuProfilerTrace::GetOrCreateSpecId(Id,Name,__FILE__,__LINE__);
            FCpuProfilerTrace::OutputBeginEvent(Id);
        }
        (Paint?Paints:Prepasses).Add({W,Emit});
        ++(Paint?Item->Paints:Item->Prepasses);
    }
    void End(const SWidget* W,bool Paint)
    {
        if (!IsInGameThread() || !Find(W)) return;
        auto& Stack=Paint?Paints:Prepasses;
        if (Stack.IsEmpty() || Stack.Last().Widget!=W) { ++InvalidPairs; return; }
        const auto Item=Stack.Pop(EAllowShrinking::No);
        if (Item.Emitted) FCpuProfilerTrace::OutputEndEvent();
    }
    void Stop()
    {
        if (Stopped) return;
        Stopped=true; Last=GFrameCounter;
        FSlateDebugging::BeginWidgetPrepass.Remove(PreBegin); FSlateDebugging::EndWidgetPrepass.Remove(PreEnd);
        FSlateDebugging::BeginWindow.Remove(PaintBegin); FSlateDebugging::EndWindow.Remove(PaintEnd);
        FCoreDelegates::OnBeginFrame.Remove(BeginFrame); FEditorDelegates::EndPIE.Remove(EndPIE);
        InvalidPairs+=Prepasses.Num()+Paints.Num();
        auto Root=MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("scope_version"),2);
        Root->SetBoolField(TEXT("complete"),true);
        Root->SetBoolField(TEXT("valid"),InvalidPairs==0 && BindingChanges==0 && CheckedFrames>0);
        Root->SetNumberField(TEXT("first_frame"),double(First)); Root->SetNumberField(TEXT("last_frame"),double(Last));
        Root->SetNumberField(TEXT("checked_frames"),double(CheckedFrames));
        Root->SetNumberField(TEXT("invalid_pairs"),InvalidPairs); Root->SetNumberField(TEXT("binding_changes"),BindingChanges);
        Root->SetNumberField(TEXT("prepass_callback_count"),double(PrepassCallbacks));
        Root->SetNumberField(TEXT("unknown_window_paints"),double(UnknownPaints));
        Root->SetStringField(TEXT("method"),TEXT("Client upper bound = complete Slate ancestry union minus only identified editor main/MessageLog root prepass and PaintWindow intervals. All unassigned/shared work, batching, cursors, popups and callback overhead remain charged to the client. Game-window prepass/paint is separately reported; it is not the complete client cost. No changes to invalidation, visibility or rendering."));
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (const auto& Item:Windows)
        {
            auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("title"),Item.Title); Row->SetStringField(TEXT("kind"),Item.Kind);
            Row->SetBoolField(TEXT("excluded_editor_only"),Item.EditorOnly);
            Row->SetNumberField(TEXT("prepass_calls"),double(Item.Prepasses)); Row->SetNumberField(TEXT("paint_calls"),double(Item.Paints));
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("windows"),Rows);
        FString Json; FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
        if (!FFileHelper::SaveStringToFile(Json,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogTemp,Error,TEXT("Cannot write Slate window capture: %s"),*Output);
    }
};
TUniquePtr<FCapture> Active;
FAutoConsoleCommand StartCommand(TEXT("gs.Slate.Windows.Start"),TEXT("Native window attribution. JSON report path outside project or under outputs/performance."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Active && !Active->Stopped) return;
        if (!GEditor || !GEditor->IsPlaySessionInProgress() || Args.Num()!=1 || FPaths::IsRelative(Args[0])) return;
        FString Output=FPaths::ConvertRelativePathToFull(Args[0].TrimQuotes()); FPaths::CollapseRelativeDirectories(Output);
        const FString Project=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        if (FPaths::GetExtension(Output)!=TEXT("json") || (FPaths::IsUnderDirectory(Output,Project) &&
            !FPaths::IsUnderDirectory(Output,Project/TEXT("outputs/performance")))) return;
        Active=MakeUnique<FCapture>();
        if (!Active->Start(Output)) UE_LOG(LogTemp,Error,TEXT("Slate window capture requires one detached rendered PIE viewport."));
    }));
FAutoConsoleCommand StopCommand(TEXT("gs.Slate.Windows.Stop"),TEXT("Remove window attribution callbacks and save report."),
    FConsoleCommandDelegate::CreateLambda([]{if (Active) Active->Stop();}));
}
#endif

// Called before editor-module unload, including shutdown during a capture.
void StopGuLiSlateWindowCapture()
{
#if WITH_SLATE_DEBUGGING && CPUPROFILERTRACE_ENABLED
    GuLiSlateWindowCapture::Active.Reset();
#endif
}
