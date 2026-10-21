#include "GuLiFrameProfilerAnalysis.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"

namespace GuLiFrameProfiler
{
namespace
{
struct FFrame { double Start=0,End=0; };
struct FTimer { FString Name; double Own=0,Inclusive=0; uint64 Calls=0; };
struct FFrameGap { FString Before,After; double Total=0,Peak=0; uint64 Calls=0; };
enum ERange : uint32
{
    Hybrid=1<<0,WorldUpdate=1<<1,StateApply=1<<2,Receipts=1<<3,StateTree=1<<4,
    Pathfinding=1<<5,Avoidance=1<<6,Slate=1<<7,EditorSlate=1<<8,GameWindow=1<<9,
    GameUI=1<<10,Wait=1<<11,Snapshot=1<<12,PlatformInput=1<<13,SharedSlate=1<<14
};
struct FRange { const TCHAR* Key; uint32 Flag; const TCHAR* Label; };
const FRange Ranges[]={
    {TEXT("hybrid_complete"),Hybrid,TEXT("完整 Hybrid")},
    {TEXT("world_update"),WorldUpdate,TEXT("Hybrid 世界更新")},
    {TEXT("state_apply"),StateApply,TEXT("状态应用")},
    {TEXT("receipts"),Receipts,TEXT("回执交付")},
    {TEXT("state_tree"),StateTree,TEXT("StateTree")},
    {TEXT("pathfinding"),Pathfinding,TEXT("寻路")},
    {TEXT("avoidance"),Avoidance,TEXT("避障")},
    {TEXT("snapshot"),Snapshot,TEXT("快照采集/捕获屏障")},
    {TEXT("slate_full"),Slate,TEXT("完整进程 Slate 布局/绘制")},
    {TEXT("slate_editor_only"),EditorSlate,TEXT("已归属编辑器 Slate")},
    {TEXT("slate_game_window"),GameWindow,TEXT("已归属游戏 Slate")},
    {TEXT("game_ui_paint"),GameUI,TEXT("已埋点游戏 UI 绘制")},
    {TEXT("platform_input"),PlatformInput,TEXT("平台消息与输入")},
    {TEXT("wait_sync"),Wait,TEXT("显式等待/同步")},
    {TEXT("slate_shared"),SharedSlate,TEXT("共享/未归属 Slate")}
};

bool IsPredictive(const FString& N)
{
    return N.StartsWith(TEXT("GuLiCommander_Predictive")) || N.StartsWith(TEXT("GuLiCommanderPredictiveAvoidanceProcessor"));
}

uint32 Classify(const FString& N)
{
    uint32 F=0;
    if (N==TEXT("GuLiHybrid_WorldTick") || N==TEXT("GuLiHybrid_MainLogicalBatch") ||
        N==TEXT("GuLiHybrid_MainGameplayApply") || N==TEXT("GuLiHybrid_MainReceipt") ||
        N==TEXT("GuLiCommander_MovementApply") || N==TEXT("GuLiCommander_SoldierCombat") ||
        N==TEXT("GuLiHybrid_ControlPointSnapshot")) F|=WorldUpdate;
    if ((F&WorldUpdate) || N.StartsWith(TEXT("GuLiHybrid_")) || N.StartsWith(TEXT("GuLiSim_")) ||
        IsPredictive(N) || N==TEXT("GuLiCommander_ManualAvoidanceRefresh")) F|=Hybrid;
    if (N==TEXT("GuLiHybrid_MainApplyBatch") || N==TEXT("GuLiHybrid_MainGameplayApply") ||
        N==TEXT("GuLiHybrid_ApplyCompletedWork")) F|=StateApply;
    if (N==TEXT("GuLiHybrid_MainReceipt") || N.Contains(TEXT("Receipt"))) F|=Receipts;
    if (N.Contains(TEXT("StateTree"))) F|=StateTree;
    if (N.Contains(TEXT("FindPath")) || N.Contains(TEXT("Pathfind")) || N.StartsWith(TEXT("GuLiNav_")) || N==TEXT("GuLiCommander_SharedRouteWork")) F|=Pathfinding;
    if (IsPredictive(N) || N.Contains(TEXT("Avoidance")) || N.Contains(TEXT("DetourCrowd"))) F|=Avoidance;
    if (N==TEXT("Slate::Tick (Time and Widgets)") || N==TEXT("Slate::DrawWindows") ||
        N==TEXT("Slate::Prepass") || N==TEXT("Slate::DrawWindow") || N.StartsWith(TEXT("GuLiSlate_"))) F|=Slate;
    if (N==TEXT("GuLiSlate_EditorOnly_Prepass") || N==TEXT("GuLiSlate_EditorOnly_Paint")) F|=EditorSlate;
    if (N==TEXT("GuLiSlate_GameWindow_Prepass") || N==TEXT("GuLiSlate_GameWindow_Paint")) F|=GameWindow;
    if (N.StartsWith(TEXT("GuLiSlate_OwnerEditor_"))) F|=EditorSlate;
    if (N.StartsWith(TEXT("GuLiSlate_OwnerGame_"))) F|=GameWindow;
    if (N.StartsWith(TEXT("GuLiSlate_OwnerShared_"))) F|=SharedSlate;
    if (N==TEXT("Paint: Game UI") || N==TEXT("GuLiSceneUI_Paint") ||
        N.StartsWith(TEXT("GuLiCommanderMiniMap_")) && N.EndsWith(TEXT("Paint"))) F|=GameUI;
    if (N==TEXT("Slate::Tick (Platform and Input)")) F|=PlatformInput;
    if (N.Contains(TEXT("Wait")) || N.Contains(TEXT("Sleep")) || N==TEXT("FFrameEndSync::Sync")) F|=Wait;
    if ((N.Contains(TEXT("Snapshot")) && (N.StartsWith(TEXT("GuLiHybrid_")) || N.StartsWith(TEXT("GuLiSim_")))) ||
        N==TEXT("GuLiHybrid_MainCaptureBarrier")) F|=Snapshot;
    return F;
}

TSharedRef<FJsonObject> Distribution(const TArray<double>& Values,double Seconds)
{
    auto J=MakeShared<FJsonObject>();
    if (Values.IsEmpty()) { J->SetBoolField(TEXT("available"),false); return J; }
    TArray<double> Sorted=Values; Sorted.Sort(); double Sum=0; for (double V:Values) Sum+=V;
    J->SetBoolField(TEXT("available"),true);
    J->SetStringField(TEXT("status"),TEXT("measured"));
    J->SetNumberField(TEXT("samples"),Values.Num());
    J->SetNumberField(TEXT("mean_ms"),Sum/Values.Num());
    J->SetNumberField(TEXT("p95_ms"),Sorted[FMath::Clamp(FMath::CeilToInt(Sorted.Num()*.95)-1,0,Sorted.Num()-1)]);
    J->SetNumberField(TEXT("peak_ms"),Sorted.Last());
    J->SetNumberField(TEXT("total_ms"),Sum);
    J->SetNumberField(TEXT("scope_ms_per_second"),Seconds>0?Sum/Seconds:0);
    return J;
}

TArray<TSharedPtr<FJsonValue>> TimerRows(const TMap<uint32,FTimer>& Timers,int32 Frames,double Denominator)
{
    TArray<FTimer> Sorted; Timers.GenerateValueArray(Sorted);
    Sorted.Sort([](const FTimer& A,const FTimer& B){return A.Own>B.Own;});
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& T:Sorted)
    {
        auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("name"),T.Name);
        J->SetNumberField(TEXT("self_ms"),T.Own*1000);
        J->SetNumberField(TEXT("inclusive_ms"),T.Inclusive*1000);
        J->SetNumberField(TEXT("self_ms_per_frame"),Frames?T.Own*1000/Frames:0);
        J->SetNumberField(TEXT("inclusive_ms_per_frame"),Frames?T.Inclusive*1000/Frames:0);
        J->SetNumberField(TEXT("calls"),double(T.Calls));
        J->SetNumberField(TEXT("self_percent_of_thread_scopes"),Denominator>0?100*T.Own/Denominator:0);
        Rows.Add(MakeShared<FJsonValueObject>(J));
    }
    return Rows;
}

FString StatsLine(const TCHAR* Label,const TSharedPtr<FJsonObject>& J)
{
    FString State; if (J) J->TryGetStringField(TEXT("status"),State);
    const TCHAR* Status=State==TEXT("not_applicable")?TEXT("不适用"):(State==TEXT("not_ready")?TEXT("运行环境未就绪"):TEXT("采集缺失"));
    if (!J || !J->GetBoolField(TEXT("available"))) return FString::Printf(TEXT("| %s | — | — | — | %s |\n"),Label,Status);
    return FString::Printf(TEXT("| %s | %.3f | %.3f | %.3f | %s |\n"),Label,
        J->GetNumberField(TEXT("mean_ms")),J->GetNumberField(TEXT("p95_ms")),J->GetNumberField(TEXT("peak_ms")),
        State==TEXT("no_activity")?TEXT("已覆盖，无调用"):TEXT("已测量"));
}

void AppendHotspots(FString& M,const TArray<TSharedPtr<FJsonValue>>& Rows,int32 Limit)
{
    M+=TEXT("\n| 热点 | 自身 ms/帧 | 包含 ms/帧 | 调用次数 |\n|---|---:|---:|---:|\n");
    for (int32 I=0;I<FMath::Min(Limit,Rows.Num());++I)
    {
        const auto J=Rows[I]->AsObject();
        M+=FString::Printf(TEXT("| %s | %.4f | %.4f | %.0f |\n"),*J->GetStringField(TEXT("name")),
            J->GetNumberField(TEXT("self_ms_per_frame")),J->GetNumberField(TEXT("inclusive_ms_per_frame")),J->GetNumberField(TEXT("calls")));
    }
}
}

bool SaveText(const FString& Path,const FString& Text)
{
    const FString Temp=Path+TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        IFileManager::Get().Move(*Path,*Temp,true,true);
}
bool SaveJson(const FString& Path,const TSharedRef<FJsonObject>& Object)
{
    FString Text; FJsonSerializer::Serialize(Object,TJsonWriterFactory<TCHAR,TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text));
    return SaveText(Path,Text);
}

FAnalysisResult Analyze(const FString& TracePath,const FString& CaptureId,const TSharedRef<FJsonObject>& Context,
    const TSharedPtr<const TraceServices::IAnalysisSession>& Session,const TSharedRef<FThreadSafeBool>& Cancel)
{
    FAnalysisResult Result; auto Root=MakeShared<FJsonObject>(); Result.Json=Root;
    Root->SetStringField(TEXT("schema"),TEXT("GuLiFrameProfiler/v1"));
    Root->SetNumberField(TEXT("analysis_version"),2); Root->SetNumberField(TEXT("hybrid_scope_version"),5);
    Root->SetStringField(TEXT("capture_id"),CaptureId); Root->SetObjectField(TEXT("context"),Context);
    const auto Fail=[&](const FString& Error)
    {
        Root->SetStringField(TEXT("status"),TEXT("incomplete")); Root->SetStringField(TEXT("error"),Error);
        Result.Markdown=TEXT("# GuLi Frame Profiler\n\n分析未完成：")+Error+TEXT("\n\n原始证据目录：")+FPaths::GetPath(TracePath);
        return Result;
    };
    if (!Session) return Fail(TEXT("Cannot open CPU trace."));
    const double Deadline=FPlatformTime::Seconds()+120;
    while (!Session->IsAnalysisComplete())
    {
        if (*Cancel || FPlatformTime::Seconds()>Deadline)
        { Session->Stop(true); return Fail(*Cancel?TEXT("Analysis cancelled."):TEXT("Trace analysis timed out.")); }
        FPlatformProcess::Sleep(.01f);
    }
    TraceServices::FAnalysisSessionReadScope SessionScope(*Session);
    const auto* Timing=TraceServices::ReadTimingProfilerProvider(*Session);
    if (!Timing) return Fail(TEXT("CPU timing provider unavailable."));
    double Begin=-1,End=-1;
    uint64 BookmarkCount=0;
    TArray<TSharedPtr<FJsonValue>> CaptureBookmarks;
    const FString BeginName=TEXT("GuLiFrameProfiler_Begin ")+CaptureId,EndName=TEXT("GuLiFrameProfiler_End ")+CaptureId;
    TraceServices::ReadBookmarkProvider(*Session).EnumerateBookmarks(0,Session->GetDurationSeconds(),[&](const TraceServices::FBookmark& B)
    {
        ++BookmarkCount;
        if (B.Text && FString(B.Text).StartsWith(TEXT("GuLiFrameProfiler_")) && CaptureBookmarks.Num()<8)
        {
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("text"),B.Text);Row->SetNumberField(TEXT("time"),B.Time);
            CaptureBookmarks.Add(MakeShared<FJsonValueObject>(Row));
        }
        if (BeginName==B.Text) Begin=B.Time;
        if (EndName==B.Text) End=B.Time;
    });
    Root->SetNumberField(TEXT("bookmark_events"),double(BookmarkCount));
    Root->SetArrayField(TEXT("capture_bookmarks"),CaptureBookmarks);
    if (Begin<0 || End<=Begin) return Fail(TEXT("Missing capture boundary bookmarks; no complete CPU window."));
    Root->SetNumberField(TEXT("bookmark_begin"),Begin); Root->SetNumberField(TEXT("bookmark_end"),End);
    Root->SetNumberField(TEXT("bookmark_seconds"),End-Begin);
    TArray<TraceServices::FThreadInfo> Threads;
    TraceServices::ReadThreadProvider(*Session).EnumerateThreads([&](const TraceServices::FThreadInfo& T){Threads.Add(T);});
    const auto* GT=Threads.FindByPredicate([](const auto& T){return T.Name && FCString::Stricmp(T.Name,TEXT("GameThread"))==0;});
    uint32 GameTimeline=0;
    if (!GT || !Timing->GetCpuThreadTimelineIndex(GT->Id,GameTimeline)) return Fail(TEXT("GameThread CPU scopes unavailable."));
    const uint32 GameThreadId=GT->Id;
    TArray<FFrame> Frames;
    Timing->ReadTimers([&](const TraceServices::ITimingProfilerTimerReader& Reader)
    {
        Timing->ReadTimeline(GameTimeline,[&](const TraceServices::ITimingProfilerTimeline& Timeline)
        {
            Timeline.EnumerateEvents(Begin,End,TraceServices::ITimingProfilerTimeline::EventRangeCallback(
                [&](double A,double B,uint32,const TraceServices::FTimingProfilerEvent& E)
                {
                    const auto* T=Reader.GetTimer(E.TimerIndex);
                    if (T && FCString::Strcmp(T->Name,TEXT("FEngineLoop::Tick"))==0 && A>=Begin && B<=End && FMath::IsFinite(B))
                        Frames.Add({A,B});
                    return TraceServices::EEventEnumerate::Continue;
                }));
        });
    });
    Frames.Sort([](const FFrame& A,const FFrame& B){return A.Start<B.Start;});
    if (Frames.IsEmpty()) return Fail(TEXT("No complete FEngineLoop::Tick frames inside capture window."));
    const double Start=Frames[0].Start,Stop=Frames.Last().End,Seconds=Stop-Start;
    const int32 Count=Frames.Num();
    Root->SetNumberField(TEXT("frames"),Count); Root->SetNumberField(TEXT("analyzed_begin"),Start);
    Root->SetNumberField(TEXT("analyzed_end"),Stop); Root->SetNumberField(TEXT("analyzed_seconds"),Seconds);
    Root->SetStringField(TEXT("basis"),TEXT("CPU scope wall time, including explicitly reported waits; not sampled OS CPU utilization. Full GameThread Tick roots only; per-frame denominators are shared, worker scopes are not a critical path estimate."));
    TArray<double> TickMs; for (const auto& F:Frames) TickMs.Add((F.End-F.Start)*1000);
    Root->SetObjectField(TEXT("game_thread_tick"),Distribution(TickMs,Seconds));
    TArray<double> FrameIntervals;
    TraceServices::ReadFrameProvider(*Session).EnumerateFrames(ETraceFrameType::TraceFrameType_Game,Begin,End,
        [&](const TraceServices::FFrame& F){if (F.StartTime>=Begin && F.EndTime<=End && FMath::IsFinite(F.EndTime)) FrameIntervals.Add((F.EndTime-F.StartTime)*1000);});
    Root->SetObjectField(TEXT("frame_interval"),Distribution(FrameIntervals,End-Begin));
    TMap<uint32,TArray<double>> GroupValues,BackgroundValues; TSet<uint32> Observed,BackgroundObserved;
    for (const auto& G:Ranges)
    {GroupValues.Add(G.Flag,TArray<double>()).Init(0,Count);BackgroundValues.Add(G.Flag,TArray<double>()).Init(0,Count);}
    TArray<TSharedPtr<FJsonValue>> ThreadRows,GameHotspots; TMap<uint32,FTimer> AllTimers;
    TMap<FString,FFrameGap> GameFrameGaps;
    double TotalScopeSeconds=0,GameScopeSeconds=0; uint64 LogicalSteps=0,GameWindowPaints=0; int32 InvalidStacks=0;
    TArray<double> GamePaintFrameCalls; GamePaintFrameCalls.Init(0,Count);
    Timing->ReadTimers([&](const TraceServices::ITimingProfilerTimerReader& Reader)
    {
        for (const auto& Thread:Threads)
        {
            if (*Cancel) break;
            uint32 Index=0; if (!Timing->GetCpuThreadTimelineIndex(Thread.Id,Index)) continue;
            const bool IsGame=Thread.Id==GameThreadId;
            struct FNode {uint32 Id=0,Flags=0; double Start=0;};
            TArray<FNode,TInlineAllocator<64>> Stack; TMap<uint32,FTimer> Timers;
            TArray<double> Work,Waiting; Work.Init(0,Count); Waiting.Init(0,Count);
            double Last=Start,ScopeSeconds=0,WaitSeconds=0; int32 Cursor=0;
            FString LastBoundary=TEXT("analysis start");
            const auto Advance=[&](double T,const FString& NextBoundary)
            {
                const double A=FMath::Clamp(Last,Start,Stop),B=FMath::Clamp(T,Start,Stop); Last=T;
                if (B<=A || Stack.IsEmpty()) return;
                const auto& Node=Stack.Last(); const double D=B-A;
                auto& Timer=Timers.FindChecked(Node.Id);
                Timer.Own+=D; ScopeSeconds+=D;
                // Report where otherwise-unattributed Frame wall time sits.
                // Analysis only: no additional instrumentation in the game loop.
                // These gaps can include waiting/descheduling, not just CPU work.
                if (IsGame && Timer.Name==TEXT("Frame"))
                {
                    auto& Gap=GameFrameGaps.FindOrAdd(LastBoundary+TEXT("\n")+NextBoundary);
                    Gap.Before=LastBoundary;Gap.After=NextBoundary;
                    Gap.Total+=D;Gap.Peak=FMath::Max(Gap.Peak,D);++Gap.Calls;
                }
                if (Node.Flags&Wait) WaitSeconds+=D;
                while (Cursor<Count && Frames[Cursor].End<=A) ++Cursor;
                for (int32 I=Cursor;I<Count && Frames[I].Start<B;++I)
                {
                    const double Part=FMath::Max(0.,FMath::Min(B,Frames[I].End)-FMath::Max(A,Frames[I].Start))*1000;
                    Work[I]+=Part; if (Node.Flags&Wait) Waiting[I]+=Part;
                    for (const auto& G:Ranges)
                        if ((Node.Flags&G.Flag) && !(G.Flag==EditorSlate && (Node.Flags&GameWindow)))
                            (IsGame?GroupValues:BackgroundValues).FindChecked(G.Flag)[I]+=Part;
                }
            };
            Timing->ReadTimeline(Index,[&](const TraceServices::ITimingProfilerTimeline& Timeline)
            {
                Timeline.EnumerateEvents(Start,Stop,TraceServices::ITimingProfilerTimeline::EventCallback(
                    [&](bool IsStart,double T,const TraceServices::FTimingProfilerEvent& E)
                    {
                        if (*Cancel) return TraceServices::EEventEnumerate::Stop;
                        const auto* Spec=Reader.GetTimer(E.TimerIndex);
                        if (!Spec) {++InvalidStacks; return TraceServices::EEventEnumerate::Stop;}
                        const FString Boundary=(IsStart?TEXT("begin "):TEXT("end "))+FString(Spec->Name);
                        Advance(T,Boundary);
                        if (IsStart)
                        {
                            auto& Timer=Timers.FindOrAdd(Spec->Id); Timer.Name=Spec->Name; ++Timer.Calls;
                            const uint32 Own=Classify(Timer.Name);
                            for (const auto& G:Ranges) if (Own&G.Flag) (IsGame?Observed:BackgroundObserved).Add(G.Flag);
                            if (IsGame)
                            {
                                for (const auto& G:Ranges) if (Own&G.Flag) Observed.Add(G.Flag);
                                if (Timer.Name==TEXT("GuLiHybrid_MainLogicalBatch")) ++LogicalSteps;
                                if (Timer.Name==TEXT("GuLiSlate_GameWindow_Paint"))
                                {
                                    ++GameWindowPaints;
                                    if (Cursor<Count && T>=Frames[Cursor].Start && T<Frames[Cursor].End) ++GamePaintFrameCalls[Cursor];
                                }
                            }
                            uint32 Flags=Own | (Stack.IsEmpty()?0:Stack.Last().Flags);
                            // v3 ownership is an exclusive partition, not an
                            // ancestry union. A game/shared child must not be
                            // subtracted as editor time through its ancestor.
                            if (Timer.Name.StartsWith(TEXT("GuLiSlate_Owner")))
                                Flags=(Flags&~uint32(EditorSlate|GameWindow|SharedSlate))|(Own&uint32(EditorSlate|GameWindow|SharedSlate));
                            Stack.Add({Spec->Id,Flags,T});
                        }
                        else
                        {
                            if (Stack.IsEmpty() || Stack.Last().Id!=Spec->Id) {++InvalidStacks; return TraceServices::EEventEnumerate::Stop;}
                            const auto Node=Stack.Pop(EAllowShrinking::No);
                            Timers.FindChecked(Node.Id).Inclusive+=FMath::Max(0.,FMath::Min(T,Stop)-FMath::Max(Node.Start,Start));
                        }
                        LastBoundary=Boundary;
                        return TraceServices::EEventEnumerate::Continue;
                    }));
                Advance(Stop,TEXT("analysis end"));
                // The timeline emits boundary events for enclosing scopes. Account
                // for any enclosing scope that extends beyond the selected window.
                while (!Stack.IsEmpty())
                {
                    const auto Node=Stack.Pop(EAllowShrinking::No);
                    Timers.FindChecked(Node.Id).Inclusive+=FMath::Max(0.,Stop-FMath::Max(Node.Start,Start));
                }
            });
            if (!ScopeSeconds) continue;
            auto Row=MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("id"),Thread.Id); Row->SetStringField(TEXT("name"),Thread.Name?Thread.Name:TEXT("unnamed"));
            Row->SetObjectField(TEXT("traced_scope_time_per_frame"),Distribution(Work,Seconds));
            Row->SetObjectField(TEXT("wait_sync_per_frame"),Distribution(Waiting,Seconds));
            Row->SetNumberField(TEXT("scope_ms_per_second"),ScopeSeconds*1000/Seconds);
            Row->SetNumberField(TEXT("wait_sync_ms_per_second"),WaitSeconds*1000/Seconds);
            Row->SetNumberField(TEXT("traced_non_wait_ms_per_second"),(ScopeSeconds-WaitSeconds)*1000/Seconds);
            Row->SetArrayField(TEXT("hotspots"),TimerRows(Timers,Count,ScopeSeconds));
            ThreadRows.Add(MakeShared<FJsonValueObject>(Row)); TotalScopeSeconds+=ScopeSeconds;
            if (IsGame) {GameHotspots=Row->GetArrayField(TEXT("hotspots")); GameScopeSeconds=ScopeSeconds;}
            for (const auto& Pair:Timers)
            {
                auto& T=AllTimers.FindOrAdd(Pair.Key);T.Name=Pair.Value.Name;T.Own+=Pair.Value.Own;
                T.Inclusive+=Pair.Value.Inclusive;T.Calls+=Pair.Value.Calls;
            }
        }
    });
    if (*Cancel) return Fail(TEXT("Analysis cancelled."));
    Root->SetNumberField(TEXT("invalid_event_stacks"),InvalidStacks);
    Root->SetArrayField(TEXT("threads"),ThreadRows); Root->SetArrayField(TEXT("game_thread_hotspots"),GameHotspots);
    TArray<FFrameGap> SortedGaps;GameFrameGaps.GenerateValueArray(SortedGaps);
    SortedGaps.Sort([](const FFrameGap& A,const FFrameGap& B){return A.Total>B.Total;});
    TArray<TSharedPtr<FJsonValue>> GapRows;
    for (const auto& Gap:SortedGaps)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("before"),Gap.Before);Row->SetStringField(TEXT("after"),Gap.After);
        Row->SetNumberField(TEXT("total_ms"),Gap.Total*1000);Row->SetNumberField(TEXT("ms_per_frame"),Gap.Total*1000/Count);
        Row->SetNumberField(TEXT("peak_gap_ms"),Gap.Peak*1000);Row->SetNumberField(TEXT("calls"),double(Gap.Calls));
        GapRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("game_thread_frame_gaps"),GapRows);
    Root->SetStringField(TEXT("frame_gap_basis"),TEXT("Unattributed Frame self wall time between adjacent trace boundaries; includes uninstrumented work, waiting and descheduling. Does not identify a function by itself."));
    Root->SetArrayField(TEXT("all_thread_hotspots"),TimerRows(AllTimers,Count,TotalScopeSeconds));
    Root->SetNumberField(TEXT("all_thread_scope_ms_per_second"),TotalScopeSeconds*1000/Seconds);
    Root->SetNumberField(TEXT("game_thread_scope_coverage"),GameScopeSeconds*1000/Root->GetObjectField(TEXT("game_thread_tick"))->GetNumberField(TEXT("total_ms")));
    const bool PIE=Context->GetStringField(TEXT("mode")).StartsWith(TEXT("PIE"));
    const TSharedPtr<FJsonObject>* Adapter=nullptr;
    Context->TryGetObjectField(TEXT("project"),Adapter);
    const TSharedPtr<FJsonObject>* Support=nullptr;
    const TSharedPtr<FJsonObject>* Metadata=nullptr;
    bool RuntimeReady=false,OwnerCovered=false;int32 OwnerVersion=0;
    if (Adapter && *Adapter)
    {
        (*Adapter)->TryGetObjectField(TEXT("metric_support"),Support);
        bool AtStart=false,AtEnd=false;
        (*Adapter)->TryGetBoolField(TEXT("runtime_ready_at_start"),AtStart);
        (*Adapter)->TryGetBoolField(TEXT("runtime_ready_at_end"),AtEnd);
        RuntimeReady=AtStart && AtEnd;
        if ((*Adapter)->TryGetObjectField(TEXT("slate_attribution"),Metadata))
        {
            double Version=0;(*Metadata)->TryGetNumberField(TEXT("scope_version"),Version);OwnerVersion=int32(Version);
            bool Valid=false,Complete=false;
            (*Metadata)->TryGetBoolField(TEXT("valid"),Valid);(*Metadata)->TryGetBoolField(TEXT("complete"),Complete);
            OwnerCovered=Valid && Complete && OwnerVersion>=2;
            if (OwnerVersion==2)
            {
                OwnerCovered&=Observed.Contains(GameWindow) && Observed.Contains(Slate);
                for (double Calls:GamePaintFrameCalls) OwnerCovered&=Calls>=1;
            }
        }
    }
    const auto Missing=[](const TCHAR* State,const TCHAR* Reason)
    {
        auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("available"),false);
        J->SetStringField(TEXT("status"),State);J->SetStringField(TEXT("reason"),Reason);return J;
    };
    const uint32 RuntimeFlags=Hybrid|WorldUpdate|StateApply|Receipts|StateTree|Pathfinding|Avoidance|Snapshot;
    const auto Metric=[&](const FRange& G,const TArray<double>& Values,bool Seen)
    {
        if ((!PIE && (G.Flag&RuntimeFlags)) || (!PIE && (G.Flag==GameWindow || G.Flag==GameUI)))
            return Missing(TEXT("not_applicable"),TEXT("未运行 PIE；本次是编辑器视口采样。"));
        bool Supported=false;
        if (Support && *Support) (*Support)->TryGetBoolField(G.Key,Supported);
        if (G.Flag==EditorSlate || G.Flag==GameWindow || G.Flag==SharedSlate) Supported=OwnerCovered;
        if (G.Flag==GameUI) Supported=OwnerCovered && PIE;
        if (Seen)
        {
            auto J=Distribution(Values,Seconds);J->SetStringField(TEXT("status"),TEXT("measured"));return J;
        }
        if ((G.Flag&RuntimeFlags) && Supported && !RuntimeReady)
            return Missing(TEXT("not_ready"),TEXT("PIE 运行环境尚未就绪，不能把未观测值登记为零。"));
        if (Supported)
        {
            auto J=Distribution(Values,Seconds);J->SetStringField(TEXT("status"),TEXT("no_activity"));
            J->SetNumberField(TEXT("observed_calls"),0);
            J->SetStringField(TEXT("reason"),TEXT("原生埋点覆盖已确认，本线程范围在窗口内没有调用；后台线程工作量单独统计。"));return J;
        }
        return Missing(TEXT("missing_instrumentation"),TEXT("本次采样缺少可验证的覆盖声明或归属；不填写零值。"));
    };
    // No game world exists in editor-only captures: all Slate work belongs to
    // the editor. In PIE only proven exclusive editor segments are subtracted.
    if (!PIE)
    {
        GroupValues.FindChecked(EditorSlate)=GroupValues.FindChecked(Slate);
        if (Observed.Contains(Slate)) Observed.Add(EditorSlate);
    }
    TArray<double> Shared;Shared.Init(0,Count);
    bool PartitionValid=true;
    for (int32 I=0;I<Count;++I)
    {
        Shared[I]=GroupValues.FindChecked(Slate)[I]-GroupValues.FindChecked(EditorSlate)[I]-GroupValues.FindChecked(GameWindow)[I];
        if (PIE) PartitionValid&=Shared[I]>=-1e-6;
        Shared[I]=FMath::Max(0.,Shared[I]);
    }
    GroupValues.FindChecked(SharedSlate)=MoveTemp(Shared);
    OwnerCovered&=PartitionValid;
    auto Groups=MakeShared<FJsonObject>(),Background=MakeShared<FJsonObject>();
    for (const auto& G:Ranges)
    {
        Groups->SetObjectField(G.Key,Metric(G,GroupValues.FindChecked(G.Flag),Observed.Contains(G.Flag)));
        if (G.Flag&RuntimeFlags) Background->SetObjectField(G.Key,Metric(G,BackgroundValues.FindChecked(G.Flag),BackgroundObserved.Contains(G.Flag)));
    }
    Root->SetObjectField(TEXT("game_thread_ranges"),Groups);
    Root->SetObjectField(TEXT("background_thread_ranges"),Background);
    Root->SetStringField(TEXT("range_basis"),TEXT("Ancestry union per named range. Ranges can overlap and must not be added together. Hybrid scope matches analyze_hotspot_capture v5, including predictive processor root and moved main-thread work."));
    Root->SetNumberField(TEXT("logical_steps"),double(LogicalSteps));
    if (LogicalSteps) Root->SetNumberField(TEXT("hybrid_ms_per_logical_step"),Groups->GetObjectField(TEXT("hybrid_complete"))->GetNumberField(TEXT("total_ms"))/LogicalSteps);
    auto ClientSlate=MakeShared<FJsonObject>(); bool Covered=PIE && OwnerCovered;
    if (Covered)
    {
        TArray<double> Bound; Bound.Init(0,Count);
        for (int32 I=0;I<Count;++I)
        {
            Bound[I]=GroupValues.FindChecked(Slate)[I]-GroupValues.FindChecked(EditorSlate)[I];
            Covered&=Bound[I]>=-1e-6 && Bound[I]+1e-6>=GroupValues.FindChecked(GameWindow)[I];
        }
        if (Covered) ClientSlate=Distribution(Bound,Seconds);
    }
    if (!Covered) ClientSlate=Missing(PIE?TEXT("missing_instrumentation"):TEXT("not_applicable"),PIE?
        TEXT("Slate 归属回调、生命周期或区间校验未通过。查看 context.project.slate_attribution。"):
        TEXT("未运行 PIE，没有游戏客户端 UI 可供验收。"));
    ClientSlate->SetBoolField(TEXT("available"),Covered);
    ClientSlate->SetStringField(TEXT("basis"),TEXT("Conservative client upper bound: full Slate ancestry union minus only proven exclusive editor segments. Shared/mixed ancestors and unknown widgets remain charged to client. v3 supports embedded PIE."));
    Root->SetObjectField(TEXT("client_slate_upper_bound"),ClientSlate);
    const bool WindowComplete=Context->GetBoolField(TEXT("cpu_window_complete"));
    Root->SetStringField(TEXT("status"),InvalidStacks==0 && WindowComplete?TEXT("complete"):TEXT("incomplete"));
    Root->SetNumberField(TEXT("game_window_paint_calls"),double(GameWindowPaints));
    Result.Success=InvalidStacks==0;
    FString& M=Result.Markdown;
    M=TEXT("# GuLi Frame Profiler\n\n");
    M+=FString::Printf(TEXT("采集：%s\n\n类型：%s；CPU 窗口 %.3f 秒，完整 Tick 帧 %d。\n\n"),*CaptureId,*Context->GetStringField(TEXT("mode")),End-Begin,Count);
    M+=TEXT("CPU 统计来自截帧前的运行区间。RenderDoc 截帧停顿、Trace 启停和分析不计入该窗口。CPU scope 是线程区间耗时，含显式等待；不代表操作系统 CPU 利用率。\n\n");
    M+=FString::Printf(TEXT("状态：%s；RenderDoc：%s。\n\n"),*Root->GetStringField(TEXT("status")),*Context->GetStringField(TEXT("renderdoc_status")));
    M+=TEXT("## 每帧耗时\n\n| 范围 | 平均 ms | P95 ms | 峰值 ms | 状态 |\n|---|---:|---:|---:|---|\n");
    M+=StatsLine(TEXT("引擎帧间隔"),Root->GetObjectField(TEXT("frame_interval")));
    M+=StatsLine(TEXT("游戏线程 Tick"),Root->GetObjectField(TEXT("game_thread_tick")));
    for (const auto& G:Ranges) M+=StatsLine(G.Label,Groups->GetObjectField(G.Key));
    M+=StatsLine(TEXT("游戏客户端 Slate 上界"),ClientSlate);
    if (!Covered) M+=PIE?TEXT("\n游戏 UI 与编辑器 Slate 归属校验未通过，不能据此验收客户端 UI 的 2.5 ms 目标。\n"):
        TEXT("\n本次未运行 PIE：玩法和游戏客户端 UI 指标不适用。\n");
    M+=TEXT("\n各范围可能互相包含，不能相加；同一范围内部按区间并集去重。只有原生覆盖已确认时，无调用才显示为零。\n");
    M+=TEXT("\n### 分项状态说明\n\n");
    for (const auto& G:Ranges)
    {
        const auto J=Groups->GetObjectField(G.Key);FString Reason;
        if (J->TryGetStringField(TEXT("reason"),Reason)) M+=FString::Printf(TEXT("- %s：%s\n"),G.Label,*Reason);
    }
    if (PIE)
    {
        M+=TEXT("\n## 后台分项 CPU 工作量\n\n| 范围 | 平均 ms/帧 | P95 ms | 峰值 ms | 状态 |\n|---|---:|---:|---:|---|\n");
        for (const auto& G:Ranges) if (G.Flag&RuntimeFlags) M+=StatsLine(G.Label,Background->GetObjectField(G.Key));
        M+=TEXT("\n后台值为各工作线程区间的 CPU 工作量之和，不能当作主线程成本或帧关键路径。\n");
    }
    double BackgroundFrames=0;
    if (Context->TryGetNumberField(TEXT("background_frames"),BackgroundFrames) && BackgroundFrames>0)
        M+=FString::Printf(TEXT("\n采样期间有 %.0f 个主循环检查点处于后台，后台限速和绘制暂停可能影响结果。\n"),BackgroundFrames);
    if (LogicalSteps) M+=FString::Printf(TEXT("\n逻辑批次：%llu；完整 Hybrid / 批次：%.3f ms。\n"),LogicalSteps,Root->GetNumberField(TEXT("hybrid_ms_per_logical_step")));
    M+=TEXT("\n## 游戏线程热点\n"); AppendHotspots(M,GameHotspots,20);
    M+=TEXT("\n## 线程工作量\n\n| 线程 | 区间 ms/帧 | P95 | 峰值 | 区间 ms/秒 | 等待/同步 ms/秒 |\n|---|---:|---:|---:|---:|---:|\n");
    ThreadRows.Sort([](const auto& A,const auto& B){return A->AsObject()->GetNumberField(TEXT("scope_ms_per_second"))>B->AsObject()->GetNumberField(TEXT("scope_ms_per_second"));});
    for (const auto& V:ThreadRows)
    {
        const auto J=V->AsObject(),S=J->GetObjectField(TEXT("traced_scope_time_per_frame"));
        M+=FString::Printf(TEXT("| %s (%u) | %.3f | %.3f | %.3f | %.2f | %.2f |\n"),*J->GetStringField(TEXT("name")),uint32(J->GetNumberField(TEXT("id"))),
            S->GetNumberField(TEXT("mean_ms")),S->GetNumberField(TEXT("p95_ms")),S->GetNumberField(TEXT("peak_ms")),J->GetNumberField(TEXT("scope_ms_per_second")),J->GetNumberField(TEXT("wait_sync_ms_per_second")));
    }
    M+=TEXT("\n多线程工作量不等于帧关键路径；各线程详细热点及原生负载、队列证据在 JSON 中。\n\n## 全线程热点\n");
    AppendHotspots(M,Root->GetArrayField(TEXT("all_thread_hotspots")),20);
    M+=TEXT("\n## 上下文\n\n地图：")+Context->GetStringField(TEXT("map"))+TEXT("\n\n构建：")+Context->GetStringField(TEXT("build_id"));
    if (Adapter && *Adapter)
    {
        const TSharedPtr<FJsonObject>* Profile=nullptr;
        if ((*Adapter)->TryGetObjectField(TEXT("native_profile"),Profile))
        {
            const TArray<TSharedPtr<FJsonValue>>* Samples=nullptr;
            if ((*Profile)->TryGetArrayField(TEXT("network_samples"),Samples) && Samples && !Samples->IsEmpty())
            {
                const auto First=(*Samples)[0]->AsObject(),Last=Samples->Last()->AsObject();
                M+=TEXT("\n\n### 实际模拟负载与队列（窗口首尾）\n\n");
                const TSharedPtr<FJsonObject>* Census=nullptr;
                for (const auto& Sample:{First,Last})
                {
                    M+=Sample==First?TEXT("开始：\n"):TEXT("结束：\n");
                    if (Sample->TryGetObjectField(TEXT("simulation_census"),Census))
                    {
                        FString Text;FJsonSerializer::Serialize((*Census).ToSharedRef(),TJsonWriterFactory<>::Create(&Text));M+=TEXT("```json\n")+Text+TEXT("\n```\n");
                    }
                    const TSharedPtr<FJsonObject>* Queue=nullptr;
                    if (Sample->TryGetObjectField(TEXT("async_transactions"),Queue))
                    {
                        FString Text;FJsonSerializer::Serialize((*Queue).ToSharedRef(),TJsonWriterFactory<>::Create(&Text));M+=TEXT("```json\n")+Text+TEXT("\n```\n");
                    }
                }
            }
        }
    }
    M+=TEXT("\n\n文件目录：")+FPaths::GetPath(TracePath)+TEXT("\n");
    return Result;
}
}
