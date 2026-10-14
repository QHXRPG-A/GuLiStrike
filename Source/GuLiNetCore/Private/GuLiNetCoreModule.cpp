#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "GuLiNetTrace.h"

class FGuLiNetCoreModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
#if CPUPROFILERTRACE_ENABLED
        GuLiNet::SetTraceBegin([](const char* Name)->GuLiNet::TraceEnd
        {
            if (!CpuChannel) return nullptr;
            FCpuProfilerTrace::OutputBeginDynamicEvent(Name);
            return &FCpuProfilerTrace::OutputEndEvent;
        });
#endif
    }
    void ShutdownModule() override { GuLiNet::SetTraceBegin(nullptr); }
};
IMPLEMENT_MODULE(FGuLiNetCoreModule, GuLiNetCore)
