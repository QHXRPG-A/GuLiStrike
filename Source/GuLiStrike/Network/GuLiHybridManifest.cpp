#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/Private/GuLiHybridManifestPortable.h"
#include "GuLiNetCapability.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridCommandRegistry.h"
#include "Network/GuLiHybridLedgerBinding.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include <cmath>
#include <limits>

namespace
{
using namespace GuLiNet;
double Number(const FJsonObject& O, const TCHAR* Key, double Default = 0)
{
    if (!O.HasField(Key)) return Default;
    FString TextValue;
    if (O.TryGetStringField(Key,TextValue))
    {
        double V;
        if (DecodeManifestNumber(TCHAR_TO_UTF8(*TextValue),V)) return V;
    }
    double V; return O.TryGetNumberField(Key, V) ? V : std::numeric_limits<double>::quiet_NaN();
}
void PreciseNumber(FJsonObject& O,const TCHAR* Key,double V)
{ O.SetStringField(Key,UTF8_TO_TCHAR(EncodeManifestNumber(V).c_str())); }
std::string Text(const FJsonObject& O, const TCHAR* Key)
{ FString V; O.TryGetStringField(Key,V); return TCHAR_TO_UTF8(*V); }
bool Integer(double V, double Min, double Max)
{ return std::isfinite(V) && V >= Min && V <= Max && std::floor(V) == V; }
bool ScalarFloat(double V)
{ return std::isfinite(V) && V >= 0 && V <= std::numeric_limits<float>::max(); }
bool Parse(const FJsonObject& Root, RuntimeManifest& M)
{
    const TArray<TSharedPtr<FJsonValue>> *Objects = nullptr, *Rules = nullptr;
    const TSharedPtr<FJsonObject>* Numbers = nullptr;
    const TSharedPtr<FJsonObject>* Strings = nullptr;
    if (Number(Root,TEXT("schema")) != ManifestVersion || Number(Root,TEXT("protocol_version"))!=ProtocolVersion
        || Number(Root,TEXT("handoff_version"))!=HandoffVersion || !Root.TryGetArrayField(TEXT("objects"),Objects)
        || !Root.TryGetArrayField(TEXT("rules"),Rules) || !Root.TryGetObjectField(TEXT("numbers"),Numbers)
        || !Root.TryGetObjectField(TEXT("strings"),Strings)) return false;
    M.MapPackage=Text(Root,TEXT("map_package"));
    for (const auto& Entry : *Objects)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Entry || !Entry->TryGetObject(Object) || !Object || !*Object) return false;
        const auto& V=**Object; ObjectRecord O;
        if (!Id::Parse(Text(V,TEXT("id")),O.Grant.Object.StableId)) return false;
        const auto Kind=Number(V,TEXT("kind")), Generation=Number(V,TEXT("generation"),1), TeamValue=Number(V,TEXT("team"));
        const auto HealthValue=Number(V,TEXT("health")), ShieldValue=Number(V,TEXT("shield")), Supplied=Number(V,TEXT("supplied"));
        const auto Completed=Number(V,TEXT("completed"),1);
        if (!Integer(Kind,1,static_cast<unsigned>(ObjectKind::BusinessWork)) || !Integer(Generation,1,UINT32_MAX)
            || !Integer(TeamValue,0,2) || !Integer(Supplied,0,1) || !Integer(Completed,0,1)
            || !ScalarFloat(HealthValue) || !ScalarFloat(ShieldValue)) return false;
        O.Grant.Object.Kind=static_cast<ObjectKind>(static_cast<unsigned>(Kind));
        O.Grant.Object.Generation=static_cast<uint32>(Generation); O.Grant.Faction=static_cast<Team>(static_cast<unsigned>(TeamValue));
        O.Definition=Text(V,TEXT("definition")); O.Grant.EstimatedCost=Number(V,TEXT("cost"),1);
        const double Ordinal=Number(V,TEXT("ordinal"));
        if (!Integer(Ordinal,0,UINT32_MAX)) return false;
        O.Ordinal=static_cast<uint32>(Ordinal); O.FacingYaw=static_cast<float>(Number(V,TEXT("facing_yaw")));
        O.X=Number(V,TEXT("x")); O.Y=Number(V,TEXT("y")); O.Z=Number(V,TEXT("z"));
        O.Vitals.Maximum=O.Vitals.Value=static_cast<float>(HealthValue); O.Vitals.Version=HealthValue>0?1:0;
        O.Vitals.MaximumShield=O.Vitals.Shield=static_cast<float>(ShieldValue);
        O.ShieldRadius=Number(V,TEXT("shield_radius")); O.Supplied=Supplied!=0; O.Completed=Completed!=0;
        if (!M.Objects.emplace(O.Grant.Object,std::move(O)).second) return false;
    }
    for (const auto& Entry : *Rules)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Entry || !Entry->TryGetObject(Object) || !Object || !*Object) return false;
        const auto& V=**Object; Rule R;
        const auto Kind=Number(V,TEXT("kind")), Damage=Number(V,TEXT("damage"));
        const auto Blue=Number(V,TEXT("blue")), Red=Number(V,TEXT("red")), Cooldown=Number(V,TEXT("cooldown_ticks"));
        const auto Lifetime=Number(V,TEXT("effect_ticks"),600), Targets=Number(V,TEXT("max_targets"),1);
        if (!Integer(Kind,0,static_cast<unsigned>(TransactionKind::Capability)) || Kind==static_cast<unsigned>(TransactionKind::AdmitWork) || !ScalarFloat(Damage)
            || !Integer(Blue,0,INT32_MAX) || !Integer(Red,0,INT32_MAX)
            || !Integer(Cooldown,0,TicksPerSecond*86400) || !Integer(Lifetime,0,TicksPerSecond*86400) || !Integer(Targets,1,4096)) return false;
        R.Kind=static_cast<TransactionKind>(static_cast<unsigned>(Kind)); R.SourceDefinition=Text(V,TEXT("source_definition"));
        R.Damage=static_cast<float>(Damage); R.Amount.Blue=static_cast<int32>(Blue); R.Amount.Red=static_cast<int32>(Red);
        R.Cooldown=static_cast<Tick>(Cooldown); R.MaxEffectLifetime=static_cast<Tick>(Lifetime); R.MaxTargets=static_cast<uint32>(Targets);
        const auto SpawnKind=Number(V,TEXT("spawn_kind")), SpawnHealth=Number(V,TEXT("spawn_health")), SpawnShield=Number(V,TEXT("spawn_shield"));
        const auto MinimumAge=Number(V,TEXT("minimum_age_ticks")), MaximumChildren=Number(V,TEXT("max_live_children"));
        const auto MaximumFaction=Number(V,TEXT("max_faction_objects")), SpawnSeat=Number(V,TEXT("spawn_controller_seat"),3);
        const auto SpawnCompleted=Number(V,TEXT("spawn_completed"),1), Relation=Number(V,TEXT("relation")), RelationValue=Number(V,TEXT("relation_value"));
        if (!Integer(SpawnKind,0,static_cast<unsigned>(ObjectKind::BusinessWork)) || !ScalarFloat(SpawnHealth) || !ScalarFloat(SpawnShield)
            || !Integer(MinimumAge,0,TicksPerSecond*86400) || !Integer(MaximumChildren,0,100000)
            || !Integer(MaximumFaction,0,100000) || !Integer(SpawnSeat,0,static_cast<unsigned>(Role::Observer))
            || !Integer(SpawnCompleted,0,1) || !Integer(Relation,0,static_cast<unsigned>(RelationKind::Parent)) || !Integer(RelationValue,0,1)) return false;
        R.TargetDefinition=Text(V,TEXT("target_definition")); R.SpawnKind=static_cast<ObjectKind>(static_cast<unsigned>(SpawnKind));
        R.SpawnHealth=static_cast<float>(SpawnHealth); R.SpawnShield=static_cast<float>(SpawnShield);
        R.SpawnCost=Number(V,TEXT("spawn_cost"),1); R.SpawnShieldRadius=Number(V,TEXT("spawn_shield_radius"));
        R.MinimumAge=static_cast<Tick>(MinimumAge); R.MaximumLiveChildren=static_cast<uint32>(MaximumChildren);
        R.MaximumFactionObjects=static_cast<uint32>(MaximumFaction); R.SpawnControllerSeat=static_cast<Role>(static_cast<unsigned>(SpawnSeat));
        R.SpawnCompleted=SpawnCompleted!=0; R.Relation=static_cast<RelationKind>(static_cast<unsigned>(Relation)); R.RelationValue=RelationValue!=0;
        const double CapabilityVersion=Number(V,TEXT("capability_version"),0);
        if (!Integer(CapabilityVersion,0,UINT32_MAX)) return false;
        R.Capability=Text(V,TEXT("capability"));R.CapabilityOperation=Text(V,TEXT("capability_operation"));R.CapabilityVersion=static_cast<uint32>(CapabilityVersion);
        R.UpgradeCatalog=Text(V,TEXT("upgrade_catalog"));
        R.RequiredCapabilityCatalog=Text(V,TEXT("required_capability_catalog"));R.RequiredCapabilityGroup=Text(V,TEXT("required_capability_group"));R.RequiredCapability=Text(V,TEXT("required_capability"));
        if (!M.Rules.emplace(Text(V,TEXT("id")),R).second) return false;
    }
    const auto RB=Number(Root,TEXT("red_team_blue")), RR=Number(Root,TEXT("red_team_red"));
    const auto BB=Number(Root,TEXT("blue_team_blue")), BR=Number(Root,TEXT("blue_team_red"));
    if (!Integer(RB,0,INT32_MAX) || !Integer(RR,0,INT32_MAX) || !Integer(BB,0,INT32_MAX) || !Integer(BR,0,INT32_MAX)) return false;
    M.StartingRed.Blue=static_cast<int32>(RB); M.StartingRed.Red=static_cast<int32>(RR);
    M.StartingBlue.Blue=static_cast<int32>(BB); M.StartingBlue.Red=static_cast<int32>(BR);
    for (const auto& Entry : (*Numbers)->Values)
    {
        FString S; double V;
        if (!Entry.Value || !Entry.Value->TryGetString(S) || !DecodeManifestNumber(TCHAR_TO_UTF8(*S),V)) return false;
        M.Numbers.emplace(TCHAR_TO_UTF8(*Entry.Key),V);
    }
    for (const auto& Entry : (*Strings)->Values)
    { FString V; if (!Entry.Value || !Entry.Value->TryGetString(V)) return false; M.Strings.emplace(TCHAR_TO_UTF8(*Entry.Key),TCHAR_TO_UTF8(*V)); }
    std::string Why;return InitializeManifestCapabilities(M,Why);
}
TSharedRef<FJsonObject> ToJson(const RuntimeManifest& M, const ManifestHashes& H)
{
    auto Root=MakeShared<FJsonObject>(); Root->SetNumberField(TEXT("schema"),M.Version);
    Root->SetNumberField(TEXT("protocol_version"),ProtocolVersion); Root->SetNumberField(TEXT("handoff_version"),HandoffVersion);
    Root->SetStringField(TEXT("map_package"),UTF8_TO_TCHAR(M.MapPackage.c_str()));
    Root->SetStringField(TEXT("config_hash"),UTF8_TO_TCHAR(H.Configuration.c_str())); Root->SetStringField(TEXT("map_hash"),UTF8_TO_TCHAR(H.Map.c_str()));
    Root->SetNumberField(TEXT("red_team_blue"),M.StartingRed.Blue); Root->SetNumberField(TEXT("red_team_red"),M.StartingRed.Red);
    Root->SetNumberField(TEXT("blue_team_blue"),M.StartingBlue.Blue); Root->SetNumberField(TEXT("blue_team_red"),M.StartingBlue.Red);
    TArray<TSharedPtr<FJsonValue>> Objects, Rules;
    for (const auto& [Key,O] : M.Objects)
    {
        auto J=MakeShared<FJsonObject>();
        J->SetStringField(TEXT("id"),UTF8_TO_TCHAR(Key.StableId.ToString().c_str())); J->SetNumberField(TEXT("kind"),static_cast<uint8>(Key.Kind));
        J->SetNumberField(TEXT("generation"),Key.Generation); J->SetNumberField(TEXT("team"),static_cast<uint8>(O.Grant.Faction));
        J->SetStringField(TEXT("definition"),UTF8_TO_TCHAR(O.Definition.c_str())); PreciseNumber(*J,TEXT("cost"),O.Grant.EstimatedCost);
        J->SetNumberField(TEXT("ordinal"),O.Ordinal); PreciseNumber(*J,TEXT("facing_yaw"),O.FacingYaw);
        PreciseNumber(*J,TEXT("x"),O.X); PreciseNumber(*J,TEXT("y"),O.Y); PreciseNumber(*J,TEXT("z"),O.Z);
        PreciseNumber(*J,TEXT("health"),O.Vitals.Value); PreciseNumber(*J,TEXT("shield"),O.Vitals.Shield);
        PreciseNumber(*J,TEXT("shield_radius"),O.ShieldRadius); J->SetNumberField(TEXT("supplied"),O.Supplied?1:0);
        J->SetNumberField(TEXT("completed"),O.Completed?1:0);
        Objects.Add(MakeShared<FJsonValueObject>(J));
    }
    for (const auto& [Key,R] : M.Rules)
    {
        auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"),UTF8_TO_TCHAR(Key.c_str()));
        J->SetNumberField(TEXT("kind"),static_cast<uint8>(R.Kind)); J->SetStringField(TEXT("source_definition"),UTF8_TO_TCHAR(R.SourceDefinition.c_str()));
        PreciseNumber(*J,TEXT("damage"),R.Damage); J->SetNumberField(TEXT("blue"),R.Amount.Blue); J->SetNumberField(TEXT("red"),R.Amount.Red);
        J->SetNumberField(TEXT("cooldown_ticks"),static_cast<double>(R.Cooldown)); J->SetNumberField(TEXT("effect_ticks"),static_cast<double>(R.MaxEffectLifetime));
        J->SetNumberField(TEXT("max_targets"),R.MaxTargets);
        J->SetStringField(TEXT("target_definition"),UTF8_TO_TCHAR(R.TargetDefinition.c_str())); J->SetNumberField(TEXT("spawn_kind"),static_cast<uint8>(R.SpawnKind));
        PreciseNumber(*J,TEXT("spawn_health"),R.SpawnHealth); PreciseNumber(*J,TEXT("spawn_shield"),R.SpawnShield);
        PreciseNumber(*J,TEXT("spawn_cost"),R.SpawnCost); PreciseNumber(*J,TEXT("spawn_shield_radius"),R.SpawnShieldRadius);
        J->SetNumberField(TEXT("minimum_age_ticks"),static_cast<double>(R.MinimumAge)); J->SetNumberField(TEXT("max_live_children"),R.MaximumLiveChildren);
        J->SetNumberField(TEXT("max_faction_objects"),R.MaximumFactionObjects); J->SetNumberField(TEXT("spawn_controller_seat"),static_cast<uint8>(R.SpawnControllerSeat));
        J->SetNumberField(TEXT("spawn_completed"),R.SpawnCompleted?1:0); J->SetNumberField(TEXT("relation"),static_cast<uint8>(R.Relation));
        J->SetNumberField(TEXT("relation_value"),R.RelationValue?1:0);
        J->SetStringField(TEXT("capability"),UTF8_TO_TCHAR(R.Capability.c_str()));J->SetNumberField(TEXT("capability_version"),R.CapabilityVersion);
        J->SetStringField(TEXT("capability_operation"),UTF8_TO_TCHAR(R.CapabilityOperation.c_str()));
        J->SetStringField(TEXT("upgrade_catalog"),UTF8_TO_TCHAR(R.UpgradeCatalog.c_str()));
        J->SetStringField(TEXT("required_capability_catalog"),UTF8_TO_TCHAR(R.RequiredCapabilityCatalog.c_str()));J->SetStringField(TEXT("required_capability_group"),UTF8_TO_TCHAR(R.RequiredCapabilityGroup.c_str()));J->SetStringField(TEXT("required_capability"),UTF8_TO_TCHAR(R.RequiredCapability.c_str()));Rules.Add(MakeShared<FJsonValueObject>(J));
    }
    auto Numbers=MakeShared<FJsonObject>(); for (const auto& [Key,N] : M.Numbers) PreciseNumber(*Numbers,UTF8_TO_TCHAR(Key.c_str()),N);
    auto Strings=MakeShared<FJsonObject>(); for (const auto& [Key,S] : M.Strings) Strings->SetStringField(UTF8_TO_TCHAR(Key.c_str()),UTF8_TO_TCHAR(S.c_str()));
    Root->SetObjectField(TEXT("strings"),Strings);
    Root->SetArrayField(TEXT("objects"),Objects); Root->SetArrayField(TEXT("rules"),Rules); Root->SetObjectField(TEXT("numbers"),Numbers);
    return Root;
}
bool LoadVerified(const FString& File,RuntimeManifest& M,ManifestHashes& H,FString& Error)
{
    const auto Size=IFileManager::Get().FileSize(*File);
    FString Json; TSharedPtr<FJsonObject> Root;
    if (Size<=0 || Size>16*1024*1024 || !FFileHelper::LoadFileToString(Json,*File)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root) || !Root || !Parse(*Root,M))
    { Error=TEXT("Missing, oversized or malformed Hybrid runtime manifest"); return false; }
    std::string Why;
    if (!InitializeManifestCapabilities(M,Why) || !HashManifest(M,H,Why)) { Error=UTF8_TO_TCHAR(Why.c_str()); return false; }
    if (Text(*Root,TEXT("config_hash"))!=H.Configuration || Text(*Root,TEXT("map_hash"))!=H.Map)
    { Error=TEXT("Manifest content does not match its hashes"); return false; }
    Error.Reset(); return true;
}
}
bool GuLiNetBridge::ReadPortableManifest(const FString& File,GuLiNet::RuntimeManifest& Out,FString& Error)
{GuLiNet::ManifestHashes Hashes;return LoadVerified(File,Out,Hashes,Error);}
bool FGuLiHybridManifest::Write(const FGuLiHybridManifestData& Data,const FString& File,FString& Error)
{
    auto Contracts=Data;
    if (!FGuLiHybridAdapterRegistry::BindDefinition(Contracts,UTF8_TO_TCHAR(TransactionWorkDefinition),TEXT("TransactionWork"),Error)
        || !FGuLiHybridAdapterRegistry::Validate(Contracts,Error)) return false;
    RuntimeManifest M; M.MapPackage=TCHAR_TO_UTF8(*Data.MapPackage);
    M.Numbers.emplace("simulation/logic_hz",TicksPerSecond);
    M.Numbers.emplace("simulation/mass_hz",MassSimulationHz);
    M.Numbers.emplace("simulation/wingman_hz",WingmanSimulationHz);
    M.Numbers.emplace("network/wingman/motion_hz",WingmanMotionHz);
    M.Numbers.emplace("network/state_hz",StatePublicationHz);
    const auto* Settings=GetDefault<UGuLiNetworkSettings>();
    M.Numbers.emplace("ownership/idle_seconds",Settings->CommanderIdleSeconds);
    M.Numbers.emplace("ownership/evaluation_seconds",Settings->OwnershipEvaluationSeconds);
    M.Numbers.emplace("ownership/handoff_timeout_seconds",Settings->HandoffTimeoutSeconds);
    M.Numbers.emplace("ownership/minimum_relative_gap",Settings->MinimumRelativeLoadGap);
    M.Numbers.emplace("ownership/minimum_gap_ms",Settings->MinimumLoadGapMilliseconds);
    M.Numbers.emplace("ownership/maximum_background_batch",Settings->MaximumBackgroundHandoffBatch);
    M.StartingRed.Blue=Data.RedTeamBlue; M.StartingRed.Red=Data.RedTeamRed;
    M.StartingBlue.Blue=Data.BlueTeamBlue; M.StartingBlue.Red=Data.BlueTeamRed;
    for (const auto& V : Data.Objects)
    {
        ObjectRecord O; O.Grant.Object=GuLiNetBridge::ToCore(V.Key); O.Grant.Faction=GuLiNetBridge::ToCore(V.Team);
        O.Grant.EstimatedCost=V.EstimatedCost; O.Definition=TCHAR_TO_UTF8(*V.Definition);
        O.Ordinal=V.Ordinal; O.FacingYaw=V.FacingYawDegrees;
        O.X=V.Location.X; O.Y=V.Location.Y; O.Z=V.Location.Z;
        O.Vitals.Value=O.Vitals.Maximum=V.Health; O.Vitals.Shield=O.Vitals.MaximumShield=V.Shield; O.Vitals.Version=V.Health>0?1:0;
        O.ShieldRadius=V.ShieldRadius; O.Supplied=V.bSupplied; O.Completed=V.bCompleted;
        if (!M.Objects.emplace(O.Grant.Object,std::move(O)).second) { Error=TEXT("Duplicate startup identity"); return false; }
    }
    for (const auto& V : Data.Rules)
    {
        Rule R; R.Kind=static_cast<TransactionKind>(V.Kind); R.SourceDefinition=TCHAR_TO_UTF8(*V.SourceDefinition);
        R.Damage=V.Damage; R.Amount.Blue=V.Blue; R.Amount.Red=V.Red; R.Cooldown=V.CooldownTicks; R.MaxEffectLifetime=V.EffectTicks; R.MaxTargets=V.MaximumTargets;
        R.TargetDefinition=TCHAR_TO_UTF8(*V.TargetDefinition); R.SpawnKind=static_cast<ObjectKind>(V.SpawnKind);
        R.SpawnHealth=V.SpawnHealth; R.SpawnShield=V.SpawnShield; R.SpawnCost=V.SpawnCost; R.SpawnShieldRadius=V.SpawnShieldRadius;
        R.MinimumAge=V.MinimumAgeTicks; R.MaximumLiveChildren=V.MaximumLiveChildren; R.SpawnCompleted=V.bSpawnCompleted;
        R.MaximumFactionObjects=V.MaximumFactionObjects; R.SpawnControllerSeat=static_cast<Role>(V.SpawnControllerSeat);
        R.Relation=static_cast<RelationKind>(V.Relation); R.RelationValue=V.bRelationValue;
        R.Capability=TCHAR_TO_UTF8(*V.Capability);R.CapabilityOperation=TCHAR_TO_UTF8(*V.CapabilityOperation);R.CapabilityVersion=V.CapabilityVersion;
        R.UpgradeCatalog=TCHAR_TO_UTF8(*V.UpgradeCatalog);
        R.RequiredCapabilityCatalog=TCHAR_TO_UTF8(*V.RequiredCapabilityCatalog);R.RequiredCapabilityGroup=TCHAR_TO_UTF8(*V.RequiredCapabilityGroup);R.RequiredCapability=TCHAR_TO_UTF8(*V.RequiredCapability);
        if (!M.Rules.emplace(TCHAR_TO_UTF8(*V.Id),R).second) { Error=TEXT("Duplicate rule identity"); return false; }
    }
    for (const auto& [Key,N] : Contracts.Numbers) M.Numbers.emplace(TCHAR_TO_UTF8(*Key),N);
    for (const auto& [Key,S] : Contracts.Strings) M.Strings.emplace(TCHAR_TO_UTF8(*Key),TCHAR_TO_UTF8(*S));
    ManifestHashes H; std::string Why; if (!InitializeManifestCapabilities(M,Why) || !HashManifest(M,H,Why)) { Error=UTF8_TO_TCHAR(Why.c_str()); return false; }
    FString Json; if (!FJsonSerializer::Serialize(ToJson(M,H),TJsonWriterFactory<>::Create(&Json))) { Error=TEXT("Manifest serialization failed"); return false; }
    const FString Absolute=FPaths::ConvertRelativePathToFull(File), Pending=Absolute+TEXT(".pending");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Absolute),true);
    if (!FFileHelper::SaveStringToFile(Json,*Pending,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { Error=TEXT("Manifest write failed"); return false; }
    FGuLiHybridManifestData ReadBack;
    if (!Read(Pending,ReadBack,Error)) return false;
    if (!IFileManager::Get().Move(*Absolute,*Pending,true,true)) { Error=TEXT("Manifest atomic replacement failed"); return false; }
    Error.Reset(); return true;
}
bool FGuLiHybridManifest::ValidateFile(const FString& File,FString& ConfigHash,FString& MapHash,FString& MapPackage,FString& Error)
{
    RuntimeManifest M; ManifestHashes H;
    if (!LoadVerified(File,M,H,Error)) return false;
    ConfigHash=UTF8_TO_TCHAR(H.Configuration.c_str()); MapHash=UTF8_TO_TCHAR(H.Map.c_str()); MapPackage=UTF8_TO_TCHAR(M.MapPackage.c_str()); Error.Reset(); return true;
}
bool FGuLiHybridManifest::Read(const FString& File,FGuLiHybridManifestData& Data,FString& Error)
{
    RuntimeManifest M; ManifestHashes H;
    if (!LoadVerified(File,M,H,Error)) return false;
    FGuLiHybridManifestData Out; Out.MapPackage=UTF8_TO_TCHAR(M.MapPackage.c_str());
    Out.RedTeamBlue=M.StartingRed.Blue; Out.RedTeamRed=M.StartingRed.Red;
    Out.BlueTeamBlue=M.StartingBlue.Blue; Out.BlueTeamRed=M.StartingBlue.Red;
    for (const auto& [K,O] : M.Objects)
    {
        auto& V=Out.Objects.AddDefaulted_GetRef(); V.Key=GuLiNetBridge::ToUE(K);
        V.Team=GuLiNetBridge::ToUE(O.Grant.Faction); V.Definition=UTF8_TO_TCHAR(O.Definition.c_str());
        V.Ordinal=O.Ordinal; V.FacingYawDegrees=O.FacingYaw; V.Location=FVector(O.X,O.Y,O.Z);
        V.Health=O.Vitals.Value; V.Shield=O.Vitals.Shield; V.ShieldRadius=O.ShieldRadius;
        V.bSupplied=O.Supplied; V.EstimatedCost=O.Grant.EstimatedCost; V.bCompleted=O.Completed;
    }
    for (const auto& [K,R] : M.Rules)
    {
        auto& V=Out.Rules.AddDefaulted_GetRef(); V.Id=UTF8_TO_TCHAR(K.c_str()); V.Kind=static_cast<EGuLiHybridRuleKind>(R.Kind);
        V.SourceDefinition=UTF8_TO_TCHAR(R.SourceDefinition.c_str()); V.Damage=R.Damage;
        V.Blue=R.Amount.Blue; V.Red=R.Amount.Red; V.CooldownTicks=R.Cooldown;
        V.EffectTicks=R.MaxEffectLifetime; V.MaximumTargets=R.MaxTargets;
        V.TargetDefinition=UTF8_TO_TCHAR(R.TargetDefinition.c_str()); V.SpawnKind=static_cast<EGuLiHybridObjectKind>(R.SpawnKind);
        V.SpawnHealth=R.SpawnHealth; V.SpawnShield=R.SpawnShield; V.SpawnCost=R.SpawnCost; V.SpawnShieldRadius=R.SpawnShieldRadius;
        V.MinimumAgeTicks=R.MinimumAge; V.MaximumLiveChildren=R.MaximumLiveChildren; V.bSpawnCompleted=R.SpawnCompleted;
        V.MaximumFactionObjects=R.MaximumFactionObjects; V.SpawnControllerSeat=static_cast<uint8>(R.SpawnControllerSeat);
        V.Relation=static_cast<EGuLiHybridRelationKind>(R.Relation); V.bRelationValue=R.RelationValue;
        V.Capability=UTF8_TO_TCHAR(R.Capability.c_str());V.CapabilityOperation=UTF8_TO_TCHAR(R.CapabilityOperation.c_str());V.CapabilityVersion=R.CapabilityVersion;
        V.UpgradeCatalog=UTF8_TO_TCHAR(R.UpgradeCatalog.c_str());
        V.RequiredCapabilityCatalog=UTF8_TO_TCHAR(R.RequiredCapabilityCatalog.c_str());V.RequiredCapabilityGroup=UTF8_TO_TCHAR(R.RequiredCapabilityGroup.c_str());V.RequiredCapability=UTF8_TO_TCHAR(R.RequiredCapability.c_str());
    }
    for (const auto& [K,N] : M.Numbers) Out.Numbers.Add(UTF8_TO_TCHAR(K.c_str()),N);
    for (const auto& [K,S] : M.Strings) Out.Strings.Add(UTF8_TO_TCHAR(K.c_str()),UTF8_TO_TCHAR(S.c_str()));
    if (!FGuLiHybridAdapterRegistry::Validate(Out,Error)
        || !FGuLiHybridCommandRegistration::Validate(Out,Error)
        || !FGuLiHybridLedgerBindingRegistration::Validate(Out,Error)) return false;
    Data=MoveTemp(Out); return true;
}
FString FGuLiHybridManifest::MakeDamageRuleId(const FString& Definition,const FName Skill,const FName Slot)
{ return UTF8_TO_TCHAR(DamageRuleId(TCHAR_TO_UTF8(*Definition),TCHAR_TO_UTF8(*Skill.ToString()),TCHAR_TO_UTF8(*Slot.ToString())).c_str()); }
FGuLiHybridObjectKey FGuLiHybridManifest::MakeStableKey(const FString& MapPackage,EGuLiHybridObjectKind Kind,const FString& AuthoredId)
{
    if (!MapPackage.StartsWith(TEXT("/Game/")) || AuthoredId.IsEmpty() || Kind==EGuLiHybridObjectKind::None) return {};
    Writer W(8192); W.Text("GuLiStrike/StartupObject/v1"); W.Text(TCHAR_TO_UTF8(*MapPackage));
    W.U8(static_cast<uint8>(Kind)); W.Text(TCHAR_TO_UTF8(*AuthoredId));
    const auto B=W.Take(); Id Stable;
    if (B.empty() || !Id::Parse(Sha256(B).substr(0,32),Stable) || !Stable.Valid()) return {};
    FGuLiHybridObjectKey Key; Key.Kind=Kind; Key.StableId=GuLiNetBridge::ToUE(Stable); return Key;
}
