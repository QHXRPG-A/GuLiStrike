#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#if defined(GULINETCORE_SHARED)
// UBT's generated API macro expands to engine platform macros. Resolve our ABI
// directly so portable translation units never have to include engine headers.
#undef GULINETCORE_API
#if defined(_WIN32)
#if defined(GULINETCORE_EXPORTS)
#define GULINETCORE_API __declspec(dllexport)
#else
#define GULINETCORE_API __declspec(dllimport)
#endif
#else
#define GULINETCORE_API __attribute__((visibility("default")))
#endif
#elif defined(GULINETCORE_STANDALONE) && !defined(GULINETCORE_API)
#define GULINETCORE_API
#endif

namespace GuLiNet
{
using Bytes = std::vector<std::uint8_t>;
using Tick = std::uint64_t;
using Revision = std::uint64_t;

inline constexpr std::uint16_t ProtocolVersion = 33;
inline constexpr std::uint32_t HandoffVersion = 3;
inline constexpr std::size_t MaxDatagramBytes = 1000;
inline constexpr std::size_t MaxMessageBytes = 4 * 1024 * 1024;
// A private cut retains its full decision history. The native lossless
// transport envelope must still fit MaxMessageBytes before it can be queued.
inline constexpr std::size_t MaxPrivateStateBytes = 16 * 1024 * 1024;
inline constexpr Tick TicksPerSecond = 60;
inline constexpr std::uint32_t MassSimulationHz = 10;
inline constexpr std::uint32_t WingmanSimulationHz = 30;
inline constexpr std::uint32_t WingmanMotionHz = 10;
inline constexpr std::uint32_t StatePublicationHz = 10;

struct GULINETCORE_API Id
{
    std::array<std::uint32_t, 4> Words{};
    bool Valid() const;
    std::string ToString() const;
    static bool Parse(const std::string& Text, Id& Out);
    friend bool operator==(const Id&, const Id&) = default;
    friend bool operator<(const Id& A, const Id& B) { return A.Words < B.Words; }
};

enum class Team : std::uint8_t { None, Red, Blue };
enum class Role : std::uint8_t { Commander, Ground, Air, Observer };
enum class ObjectKind : std::uint8_t
{
    None, Soldier, Wingman, Ship, GroundPlayer, Engineer, Miner, Building,
    Flight, Field, Territory, ResourceNode, BusinessWork
};
// These kinds share UE handle registries. Ordinals identify one life only;
// stable identity is the separate ObjectKey.StableId.
constexpr ObjectKind OrdinalScope(ObjectKind Kind)
{
    return Kind==ObjectKind::Miner ? ObjectKind::Engineer
        : Kind==ObjectKind::Territory ? ObjectKind::Building : Kind;
}
enum class Life : std::uint8_t { Registered, Alive, Dead, Destroyed };
enum class Domain : std::uint8_t { Command, Simulation };
enum class OwnershipPolicy : std::uint8_t { Fixed, CommanderActivity, ControllerPinned, RetainedSource, SharedGroup, ParentWork };
enum class Mode : std::uint8_t { Hybrid = 1 };
enum class Transport : std::uint8_t { LocalUdp, EOS };
enum class RoomPhase : std::uint8_t { Preparing, Running, MembershipChanging, Finished, Aborted };

struct ObjectKey
{
    ObjectKind Kind = ObjectKind::None;
    Id StableId;
    std::uint32_t Generation = 0;
    bool Valid() const { return Kind != ObjectKind::None && StableId.Valid() && Generation != 0; }
    friend bool operator==(const ObjectKey&, const ObjectKey&) = default;
    friend bool operator<(const ObjectKey& A, const ObjectKey& B)
    {
        return std::tie(A.Kind, A.StableId, A.Generation) < std::tie(B.Kind, B.StableId, B.Generation);
    }
};

struct Authority
{
    ObjectKey Object;
    Team Faction = Team::None;
    Id Controller;
    Revision ControlEpoch = 1;
    Id SimOwner;
    Revision OwnerEpoch = 0;
    Life State = Life::Registered;
    Id SourceLease;
    double EstimatedCost = 1;
    // Custody stores a frozen private shard; it never grants simulation or input.
    bool Dormant = false;
    Id StateCustodian;
    OwnershipPolicy Policy = OwnershipPolicy::Fixed;
    Role ControllerSeat = Role::Commander;
    bool RetireOnVacancy = false;
    // Authored ownership domain, independent of faction and input authority.
    Id OwnershipGroup;
    Revision GroupEpoch = 0;
    friend bool operator==(const Authority&, const Authority&) = default;
};

inline bool HasPrivateCustody(const Authority& A, Id Player)
{
    if (!Player.Valid() || !A.OwnerEpoch || A.State==Life::Destroyed) return false;
    if (A.Dormant) return A.Policy==OwnershipPolicy::ControllerPinned && !A.RetireOnVacancy
        && !A.Controller.Valid() && !A.SimOwner.Valid() && A.StateCustodian==Player;
    return A.SimOwner==Player && (A.Policy!=OwnershipPolicy::ControllerPinned || A.Controller==Player);
}

struct Defaults
{
    double ReportSeconds = 1;
    double ConnectTimeoutSeconds = 10;
    double ReadyTimeoutSeconds = 5;
    double MembershipTimeoutSeconds = 15;
    double EmptyTeamTimeoutSeconds = 30;
};

// Retired sequence numbers never become acceptable when the recent set is pruned.
class GULINETCORE_API SequenceWindow
{
public:
    enum class Verdict { Fresh, Duplicate, TooOld, Invalid };
    explicit SequenceWindow(std::uint64_t Width = 4096) : Capacity(Width ? Width : 1) {}
    Verdict Inspect(std::uint64_t Sequence) const;
    Verdict Observe(std::uint64_t Sequence);
    std::uint64_t GetFloor() const { return Floor; }
    std::uint64_t GetHighest() const { return Highest; }
    const std::set<std::uint64_t>& GetRecent() const { return Recent; }
    bool Restore(std::uint64_t NewFloor, std::uint64_t NewHighest, std::set<std::uint64_t> Values);
private:
    std::uint64_t Capacity;
    std::uint64_t Floor = 0;
    std::uint64_t Highest = 0;
    std::set<std::uint64_t> Recent;
};

class GULINETCORE_API BattleClock
{
public:
    void Start(Revision Epoch, Tick At = 0);
    void SetPaused(bool Value) { Paused = Value; if (Value) Accumulator = 0; }
    std::uint32_t Advance(double WallDeltaSeconds, std::uint32_t MaxSteps = 4);
    Tick Now() const { return Current; }
    Revision GetEpoch() const { return MatchEpoch; }
private:
    Revision MatchEpoch = 0;
    Tick Current = 0;
    double Accumulator = 0;
    bool Paused = true;
};

GULINETCORE_API bool Playable(Team Value);
GULINETCORE_API std::uint32_t NextRevision(std::uint32_t Value);
GULINETCORE_API Id DeriveDeathId(const Id& Damage);
GULINETCORE_API Id DeriveRewardId(const Id& Damage);
}
