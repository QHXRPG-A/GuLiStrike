#include "Network/Private/GuLiGameplayComputePool.h"
#include "GuLiNetGameplayPool.h"
#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.h"

std::shared_ptr<GuLiNet::IGameplayTaskExecutor> AcquireGuLiGameplayComputePool(unsigned Workers)
{
    check(IsInGameThread());
    static std::weak_ptr<GuLiNet::IGameplayTaskExecutor> Shared;
    if (auto Pool=Shared.lock())
    {
        ensureMsgf(Pool->Concurrency()==Workers,TEXT("A live process pool keeps its startup worker count."));
        return Pool;
    }
    auto Pool=GuLiNet::MakeGameplayTaskPool(Workers,[](unsigned Index)
    {
        const FString Name=FString::Printf(TEXT("GuLiGameplay-%u"),Index);
        FPlatformProcess::SetThreadName(*Name);
        UE::Trace::ThreadRegister(*Name,FPlatformTLS::GetCurrentThreadId(),0);
    });
    Shared=Pool;return Pool;
}
