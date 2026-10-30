#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"

namespace GuLiLocalPIERolePicker
{
/** Editor-only, before session creation. An unset result means the player cancelled. */
TOptional<EGuLiCommanderRole> Show(EGuLiCommanderRole InitialRole);
}
