#include "GuLiNetTypes.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace GuLiNet
{
bool Id::Valid() const { return Words[0] || Words[1] || Words[2] || Words[3]; }
std::string Id::ToString() const
{
    char Text[33]{};
    std::snprintf(Text, sizeof(Text), "%08x%08x%08x%08x", Words[0], Words[1], Words[2], Words[3]);
    return Text;
}
bool Id::Parse(const std::string& Text, Id& Out)
{
    if (Text.size() != 32) return false;
    Id Parsed;
    for (std::size_t Index = 0; Index != 4; ++Index)
    {
        const char* Start = Text.data() + Index * 8;
        const auto Result = std::from_chars(Start, Start + 8, Parsed.Words[Index], 16);
        if (Result.ec != std::errc{} || Result.ptr != Start + 8) return false;
    }
    Out = Parsed;
    return true;
}
bool Playable(Team Value) { return Value == Team::Red || Value == Team::Blue; }
std::uint32_t NextRevision(std::uint32_t Value) { return Value == UINT32_MAX ? 1u : Value + 1u; }
Id DeriveDeathId(const Id& Damage)
{
    if (!Damage.Valid()) return {};
    Id Result = Damage;
    Result.Words[0] ^= 0x44454154u;
    Result.Words[1] ^= 0x485f4556u;
    Result.Words[2] ^= 0x454e545fu;
    Result.Words[3] ^= 0x49445f31u;
    if (!Result.Valid()) Result.Words[3] = 1;
    return Result;
}
Id DeriveRewardId(const Id& Damage)
{
    if (!Damage.Valid()) return {};
    Id Result = Damage;
    Result.Words[0] ^= 0x52455741u;
    Result.Words[1] ^= 0x52445f45u;
    Result.Words[2] ^= 0x56454e54u;
    Result.Words[3] ^= 0x5f494431u;
    if (!Result.Valid()) Result.Words[3] = 1;
    return Result;
}
SequenceWindow::Verdict SequenceWindow::Inspect(std::uint64_t Sequence) const
{
    if (!Sequence) return Verdict::Invalid;
    if (Sequence <= Floor) return Verdict::TooOld;
    return Recent.contains(Sequence) ? Verdict::Duplicate : Verdict::Fresh;
}
SequenceWindow::Verdict SequenceWindow::Observe(std::uint64_t Sequence)
{
    const auto Result = Inspect(Sequence);
    if (Result != Verdict::Fresh) return Result;
    Highest = std::max(Highest, Sequence);
    Floor = Highest > Capacity ? Highest - Capacity : 0;
    Recent.erase(Recent.begin(), Recent.upper_bound(Floor));
    Recent.insert(Sequence);
    return Result;
}
bool SequenceWindow::Restore(std::uint64_t NewFloor, std::uint64_t NewHighest, std::set<std::uint64_t> Values)
{
    if (NewHighest < NewFloor || NewHighest - NewFloor > Capacity || Values.size() > Capacity) return false;
    if (!Values.empty() && (*Values.begin() <= NewFloor || *Values.rbegin() > NewHighest)) return false;
    Floor = NewFloor; Highest = NewHighest; Recent = std::move(Values); return true;
}
void BattleClock::Start(Revision Epoch, Tick At)
{
    MatchEpoch = Epoch; Current = At; Accumulator = 0; Paused = true;
}
std::uint32_t BattleClock::Advance(double WallDeltaSeconds, std::uint32_t MaxSteps)
{
    if (Paused || !MaxSteps || !std::isfinite(WallDeltaSeconds) || WallDeltaSeconds < 0) return 0;
    Accumulator = std::min(Accumulator + WallDeltaSeconds, 4.0 / TicksPerSecond);
    const auto Steps = static_cast<std::uint32_t>(std::min<Tick>(MaxSteps,
        static_cast<Tick>(Accumulator * TicksPerSecond + 1e-9)));
    Current += Steps; Accumulator -= static_cast<double>(Steps) / TicksPerSecond;
    return Steps;
}
}
