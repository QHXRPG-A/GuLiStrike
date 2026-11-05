#include "GuLiFrameProfilerAnalysis.h"
#include "Dom/JsonObject.h"

namespace GuLiFrameProfiler
{
namespace
{
constexpr struct { const TCHAR* Key; const TCHAR* Label; } QueueKinds[]={
    {TEXT("pose"),TEXT("姿态")}, {TEXT("ack"),TEXT("姿态确认回执")}, {TEXT("component"),TEXT("组件状态")}
};
constexpr struct { const TCHAR* Key; const TCHAR* Label; } MassReasons[]={
    {TEXT("first_or_reset"),TEXT("首次或重置")},{TEXT("fields_changed"),TEXT("真实字段变化")},
    {TEXT("animation_correction"),TEXT("动画进度纠偏")},{TEXT("unconfirmed_retry"),TEXT("未确认补发")},
    {TEXT("heartbeat"),TEXT("周期校正")}
};

struct FQueueCounts
{
    double Enqueued=0,Sent=0,Replaced=0,Pending=0,OldestMs=0;
};

struct FQueueSample
{
    double Seconds=0,Epoch=0,Generation=0,Peers=0,StationarySent=0,StationaryMassSent=0,StationarySkipped=0;
    FQueueCounts Kinds[UE_ARRAY_COUNT(QueueKinds)];
    bool HasReasons=false;
    double Reasons[UE_ARRAY_COUNT(MassReasons)]{},ExactSkips=0,PhaseSkips=0;
    bool HasBytes=false;
    double WireBytes=0,RecordBytes[3]{},BodyBytes[3]{};
    struct FVehicle { double Sent=0,Body=0,Records=0,Ack=0; } Vehicles[2];
};

bool ReadQueueSample(const TSharedPtr<FJsonValue>& Value,FQueueSample& Out)
{
    if (!Value || Value->Type!=EJson::Object) return false;
    const auto Sample=Value->AsObject();
    const TSharedPtr<FJsonObject>* Transport=nullptr;
    const TSharedPtr<FJsonObject>* Counters=nullptr;
    if (!Sample || !Sample->TryGetNumberField(TEXT("wall_seconds"),Out.Seconds)
        || !Sample->TryGetObjectField(TEXT("hybrid_transport"),Transport)
        || !(*Transport)->TryGetNumberField(TEXT("epoch"),Out.Epoch)
        || !(*Transport)->TryGetObjectField(TEXT("motion_queue_counters"),Counters)
        || !(*Counters)->TryGetNumberField(TEXT("generation"),Out.Generation)
        || !(*Counters)->TryGetNumberField(TEXT("peer_count"),Out.Peers)
        || !(*Counters)->TryGetNumberField(TEXT("stationary_unit_sent"),Out.StationarySent)
        || !(*Counters)->TryGetNumberField(TEXT("stationary_mass_sent"),Out.StationaryMassSent)
        || !(*Counters)->TryGetNumberField(TEXT("stationary_unit_skipped"),Out.StationarySkipped)) return false;
    Out.HasBytes=(*Counters)->TryGetNumberField(TEXT("wire_bytes"),Out.WireBytes);
    for (int32 I=0;I<UE_ARRAY_COUNT(QueueKinds);++I)
    {
        const TSharedPtr<FJsonObject>* Row=nullptr;
        auto& Counts=Out.Kinds[I];
        if (!(*Counters)->TryGetObjectField(QueueKinds[I].Key,Row)
            || !(*Row)->TryGetNumberField(TEXT("enqueued"),Counts.Enqueued)
            || !(*Row)->TryGetNumberField(TEXT("sent"),Counts.Sent)
            || !(*Row)->TryGetNumberField(TEXT("replaced"),Counts.Replaced)
            || !(*Row)->TryGetNumberField(TEXT("pending"),Counts.Pending)
            || !(*Row)->TryGetNumberField(TEXT("oldest_pending_ms"),Counts.OldestMs)) return false;
        Out.HasBytes &= (*Row)->TryGetNumberField(TEXT("record_bytes"),Out.RecordBytes[I])
            && (*Row)->TryGetNumberField(TEXT("body_bytes"),Out.BodyBytes[I]);
    }
    for (int32 I=0;I<2;++I)
    {
        const TSharedPtr<FJsonObject>* V=nullptr;auto& Count=Out.Vehicles[I];
        Out.HasBytes &= (*Counters)->TryGetObjectField(I==0?TEXT("engineer"):TEXT("miner"),V)
            && (*V)->TryGetNumberField(TEXT("pose_sent"),Count.Sent) && (*V)->TryGetNumberField(TEXT("body_bytes"),Count.Body)
            && (*V)->TryGetNumberField(TEXT("pose_record_bytes"),Count.Records) && (*V)->TryGetNumberField(TEXT("ack_record_bytes"),Count.Ack);
    }
    const TSharedPtr<FJsonObject>* Reasons=nullptr;
    Out.HasReasons=(*Counters)->TryGetObjectField(TEXT("stationary_mass_sent_reasons"),Reasons)
        && (*Counters)->TryGetNumberField(TEXT("stationary_mass_skipped_exact"),Out.ExactSkips)
        && (*Counters)->TryGetNumberField(TEXT("stationary_mass_skipped_natural_phase"),Out.PhaseSkips);
    if (Out.HasReasons) for (int32 I=0;I<UE_ARRAY_COUNT(MassReasons);++I)
        Out.HasReasons &= (*Reasons)->TryGetNumberField(MassReasons[I].Key,Out.Reasons[I]);
    return true;
}

bool CountersRewound(const FQueueSample& Before,const FQueueSample& After)
{
    if (After.Seconds<Before.Seconds || After.StationarySent<Before.StationarySent
        || After.StationaryMassSent<Before.StationaryMassSent || After.StationarySkipped<Before.StationarySkipped) return true;
    for (int32 I=0;I<UE_ARRAY_COUNT(QueueKinds);++I)
    {
        const auto& A=Before.Kinds[I];const auto& B=After.Kinds[I];
        if (B.Enqueued<A.Enqueued || B.Sent<A.Sent || B.Replaced<A.Replaced) return true;
    }
    if (Before.HasReasons && After.HasReasons)
    {
        if (After.ExactSkips<Before.ExactSkips || After.PhaseSkips<Before.PhaseSkips) return true;
        for (int32 I=0;I<UE_ARRAY_COUNT(MassReasons);++I) if (After.Reasons[I]<Before.Reasons[I]) return true;
    }
    if (Before.HasBytes && After.HasBytes)
    {
        if (After.WireBytes<Before.WireBytes) return true;
        for (int32 I=0;I<UE_ARRAY_COUNT(QueueKinds);++I)
            if (After.RecordBytes[I]<Before.RecordBytes[I] || After.BodyBytes[I]<Before.BodyBytes[I]) return true;
        for (int32 I=0;I<2;++I)
        {
            const auto& A=Before.Vehicles[I]; const auto& B=After.Vehicles[I];
            if (B.Sent<A.Sent || B.Body<A.Body || B.Records<A.Records || B.Ack<A.Ack) return true;
        }
    }
    return false;
}
}

void AppendMotionQueueReport(const FJsonObject* Profile,FJsonObject& Report,FString& Markdown)
{
    auto Window=MakeShared<FJsonObject>();
    Report.SetObjectField(TEXT("motion_queue_window"),Window);
    Markdown+=TEXT("\n## 姿态发送队列\n\n");
    const auto Unavailable=[&](const TCHAR* Reason)
    {
        Window->SetBoolField(TEXT("available"),false);
        Window->SetStringField(TEXT("reason"),Reason);
        Markdown+=FString(Reason)+TEXT("\n");
    };
    const TArray<TSharedPtr<FJsonValue>>* Samples=nullptr;
    if (!Profile || !Profile->TryGetArrayField(TEXT("network_samples"),Samples) || Samples->Num()<2)
    { Unavailable(TEXT("网络窗口采样点不足。"));return; }
    double Dropped=0;
    if (Profile->TryGetNumberField(TEXT("network_samples_dropped"),Dropped) && Dropped>0)
    { Unavailable(TEXT("网络采样点有丢弃，本窗口计数不完整。"));return; }

    FQueueSample First,Last;
    TArray<FQueueSample> Timeline;
    bool AllReasons=true,AllBytes=true;
    for (int32 I=0;I<Samples->Num();++I)
    {
        FQueueSample Current;
        if (!ReadQueueSample((*Samples)[I],Current)) {Unavailable(TEXT("缺少分类队列计数。"));return;}
        if (Current.Peers<1 || Current.Epoch<1) {Unavailable(TEXT("采样期间缺少已建立的联机对端。"));return;}
        if (I==0) First=Current;
        else if (Current.Generation!=First.Generation || Current.Epoch!=First.Epoch || Current.Peers!=First.Peers)
        { Unavailable(TEXT("采样期间对端或会话发生变化，分类计数无法相减。"));return; }
        else if (CountersRewound(Last,Current)) {Unavailable(TEXT("采样期间时钟或累计计数回退。"));return;}
        Last=Current; Timeline.Add(Current); AllReasons &= Current.HasReasons; AllBytes &= Current.HasBytes;
    }
    const double Seconds=Last.Seconds-First.Seconds;
    if (Seconds<=0) {Unavailable(TEXT("网络采样窗口长度为零。"));return;}
    Window->SetBoolField(TEXT("available"),true);
    Window->SetNumberField(TEXT("seconds"),Seconds);
    Window->SetNumberField(TEXT("peer_count"),Last.Peers);
    Markdown+=FString::Printf(TEXT("%.3f 秒网络采样，%.0f 个接收端。数量按发往各接收端的逻辑记录计算。\n\n"),Seconds,Last.Peers);
    Markdown+=TEXT("| 类型 | 入队 | 发出 | 被替换 | 期末待发 | 期末最老待发 ms |\n|---|---:|---:|---:|---:|---:|\n");
    for (int32 I=0;I<UE_ARRAY_COUNT(QueueKinds);++I)
    {
        const auto& A=First.Kinds[I];const auto& B=Last.Kinds[I];
        const double Enqueued=B.Enqueued-A.Enqueued,Sent=B.Sent-A.Sent,Replaced=B.Replaced-A.Replaced;
        auto Row=MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("enqueued"),Enqueued);
        Row->SetNumberField(TEXT("sent"),Sent);
        Row->SetNumberField(TEXT("replaced"),Replaced);
        Row->SetNumberField(TEXT("pending_start"),A.Pending);
        Row->SetNumberField(TEXT("pending_end"),B.Pending);
        Row->SetNumberField(TEXT("oldest_pending_ms_end"),B.OldestMs);
        if (AllBytes)
        {
            Row->SetNumberField(TEXT("record_bytes"),Last.RecordBytes[I]-First.RecordBytes[I]);
            Row->SetNumberField(TEXT("body_bytes"),Last.BodyBytes[I]-First.BodyBytes[I]);
        }
        Window->SetObjectField(QueueKinds[I].Key,Row);
        Markdown+=FString::Printf(TEXT("| %s | %.0f | %.0f | %.0f | %.0f | %.1f |\n"),QueueKinds[I].Label,Enqueued,Sent,Replaced,B.Pending,B.OldestMs);
    }
    if (AllBytes)
    {
        const double Wire=Last.WireBytes-First.WireBytes;
        Window->SetNumberField(TEXT("wire_bytes"),Wire);
        Window->SetNumberField(TEXT("wire_decimal_kB_s"),Wire/Seconds/1000.);
        Markdown+=FString::Printf(TEXT("\n姿态通道实际发出 **%.2f KB/s**，含批次与传输包头。\n"),Wire/Seconds/1000.);
        Markdown+=TEXT("\n| 车辆 | 姿态条数 | 正文 KB/s | 姿态记录 KB/s | 回执记录 KB/s |\n|---|---:|---:|---:|---:|\n");
        for (int32 I=0;I<2;++I)
        {
            const auto& A=First.Vehicles[I];const auto& B=Last.Vehicles[I];
            auto V=MakeShared<FJsonObject>();
            V->SetNumberField(TEXT("pose_sent"),B.Sent-A.Sent);V->SetNumberField(TEXT("body_bytes"),B.Body-A.Body);
            V->SetNumberField(TEXT("pose_record_bytes"),B.Records-A.Records);V->SetNumberField(TEXT("ack_record_bytes"),B.Ack-A.Ack);
            Window->SetObjectField(I==0?TEXT("engineer"):TEXT("miner"),V);
            Markdown+=FString::Printf(TEXT("| %s | %.0f | %.2f | %.2f | %.2f |\n"),I==0?TEXT("工程车"):TEXT("采矿车"),B.Sent-A.Sent,
                (B.Body-A.Body)/Seconds/1000.,(B.Records-A.Records)/Seconds/1000.,(B.Ack-A.Ack)/Seconds/1000.);
        }
    }
    const double StationarySent=Last.StationarySent-First.StationarySent;
    const double StationaryMassSent=Last.StationaryMassSent-First.StationaryMassSent;
    const double StationarySkipped=Last.StationarySkipped-First.StationarySkipped;
    Window->SetNumberField(TEXT("stationary_unit_sent"),StationarySent);
    Window->SetNumberField(TEXT("stationary_mass_sent"),StationaryMassSent);
    Window->SetNumberField(TEXT("stationary_unit_skipped"),StationarySkipped);
    Markdown+=FString::Printf(TEXT("\n静止单位姿态实际发出 **%.0f** 条（其中 Mass 士兵 %.0f 条），编码前因状态未变跳过 **%.0f** 次。\n"),StationarySent,StationaryMassSent,StationarySkipped);
    Markdown+=TEXT("Mass 采用三轴量化速度为零且没有瞬移的判定。工程车、采矿车、僚机及玩家载具采用姿态中的线速度为零。原地转向、动画变化和定期校正仍可能发出。静止发出已包含在姿态行内，跳过按接收端在 Mass 编码前计数。\n");
    if (AllReasons)
    {
        auto Reasons=MakeShared<FJsonObject>(); double Sum=0;
        Markdown+=TEXT("\n| 静止 Mass 实发原因 | 条数 |\n|---|---:|\n");
        for (int32 I=0;I<UE_ARRAY_COUNT(MassReasons);++I)
        {
            const double Count=Last.Reasons[I]-First.Reasons[I]; Sum+=Count;
            Reasons->SetNumberField(MassReasons[I].Key,Count);
            Markdown+=FString::Printf(TEXT("| %s | %.0f |\n"),MassReasons[I].Label,Count);
        }
        Window->SetObjectField(TEXT("stationary_mass_sent_reasons"),Reasons);
        const double Exact=Last.ExactSkips-First.ExactSkips,Phase=Last.PhaseSkips-First.PhaseSkips;
        Window->SetNumberField(TEXT("stationary_mass_skipped_exact"),Exact);
        Window->SetNumberField(TEXT("stationary_mass_skipped_natural_phase"),Phase);
        Markdown+=FString::Printf(TEXT("\n编码前跳过完全重复 %.0f 次，自然待机动画推进 %.0f 次。\n"),Exact,Phase);
        if (Sum!=StationaryMassSent || Exact+Phase!=StationarySkipped)
            Markdown+=TEXT("分类合计与总数不符，此窗口不能用于判断原因占比。\n");
    }
    else Markdown+=TEXT("\n此采集未提供完整的静止 Mass 原因分类。\n");
    Markdown+=TEXT("\n各采样点发包后的待发条数 / 最老年龄（毫秒）\n\n| 相对秒 | 姿态 | 回执 | 组件 |\n|---:|---:|---:|---:|\n");
    TArray<TSharedPtr<FJsonValue>> Points;
    for (const auto& Sample:Timeline)
    {
        auto Point=MakeShared<FJsonObject>();
        Point->SetNumberField(TEXT("seconds"),Sample.Seconds-First.Seconds);
        Markdown+=FString::Printf(TEXT("| %.2f"),Sample.Seconds-First.Seconds);
        for (int32 I=0;I<UE_ARRAY_COUNT(QueueKinds);++I)
        {
            auto Gauge=MakeShared<FJsonObject>();
            Gauge->SetNumberField(TEXT("pending"),Sample.Kinds[I].Pending);
            Gauge->SetNumberField(TEXT("oldest_pending_ms"),Sample.Kinds[I].OldestMs);
            Point->SetObjectField(QueueKinds[I].Key,Gauge);
            Markdown+=FString::Printf(TEXT(" | %.0f / %.1f"),Sample.Kinds[I].Pending,Sample.Kinds[I].OldestMs);
        }
        Markdown+=TEXT(" |\n"); Points.Add(MakeShared<FJsonValueObject>(Point));
    }
    Window->SetArrayField(TEXT("queue_samples"),Points);
    Markdown+=TEXT("前三列取窗口增量。入队指传输队列接纳，发出按首次成功提交的记录计数，同一批中的记录分别计数。被替换指队列内旧记录被同对象同类型的新记录覆盖。末两列取末次发包后的队列状态。\n");
}
}
