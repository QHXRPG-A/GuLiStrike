#include "GuLiNetRoom.h"
#include <cmath>

namespace GuLiNet
{
Bytes Room::ExportMetadata() const
{
    Writer W(32*1024*1024);W.U32(ProtocolVersion);W.Identity(Match);W.Text(ConfigurationHash);W.Text(LevelHash);
    W.U8(static_cast<std::uint8_t>(CurrentPhase));W.Text(FailureReason);W.U64(NextMembership);W.U64(Scheduler.NextTransferId());
    W.U8(InitialRosterReady?1:0);W.U32(static_cast<std::uint32_t>(Players.size()));
    for (const auto& [P,M]:Players)
    {
        W.Identity(P);W.U8(static_cast<std::uint8_t>(M.Faction));W.U8(static_cast<std::uint8_t>(M.Seat));
        W.U8(static_cast<std::uint8_t>(M.Carrier));W.U64(M.SeatVersion);W.U32(M.OrdinalSlot);
        W.Text(M.ProductUserId);W.Text(M.Endpoint);W.U8(M.Online?1:0);W.U8(M.Ready?1:0);
    }
    W.U32(static_cast<std::uint32_t>(Directory.size()));
    for (const auto& [K,O]:Directory) {WriteAuthority(W,O.Grant);W.Text(O.Definition);W.U32(O.Ordinal);W.Key(O.Parent);}
    W.U32(static_cast<std::uint32_t>(Groups.size()));
    for (const auto& [Key,G]:Groups) {W.Identity(Key);W.Identity(G.Owner);W.U64(G.Version);}
    W.U8(MembershipValue?1:0);
    if (MembershipValue)
    {
        const auto& M=*MembershipValue;W.U64(M.Version);W.Identity(M.Player);W.U8(static_cast<std::uint8_t>(M.Operation));
        W.U8(M.Abrupt?1:0);W.U32(static_cast<std::uint32_t>(M.Waiting.size()));for (Id P:M.Waiting) W.Identity(P);
    }
    return W.Take();
}
bool Room::ImportMetadata(std::span<const std::uint8_t> Data,double Now)
{
    if (!Players.empty() || !Directory.empty() || !std::isfinite(Now)) return false;
    Reader R(Data);std::uint32_t Schema,Count;std::uint8_t Phase,Initial;Id SavedMatch;std::string Config,Map,Failure;
    Revision MembershipCounter,TransferCounter;
    if (!R.U32(Schema) || Schema!=ProtocolVersion || !R.Identity(SavedMatch) || SavedMatch!=Match
        || !R.Text(Config) || Config!=ConfigurationHash || !R.Text(Map) || Map!=LevelHash
        || !R.U8(Phase) || Phase>static_cast<std::uint8_t>(RoomPhase::Aborted) || !R.Text(Failure)
        || !R.U64(MembershipCounter) || !MembershipCounter || !R.U64(TransferCounter) || !TransferCounter
        || !R.U8(Initial) || Initial>1 || !R.U32(Count) || Count>64) return false;
    Room Trial=*this;
    for (std::uint32_t I=0;I<Count;++I)
    {
        Member M;std::uint8_t F,S,C,Online,Ready;
        if (!R.Identity(M.Player) || !R.U8(F) || F<1 || F>2 || !R.U8(S) || S>3 || !R.U8(C) || C>1
            || !R.U64(M.SeatVersion) || !R.U32(M.OrdinalSlot) || !M.OrdinalSlot || M.OrdinalSlot>255
            || !R.Text(M.ProductUserId) || !R.Text(M.Endpoint) || !R.U8(Online) || Online>1 || !R.U8(Ready) || Ready>1) return false;
        M.Faction=static_cast<Team>(F);M.Seat=static_cast<Role>(S);M.Carrier=static_cast<Transport>(C);
        M.Online=Online!=0;M.Ready=Ready!=0;M.LastSeen=Now;if (!Trial.ImportMember(M)) return false;
    }
    if (!R.U32(Count) || Count>100000) return false;
    for (std::uint32_t I=0;I<Count;++I)
    {
        ObjectRegistration O;
        if (!ReadAuthority(R,O.Grant) || !R.Text(O.Definition) || !R.U32(O.Ordinal) || !R.Key(O.Parent)
            || !Definitions.contains(O.Definition) || !Trial.Directory.emplace(O.Grant.Object,O).second
            || !Trial.Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now)) return false;
    }
    if (!R.U32(Count) || Count>4096) return false;
    for (std::uint32_t I=0;I<Count;++I)
    {
        Id Key;OwnershipGroupAuthority G;
        if (!R.Identity(Key) || !Key.Valid() || !R.Identity(G.Owner) || !R.U64(G.Version)
            || !Trial.Groups.emplace(Key,G).second) return false;
    }
    for (const auto& [K,O]:Trial.Directory) if (O.Grant.OwnershipGroup.Valid())
    {
        const auto G=Trial.Groups.find(O.Grant.OwnershipGroup);
        if (G==Trial.Groups.end() || (O.Grant.State!=Life::Destroyed && O.Grant.GroupEpoch!=G->second.Version)) return false;
    }
    std::uint8_t HasMembership;if (!R.U8(HasMembership) || HasMembership>1) return false;
    if (HasMembership)
    {
        MembershipChange M;std::uint8_t Op,Abrupt;
        if (!R.U64(M.Version) || !M.Version || !R.Identity(M.Player) || !Trial.Players.contains(M.Player)
            || !R.U8(Op) || Op>1 || !R.U8(Abrupt) || Abrupt>1 || !R.U32(Count) || Count>64) return false;
        M.Operation=static_cast<MembershipOperation>(Op);M.Abrupt=Abrupt!=0;M.Deadline=Now+Settings.MembershipTimeoutSeconds;
        for (std::uint32_t I=0;I<Count;++I) {Id P;if (!R.Identity(P) || !Trial.Players.contains(P) || !M.Waiting.insert(P).second) return false;}
        Trial.MembershipValue=std::move(M);Trial.Scheduler.SetMembershipPaused(true,Now);
    }
    if (!R.Done() || (Phase==static_cast<std::uint8_t>(RoomPhase::MembershipChanging))!=(HasMembership!=0)) return false;
    Trial.CurrentPhase=static_cast<RoomPhase>(Phase);Trial.FailureReason=std::move(Failure);Trial.NextMembership=MembershipCounter;
    Trial.InitialRosterReady=Initial!=0;Trial.Scheduler.SetNextTransferId(TransferCounter);*this=std::move(Trial);return true;
}
}
