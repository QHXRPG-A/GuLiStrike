#include "GuLiNetCapability.h"
#include "Coordinator.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include "Review.h"
#include <boost/json.hpp>
#include <chrono>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <sstream>

namespace GuLiCoordinator
{
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using namespace GuLiNet;

namespace
{
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool SameAuthorityEncoding(const Authority& A,const Authority& B)
{
    return A==B && std::bit_cast<std::uint64_t>(A.EstimatedCost)==std::bit_cast<std::uint64_t>(B.EstimatedCost);
}
Id NewId()
{
    std::random_device Random; Id V;
    for (auto& Word : V.Words) Word = static_cast<std::uint32_t>(Random());
    if (!V.Valid()) V.Words[3] = 1; return V;
}
std::string Text(const boost::json::object& V, const char* Name)
{ auto It = V.find(Name); return It != V.end() && It->value().is_string() ? std::string(It->value().as_string()) : ""; }
double Number(const boost::json::object& V, const char* Name, double Default = 0)
{
    auto It = V.find(Name); if (It == V.end()) return Default;
    if (It->value().is_string())
    {
        double Result;
        const auto& S = It->value().as_string();
        if (DecodeManifestNumber(std::string_view(S.data(), S.size()), Result)) return Result;
    }
    return It->value().is_number() ? It->value().to_number<double>() : std::numeric_limits<double>::quiet_NaN();
}
bool Integer(double V, double Min, double Max)
{ return std::isfinite(V) && V >= Min && V <= Max && std::floor(V) == V; }
bool ScalarFloat(double V)
{ return std::isfinite(V) && V >= 0 && V <= std::numeric_limits<float>::max(); }
bool LoadManifest(const std::string& Path, std::string& ConfigHash, std::string& MapHash,
    std::vector<ObjectRecord>& Objects, std::map<std::string, Rule>& Rules, Resources& Red, Resources& Blue, std::string& Error,RuntimeManifest* Catalog=nullptr)
{
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File || File.tellg() < 0 || File.tellg() > 16 * 1024 * 1024) { Error = "Manifest is missing or too large"; return false; }
    File.seekg(0); std::string Contents((std::istreambuf_iterator<char>(File)), {});
    // Canonical hashes contain the exact IEEE-754 values exported by UE. Boost's
    // default fast parser can round the same decimal to a neighbouring double.
    boost::json::parse_options Options;
    Options.numbers = boost::json::number_precision::precise;
    boost::system::error_code EC; const auto Parsed = boost::json::parse(Contents, EC, {}, Options);
    if (EC || !Parsed.is_object()) { Error = "Invalid manifest JSON"; return false; }
    const auto& Root = Parsed.as_object();
    ConfigHash = Text(Root, "config_hash"); MapHash = Text(Root, "map_hash");
    if (Number(Root, "schema") != ManifestVersion || Number(Root,"protocol_version")!=ProtocolVersion
        || Number(Root,"handoff_version")!=HandoffVersion || !IsSha256(ConfigHash) || !IsSha256(MapHash)
        || !Root.contains("objects") || !Root.at("objects").is_array()
        || !Root.contains("rules") || !Root.at("rules").is_array()
        || !Root.contains("numbers") || !Root.at("numbers").is_object()
        || !Root.contains("strings") || !Root.at("strings").is_object()) { Error = "Manifest schema/hash fields are invalid"; return false; }
    RuntimeManifest Manifest; Manifest.MapPackage = Text(Root, "map_package");
    for (const auto& Entry : Root.at("objects").as_array())
    {
        if (!Entry.is_object()) { Error = "Invalid object entry"; return false; }
        const auto& V = Entry.as_object(); ObjectRecord O;
        if (!Id::Parse(Text(V, "id"), O.Grant.Object.StableId)) { Error = "Invalid object identity"; return false; }
        const auto Kind = Number(V, "kind"), Generation = Number(V, "generation", 1), TeamValue = Number(V, "team");
        const auto HealthValue = Number(V, "health"), ShieldValue = Number(V, "shield"), Supplied = Number(V, "supplied");
        const auto Completed=Number(V,"completed",1);
        if (!Integer(Kind, 1, static_cast<unsigned>(ObjectKind::BusinessWork)) || !Integer(Generation, 1, UINT32_MAX)
            || !Integer(TeamValue, 0, 2) || !Integer(Supplied, 0, 1) || !Integer(Completed,0,1)
            || !ScalarFloat(HealthValue) || !ScalarFloat(ShieldValue))
        { Error = "Object number is outside its protocol range"; return false; }
        O.Grant.Object.Kind = static_cast<ObjectKind>(static_cast<unsigned>(Kind));
        O.Grant.Object.Generation = static_cast<std::uint32_t>(Generation);
        O.Grant.Faction = static_cast<Team>(static_cast<unsigned>(TeamValue));
        O.Definition = Text(V, "definition"); O.Grant.EstimatedCost = Number(V, "cost", 1);
        const double Ordinal=Number(V,"ordinal");
        if (!Integer(Ordinal,0,UINT32_MAX)) { Error="Invalid startup ordinal"; return false; }
        O.Ordinal=static_cast<std::uint32_t>(Ordinal); O.FacingYaw=static_cast<float>(Number(V,"facing_yaw"));
        O.X = Number(V, "x"); O.Y = Number(V, "y"); O.Z = Number(V, "z");
        O.Vitals.Maximum = O.Vitals.Value = static_cast<float>(HealthValue); O.Vitals.Version = HealthValue>0?1:0;
        O.Vitals.MaximumShield = O.Vitals.Shield = static_cast<float>(ShieldValue);
        O.ShieldRadius = Number(V, "shield_radius"); O.Supplied = Supplied != 0; O.Completed=Completed!=0;
        if (!Manifest.Objects.emplace(O.Grant.Object, std::move(O)).second) { Error = "Duplicate startup identity"; return false; }
    }
    for (const auto& Entry : Root.at("rules").as_array())
    {
        if (!Entry.is_object()) { Error = "Invalid rule entry"; return false; }
        const auto& V = Entry.as_object(); Rule R;
        const auto Kind = Number(V, "kind"), Damage = Number(V, "damage");
        const auto BlueCost = Number(V, "blue"), RedCost = Number(V, "red"), Cooldown = Number(V, "cooldown_ticks");
        const auto Lifetime = Number(V, "effect_ticks", 600), Targets = Number(V, "max_targets", 1);
        if (!Integer(Kind, 0, static_cast<unsigned>(TransactionKind::Capability)) || Kind==static_cast<unsigned>(TransactionKind::AdmitWork) || !ScalarFloat(Damage)
            || !Integer(BlueCost, 0, INT32_MAX) || !Integer(RedCost, 0, INT32_MAX)
            || !Integer(Cooldown, 0, TicksPerSecond * 86400) || !Integer(Lifetime, 0, TicksPerSecond * 86400)
            || !Integer(Targets, 1, 4096)) { Error = "Rule number is outside its protocol range"; return false; }
        R.Kind = static_cast<TransactionKind>(static_cast<unsigned>(Kind));
        R.SourceDefinition = Text(V, "source_definition"); R.Damage = static_cast<float>(Damage);
        R.Amount.Blue = static_cast<std::int32_t>(BlueCost); R.Amount.Red = static_cast<std::int32_t>(RedCost);
        R.Cooldown = static_cast<Tick>(Cooldown); R.MaxEffectLifetime = static_cast<Tick>(Lifetime);
        R.MaxTargets = static_cast<std::uint32_t>(Targets);
        const auto SpawnKind=Number(V,"spawn_kind"), SpawnHealth=Number(V,"spawn_health"), SpawnShield=Number(V,"spawn_shield");
        const auto MinimumAge=Number(V,"minimum_age_ticks"), MaximumChildren=Number(V,"max_live_children");
        const auto MaximumFaction=Number(V,"max_faction_objects"), SpawnSeat=Number(V,"spawn_controller_seat",3);
        const auto SpawnCompleted=Number(V,"spawn_completed",1), Relation=Number(V,"relation"), RelationValue=Number(V,"relation_value");
        if (!Integer(SpawnKind,0,static_cast<unsigned>(ObjectKind::BusinessWork)) || !ScalarFloat(SpawnHealth) || !ScalarFloat(SpawnShield)
            || !Integer(MinimumAge,0,TicksPerSecond*86400) || !Integer(MaximumChildren,0,100000)
            || !Integer(MaximumFaction,0,100000) || !Integer(SpawnSeat,0,static_cast<unsigned>(Role::Observer))
            || !Integer(SpawnCompleted,0,1) || !Integer(Relation,0,static_cast<unsigned>(RelationKind::Parent)) || !Integer(RelationValue,0,1))
        { Error="Object rule number is outside its protocol range"; return false; }
        R.TargetDefinition=Text(V,"target_definition"); R.SpawnKind=static_cast<ObjectKind>(static_cast<unsigned>(SpawnKind));
        R.SpawnHealth=static_cast<float>(SpawnHealth); R.SpawnShield=static_cast<float>(SpawnShield);
        R.SpawnCost=Number(V,"spawn_cost",1); R.SpawnShieldRadius=Number(V,"spawn_shield_radius");
        R.MinimumAge=static_cast<Tick>(MinimumAge); R.MaximumLiveChildren=static_cast<std::uint32_t>(MaximumChildren);
        R.MaximumFactionObjects=static_cast<std::uint32_t>(MaximumFaction); R.SpawnControllerSeat=static_cast<Role>(static_cast<unsigned>(SpawnSeat));
        R.SpawnCompleted=SpawnCompleted!=0; R.Relation=static_cast<RelationKind>(static_cast<unsigned>(Relation)); R.RelationValue=RelationValue!=0;
        const double CapabilityVersion=Number(V,"capability_version",0);
        if (!Integer(CapabilityVersion,0,UINT32_MAX)) {Error="Invalid capability version";return false;}
        R.Capability=Text(V,"capability");R.CapabilityOperation=Text(V,"capability_operation");R.CapabilityVersion=static_cast<std::uint32_t>(CapabilityVersion);
        R.UpgradeCatalog=Text(V,"upgrade_catalog");
        R.RequiredCapabilityCatalog=Text(V,"required_capability_catalog");R.RequiredCapabilityGroup=Text(V,"required_capability_group");R.RequiredCapability=Text(V,"required_capability");
        if (!Manifest.Rules.emplace(Text(V, "id"), R).second) { Error = "Duplicate rule identity"; return false; }
    }
    const auto RedBlue = Number(Root, "red_team_blue"), RedRed = Number(Root, "red_team_red");
    const auto BlueBlue = Number(Root, "blue_team_blue"), BlueRed = Number(Root, "blue_team_red");
    if (!Integer(RedBlue, 0, INT32_MAX) || !Integer(RedRed, 0, INT32_MAX)
        || !Integer(BlueBlue, 0, INT32_MAX) || !Integer(BlueRed, 0, INT32_MAX))
    { Error = "Invalid starting resources"; return false; }
    Manifest.StartingRed.Blue = static_cast<std::int32_t>(RedBlue); Manifest.StartingRed.Red = static_cast<std::int32_t>(RedRed);
    Manifest.StartingBlue.Blue = static_cast<std::int32_t>(BlueBlue); Manifest.StartingBlue.Red = static_cast<std::int32_t>(BlueRed);
    for (const auto& Entry : Root.at("numbers").as_object())
    {
        double Value;
        const auto& J = Entry.value();
        if (!J.is_string() || !DecodeManifestNumber(std::string_view(J.as_string().data(), J.as_string().size()), Value))
        { Error = "Invalid precise numerical catalog entry"; return false; }
        Manifest.Numbers.emplace(std::string(Entry.key()), Value);
    }
    for (const auto& Entry : Root.at("strings").as_object())
    {
        if (!Entry.value().is_string()) { Error="Invalid authored catalog selector"; return false; }
        Manifest.Strings.emplace(std::string(Entry.key()),std::string(Entry.value().as_string()));
    }
    ManifestHashes Hashes;
    if (!InitializeManifestCapabilities(Manifest,Error) || !HashManifest(Manifest, Hashes, Error)) return false;
    if (Hashes.Configuration != ConfigHash || Hashes.Map != MapHash)
    {
        Error = "Manifest content does not match its hashes: configuration expected=" + ConfigHash
            + " actual=" + Hashes.Configuration + "; map expected=" + MapHash + " actual=" + Hashes.Map;
        return false;
    }
    if (Catalog) *Catalog=Manifest;
    Objects.clear(); for (auto& [Key, O] : Manifest.Objects) { (void)Key; Objects.push_back(std::move(O)); }
    Rules = std::move(Manifest.Rules); Red = Manifest.StartingRed; Blue = Manifest.StartingBlue;
    return true;
}
}

bool Coordinator::ValidateManifestFile(const std::string& Path, std::string& Error)
{
    std::string Configuration, Map; std::vector<ObjectRecord> Objects;
    std::map<std::string, Rule> Rules; Resources Red, Blue;
    return LoadManifest(Path, Configuration, Map, Objects, Rules, Red, Blue, Error);
}

struct Coordinator::State : std::enable_shared_from_this<Coordinator::State>
{
    struct Session;
    struct HostedRoom
    {
        std::unique_ptr<Room> Core;
        std::map<Id,std::weak_ptr<Session>> Connections;
        std::map<std::pair<Id,Id>,Id> PairKeys;
        std::map<std::pair<Id,Id>,Id> SavedPairKeys;
        std::map<Id,double> Disconnected;
        std::map<std::pair<Id,Id>,std::pair<bool,double>> PeerPresence;
        std::map<ObjectKey,Authority> PublishedGrants;
        std::map<Id,std::set<ObjectKey>> PendingRegistrationReceipts;
        Bytes SavedMetadata;
        std::vector<HandoffAction> OwnershipOutbox;
        std::map<Revision,boost::json::object> MembershipDeadlineFailures;
        Revision PublishedMembership=0;
        Bytes PublishedResourceGroups;
        RoomPhase PublishedPhase=RoomPhase::Preparing;
        std::uint64_t NextMessage=1;
        bool Dirty=true;
    };
    asio::io_context& Context;
    Config Settings;
    tcp::acceptor Listener;
    asio::steady_timer Timer;
    Store Database;
    IdentityVerifier Identities;
    Review Acceptance;
    std::string ConfigHash,MapHash;
    RuntimeManifest Catalog;
    OwnershipSettings OwnershipConfig;
    std::map<std::string,OwnershipDefinition> Definitions;
    std::map<ObjectKey,ObjectRecord> InitialObjects;
    std::map<Id,HostedRoom> Rooms;
    bool Stopping=false;
    State(asio::io_context& C,Config S)
        :Context(C),Settings(std::move(S)),Listener(C),Timer(C),Database(Settings.Database),Acceptance(Settings.ReviewDirectory,Settings.ReviewRun) {}
    bool Start(std::string& Error);
    void Accept();
    void Poll();
    void Join(const std::shared_ptr<Session>& Connection,Message Value,Hello Greeting);
    void Handle(const std::shared_ptr<Session>& Connection,const Message& Value);
    void Lost(Id Match,Id Player,Session* Expected);
    void Roster(HostedRoom& R);
    void Broadcast(HostedRoom& R,Message Value);
    Message Control(HostedRoom& R,MessageKind Kind,Bytes Body={});
    Message Registration(HostedRoom& R,const ObjectRegistration& Object);
    void AcknowledgeRegistration(HostedRoom& R,const std::shared_ptr<Session>& To,const ObjectRegistration& Object);
    bool Persist(HostedRoom& R);
    void Directory(HostedRoom& R,const std::shared_ptr<Session>& To={});
    void Publish(HostedRoom& R,double Time);
    void SendMembership(HostedRoom& R,const std::shared_ptr<Session>& To={});
};

struct Coordinator::State::Session : std::enable_shared_from_this<Session>
{
    websocket::stream<beast::tcp_stream> Socket;
    beast::flat_buffer Buffer;
    http::request<http::string_body> Request;
    std::weak_ptr<State> Host;
    std::deque<std::shared_ptr<Bytes>> Writes;
    std::function<std::optional<Message>()> DirectoryNext;
    Id Match, Player;
    bool Loopback = false;
    bool Bound = false;
    bool Closed = false;
    bool CloseAfterWrite = false;
    bool Writing = false;
    std::size_t QueuedBytes = 0;
    explicit Session(tcp::socket S, std::shared_ptr<State> H) : Socket(std::move(S)), Host(H)
    { boost::system::error_code EC; const auto Endpoint = beast::get_lowest_layer(Socket).socket().remote_endpoint(EC); Loopback = !EC && Endpoint.address().is_loopback(); }
    void Start()
    {
        boost::system::error_code OptionError;
        beast::get_lowest_layer(Socket).socket().set_option(tcp::no_delay(true),OptionError);
        if (OptionError) {Stop();return;}
        beast::get_lowest_layer(Socket).expires_after(std::chrono::seconds(10));
        http::async_read(Socket.next_layer(), Buffer, Request,
            [Self = shared_from_this()](boost::system::error_code EC, std::size_t)
            {
                if (EC || Self->Request.target() != "/v1/match" || !websocket::is_upgrade(Self->Request)) { Self->Stop(); return; }
                beast::get_lowest_layer(Self->Socket).expires_never();
                websocket::stream_base::timeout Timeouts;Timeouts.handshake_timeout=std::chrono::seconds(10);
                Timeouts.idle_timeout=websocket::stream_base::none();Timeouts.keep_alive_pings=false;Self->Socket.set_option(Timeouts);
                Self->Socket.read_message_max(MaxMessageBytes); Self->Socket.binary(true);
                Self->Socket.async_accept(Self->Request, [Self](boost::system::error_code E)
                { if (E) Self->Stop(); else Self->Read(); });
            });
    }
    void Stop()
    {
        if (Closed) return; Closed = true;
        DirectoryNext={};
        boost::system::error_code EC; beast::get_lowest_layer(Socket).socket().close(EC);
        if (auto H = Host.lock(); H && Bound) H->Lost(Match, Player, this);
    }
    void Error(const std::string& Why)
    {
        // Authentication/rejection replies own no subsequent read operation.
        // Close after the last queued reply so rejected sessions cannot linger.
        CloseAfterWrite = true;
        Message V; V.Header.Kind = MessageKind::Error; V.Header.Match = Match;
        Writer W; W.Text(Why); V.Body = W.Take(); Send(V);
    }
    void Read()
    {
        if (Closed || CloseAfterWrite) return;
        Socket.async_read(Buffer, [Self = shared_from_this()](boost::system::error_code EC, std::size_t Size)
        {
            if (EC) { Self->Stop(); return; }
            auto Host = Self->Host.lock(); if (!Host) { Self->Stop(); return; }
            Host->Acceptance.Received(Size);
            Bytes Wire(Size); asio::buffer_copy(asio::buffer(Wire), Self->Buffer.data()); Self->Buffer.consume(Size);
            Message V;
            if (!Self->Socket.got_binary() || !DecodeMessage(Wire, V)) { Self->Stop(); return; }
            if (!Self->Bound)
            {
                Hello Greeting;
                if (V.Header.Kind != MessageKind::Hello || !V.Header.Match.Valid() || !V.Header.Sender.Valid() || !DecodeHello(V.Body, Greeting))
                { Self->Stop(); return; }
                // Used only to address an error; membership is still unbound
                // until the transport-specific identity check and Join succeed.
                Self->Match = V.Header.Match;
                if (Greeting.Carrier == Transport::LocalUdp)
                {
                    if (!Host->Settings.AllowDevelopmentIdentity || !Self->Loopback || !Greeting.ProductUserId.empty())
                    { Self->Error("Development identity requires an explicit loopback development service"); return; }
                    Host->Join(Self, std::move(V), std::move(Greeting)); if (Self->Bound) Self->Read();
                }
                else
                {
                    Host->Identities.Verify(Greeting, [Self, V, Greeting](bool Valid, std::string Error)
                    {
                        if (Self->Closed) return;
                        if (!Valid) { Self->Error(Error); return; }
                        Id Puid;
                        if (!Id::Parse(Greeting.ProductUserId, Puid) || Puid != V.Header.Sender)
                        { Self->Error("Player identity does not match verified ProductUserId"); return; }
                        if (auto H = Self->Host.lock()) { H->Join(Self, V, Greeting); if (Self->Bound) Self->Read(); }
                    });
                }
                return;
            }
            if (V.Header.Match != Self->Match || V.Header.Sender != Self->Player) { Self->Stop(); return; }
            Host->Handle(Self, V); Self->Read();
        });
    }
    void Send(const Message& Value)
    {
        if (Closed) return;
        auto Wire = std::make_shared<Bytes>();
        if (!EncodeMessage(Value, *Wire) || QueuedBytes + Wire->size() > 16 * 1024 * 1024) { Stop(); return; }
        QueuedBytes += Wire->size(); Writes.push_back(Wire); Write();
    }
    void ContinueDirectory()
    {
        // A small bounded look-ahead keeps a large historical directory moving
        // even while live grants/rosters keep the socket queue nonempty. Each
        // record still gets its grant and sequence at enqueue time, before any
        // later delta, and all messages retain the existing FIFO wire format.
        while (!Closed && !CloseAfterWrite && DirectoryNext && QueuedBytes<64*1024)
        {
            auto Next=DirectoryNext();
            if (!Next) {DirectoryNext={};return;}
            Send(*Next);
        }
    }
    void StreamDirectory(std::function<std::optional<Message>()> Next)
    {
        if (Closed) return;
        if (DirectoryNext) {Stop();return;}
        DirectoryNext=std::move(Next);ContinueDirectory();
    }
    void Write()
    {
        if (Closed || Writing || Writes.empty()) return;
        Writing=true;
        Socket.async_write(asio::buffer(*Writes.front()), [Self = shared_from_this()](boost::system::error_code EC, std::size_t Size)
        {
            Self->Writing=false;
            if (auto Host=Self->Host.lock()) Host->Acceptance.Sent(Size);
            if (EC) { Self->Stop(); return; }
            Self->QueuedBytes -= Self->Writes.front()->size(); Self->Writes.pop_front();
            if (Self->Writes.empty() && Self->CloseAfterWrite) {Self->Stop();return;}
            Self->ContinueDirectory();
            Self->Write();
        });
    }
};

bool Coordinator::State::Start(std::string& Error)
{
    if (!Database.Good()) {Error=Database.Error();return false;}
    std::vector<ObjectRecord> Objects;std::map<std::string,Rule> Rules;Resources Red,Blue;
    if (!LoadManifest(Settings.Manifest,ConfigHash,MapHash,Objects,Rules,Red,Blue,Error,&Catalog)
        || !ReadOwnershipDefinitions(Catalog,Definitions,Error) || !ReadOwnershipSettings(Catalog,OwnershipConfig,Error) || !RuntimeObjects(Catalog,InitialObjects,Error)) return false;
    boost::system::error_code EC;const auto Address=asio::ip::make_address(Settings.Address,EC);
    if (EC || !Address.is_loopback()) {Error="Bind to loopback; public connections require a WSS reverse proxy";return false;}
    Listener.open(Address.is_v6()?tcp::v6():tcp::v4(),EC);
    if (!EC) Listener.set_option(asio::socket_base::reuse_address(true),EC);
    if (!EC) Listener.bind({Address,Settings.Port},EC);
    if (!EC) Listener.listen(asio::socket_base::max_listen_connections,EC);
    if (EC) {Error=EC.message();return false;}Accept();Poll();return true;
}
void Coordinator::State::Accept()
{
    Listener.async_accept(asio::make_strand(Context),[Self=shared_from_this()](boost::system::error_code EC,tcp::socket Socket)
    {if (!EC) std::make_shared<Session>(std::move(Socket),Self)->Start();if (!Self->Stopping) Self->Accept();});
}
Message Coordinator::State::Control(HostedRoom& R,MessageKind Kind,Bytes Body)
{
    Message V;V.Header.Kind=Kind;V.Header.Match=R.Core->MatchId();V.Header.Epoch=R.Core->Epoch();
    V.Header.Sequence=R.NextMessage++;V.Body=std::move(Body);return V;
}
void Coordinator::State::Broadcast(HostedRoom& R,Message V)
{for (const auto& [P,Weak]:R.Connections) if (auto C=Weak.lock()) C->Send(V);}
bool Coordinator::State::Persist(HostedRoom& R)
{
    const auto Data=R.Core->ExportMetadata();
    // Ready/frozen acknowledgements change an in-flight transfer, not its
    // durable directory. Do not rewrite the room for an unchanged payload.
    if (Data==R.SavedMetadata && R.PairKeys==R.SavedPairKeys) {R.Dirty=false;return true;}
    const double Start=Now();const bool Ok=Database.Save(R.Core->MatchId(),{Data,R.PairKeys});
    Acceptance.Record("database_metadata_commit",(Now()-Start)*1000);
    if (!Ok) {std::cerr<<"Room metadata persistence: "<<Database.Error()<<'\n';return false;}
    R.SavedMetadata=Data;R.SavedPairKeys=R.PairKeys;R.Dirty=false;return true;
}
void Coordinator::State::Roster(HostedRoom& R)
{
    for (const auto& [Recipient,Weak]:R.Connections)
    {
        const auto C=Weak.lock();if (!C) continue;
        Writer W;W.U32(static_cast<std::uint32_t>(R.Core->Members().size()));
        for (const auto& [P,M]:R.Core->Members())
        {
            W.Identity(P);W.U8(static_cast<std::uint8_t>(M.Faction));W.U8(static_cast<std::uint8_t>(M.Seat));
            W.U8(static_cast<std::uint8_t>(M.Carrier));W.U8(M.Online?1:0);W.U8(M.Ready?1:0);W.U64(M.SeatVersion);W.U32(M.OrdinalSlot);
            W.Text(M.Endpoint);W.Text(M.ProductUserId);
            W.Identity(P==Recipient?Id{}:R.PairKeys.at(std::minmax(P,Recipient)));
        }
        C->Send(Control(R,MessageKind::Roster,W.Take()));
        Writer Groups;const auto Values=R.Core->ResourceGroups();Groups.U32(static_cast<std::uint32_t>(Values.size()));
        for (const auto& [Key,G]:Values) WriteResourceGroup(Groups,G);
        C->Send(Control(R,MessageKind::ResourceGroups,Groups.Take()));
    }
}
void Coordinator::State::Directory(HostedRoom& R,const std::shared_ptr<Session>& To)
{
    if (!To)
    {
        for (const auto& [Player,Weak]:R.Connections) if (auto Connection=Weak.lock()) Directory(R,Connection);
        return;
    }
    // Snapshot keys only (bounded by the existing 100000-record room limit).
    // Queue a bounded slice of unchanged registrations as writes drain. A
    // late directory must not synchronously overflow the 16MiB socket queue.
    // Read current grants at enqueue time so interleaved deltas cannot be
    // followed by an older saved grant. Historical keys are never erased.
    std::vector<ObjectKey> Keys;Keys.reserve(R.Core->Objects().size());
    for (const auto& [K,O]:R.Core->Objects()) Keys.push_back(K);
    const Id Match=R.Core->MatchId();
    To->StreamDirectory([Weak=weak_from_this(),Match,Keys=std::move(Keys),Cursor=std::size_t{0},Ended=false]() mutable -> std::optional<Message>
    {
        const auto Owner=Weak.lock();if (!Owner) return std::nullopt;
        const auto Found=Owner->Rooms.find(Match);if (Found==Owner->Rooms.end()) return std::nullopt;
        auto& RoomValue=Found->second;
        if (Cursor<Keys.size()) return Owner->Registration(RoomValue,RoomValue.Core->Objects().at(Keys[Cursor++]));
        if (Ended) return std::nullopt;Ended=true;
        Writer W;W.U32(static_cast<std::uint32_t>(RoomValue.Core->Objects().size()));W.U8(static_cast<std::uint8_t>(RoomValue.Core->Phase()));
        W.U32(static_cast<std::uint32_t>(RoomValue.Core->Ownership().Transfers().size()));
        for (const auto& [K,T]:RoomValue.Core->Ownership().Transfers()) W.Blob(EncodeHandoff(T));
        return Owner->Control(RoomValue,MessageKind::DirectoryComplete,W.Take());
    });
}
Message Coordinator::State::Registration(HostedRoom& R,const ObjectRegistration& O)
{
    Writer W;WriteAuthority(W,O.Grant);W.Text(O.Definition);W.U32(O.Ordinal);W.Key(O.Parent);
    auto V=Control(R,MessageKind::RegisterObject,W.Take());V.Header.Object=O.Grant.Object;return V;
}
void Coordinator::State::AcknowledgeRegistration(HostedRoom& R,const std::shared_ptr<Session>& To,const ObjectRegistration& O)
{
    // Duplicate requests must receive the accepted grant, but must not force a
    // whole-room commit ahead of the scheduled publication batch.
    if (R.Dirty) R.PendingRegistrationReceipts[To->Player].insert(O.Grant.Object);
    else To->Send(Registration(R,O));
}
void Coordinator::State::SendMembership(HostedRoom& R,const std::shared_ptr<Session>& To)
{
    if (!R.Core->Membership()) return;const auto& M=*R.Core->Membership();
    Writer W;W.U64(M.Version);W.U8(static_cast<std::uint8_t>(M.Operation));W.Identity(M.Player);W.U8(M.Abrupt?1:0);
    auto V=Control(R,MessageKind::MembershipBegin,W.Take());if (To) To->Send(V);else Broadcast(R,V);
}
void Coordinator::State::Join(const std::shared_ptr<Session>& C,Message V,Hello H)
{
    const double Time=Now();C->Match=V.Header.Match;
    if (!Rooms.contains(C->Match))
    {
        if (Rooms.size()>=Settings.MaxRooms) {C->Error("Room capacity exhausted");return;}
        HostedRoom R;R.Core=std::make_unique<Room>(C->Match,ConfigHash,MapHash,Defaults{},OwnershipConfig);
        if (!R.Core->ConfigureDefinitions(Definitions)) {C->Error("Invalid ownership definitions");return;}
        StoredRoom Saved;bool Found;
        if (!Database.Load(C->Match,Saved,Found)) {C->Error(Database.Error());return;}
        if (Found)
        {
            if (!R.Core->ImportMetadata(Saved.Metadata,Time)) {C->Error("Incompatible room metadata");return;}
            R.PairKeys=std::move(Saved.PairKeys);R.SavedMetadata=std::move(Saved.Metadata);
            R.SavedPairKeys=R.PairKeys;
            for (const auto& [P,M]:R.Core->Members()) if (M.Online) R.Disconnected[P]=Time;
        }
        else for (const auto& [K,O]:InitialObjects)
            if (!R.Core->Register({O.Grant,O.Definition,O.Ordinal,O.Parent},Time)) {C->Error("Invalid startup registration");return;}
        R.PublishedPhase=R.Core->Phase();Rooms.emplace(C->Match,std::move(R));
    }
    auto& R=Rooms.at(C->Match);
    if (const auto Existing=R.Connections.find(V.Header.Sender);Existing!=R.Connections.end())
        if (const auto Other=Existing->second.lock();Other && !Other->Closed) {C->Error("Identity already connected");return;}
    for (const auto& [P,M]:R.Core->Members()) if (M.Online && M.Carrier!=H.Carrier) {C->Error("Transport mismatch");return;}
    Member M;M.Player=V.Header.Sender;M.Faction=H.Faction;M.Seat=H.Seat;M.Carrier=H.Carrier;M.Endpoint=H.Endpoint;M.ProductUserId=H.ProductUserId;
    if (M.Carrier==Transport::LocalUdp && !M.Endpoint.starts_with("127.0.0.1:")) {C->Error("Local UDP requires loopback");return;}
    std::string Error;if (!R.Core->Join(M,H.ConfigHash,H.MapHash,Time,Error)) {C->Error(Error);return;}
    for (const auto& [P,Other]:R.Core->Members()) if (P!=M.Player)
    {const auto Pair=std::minmax(P,M.Player);if (!R.PairKeys.contains(Pair)) R.PairKeys.emplace(Pair,NewId());}
    R.Dirty=true;if (!Persist(R)) {C->Error("Metadata persistence unavailable");return;}
    C->Player=M.Player;C->Bound=true;R.Connections[M.Player]=C;R.Disconnected.erase(M.Player);
    Writer W;W.Identity(M.Player);W.Text(ConfigHash);W.Text(MapHash);C->Send(Control(R,MessageKind::Welcome,W.Take()));
    // Announce the join before its potentially large historical directory.
    // The joining world must know it is importing peer state when directory
    // completion arrives; otherwise initial readiness can expire first.
    Roster(R);SendMembership(R,C);Directory(R,C);
    // A reattached control link receives the current grant directory. P2P keys,
    // logical ages and owner results are unchanged by reconnecting this socket.
    if (R.Core->Phase()==RoomPhase::Running) C->Send(Control(R,MessageKind::MembershipCommit));
}
void Coordinator::State::Handle(const std::shared_ptr<Session>& C,const Message& V)
{
    auto& R=Rooms.at(C->Match);auto& Core=*R.Core;const double Time=Now();
    if (V.Header.Epoch!=Core.Epoch()) return;Reader Read(V.Body);
    switch (V.Header.Kind)
    {
    case MessageKind::Heartbeat: Core.Heartbeat(C->Player,Time);C->Send(Control(R,MessageKind::Heartbeat));return;
    case MessageKind::LoadReport:
    {double Cost;if (Read.F64(Cost) && Read.Done()) Core.LoadReport(C->Player,Cost);return;}
    case MessageKind::PeerStatus:
    {
        Id Peer;std::uint8_t Seen;
        if (Read.Identity(Peer) && Read.U8(Seen) && Seen<2 && Read.Done() && Peer!=C->Player && Core.Members().contains(Peer))
            R.PeerPresence[{C->Player,Peer}]={Seen!=0,Time};return;
    }
    case MessageKind::WorldReady:
        if (!V.Body.empty() || !Core.Ready(C->Player)) return;
        Roster(R);break;
    case MessageKind::AssignmentReady:
    {
        Revision Version;if (!Read.U64(Version) || !Read.Done()) return;
        Core.CommitAssignment(C->Player,V.Header.Object,Version,Time);break;
    }
    case MessageKind::SelectionActivity:
    case MessageKind::DirectOperation:
    {
        Revision Version=0;std::uint32_t Count;std::vector<ObjectKey> Keys;
        if ((V.Header.Kind==MessageKind::SelectionActivity && !Read.U64(Version)) || !Read.U32(Count) || Count>100000) return;
        for (std::uint32_t I=0;I<Count;++I) {ObjectKey K;if (!Read.Key(K) || !K.Valid()) return;Keys.push_back(K);}
        if (!Read.Done()) return;
        if (V.Header.Kind==MessageKind::SelectionActivity) Core.Select(C->Player,Version,Keys,Time);else Core.Operate(C->Player,Keys,Time);break;
    }
    case MessageKind::HandoffReady:
    {ObjectHandoff T;if (DecodeHandoff(V.Body,T) && T.Object==V.Header.Object) Core.Ownership().TargetPrepared(C->Player,T.Object,T.IdValue);break;}
    case MessageKind::HandoffInstalled:
    {
        Bytes Bytes;ObjectHandoff T;std::uint8_t Stage;
        if (!Read.U8(Stage) || Stage>1 || !Read.Blob(Bytes) || !DecodeHandoff(Bytes,T) || T.Object!=V.Header.Object || !Read.Done()) return;
        if (Stage==0) Core.Ownership().SourceFrozen(C->Player,T.Object,T.IdValue,T.FrozenState);
        else Core.Ownership().TargetInstalled(C->Player,T.Object,T.IdValue,T.InstalledState);break;
    }
    case MessageKind::HandoffCancel:
    {Revision Transfer;if (Read.U64(Transfer) && Read.Done()) Core.Ownership().Reject(C->Player,V.Header.Object,Transfer);break;}
    case MessageKind::ResourceSnapshotInstalled:
    {
        Id Group;Revision Recovery,Number;std::string Digest;std::uint32_t Count;std::vector<ObjectRegistration> Objects;
        if (!Read.Identity(Group)||!Read.U64(Recovery)||!Read.U64(Number)||!Read.Text(Digest,64)||!Read.U32(Count)||!Count||Count>4096) return;
        for (std::uint32_t I=0;I<Count;++I)
        {ObjectRegistration O;if (!ReadAuthority(Read,O.Grant)||!Read.Text(O.Definition,256)||!Read.U32(O.Ordinal)||!Read.Key(O.Parent)) return;Objects.push_back(std::move(O));}
        if (!Read.Done()||!Core.ResourceSnapshotInstalled(C->Player,Group,Recovery,Number,Digest,Objects,Time)) return;
        break;
    }
    case MessageKind::MembershipReady:
    {Revision Version;if (Read.U64(Version) && Read.Done()) Core.MembershipReady(C->Player,Version,Time);break;}
    case MessageKind::Leave: if (Read.Done()) Core.BeginLeave(C->Player,false,Time);break;
    case MessageKind::RegisterObject:
    {
        ObjectRegistration O;
        if (!ReadAuthority(Read,O.Grant) || !Read.Text(O.Definition,256) || !Read.U32(O.Ordinal) || !Read.Key(O.Parent) || !Read.Done()
            || O.Grant.Object!=V.Header.Object || !O.Parent.Valid()) return;
        const bool ReviewBirth=O.Grant.Object.Kind==ObjectKind::BusinessWork || O.Grant.Object.Kind==ObjectKind::Building;
        const auto ReviewDefinition=ReviewBirth?O.Definition:std::string{};
        const auto RejectRegistration=[&](const std::string& Reason)
        {
            static unsigned Logged=0;if (!ReviewBirth || Logged>=16) return;++Logged;
            const auto Member=Core.Members().find(C->Player);
            std::cerr<<"Registration rejected reason="<<Reason<<" object="<<O.Grant.Object.StableId.ToString()
                <<" definition="<<ReviewDefinition<<" ordinal="<<O.Ordinal<<" parent="<<O.Parent.StableId.ToString()
                <<" creator="<<C->Player.ToString()<<" ready="<<(Member!=Core.Members().end() && Member->second.Ready)
                <<" policy="<<unsigned(O.Grant.Policy)<<" group_version="<<O.Grant.GroupEpoch
                <<" owner_version="<<O.Grant.OwnerEpoch<<" source_lease="<<O.Grant.SourceLease.Valid()<<std::endl;
        };
        if (const auto Old=Core.Objects().find(O.Grant.Object);Old!=Core.Objects().end())
        {
            // A repeated registration still needs a receipt, even when the
            // original broadcast was missed. It is not a metadata mutation.
            if (Old->second.Definition==O.Definition && Old->second.Ordinal==O.Ordinal && Old->second.Parent==O.Parent)
                AcknowledgeRegistration(R,C,Old->second);
            return;
        }
        const auto Parent=Core.Objects().find(O.Parent);
        if (Parent==Core.Objects().end()) {RejectRegistration("parent-not-registered");return;}
        const bool ParentOwned=(Parent->second.Grant.Dormant?Parent->second.Grant.StateCustodian:Parent->second.Grant.SimOwner)==C->Player;
        const bool Recovery=O.Grant.SimOwner!=C->Player && Core.CanRecoverBirth(O,C->Player);
        if (O.Grant.SimOwner!=C->Player && !Recovery) {RejectRegistration("invalid-birth-recovery");return;}
        bool GroupOwned=false;
        const auto Definition=Core.ObjectDefinitions().find(O.Definition);
        if (Definition!=Core.ObjectDefinitions().end() && Definition->second.Policy==OwnershipPolicy::SharedGroup)
        {
            const auto Group=StableId("GuLiOwnershipGroup/v1",Definition->second.Group);
            const auto Authority=Core.OwnershipGroups().find(Group);
            GroupOwned=Authority!=Core.OwnershipGroups().end() && Authority->second.Owner==C->Player
                && O.Grant.Policy==OwnershipPolicy::SharedGroup && O.Grant.OwnershipGroup==Group
                && (Recovery || (O.Grant.GroupEpoch==Authority->second.Version && O.Grant.SimOwner==C->Player
                    && O.Grant.OwnerEpoch==1 && O.Grant.SourceLease.Valid()));
            if (!GroupOwned) {RejectRegistration("group-authority");return;}
        }
        // A shared-group child's simulator can differ from its parent's.
        // The configured group's current owner and epoch authorize this birth.
        if (!ParentOwned && !GroupOwned && !Recovery) {RejectRegistration("creator-authority");return;}
        if (O.Ordinal)
        {
            // Ordinals keep their creator namespace when a decided birth is
            // delivered after that creator's departure and state takeover.
            const auto Slot=O.Ordinal>>24;
            if (std::none_of(Core.Members().begin(),Core.Members().end(),[&](const auto& M){return M.second.OrdinalSlot==Slot;}))
            {RejectRegistration("ordinal-namespace");return;}
            for (const auto& [K,V]:Core.Objects()) if (OrdinalScope(K.Kind)==OrdinalScope(O.Grant.Object.Kind) && V.Ordinal==O.Ordinal)
            {RejectRegistration("ordinal-duplicate/"+K.StableId.ToString()+"/"+V.Definition);return;}
        }
        if (!Core.Register(std::move(O),Time,C->Player)) {RejectRegistration("room-registration");return;}
        break;
    }
    case MessageKind::ObjectEvent:
    {
        std::uint8_t State;if (!Read.U8(State) || !Read.Done() || State>static_cast<unsigned>(Life::Destroyed)) return;
        const auto O=Core.Objects().find(V.Header.Object);if (O==Core.Objects().end()) return;
        const auto& A=O->second.Grant;
        if (V.Header.OwnerEpoch<A.OwnerEpoch || (V.Header.OwnerEpoch==A.OwnerEpoch && State<=static_cast<unsigned>(A.State)))
        {
            // A no-op/obsolete update is acknowledged with the current grant.
            // Without this receipt the client retries forever and each retry
            // used to persist the whole room again, starving control traffic.
            AcknowledgeRegistration(R,C,O->second);return;
        }
        if (!Core.UpdateLifecycle(C->Player,V.Header.Object,V.Header.OwnerEpoch,static_cast<Life>(State),Time)) return;
        break;
    }
    default: return; // Gameplay values never execute or transit through this service.
    }
    // The existing 50 ms room poll commits and publishes the ordered mutations
    // together. Per-message serialization/persistence starves a large handoff
    // batch as the lifetime directory grows during normal gameplay.
    R.Dirty=true;
}
void Coordinator::State::Lost(Id Match,Id Player,Session* Expected)
{
    const auto I=Rooms.find(Match);if (I==Rooms.end()) return;auto& R=I->second;
    if (const auto C=R.Connections.find(Player);C!=R.Connections.end())
    {
        const auto Existing=C->second.lock();if (Existing && Existing.get()!=Expected) return;
        R.Connections.erase(C);R.Disconnected[Player]=Now();
    }
    // Socket loss alone is not a player exit: peers may still exchange data.
}
void Coordinator::State::Publish(HostedRoom& R,double Time)
{
    auto& Core=*R.Core;const auto Actions=Core.TakeOwnershipActions(Time);
    Writer Groups;const auto Values=Core.ResourceGroups();Groups.U32(static_cast<std::uint32_t>(Values.size()));
    for (const auto& [Key,G]:Values) WriteResourceGroup(Groups,G);
    auto GroupBytes=Groups.Take();
    R.OwnershipOutbox.insert(R.OwnershipOutbox.end(),Actions.begin(),Actions.end());
    if (!Persist(R)) return;
    for (const auto& [Player,Keys]:R.PendingRegistrationReceipts)
        if (const auto Connection=R.Connections.find(Player);Connection!=R.Connections.end())
            if (const auto To=Connection->second.lock())
                for (const auto& Key:Keys)
                    if (const auto O=Core.Objects().find(Key);O!=Core.Objects().end()) To->Send(Registration(R,O->second));
    R.PendingRegistrationReceipts.clear();
    if (Core.Membership() && R.PublishedMembership!=Core.Membership()->Version)
    {R.PublishedMembership=Core.Membership()->Version;SendMembership(R);}
    std::set<Id> CommittedGroups;bool CommittedIndependent=false;
    for (const auto& A:R.OwnershipOutbox)
    {
        Acceptance.ObserveOwnership({{"object",A.Transfer.Object.StableId.ToString()},{"transfer",A.Transfer.IdValue},
            {"action",static_cast<unsigned>(A.Kind)},{"reason",static_cast<unsigned>(A.Transfer.Reason)},
            {"source",A.Transfer.Source.ToString()},{"target",A.Transfer.Target.ToString()},{"owner_version",A.Transfer.NextOwnerVersion}});
        if (A.Kind==HandoffActionKind::Commit)
        {
            if (A.Transfer.Group.Valid()) {if (!CommittedGroups.insert(A.Transfer.Group).second) continue;}
            else {if (CommittedIndependent) continue;CommittedIndependent=true;}
            std::vector<const HandoffAction*> Batch;
            for (const auto& B:R.OwnershipOutbox) if (B.Kind==HandoffActionKind::Commit
                && (A.Transfer.Group.Valid()?B.Transfer.Group==A.Transfer.Group:!B.Transfer.Group.Valid())) Batch.push_back(&B);
            Writer W;W.U32(static_cast<std::uint32_t>(Batch.size()));
            for (const auto* B:Batch) {W.Blob(EncodeHandoff(B->Transfer));WriteAuthority(W,Core.Objects().at(B->Transfer.Object).Grant);}
            Broadcast(R,Control(R,MessageKind::HandoffCommit,W.Take()));
        }
        else
        {
        if (A.Kind==HandoffActionKind::PrepareTarget)
        {
            const auto& Object=Core.Objects().at(A.Transfer.Object);
            // An initial assignment and its first handoff can share this
            // publication. Receivers need that current grant before Prepare.
            if (Object.Grant.OwnerEpoch==A.Transfer.PreviousOwnerVersion)
            {
                if (const auto Published=R.PublishedGrants.find(A.Transfer.Object);
                    Published==R.PublishedGrants.end() || !SameAuthorityEncoding(Published->second,Object.Grant))
                {
                    Broadcast(R,Registration(R,Object));
                    R.PublishedGrants[A.Transfer.Object]=Object.Grant;
                }
            }
        }
        const auto Kind=A.Kind==HandoffActionKind::PrepareTarget?MessageKind::HandoffPrepare
            :A.Kind==HandoffActionKind::FreezeSource?MessageKind::HandoffFreeze
            :MessageKind::HandoffCancel;
        Writer W;W.Blob(EncodeHandoff(A.Transfer));
        auto V=Control(R,Kind,W.Take());V.Header.Object=A.Transfer.Object;
        // Freeze has no observer-side work. Keep Prepare/Cancel and the final
        // grants visible to everyone, but deliver this command only to its
        // actual source so a large leave does not repeat ignored work sixfold.
        if (A.Kind==HandoffActionKind::FreezeSource)
        {
            if (const auto I=R.Connections.find(A.Transfer.Source);I!=R.Connections.end())
                if (const auto Source=I->second.lock()) Source->Send(V);
        }
        else Broadcast(R,V);
        }

    }
    R.OwnershipOutbox.clear();
    bool GrantsChanged=false;
    for (const auto& K:Core.PendingDirectoryUpdates())
    {
        const auto Object=Core.Objects().find(K);
        if (Object==Core.Objects().end()) {R.PublishedGrants.erase(K);continue;}
        const auto& O=Object->second;
        if (const auto Published=R.PublishedGrants.find(K);
            Published!=R.PublishedGrants.end() && SameAuthorityEncoding(Published->second,O.Grant)) continue;
        R.PublishedGrants[K]=O.Grant;GrantsChanged=true;
        Broadcast(R,Registration(R,O));
    }
    Core.AcknowledgeDirectoryUpdates();
    // Ordered control delivery installs every grant before releasing the group
    // recovery fence. An installed snapshot alone never activates a simulator.
    if (GroupBytes!=R.PublishedResourceGroups)
    {R.PublishedResourceGroups=GroupBytes;Broadcast(R,Control(R,MessageKind::ResourceGroups,std::move(GroupBytes)));}
    if (Core.Membership() && R.PublishedMembership!=Core.Membership()->Version)
    {R.PublishedMembership=Core.Membership()->Version;SendMembership(R);}
    if (R.PublishedPhase!=Core.Phase())
    {
        R.PublishedPhase=Core.Phase();
        if (Core.Phase()==RoomPhase::Running) {Roster(R);Broadcast(R,Control(R,MessageKind::MembershipCommit));}
        else if (Core.Phase()==RoomPhase::Aborted) {Writer W;W.Text(Core.Failure());Broadcast(R,Control(R,MessageKind::MatchResult,W.Take()));}
    }
    const bool HasUnreadyConnection=std::any_of(R.Connections.begin(),R.Connections.end(),[&](const auto& Entry)
    {
        const auto Member=Core.Members().find(Entry.first);
        return !Entry.second.expired() && Member!=Core.Members().end() && !Member->second.Ready;
    });
    if (GrantsChanged && HasUnreadyConnection)
    {
        Writer W;W.U32(static_cast<std::uint32_t>(Core.Objects().size()));W.U8(static_cast<std::uint8_t>(Core.Phase()));
        W.U32(static_cast<std::uint32_t>(Core.Ownership().Transfers().size()));for (const auto& [K,T]:Core.Ownership().Transfers()) W.Blob(EncodeHandoff(T));
        // Full reconciliation is a join/readiness boundary. Ready peers already
        // receive ordered handoff deltas; resending the whole transfer set on
        // every projectile birth/retirement amplifies a large selection burst.
        // A joining connection's stream emits its own final boundary. An
        // interleaved delta must not announce completion before its directory.
        const auto End=Control(R,MessageKind::DirectoryComplete,W.Take());
        for (const auto& [Player,Weak]:R.Connections)
            if (const auto Member=Core.Members().find(Player);Member!=Core.Members().end() && !Member->second.Ready)
                if (auto Connection=Weak.lock();Connection && !Connection->DirectoryNext) Connection->Send(End);
    }
}
void Coordinator::State::Poll()
{
    const double Time=Now();boost::json::array RoomStates;bool AllReady=!Rooms.empty();
    double ProposeMilliseconds=0,CorePollMilliseconds=0,PublishMilliseconds=0;
    const bool CaptureReviewState=Acceptance.NeedsSnapshot();
    for (auto& [Match,R]:Rooms)
    {
        auto& Core=*R.Core;
        for (const auto& [P,LostAt]:R.Disconnected)
        {
            if (Time-LostAt<10 || !Core.Members().at(P).Online) continue;
            unsigned Reports=0;bool Seen=false;
            for (const auto& [Other,Weak]:R.Connections) if (Other!=P && !Weak.expired())
            {
                const auto Report=R.PeerPresence.find({Other,P});
                if (Report!=R.PeerPresence.end() && Time-Report->second.second<3) {++Reports;Seen=Seen || Report->second.first;}
            }
            if (Reports && !Seen && Core.BeginLeave(P,true,Time)) {Roster(R);R.Dirty=true;break;}
        }
        unsigned Red=0,Blue=0;
        for (const auto& [P,M]:Core.Members()) if (M.Online && M.Ready && M.Seat!=Role::Observer)
        {if (M.Faction==Team::Red) ++Red;else ++Blue;}
        const bool InitialReady=Red>=Settings.ExpectedRed && Blue>=Settings.ExpectedBlue;
        if (InitialReady) Core.SetInitialRosterReady(true);
        if (InitialReady || Core.Phase()!=RoomPhase::Preparing)
        {
            const double ProposeStarted=Now();const auto Offers=Core.Propose(Time);
            ProposeMilliseconds+=(Now()-ProposeStarted)*1000;
            if (!Offers.empty())
            {
                R.Dirty=true;if (!Persist(R)) continue;
                for (const auto& O:Offers)
                {
                    const auto& Registration=Core.Objects().at(O.Object);Writer W;WriteAuthority(W,Registration.Grant);
                    W.Text(Registration.Definition);W.U32(Registration.Ordinal);W.Key(Registration.Parent);W.Identity(O.ProposedOwner);W.U64(O.ProposedEpoch);
                    auto V=Control(R,MessageKind::AssignmentOffer,W.Take());V.Header.Object=O.Object;Broadcast(R,V);
                }
            }
        }
        if (const auto& Change=Core.Membership();Change && Time>=Change->Deadline)
        {
            R.MembershipDeadlineFailures.try_emplace(Change->Version,boost::json::object{
                {"version",Change->Version},{"player",Change->Player.ToString()},
                {"operation",unsigned(Change->Operation)},{"abrupt",Change->Abrupt}});
            if (R.MembershipDeadlineFailures.size()>64) R.MembershipDeadlineFailures.erase(R.MembershipDeadlineFailures.begin());
        }
        const double CorePollStarted=Now();Core.Poll(Time);CorePollMilliseconds+=(Now()-CorePollStarted)*1000;
        const double PublishStarted=Now();Publish(R,Time);PublishMilliseconds+=(Now()-PublishStarted)*1000;
        AllReady=AllReady && Core.Phase()==RoomPhase::Running;
        if (CaptureReviewState)
        {
            boost::json::array Members,Objects,Transfers;
            for (const auto& [P,M]:Core.Members()) Members.emplace_back(boost::json::object{{"player",P.ToString()},{"team",unsigned(M.Faction)},
                {"seat",unsigned(M.Seat)},{"seat_version",M.SeatVersion},{"online",M.Online},{"ready",M.Ready},
                {"simulation_ms",Core.Ownership().SmoothedLoad(P).value_or(-1)}});
            std::uint64_t RetiredTransient=0;
            for (const auto& [K,O]:Core.Objects())
            {
                if (O.Grant.State==Life::Destroyed && (O.Grant.Policy==OwnershipPolicy::RetainedSource || O.Grant.Policy==OwnershipPolicy::ParentWork))
                {++RetiredTransient;continue;}
                const auto A=Core.Ownership().Activities().find(K);const bool Selected=A!=Core.Ownership().Activities().end() && A->second.Selected;
                const double Idle=A==Core.Ownership().Activities().end()?0:std::max(0.,Core.Ownership().ActiveSeconds(Time)-std::max(A->second.LastOperation,A->second.LastDeselection));
                Objects.emplace_back(boost::json::object{{"id",K.StableId.ToString()},{"generation",K.Generation},{"definition",O.Definition},
                    {"owner",O.Grant.SimOwner.ToString()},{"controller",O.Grant.Controller.ToString()},{"owner_version",O.Grant.OwnerEpoch},{"life",unsigned(O.Grant.State)},
                    {"policy",unsigned(O.Grant.Policy)},{"ownership_group",O.Grant.OwnershipGroup.ToString()},{"group_version",O.Grant.GroupEpoch},{"selected",Selected},{"idle_seconds",Idle}});
            }
            for (const auto& [K,T]:Core.Ownership().Transfers()) Transfers.emplace_back(boost::json::object{{"id",T.IdValue},
                {"object",K.StableId.ToString()},{"reason",unsigned(T.Reason)},{"source",T.Source.ToString()},{"target",T.Target.ToString()},
                {"target_prepared",T.TargetPrepared},{"frozen_state",T.FrozenState},{"installed_state",T.InstalledState},{"deadline",T.Deadline}});
            boost::json::object State{{"match",Match.ToString()},{"room_phase",unsigned(Core.Phase())},{"members",std::move(Members)},
                {"objects",std::move(Objects)},{"handoffs",std::move(Transfers)},{"membership_paused",Core.Membership().has_value()},
                {"directory_records",Core.Objects().size()},{"retired_transient_records",RetiredTransient}};
            boost::json::array DeadlineFailures;
            for (const auto& [Version,Failure]:R.MembershipDeadlineFailures) DeadlineFailures.emplace_back(Failure);
            State["membership_deadline_failures"]=std::move(DeadlineFailures);
            if (const auto& Change=Core.Membership())
            {
                boost::json::array Waiting;for (const auto& P:Change->Waiting) Waiting.emplace_back(P.ToString());
                State["membership"]=boost::json::object{{"version",Change->Version},{"player",Change->Player.ToString()},
                    {"operation",unsigned(Change->Operation)},{"abrupt",Change->Abrupt},{"seconds_remaining",Change->Deadline-Time},{"waiting",std::move(Waiting)}};
            }
            if (!Core.Failure().empty()) State["failure"]=Core.Failure();RoomStates.emplace_back(std::move(State));
        }
    }
    Acceptance.Record("service_poll",(Now()-Time)*1000);
    boost::json::object ReviewState{{"ready",AllReady},{"last_service_poll_ms",(Now()-Time)*1000}};
    ReviewState["last_service_propose_ms"]=ProposeMilliseconds;
    ReviewState["last_service_core_poll_ms"]=CorePollMilliseconds;
    ReviewState["last_service_publish_ms"]=PublishMilliseconds;
    if (CaptureReviewState) ReviewState["rooms"]=std::move(RoomStates);
    if (Acceptance.Tick(std::move(ReviewState))) {Stopping=true;Context.stop();return;}
    Timer.expires_after(std::chrono::milliseconds(50));Timer.async_wait([Self=shared_from_this()](boost::system::error_code EC){if (!EC) Self->Poll();});
}
Coordinator::Coordinator(asio::io_context& Context,Config Settings):Impl(std::make_shared<State>(Context,std::move(Settings))) {}
Coordinator::~Coordinator() {Impl->Stopping=true;boost::system::error_code EC;Impl->Listener.close(EC);Impl->Timer.cancel();}
bool Coordinator::Start(std::string& Error) {return Impl->Start(Error);}
}
