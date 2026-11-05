#pragma once
#include <atomic>
#include <cstdint>

namespace GuLiNet
{
enum class MassPoseReason : std::uint8_t { First, Fields, Animation, Retry, Heartbeat, Count };

// Local to one recipient and one admission. Only the transport writes a send
// time. Both the gameplay worker and the game thread can observe the outcome.
struct MotionDelivery
{
    static constexpr double Pending = -1.0, Discarded = -2.0;
    explicit MotionDelivery(MassPoseReason InReason=MassPoseReason::Count) : Reason(InReason) {}
    const MassPoseReason Reason;
    std::atomic<double> SentAt{Pending};

    void DiscardPending()
    {
        double Expected = Pending;
        SentAt.compare_exchange_strong(Expected, Discarded);
    }
};
}
