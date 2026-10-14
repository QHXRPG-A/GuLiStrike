#pragma once
#include <boost/json.hpp>
#include <array>
#include <map>
#include <deque>
#include <string>

namespace GuLiCoordinator
{
/** Development IPC and metrics. It never manufactures gameplay assertions. */
class Review
{
public:
    Review(std::string Directory,std::string Run);
    bool Tick(boost::json::object State);
    bool Capturing() const { return Active; }
    bool Enabled() const { return !Directory.empty(); }
    bool NeedsSnapshot() const;
    bool Detailed() const {return Scenario.starts_with("gameplay_") || Scenario.starts_with("fault_");}
    void ObserveOwnership(boost::json::object Value);
    void Record(const char* Metric,double Milliseconds);
    void Received(std::size_t Bytes) { Rx+=Bytes; }
    void Sent(std::size_t Bytes) { Tx+=Bytes; }
private:
    struct Distribution
    {
        std::uint64_t Count=0;
        double Sum=0, Maximum=0;
        std::array<std::uint64_t,64> Buckets{};
        void Add(double Value);
        boost::json::object Json() const;
    };
    bool Write(const char* Name,const boost::json::object& Value);
    std::string Directory, Run, Phase="prepare", Failure;
    std::string Scenario,Stage="prepare";
    unsigned StageNumber=0;
    bool Active=false, Written=false;
    double Started=0, NextRead=0, NextWrite=0;
    double StateWriteFailureSince=-1;
    std::uint64_t Rx=0, Tx=0, FirstRx=0, FirstTx=0, Samples=0;
    std::map<std::string,Distribution> Measurements;
    boost::json::array TimedMeasurements;
    std::uint64_t DroppedMeasurements=0;
    std::deque<boost::json::object> Decisions;
    std::uint64_t DecisionSequence=0;
};
}
