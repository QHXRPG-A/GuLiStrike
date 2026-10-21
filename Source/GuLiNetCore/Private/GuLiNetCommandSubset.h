#pragma once
#include "GuLiNetClient.h"
#include <utility>

namespace GuLiNet
{
// Construct only the requested members. Copying Source and then replacing its
// list turns per-member delivery into quadratic copying of the full selection.
inline FrozenCommand CommandWithMembers(const FrozenCommand& Source,std::vector<CommandMember> Members)
{
    return {Source.Event,Source.At,Source.Operation,Source.Arguments,std::move(Members)};
}
}
