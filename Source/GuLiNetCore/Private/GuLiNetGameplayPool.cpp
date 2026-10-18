#include "GuLiNetGameplayPool.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace GuLiNet
{
namespace
{
class GameplayTaskPool final : public IGameplayTaskExecutor
{
    struct Job {std::function<void()> Run;std::size_t Bytes;};
    static constexpr std::size_t Limit=64ull*1024*1024;
    std::mutex Mutex;
    std::condition_variable Wake;
    std::deque<Job> Jobs;
    std::vector<std::thread> Threads;
    std::size_t Charged=0;
    bool Stopping=false;
public:
    GameplayTaskPool(unsigned Count,std::function<void(unsigned)> Initialize)
    {
        for (unsigned I=0;I<std::clamp(Count,1u,4u);++I)
            Threads.emplace_back([this,I,Initialize]
            {
                if (Initialize) Initialize(I);
                for (;;)
                {
                    Job Next;
                    {
                        std::unique_lock Lock(Mutex);
                        Wake.wait(Lock,[&]{return Stopping||!Jobs.empty();});
                        if (Jobs.empty()) return;
                        Next=std::move(Jobs.front());Jobs.pop_front();
                    }
                    Next.Run();
                    {std::lock_guard Lock(Mutex);Charged-=Next.Bytes;}
                }
            });
    }
    ~GameplayTaskPool() override
    {
        {std::lock_guard Lock(Mutex);Stopping=true;}
        Wake.notify_all();
        for (auto& Thread:Threads) Thread.join();
    }
    bool TrySubmit(std::function<void()> Task,std::size_t Bytes) override
    {
        if (!Task || Bytes>Limit-128) return false;
        Bytes+=128;
        {std::lock_guard Lock(Mutex);if (Stopping||Charged>Limit-Bytes) return false;
            Charged+=Bytes;Jobs.push_back({std::move(Task),Bytes});}
        Wake.notify_one();return true;
    }
    unsigned Concurrency() const override {return static_cast<unsigned>(Threads.size());}
};
}
std::shared_ptr<IGameplayTaskExecutor> MakeGameplayTaskPool(unsigned Threads,std::function<void(unsigned)> Initialize)
{return std::make_shared<GameplayTaskPool>(Threads,std::move(Initialize));}
}
