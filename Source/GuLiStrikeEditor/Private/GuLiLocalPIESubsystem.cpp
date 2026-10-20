#include "GuLiLocalPIESubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

FString UGuLiLocalPIESubsystem::PreparePerformanceProduction(const TArray<FString>& AlreadyPrepared)
{
    TArray<TSharedPtr<FJsonValue>> Prepared;
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
    if (World && World->WorldType==EWorldType::PIE &&
        UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==TEXT("/Game/Maps/LVL_CommanderMassPrototype"))
        for (TObjectIterator<UGuLiBuildingProductionComponent> It;It;++It)
            if (It->GetWorld()==World && !AlreadyPrepared.Contains(It->GetPathName()) && It->PrepareDevelopmentProductionCycle())
                Prepared.Add(MakeShared<FJsonValueString>(It->GetPathName()));
    auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("primed"),Prepared);
    FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Result,Writer);return Text;
}

namespace
{
const TCHAR* LocalMap=TEXT("/Game/Maps/LVL_CommanderMassPrototype");
UWorld* EditorWorld()
{
    if (GEditor) for (const auto& Context:GEditor->GetWorldContexts())
        if (Context.WorldType==EWorldType::Editor) return Context.World();
    return nullptr;
}
bool IsLocalMap(const UWorld* World)
{ return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LocalMap; }
void SingleWorld(ULevelEditorPlaySettings* Settings)
{
    if (!Settings) return;
    Settings->SetPlayNetMode(PIE_Standalone);
    Settings->SetPlayNumberOfClients(1);
    Settings->SetRunUnderOneProcess(true);
    Settings->bLaunchSeparateServer=false;
}
bool LoopbackSocket(int32 Port,bool Bind)
{
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!Sockets) return false;
    auto Address=Sockets->CreateInternetAddr();bool Valid=false;
    Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(Port);
    FSocket* Socket=Sockets->CreateSocket(NAME_Stream,TEXT("GuLiLocalPIE"),false);
    if (!Valid || !Socket) return false;
    const bool Result=Bind?Socket->Bind(*Address):Socket->Connect(*Address);
    Socket->Close();Sockets->DestroySocket(Socket);return Result;
}
}

struct UGuLiLocalPIESubsystem::FState
{
    FGuLiNetworkLaunchConfig Config;
    FGuid Peer;
    FProcHandle Coordinator,Logic;
    uint32 CoordinatorPid=0,LogicPid=0;
    FString Directory,Error;
    TArray<TSharedPtr<FJsonValue>> Shutdown;
    bool CleanupComplete=false;
    double Started=0;
    bool Active=false,Began=false;
#if PLATFORM_WINDOWS
    HANDLE Job=nullptr,StopEvent=nullptr;
#endif
};
void UGuLiLocalPIESubsystem::FStateDeleter::operator()(FState* Value) const { delete Value; }
UGuLiLocalPIESubsystem::UGuLiLocalPIESubsystem() : State(new FState) {}
UGuLiLocalPIESubsystem::~UGuLiLocalPIESubsystem()=default;

void UGuLiLocalPIESubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UGuLiNetworkSettings::WorldLaunchOverride().BindUObject(this,&ThisClass::ResolveWorld);
    PreHandle=FEditorDelegates::PreBeginPIE.AddUObject(this,&ThisClass::BeforePIE);
    BeginHandle=FEditorDelegates::BeginPIE.AddUObject(this,&ThisClass::BeganPIE);
    EndHandle=FEditorDelegates::EndPIE.AddUObject(this,&ThisClass::EndedPIE);
    MapHandle=FEditorDelegates::OnMapOpened.AddUObject(this,&ThisClass::MapOpened);
    ExitHandle=FCoreDelegates::OnPreExit.AddUObject(this,&ThisClass::StopChildren);
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Tick),0.25f);
    ConfigurePlaySettings();
}
void UGuLiLocalPIESubsystem::Deinitialize()
{
    StopChildren();
    FEditorDelegates::PreBeginPIE.Remove(PreHandle);FEditorDelegates::BeginPIE.Remove(BeginHandle);
    FEditorDelegates::EndPIE.Remove(EndHandle);FEditorDelegates::OnMapOpened.Remove(MapHandle);
    FCoreDelegates::OnPreExit.Remove(ExitHandle);FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    if (UGuLiNetworkSettings::WorldLaunchOverride().IsBoundToObject(this))
        UGuLiNetworkSettings::WorldLaunchOverride().Unbind();
    Super::Deinitialize();
}
void UGuLiLocalPIESubsystem::MapOpened(const FString&,bool) { ConfigurePlaySettings(); }
void UGuLiLocalPIESubsystem::ConfigurePlaySettings()
{
    if (IsLocalMap(EditorWorld()) && GetDefault<UGuLiLocalPIESettings>()->bEnabled)
        SingleWorld(GetMutableDefault<ULevelEditorPlaySettings>());
}
void UGuLiLocalPIESubsystem::BeforePIE(bool Simulate)
{
    if (Simulate || !GEditor || !IsLocalMap(EditorWorld())
        || !GetDefault<UGuLiLocalPIESettings>()->bEnabled) return;
    ConfigurePlaySettings();
    // UE duplicates the play settings when the request is queued. Correct the
    // actual request object as well as the defaults used by the next click.
    const auto Request=GEditor->GetPlaySessionRequest();
    if (Request.IsSet()) SingleWorld(Request->EditorPlaySettings);
}
bool UGuLiLocalPIESubsystem::PrepareForPIE(FString& Error)
{
    const auto* Settings=GetDefault<UGuLiLocalPIESettings>();
    if (!GEditor || !IsLocalMap(EditorWorld()) || !Settings->bEnabled) return true;
    if (State->Active) return true;
    State->Error.Reset();State->Began=false;State->Shutdown.Reset();State->CleanupComplete=false;
    auto Fail=[&](const FString& Why)
    { State->Error=Why;Error=Why;StopChildren();UE_LOG(LogTemp,Error,TEXT("[GuLiLocalPIE] %s"),*Why);return false; };
    if (Settings->MainRole!=EGuLiCommanderRole::Commander && Settings->MainRole!=EGuLiCommanderRole::Ground
        && Settings->MainRole!=EGuLiCommanderRole::Air) return Fail(TEXT("Choose Commander, Ground or Air before PIE."));
    if (Settings->CoordinatorPort<1024 || Settings->CoordinatorPort>65535)
        return Fail(TEXT("The local Coordinator port must be in 1024..65535."));
    if (!LoopbackSocket(Settings->CoordinatorPort,true))
        return Fail(FString::Printf(TEXT("Local Coordinator port %d is occupied; choose another port in GuLi Local PIE settings."),Settings->CoordinatorPort));
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString Server=Root/TEXT("Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe");
    const FString Client=FPaths::ConvertRelativePathToFull(FPaths::EngineDir()/TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
    if (!FPaths::FileExists(Server) || !FPaths::FileExists(Client)) return Fail(TEXT("The compiled local Coordinator or source-engine editor is missing."));
    State->Config=UGuLiNetworkSettings::Resolve();
    auto& Config=State->Config;
    if (!Config.ValidationError.IsEmpty()) return Fail(Config.ValidationError);
    Config.Mode=EGuLiNetworkMode::Hybrid;Config.Transport=EGuLiNetworkTransport::LocalUdp;
    Config.ClientRunMode=EGuLiClientRunMode::Rendered;Config.Team=EGuLiTeam::Red;Config.Role=Settings->MainRole;
    Config.ManifestFile=Root/TEXT("Config/Hybrid/RuntimeManifest-600.json");
    Config.RoomId=FGuid::NewGuid();Config.DevelopmentPlayerId=FGuid::NewGuid();Config.LocalUdpPort=0;
    Config.CoordinatorUrl=FString::Printf(TEXT("ws://127.0.0.1:%d/v1/match"),Settings->CoordinatorPort);
    FString ConfigHash,MapHash,Map,Why;
    if (!FGuLiHybridManifest::ValidateFile(Config.ManifestFile,ConfigHash,MapHash,Map,Why) || Map!=LocalMap)
        return Fail(Why.IsEmpty()?TEXT("The normal runtime manifest belongs to another map."):Why);
    FGuLiHybridManifestData Manifest;
    if (!FGuLiHybridManifest::Read(Config.ManifestFile,Manifest,Why)) return Fail(Why);
    int32 Units=0;for (const auto& Object:Manifest.Objects) if (Object.Key.Kind==EGuLiHybridObjectKind::Soldier) ++Units;
    if (Units!=600 || Manifest.Numbers.FindRef(TEXT("acceptance/resource_supply"))!=0)
        return Fail(TEXT("Normal local PIE requires the production 600-unit manifest without acceptance supply."));
    State->Peer=FGuid::NewGuid();
    State->Directory=FPaths::Combine(FPlatformProcess::UserTempDir(),TEXT("GuLiStrikePIE"),Config.RoomId.ToString(EGuidFormats::Digits));
    if (!IFileManager::Get().MakeDirectory(*State->Directory,true)) return Fail(TEXT("Could not create the local session directory."));
    State->Active=true;State->Started=FPlatformTime::Seconds();
    FString StopArguments,ClientStopArguments;
#if PLATFORM_WINDOWS
    const FString StopName=TEXT("Local\\GuLiStrikePIEStop_")+Config.RoomId.ToString(EGuidFormats::Digits);
    State->StopEvent=CreateEventW(nullptr,1,0,*StopName);
    if (!State->StopEvent) return Fail(TEXT("Could not create the owned local PIE stop event."));
    StopArguments=FString::Printf(TEXT(" --local-stop-event \"%s\""),*StopName);
    ClientStopArguments=FString::Printf(TEXT(" -GuLiLocalStopEvent=\"%s\""),*StopName);
    State->Job=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};Limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!State->Job || !SetInformationJobObject(State->Job,JobObjectExtendedLimitInformation,&Limits,sizeof(Limits)))
        return Fail(TEXT("Could not create the owned local PIE process job."));
#endif
    auto Launch=[&](const FString& Exe,const FString& Args,FProcHandle& Handle,uint32& Pid)
    {
        Handle=FPlatformProcess::CreateProc(*Exe,*Args,false,true,true,&Pid,0,*Root,nullptr);
        if (!Handle.IsValid()) return false;
#if PLATFORM_WINDOWS
        if (!AssignProcessToJobObject(State->Job,Handle.Get())) return false;
#endif
        return true;
    };
    const FString ServerArgs=FString::Printf(TEXT("--dev --bind 127.0.0.1 --port %d --expected-red 1 --expected-blue 1 --manifest \"%s\" --database \"%s\""),
        Settings->CoordinatorPort,*Config.ManifestFile,*(State->Directory/TEXT("room.sqlite")))+StopArguments;
    if (!Launch(Server,ServerArgs,State->Coordinator,State->CoordinatorPid)) return Fail(TEXT("Could not start the owned Coordinator."));
    bool Ready=false;
    const double Deadline=FPlatformTime::Seconds()+5;
    while (FPlatformTime::Seconds()<Deadline && FPlatformProcess::IsProcRunning(State->Coordinator))
    {
        if (LoopbackSocket(Settings->CoordinatorPort,false)) { Ready=true;break; }
        FPlatformProcess::Sleep(0.02f);
    }
    if (!Ready) return Fail(TEXT("The owned Coordinator did not become ready."));
    const FString Project=FPaths::GetProjectFilePath();
    const FString Args=FString::Printf(TEXT("\"%s\" %s -game -NullRHI -nosound -unattended -NoSplash -NoLiveCoding -GuLiNetMode=Hybrid -GuLiTransport=LocalUdp -GuLiRoom=%s -GuLiDevPlayerId=%s -GuLiTeam=Blue -GuLiRole=Commander -GuLiClientRunMode=LogicOnly -GuLiHotspotMask=%u -GuLiGameplayWorkers=%u -GuLiUdpPort=0 -GuLiCoordinatorUrl=\"%s\" -GuLiManifest=\"%s\" -abslog=\"%s\""),
        *Project,LocalMap,*Config.RoomId.ToString(EGuidFormats::Digits),*State->Peer.ToString(EGuidFormats::Digits),
        Config.HotspotMask,Config.GameplayWorkers,*Config.CoordinatorUrl,*Config.ManifestFile,*(State->Directory/TEXT("logic.log")))+ClientStopArguments;
    if (!Launch(Client,Args,State->Logic,State->LogicPid)) return Fail(TEXT("Could not start the owned logic client."));
    UE_LOG(LogTemp,Display,TEXT("[GuLiLocalPIE] room=%s editor=%u logic=%u coordinator=%u role=%s"),
        *Config.RoomId.ToString(),FPlatformProcess::GetCurrentProcessId(),State->LogicPid,State->CoordinatorPid,
        *StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(Config.Role)));
    return true;
}
bool UGuLiLocalPIESubsystem::ResolveWorld(const UWorld* World,FGuLiNetworkLaunchConfig& Config)
{
    if (!State->Active || !World || World->WorldType!=EWorldType::PIE || !IsLocalMap(World)) return false;
    Config=State->Config;return true;
}
void UGuLiLocalPIESubsystem::BeganPIE(bool) { if (State->Active) State->Began=true; }
void UGuLiLocalPIESubsystem::EndedPIE(bool) { StopChildren(); }
void UGuLiLocalPIESubsystem::StopChildren()
{
    State->Active=false;
#if PLATFORM_WINDOWS
    if (State->StopEvent) SetEvent(State->StopEvent);
    const double Deadline=FPlatformTime::Seconds()+15;
    while (FPlatformTime::Seconds()<Deadline
        && ((State->Logic.IsValid() && FPlatformProcess::IsProcRunning(State->Logic))
            || (State->Coordinator.IsValid() && FPlatformProcess::IsProcRunning(State->Coordinator))))
        FPlatformProcess::Sleep(0.02f);
#endif
    for (FProcHandle* Handle:{&State->Logic,&State->Coordinator}) if (Handle->IsValid())
    {
        const bool Forced=FPlatformProcess::IsProcRunning(*Handle);
        if (Forced) FPlatformProcess::TerminateProc(*Handle,true);
        FPlatformProcess::WaitForProc(*Handle);
        int32 Code=0;const bool HasCode=FPlatformProcess::GetProcReturnCode(*Handle,&Code);
        auto Result=MakeShared<FJsonObject>();const bool Logic=Handle==&State->Logic;
        Result->SetStringField(TEXT("role"),Logic?TEXT("logic"):TEXT("coordinator"));
        Result->SetNumberField(TEXT("pid"),Logic?State->LogicPid:State->CoordinatorPid);
        Result->SetBoolField(TEXT("forced"),Forced);Result->SetBoolField(TEXT("has_exit_code"),HasCode);
        Result->SetNumberField(TEXT("exit_code"),Code);State->Shutdown.Add(MakeShared<FJsonValueObject>(Result));
        UE_LOG(LogTemp,Display,TEXT("[GuLiLocalPIE] stop role=%s forced=%d has_exit_code=%d exit_code=%d"),
            Logic?TEXT("logic"):TEXT("coordinator"),Forced,HasCode,Code);
        FPlatformProcess::CloseProc(*Handle);Handle->Reset();
    }
#if PLATFORM_WINDOWS
    // Keep the job as a crash/timeout safety net until normal shutdown finishes.
    if (State->Job) {CloseHandle(State->Job);State->Job=nullptr;}
    if (State->StopEvent) {CloseHandle(State->StopEvent);State->StopEvent=nullptr;}
#endif
    // This directory was generated below the OS temp root for this room only.
    if (!State->Directory.IsEmpty() && FPaths::IsUnderDirectory(State->Directory,FPlatformProcess::UserTempDir()))
        State->CleanupComplete=IFileManager::Get().DeleteDirectory(*State->Directory,false,true);
    State->Directory.Reset();State->Began=false;
}
bool UGuLiLocalPIESubsystem::Tick(float)
{
    if (!State->Active) return true;
    if (!GEditor || !GEditor->IsPlaySessionInProgress()) { StopChildren();return true; }
    const bool Children=State->Coordinator.IsValid() && FPlatformProcess::IsProcRunning(State->Coordinator)
        && State->Logic.IsValid() && FPlatformProcess::IsProcRunning(State->Logic);
    UWorld* World=GEditor->PlayWorld.Get();
    auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (!Children || (State->Began && FPlatformTime::Seconds()-State->Started>120 && (!Hybrid || !Hybrid->IsWorldReady())))
    {
        State->Error=Children?TEXT("Local PIE startup did not settle within 120 seconds."):TEXT("An owned local PIE child exited.");
        UE_LOG(LogTemp,Error,TEXT("[GuLiLocalPIE] %s"),*State->Error);
        GEditor->RequestEndPlayMap();StopChildren();
    }
    return true;
}
FString UGuLiLocalPIESubsystem::GetSessionStatus() const
{
    auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("active"),State->Active);J->SetStringField(TEXT("error"),State->Error);
    J->SetStringField(TEXT("room"),State->Config.RoomId.ToString(EGuidFormats::Digits));
    J->SetStringField(TEXT("role"),StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(State->Config.Role)));
    J->SetNumberField(TEXT("editor_pid"),FPlatformProcess::GetCurrentProcessId());
    J->SetNumberField(TEXT("logic_pid"),State->LogicPid);J->SetNumberField(TEXT("coordinator_pid"),State->CoordinatorPid);
    J->SetBoolField(TEXT("logic_running"),State->Logic.IsValid() && FPlatformProcess::IsProcRunning(State->Logic));
    J->SetBoolField(TEXT("coordinator_running"),State->Coordinator.IsValid() && FPlatformProcess::IsProcRunning(State->Coordinator));
    J->SetStringField(TEXT("session_directory"),State->Directory);
    J->SetArrayField(TEXT("shutdown"),State->Shutdown);
    J->SetBoolField(TEXT("cleanup_complete"),State->CleanupComplete);
    UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
    if (World)
    {
        J->SetStringField(TEXT("world"),World->GetOutermost()->GetName());
        J->SetBoolField(TEXT("standalone"),World->GetNetMode()==NM_Standalone);
        J->SetBoolField(TEXT("can_present"),UGuLiNetworkSettings::CanPresent(World));
        auto* GI=World->GetGameInstance();auto* Session=GI?GI->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;
        auto* H=World->GetSubsystem<UGuLiHybridWorldSubsystem>();J->SetBoolField(TEXT("world_ready"),H && H->IsWorldReady());
        if (Session)
        {
            J->SetStringField(TEXT("network_error"),Session->GetError());
            TArray<TSharedPtr<FJsonValue>> Members;
            for (const auto& M:Session->GetMembers())
            {
                auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("player"),M.Player.ToString(EGuidFormats::Digits));
                V->SetBoolField(TEXT("online"),M.bOnline);V->SetBoolField(TEXT("ready"),M.bReady);
                V->SetStringField(TEXT("team"),StaticEnum<EGuLiTeam>()->GetNameStringByValue(int64(M.Team)));
                V->SetStringField(TEXT("role"),StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(M.Role)));
                Members.Add(MakeShared<FJsonValueObject>(V));
            }
            J->SetArrayField(TEXT("members"),Members);
            if (H)
            {
                TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);int32 PeerObjects=0,Units=0;
                for (const auto& O:Objects)
                { if (O.SimulationOwner==State->Peer) ++PeerObjects;if (O.Key.Kind==EGuLiHybridObjectKind::Soldier) ++Units; }
                J->SetNumberField(TEXT("units"),Units);J->SetNumberField(TEXT("peer_owned_objects"),PeerObjects);
            }
        }
        auto* PC=World->GetFirstPlayerController();
        J->SetStringField(TEXT("controller"),PC?PC->GetClass()->GetPathName():TEXT(""));
        J->SetStringField(TEXT("pawn"),PC && PC->GetPawn()?PC->GetPawn()->GetClass()->GetPathName():TEXT(""));
        J->SetStringField(TEXT("hud"),PC && PC->GetHUD()?PC->GetHUD()->GetClass()->GetPathName():TEXT(""));
        if (const auto* Commander=Cast<AGuLiCommanderPlayerController>(PC))
        {
            J->SetBoolField(TEXT("commander_view_active"),Commander->IsCommanderViewActive());
            J->SetBoolField(TEXT("can_issue_orders"),Commander->CanIssueCommanderOrders());
            J->SetBoolField(TEXT("mouse_cursor_visible"),Commander->bShowMouseCursor);
            const auto* HUD=Cast<AGuLiCommanderHUD>(Commander->GetHUD());
            const auto* Widget=HUD?HUD->GetRuntimeHUDWidget():nullptr;
            J->SetStringField(TEXT("commander_hud_widget"),Widget?Widget->GetClass()->GetPathName():TEXT(""));
            J->SetBoolField(TEXT("commander_hud_in_viewport"),Widget && Widget->IsInViewport());
            J->SetBoolField(TEXT("commander_hud_visible"),Widget && Widget->IsVisible());
            J->SetBoolField(TEXT("commander_hud_owner_matches"),Widget && Widget->GetOwningPlayer()==Commander);
            const FVector2D Size=Widget?Widget->GetCachedGeometry().GetLocalSize():FVector2D::ZeroVector;
            J->SetNumberField(TEXT("commander_hud_width"),Size.X);
            J->SetNumberField(TEXT("commander_hud_height"),Size.Y);
        }
        if (const auto* Resources=World->GetSubsystem<UGuLiResourceWorldSubsystem>())
        {
            J->SetBoolField(TEXT("resource_query_ready"),Resources->IsHybridQuerySceneReady());
            J->SetBoolField(TEXT("legacy_resource_runtime_ready"),Resources->IsRuntimeReady());
        }
    }
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));return Text;
}
