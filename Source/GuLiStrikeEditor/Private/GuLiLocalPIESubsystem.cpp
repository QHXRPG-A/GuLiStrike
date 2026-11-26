#include "GuLiLocalPIESubsystem.h"
#include "GuLiWingmanPlayRange.h"
#include "GuLiXuntianRange.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDWidget.h"
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
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
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
TAutoConsoleVariable<int32> LogicDiagnostics(TEXT("gs.LocalPIE.LogicDiagnostics"),0,
    TEXT("Next PIE launch: 0 normal, 1 correctness snapshots, 2 existing network fault review. Never use diagnostics for performance sampling."));
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
    FSocket* Socket=Sockets->CreateSocket(NAME_DGram,TEXT("GuLiLocalPIE"),false);
    if (!Valid || !Socket) return false;
    const bool Result=Bind?Socket->Bind(*Address):Socket->Connect(*Address);
    Socket->Close();Sockets->DestroySocket(Socket);return Result;
}
}

struct UGuLiLocalPIESubsystem::FState
{
    FGuLiNetworkLaunchConfig Config;
    FGuid Peer;
    struct FChild { FProcHandle Handle;uint32 Pid=0; };
    TArray<FChild> Children;
    FString Directory,Error;
    TArray<TSharedPtr<FJsonValue>> Shutdown;
    bool CleanupComplete=false;
    double Started=0;
    bool Active=false,Began=false,ReachedReady=false;
    GuLiLocalPIERolePicker::ETrial Trial=GuLiLocalPIERolePicker::ETrial::None;
    FGuLiWingmanPlayRange WingmanRange;
    TOptional<EGuLiCommanderRole> LastSelectedRole;
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
    State->Error.Reset();State->Began=false;State->ReachedReady=false;State->Shutdown.Reset();State->CleanupComplete=false;
    State->Trial=GuLiLocalPIERolePicker::ETrial::None;
    State->WingmanRange={};
    auto Fail=[&](const FString& Why)
    { State->Error=Why;Error=Why;StopChildren();UE_LOG(LogTemp,Error,TEXT("[GuLiLocalPIE] %s"),*Why);return false; };
    if (Settings->MainRole!=EGuLiCommanderRole::Commander && Settings->MainRole!=EGuLiCommanderRole::Ground
        && Settings->MainRole!=EGuLiCommanderRole::Air) return Fail(TEXT("Choose Commander, Ground or Air before PIE."));
    const int32 ClientCount=Settings->bFourClients?4:2;
    if (Settings->LocalUdpBasePort<1024 || Settings->LocalUdpBasePort+ClientCount>65536)
        return Fail(TEXT("Invalid local UDP port range."));
    for (int32 I=0;I<ClientCount;++I) if (!LoopbackSocket(Settings->LocalUdpBasePort+I,true))
        return Fail(FString::Printf(TEXT("Local UDP port %d is occupied."),Settings->LocalUdpBasePort+I));
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString Client=FPaths::ConvertRelativePathToFull(FPaths::EngineDir()/TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
    if (!FPaths::FileExists(Client)) return Fail(TEXT("The source-engine editor is missing."));
    State->Config=UGuLiNetworkSettings::Resolve();
    auto& Config=State->Config;
    if (!Config.ValidationError.IsEmpty()) return Fail(Config.ValidationError);
    Config.Mode=EGuLiNetworkMode::Hybrid;Config.Transport=EGuLiNetworkTransport::LocalUdp;
    Config.ClientRunMode=EGuLiClientRunMode::Rendered;Config.Team=EGuLiTeam::Red;Config.Role=Settings->MainRole;
    Config.ManifestFile=Root/TEXT("Config/Hybrid/RuntimeManifest-600.json");
    Config.RoomId=FGuid::NewGuid();Config.DevelopmentPlayerId=FGuid::NewGuid();Config.LocalUdpPort=Settings->LocalUdpBasePort;
    Config.HostRoom=true;Config.ExpectedPlayers=ClientCount;
    FString ConfigHash,MapHash,Map,Why;
    if (!FGuLiHybridManifest::ValidateFile(Config.ManifestFile,ConfigHash,MapHash,Map,Why) || Map!=LocalMap)
        return Fail(Why.IsEmpty()?TEXT("The normal runtime manifest belongs to another map."):Why);
    FGuLiHybridManifestData Manifest;
    if (!FGuLiHybridManifest::Read(Config.ManifestFile,Manifest,Why)) return Fail(Why);
    int32 Units=0;for (const auto& Object:Manifest.Objects) if (Object.Key.Kind==EGuLiHybridObjectKind::Soldier) ++Units;
    if (Units!=600 || Manifest.Numbers.FindRef(TEXT("acceptance/resource_supply"))!=0)
        return Fail(TEXT("Normal local PIE requires the production 600-unit manifest without acceptance supply."));
    // Keep unattended callers on their explicit role; interactive PIE chooses
    // before creating the room directory, stop event or logic peers.
    if (Settings->bPromptForRole && !FApp::IsUnattended() && !IsRunningCommandlet())
    {
        const auto Selected = GuLiLocalPIERolePicker::Show(State->LastSelectedRole.Get(Settings->MainRole),State->Trial);
        if (!Selected.IsSet())
        {
            Error=TEXT("已取消开局选择。");
            State->CleanupComplete=true;
            return false;
        }
        Config.Role=Selected.GetValue();
        State->LastSelectedRole=Config.Role;
    }
    State->Peer=FGuid::NewGuid();
    State->Directory=FPaths::Combine(FPlatformProcess::UserTempDir(),TEXT("GuLiStrikePIE"),Config.RoomId.ToString(EGuidFormats::Digits));
    if (!IFileManager::Get().MakeDirectory(*State->Directory,true)) return Fail(TEXT("Could not create the local session directory."));
    State->Active=true;State->Started=FPlatformTime::Seconds();
    if (State->Trial==GuLiLocalPIERolePicker::ETrial::Barrage)
    {
        Config.ManifestFile=Root/TEXT("Config/Hybrid/RuntimeManifest-NoSurvivors.json");
        FString RangeConfigHash,RangeMapHash,RangeMap;
        if (!FGuLiHybridManifest::ValidateFile(Config.ManifestFile,RangeConfigHash,RangeMapHash,RangeMap,Why))
            return Fail(Why);
        if (RangeMap!=LocalMap || RangeMapHash!=MapHash)
            return Fail(TEXT("The barrage range roster does not match the current map. Re-export the range manifest."));
    }
    else if (State->Trial==GuLiLocalPIERolePicker::ETrial::XuntianHUD)
    {
        Config.ManifestFile=State->Directory/TEXT("XuntianHUD.json");
        if (!GuLiXuntianRange::Prepare(*EditorWorld(),Manifest,Config.ManifestFile,Why)) return Fail(Why);
    }
    else if (GuLiLocalPIERolePicker::IsWingmanTrial(State->Trial))
    {
        Config.ManifestFile=State->Directory/TEXT("WingmanRange.json");
        if (!State->WingmanRange.Prepare(*EditorWorld(),State->Trial,Manifest,Config.ManifestFile,Why)) return Fail(Why);
    }
    FString ClientStopArguments;
#if PLATFORM_WINDOWS
    const FString StopName=TEXT("Local\\GuLiStrikePIEStop_")+Config.RoomId.ToString(EGuidFormats::Digits);
    State->StopEvent=CreateEventW(nullptr,1,0,*StopName);
    if (!State->StopEvent) return Fail(TEXT("Could not create the owned local PIE stop event."));
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
    struct FSeat {FGuid Player;EGuLiTeam Team;EGuLiCommanderRole Role;};
    const bool bAirOpponent=GuLiLocalPIERolePicker::HasAirOpponent(State->Trial);
    const auto BlueMainRole=bAirOpponent && !Settings->bFourClients ? EGuLiCommanderRole::Air : EGuLiCommanderRole::Commander;
    TArray<FSeat> Seats={{Config.DevelopmentPlayerId,EGuLiTeam::Red,Config.Role},{State->Peer,EGuLiTeam::Blue,BlueMainRole}};
    if (Settings->bFourClients)
    {
        Seats.Add({FGuid::NewGuid(),EGuLiTeam::Red,Config.Role==EGuLiCommanderRole::Commander?EGuLiCommanderRole::Ground:EGuLiCommanderRole::Commander});
        Seats.Add({FGuid::NewGuid(),EGuLiTeam::Blue,bAirOpponent?EGuLiCommanderRole::Air:EGuLiCommanderRole::Ground});
    }
    auto Ticket=MakeShared<FJsonObject>();Ticket->SetStringField(TEXT("room"),Config.RoomId.ToString(EGuidFormats::Digits));
    Ticket->SetStringField(TEXT("host"),Config.DevelopmentPlayerId.ToString(EGuidFormats::Digits));TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 I=0;I<Seats.Num();++I)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("player"),Seats[I].Player.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("team"),Seats[I].Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"));
        J->SetStringField(TEXT("role"),StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(Seats[I].Role)));
        J->SetStringField(TEXT("endpoint"),FString::Printf(TEXT("127.0.0.1:%d"),Settings->LocalUdpBasePort+I));Rows.Add(MakeShared<FJsonValueObject>(J));
    }
    Ticket->SetArrayField(TEXT("members"),Rows);FString TicketText;FJsonSerializer::Serialize(Ticket,TJsonWriterFactory<>::Create(&TicketText));
    Config.PeerRosterFile=State->Directory/TEXT("peers.json");
    if (!FFileHelper::SaveStringToFile(TicketText,*Config.PeerRosterFile)) return Fail(TEXT("Cannot write peer roster."));
    State->Children.SetNum(Seats.Num()-1);
    const FString Project=FPaths::GetProjectFilePath();
    const auto CVarValue=[](const TCHAR* Name)
    {const auto* V=IConsoleManager::Get().FindConsoleVariable(Name);return V?V->GetInt():1;};
    const FString CpuOptions=FString::Printf(TEXT(" -GuLiYieldBetweenCaptureCuts=%d -GuLiFastPackCacheKey=%d -GuLiNativePeerReceive=%d -GuLiReusePayloadCompressor=%d -GuLiFastSmallPayloadCompression=%d -ExecCmds=\"gs.Hybrid.FlightServiceFastPath %d,gs.Hybrid.NativePeerSend %d,gs.Hybrid.EntityLayoutCache %d,gs.Review.GroundIsolateCombat %d,gs.Perf.MovementFixtureHold %d\""),
        CVarValue(TEXT("gs.Hybrid.YieldBetweenCaptureCuts")),CVarValue(TEXT("gs.Hybrid.FastPackCacheKey")),
        CVarValue(TEXT("gs.Hybrid.NativePeerReceive")),
        CVarValue(TEXT("gs.Hybrid.ReusePayloadCompressor")),
        CVarValue(TEXT("gs.Hybrid.FastSmallPayloadCompression")),
        CVarValue(TEXT("gs.Hybrid.FlightServiceFastPath")),CVarValue(TEXT("gs.Hybrid.NativePeerSend")),
        CVarValue(TEXT("gs.Hybrid.EntityLayoutCache")),CVarValue(TEXT("gs.Review.GroundIsolateCombat")),CVarValue(TEXT("gs.Perf.MovementFixtureHold")));
    for (int32 I=1;I<Seats.Num();++I)
    {
        const auto& Seat=Seats[I];auto& Child=State->Children[I-1];
        FString Args=FString::Printf(TEXT("\"%s\" %s -game -NullRHI -nosound -unattended -NoSplash -NoLiveCoding -GuLiNetMode=Hybrid -GuLiTransport=LocalUdp -GuLiRoom=%s -GuLiDevPlayerId=%s -GuLiTeam=%s -GuLiRole=%s -GuLiClientRunMode=LogicOnly -GuLiHotspotMask=%u -GuLiGameplayWorkers=%u -GuLiUdpPort=%d -GuLiPeerRoster=\"%s\" -GuLiManifest=\"%s\" -abslog=\"%s\""),
            *Project,LocalMap,*Config.RoomId.ToString(EGuidFormats::Digits),*Seat.Player.ToString(EGuidFormats::Digits),
            Seat.Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"),*StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(Seat.Role)),
            Config.HotspotMask,Config.GameplayWorkers,Settings->LocalUdpBasePort+I,*Config.PeerRosterFile,*Config.ManifestFile,
            *(State->Directory/(I==1?TEXT("logic.log"):FString::Printf(TEXT("logic-%d.log"),I))))+ClientStopArguments+CpuOptions;
        if (LogicDiagnostics.GetValueOnGameThread()!=0)
            Args+=FString::Printf(TEXT(" -GuLiAcceptanceRun=%s -GuLiAcceptanceCase=combat-diagnostics -GuLiAcceptanceDirectory=\"%s\""),
                *Config.RoomId.ToString(EGuidFormats::Digits),
                *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("TestResults/LocalPIE")/Config.RoomId.ToString(EGuidFormats::Digits)/FString::Printf(TEXT("Logic-%d"),I)));
        if (!Launch(Client,Args,Child.Handle,Child.Pid)) return Fail(TEXT("Could not start an owned logic client."));
    }
    UE_LOG(LogTemp,Display,TEXT("[GuLiLocalPIE] room=%s clients=%d editor=%u role=%s"),
        *Config.RoomId.ToString(),Seats.Num(),FPlatformProcess::GetCurrentProcessId(),
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
    while (FPlatformTime::Seconds()<Deadline)
    {
        bool AnyChildRunning=false;
        for (auto& Child:State->Children)
        {
            if (Child.Handle.IsValid() && FPlatformProcess::IsProcRunning(Child.Handle)) {AnyChildRunning=true;break;}
        }
        if (!AnyChildRunning) break;
        FPlatformProcess::Sleep(0.02f);
    }
#endif
    for (auto& Child:State->Children) if (Child.Handle.IsValid())
    {
        auto* Handle=&Child.Handle;
        const bool Forced=FPlatformProcess::IsProcRunning(*Handle);
        if (Forced) FPlatformProcess::TerminateProc(*Handle,true);
        FPlatformProcess::WaitForProc(*Handle);
        int32 Code=0;const bool HasCode=FPlatformProcess::GetProcReturnCode(*Handle,&Code);
        auto Result=MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("role"),TEXT("logic"));
        Result->SetNumberField(TEXT("pid"),Child.Pid);
        Result->SetBoolField(TEXT("forced"),Forced);Result->SetBoolField(TEXT("has_exit_code"),HasCode);
        Result->SetNumberField(TEXT("exit_code"),Code);State->Shutdown.Add(MakeShared<FJsonValueObject>(Result));
        UE_LOG(LogTemp,Display,TEXT("[GuLiLocalPIE] stop role=%s forced=%d has_exit_code=%d exit_code=%d"),
            TEXT("logic"),Forced,HasCode,Code);
        FPlatformProcess::CloseProc(*Handle);Handle->Reset();
    }
#if PLATFORM_WINDOWS
    // Keep the job as a crash/timeout safety net until normal shutdown finishes.
    if (State->Job) {CloseHandle(State->Job);State->Job=nullptr;}
    if (State->StopEvent) {CloseHandle(State->StopEvent);State->StopEvent=nullptr;}
#endif
    // This directory was generated below the OS temp root for this room only.
    if (!State->Directory.IsEmpty() && FPaths::IsUnderDirectory(State->Directory,FPlatformProcess::UserTempDir()))
    {
        // Keep the failed child's final diagnostics before removing the owned room.
        // One fixed file bounds retention; successful sessions do not overwrite it.
        const FString LogicLog=State->Directory/TEXT("logic.log");
        if (!State->Error.IsEmpty() && IFileManager::Get().FileExists(*LogicLog))
        {
            const FString Retained=FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir()/TEXT("GuLiLocalPIE-last-failure.log"));
            const bool Saved=IFileManager::Get().Copy(*Retained,*LogicLog,true,false,false)==COPY_OK;
            UE_LOG(LogTemp,Display,TEXT("[GuLiLocalPIE] failure_log_saved=%d room=%s path=%s"),
                Saved,*State->Config.RoomId.ToString(EGuidFormats::Digits),*Retained);
        }
        State->CleanupComplete=IFileManager::Get().DeleteDirectory(*State->Directory,false,true);
    }
    State->Directory.Reset();State->Began=false;
}
bool UGuLiLocalPIESubsystem::Tick(float)
{
    if (!State->Active) return true;
    if (!GEditor || !GEditor->IsPlaySessionInProgress()) { StopChildren();return true; }
    bool Children=true;
    for (auto& Child:State->Children)
    {
        if (!Child.Handle.IsValid() || !FPlatformProcess::IsProcRunning(Child.Handle)) {Children=false;break;}
    }
    UWorld* World=GEditor->PlayWorld.Get();
    auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if(Hybrid && Hybrid->IsWorldReady())State->ReachedReady=true;
    if (State->Trial==GuLiLocalPIERolePicker::ETrial::Barrage && State->ReachedReady && World)
    {
        auto* PC=World->GetFirstPlayerController();
        if (auto* Mech=PC ? Cast<AGuLiGroundMechCharacter>(PC->GetPawn()) : nullptr)
            for (TActorIterator<ATargetPoint> It(World);It;++It)
                if (It->ActorHasTag(TEXT("GuLi.NoSurvivors.Start")))
                {
                    if (Mech->EnterBarrageRange(It->GetActorTransform())) State->Trial=GuLiLocalPIERolePicker::ETrial::None;
                    break;
                }
    }
    if (State->ReachedReady && World && GuLiLocalPIERolePicker::IsWingmanTrial(State->Trial) && !State->WingmanRange.bReady
        && !State->WingmanRange.Advance(*World,State->Error))
    {
        UE_LOG(LogTemp,Error,TEXT("[GuLiLocalPIE] %s"),*State->Error);
        GEditor->RequestEndPlayMap();StopChildren();return true;
    }
    const bool StartupTimedOut=!State->ReachedReady && State->Began && FPlatformTime::Seconds()-State->Started>120;
    const FString Failure=Hybrid?Hybrid->GetFailure():FString();
    if((!Children && !State->ReachedReady) || StartupTimedOut || !Failure.IsEmpty())
    {
        State->Error=!Failure.IsEmpty()?Failure:!Children?TEXT("An owned local PIE child exited."):TEXT("Local PIE startup did not settle within 120 seconds.");
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
    TArray<TSharedPtr<FJsonValue>> Processes;
    for (auto& Child:State->Children)
    {auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("pid"),Child.Pid);P->SetBoolField(TEXT("running"),Child.Handle.IsValid() && FPlatformProcess::IsProcRunning(Child.Handle));Processes.Add(MakeShared<FJsonValueObject>(P));}
    J->SetArrayField(TEXT("logic_processes"),Processes);
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
            // Read native geometry here: Python transport must not infer visibility
            // from a UMG registry entry or a round-tripped FGeometry alone.
            const auto* GroundWidget=HUD?HUD->GetGroundHUDWidget():nullptr;
            J->SetStringField(TEXT("ground_hud_widget"),GroundWidget?GroundWidget->GetClass()->GetPathName():TEXT(""));
            J->SetBoolField(TEXT("ground_hud_in_viewport"),GroundWidget && GroundWidget->IsInViewport());
            J->SetBoolField(TEXT("ground_hud_visible"),GroundWidget && GroundWidget->IsVisible());
            J->SetBoolField(TEXT("ground_hud_owner_matches"),GroundWidget && GroundWidget->GetOwningPlayer()==Commander);
            const FVector2D GroundSize=GroundWidget?GroundWidget->GetCachedGeometry().GetLocalSize():FVector2D::ZeroVector;
            J->SetNumberField(TEXT("ground_hud_width"),GroundSize.X);
            J->SetNumberField(TEXT("ground_hud_height"),GroundSize.Y);
        }
        if (const auto* Resources=World->GetSubsystem<UGuLiResourceWorldSubsystem>())
        {
            J->SetBoolField(TEXT("resource_query_ready"),Resources->IsHybridQuerySceneReady());
            J->SetBoolField(TEXT("legacy_resource_runtime_ready"),Resources->IsRuntimeReady());
        }
    }
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));return Text;
}
