#pragma once

// Private bridge: portable types must not leak into reflected/public contracts.
#include "GuLiNetRules.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Economy/GuLiEconomyTypes.h"

namespace GuLiNetBridge
{
inline GuLiNet::Id ToCore(const FGuid& Value) { return {{Value.A, Value.B, Value.C, Value.D}}; }
inline FGuid ToUE(const GuLiNet::Id& Value)
{ return FGuid(Value.Words[0], Value.Words[1], Value.Words[2], Value.Words[3]); }
inline GuLiNet::Team ToCore(EGuLiTeam Value)
{ return Value == EGuLiTeam::Red ? GuLiNet::Team::Red : Value == EGuLiTeam::Blue ? GuLiNet::Team::Blue : GuLiNet::Team::None; }
inline EGuLiTeam ToUE(GuLiNet::Team Value)
{ return Value == GuLiNet::Team::Red ? EGuLiTeam::Red : Value == GuLiNet::Team::Blue ? EGuLiTeam::Blue : EGuLiTeam::Unassigned; }
inline GuLiNet::Health ToCore(const FGuLiCombatHealthState& S)
{ return {S.Health, S.MaxHealth, S.Shield, S.MaxShield, S.Revision, S.bDead}; }
inline void Apply(const GuLiNet::Health& S, FGuLiCombatHealthState& Out)
{
    Out.Health = S.Value; Out.MaxHealth = S.Maximum; Out.Shield = S.Shield;
    Out.MaxShield = S.MaximumShield; Out.Revision = S.Version; Out.bDead = S.Dead;
}
inline GuLiNet::Resources ToCore(const FGuLiResourceAmounts& S) { return {S.Blue, S.Red, S.Revision}; }
inline FGuLiResourceAmounts ToUE(const GuLiNet::Resources& S)
{ FGuLiResourceAmounts Out; Out.Blue = S.Blue; Out.Red = S.Red; Out.Revision = S.Version; return Out; }
}
