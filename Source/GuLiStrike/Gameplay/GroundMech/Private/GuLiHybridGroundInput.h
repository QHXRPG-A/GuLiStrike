#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"

namespace GuLiHybridGround
{
inline void Input(FGuLiHybridStateArchive& A,FGuLiHybridGroundInput& V)
{
    uint32 Version=3; A.Value(Version); if (Version!=3) A.Reject();
    A.Value(V.Axis); A.Value(V.Aim); A.Value(V.bSprint); A.Value(V.bFire); A.Value(V.bRocket); A.Value(V.bHasAim);
    A.Value(V.bFaceAim);
    A.Value(V.bFaceCamera); A.Value(V.CameraYaw);
    if (V.Axis.SizeSquared()>1.00001 || (V.bFire && !V.bHasAim)) A.Reject();
}
inline bool Decode(TConstArrayView<uint8> Bytes,FGuLiHybridGroundInput& Out)
{ FGuLiHybridStateArchive A(Bytes,256); Input(A,Out); return A.Done(); }
}
