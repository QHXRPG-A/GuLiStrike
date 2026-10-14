#include "Review.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace GuLiCoordinator
{
namespace
{
double Seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::uint64_t Counter()
{
#ifdef _WIN32
    LARGE_INTEGER Value;QueryPerformanceCounter(&Value);return Value.QuadPart;
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}
std::uint64_t CounterFrequency()
{
#ifdef _WIN32
    LARGE_INTEGER Value;QueryPerformanceFrequency(&Value);return Value.QuadPart;
#else
    return std::chrono::steady_clock::period::den/std::chrono::steady_clock::period::num;
#endif
}
unsigned ProcessId()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned>(getpid());
#endif
}
}
Review::Review(std::string Path,std::string RunId) : Directory(std::move(Path)),Run(std::move(RunId)) {}
void Review::Distribution::Add(double Value)
{
    if (!std::isfinite(Value) || Value<0) return;
    ++Count; Sum+=Value; Maximum=std::max(Maximum,Value);
    const double Microseconds=Value*1000;
    const auto Bucket=Microseconds<=1 ? 0u : static_cast<unsigned>(std::min(63.,std::ceil(std::log2(Microseconds))));
    ++Buckets[Bucket];
}
boost::json::object Review::Distribution::Json() const
{
    double P95=0; std::uint64_t Total=0;
    for (unsigned I=0;I<Buckets.size() && Count;++I)
    {
        Total+=Buckets[I];
        if (static_cast<double>(Total)>=std::ceil(static_cast<double>(Count)*.95))
        { P95=std::ldexp(1.,static_cast<int>(I))/1000.; break; }
    }
    return {{"count",Count},{"mean_ms",Count ? Sum/static_cast<double>(Count) : 0},
        {"max_ms",Maximum},{"p95_upper_ms",P95},{"p95_method","power-of-two microsecond histogram"}};
}
void Review::Record(const char* Metric,double Milliseconds)
{
    if (!Active) return;
    Measurements[Metric].Add(Milliseconds);
    if (TimedMeasurements.size()<1000000) TimedMeasurements.emplace_back(boost::json::object{
        {"qpc",Counter()},{"metric",Metric},{"value",Milliseconds}});
    else ++DroppedMeasurements;
}
void Review::ObserveOwnership(boost::json::object Value)
{
    if (!Detailed()) return;
    Value["sequence"]=++DecisionSequence;Value["qpc"]=Counter();Decisions.push_back(std::move(Value));
    while (Decisions.size()>4096) Decisions.pop_front();
}
bool Review::Write(const char* Name,const boost::json::object& Value)
{
    std::error_code EC; const std::filesystem::path Folder=std::filesystem::u8path(Directory);
    std::filesystem::create_directories(Folder,EC); if (EC) return false;
    const auto Destination=Folder/Name; auto Pending=Destination; Pending+=".pending";
    {
        std::ofstream Out(Pending,std::ios::binary | std::ios::trunc);
        if (!Out) return false; Out<<boost::json::serialize(Value); Out.flush(); if (!Out) return false;
    }
#ifdef _WIN32
    return MoveFileExW(Pending.c_str(),Destination.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(Pending,Destination,EC); return !EC;
#endif
}
bool Review::NeedsSnapshot() const {return Enabled() && (Seconds()>=NextWrite || Phase=="stop");}
bool Review::Tick(boost::json::object State)
{
    if (!Enabled()) return false;
    const double Now=Seconds();
    if (Now<NextRead) return false;
    NextRead=Now+.1;
    const auto Control=std::filesystem::u8path(Directory).parent_path()/"control.json";
    std::ifstream Input(Control,std::ios::binary | std::ios::ate);
    if (Input && Input.tellg()>0 && Input.tellg()<=32768)
    {
        Input.seekg(0); std::string Text((std::istreambuf_iterator<char>(Input)),{});
        boost::system::error_code EC; const auto Parsed=boost::json::parse(Text,EC);
        if (!EC && Parsed.is_object())
        {
            const auto& O=Parsed.as_object(); const auto S=O.if_contains("schema"), R=O.if_contains("run_id"), P=O.if_contains("phase");
            if (S && S->is_number() && (S->to_number<int>()==1 || S->to_number<int>()==2) && R && R->is_string() && R->as_string()==Run && P && P->is_string())
            {
                if (S->to_number<int>()==2)
                {
                    const auto Id=O.if_contains("stage_id"),N=O.if_contains("stage_number"),Case=O.if_contains("scenario");
                    if (!Id || !Id->is_string() || Id->as_string().empty() || Id->as_string().size()>96
                        || !N || (!N->is_uint64() && !N->is_int64()) || N->to_number<std::int64_t>()<StageNumber
                        || N->to_number<std::int64_t>()>10000 || !Case || !Case->is_string()
                        || Case->as_string().empty() || Case->as_string().size()>64
                        || (N->to_number<unsigned>()==StageNumber && Id->as_string()!=Stage))
                    {Failure="Stale or malformed integrated review stage";}
                    else {Stage=std::string(Id->as_string());StageNumber=N->to_number<unsigned>();Scenario=std::string(Case->as_string());}
                }
                const std::string NewPhase(P->as_string());
                if (NewPhase=="ship_equipment" || NewPhase=="prepare" || NewPhase=="roles" || NewPhase=="ground_movement" || NewPhase=="ground_combat" || NewPhase=="ship_movement" || NewPhase=="gameplay" || NewPhase=="steady" || NewPhase=="capture"
                    || NewPhase=="flights" || NewPhase=="handoff"
                    || NewPhase=="finish_capture" || NewPhase=="stop") Phase=NewPhase;
                else Failure="Unsupported acceptance phase";

            }
            else Failure="Stale or incompatible acceptance control";
        }
        else Failure="Invalid acceptance control JSON";
    }
    if (Phase=="capture" && !Active && !Written)
    {
        Active=true; Started=Now; Samples=0; FirstRx=Rx; FirstTx=Tx; Measurements.clear();
        Measurements.emplace("database_metadata_commit",Distribution{});
        TimedMeasurements.clear();DroppedMeasurements=0;
    }
    if (Active) ++Samples;
    if (Phase=="finish_capture" && Active)
    {
        Active=false; boost::json::object Metrics;
        for (const auto& [Key,D] : Measurements) Metrics[Key]=D.Json();
        boost::json::object Result{{"schema",1},{"run_id",Run},{"pid",ProcessId()},
            {"capture_seconds",Now-Started},{"sample_count",Samples},{"failure",Failure},
            {"qpc_frequency",CounterFrequency()},{"timed_measurements",std::move(TimedMeasurements)},
            {"measurements_dropped",DroppedMeasurements},{"capture_valid",DroppedMeasurements==0},
            {"metrics",std::move(Metrics)},{"received_application_bytes",Rx-FirstRx},{"sent_application_bytes",Tx-FirstTx},
            {"network_units","encoded WebSocket application bytes successfully read/written; excludes carrier headers"}};
        Written=Write("performance.json",Result);
        if (!Written) Failure="Cannot persist coordinator performance evidence";
    }
    State["schema"]=1; State["run_id"]=Run; State["pid"]=ProcessId();
    State["phase"]=Phase; State["capturing"]=Active; State["participating"]=true;
    State["scenario"]=Scenario;State["stage_id"]=Stage;State["stage_number"]=StageNumber;
    if (Detailed())
    {
        boost::json::array Values;for (const auto& D:Decisions) Values.emplace_back(D);
        State["decisions"]=std::move(Values);State["decision_sequence"]=DecisionSequence;
    }
    if (!Failure.empty()) State["failure"]=Failure;
    if ((Now>=NextWrite || Phase=="stop") && State.contains("rooms"))
    {
        if (Write("state.json",State)) { StateWriteFailureSince=-1; NextWrite=Now+1.; }
        else
        {
            // Windows readers may briefly prevent atomic replacement. Retry
            // without blocking room metadata work; persistent loss is fatal.
            if (StateWriteFailureSince<0) StateWriteFailureSince=Now;
            NextWrite=Now+.1;
            if (Now-StateWriteFailureSince>=15) Failure="Cannot persist coordinator runtime evidence";
        }
    }
    // A stop first read on a lightweight poll gets its final snapshot next poll.
    return Phase=="stop" && State.contains("rooms");
}
}
