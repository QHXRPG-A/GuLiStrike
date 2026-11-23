#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"
#include "GuLiNetEffects.h"
#include "Gameplay/GroundMech/Network/GuLiHybridGroundInputTypes.h"

namespace GuLiHybridGround
{
inline void Input(FGuLiHybridStateArchive& A,FGuLiHybridGroundInput& V)
{
    uint32 Version=8; A.Value(Version); if (Version!=8 && Version!=7) A.Reject();
    A.Value(V.Axis); A.Value(V.Aim); A.Value(V.bSprint); A.Value(V.bFire); A.Value(V.bRocket); A.Value(V.bHasAim);
    A.Value(V.bFaceAim);
    A.Value(V.BlinkRequest); A.Value(V.BlinkYaw);
    uint8 Space=uint8(V.MoveSpace),AimMode=uint8(V.WeaponAimMode);
    A.Value(Space); A.Value(AimMode);
    V.MoveSpace=EGuLiGroundMoveSpace(Space); V.WeaponAimMode=EGuLiGroundWeaponAimMode(AimMode);
    if(A.IsReading())V.Lock={};
    if(Version>=8)
    {
        auto& lock=V.Lock;
        A.Array(lock.Candidates,GuLiNet::Effects::MaximumLockTargets,[](auto& ar,auto& key){ar.Value(key);if(!key.IsValid())ar.Reject();});
        if(!lock.Candidates.IsEmpty())
        {
            TSet<FGuLiHybridObjectKey> unique;
            for(const auto& key:lock.Candidates)unique.Add(key);
            A.Value(lock.Origin); A.Value(lock.Forward); A.Value(lock.Right); A.Value(lock.Focal); A.Value(lock.Offset);
            if(unique.Num()!=lock.Candidates.Num() || !lock.Forward.IsNormalized() || !lock.Right.IsNormalized() ||
               FMath::Abs(FVector::DotProduct(lock.Forward,lock.Right))>.001 || lock.Focal.X<=0 || lock.Focal.Y>=0 ||
               !V.bHasAim || AimMode!=0)A.Reject();
        }
    }
    if (Space>1 || AimMode>1 || V.Axis.SizeSquared()>1.00001
        || (V.bFire && !V.bHasAim && V.WeaponAimMode!=EGuLiGroundWeaponAimMode::Forward)) A.Reject();
}
inline bool Decode(TConstArrayView<uint8> Bytes,FGuLiHybridGroundInput& Out)
{ FGuLiHybridStateArchive A(Bytes,1024); Input(A,Out); return A.Done(); }
}
