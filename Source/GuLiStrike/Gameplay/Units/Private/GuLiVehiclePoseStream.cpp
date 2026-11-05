#include "Gameplay/Units/Private/GuLiVehiclePoseStream.h"
#include "Gameplay/Units/Private/GuLiHybridEngineeringState.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
constexpr uint8 PositionField=1,RotationField=2,VelocityField=4,TransitField=8,WorkField=16;
constexpr uint32 MaxFieldBytes=65536;
const FGuLiVehiclePose DefaultPose;

uint8 Changes(const FGuLiVehiclePose& P,const FGuLiVehiclePose& B)
{
    return (P.Position!=B.Position?PositionField:0) | (P.Rotation!=B.Rotation?RotationField:0)
        | (P.Velocity!=B.Velocity?VelocityField:0) | (P.Transit!=B.Transit?TransitField:0) | (P.Work!=B.Work?WorkField:0);
}
void WriteVector(GuLiNet::Writer& W,const FVector& V) {W.F64(V.X);W.F64(V.Y);W.F64(V.Z);}
bool ReadVector(GuLiNet::Reader& R,FVector& V) {return R.F64(V.X)&&R.F64(V.Y)&&R.F64(V.Z);}
void WriteSection(GuLiNet::Writer& W,const TArray<uint8>& Data)
{
    W.VarUnsigned(Data.Num()); W.Raw({Data.GetData(),static_cast<size_t>(Data.Num())});
}
bool ReadSection(GuLiNet::Reader& R,TArray<uint8>& Data)
{
    uint64 Size=0; std::span<const uint8> Bytes;
    if (!R.VarUnsigned(Size) || Size>MaxFieldBytes || !R.Raw(static_cast<size_t>(Size),Bytes)) return false;
    Data.Reset(); if (Size) Data.Append(Bytes.data(),static_cast<int32>(Size)); return true;
}
bool Encode(const FGuLiVehiclePose& P,const FGuLiVehiclePose& Base,uint64 Distance,TArray<uint8>& Out)
{
    GuLiNet::Writer W(MaxFieldBytes);
    const uint8 Mask=Changes(P,Base);
    W.VarUnsigned(Distance); W.U8(Mask);
    if (Mask&PositionField) WriteVector(W,P.Position);
    if (Mask&RotationField) {W.F64(P.Rotation.X);W.F64(P.Rotation.Y);W.F64(P.Rotation.Z);W.F64(P.Rotation.W);}
    if (Mask&VelocityField) WriteVector(W,P.Velocity);
    if (Mask&TransitField) WriteSection(W,P.Transit);
    if (Mask&WorkField) WriteSection(W,P.Work);
    if (!W.Good()) return false;
    Out.Reset(); Out.Append(W.Data().data(),static_cast<int32>(W.Data().size())); return true;
}
void Reply(UWorld& World,const FGuLiHybridObjectState& Object,uint64 At)
{
    GuLiNet::Writer W(10); W.VarUnsigned(At);
    TArray<uint8> Bytes; Bytes.Append(W.Data().data(),static_cast<int32>(W.Data().size()));
    World.GetSubsystem<UGuLiHybridWorldSubsystem>()->PublishMotionAcknowledgment(Object.Key,Bytes);
}
}

bool FGuLiVehiclePose::SetTransit(FGuLiStrongholdTransitState State)
{
    FGuLiHybridStateArchive A(MaxFieldBytes); GuLiHybridEngineeringState::Transit(A,State);
    return A.Finish(Transit);
}
bool FGuLiVehiclePose::GetTransit(FGuLiStrongholdTransitState& Out) const
{
    FGuLiHybridStateArchive A(Transit,MaxFieldBytes); GuLiHybridEngineeringState::Transit(A,Out); return A.Done();
}
void FGuLiVehiclePoseStream::FStream::Bind(const FGuLiHybridObjectState& Object,const FGuid& Pair,uint64 MatchEpoch)
{
    if (Key==Object.Key && Owner==Object.OwnerEpoch && Control==Object.ControlEpoch && Connection==Pair && Epoch==MatchEpoch) return;
    *this=FStream{}; Key=Object.Key; Owner=Object.OwnerEpoch; Control=Object.ControlEpoch; Connection=Pair; Epoch=MatchEpoch;
}
const FGuLiVehiclePoseStream::FEntry* FGuLiVehiclePoseStream::FStream::Find(uint64 At) const
{
    return At ? History.FindByPredicate([At](const FEntry& E){return E.At==At;}) : nullptr;
}
void FGuLiVehiclePoseStream::FStream::Store(FEntry E)
{
    Latest=E.At;
    if (History.Num()<32) History.Add(MoveTemp(E));
    else {History[Next]=MoveTemp(E); Next=(Next+1)%32;}
}
bool FGuLiVehiclePoseStream::Publish(UWorld& World,const FGuLiHybridObjectState& Object,uint64 At,const FGuLiVehiclePose& Pose,bool WorkLease)
{
    auto* Session=World.GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
    auto* Hybrid=World.GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Session || !Hybrid || !At) return false;
    TSharedPtr<const FGuLiVehiclePose> Shared;
    bool Success=true; const double Now=FPlatformTime::Seconds();
    for (const auto& Peer:Session->GetMembers())
    {
        if (!Peer.bReady || !Peer.bOnline || Peer.Player==Session->GetLocalPlayerId() || !Peer.Connection.IsValid()) continue;
        auto& S=Senders.FindOrAdd(Peer.Player); S.Bind(Object,Peer.Connection,Session->GetMatchEpoch());
        if (At<=S.Latest) continue;
        const auto* Last=S.Find(S.Latest);
        const bool NewState=!Last || Changes(Pose,*Last->Pose)!=0;
        if (!NewState && !S.ForceFull)
        {
            const double Sent=Last->Delivery->SentAt.load();
            const double Period=WorkLease || S.Confirmed<S.FirstOfState ? .2 : .5;
            if (Sent==GuLiNet::MotionDelivery::Pending || (Sent>=0 && Now-Sent<Period)) continue;
        }
        const auto* Base=!S.ForceFull && S.Confirmed && At-S.Confirmed<=120 ? S.Find(S.Confirmed) : nullptr;
        TArray<uint8> Body;
        auto Delivery=std::make_shared<GuLiNet::MotionDelivery>();
        if (!Encode(Pose,Base?*Base->Pose:DefaultPose,Base?At-Base->At:0,Body)
            || !Hybrid->PublishMotionTo(Object.Key,Peer.Player,Body,At,Peer.Connection,Delivery,Pose.Velocity.IsZero()))
        {Success=false; continue;}
        if (!Shared) Shared=MakeShared<FGuLiVehiclePose>(Pose);
        if (NewState) S.FirstOfState=At;
        S.ForceFull=false; S.Store({At,Shared,MoveTemp(Delivery)});
    }
    return Success;
}
FGuLiVehiclePoseStream::ERead FGuLiVehiclePoseStream::Read(UWorld& World,const FGuLiHybridObjectState& Object,uint64 At,
    TConstArrayView<uint8> Bytes,FGuLiVehiclePose& Out)
{
    auto* Session=World.GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
    if (!Session || !At || Bytes.Num()>MaxFieldBytes) return ERead::Invalid;
    const auto Pair=Session->GetPeerConnection(Object.SimulationOwner);
    if (!Pair.IsValid()) return ERead::Invalid;
    Receiver.Bind(Object,Pair,Session->GetMatchEpoch());
    GuLiNet::Reader R({Bytes.GetData(),static_cast<size_t>(Bytes.Num())});
    uint64 Distance=0; uint8 Mask=0;
    if (!R.VarUnsigned(Distance) || Distance>120 || Distance>=At || !R.U8(Mask) || Mask>31) return ERead::Invalid;
    const auto* Base=Receiver.Find(At-Distance);
    if (Distance && !Base) {Reply(World,Object,0);return ERead::MissingBaseline;}
    Out=Distance ? *Base->Pose : DefaultPose;
    if ((Mask&PositionField) && !ReadVector(R,Out.Position)) return ERead::Invalid;
    if ((Mask&RotationField) && (!R.F64(Out.Rotation.X)||!R.F64(Out.Rotation.Y)||!R.F64(Out.Rotation.Z)||!R.F64(Out.Rotation.W)
        || FMath::Abs(Out.Rotation.SizeSquared()-1.)>.001)) return ERead::Invalid;
    if ((Mask&VelocityField) && !ReadVector(R,Out.Velocity)) return ERead::Invalid;
    if ((Mask&TransitField) && !ReadSection(R,Out.Transit)) return ERead::Invalid;
    if ((Mask&WorkField) && !ReadSection(R,Out.Work)) return ERead::Invalid;
    return R.Done() ? ERead::Ready : ERead::Invalid;
}
void FGuLiVehiclePoseStream::Applied(UWorld& World,const FGuLiHybridObjectState& Object,uint64 At,FGuLiVehiclePose Pose)
{
    Receiver.Store({At,MakeShared<FGuLiVehiclePose>(MoveTemp(Pose)),{}});
    Reply(World,Object,At);
}
bool FGuLiVehiclePoseStream::Feedback(UWorld& World,const FGuLiHybridObjectState& Object,const FGuid& Peer,TConstArrayView<uint8> Bytes)
{
    auto* S=Senders.Find(Peer);
    auto* Session=World.GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
    if (!S || !Session) return false;
    S->Bind(Object,Session->GetPeerConnection(Peer),Session->GetMatchEpoch());
    GuLiNet::Reader R({Bytes.GetData(),static_cast<size_t>(Bytes.Num())});
    uint64 At=0; if (!R.VarUnsigned(At) || !R.Done()) return false;
    if (!At) {S->Confirmed=0;S->ForceFull=true;return true;}
    if (At<=S->Confirmed) return true;
    const auto* E=S->Find(At);
    if (!E) return At<=S->Latest;
    if (!E->Delivery || E->Delivery->SentAt.load()<0) return false;
    S->Confirmed=At;return true;
}
