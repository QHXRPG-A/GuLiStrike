#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/Private/GuLiHybridMessageConversions.h"
#include "Network/Private/GuLiHybridTransport.h"
#include "Network/GuLiHybridManifest.h"
#include "GuLiNetGameplayTypes.h"
#include "GuLiNetOwnerState.h"
#include "GuLiNetHash.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "Containers/Queue.h"
#include <atomic>
#include <memory>

namespace
{
enum class ESocketEvent : uint8 { Connected, Message, Failed };
struct FSocketEvent { ESocketEvent Kind; TArray<uint8> Bytes; FString Error; };
struct FMailbox
{
    FCriticalSection Lock;
    TArray<uint8> Partial;
    TQueue<FSocketEvent, EQueueMode::Mpsc> Events;
    std::atomic<SIZE_T> QueuedBytes{0};
    std::atomic<bool> Overflow{false};
    std::atomic<uint64> ReceivedApplicationBytes{0};
};
GuLiNet::Role ToRole(EGuLiCommanderRole V)
{
    return V == EGuLiCommanderRole::Ground ? GuLiNet::Role::Ground
        : V == EGuLiCommanderRole::Air ? GuLiNet::Role::Air
        : V == EGuLiCommanderRole::Observer ? GuLiNet::Role::Observer : GuLiNet::Role::Commander;
}
EGuLiCommanderRole FromRole(uint8 V)
{
    return V == static_cast<uint8>(GuLiNet::Role::Ground) ? EGuLiCommanderRole::Ground
        : V == static_cast<uint8>(GuLiNet::Role::Air) ? EGuLiCommanderRole::Air
        : V == static_cast<uint8>(GuLiNet::Role::Observer) ? EGuLiCommanderRole::Observer : EGuLiCommanderRole::Commander;
}
}
struct UGuLiHybridSessionSubsystem::FState
{
    FGuLiNetworkLaunchConfig Config;
    FGuLiTransportIdentity Identity;
    TSharedPtr<IGuLiHybridTransport> Transport;
    TSharedPtr<IWebSocket> Socket;
    TSharedPtr<FMailbox, ESPMode::ThreadSafe> Mailbox;
    std::map<GuLiNet::Id, std::unique_ptr<GuLiNet::PeerChannel>> Channels;
    std::map<GuLiNet::Id, GuLiNet::Id> PairKeys;
    struct FMotionSlot {GuLiNet::Message Latest;double NextSend=0;bool Pending=false;};
    std::map<std::tuple<GuLiNet::Id,GuLiNet::ObjectKey,GuLiNet::MessageKind>,FMotionSlot> MotionOutbox;
    struct FMetadataDelivery { GuLiNet::Message Request; double SentAt=0; };
    std::map<std::pair<GuLiNet::MessageKind,GuLiNet::ObjectKey>,FMetadataDelivery> MetadataOutbox;
    uint64 MotionOutboxBytes=0;
    uint64 SubmittedControlBytes=0;
    std::map<GuLiNet::Id,double> LastPeerSeen;
    TArray<FGuLiHybridMember> Members;
    FString Error, ConfigHash, MapHash;
    uint64 Epoch = 0, NextControlSequence = 1;
    uint64 Generation = 0;
    double Started = 0, LastControl = 0, LastHeartbeat = 0, SimulationCost = 0;
    uint64 SimulationFrames=0;
    double ReviewLoadCost=-1;
    double NextRetry = 0, RetryDelay = 1;
    uint64 ControlInterruptions=0,ControlReconnects=0;
    FString LastInterruption;
    bool Active = false, Welcomed = false, FailureNotified = false, Reconnecting = false;
    struct FReviewFault
    {
        FGuid Operation;
        FName Mode;
        int32 Remaining=0;
        uint16 KindFilter=0;
        int32 BusinessKind=-1;
        uint64 Intercepted=0,Sent=0,Released=0,Bytes=0;
        double Armed=0,PacketAt=0;
        FGuid PacketPeer;
        TArray<uint8> Packet;
        TArray<TPair<FGuid,FGuLiHybridMessage>> Controls;
        TArray<TPair<FGuid,FGuLiHybridMessage>> Receipts;
        TArray<TSharedPtr<FJsonValue>> Evidence;
        bool Accepts(uint16 Kind) const {return Remaining>0 && (!KindFilter || KindFilter==Kind);}
        bool MatchesBusiness(const GuLiNet::Message& M) const
        {
            if (BusinessKind<0) return true;
            if (M.Header.Kind==GuLiNet::MessageKind::HitFact) return BusinessKind==int32(GuLiNet::TransactionKind::Damage);
            if (M.Header.Kind==GuLiNet::MessageKind::TransactionResult)
            {GuLiNet::CommitResult R;return GuLiNet::DecodeCommit(M.Body,R) && int32(R.Kind)==BusinessKind;}
            if (M.Header.Kind==GuLiNet::MessageKind::WorkPrepare)
            {GuLiNet::OwnerWork W;return GuLiNet::DecodeOwnerWork(M.Body,W) && int32(W.Request.Kind)==BusinessKind;}
            GuLiNet::TransactionRequest R;return M.Header.Kind==GuLiNet::MessageKind::Transaction
                && GuLiNet::DecodeTransaction(M.Header,M.Body,R) && int32(R.Kind)==BusinessKind;
        }
        void Record(const GuLiNet::Message& M)
        {
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),UTF8_TO_TCHAR(M.Header.Event.ToString().c_str()));
            J->SetNumberField(TEXT("kind"),uint16(M.Header.Kind));J->SetNumberField(TEXT("epoch"),double(M.Header.Epoch));
            J->SetNumberField(TEXT("owner_version"),double(M.Header.OwnerEpoch));J->SetNumberField(TEXT("generation"),M.Header.Object.Generation);
            Evidence.Add(MakeShared<FJsonValueObject>(J));
        }
    };
    FReviewFault Fault;
    uint64 ReviewEpochRejected=0,ReviewIdentityRejected=0;
    bool SendDatagram(const FGuid& Peer,std::span<const uint8> Packet)
    {
        auto Send=[&](std::span<const uint8> P){return Transport->Send(Peer,MakeArrayView(P.data(),int32(P.size())));};
        GuLiNet::Lane Lane;
        if (Fault.Mode==TEXT("handoff_hold") && GuLiNet::InspectDatagramLane(Packet,Lane) && Lane==GuLiNet::Lane::Handoff)
        {++Fault.Intercepted;return false;}
        if (Fault.Mode==TEXT("datagram_reorder") && (Fault.Remaining>0 || !Fault.Packet.IsEmpty()))
        {
            if (Fault.Packet.IsEmpty())
            {
                Fault.Packet.Append(Packet.data(),int32(Packet.size()));Fault.PacketPeer=Peer;Fault.PacketAt=FPlatformTime::Seconds();
                --Fault.Remaining;++Fault.Intercepted;return true;
            }
            if (Peer==Fault.PacketPeer)
            {
                if (!Send(Packet)) return false;
                if (!Transport->Send(Fault.PacketPeer,Fault.Packet)) return false;
                Fault.Packet.Reset();Fault.Sent+=2;++Fault.Released;return true;
            }
        }
        if (!Send(Packet)) return false;
        if (Fault.Mode==TEXT("datagram_duplicate") && Fault.Remaining>0)
        {
            --Fault.Remaining;++Fault.Intercepted;
            if (Send(Packet)) ++Fault.Sent;
        }
        return true;
    }
};
void UGuLiHybridSessionSubsystem::FStateDeleter::operator()(FState* Value) const { delete Value; }
UGuLiHybridSessionSubsystem::UGuLiHybridSessionSubsystem() : State(new FState) {}
UGuLiHybridSessionSubsystem::~UGuLiHybridSessionSubsystem() = default;
void UGuLiHybridSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::NetworkTick));
}
void UGuLiHybridSessionSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker); Stop();
    OnControl.Clear(); OnPeer.Clear(); OnFailure.Clear(); OnRosterChanged.Clear();
    Super::Deinitialize();
}
bool UGuLiHybridSessionSubsystem::Start(const FGuLiNetworkLaunchConfig& Config, FString& Error)
{
    Stop(); State->Config = Config; State->Error.Reset(); State->ConfigHash.Reset(); State->MapHash.Reset();
    auto Reject = [&](const TCHAR* Why) { Error = Why; State->Error = Error; return false; };
    if (!Config.ValidationError.IsEmpty()) return Reject(*Config.ValidationError);
    if (Config.Mode != EGuLiNetworkMode::Hybrid || !Config.RoomId.IsValid()) return Reject(TEXT("Hybrid requires an explicit valid room identity"));
    if (Config.Transport == EGuLiNetworkTransport::LocalUdp && !Config.CoordinatorUrl.StartsWith(TEXT("ws://127.0.0.1:")))
        return Reject(TEXT("LocalUdp development authentication is restricted to loopback"));
    if (Config.Transport == EGuLiNetworkTransport::EOS && !Config.CoordinatorUrl.StartsWith(TEXT("wss://"))
        && !Config.CoordinatorUrl.StartsWith(TEXT("ws://127.0.0.1:"))) return Reject(TEXT("Public EOS coordinator connections require WSS"));
    FString MapPackage, ManifestError;
    if (!FGuLiHybridManifest::ValidateFile(Config.ManifestFile, State->ConfigHash, State->MapHash, MapPackage, ManifestError))
        return Reject(*ManifestError);
    if (!GetWorld() || UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != MapPackage)
        return Reject(TEXT("The local world does not match the exported Hybrid map package"));
    State->Active = true; State->FailureNotified = false; State->Started = State->LastControl = FPlatformTime::Seconds();
    State->Mailbox = MakeShared<FMailbox, ESPMode::ThreadSafe>();
    State->Transport = Config.Transport == EGuLiNetworkTransport::EOS ? MakeGuLiEosTransport() : MakeGuLiLocalUdpTransport();
    const auto Weak = TWeakObjectPtr<UGuLiHybridSessionSubsystem>(this); const uint64 Generation = State->Generation;
    State->Transport->Start(Config, [Weak, Generation](const FGuLiTransportIdentity& Identity)
    {
        auto* Self = Weak.Get(); if (!Self || !Self->State->Active || Self->State->Generation != Generation) return;
        auto& S = *Self->State; S.Identity = Identity;
        if (S.Error.IsEmpty() && !S.Socket && !S.Reconnecting) Self->ConnectCoordinator();
    }, [Weak, Generation](const FString& Why)
    { if (auto* Self = Weak.Get(); Self && Self->State->Active && Self->State->Generation == Generation) Self->Fail(Why); });
    if (!State->Error.IsEmpty()) { Error = State->Error; return false; } return true;
}
bool UGuLiHybridSessionSubsystem::RequestLeave()
{
    if (!IsConnected()) return false;
    GuLiNet::Message V;V.Header.Kind=GuLiNet::MessageKind::Leave;return SendControl(GuLiNetBridge::ToUE(V));
}
void UGuLiHybridSessionSubsystem::Stop()
{
    ++State->Generation; State->Active = false; State->Welcomed = false;
    State->Reconnecting = false; State->NextRetry = 0; State->RetryDelay = 1;
    CloseCoordinator();
    if (State->Transport) { State->Transport->Stop(); State->Transport.Reset(); }
    State->Channels.clear(); State->PairKeys.clear(); State->LastPeerSeen.clear();State->MotionOutbox.clear();
    State->MetadataOutbox.clear();State->MotionOutboxBytes=0;
    State->Members.Reset(); State->Mailbox.Reset(); State->Identity = {};
    State->SimulationCost = 0;State->SimulationFrames=0;State->ReviewLoadCost=-1; State->LastHeartbeat = 0;
    State->SubmittedControlBytes=0;
    State->Epoch = 0; State->NextControlSequence = 1;
    State->ControlInterruptions=State->ControlReconnects=0;State->LastInterruption.Reset();
    State->Fault={};State->ReviewEpochRejected=State->ReviewIdentityRejected=0;
}
void UGuLiHybridSessionSubsystem::CloseCoordinator()
{
    if (State->Socket)
    {
        State->Socket->OnConnected().Clear(); State->Socket->OnConnectionError().Clear(); State->Socket->OnClosed().Clear();
        State->Socket->OnBinaryMessage().Clear(); State->Socket->Close(); State->Socket.Reset();
    }
    State->Mailbox.Reset();
}
void UGuLiHybridSessionSubsystem::ConnectCoordinator()
{
    if (!State->Active || State->Socket || !State->Identity.Player.IsValid()
        || (!State->Error.IsEmpty() && !State->Reconnecting)) return;
    State->Started = State->LastControl = FPlatformTime::Seconds();
    State->Mailbox = MakeShared<FMailbox, ESPMode::ThreadSafe>();
    State->Socket = FWebSocketsModule::Get().CreateWebSocket(State->Config.CoordinatorUrl);
    const auto Mailbox = State->Mailbox;
    State->Socket->OnConnected().AddLambda([Mailbox]() { Mailbox->Events.Enqueue({ESocketEvent::Connected, {}, {}}); });
    State->Socket->OnConnectionError().AddLambda([Mailbox](const FString& Error)
    { Mailbox->Events.Enqueue({ESocketEvent::Failed, {}, Error}); });
    State->Socket->OnClosed().AddLambda([Mailbox](int32, const FString& Reason, bool)
    { Mailbox->Events.Enqueue({ESocketEvent::Failed, {}, FString(TEXT("Coordinator connection closed: ")) + Reason}); });
    // OnRawMessage's remaining byte count only covers the current WebSocket
    // fragment. A coordinator directory can span several fragments.
    State->Socket->OnBinaryMessage().AddLambda([Mailbox](const void* Data, SIZE_T Size, bool bIsLastFragment)
    {
        Mailbox->ReceivedApplicationBytes.fetch_add(Size);
        FScopeLock Guard(&Mailbox->Lock);
        if (Mailbox->Overflow.load()) return;
        if (Size > GuLiNet::MaxMessageBytes
            || static_cast<SIZE_T>(Mailbox->Partial.Num()) + Size > GuLiNet::MaxMessageBytes)
        { Mailbox->Overflow = true; Mailbox->Partial.Reset(); return; }
        Mailbox->Partial.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
        if (bIsLastFragment)
        {
            const SIZE_T SizeNow = Mailbox->Partial.Num();
            if (Mailbox->QueuedBytes.fetch_add(SizeNow) + SizeNow > 16 * 1024 * 1024)
            { Mailbox->Overflow = true; Mailbox->Partial.Reset(); return; }
            Mailbox->Events.Enqueue({ESocketEvent::Message, MoveTemp(Mailbox->Partial), {}}); Mailbox->Partial.Reset();
        }
    });
    State->Socket->Connect();
}
void UGuLiHybridSessionSubsystem::InterruptConnection(const FString& Error)
{
    if (!State->Active || (!State->Error.IsEmpty() && !State->Reconnecting)) return;
    const double Now = FPlatformTime::Seconds();
    ++State->ControlInterruptions;State->LastInterruption=Error;
    UE_LOG(LogNet,Warning,TEXT("Hybrid control reconnect: %s (last receive %.3fs ago, metadata pending=%llu)"),
        *Error,Now-State->LastControl,uint64(State->MetadataOutbox.size()));
    if (!State->Reconnecting)
    {
        State->Reconnecting = true;
        State->RetryDelay = 1; State->FailureNotified = false;
    }
    State->Error = Error; State->Welcomed = false; State->NextRetry = Now + State->RetryDelay;
    State->RetryDelay = FMath::Min(State->RetryDelay * 2, 4.0); CloseCoordinator();
}
void UGuLiHybridSessionSubsystem::Fail(const FString& Error)
{
    if (!State->Error.IsEmpty() && !State->Reconnecting) return;
    State->Error = Error; State->Welcomed = false; State->Reconnecting = false; State->FailureNotified = false;
    CloseCoordinator();
    // Existing ownership keeps running when only the coordinator is unavailable.
}
bool UGuLiHybridSessionSubsystem::IsConnected() const { return State->Active && State->Welcomed && State->Error.IsEmpty(); }
bool UGuLiHybridSessionSubsystem::IsReconnecting() const { return State->Active && State->Reconnecting; }
const FString& UGuLiHybridSessionSubsystem::GetError() const { return State->Error; }
FGuid UGuLiHybridSessionSubsystem::GetLocalPlayerId() const { return State->Identity.Player; }
FGuid UGuLiHybridSessionSubsystem::GetRoomId() const { return State->Config.RoomId; }
uint64 UGuLiHybridSessionSubsystem::GetMatchEpoch() const { return State->Epoch; }
const TArray<FGuLiHybridMember>& UGuLiHybridSessionSubsystem::GetMembers() const { return State->Members; }
bool UGuLiHybridSessionSubsystem::SendControl(FGuLiHybridMessage Value)
{
    if (!State->Active) return false;
    const auto Core=GuLiNetBridge::ToCore(Value);
    const bool Retain=Core.Header.Kind==GuLiNet::MessageKind::RegisterObject || Core.Header.Kind==GuLiNet::MessageKind::ObjectEvent;
    if (Retain)
    {
        const auto Key=std::pair{Core.Header.Kind,Core.Header.Object};
        if (State->MetadataOutbox.size()>=100000 && !State->MetadataOutbox.contains(Key)) return false;
        State->MetadataOutbox[Key]={Core,FPlatformTime::Seconds()};
    }
    if (!State->Socket || !State->Socket->IsConnected()) return Retain;
    if (!State->Welcomed && Core.Header.Kind!=GuLiNet::MessageKind::Hello) return Retain;
    if ((!State->Welcomed && Value.Kind != static_cast<uint16>(GuLiNet::MessageKind::Hello)) || State->NextControlSequence == MAX_uint64) return false;
    Value.Match = State->Config.RoomId; Value.Sender = State->Identity.Player; Value.MatchEpoch = State->Epoch;
    Value.Sequence = State->NextControlSequence++;
    GuLiNet::Bytes Wire; if (!GuLiNet::EncodeMessage(GuLiNetBridge::ToCore(Value), Wire)) return false;
    State->Socket->Send(Wire.data(),Wire.size(),true);State->SubmittedControlBytes+=Wire.size();
    return true;
}
bool UGuLiHybridSessionSubsystem::SendPeer(const FGuid& Peer, FGuLiHybridMessage Value)
{
    if (!State->Active || !State->Transport || !State->Epoch || (!State->Error.IsEmpty() && !State->Reconnecting)) return false;
    const auto Id = GuLiNetBridge::ToCore(Peer); const auto It = State->Channels.find(Id);
    if (It == State->Channels.end()) return false;
    Value.Match = State->Config.RoomId; Value.Sender = State->Identity.Player; Value.MatchEpoch = State->Epoch;
    const auto Core=GuLiNetBridge::ToCore(Value);auto& Fault=State->Fault;
    const bool Transaction=Core.Header.Kind==GuLiNet::MessageKind::Transaction || Core.Header.Kind==GuLiNet::MessageKind::HitFact
        || Core.Header.Kind==GuLiNet::MessageKind::WorkPrepare;
    if (Transaction && Fault.Accepts(Value.Kind) && Fault.MatchesBusiness(Core) && Fault.Mode==TEXT("work_hold"))
    {
        if (Fault.Bytes+Value.Body.Num()>16*1024*1024) return false;
        Fault.Controls.Add({Peer,Value});Fault.Bytes+=Value.Body.Num();--Fault.Remaining;++Fault.Intercepted;Fault.Record(Core);return true;
    }
    const bool Motion=Core.Header.Kind==GuLiNet::MessageKind::Motion || Core.Header.Kind==GuLiNet::MessageKind::ComponentState;
    bool Queued=false;
    if (Motion)
    {
        // One newest sample per object/channel/recipient, at most 10 publications per wall second.
        const auto Key=std::tuple{Id,Core.Header.Object,Core.Header.Kind};
        if (State->MotionOutbox.size()>=100000 && !State->MotionOutbox.contains(Key)) return false;
        const auto Previous=State->MotionOutbox.find(Key);
        const uint64 OldBytes=Previous==State->MotionOutbox.end()?0:Previous->second.Latest.Body.size();
        if (State->MotionOutboxBytes-OldBytes+Core.Body.size()>16*1024*1024) return false;
        State->MotionOutboxBytes=State->MotionOutboxBytes-OldBytes+Core.Body.size();
        auto& Slot=State->MotionOutbox[Key];Slot.Latest=Core;Slot.Pending=true;Queued=true;
    }
    else Queued=It->second->Queue(Core,FPlatformTime::Seconds());
    if (Queued && Transaction && Fault.Accepts(Value.Kind) && Fault.MatchesBusiness(Core) && Fault.Mode==TEXT("business_duplicate"))
    {
        if (It->second->Queue(Core,FPlatformTime::Seconds())) {--Fault.Remaining;++Fault.Intercepted;++Fault.Sent;Fault.Record(Core);}
    }
    if (Queued && Fault.Accepts(Value.Kind) && Value.Kind==uint16(GuLiNet::MessageKind::Motion)
        && (Fault.Mode!=TEXT("stale_life") || Value.LifeGeneration>1)
        && (Fault.Mode!=TEXT("stale_owner") || Value.OwnerEpoch>1)
        && (Fault.Mode==TEXT("stale_owner") || Fault.Mode==TEXT("stale_life") || Fault.Mode==TEXT("stale_epoch")))
    {
        auto Stale=GuLiNetBridge::ToCore(Value);
        if (Fault.Mode==TEXT("stale_owner")) Stale.Header.OwnerEpoch=Value.OwnerEpoch?Value.OwnerEpoch-1:0;
        if (Fault.Mode==TEXT("stale_life")) Stale.Header.Object.Generation=Value.LifeGeneration?Value.LifeGeneration-1:0;
        if (Fault.Mode==TEXT("stale_epoch")) Stale.Header.Epoch=Value.MatchEpoch-1;
        if (It->second->Queue(Stale,FPlatformTime::Seconds()))
        {--Fault.Remaining;++Fault.Intercepted;Fault.Record(Stale);}
    }
    return Queued;
}
bool UGuLiHybridSessionSubsystem::CanQueueHandoff(const FGuid& Peer) const
{
    const auto It=State->Channels.find(GuLiNetBridge::ToCore(Peer));
    return It!=State->Channels.end() && It->second->CanQueueHandoff();
}
bool UGuLiHybridSessionSubsystem::BroadcastPeer(const FGuLiHybridMessage& Value)
{
    bool Success = true;
    for (const auto& M : State->Members)
        if (M.bOnline && M.bReady && M.Player != State->Identity.Player) Success = SendPeer(M.Player, Value) && Success;
    return Success;
}
TArray<FGuLiHybridMessage> UGuLiHybridSessionSubsystem::GetUnacknowledged(const FGuid& Peer) const
{
    TArray<FGuLiHybridMessage> Out;
    if (const auto It = State->Channels.find(GuLiNetBridge::ToCore(Peer)); It != State->Channels.end())
        for (const auto& V : It->second->Unacknowledged()) Out.Add(GuLiNetBridge::ToUE(V));
    return Out;
}
void UGuLiHybridSessionSubsystem::AccumulateSimulationCost(double Milliseconds,uint32 LogicalFrames)
{
    if (LogicalFrames && FMath::IsFinite(Milliseconds) && Milliseconds>=0)
    {State->SimulationCost+=Milliseconds;State->SimulationFrames+=LogicalFrames;}
}
bool UGuLiHybridSessionSubsystem::SetReviewLoadCost(double Milliseconds)
{
#if UE_BUILD_SHIPPING
    return false;
#else
    FString Run,Case;
    if (!FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),Run)
        || !FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),Case)
        || (Case!=TEXT("gameplay_merged") && !Case.StartsWith(TEXT("fault_")))
        || !FMath::IsFinite(Milliseconds) || Milliseconds< -1 || Milliseconds>1000) return false;
    State->ReviewLoadCost=Milliseconds;return true;
#endif
}
uint64 UGuLiHybridSessionSubsystem::GetPeerQueuedBytes(const FGuid& Peer) const
{
    const auto It=State->Channels.find(GuLiNetBridge::ToCore(Peer));
    return It==State->Channels.end() ? MAX_uint64 : It->second->Metrics().QueuedBytes;
}
bool UGuLiHybridSessionSubsystem::ArmReviewFault(FGuid Operation,FName Mode,int32 Count,uint16 KindFilter,int32 BusinessKind)
{
#if !UE_BUILD_SHIPPING
    FString Run,Case;
    static const TSet<FName> Allowed={TEXT("datagram_duplicate"),TEXT("datagram_reorder"),TEXT("handoff_hold"),
        TEXT("business_duplicate"),TEXT("work_hold"),TEXT("receipt_hold"),TEXT("stale_owner"),TEXT("stale_life"),TEXT("stale_epoch")};
    if (State->Config.Transport!=EGuLiNetworkTransport::LocalUdp || !FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),Run)
        || !FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),Case) || !Case.StartsWith(TEXT("fault_"))
        || Run.IsEmpty() || !Operation.IsValid() || !Allowed.Contains(Mode) || Count<1 || Count>256
        || BusinessKind< -1 || BusinessKind>int32(GuLiNet::TransactionKind::Capability)) return false;
    if (State->Fault.Operation==Operation) return State->Fault.Mode==Mode;
    if (!State->Fault.Mode.IsNone() || !State->Fault.Controls.IsEmpty() || !State->Fault.Receipts.IsEmpty() || !State->Fault.Packet.IsEmpty()) return false;
    State->Fault={};State->Fault.Operation=Operation;State->Fault.Mode=Mode;State->Fault.Remaining=Count;
    State->Fault.KindFilter=KindFilter;State->Fault.BusinessKind=BusinessKind;State->Fault.Armed=FPlatformTime::Seconds();return true;
#else
    return false;
#endif
}
bool UGuLiHybridSessionSubsystem::ReleaseReviewFault(FGuid Operation)
{
    auto& Fault=State->Fault;
    if (!Operation.IsValid() || Fault.Operation!=Operation || !State->Transport) return false;
    const auto HeldMode=Fault.Mode;Fault.Mode=NAME_None;
    for (const auto& V:Fault.Controls) if (!SendPeer(V.Key,V.Value)) {Fault.Mode=HeldMode;return false;} else {++Fault.Sent;++Fault.Released;}
    Fault.Controls.Reset();
    auto Receipts=MoveTemp(Fault.Receipts);Fault.Receipts.Reset();
    for (const auto& V:Receipts) {OnPeer.Broadcast(V.Key,V.Value);++Fault.Released;}
    if (!Fault.Packet.IsEmpty())
    {
        if (!State->Transport->Send(Fault.PacketPeer,Fault.Packet)) return false;
        Fault.Packet.Reset();++Fault.Sent;++Fault.Released;
    }
    Fault.Bytes=0;Fault.Mode=NAME_None;Fault.Remaining=0;return true;
}
TSharedRef<FJsonObject> UGuLiHybridSessionSubsystem::GetNetworkCaptureSnapshot() const
{
    check(IsInGameThread());
    auto Root=MakeShared<FJsonObject>(); const auto Bytes=State->Transport ? State->Transport->GetByteCounters() : FGuLiTransportByteCounters{};
    Root->SetBoolField(TEXT("connected"),IsConnected()); Root->SetNumberField(TEXT("epoch"),double(State->Epoch));
    Root->SetBoolField(TEXT("reconnecting"),State->Reconnecting);
    Root->SetNumberField(TEXT("control_interruptions"),double(State->ControlInterruptions));
    Root->SetNumberField(TEXT("control_reconnects"),double(State->ControlReconnects));
    Root->SetStringField(TEXT("last_control_interruption"),State->LastInterruption);
    Root->SetNumberField(TEXT("pending_metadata"),double(State->MetadataOutbox.size()));
    Root->SetNumberField(TEXT("review_load_cost_ms"),State->ReviewLoadCost);
    Root->SetStringField(TEXT("datagram_byte_basis"),State->Config.Transport==EGuLiNetworkTransport::LocalUdp
        ? TEXT("Actual successful UDP socket payload bytes, including application headers, fragments, ACKs and retries; excludes UDP/IP headers")
        : TEXT("EOS SDK accepted/received application datagrams; excludes opaque EOS transport overhead"));
    Root->SetNumberField(TEXT("datagram_sent_bytes"),double(Bytes.SentBytes)); Root->SetNumberField(TEXT("datagram_received_bytes"),double(Bytes.ReceivedBytes));
    Root->SetNumberField(TEXT("datagram_sent_packets"),double(Bytes.SentPackets)); Root->SetNumberField(TEXT("datagram_received_packets"),double(Bytes.ReceivedPackets));
    Root->SetNumberField(TEXT("datagram_send_failures"),double(Bytes.SendFailures)); Root->SetNumberField(TEXT("datagram_rejected_packets"),double(Bytes.RejectedPackets));
    Root->SetNumberField(TEXT("receive_queue_overflows"),double(Bytes.ReceiveQueueOverflows));
    Root->SetNumberField(TEXT("expired_receive_packets"),double(Bytes.ExpiredReceivePackets));
    Root->SetNumberField(TEXT("queued_receive_bytes"),double(Bytes.QueuedReceiveBytes));
    Root->SetNumberField(TEXT("queued_receive_packets"),double(Bytes.QueuedReceivePackets));
    Root->SetNumberField(TEXT("oldest_receive_ms"),Bytes.OldestReceiveSeconds*1000.);
    Root->SetNumberField(TEXT("control_submitted_application_bytes"),double(State->SubmittedControlBytes));
    Root->SetNumberField(TEXT("control_received_application_bytes"),State->Mailbox ? double(State->Mailbox->ReceivedApplicationBytes.load()) : 0);
    Root->SetStringField(TEXT("control_byte_basis"),TEXT("WebSocket application bytes; submitted bytes are queued by UE, not proof of socket delivery"));
    TArray<TSharedPtr<FJsonValue>> Peers;
    for (const auto& [Id,Channel] : State->Channels)
    {
        const auto& M=Channel->Metrics(); auto P=MakeShared<FJsonObject>();
        P->SetStringField(TEXT("peer"),GuLiNetBridge::ToUE(Id).ToString(EGuidFormats::DigitsWithHyphens));
        P->SetNumberField(TEXT("queued_bytes"),double(M.QueuedBytes)); P->SetNumberField(TEXT("oldest_queue_ms"),M.OldestSeconds*1000.);
        P->SetNumberField(TEXT("retransmit_bytes"),double(M.RetransmitBytes)); P->SetNumberField(TEXT("replaced_motion"),double(M.ReplacedStates));
        P->SetNumberField(TEXT("rejected_packets"),double(M.RejectedPackets));
        P->SetNumberField(TEXT("duplicate_packets"),double(M.DuplicatePackets));P->SetNumberField(TEXT("out_of_order_messages"),double(M.OutOfOrderMessages));
        P->SetNumberField(TEXT("sent_bytes"),double(M.SentBytes)); P->SetNumberField(TEXT("received_bytes"),double(M.ReceivedBytes));
        P->SetNumberField(TEXT("handoff_sent_bytes"),double(M.SentLaneBytes[static_cast<size_t>(GuLiNet::Lane::Handoff)]));
        P->SetNumberField(TEXT("handoff_received_bytes"),double(M.ReceivedLaneBytes[static_cast<size_t>(GuLiNet::Lane::Handoff)]));
        for (int32 Lane=0;Lane<4;++Lane)
        {
            P->SetNumberField(FString::Printf(TEXT("lane_%d_sent_bytes"),Lane),double(M.SentLaneBytes[Lane]));
            P->SetNumberField(FString::Printf(TEXT("lane_%d_received_bytes"),Lane),double(M.ReceivedLaneBytes[Lane]));
            P->SetNumberField(FString::Printf(TEXT("lane_%d_queued_bytes"),Lane),double(M.QueuedLaneBytes[Lane]));
            P->SetNumberField(FString::Printf(TEXT("lane_%d_queued_messages"),Lane),double(M.QueuedLaneMessages[Lane]));
            P->SetNumberField(FString::Printf(TEXT("lane_%d_oldest_queue_ms"),Lane),M.OldestLaneSeconds[Lane]*1000.);
            P->SetNumberField(FString::Printf(TEXT("lane_%d_oldest_payload_ms"),Lane),M.OldestPayloadSeconds[Lane]*1000.);
            P->SetNumberField(FString::Printf(TEXT("lane_%d_sent_through"),Lane),double(Channel->SentThrough(GuLiNet::Lane(Lane))));
            P->SetNumberField(FString::Printf(TEXT("lane_%d_received_through"),Lane),double(Channel->ReceivedThrough(GuLiNet::Lane(Lane))));
        }
        Peers.Add(MakeShared<FJsonValueObject>(P));
    }
    Root->SetArrayField(TEXT("peers"),Peers);
    if (State->Fault.Operation.IsValid())
    {
        const auto& F=State->Fault;auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("operation"),F.Operation.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("mode"),F.Mode.ToString());J->SetNumberField(TEXT("intercepted"),double(F.Intercepted));J->SetNumberField(TEXT("injected_sends"),double(F.Sent));
        J->SetNumberField(TEXT("released"),double(F.Released));J->SetNumberField(TEXT("held_controls"),F.Controls.Num());J->SetNumberField(TEXT("held_receipts"),F.Receipts.Num());
        J->SetNumberField(TEXT("held_bytes"),double(F.Bytes));J->SetArrayField(TEXT("messages"),F.Evidence);Root->SetObjectField(TEXT("review_fault"),J);
    }
    Root->SetNumberField(TEXT("rejected_delivery_epochs"),double(State->ReviewEpochRejected));
    Root->SetNumberField(TEXT("rejected_delivery_identity"),double(State->ReviewIdentityRejected));return Root;
}
bool UGuLiHybridSessionSubsystem::NetworkTick(float)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_NetworkTick);
    if (!State->Active) return true;
    const uint64 Generation = State->Generation;
    const double Now = FPlatformTime::Seconds(); FSocketEvent Event;
    if (State->Mailbox && State->Mailbox->Overflow) Fail(TEXT("Coordinator inbox capacity exhausted"));
    while (State->Mailbox && State->Mailbox->Events.Dequeue(Event))
    {
        if (Event.Kind == ESocketEvent::Message) State->Mailbox->QueuedBytes.fetch_sub(Event.Bytes.Num());
        if (Event.Kind == ESocketEvent::Failed) { InterruptConnection(Event.Error); continue; }
        if (Event.Kind == ESocketEvent::Connected)
        {
            GuLiNet::Hello H;
            H.Carrier = State->Config.Transport == EGuLiNetworkTransport::EOS ? GuLiNet::Transport::EOS : GuLiNet::Transport::LocalUdp;
            H.Faction = GuLiNetBridge::ToCore(State->Config.Team); H.Seat = ToRole(State->Config.Role);
            H.ConfigHash = TCHAR_TO_UTF8(*State->ConfigHash); H.MapHash = TCHAR_TO_UTF8(*State->MapHash);
            H.Endpoint = TCHAR_TO_UTF8(*State->Identity.Endpoint); H.ProductUserId = TCHAR_TO_UTF8(*State->Identity.ProductUserId);
            H.IdentityToken = TCHAR_TO_UTF8(*State->Identity.IdentityToken);
            GuLiNet::Message M; M.Header.Kind = GuLiNet::MessageKind::Hello; M.Body = GuLiNet::EncodeHello(H);
            SendControl(GuLiNetBridge::ToUE(M)); continue;
        }
        GuLiNet::Message V;
        if (!GuLiNet::DecodeMessage(std::span<const uint8>(Event.Bytes.GetData(), Event.Bytes.Num()), V)
            || V.Header.Sender.Valid() || V.Header.Match != GuLiNetBridge::ToCore(State->Config.RoomId))
        { Fail(TEXT("Invalid coordinator message")); continue; }
        State->LastControl = Now;
        if (V.Header.Kind == GuLiNet::MessageKind::Error)
        {
            GuLiNet::Reader R(V.Body);std::string Reason;
            if (R.Text(Reason))
            {
                // Close is asynchronous. A previously authenticated session
                // may still be retiring when its replacement reaches the host.
                // Keep the established P2P world alive and retry this control link.
                if (State->Reconnecting && State->Epoch && Reason=="Identity already connected")
                    InterruptConnection(UTF8_TO_TCHAR(Reason.c_str()));
                else Fail(UTF8_TO_TCHAR(Reason.c_str()));
            }
            continue;
        }
        if (V.Header.Kind == GuLiNet::MessageKind::Welcome)
        {
            GuLiNet::Reader Welcome(V.Body); GuLiNet::Id Player; std::string ConfigHash, MapHash;
            if (!Welcome.Identity(Player) || Player != GuLiNetBridge::ToCore(State->Identity.Player)
                || !Welcome.Text(ConfigHash,128) || ConfigHash != TCHAR_TO_UTF8(*State->ConfigHash)
                || !Welcome.Text(MapHash,128) || MapHash != TCHAR_TO_UTF8(*State->MapHash) || !Welcome.Done() || !V.Header.Epoch)
            { Fail(TEXT("Invalid coordinator welcome")); continue; }
            if (State->Reconnecting) ++State->ControlReconnects;
            State->Epoch = V.Header.Epoch; State->Welcomed = true;
            State->Reconnecting = false; State->Error.Reset(); State->FailureNotified = false;
        }
        else if (V.Header.Epoch != State->Epoch) {++State->ReviewEpochRejected;continue;}
        if (V.Header.Kind == GuLiNet::MessageKind::Roster)
        {
            GuLiNet::Reader R(V.Body); uint32 Count = 0; bool Valid = R.U32(Count) && Count <= 64;
            TArray<FGuLiHybridMember> Members; TMap<FGuid, FString> Addresses; std::set<GuLiNet::Id> Live;
            for (uint32 I = 0; Valid && I < Count; ++I)
            {
                GuLiNet::Id Player, PairKey; uint8 Team = 0, Role = 0, Carrier = 0, Online = 0, Ready = 0; uint64 SeatVersion=0; uint32 OrdinalSlot=0; std::string Endpoint, Puid;
                Valid = R.Identity(Player) && R.U8(Team) && Team <= 2 && R.U8(Role) && Role <= 3
                    && R.U8(Carrier) && Carrier <= 1 && R.U8(Online) && Online <= 1 && R.U8(Ready) && Ready <= 1
                    && R.U64(SeatVersion) && SeatVersion && SeatVersion<=MAX_int64 && R.U32(OrdinalSlot) && OrdinalSlot>0 && OrdinalSlot<=255
                    && R.Text(Endpoint, 512) && R.Text(Puid, 128) && R.Identity(PairKey);
                if (!Valid) break;
                const FGuid PlayerId = GuLiNetBridge::ToUE(Player);
                Members.Add({PlayerId, GuLiNetBridge::ToUE(static_cast<GuLiNet::Team>(Team)), FromRole(Role), Online != 0, Ready != 0,SeatVersion,OrdinalSlot});
                if (PlayerId == State->Identity.Player || !Online) continue;
                if (!PairKey.Valid()) { Valid = false; break; }
                Live.insert(Player); Addresses.Add(PlayerId, UTF8_TO_TCHAR((Carrier ? Puid : Endpoint).c_str()));
                if (!State->PairKeys.contains(Player) || State->PairKeys.at(Player) != PairKey)
                {
                    const auto* Settings=GetDefault<UGuLiNetworkSettings>();GuLiNet::QueueLimits Limits;
                    Limits.BytesPerSecond[0]=FMath::Max(1000,Settings->ReliableBytesPerSecond);
                    Limits.BytesPerSecond[1]=FMath::Max(1000,Settings->MotionBytesPerSecond);
                    Limits.BytesPerSecond[3]=FMath::Max(1000,Settings->HandoffBytesPerSecond);
                    State->Channels[Player] = std::make_unique<GuLiNet::PeerChannel>(PairKey,Limits); State->PairKeys[Player] = PairKey;
                }
            }
            if (!Valid || !R.Done()) { Fail(TEXT("Invalid coordinator roster")); continue; }
            for (auto It = State->Channels.begin(); It != State->Channels.end();)
            { if (!Live.contains(It->first)) { State->PairKeys.erase(It->first); It = State->Channels.erase(It); } else ++It; }
            State->Members = MoveTemp(Members); State->Transport->SetPeers(Addresses); OnRosterChanged.Broadcast();
            if (!State->Active || State->Generation != Generation) return true;
        }
        if (V.Header.Kind==GuLiNet::MessageKind::RegisterObject)
        {
            GuLiNet::Reader Directory(V.Body);GuLiNet::Authority Grant;
            if (GuLiNet::ReadAuthority(Directory,Grant))
            {
                State->MetadataOutbox.erase({GuLiNet::MessageKind::RegisterObject,Grant.Object});
                const auto Life=State->MetadataOutbox.find({GuLiNet::MessageKind::ObjectEvent,Grant.Object});
                if (Life!=State->MetadataOutbox.end())
                {
                    GuLiNet::Reader Body(Life->second.Request.Body);uint8 Requested;
                    if (Grant.OwnerEpoch>Life->second.Request.Header.OwnerEpoch
                        || (Body.U8(Requested) && static_cast<uint8>(Grant.State)>=Requested)) State->MetadataOutbox.erase(Life);
                }
            }
        }
        OnControl.Broadcast(GuLiNetBridge::ToUE(V));
        if (!State->Active || State->Generation != Generation) return true;
    }
    TArray<TPair<FGuid, FGuLiHybridMessage>> Received;
    if (State->Transport)
    {
        State->Transport->Poll([this, Now, &Received](const FGuid& Sender, const TArray<uint8>& Bytes)
        {
            const auto Id = GuLiNetBridge::ToCore(Sender); const auto It = State->Channels.find(Id); if (It == State->Channels.end()) return;
            It->second->Receive(std::span<const uint8>(Bytes.GetData(), Bytes.Num()), Now,
                [this, Sender](std::span<const uint8> Packet) { return State->SendDatagram(Sender,Packet); },
                [this, Sender, Id, Now, &Received](GuLiNet::Message V)
                {
                    if (V.Header.Sender == Id && V.Header.Match == GuLiNetBridge::ToCore(State->Config.RoomId) && V.Header.Epoch == State->Epoch)
                    {
                        State->LastPeerSeen[Id]=Now;
                        if (V.Header.Kind!=GuLiNet::MessageKind::Heartbeat) Received.Emplace(Sender,GuLiNetBridge::ToUE(V));
                    }
                    else if (V.Header.Epoch!=State->Epoch) ++State->ReviewEpochRejected;
                    else ++State->ReviewIdentityRejected;
                });
        });
        for (auto It=State->MotionOutbox.begin();It!=State->MotionOutbox.end();)
        {
            if (!State->Channels.contains(std::get<0>(It->first)) || (!It->second.Pending && Now>It->second.NextSend+1))
            {State->MotionOutboxBytes-=It->second.Latest.Body.size();It=State->MotionOutbox.erase(It);}else ++It;
        }
        for (auto& [Key,Slot]:State->MotionOutbox) if (Slot.Pending && Now>=Slot.NextSend)
        {
            const auto Channel=State->Channels.find(std::get<0>(Key));
            if (Channel!=State->Channels.end() && Channel->second->Queue(Slot.Latest,Now))
            {Slot.Pending=false;Slot.NextSend=Now+1.0/GuLiNet::StatePublicationHz;}
        }
        for (auto& [Peer, Channel] : State->Channels)
        {
            const FGuid PeerId = GuLiNetBridge::ToUE(Peer);
            Channel->Pump(Now, [this, PeerId](std::span<const uint8> Packet)
            { return State->SendDatagram(PeerId,Packet); });

        }
        auto& F=State->Fault;
        if (!F.Packet.IsEmpty() && Now-F.PacketAt>=.2 && State->Transport->Send(F.PacketPeer,F.Packet))
        {F.Packet.Reset();++F.Sent;++F.Released;}
    }
    for (const auto& V : Received)
    {
        auto& Fault=State->Fault;const auto Core=GuLiNetBridge::ToCore(V.Value);
        if (Core.Header.Kind==GuLiNet::MessageKind::TransactionResult && Fault.Mode==TEXT("receipt_hold")
            && Fault.Accepts(V.Value.Kind) && Fault.MatchesBusiness(Core) && Fault.Bytes+V.Value.Body.Num()<=16*1024*1024)
        {Fault.Receipts.Add(V);Fault.Bytes+=V.Value.Body.Num();--Fault.Remaining;++Fault.Intercepted;Fault.Record(Core);continue;}
        OnPeer.Broadcast(V.Key, V.Value);
        if (!State->Active || State->Generation != Generation) return true;
    }
    if (State->Epoch && Now-State->LastHeartbeat>=1)
    {
        State->LastHeartbeat=Now;GuLiNet::Message M;M.Header.Kind=GuLiNet::MessageKind::Heartbeat;
        SendControl(GuLiNetBridge::ToUE(M));BroadcastPeer(GuLiNetBridge::ToUE(M));
        if (State->SimulationFrames || State->ReviewLoadCost>=0)
        {
            // Include every logical frame and shared batch in the reporting window.
            // Measuring only the last frame would alias Mass's 10 Hz work.
            GuLiNet::Writer Cost;Cost.F64(State->ReviewLoadCost>=0?State->ReviewLoadCost:State->SimulationCost/State->SimulationFrames);
            M.Header.Kind=GuLiNet::MessageKind::LoadReport;M.Body=Cost.Take();SendControl(GuLiNetBridge::ToUE(M));
            State->SimulationCost=0;State->SimulationFrames=0;
        }
        for (const auto& [Peer,Channel]:State->Channels)
        {
            const auto Seen=State->LastPeerSeen.find(Peer);GuLiNet::Writer W;W.Identity(Peer);
            W.U8(Seen!=State->LastPeerSeen.end() && Now-Seen->second<10?1:0);
            M.Header.Kind=GuLiNet::MessageKind::PeerStatus;M.Body=W.Take();SendControl(GuLiNetBridge::ToUE(M));
        }
    }
    if (IsConnected())
    {
        std::vector<GuLiNet::Message> Retry;
        for (const auto& [Key,Delivery]:State->MetadataOutbox) if (Now-Delivery.SentAt>=1) Retry.push_back(Delivery.Request);
        for (const auto& M:Retry) SendControl(GuLiNetBridge::ToUE(M));
    }
    if (State->Error.IsEmpty() || State->Reconnecting)
    {
        if (State->Socket && ((!State->Welcomed && Now - State->Started > State->Config.ConnectionTimeout)
            || (State->Welcomed && Now - State->LastControl > State->Config.ConnectionTimeout)))
            InterruptConnection(TEXT("Coordinator connection timed out"));
        else if (!State->Identity.Player.IsValid() && Now - State->Started > State->Config.ConnectionTimeout)
            Fail(TEXT("Transport identity initialization timed out"));
    }
    if (State->Reconnecting)
    {
        if (!State->Socket && Now >= State->NextRetry) ConnectCoordinator();
    }
    if (!State->Reconnecting && !State->Error.IsEmpty() && !State->FailureNotified)
    { State->FailureNotified = true; OnFailure.Broadcast(State->Error); }
    return true;
}
