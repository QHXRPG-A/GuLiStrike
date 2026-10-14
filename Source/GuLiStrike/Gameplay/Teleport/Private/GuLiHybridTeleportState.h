#pragma once
#include "GuLiNetRelocation.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Gameplay/Units/GuLiExternalControlRegistry.h"
namespace GuLiHybridTeleport
{
    namespace Net=GuLiNet::Relocation;
    inline Net::Pose Pose(const FTransform& T)
    {const auto P=T.GetLocation();const auto R=T.Rotator();const auto S=T.GetScale3D();return {P.X,P.Y,P.Z,float(R.Pitch),float(R.Yaw),float(R.Roll),float(S.X),float(S.Y),float(S.Z)};}
    inline FTransform Transform(const Net::Pose& P) {return FTransform(FRotator(P.Pitch,P.Yaw,P.Roll),FVector(P.X,P.Y,P.Z),FVector(P.ScaleX,P.ScaleY,P.ScaleZ));}
    template<class T> bool Read(const FGuLiHybridObjectState& O,const TCHAR* Name,T& S)
    {const auto* C=O.Capabilities.Find(Name);return C&&C->Version==Net::Version&&Net::Decode({C->Data.GetData(),SIZE_T(C->Data.Num())},S);}
    bool Collect(UWorld&,const FGuLiHybridObjectState&,const Net::State&,Net::Command&);
    bool Plan(UWorld&,const Net::State&,Net::Command&,bool Returning);
}
