#pragma once
#include "GuLiNetRoom.h"
#include "GuLiNetManifest.h"
#include <functional>
#include <deque>

namespace GuLiNet
{
// Client room control only. No sockets, database, gameplay tick or load balancing.
// All peers retain the small membership/authority view so the host can leave.
class GULINETCORE_API PeerRoom
{
public:
    using Send=std::function<bool(Id,const Message&)>;
    PeerRoom(Id Match,Id Local,const RuntimeManifest& Manifest,Send Output);
    bool Start(std::map<Id,Member> Members,Id Host,double Now,std::string& Error);
    bool Request(Message Value,double Now);
    bool Receive(Id Sender,const Message& Value,double Now);
    void Observe(const Message& Value,double Now);
    void Seen(Id Player,double Now);
    void Tick(double Now);
    Id Host() const {return HostPlayer;}
    bool Started() const {return Bootstrapped;}
    const std::string& Failure() const {return Error;}
    static Id PairIdentity(Id Match,Id A,Id B);
private:
    Message Control(MessageKind Kind,Bytes Body={},ObjectKey Object={}) const;
    void Local(const Message& Value);
    void Broadcast(const Message& Value);
    void Roster();
    void DirectoryComplete();
    void Bootstrap();
    void Handle(Id Sender,const Message& Value,double Now);
    void Publish(double Now);
    void SetPaused(bool Value);
    void End(const std::string& Reason);
    Id Match,Player,HostPlayer;
    ManifestHashes Hashes;
    std::unique_ptr<Room> Core;
    Send Output;
    std::string Error;
    std::set<Id> Handshakes,Leaving;
    std::map<Id,std::deque<Message>> Outbox;
    std::size_t OutboxBytes=0;
    bool Emit(Id To,const Message& Value);
    std::map<Id,double> LastSeen;
    std::map<std::pair<Id,Id>,std::pair<bool,double>> Reports;
    std::map<ObjectKey,Authority> Published;
    Bytes PublishedGroups;
    Revision PublishedMembership=0;
    RoomPhase PublishedPhase=RoomPhase::Preparing;
    double NowValue=0,StartedAt=0,LastReport=0,SuspicionStarted=0;
    bool Bootstrapped=false,Paused=false,Ended=false;
};
}
