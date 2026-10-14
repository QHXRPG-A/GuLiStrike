#pragma once

namespace GuLiNet
{
// Optional host instrumentation. Portable gameplay never includes engine headers.
using TraceEnd = void (*)();
using TraceBegin = TraceEnd (*)(const char* Name);
void SetTraceBegin(TraceBegin Begin);
class TraceScope
{
public:
    explicit TraceScope(const char* Name);
    ~TraceScope() { if (End) End(); }
    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;
private:
    TraceEnd End=nullptr;
};
}
