// Explicit short diagnostics only. These per-widget callbacks are unbound in capture windows.
#include "CoreMinimal.h"
#include "Debugging/SlateDebugging.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Commander/UI/GuLiSceneUISubsystem.h"
#include "Commander/UI/GuLiSceneUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#if WITH_SLATE_DEBUGGING
namespace GuLiSlatePrepassDiagnostics
{
struct FSample {uint64 Frame=0;double Inclusive=0,Exclusive=0;uint32 Calls=0;};
struct FTarget
{
    TWeakPtr<SWidget> Widget;
    FString Name,Kind,World,Window;
    const SWidget* Parent=nullptr;
    TArray<FSample> Samples[2];
    TMap<uint32,uint64> Reasons;
};
struct FActive {const SWidget* Widget=nullptr;uint64 At=0;double Children=0;};
class FCapture
{
public:
    TMap<const SWidget*,FTarget> Targets;
    TArray<const SWidget*> Order;
    TArray<FActive> Stacks[2];
    FDelegateHandle PreBegin,PreEnd,PaintBegin,PaintEnd,Invalidated,EndPIE;
    FString Output;
    uint64 First=0,Last=0,Callbacks=0,UnattributedInvalidations=0;
    int32 InvalidPairs=0,DroppedSamples=0;
    bool Stopped=false;
    ~FCapture() {Stop();}
    void Add(TSharedPtr<SWidget> Widget,const FString& Kind,const FString& Name,const FString& World={})
    {
        if (!Widget || Targets.Contains(Widget.Get())) return;
        FTarget T;T.Widget=Widget;T.Kind=Kind;T.Name=Name;T.World=World;
        if (auto Window=FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef())) T.Window=Window->GetTitle().ToString();
        for (auto& Samples:T.Samples) Samples.Reserve(512);
        Order.Add(Widget.Get());Targets.Add(Widget.Get(),MoveTemp(T));
    }
    void AddWindow(TSharedRef<SWindow> Window)
    {
        if (Window->IsVisible() && !Window->IsWindowMinimized()) Add(Window,TEXT("window"),Window->GetTitle().ToString());
        for (const auto& Child:Window->GetChildWindows()) AddWindow(Child);
    }
    const SWidget* NearestTarget(const SWidget* Widget,bool IncludeSelf=true) const
    {
        const SWidget* Current=IncludeSelf?Widget:nullptr;
        TSharedPtr<SWidget> Parent;
        if (!IncludeSelf && Widget) {Parent=Widget->GetParentWidget();Current=Parent.Get();}
        for (int32 Depth=0;Current && Depth<256;++Depth)
        {
            if (Targets.Contains(Current)) return Current;
            Parent=Current->GetParentWidget();Current=Parent.Get();
        }
        return nullptr;
    }
    bool IsAncestor(const SWidget* Parent,const SWidget* Child) const
    {
        auto Next=Child?Child->GetParentWidget():nullptr;
        for (int32 Depth=0;Next && Depth<256;++Depth)
        {if (Next.Get()==Parent) return true;Next=Next->GetParentWidget();}
        return false;
    }
    void Start(const FString& Path)
    {
        Output=Path;First=Last=GFrameCounter;
        for (const auto& Window:FSlateApplication::Get().GetTopLevelWindows()) AddWindow(Window);
        for (const auto& Context:GEngine->GetWorldContexts())
        {
            auto* W=Context.World();if (!W || W->WorldType!=EWorldType::PIE || !W->GetGameInstance()) continue;
            const FString World=W->GetPathName();
            if (W->GetGameViewport()) Add(W->GetGameViewport()->GetGameViewportWidget(),TEXT("game_viewport"),TEXT("Viewport"),World);
            for (auto* Player:W->GetGameInstance()->GetLocalPlayers())
            {
                auto* PC=Player->GetPlayerController(W);
                if (auto* HUD=PC?Cast<AGuLiCommanderHUD>(PC->GetHUD()):nullptr)
                    if (auto* UI=HUD->GetRuntimeHUDWidget())
                    {
                        Add(UI->GetCachedWidget(),TEXT("commander_hud"),UI->GetPathName(),World);
                        for (const TCHAR* Name:{TEXT("SB_CommandDockScale"),TEXT("SB_CommandDockDesign"),TEXT("SB_TopStatus"),
                            TEXT("SB_MapDesign"),TEXT("SB_ShortcutsDesign"),TEXT("SB_DockDesign"),TEXT("C_Commands"),TEXT("C_UnitTypeCard")})
                            if (auto* Widget=UI->GetWidgetFromName(FName(Name))) Add(Widget->GetCachedWidget(),TEXT("hud_part"),Name,World);
                    }
                if (auto* Scene=Player->GetSubsystem<UGuLiSceneUISubsystem>())
                    if (auto* UI=Scene->GetWidget()) Add(UI->GetCachedWidget(),TEXT("scene_ui"),UI->GetPathName(),World);
            }
        }
        for (auto& Pair:Targets) Pair.Value.Parent=NearestTarget(Pair.Key,false);
        PreBegin=FSlateDebugging::BeginWidgetPrepass.AddLambda([this](const SWidget* W){Begin(W,0);});
        PreEnd=FSlateDebugging::EndWidgetPrepass.AddLambda([this](const SWidget* W){End(W,0);});
        PaintBegin=FSlateDebugging::BeginWidgetPaint.AddLambda([this](const SWidget* W,const FPaintArgs&,const FGeometry&,const FSlateRect&,const FSlateWindowElementList&,int32){Begin(W,1);});
        PaintEnd=FSlateDebugging::EndWidgetPaint.AddLambda([this](const SWidget* W,const FSlateWindowElementList&,int32){End(W,1);});
        Invalidated=FSlateDebugging::WidgetInvalidateEvent.AddLambda([this](const FSlateDebuggingInvalidateArgs& A)
        {
            ++Callbacks;
            const auto* Owner=NearestTarget(A.WidgetInvalidated);
            const uint32 Reason=uint32(A.InvalidateWidgetReason)|(uint32(A.InvalidateInvalidationRootReason)<<16);
            if (auto* T=Targets.Find(Owner)) ++T->Reasons.FindOrAdd(Reason);else ++UnattributedInvalidations;
        });
        EndPIE=FEditorDelegates::EndPIE.AddLambda([this](bool){Stop();});
    }
    void Begin(const SWidget* W,int32 Phase)
    {
        ++Callbacks;const auto* T=Targets.Find(W);
        if (T && T->Widget.Pin().Get()==W) Stacks[Phase].Add({W,FPlatformTime::Cycles64(),0});
    }
    void End(const SWidget* W,int32 Phase)
    {
        ++Callbacks;if (!Targets.Contains(W)) return;
        auto& Stack=Stacks[Phase];
        if (Stack.IsEmpty() || Stack.Last().Widget!=W) {++InvalidPairs;return;}
        const FActive A=Stack.Pop(EAllowShrinking::No);
        const double Ms=FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64()-A.At);
        if (!Stack.IsEmpty() && IsAncestor(Stack.Last().Widget,W)) Stack.Last().Children+=Ms;
        auto& Samples=Targets.FindChecked(W).Samples[Phase];Last=GFrameCounter;
        if (Samples.IsEmpty() || Samples.Last().Frame!=GFrameCounter)
        {if (Samples.Num()>=4096) {++DroppedSamples;return;}Samples.Add({GFrameCounter});}
        auto& S=Samples.Last();S.Inclusive+=Ms;S.Exclusive+=FMath::Max(0.,Ms-A.Children);++S.Calls;
    }
    void Stop()
    {
        if (Stopped) return;Stopped=true;Last=GFrameCounter;
        FSlateDebugging::BeginWidgetPrepass.Remove(PreBegin);FSlateDebugging::EndWidgetPrepass.Remove(PreEnd);
        FSlateDebugging::BeginWidgetPaint.Remove(PaintBegin);FSlateDebugging::EndWidgetPaint.Remove(PaintEnd);
        FSlateDebugging::WidgetInvalidateEvent.Remove(Invalidated);FEditorDelegates::EndPIE.Remove(EndPIE);
        InvalidPairs+=Stacks[0].Num()+Stacks[1].Num();
        auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("complete"),true);
        Root->SetNumberField(TEXT("first_frame"),double(First));Root->SetNumberField(TEXT("last_frame"),double(Last));
        Root->SetNumberField(TEXT("invalid_pairs"),InvalidPairs);Root->SetNumberField(TEXT("dropped_samples"),DroppedSamples);
        Root->SetNumberField(TEXT("callback_count"),double(Callbacks));Root->SetNumberField(TEXT("unattributed_invalidations"),double(UnattributedInvalidations));
        Root->SetStringField(TEXT("scope_note"),TEXT("Short diagnostic window only. Exclusive time subtracts nested observed targets only when actual Slate ancestry matches. Window residual includes editor/unattributed work and instrumentation; do not add nested inclusive totals."));
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (const auto* W:Order)
        {
            const auto& T=Targets.FindChecked(W);auto Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("name"),T.Name);Row->SetStringField(TEXT("kind"),T.Kind);Row->SetStringField(TEXT("world"),T.World);Row->SetStringField(TEXT("window"),T.Window);
            Row->SetStringField(TEXT("parent_target"),T.Parent?Targets.FindChecked(T.Parent).Name:FString());
            for (int32 Phase=0;Phase<2;++Phase)
            {
                TArray<TSharedPtr<FJsonValue>> Values;
                for (const auto& S:T.Samples[Phase])
                {
                    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("frame"),double(S.Frame));
                    J->SetNumberField(TEXT("inclusive_ms"),S.Inclusive);J->SetNumberField(TEXT("exclusive_ms"),S.Exclusive);
                    J->SetNumberField(TEXT("calls"),S.Calls);Values.Add(MakeShared<FJsonValueObject>(J));
                }
                Row->SetArrayField(Phase?TEXT("paint"):TEXT("prepass"),Values);
            }
            auto Reasons=MakeShared<FJsonObject>();for (const auto& Pair:T.Reasons) Reasons->SetNumberField(FString::FromInt(Pair.Key),double(Pair.Value));
            Row->SetObjectField(TEXT("invalidation_reasons_low16_widget_high16_root"),Reasons);Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("targets"),Rows);FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
        if (!FFileHelper::SaveStringToFile(Json,*Output)) UE_LOG(LogTemp,Error,TEXT("Cannot write Slate diagnostics: %s"),*Output);
    }
};
TUniquePtr<FCapture> Active;
FAutoConsoleCommand StartCommand(TEXT("gs.Slate.Prepass.Start"),TEXT("Short Prepass/Paint/invalidation diagnostics. Absolute JSON path outside the project, or under outputs/performance."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Active && !Active->Stopped) return;
        if (!GEditor || !GEditor->IsPlaySessionInProgress() || Args.Num()!=1 || FPaths::IsRelative(Args[0])) return;
        FString Output=FPaths::ConvertRelativePathToFull(Args[0].TrimQuotes());FPaths::CollapseRelativeDirectories(Output);
        const FString Project=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        if (FPaths::GetExtension(Output)!=TEXT("json") || (FPaths::IsUnderDirectory(Output,Project)
            && !FPaths::IsUnderDirectory(Output,Project/TEXT("outputs/performance")))) return;
        Active=MakeUnique<FCapture>();Active->Start(Output);
    }));
FAutoConsoleCommand StopCommand(TEXT("gs.Slate.Prepass.Stop"),TEXT("Unbind diagnostic callbacks and save their report."),
    FConsoleCommandDelegate::CreateLambda([] {if (Active) Active->Stop();}));
}
#endif
