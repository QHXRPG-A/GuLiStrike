#pragma once
#include <memory>
namespace GuLiNet { class IGameplayTaskExecutor; }
/** Game-thread acquisition; all Hybrid worlds in this process share one pool. */
std::shared_ptr<GuLiNet::IGameplayTaskExecutor> AcquireGuLiGameplayComputePool(unsigned Workers);
