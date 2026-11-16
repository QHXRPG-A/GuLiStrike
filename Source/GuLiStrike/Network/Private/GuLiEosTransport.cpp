#include "Network/Private/GuLiHybridTransport.h"
#include "Modules/ModuleManager.h"
#include "EOSShared.h"
#include "IEOSSDKManager.h"
#include "Misc/Paths.h"
#if WITH_EOS_SDK
#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_p2p.h"
#include "eos_lobby.h"
#endif

namespace
{
#if WITH_EOS_SDK
struct FEosState;
struct FGuLiEosCallbackBase { virtual ~FGuLiEosCallbackBase() = default; };
template<typename T> struct TGuLiEosCallback : FGuLiEosCallbackBase
{
    TWeakPtr<FEosState> State;
    TFunction<void(FEosState&, const T&)> Invoke;
    static void EOS_CALL Complete(const T* Info);
};

struct FEosState : TSharedFromThis<FEosState>
{
    IEOSPlatformHandlePtr Platform;
    EOS_HConnect Connect = nullptr;
    EOS_HP2P P2P = nullptr;
    EOS_HLobby Lobby = nullptr;
    FGuLiNetworkLaunchConfig Config;
    FString LobbyId;
    FGuid PlayerId;
    bool RoomOpening=false,AttributesReady=false;
    double NextRoomRead=0;
    EOS_NotificationId LobbyUpdated=EOS_INVALID_NOTIFICATIONID,MemberUpdated=EOS_INVALID_NOTIFICATIONID,MemberStatus=EOS_INVALID_NOTIFICATIONID;
    EOS_ProductUserId LocalUser = nullptr;
    EOS_P2P_SocketId Socket{};
    EOS_NotificationId Expiration = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId LoginChanged = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId PeerRequest = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId PeerClosed = EOS_INVALID_NOTIFICATIONID;
    bool Active = true;
    IGuLiHybridTransport::FReady Ready;
    IGuLiHybridTransport::FFailure Failure;
    TMap<FGuid, EOS_ProductUserId> Peers;
    TArray<TUniquePtr<FGuLiEosCallbackBase>> PendingCallbacks;

    template<typename T> TGuLiEosCallback<T>* Callback(TFunction<void(FEosState&, const T&)> Invoke)
    {
        auto Value=MakeUnique<TGuLiEosCallback<T>>(); auto* Raw=Value.Get();
        Value->State=AsShared(); Value->Invoke=MoveTemp(Invoke); PendingCallbacks.Add(MoveTemp(Value)); return Raw;
    }
    void Fail(const FString& Why) { if (Active && Failure) Failure(Why); }
    void Login()
    {
        EOS_Connect_Credentials Credentials{}; Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
        Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
        EOS_Connect_UserLoginInfo User{}; User.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST; User.DisplayName = "GuLiPlayer";
        EOS_Connect_LoginOptions Options{}; Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; Options.Credentials = &Credentials; Options.UserLoginInfo = &User;
        auto* Context = Callback<EOS_Connect_LoginCallbackInfo>([](FEosState& S, const auto& Info)
        {
            if (Info.ResultCode == EOS_EResult::EOS_Success) { S.LoggedIn(Info.LocalUserId); return; }
            if (Info.ResultCode != EOS_EResult::EOS_InvalidUser || !Info.ContinuanceToken)
            { S.Fail(TEXT("EOS Connect anonymous login failed")); return; }
            EOS_Connect_CreateUserOptions Create{}; Create.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST; Create.ContinuanceToken = Info.ContinuanceToken;
            auto* C = S.Callback<EOS_Connect_CreateUserCallbackInfo>([](FEosState& State, const auto& Result)
            {
                if (Result.ResultCode == EOS_EResult::EOS_Success) State.LoggedIn(Result.LocalUserId);
                else State.Fail(TEXT("EOS Connect user creation failed"));
            });
            EOS_Connect_CreateUser(S.Connect, &Create, C, &TGuLiEosCallback<EOS_Connect_CreateUserCallbackInfo>::Complete);
        });
        EOS_Connect_Login(Connect, &Options, Context, &TGuLiEosCallback<EOS_Connect_LoginCallbackInfo>::Complete);
    }
    void LoggedIn(EOS_ProductUserId User)
    {
        if (LocalUser && LocalUser != User) { Fail(TEXT("EOS identity changed during the match")); return; }
        LocalUser = User;
        FGuLiTransportIdentity Identity;
        char Text[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{}; int32 Size = UE_ARRAY_COUNT(Text);
        if (EOS_ProductUserId_ToString(User,Text,&Size)!=EOS_EResult::EOS_Success)
        {Fail(TEXT("Invalid EOS product user identity"));return;}
        Identity.ProductUserId=UTF8_TO_TCHAR(Text);
        if (!FGuid::ParseExact(Identity.ProductUserId,EGuidFormats::Digits,Identity.Player))
        {Fail(TEXT("EOS product identity cannot be represented by the protocol"));return;}
        PlayerId=Identity.Player;
        if (Expiration == EOS_INVALID_NOTIFICATIONID) RegisterNotifications();
        if (Expiration == EOS_INVALID_NOTIFICATIONID || LoginChanged == EOS_INVALID_NOTIFICATIONID
            || PeerRequest == EOS_INVALID_NOTIFICATIONID || PeerClosed == EOS_INVALID_NOTIFICATIONID)
        { Fail(TEXT("EOS notification registration failed")); return; }
        if (Ready) Ready(Identity);
        if (!RoomOpening) OpenRoom();
    }
    void OpenRoom()
    {
        RoomOpening=true;
        if (Config.HostRoom)
        {
            if (!Config.RoomId.IsValid()) Config.RoomId=FGuid::NewGuid();
            const FTCHARToUTF8 Id(*Config.RoomId.ToString(EGuidFormats::Digits));
            EOS_Lobby_CreateLobbyOptions O{};O.ApiVersion=EOS_LOBBY_CREATELOBBY_API_LATEST;O.LocalUserId=LocalUser;
            O.MaxLobbyMembers=Config.ExpectedPlayers;O.PermissionLevel=EOS_ELobbyPermissionLevel::EOS_LPL_JOINVIAPRESENCE;
            O.BucketId="GuLiHybrid45";O.bDisableHostMigration=EOS_FALSE;O.bEnableJoinById=EOS_TRUE;O.LobbyId=Id.Get();
            auto* C=Callback<EOS_Lobby_CreateLobbyCallbackInfo>([](FEosState& S,const auto& I)
            {if (I.ResultCode!=EOS_EResult::EOS_Success) {S.Fail(TEXT("EOS lobby creation failed"));return;}S.LobbyId=UTF8_TO_TCHAR(I.LobbyId);S.AdvertiseSeat();});
            EOS_Lobby_CreateLobby(Lobby,&O,C,&TGuLiEosCallback<EOS_Lobby_CreateLobbyCallbackInfo>::Complete);
        }
        else
        {
            if (Config.LobbyId.IsEmpty()) {Fail(TEXT("EOS join requires -GuLiLobby=<lobby ID>"));return;}
            const FTCHARToUTF8 Id(*Config.LobbyId);
            EOS_Lobby_JoinLobbyByIdOptions O{};O.ApiVersion=EOS_LOBBY_JOINLOBBYBYID_API_LATEST;O.LocalUserId=LocalUser;O.LobbyId=Id.Get();
            auto* C=Callback<EOS_Lobby_JoinLobbyByIdCallbackInfo>([](FEosState& S,const auto& I)
            {if (I.ResultCode!=EOS_EResult::EOS_Success) {S.Fail(TEXT("EOS lobby join failed"));return;}S.LobbyId=UTF8_TO_TCHAR(I.LobbyId);S.AdvertiseSeat();});
            EOS_Lobby_JoinLobbyById(Lobby,&O,C,&TGuLiEosCallback<EOS_Lobby_JoinLobbyByIdCallbackInfo>::Complete);
        }
    }
    void AdvertiseSeat()
    {
        UE_LOG(LogNet,Display,TEXT("[GuLiPeerRoom] EOS lobby=%s"),*LobbyId);
        const FTCHARToUTF8 Id(*LobbyId);EOS_HLobbyModification Modification=nullptr;
        EOS_Lobby_UpdateLobbyModificationOptions O{};O.ApiVersion=EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;O.LocalUserId=LocalUser;O.LobbyId=Id.Get();
        if (EOS_Lobby_UpdateLobbyModification(Lobby,&O,&Modification)!=EOS_EResult::EOS_Success)
        {Fail(TEXT("EOS lobby metadata unavailable"));return;}
        const auto Add=[&](const char* Key,const FString& Value,bool Member)
        {
            const FTCHARToUTF8 Text(*Value);EOS_Lobby_AttributeData Data{};Data.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
            Data.Key=Key;Data.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;Data.Value.AsUtf8=Text.Get();
            if (Member)
            {EOS_LobbyModification_AddMemberAttributeOptions A{};A.ApiVersion=EOS_LOBBYMODIFICATION_ADDMEMBERATTRIBUTE_API_LATEST;A.Attribute=&Data;A.Visibility=EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;return EOS_LobbyModification_AddMemberAttribute(Modification,&A)==EOS_EResult::EOS_Success;}
            EOS_LobbyModification_AddAttributeOptions A{};A.ApiVersion=EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;A.Attribute=&Data;A.Visibility=EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
            return EOS_LobbyModification_AddAttribute(Modification,&A)==EOS_EResult::EOS_Success;
        };
        bool Good=Add("guli-team",Config.Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"),true)
            && Add("guli-role",StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(Config.Role)),true);
        if (Config.HostRoom) Good=Good && Add("guli-match",Config.RoomId.ToString(EGuidFormats::Digits),false)
            && Add("guli-host",PlayerId.ToString(EGuidFormats::Digits),false) && Add("guli-players",FString::FromInt(Config.ExpectedPlayers),false);
        if (!Good) {EOS_LobbyModification_Release(Modification);Fail(TEXT("EOS lobby attributes rejected"));return;}
        EOS_Lobby_UpdateLobbyOptions Update{};Update.ApiVersion=EOS_LOBBY_UPDATELOBBY_API_LATEST;Update.LobbyModificationHandle=Modification;
        auto* C=Callback<EOS_Lobby_UpdateLobbyCallbackInfo>([](FEosState& S,const auto& I)
        {if (I.ResultCode==EOS_EResult::EOS_Success) S.AttributesReady=true;else S.Fail(TEXT("EOS lobby seat publication failed"));});
        EOS_Lobby_UpdateLobby(Lobby,&Update,C,&TGuLiEosCallback<EOS_Lobby_UpdateLobbyCallbackInfo>::Complete);
        EOS_LobbyModification_Release(Modification);
    }
    bool ReadRoom(FGuLiTransportRoom& Out)
    {
        if (!AttributesReady || FPlatformTime::Seconds()<NextRoomRead) return false;NextRoomRead=FPlatformTime::Seconds()+.5;
        const FTCHARToUTF8 Id(*LobbyId);EOS_HLobbyDetails Details=nullptr;
        EOS_Lobby_CopyLobbyDetailsHandleOptions O{};O.ApiVersion=EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;O.LobbyId=Id.Get();O.LocalUserId=LocalUser;
        if (EOS_Lobby_CopyLobbyDetailsHandle(Lobby,&O,&Details)!=EOS_EResult::EOS_Success) return false;
        const auto Attribute=[&](const char* Key,EOS_ProductUserId User=nullptr)
        {
            EOS_Lobby_Attribute* A=nullptr;
            if (User) {EOS_LobbyDetails_CopyMemberAttributeByKeyOptions C{};C.ApiVersion=EOS_LOBBYDETAILS_COPYMEMBERATTRIBUTEBYKEY_API_LATEST;C.TargetUserId=User;C.AttrKey=Key;EOS_LobbyDetails_CopyMemberAttributeByKey(Details,&C,&A);}
            else {EOS_LobbyDetails_CopyAttributeByKeyOptions C{};C.ApiVersion=EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;C.AttrKey=Key;EOS_LobbyDetails_CopyAttributeByKey(Details,&C,&A);}
            FString Value;if (A && A->Data && A->Data->ValueType==EOS_ELobbyAttributeType::EOS_AT_STRING && A->Data->Value.AsUtf8) Value=UTF8_TO_TCHAR(A->Data->Value.AsUtf8);
            if (A) EOS_Lobby_Attribute_Release(A);return Value;
        };
        bool Good=FGuid::Parse(Attribute("guli-match"),Out.Match) && FGuid::Parse(Attribute("guli-host"),Out.Host);
        const int32 Expected=FCString::Atoi(*Attribute("guli-players"));
        EOS_LobbyDetails_GetMemberCountOptions Count{};Count.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
        const uint32 N=EOS_LobbyDetails_GetMemberCount(Details,&Count);
        Good=Good && Expected>=2 && Expected<=4 && N==uint32(Expected);
        for (uint32 I=0;Good && I<N;++I)
        {
            EOS_LobbyDetails_GetMemberByIndexOptions Index{};Index.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;Index.MemberIndex=I;
            const auto User=EOS_LobbyDetails_GetMemberByIndex(Details,&Index);FGuLiTransportRoomMember M;
            char Text[EOS_PRODUCTUSERID_MAX_LENGTH+1]{};int32 Size=UE_ARRAY_COUNT(Text);
            if (!User || EOS_ProductUserId_ToString(User,Text,&Size)!=EOS_EResult::EOS_Success) {Good=false;break;}
            M.Address=UTF8_TO_TCHAR(Text);Good=FGuid::ParseExact(M.Address,EGuidFormats::Digits,M.Player);
            const FString Team=Attribute("guli-team",User),Role=Attribute("guli-role",User);
            Good=Good && (Team==TEXT("Red") || Team==TEXT("Blue")) && (Role==TEXT("Commander") || Role==TEXT("Ground") || Role==TEXT("Air"));
            M.Team=Team==TEXT("Red")?EGuLiTeam::Red:EGuLiTeam::Blue;
            M.Role=Role==TEXT("Commander")?EGuLiCommanderRole::Commander:Role==TEXT("Air")?EGuLiCommanderRole::Air:EGuLiCommanderRole::Ground;Out.Members.Add(M);
        }
        EOS_LobbyDetails_Release(Details);return Good;
    }
    void RegisterNotifications()
    {
        EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions U{};U.ApiVersion=EOS_LOBBY_ADDNOTIFYLOBBYUPDATERECEIVED_API_LATEST;
        LobbyUpdated=EOS_Lobby_AddNotifyLobbyUpdateReceived(Lobby,&U,this,[](const EOS_Lobby_LobbyUpdateReceivedCallbackInfo* I){static_cast<FEosState*>(I->ClientData)->NextRoomRead=0;});
        EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions M{};M.ApiVersion=EOS_LOBBY_ADDNOTIFYLOBBYMEMBERUPDATERECEIVED_API_LATEST;
        MemberUpdated=EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(Lobby,&M,this,[](const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* I){static_cast<FEosState*>(I->ClientData)->NextRoomRead=0;});
        EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions S{};S.ApiVersion=EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
        MemberStatus=EOS_Lobby_AddNotifyLobbyMemberStatusReceived(Lobby,&S,this,[](const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* I){static_cast<FEosState*>(I->ClientData)->NextRoomRead=0;});
        EOS_Connect_AddNotifyAuthExpirationOptions Ex{}; Ex.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
        Expiration = EOS_Connect_AddNotifyAuthExpiration(Connect, &Ex, this, [](const EOS_Connect_AuthExpirationCallbackInfo* Info)
        { auto& S = *static_cast<FEosState*>(Info->ClientData); if (S.Active && Info->LocalUserId == S.LocalUser) S.Login(); });
        EOS_Connect_AddNotifyLoginStatusChangedOptions L{}; L.ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
        LoginChanged = EOS_Connect_AddNotifyLoginStatusChanged(Connect, &L, this, [](const EOS_Connect_LoginStatusChangedCallbackInfo* Info)
        {
            auto& S = *static_cast<FEosState*>(Info->ClientData);
            if (S.Active && Info->LocalUserId == S.LocalUser && Info->CurrentStatus != EOS_ELoginStatus::EOS_LS_LoggedIn)
                S.Fail(TEXT("EOS Connect login was lost"));
        });
        EOS_P2P_AddNotifyPeerConnectionRequestOptions P{}; P.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
        P.LocalUserId = LocalUser; P.SocketId = &Socket;
        PeerRequest = EOS_P2P_AddNotifyPeerConnectionRequest(P2P, &P, this, [](const EOS_P2P_OnIncomingConnectionRequestInfo* Info)
        {
            auto& S = *static_cast<FEosState*>(Info->ClientData);
            if (!S.Active || !Info->SocketId || FCStringAnsi::Strcmp(Info->SocketId->SocketName, S.Socket.SocketName) != 0) return;
            bool Known = false; for (const auto& Peer : S.Peers) Known |= Peer.Value == Info->RemoteUserId;
            if (!Known) return;
            EOS_P2P_AcceptConnectionOptions A{}; A.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
            A.LocalUserId = S.LocalUser; A.RemoteUserId = Info->RemoteUserId; A.SocketId = &S.Socket;
            EOS_P2P_AcceptConnection(S.P2P, &A);
        });
        EOS_P2P_AddNotifyPeerConnectionClosedOptions Closed{}; Closed.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
        Closed.LocalUserId = LocalUser; Closed.SocketId = &Socket;
        PeerClosed = EOS_P2P_AddNotifyPeerConnectionClosed(P2P, &Closed, this, [](const EOS_P2P_OnRemoteConnectionClosedInfo* Info)
        {
            auto& S = *static_cast<FEosState*>(Info->ClientData);
            // The peer-room heartbeat handles loss; do not tear down surviving peers here.
        });
    }
    void Stop()
    {
        if (Lobby)
        {
            if (LobbyUpdated!=EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyUpdateReceived(Lobby,LobbyUpdated);
            if (MemberUpdated!=EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(Lobby,MemberUpdated);
            if (MemberStatus!=EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(Lobby,MemberStatus);
            if (LocalUser && !LobbyId.IsEmpty())
            {const FTCHARToUTF8 Id(*LobbyId);EOS_Lobby_LeaveLobbyOptions O{};O.ApiVersion=EOS_LOBBY_LEAVELOBBY_API_LATEST;O.LocalUserId=LocalUser;O.LobbyId=Id.Get();EOS_Lobby_LeaveLobby(Lobby,&O,nullptr,[](const EOS_Lobby_LeaveLobbyCallbackInfo*){});}
        }
        Active = false;
        if (Connect)
        {
            if (Expiration != EOS_INVALID_NOTIFICATIONID) EOS_Connect_RemoveNotifyAuthExpiration(Connect, Expiration);
            if (LoginChanged != EOS_INVALID_NOTIFICATIONID) EOS_Connect_RemoveNotifyLoginStatusChanged(Connect, LoginChanged);
        }
        if (P2P)
        {
            if (PeerRequest != EOS_INVALID_NOTIFICATIONID) EOS_P2P_RemoveNotifyPeerConnectionRequest(P2P, PeerRequest);
            if (PeerClosed != EOS_INVALID_NOTIFICATIONID) EOS_P2P_RemoveNotifyPeerConnectionClosed(P2P, PeerClosed);
            if (LocalUser)
            {
                EOS_P2P_CloseConnectionsOptions Options{}; Options.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
                Options.LocalUserId = LocalUser; Options.SocketId = &Socket; EOS_P2P_CloseConnections(P2P, &Options);
            }
        }
        Ready = {}; Failure = {}; Peers.Reset(); Platform.Reset(); Connect = nullptr; P2P = nullptr;
        PendingCallbacks.Reset();
    }
};
template<typename T> void EOS_CALL TGuLiEosCallback<T>::Complete(const T* Info)
{
    if (!EOS_EResult_IsOperationComplete(Info->ResultCode)) return;
    auto* Context=static_cast<TGuLiEosCallback*>(Info->ClientData);
    if (auto S=Context->State.Pin())
    {
        const int32 Index=S->PendingCallbacks.IndexOfByPredicate([Context](const auto& P) { return P.Get()==Context; });
        if (Index==INDEX_NONE) return;
        TUniquePtr<FGuLiEosCallbackBase> Completing=MoveTemp(S->PendingCallbacks[Index]);
        S->PendingCallbacks.RemoveAtSwap(Index);
        if (S->Active) Context->Invoke(*S,*Info);
    }
}
#endif

class FGuLiEosTransport final : public IGuLiHybridTransport
{
public:
    virtual ~FGuLiEosTransport() override { Stop(); }
    virtual void Start(const FGuLiNetworkLaunchConfig& Config, FReady Ready, FFailure Failure) override
    {
        Stop();
        Counters={};
#if WITH_EOS_SDK
        if (Config.ProductId.IsEmpty() || Config.SandboxId.IsEmpty() || Config.DeploymentId.IsEmpty()
            || Config.ClientId.IsEmpty() || Config.ClientSecret.IsEmpty())
        { Failure(TEXT("EOS product environment and GULI_EOS_GAME_CLIENT_SECRET are required")); return; }
        FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("EOSShared"));
        auto* Manager = IEOSSDKManager::Get();
        if (!Manager || !Manager->IsInitialized()) { Failure(TEXT("EOSShared SDK manager is unavailable")); return; }
        State = MakeShared<FEosState>(); State->Config=Config;State->Ready = MoveTemp(Ready); State->Failure = MoveTemp(Failure);
        const FTCHARToUTF8 Product(*Config.ProductId), Sandbox(*Config.SandboxId), Deployment(*Config.DeploymentId),
            Client(*Config.ClientId), Secret(*Config.ClientSecret);
        EOS_Platform_Options Options{}; Options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
        Options.ProductId = Product.Get(); Options.SandboxId = Sandbox.Get(); Options.DeploymentId = Deployment.Get();
        Options.ClientCredentials.ClientId = Client.Get(); Options.ClientCredentials.ClientSecret = Secret.Get();
        Options.bIsServer = EOS_FALSE; Options.Flags = EOS_PF_DISABLE_OVERLAY; Options.TickBudgetInMilliseconds = 1;
        State->Platform = Manager->CreatePlatform(Options);
        if (!State->Platform) { State->Fail(TEXT("EOS platform creation failed")); return; }
        State->Connect = EOS_Platform_GetConnectInterface(*State->Platform); State->P2P = EOS_Platform_GetP2PInterface(*State->Platform);
        State->Lobby=EOS_Platform_GetLobbyInterface(*State->Platform);
        State->Socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
        FCStringAnsi::Strncpy(State->Socket.SocketName, "GuLiHybrid_v1", UE_ARRAY_COUNT(State->Socket.SocketName));
        EOS_P2P_SetRelayControlOptions Relay{}; Relay.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
        Relay.RelayControl = EOS_ERelayControl::EOS_RC_AllowRelays;
        if (EOS_P2P_SetRelayControl(State->P2P, &Relay) != EOS_EResult::EOS_Success)
        { State->Fail(TEXT("EOS relay policy could not be configured")); return; }
        EOS_P2P_SetPacketQueueSizeOptions Queues{}; Queues.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
        Queues.IncomingPacketQueueMaxSizeBytes = 1024 * 1024; Queues.OutgoingPacketQueueMaxSizeBytes = 65536;
        EOS_P2P_SetPacketQueueSize(State->P2P, &Queues);
        EOS_Connect_CreateDeviceIdOptions Device{}; Device.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST; Device.DeviceModel = "GuLiStrike Desktop";
        auto* Context = State->Callback<EOS_Connect_CreateDeviceIdCallbackInfo>([](FEosState& S, const auto& Info)
        {
            if (Info.ResultCode == EOS_EResult::EOS_Success || Info.ResultCode == EOS_EResult::EOS_DuplicateNotAllowed) S.Login();
            else S.Fail(TEXT("EOS anonymous device identity creation failed"));
        });
        EOS_Connect_CreateDeviceId(State->Connect, &Device, Context, &TGuLiEosCallback<EOS_Connect_CreateDeviceIdCallbackInfo>::Complete);
#else
        Failure(TEXT("This platform was built without the EOS SDK"));
#endif
    }
    virtual bool DiscoverRoom(FGuLiTransportRoom& Room) override
    {
#if WITH_EOS_SDK
        return State && State->ReadRoom(Room);
#else
        return false;
#endif
    }
    virtual void SetPeers(const TMap<FGuid, FString>& Peers) override
    {
#if WITH_EOS_SDK
        if (!State) return; State->Peers.Reset();
        for (const auto& Pair : Peers)
            if (const auto User = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*Pair.Value)); EOS_ProductUserId_IsValid(User)) State->Peers.Add(Pair.Key, User);
#endif
    }
    virtual bool Send(const FGuid& Peer, TConstArrayView<uint8> Packet) override
    {
#if WITH_EOS_SDK
        if (!State || !State->Active || !State->LocalUser || Packet.Num() <= 0
            || Packet.Num() > static_cast<int32>(GuLiNet::MaxDatagramBytes)) return false;
        const auto* Remote = State->Peers.Find(Peer); if (!Remote) return false;
        EOS_P2P_SendPacketOptions Options{}; Options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
        Options.LocalUserId = State->LocalUser; Options.RemoteUserId = *Remote; Options.SocketId = &State->Socket;
        Options.Channel = 0; Options.DataLengthBytes = Packet.Num(); Options.Data = Packet.GetData();
        Options.bAllowDelayedDelivery = EOS_TRUE; Options.Reliability = EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
        const bool Success=EOS_P2P_SendPacket(State->P2P,&Options)==EOS_EResult::EOS_Success;
        if (Success) { Counters.SentBytes+=Packet.Num(); ++Counters.SentPackets; }
        else ++Counters.SendFailures;
        return Success;
#else
        return false;
#endif
    }
    virtual void Poll(const FReceive& Receive) override
    {
#if WITH_EOS_SDK
        if (!State || !State->Active || !State->LocalUser) return;
        // EOSShared owns SDK ticking, including when the battle clock is paused.
        uint8 RequestedChannel = 0;
        for (int32 Count = 0; Count < 512; ++Count)
        {
            TArray<uint8> Data; Data.SetNumUninitialized(EOS_P2P_MAX_PACKET_SIZE);
            EOS_P2P_ReceivePacketOptions Options{}; Options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
            Options.LocalUserId = State->LocalUser; Options.MaxDataSizeBytes = Data.Num(); Options.RequestedChannel = &RequestedChannel;
            EOS_ProductUserId Sender = nullptr; EOS_P2P_SocketId Socket{}; Socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
            uint8 Channel = 0; uint32 Size = 0;
            const auto Result = EOS_P2P_ReceivePacket(State->P2P, &Options, &Sender, &Socket, &Channel, Data.GetData(), &Size);
            if (Result == EOS_EResult::EOS_NotFound) break;
            if (Result != EOS_EResult::EOS_Success) { State->Fail(TEXT("EOS receive failed")); break; }
            Counters.ReceivedBytes+=Size; ++Counters.ReceivedPackets;
            if (Size>GuLiNet::MaxDatagramBytes || Channel!=0 || FCStringAnsi::Strcmp(Socket.SocketName,State->Socket.SocketName)!=0)
            { ++Counters.RejectedPackets; continue; }
            Data.SetNum(static_cast<int32>(Size), EAllowShrinking::No);
            for (const auto& Peer : State->Peers) if (Peer.Value == Sender) { Receive(Peer.Key, Data); break; }
        }
#endif
    }
    virtual void Stop() override
    {
#if WITH_EOS_SDK
        if (State) { State->Stop(); State.Reset(); }
#endif
    }
    virtual FGuLiTransportByteCounters GetByteCounters() const override { return Counters; }
private:
    FGuLiTransportByteCounters Counters;
#if WITH_EOS_SDK
    TSharedPtr<FEosState> State;
#endif
};
}
TSharedRef<IGuLiHybridTransport> MakeGuLiEosTransport() { return MakeShared<FGuLiEosTransport>(); }
