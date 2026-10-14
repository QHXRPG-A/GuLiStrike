#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"

namespace GuLiHybridShip
{
inline void Input(FGuLiHybridStateArchive& A,FGuLiHybridShipInput& V)
{
    uint32 Version=1; A.Value(Version); if (Version!=1) A.Reject();
    A.Value(V.Thrust); A.Value(V.Turn); A.Value(V.Strafe); A.Value(V.bBoost);
    if (V.Thrust.GetAbsMax()>1.00001 || FMath::Abs(V.Turn)>1.00001 || FMath::Abs(V.Strafe)>1.00001) A.Reject();
}
inline bool Decode(TConstArrayView<uint8> Bytes,FGuLiHybridShipInput& Out)
{ FGuLiHybridStateArchive A(Bytes,128); Input(A,Out); return A.Done(); }
}
