#include "Network/GuLiNetworkSettings.h"
#include "GuLiNetTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#if !UE_BUILD_SHIPPING
#include "Development/GuLiHybridReviewSubsystem.h"
#endif

FGuLiNetworkLaunchConfig UGuLiNetworkSettings::Resolve()
{
    const auto* S = GetDefault<UGuLiNetworkSettings>(); FGuLiNetworkLaunchConfig C;
    C.Mode = EGuLiNetworkMode::Hybrid; C.Transport = S->Transport; C.CoordinatorUrl = S->CoordinatorUrl;
    C.ClientRunMode = S->ClientRunMode;
    C.ManifestFile = S->ManifestFile.FilePath.IsEmpty() ? FPaths::ProjectConfigDir() / TEXT("Hybrid/RuntimeManifest-600.json") : S->ManifestFile.FilePath;
    C.RoomId = S->RoomId; C.DevelopmentPlayerId = S->DevelopmentPlayerId; C.Team = S->Team; C.Role = S->Role;
    C.LocalUdpPort = S->LocalUdpPort; C.ProductId = S->ProductId; C.SandboxId = S->SandboxId;
    C.DeploymentId = S->DeploymentId; C.ClientId = S->ClientId;
    C.ClientSecret = FPlatformMisc::GetEnvironmentVariable(TEXT("GULI_EOS_GAME_CLIENT_SECRET"));
    C.ConnectionTimeout = S->ConnectionTimeoutSeconds; C.ReadyTimeout = S->ReadyTimeoutSeconds;
    C.MembershipTimeout = S->MembershipTimeoutSeconds; C.EmptyTeamTimeout = S->EmptyTeamTimeoutSeconds;
    const TCHAR* Command = FCommandLine::Get(); FString Value;
    if (FParse::Value(Command, TEXT("GuLiClientRunMode="), Value))
    {
        if (Value.Equals(TEXT("Rendered"), ESearchCase::IgnoreCase)) C.ClientRunMode=EGuLiClientRunMode::Rendered;
        else if (Value.Equals(TEXT("LogicOnly"), ESearchCase::IgnoreCase)) C.ClientRunMode=EGuLiClientRunMode::LogicOnly;
        else C.ValidationError=TEXT("GuLiClientRunMode must be Rendered or LogicOnly");
    }
    if (C.ClientRunMode==EGuLiClientRunMode::LogicOnly && !FParse::Param(Command,TEXT("NullRHI")))
        C.ValidationError=TEXT("LogicOnly requires -NullRHI");
    if (FParse::Value(Command, TEXT("GuLiNetMode="), Value))
    {
        if (!Value.Equals(TEXT("Hybrid"),ESearchCase::IgnoreCase))
            C.ValidationError=TEXT("Only Hybrid is supported; the CS runtime has been retired");
    }
    if (FParse::Value(Command, TEXT("GuLiTransport="), Value))
    {
        C.Transport = Value.Equals(TEXT("EOS"), ESearchCase::IgnoreCase) ? EGuLiNetworkTransport::EOS : EGuLiNetworkTransport::LocalUdp;
        if (!Value.Equals(TEXT("EOS"),ESearchCase::IgnoreCase) && !Value.Equals(TEXT("LocalUdp"),ESearchCase::IgnoreCase))
            C.ValidationError=TEXT("GuLiTransport must be LocalUdp or EOS");
    }
    FParse::Value(Command, TEXT("GuLiCoordinatorUrl="), C.CoordinatorUrl);
    FParse::Value(Command, TEXT("GuLiManifest="), C.ManifestFile);
    if (FParse::Value(Command, TEXT("GuLiRoom="), Value) && (!FGuid::Parse(Value, C.RoomId) || !C.RoomId.IsValid()))
        C.ValidationError=TEXT("GuLiRoom must be a valid nonzero GUID");
    if (FParse::Value(Command, TEXT("GuLiDevPlayerId="), Value)
        && (!FGuid::Parse(Value, C.DevelopmentPlayerId) || !C.DevelopmentPlayerId.IsValid()))
        C.ValidationError=TEXT("GuLiDevPlayerId must be a valid nonzero GUID");
    FParse::Value(Command, TEXT("GuLiUdpPort="), C.LocalUdpPort);
    if (FParse::Value(Command, TEXT("GuLiTeam="), Value))
    {
        if (Value.Equals(TEXT("Blue"), ESearchCase::IgnoreCase)) C.Team=EGuLiTeam::Blue;
        else if (Value.Equals(TEXT("Red"), ESearchCase::IgnoreCase)) C.Team=EGuLiTeam::Red;
        else C.ValidationError=TEXT("GuLiTeam must be Red or Blue");
    }
    if (FParse::Value(Command, TEXT("GuLiRole="), Value))
    {
        if (Value.Equals(TEXT("Ground"), ESearchCase::IgnoreCase)) C.Role=EGuLiCommanderRole::Ground;
        else if (Value.Equals(TEXT("Air"), ESearchCase::IgnoreCase)) C.Role=EGuLiCommanderRole::Air;
        else if (Value.Equals(TEXT("Commander"), ESearchCase::IgnoreCase)) C.Role=EGuLiCommanderRole::Commander;
        else if (Value.Equals(TEXT("Observer"), ESearchCase::IgnoreCase)) C.Role=EGuLiCommanderRole::Observer;
        else C.ValidationError=TEXT("GuLiRole must be Commander, Ground, Air or Observer");
    }
    if (C.LocalUdpPort<0 || C.LocalUdpPort>65535) C.ValidationError=TEXT("GuLiUdpPort is outside 0..65535");
    if (S->WingmanMotionRateHz!=GuLiNet::WingmanMotionHz)
        C.ValidationError=TEXT("The current simulation contract requires WingmanMotionRateHz=10");
    if (!FMath::IsFinite(C.ConnectionTimeout) || C.ConnectionTimeout<=0 || !FMath::IsFinite(C.ReadyTimeout) || C.ReadyTimeout<=0
        || !FMath::IsFinite(C.MembershipTimeout) || C.MembershipTimeout<=0 || !FMath::IsFinite(C.EmptyTeamTimeout) || C.EmptyTeamTimeout<=0)
        C.ValidationError=TEXT("Hybrid connection, ready, membership and empty-team timeouts must be finite and positive");
    return C;
}
uint32 UGuLiNetworkSettings::WingmanMotionIntervalSimulationTicks()
{ return GuLiNet::WingmanSimulationHz/GuLiNet::WingmanMotionHz; }
float UGuLiNetworkSettings::WingmanMotionIntervalSeconds()
{ return 1.f/GuLiNet::WingmanMotionHz; }
bool UGuLiNetworkSettings::IsHybrid(const UWorld* World)
{
    if (!World || !World->IsGameWorld()) return false;
    return true;
}

bool UGuLiNetworkSettings::IsLogicOnly()
{
    static const TOptional<EGuLiClientRunMode> Override=[]()
    {
        FString Value;
        if (!FParse::Value(FCommandLine::Get(),TEXT("GuLiClientRunMode="),Value)) return TOptional<EGuLiClientRunMode>{};
        return TOptional<EGuLiClientRunMode>{Value.Equals(TEXT("LogicOnly"),ESearchCase::IgnoreCase)
            ? EGuLiClientRunMode::LogicOnly : EGuLiClientRunMode::Rendered};
    }();
    return Override.Get(GetDefault<UGuLiNetworkSettings>()->ClientRunMode)==EGuLiClientRunMode::LogicOnly;
}

bool UGuLiNetworkSettings::CanPresent(const UWorld* World)
{
    return World && World->GetNetMode()!=NM_DedicatedServer && FApp::CanEverRender() && !IsLogicOnly();
}

bool UGuLiNetworkSettings::IsPresentationDisabled(const UObject* WorldContextObject)
{
    const auto* World=GEngine?GEngine->GetWorldFromContextObject(WorldContextObject,EGetWorldErrorMode::ReturnNull):nullptr;
    const bool Disabled=!CanPresent(World);
#if !UE_BUILD_SHIPPING
    if (World && WorldContextObject) if (auto* Review=World->GetSubsystem<UGuLiHybridReviewSubsystem>())
        Review->ObserveBlueprintExecution(*WorldContextObject,TEXT("IsPresentationDisabled"),Disabled);
#endif
    return Disabled;
}

bool UGuLiNetworkSettings::CanCapturePerformance(const UWorld* World)
{
    if (!World || !World->IsGameWorld() || IsLogicOnly()) return false;
    FString Role;
    if (!FParse::Value(FCommandLine::Get(),TEXT("GuLiPerfCapture="),Role))
        return World->GetNetMode()==NM_DedicatedServer || CanPresent(World);
    if (Role.Equals(TEXT("Server"),ESearchCase::IgnoreCase)) return World->GetNetMode()==NM_DedicatedServer;
    if (Role.Equals(TEXT("RenderedClient"),ESearchCase::IgnoreCase)) return CanPresent(World);
    return false;
}
