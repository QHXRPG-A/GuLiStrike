#include "GuLiNetTrace.h"
#include <atomic>

namespace GuLiNet
{
namespace { std::atomic<TraceBegin> HostTrace{nullptr}; }
void SetTraceBegin(TraceBegin Begin) { HostTrace.store(Begin); }
TraceScope::TraceScope(const char* Name)
{
    if (const auto Begin=HostTrace.load(std::memory_order_relaxed)) End=Begin(Name);
}
}
