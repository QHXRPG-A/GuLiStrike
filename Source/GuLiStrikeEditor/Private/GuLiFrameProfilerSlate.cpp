// Capture-only Slate ownership. Cached editor siblings may be subtracted;
// mixed ancestors, new widgets and shared work stay charged to the client.
#include "CoreMinimal.h"
#include "Debugging/SlateDebugging.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonSerializer.h"
#include "Slate/SGameLayerManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace GuLiProfilerSlate
{
#if WITH_SLATE_DEBUGGING && CPUPROFILERTRACE_ENABLED
enum class EOwner : uint8 {Shared,Editor,Game};
struct FIdentity {TWeakPtr<SWidget> Weak;EOwner Owner=EOwner::Shared;};
struct FActive {const SWidget* Widget=nullptr;EOwner Owner=EOwner::Shared;bool Emitted=false;};
class FCapture
{
public:
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakPtr<SViewport> ViewportWidget;
    TWeakPtr<SWindow> GameWindow;
    TWeakPtr<IGameLayerManager> LayerManager;
    TMap<const SWidget*,FIdentity> Owners;
    TSet<const SWidget*> GameRoots,Visiting;
    TArray<TWeakPtr<SWidget>> ViewportPath;
    TArray<FActive,TInlineAllocator<128>> Prepass,Paint;
    FDelegateHandle PreBegin,PreEnd,PaintBegin,PaintEnd,WindowBegin,WindowEnd,Frame;
    uint32 ScopeIds[2][3]={{0,0,0},{0,0,0}};
    uint64 Checks=0,Unknown=0,GamePaints=0,GamePrepasses=0,EditorCallbacks=0,WindowsPainted=0;
    int32 InvalidPairs=0,BindingChanges=0;
    bool PIE=false,Embedded=false,CacheComplete=true,Started=false;

    bool FindGameRoots(const TSharedRef<SWidget>& Widget,int32 Depth)
    {
        if (Depth>256 || Visiting.Contains(&Widget.Get())) {CacheComplete=false;return false;}
        Visiting.Add(&Widget.Get());
        if (Widget->GetType()==FName(TEXT("SGameLayerManager")))
        {
            auto Concrete=StaticCastSharedRef<SGameLayerManager>(Widget);
            if (static_cast<IGameLayerManager*>(&Concrete.Get())==LayerManager.Pin().Get()) GameRoots.Add(&Widget.Get());
        }
        auto* Children=Widget->GetAllChildren();
        for (int32 I=0;I<Children->Num();++I) FindGameRoots(Children->GetChildAt(I),Depth+1);
        Visiting.Remove(&Widget.Get());return true;
    }
    bool Cache(const TSharedRef<SWidget>& Widget,EOwner Inherited,int32 Depth)
    {
        if (Depth>256 || Visiting.Contains(&Widget.Get())) {CacheComplete=false;return false;}
        Visiting.Add(&Widget.Get());
        const bool IsGame=Inherited==EOwner::Game || GameRoots.Contains(&Widget.Get());
        bool ContainsGame=IsGame;
        auto* Children=Widget->GetAllChildren();
        for (int32 I=0;I<Children->Num();++I)
            ContainsGame|=Cache(Children->GetChildAt(I),IsGame?EOwner::Game:Inherited,Depth+1);
        EOwner Owner=IsGame?EOwner::Game:(ContainsGame?EOwner::Shared:Inherited);
        if (auto* Existing=Owners.Find(&Widget.Get()))
        {
            // Shared null/utility widgets can appear under more than one owner.
            if (Existing->Owner!=Owner) Existing->Owner=EOwner::Shared;
        }
        else Owners.Add(&Widget.Get(),{Widget,Owner});
        Visiting.Remove(&Widget.Get());return ContainsGame;
    }
    void CacheViewportPath()
    {
        ViewportPath.Reset();
        if (auto Widget=ViewportWidget.Pin())
        {
            FWidgetPath Path;
            FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget.ToSharedRef(),Path,EVisibility::All);
            for (int32 I=0;I<Path.Widgets.Num();++I) ViewportPath.Add(Path.Widgets[I].Widget);
        }
    }
    bool HasSameViewportPath() const
    {
        // The default GeneratePathToWidgetUnchecked path arranges/searches
        // the window. We only need identities, not arranged geometry.
        // The captured chain already gives us every edge to validate. Weak
        // identities also reject destruction/address reuse and reparenting.
        auto Child=StaticCastSharedPtr<SWidget>(ViewportWidget.Pin());
        for (int32 I=ViewportPath.Num()-1;I>=0;--I)
        {
            const auto Expected=ViewportPath[I].Pin();
            if (!Child || Child!=Expected) return false;
            auto Parent=Child->GetParentWidget();
            // A switcher can change its active child without changing parent
            // pointers. Retain the engine's path validity contract in that case.
            if (Parent && !Parent->ValidatePathToChild(Child.Get())) return false;
            Child=MoveTemp(Parent);
        }
        return !ViewportPath.IsEmpty() && !Child;
    }
    bool Start(UWorld* Target)
    {
        if (!Target || !FSlateApplication::IsInitialized()) return false;
        World=Target;PIE=Target->WorldType==EWorldType::PIE;
        auto EditorMain=FGlobalTabmanager::Get()->GetRootWindow();
        if (!EditorMain) return false;
        if (PIE)
        {
            Viewport=Target->GetGameViewport();
            if (!Viewport.IsValid()) return false;
            auto Widget=Viewport->GetGameViewportWidget();if (!Widget) return false;
            auto Window=FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef());if (!Window) return false;
            ViewportWidget=Widget;GameWindow=Window;Embedded=Window==EditorMain;
            LayerManager=Viewport->GetGameLayerManager();
            GameRoots.Add(Widget.Get());
        }
        const auto Windows=FSlateApplication::Get().GetTopLevelWindows();
        if (PIE && Embedded)
        {
            for (const auto& Window:Windows) FindGameRoots(Window,0);
            // Embedded game UI is a sibling of the viewport. Do not exclude
            // an unidentified game layer as an editor-owned subtree.
            if (LayerManager.IsValid() && GameRoots.Num()<2) return false;
        }
        TSharedPtr<SWindow> MessageLog;
        if (auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(TEXT("MessageLog"))))
            MessageLog=FSlateApplication::Get().FindWidgetWindow(Tab.ToSharedRef());
        for (const auto& Window:Windows)
        {
            const EOwner Owner=PIE && !Embedded && Window==GameWindow.Pin()?EOwner::Game:
                (Window==EditorMain || Window==MessageLog?EOwner::Editor:EOwner::Shared);
            Cache(Window,Owner,0);
        }
        CacheViewportPath();
        if (!CacheComplete || (PIE && !HasSameViewportPath())) return false;
        PreBegin=FSlateDebugging::BeginWidgetPrepass.AddLambda([this](const SWidget* W){Begin(W,false);});
        PreEnd=FSlateDebugging::EndWidgetPrepass.AddLambda([this](const SWidget* W){End(W,false);});
        PaintBegin=FSlateDebugging::BeginWidgetPaint.AddLambda([this](const SWidget* W,const FPaintArgs&,const FGeometry&,const FSlateRect&,const FSlateWindowElementList&,int32){Begin(W,true);});
        PaintEnd=FSlateDebugging::EndWidgetPaint.AddLambda([this](const SWidget* W,const FSlateWindowElementList&,int32){End(W,true);});
        WindowBegin=FSlateDebugging::BeginWindow.AddLambda([this](const FSlateWindowElementList& List){++WindowsPainted;Begin(List.GetPaintWindow(),true);});
        WindowEnd=FSlateDebugging::EndWindow.AddLambda([this](const FSlateWindowElementList& List){End(List.GetPaintWindow(),true);});
        Frame=FCoreDelegates::OnBeginFrame.AddLambda([this]
        {
            ++Checks;
            if (!World.IsValid()) {++BindingChanges;return;}
            if (PIE && (!Viewport.IsValid() || World->GetGameViewport()!=Viewport.Get() ||
                Viewport->GetGameViewportWidget()!=ViewportWidget.Pin() ||
                Viewport->GetGameLayerManager()!=LayerManager.Pin() || !HasSameViewportPath())) ++BindingChanges;
        });
        Started=true;return true;
    }
    void Begin(const SWidget* Widget,bool IsPaint)
    {
        if (!IsInGameThread()) return;
        auto& Stack=IsPaint?Paint:Prepass;
        EOwner Owner=EOwner::Shared;
        if (!Stack.IsEmpty() && Stack.Last().Owner==EOwner::Game) Owner=EOwner::Game;
        else if (const auto* Identity=Owners.Find(Widget);Identity && Identity->Weak.Pin().Get()==Widget) Owner=Identity->Owner;
        else ++Unknown;
        const bool Emit=(Stack.IsEmpty() || Owner!=Stack.Last().Owner) && bool(CpuChannel);
        if (Emit)
        {
            static const ANSICHAR* Names[2][3]={
                {"GuLiSlate_OwnerShared_Prepass","GuLiSlate_OwnerEditor_Prepass","GuLiSlate_OwnerGame_Prepass"},
                {"GuLiSlate_OwnerShared_Paint","GuLiSlate_OwnerEditor_Paint","GuLiSlate_OwnerGame_Paint"}};
            auto& Id=ScopeIds[IsPaint?1:0][static_cast<int32>(Owner)];
            FCpuProfilerTrace::GetOrCreateSpecId(Id,Names[IsPaint?1:0][static_cast<int32>(Owner)],__FILE__,__LINE__);
            FCpuProfilerTrace::OutputBeginEvent(Id);
        }
        Stack.Add({Widget,Owner,Emit});
        if (Owner==EOwner::Game) {if (IsPaint) ++GamePaints;else ++GamePrepasses;}
        if (Owner==EOwner::Editor) ++EditorCallbacks;
    }
    void End(const SWidget* Widget,bool IsPaint)
    {
        if (!IsInGameThread()) return;
        auto& Stack=IsPaint?Paint:Prepass;
        if (Stack.IsEmpty() || Stack.Last().Widget!=Widget) {++InvalidPairs;return;}
        if (Stack.Pop(EAllowShrinking::No).Emitted) FCpuProfilerTrace::OutputEndEvent();
    }
    TSharedRef<FJsonObject> Stop()
    {
        if (Started)
        {
            FSlateDebugging::BeginWidgetPrepass.Remove(PreBegin);FSlateDebugging::EndWidgetPrepass.Remove(PreEnd);
            FSlateDebugging::BeginWidgetPaint.Remove(PaintBegin);FSlateDebugging::EndWidgetPaint.Remove(PaintEnd);
            FSlateDebugging::BeginWindow.Remove(WindowBegin);FSlateDebugging::EndWindow.Remove(WindowEnd);
            FCoreDelegates::OnBeginFrame.Remove(Frame);Started=false;
        }
        auto J=MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("scope_version"),4);J->SetBoolField(TEXT("complete"),CacheComplete);
        J->SetBoolField(TEXT("valid"),CacheComplete && InvalidPairs==0 && BindingChanges==0 && Checks>0 && Paint.IsEmpty() && Prepass.IsEmpty());
        J->SetStringField(TEXT("layout"),PIE?(Embedded?TEXT("embedded_pie"):TEXT("detached_pie")):TEXT("editor_only"));
        J->SetNumberField(TEXT("checked_frames"),double(Checks));J->SetNumberField(TEXT("invalid_pairs"),InvalidPairs);
        J->SetNumberField(TEXT("binding_changes"),BindingChanges);J->SetNumberField(TEXT("cached_widgets"),Owners.Num());
        J->SetNumberField(TEXT("unknown_widget_callbacks"),double(Unknown));J->SetNumberField(TEXT("game_paint_callbacks"),double(GamePaints));
        J->SetNumberField(TEXT("game_prepass_callbacks"),double(GamePrepasses));J->SetNumberField(TEXT("editor_callbacks"),double(EditorCallbacks));
        J->SetNumberField(TEXT("window_paints"),double(WindowsPainted));
        J->SetStringField(TEXT("method"),TEXT("Capture-only cached widget ownership. Mixed ancestors, new/unclassified widgets and shared work remain in client upper bound. Owner transitions replace ancestor ownership; only exclusive proven editor segments are subtracted. Cached weak viewport ancestry, viewport/layer identity and paired callbacks are checked each frame without arranging the widget tree. Observer overhead remains charged."));
        return J;
    }
    ~FCapture() {if (Started) Stop();}
};
TUniquePtr<FCapture> Active;
#endif
}

bool IsGuLiFrameProfilerSlateActive()
{
#if WITH_SLATE_DEBUGGING && CPUPROFILERTRACE_ENABLED
    return bool(GuLiProfilerSlate::Active);
#else
    return false;
#endif
}
bool BeginGuLiFrameProfilerSlate(UWorld* World)
{
#if WITH_SLATE_DEBUGGING && CPUPROFILERTRACE_ENABLED
    if (GuLiProfilerSlate::Active) return false;
    auto Capture=MakeUnique<GuLiProfilerSlate::FCapture>();
    if (!Capture->Start(World)) return false;
    GuLiProfilerSlate::Active=MoveTemp(Capture);return true;
#else
    return false;
#endif
}
TSharedRef<FJsonObject> EndGuLiFrameProfilerSlate()
{
#if WITH_SLATE_DEBUGGING && CPUPROFILERTRACE_ENABLED
    if (GuLiProfilerSlate::Active)
    {
        auto J=GuLiProfilerSlate::Active->Stop();GuLiProfilerSlate::Active.Reset();return J;
    }
#endif
    auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("valid"),false);
    J->SetStringField(TEXT("reason"),TEXT("Slate ownership capture could not be installed."));return J;
}
