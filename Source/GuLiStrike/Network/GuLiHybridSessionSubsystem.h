#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Containers/Ticker.h"
#include "GuLiHybridSessionSubsystem.generated.h"

struct FGuLiHybridMessage
{
    uint16 Kind = 0;
    FGuid Match, Sender, Event, ObjectId;
    uint8 ObjectKind = 0;
    uint32 LifeGeneration = 0;
    uint64 MatchEpoch = 0, OwnerEpoch = 0, ControlEpoch = 0, Sequence = 0, LogicalTick = 0;
    TArray<uint8> Body;
};
struct FGuLiHybridMember
{
    FGuid Player;
    EGuLiTeam Team = EGuLiTeam::Unassigned;
    EGuLiCommanderRole Role = EGuLiCommanderRole::Observer;
    bool bOnline = false, bReady = false;
    uint64 SeatVersion = 1;
    uint32 OrdinalSlot = 0;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiHybridControlReceived, const FGuLiHybridMessage&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiHybridPeerReceived, const FGuid&, const FGuLiHybridMessage&);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiHybridSessionFailure, const FString&);
DECLARE_MULTICAST_DELEGATE(FGuLiHybridRosterChanged);

UCLASS()
class GULISTRIKE_API UGuLiHybridSessionSubsystem final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UGuLiHybridSessionSubsystem();
    virtual ~UGuLiHybridSessionSubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool Start(const FGuLiNetworkLaunchConfig& Config, FString& Error);
    UFUNCTION(BlueprintCallable,Category="GuLi|Network") bool RequestLeave();
    void Stop();
    bool SendControl(FGuLiHybridMessage Message);
    bool SendPeer(const FGuid& Peer, FGuLiHybridMessage Message);
    bool CanQueueHandoff(const FGuid& Peer) const;
    bool BroadcastPeer(const FGuLiHybridMessage& Message);
    bool IsConnected() const;
    bool IsReconnecting() const;
    const FString& GetError() const;
    FGuid GetLocalPlayerId() const;
    FGuid GetRoomId() const;
    uint64 GetMatchEpoch() const;
    const TArray<FGuLiHybridMember>& GetMembers() const;
    TArray<FGuLiHybridMessage> GetUnacknowledged(const FGuid& Peer) const;
    void AccumulateSimulationCost(double Milliseconds,uint32 LogicalFrames);
    bool SetReviewLoadCost(double Milliseconds);
    uint64 GetPeerQueuedBytes(const FGuid& Peer) const;
    TSharedRef<class FJsonObject> GetNetworkCaptureSnapshot() const;
    /** Development review only; requires an explicit LocalUdp acceptance run. */
    bool ArmReviewFault(FGuid Operation,FName Mode,int32 Count=1,uint16 KindFilter=0,int32 BusinessKind=-1);
    bool ReleaseReviewFault(FGuid Operation);
    FGuLiHybridControlReceived OnControl;
    FGuLiHybridPeerReceived OnPeer;
    FGuLiHybridSessionFailure OnFailure;
    FGuLiHybridRosterChanged OnRosterChanged;
private:
    struct FState;
    struct FStateDeleter { void operator()(FState* Value) const; };
    TUniquePtr<FState,FStateDeleter> State;
    FTSTicker::FDelegateHandle Ticker;
    bool NetworkTick(float DeltaSeconds);
    void Fail(const FString& Error);
    void InterruptConnection(const FString& Error);
    void ConnectCoordinator();
    void CloseCoordinator();
};
