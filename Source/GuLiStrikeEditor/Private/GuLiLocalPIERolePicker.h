#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"

namespace GuLiLocalPIERolePicker
{
enum class ETrial : uint8 { None, Barrage, WingmanFlight, WingmanAir, WingmanGround, WingmanCombined, XuntianHUD };
inline bool HasAirOpponent(ETrial Trial)
{
    return Trial == ETrial::WingmanAir || Trial == ETrial::WingmanCombined;
}
inline bool IsWingmanTrial(ETrial Trial)
{
    return Trial == ETrial::WingmanFlight || Trial == ETrial::WingmanGround || HasAirOpponent(Trial);
}
/** Editor-only, before session creation. An unset result means the player cancelled. */
TOptional<EGuLiCommanderRole> Show(EGuLiCommanderRole InitialRole, ETrial& Trial);
}
