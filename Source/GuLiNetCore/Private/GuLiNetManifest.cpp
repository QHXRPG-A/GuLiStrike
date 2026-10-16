#include "GuLiNetCapability.h"
#include "GuLiNetManifest.h"
#include "GuLiNetOwnerState.h"
#include <tuple>
#include "GuLiNetHash.h"
#include <charconv>
#include <cmath>
#include <limits>

namespace GuLiNet
{
namespace
{
bool Bounded(const std::string& V, std::size_t N)
{ return !V.empty() && V.size() <= N && V.find('\0') == std::string::npos; }
}
std::string EncodeManifestNumber(double Value)
{
    if (!std::isfinite(Value)) return {};
    char Buffer[64];
    const auto Result = std::to_chars(Buffer, Buffer + sizeof(Buffer), Value,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    return Result.ec == std::errc{} ? std::string(Buffer, Result.ptr) : std::string{};
}
bool DecodeManifestNumber(std::string_view Text, double& Out)
{
    if (Text.empty() || Text.size() > 64) return false;
    double Value;
    const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value, std::chars_format::general);
    if (Result.ec != std::errc{} || Result.ptr != Text.data() + Text.size() || !std::isfinite(Value)) return false;
    Out = Value; return true;
}
bool ReadOwnershipSettings(const RuntimeManifest& V,OwnershipSettings& Out,std::string& Error)
{
    OwnershipSettings C;
    const auto Read=[&](const char* Key,double& Value){if (const auto I=V.Numbers.find(Key);I!=V.Numbers.end()) Value=I->second;};
    Read("ownership/idle_seconds",C.IdleSeconds);Read("ownership/evaluation_seconds",C.EvaluationSeconds);
    Read("ownership/handoff_timeout_seconds",C.HandoffTimeoutSeconds);Read("ownership/minimum_relative_gap",C.MinimumRelativeGap);
    Read("ownership/minimum_gap_ms",C.MinimumGapMilliseconds);
    double Batch=C.MaximumBackgroundBatch;Read("ownership/maximum_background_batch",Batch);
    if (!std::isfinite(Batch) || Batch<1 || Batch>4096 || std::floor(Batch)!=Batch) {Error="Invalid ownership batch size";return false;}
    C.MaximumBackgroundBatch=static_cast<std::uint32_t>(Batch);OwnershipScheduler Check;
    if (!Check.Configure(C)) {Error="Invalid ownership configuration";return false;}Out=C;return true;
}
bool ValidateManifest(const RuntimeManifest& V, std::string& Error)
{
    auto Reject = [&](const char* Why) { Error = Why; return false; };
    if (V.Version != ManifestVersion || !Bounded(V.MapPackage, 512) || V.MapPackage.rfind("/Game/", 0) != 0)
        return Reject("Invalid runtime manifest version or map package");
    if (V.Objects.empty() || V.Objects.size() > 100000 || V.Rules.empty() || V.Rules.size() > 16384
        || V.Numbers.size() > 1000000 || V.Strings.size() > 100000
        || !V.StartingRed.Nonnegative() || !V.StartingBlue.Nonnegative())
        return Reject("Invalid runtime manifest counts or initial resources");
    std::set<std::pair<ObjectKind,std::uint32_t>> Ordinals;
    for (const auto& [K, O] : V.Objects)
    {
        const bool NoHealth=O.Vitals.Value==0 && O.Vitals.Maximum==0
            && O.Vitals.Shield==0 && O.Vitals.MaximumShield==0 && O.Vitals.Version==0 && !O.Vitals.Dead;
        if (!K.Valid() || K.Kind > ObjectKind::BusinessWork || K != O.Grant.Object
            || (!Playable(O.Grant.Faction) && !(O.Grant.Faction==Team::None && !O.Capabilities.empty() && ValidateObjectCapabilities(O)))
            || O.Grant.Controller.Valid() || O.Grant.SimOwner.Valid() || O.Grant.OwnerEpoch != 0
            || O.Grant.ControlEpoch != 1 || O.Grant.State != Life::Registered || O.Grant.SourceLease.Valid()
            || !std::isfinite(O.Grant.EstimatedCost) || O.Grant.EstimatedCost < 0 || !Bounded(O.Definition, 256)
            || !std::isfinite(O.X) || !std::isfinite(O.Y) || !std::isfinite(O.Z)
            || (!NoHealth && (!ValidHealth(O.Vitals) || O.Vitals.Dead || O.Vitals.Value <= 0))
            || O.BornAt != 0 || O.DiedAt != UINT64_MAX || O.Phased || O.Parent.Valid() || O.RelationVersion
            || !O.FrozenWork.empty() || O.Definition==TransactionWorkDefinition
            || !std::isfinite(O.FacingYaw) || (K.Kind==ObjectKind::Soldier && !O.Ordinal)
            || (O.Ordinal && !Ordinals.emplace(OrdinalScope(K.Kind),O.Ordinal).second)
            || !std::isfinite(O.ShieldRadius) || O.ShieldRadius < 0)
            return Reject("Invalid startup object or preassigned authority");
    }
    for (const auto& [K, R] : V.Rules)
        if (!Bounded(K, 256) || !ValidRule(R))
        {
            Error = "Invalid numerical rule: " + K;
            return false;
        }
    for (const auto& [K, N] : V.Numbers)
        if (!Bounded(K, 512) || !std::isfinite(N)) return Reject("Invalid numerical catalog entry");
    for (const auto& [K, S] : V.Strings)
        if (!Bounded(K,512) || S.size()>4096 || S.find('\0')!=std::string::npos)
            return Reject("Invalid authored catalog selector");
    if (V.Strings.contains("adapter/contract"))
    {
        for (const auto& [Key,Rate]:std::map<std::string,double>{{"simulation/logic_hz",static_cast<double>(TicksPerSecond)},
            {"simulation/mass_hz",MassSimulationHz},{"simulation/wingman_hz",WingmanSimulationHz},{"network/wingman/motion_hz",WingmanMotionHz}})
            if (!V.Numbers.contains(Key) || V.Numbers.at(Key)!=Rate) return Reject("Incompatible simulation or motion cadence");
    }
    OwnershipSettings Ownership;std::map<std::string,OwnershipDefinition> Definitions;
    if (!ReadOwnershipSettings(V,Ownership,Error) || !ReadOwnershipDefinitions(V,Definitions,Error)
        || !ValidateManifestCapabilities(V,Error)) return false;
    ResourceLedgerState Catalog;
    if (!ReadResourceCatalog(V,Catalog,Error)) return false;
    std::map<std::string,CompiledUpgradeCatalog> Upgrades;
    if (!ReadUpgradeCatalogs(V,Upgrades,Error)) return false;
    for (const auto& [_,Rule]:V.Rules) if (Rule.Kind==TransactionKind::Upgrade && !Upgrades.contains(Rule.UpgradeCatalog))
        return Reject("Upgrade rule references an unavailable catalog");
    for (const auto& [_,Rule]:V.Rules) if (!Rule.RequiredCapabilityCatalog.empty())
    {
        const auto RequiredCatalog=Upgrades.find(Rule.RequiredCapabilityCatalog);bool Found=false;
        if (RequiredCatalog!=Upgrades.end()) for (const auto& Group:RequiredCatalog->second.Data.Groups)
            if (Group.Configuration==Rule.RequiredCapabilityGroup && Group.Executable) for (const auto& Capability:Group.Capabilities) Found|=Capability.Id==Rule.RequiredCapability;
        if (!Found) return Reject("Rule prerequisite references an unavailable executable capability");
    }
    Error.clear(); return true;
}
bool ReadUpgradeCatalogs(const RuntimeManifest& V,std::map<std::string,CompiledUpgradeCatalog>& Out,std::string& Error)
{
    constexpr std::string_view Prefix="upgrade_catalog/";
    const auto Reject=[&](const char* Why) {Error=Why;return false;};
    const auto Integer=[&](const std::string& Key,double Low,double High,std::uint64_t& Result)
    {
        const auto It=V.Numbers.find(Key);if (It==V.Numbers.end() || !std::isfinite(It->second)
            || It->second<Low || It->second>High || std::floor(It->second)!=It->second) return false;
        Result=static_cast<std::uint64_t>(It->second);return true;
    };
    std::map<std::string,CompiledUpgradeCatalog> Result;std::set<std::string> NumberKeys,StringKeys;std::size_t TotalBytes=0;
    for (const auto& [Key,_]:V.Numbers) if (Key.starts_with(Prefix) && Key.ends_with("/chunks"))
    {
        const auto Domain=Key.substr(Prefix.size(),Key.size()-Prefix.size()-7);const auto Base=std::string(Prefix)+Domain+"/";
        if (!Bounded(Domain,192) || Result.size()>=16) return Reject("Upgrade catalog identity or count exceeds capacity");
        std::uint64_t Schema=0,Revision=0,Chunks=0;
        if (!Integer(Base+"schema",UpgradeCatalogSchema,UpgradeCatalogSchema,Schema)
            || !Integer(Base+"revision",1,9007199254740991.0,Revision)
            || !Integer(Base+"chunks",1,MaximumUpgradeCatalogBytes/2048,Chunks)) return Reject("Invalid upgrade catalog metadata");
        NumberKeys.insert(Base+"schema");NumberKeys.insert(Base+"revision");NumberKeys.insert(Key);Bytes Data;
        for (std::uint64_t Index=0;Index<Chunks;++Index)
        {
            const auto Chunk=Base+"chunk/"+std::to_string(Index);StringKeys.insert(Chunk);const auto It=V.Strings.find(Chunk);
            if (It==V.Strings.end() || It->second.empty() || It->second.size()>4096 || It->second.size()%2
                || (Index+1<Chunks && It->second.size()!=4096)) return Reject("Missing or truncated upgrade catalog chunk");
            const auto Hex=[](char C)->int {return C>='0' && C<='9'?C-'0':C>='a' && C<='f'?C-'a'+10:C>='A' && C<='F'?C-'A'+10:-1;};
            for (std::size_t At=0;At<It->second.size();At+=2)
            {const int A=Hex(It->second[At]),B=Hex(It->second[At+1]);if (A<0 || B<0) return Reject("Invalid upgrade catalog chunk encoding");Data.push_back(static_cast<std::uint8_t>((A<<4)|B));}
        }
        TotalBytes+=Data.size();if (TotalBytes>MaximumUpgradeCatalogBytes) return Reject("Shared upgrade catalogs exceed total capacity");
        UpgradeCatalog Catalog;CompiledUpgradeCatalog Compiled;
        if (!DecodeUpgradeCatalog(Data,Catalog) || Catalog.Version!=Revision || !CompileUpgradeCatalog(Catalog,Compiled,Error))
            return Reject("Invalid shared upgrade catalog or revision");
        Result.emplace(Domain,std::move(Compiled));
    }
    for (const auto& [Key,_]:V.Numbers) if (Key.starts_with(Prefix) && !NumberKeys.contains(Key)) return Reject("Unknown upgrade catalog numerical field");
    for (const auto& [Key,_]:V.Strings) if (Key.starts_with(Prefix) && !StringKeys.contains(Key)) return Reject("Unknown or extra upgrade catalog chunk");
    Error.clear();Out=std::move(Result);return true;
}
bool ReadResourceCatalog(const RuntimeManifest& V,ResourceLedgerState& Out,std::string& Error)
{
    if (!V.Numbers.contains("resource_map/node_count")) { Out={}; return true; }
    const auto Number=[&](const std::string& Key,double Low,double High,std::uint32_t& OutValue)
    {
        const auto It=V.Numbers.find(Key);
        if (It==V.Numbers.end() || !std::isfinite(It->second) || It->second<Low || It->second>High || std::floor(It->second)!=It->second) return false;
        OutValue=static_cast<std::uint32_t>(It->second); return true;
    };
    auto Reject=[&](const char* Why){Error=Why; return false;};
    std::uint32_t Nodes=0,Clusters=0,Territories=0;
    if (!Number("resource_map/node_count",1,100000,Nodes) || !Number("resource_map/Clusters/count",1,100000,Clusters)
        || !Number("resource_map/Territories/count",1,64,Territories)) return Reject("Resource catalog counts are invalid");
    std::vector<Team> Owners; std::map<std::uint32_t,std::uint32_t> TerritoryForCluster,TypeForCluster;
    std::map<std::uint32_t,std::pair<std::uint32_t,std::uint32_t>> Ranges;
    std::set<std::uint32_t> CoveredNodes;
    for (std::uint32_t I=0;I<Territories;++I)
    {
        std::uint32_t Owner=0;
        if (!Number("resource_map/Territories["+std::to_string(I)+"]/InitialOwner",0,2,Owner)) return Reject("Resource territory owner missing");
        Owners.push_back(static_cast<Team>(Owner));
    }
    for (std::uint32_t I=0;I<Clusters;++I)
    {
        const std::string Prefix="resource_map/Clusters["+std::to_string(I)+"]/";
        std::uint32_t Id=0,Territory=0,Type=0,First=0,Count=0;
        if (!Number(Prefix+"ClusterId",1,UINT32_MAX,Id) || !Number(Prefix+"TerritoryIndex",0,Territories-1,Territory)
            || !Number(Prefix+"ResourceType",0,1,Type) || !Number(Prefix+"FirstNodeIndex",0,Nodes-1,First)
            || !Number(Prefix+"NodeCount",1,Nodes,Count) || Count>Nodes-First
            || !TerritoryForCluster.emplace(Id,Territory).second) return Reject("Resource cluster accounting identity is invalid");
        TypeForCluster.emplace(Id,Type); Ranges.emplace(Id,std::pair{First+1,First+Count});
        for (std::uint32_t Node=First+1;Node<=First+Count;++Node)
            if (!CoveredNodes.insert(Node).second) return Reject("Resource cluster node ranges overlap");
    }
    if (CoveredNodes.size()!=Nodes) return Reject("Resource cluster node ranges are incomplete");
    std::map<std::uint32_t,OreDefinition> Definitions;
    for (std::uint32_t Id=1;Id<=Nodes;++Id)
    {
        const std::string Prefix="ore/"+std::to_string(Id)+"/";
        std::uint32_t Cluster=0,Type=0,Amount=0;
        if (!Number(Prefix+"cluster",1,UINT32_MAX,Cluster) || !TerritoryForCluster.contains(Cluster)
            || !Number(Prefix+"type",0,1,Type) || Type!=TypeForCluster.at(Cluster) || !Number(Prefix+"amount",1,3,Amount)
            || Id<Ranges.at(Cluster).first || Id>Ranges.at(Cluster).second) return Reject("Resource node accounting identity is invalid");
        Definitions.emplace(Id,OreDefinition{Cluster,TerritoryForCluster.at(Cluster),static_cast<Commodity>(Type),static_cast<std::uint8_t>(Amount)});
    }
    ResourceLedger Ledger;
    if (!Ledger.Configure(std::move(Definitions),std::move(Owners))) return Reject("Invalid resource conservation catalog");
    Out=Ledger.Snapshot(); Error.clear(); return true;
}
bool ConfigureOwnerResources(OwnerState& Target,const RuntimeManifest& V,std::string& Error)
{
    std::map<std::string,CompiledUpgradeCatalog> Upgrades;
    if (!ReadUpgradeCatalogs(V,Upgrades,Error) || !Target.ConfigureUpgradeCatalogs(Upgrades))
    {Error="Client shared upgrade catalog could not be installed";return false;}
    ResourceLedgerState Catalog;
    if (!ReadResourceCatalog(V,Catalog,Error)) return false;
    if (!Catalog.Configured) return true;
    if (!Target.ConfigureResourceCatalog(std::move(Catalog.Nodes),std::move(Catalog.Owners))) { Error="Client resource catalog could not be installed"; return false; }
    // Portable profiles describe a capability, including definitions with no initial object.
    // Numeric capability profiles do not depend on a UE class or concrete content ID.
    const auto Integer=[&](const std::string& Key,double Low,double High,std::uint32_t& Out)
    {
        const auto It=V.Numbers.find(Key);
        if (It==V.Numbers.end() || !std::isfinite(It->second) || It->second<Low || It->second>High || std::floor(It->second)!=It->second) return false;
        Out=static_cast<std::uint32_t>(It->second);return true;
    };
    constexpr std::string_view Prefix="resource/profile/",Suffix="/kind";
    std::map<std::pair<std::string,ResourceInventoryKind>,std::tuple<ObjectKind,std::uint32_t,Tick>> Profiles;
    std::set<std::string> ProfileFields;
    for (const auto& [Key,Value]:V.Numbers) if (Key.starts_with(Prefix) && Key.ends_with(Suffix))
    {
        const std::string Profile=Key.substr(Prefix.size(),Key.size()-Prefix.size()-Suffix.size());
        const auto Separator=Profile.rfind('/');
        if (Separator==std::string::npos) {Error="Resource profile must declare an inventory capability";return false;}
        const std::string Definition=Profile.substr(0,Separator),Name=Profile.substr(Separator+1);
        if (Name!="cargo" && Name!="processor") {Error="Unknown resource inventory capability: "+Name;return false;}
        const auto Capability=Name=="cargo"?ResourceInventoryKind::Cargo:ResourceInventoryKind::Processor;
        const std::string Base=std::string(Prefix)+Profile+"/";
        std::uint32_t Kind=0,Capacity=0,Period=0;
        if (!Integer(Key,1,static_cast<unsigned>(ObjectKind::BusinessWork),Kind)
            || !Integer(Base+"capacity",1,10000000,Capacity) || !Integer(Base+"period_ticks",1,60*86400,Period)
            || !Target.RegisterResourceActorProfile(Capability,static_cast<ObjectKind>(Kind),Definition,Capacity,Period))
        {Error="Invalid portable resource capability profile: "+Definition;return false;}
        Profiles.emplace(std::pair{Definition,Capability},std::tuple{static_cast<ObjectKind>(Kind),Capacity,Tick(Period)});
        ProfileFields.insert(Key);ProfileFields.insert(Base+"capacity");ProfileFields.insert(Base+"period_ticks");
    }
    for (const auto& [Key,Value]:V.Numbers) if (Key.starts_with(Prefix) && !ProfileFields.contains(Key))
    {Error="Incomplete or unknown resource profile field: "+Key;return false;}
    // Authorize mechanisms from the exported capability declaration, never a
    // Miner/Building object-kind assumption. Missing profiles fail at startup.
    for (const auto& [Id,Rule]:V.Rules) if (IsResourceTransaction(Rule.Kind) || IsMiningTransaction(Rule.Kind))
    {
        const auto Capability=Rule.Kind==TransactionKind::Process?ResourceInventoryKind::Processor:ResourceInventoryKind::Cargo;
        if (!Profiles.contains({Rule.SourceDefinition,Capability})
            || (Rule.Kind==TransactionKind::Deliver && !Profiles.contains({Rule.TargetDefinition,ResourceInventoryKind::Processor})))
        {Error="Resource rule omits its required inventory capability: "+Id;return false;}
    }
    for (const auto& [K,O]:Target.Objects())
    {
        for (const auto Capability:{ResourceInventoryKind::Cargo,ResourceInventoryKind::Processor})
        {
            const auto It=Profiles.find({O.Definition,Capability});if (It==Profiles.end()) continue;
            const auto [Kind,Capacity,Period]=It->second;
            if (K.Kind!=Kind || !(Capability==ResourceInventoryKind::Cargo?Target.RegisterResourceMiner(K,Capacity,Period):Target.RegisterResourceFactory(K,Capacity,Period)))
            {Error="Admitted object does not match its resource capability profile: "+O.Definition;return false;}
        }
    }
    Error.clear(); return true;
}
bool ReadOwnershipDefinitions(const RuntimeManifest& V,std::map<std::string,OwnershipDefinition>& Out,std::string& Error)
{
    std::set<std::string> Definitions;
    for (const auto& [K,O]:V.Objects) Definitions.insert(O.Definition);
    for (const auto& [K,R]:V.Rules) {Definitions.insert(R.SourceDefinition);if (!R.TargetDefinition.empty()) Definitions.insert(R.TargetDefinition);}
    std::map<std::string,OwnershipDefinition> Result;
    for (const auto& D:Definitions)
    {
        const auto Family=V.Strings.find("adapter/definition/"+D);
        const std::string Base=Family==V.Strings.end()?"ownership/definition/"+D+"/":"adapter/family/"+Family->second+"/";
        auto Number=[&](const char* Name,unsigned Max,unsigned& N)
        {
            auto I=V.Numbers.find("ownership/definition/"+D+"/"+Name);
            if (I==V.Numbers.end()) I=V.Numbers.find(Base+Name);
            if (I==V.Numbers.end() || !std::isfinite(I->second) || I->second<0 || I->second>Max || std::floor(I->second)!=I->second) return false;
            N=static_cast<unsigned>(I->second);return true;
        };
        unsigned Policy,Seat,Retire;
        if (!Number("ownership_policy",static_cast<unsigned>(OwnershipPolicy::ParentWork),Policy)
            || !Number("controller_seat",static_cast<unsigned>(Role::Observer),Seat) || !Number("retire_on_vacancy",1,Retire))
        {Error="Missing ownership policy for definition: "+D;return false;}
        OwnershipDefinition Entry{static_cast<OwnershipPolicy>(Policy),static_cast<Role>(Seat),Retire!=0};
        auto Group=V.Strings.find("ownership/definition/"+D+"/group");
        if (Group==V.Strings.end()) Group=V.Strings.find(Base+"group");
        if (Group!=V.Strings.end()) Entry.Group=Group->second;
        if ((Entry.Policy==OwnershipPolicy::SharedGroup)!=!Entry.Group.empty()) {Error="Invalid ownership group: "+D;return false;}
        if (Entry.RetireOnVacancy && Entry.Policy!=OwnershipPolicy::ControllerPinned) {Error="Invalid retirement policy: "+D;return false;}
        Result.emplace(D,Entry);
    }
    Result[TransactionWorkDefinition]={OwnershipPolicy::ParentWork,Role::Observer,false};
    Result["state/team-account"]={OwnershipPolicy::SharedGroup,Role::Commander,false,"match/resources"};
    Result["state/resource-cluster"]={OwnershipPolicy::SharedGroup,Role::Observer,false,"match/resources"};
    Result["state/territory"]={OwnershipPolicy::SharedGroup,Role::Observer,false,"match/resources"};
    Out=std::move(Result);return true;
}
bool RuntimeObjects(const RuntimeManifest& M,std::map<ObjectKey,ObjectRecord>& Out,std::string& Error)
{
    std::map<std::string,OwnershipDefinition> Definitions;
    if (!ReadOwnershipDefinitions(M,Definitions,Error)) return false;
    auto Objects=M.Objects;
    const auto Add=[&](ObjectKey K,Team Team,const char* Definition)
    {
        ObjectRecord O;O.Grant.Object=K;O.Grant.Faction=Team;O.Grant.EstimatedCost=0;
        O.Definition=Definition;O.Materialized=false;Objects.emplace(K,std::move(O));
    };
    Add(TeamAccountObject(Team::Red),Team::Red,"state/team-account");Add(TeamAccountObject(Team::Blue),Team::Blue,"state/team-account");
    ResourceLedgerState Resources;if (!ReadResourceCatalog(M,Resources,Error)) return false;
    for (const auto& [N,V]:Resources.Nodes) Add(MiningClusterObject(V.Cluster),Team::None,"state/resource-cluster");
    for (std::uint32_t I=0;I<Resources.Owners.size();++I) Add(TerritoryStateObject(I),Team::None,"state/territory");
    for (auto& [K,O]:Objects)
    {
        const auto& D=Definitions.at(O.Definition);
        O.Grant.Policy=D.Policy;O.Grant.ControllerSeat=D.ControllerSeat;O.Grant.RetireOnVacancy=D.RetireOnVacancy;
        O.Grant.OwnershipGroup=D.Group.empty()?Id{}:StableId("GuLiOwnershipGroup/v1",D.Group);
    }
    Out=std::move(Objects);return true;
}
bool HashManifest(const RuntimeManifest& V, ManifestHashes& Out, std::string& Error)
{
    if (!ValidateManifest(V, Error)) return false;
    Writer Config(16 * 1024 * 1024), Map(16 * 1024 * 1024);
    Config.U32(V.Version); Config.U16(ProtocolVersion); Config.U32(HandoffVersion);
    // Application payload packing is a negotiated configuration feature. An
    // older adapter cannot join with a hash that promises this wire decoder.
    Config.Text("peer-payload/zlib-1");
    WriteResources(Config, V.StartingRed); WriteResources(Config, V.StartingBlue);
    Config.U32(static_cast<std::uint32_t>(V.Rules.size()));
    for (const auto& [K, R] : V.Rules)
    {
        Config.Text(K); Config.U8(static_cast<std::uint8_t>(R.Kind)); Config.Text(R.SourceDefinition);
        Config.F32(R.Damage); WriteResources(Config, R.Amount); Config.U64(R.Cooldown);
        Config.U64(R.MaxEffectLifetime); Config.U32(R.MaxTargets);
        Config.Text(R.TargetDefinition); Config.U8(static_cast<std::uint8_t>(R.SpawnKind));
        Config.F32(R.SpawnHealth); Config.F32(R.SpawnShield); Config.F64(R.SpawnCost); Config.F64(R.SpawnShieldRadius);
        Config.U64(R.MinimumAge); Config.U32(R.MaximumLiveChildren); Config.U8(R.SpawnCompleted ? 1 : 0);
        Config.U32(R.MaximumFactionObjects); Config.U8(static_cast<std::uint8_t>(R.SpawnControllerSeat));
        Config.U8(static_cast<std::uint8_t>(R.Relation)); Config.U8(R.RelationValue ? 1 : 0);Config.Text(R.UpgradeCatalog);Config.Text(R.RequiredCapabilityCatalog);Config.Text(R.RequiredCapabilityGroup);Config.Text(R.RequiredCapability);
        Config.Text(R.Capability);Config.U32(R.CapabilityVersion);Config.Text(R.CapabilityOperation);
        WriteCapabilityStates(Config,R.SpawnCapabilities);WriteUpgradeSelections(Config,R.SpawnUpgrades);
    }
    Config.U32(static_cast<std::uint32_t>(V.Numbers.size()));
    for (const auto& [K, N] : V.Numbers) { Config.Text(K); Config.F64(N); }
    Config.U32(static_cast<std::uint32_t>(V.Strings.size()));
    for (const auto& [K, S] : V.Strings) { Config.Text(K); Config.Text(S); }
    Map.U32(V.Version); Map.Text(V.MapPackage); Map.U32(static_cast<std::uint32_t>(V.Objects.size()));
    for (const auto& [K, O] : V.Objects) { (void)K; WriteObject(Map, O); }
    const auto ConfigSize = Config.Data().size(), MapSize = Map.Data().size();
    const auto ConfigBytes = Config.Take(), MapBytes = Map.Take();
    if (ConfigBytes.empty() || MapBytes.empty())
    {
        Error = "Manifest canonical encoding failed: config=" + std::to_string(ConfigSize)
            + " map=" + std::to_string(MapSize) + " numbers=" + std::to_string(V.Numbers.size())
            + " strings=" + std::to_string(V.Strings.size());
        return false;
    }
    Out.Configuration = Sha256(ConfigBytes); Out.Map = Sha256(MapBytes); return true;
}
std::string DamageRuleId(const std::string& Definition, const std::string& Skill, const std::string& Slot)
{
    // Length prefixes prevent identifiers containing separators from aliasing.
    return "damage/" + std::to_string(Definition.size()) + ":" + Definition + "/"
        + std::to_string(Skill.size()) + ":" + Skill + "/" + std::to_string(Slot.size()) + ":" + Slot;
}
}
