#pragma once
#include "GuLiNetTypes.h"
#include <functional>
#include <memory>

namespace GuLiNet
{
/** Process-shared executor for owned value snapshots. Submission never waits.
 * The byte charge covers queued AND running inputs until the task returns. */
class GULINETCORE_API IGameplayTaskExecutor
{
public:
    virtual ~IGameplayTaskExecutor() = default;
    virtual bool TrySubmit(std::function<void()> Task,std::size_t Bytes) = 0;
    virtual unsigned Concurrency() const = 0;
};
GULINETCORE_API std::shared_ptr<IGameplayTaskExecutor> MakeGameplayTaskPool(
    unsigned Threads,std::function<void(unsigned)> InitializeThread = {});
}
