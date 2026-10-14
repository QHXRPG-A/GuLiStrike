#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"

namespace GuLiHybridState
{
inline void TaskCommand(FGuLiHybridStateArchive& A,FGuLiUnitTaskCommand& V,float& Pitch,float& Radius)
{
    uint32 Version=1; A.Value(Version); if (Version!=1) A.Reject();
    A.Value(V.CommandId); A.Value(V.SelectionRevision);
    A.Enum(V.Disposition,uint8(EGuLiTaskDisposition::Stop)); A.Enum(V.Kind,uint8(EGuLiUnitTaskKind::Transit));
    A.Value(V.SpecialTaskId); FVector Target=V.Target; A.Value(Target); if (A.IsReading()) V.Target=Target;
    A.Value(V.BuildingId); A.Value(V.ClusterId); A.Value(V.TerritoryId); A.Value(V.bGroundMoveOnly); A.Value(V.TargetIntent);
    A.Value(Pitch); A.Value(Radius);
    if (!V.IsWellFormed() || Pitch<0 || Radius<0) A.Reject();
}
inline bool EncodeTaskCommand(FGuLiUnitTaskCommand V,float Pitch,float Radius,TArray<uint8>& Out)
{ FGuLiHybridStateArchive A(65536); TaskCommand(A,V,Pitch,Radius); return A.Finish(Out); }
inline bool DecodeTaskCommand(TConstArrayView<uint8> Bytes,FGuLiUnitTaskCommand& V,float& Pitch,float& Radius)
{ FGuLiHybridStateArchive A(Bytes,65536); TaskCommand(A,V,Pitch,Radius); return A.Done(); }
}
