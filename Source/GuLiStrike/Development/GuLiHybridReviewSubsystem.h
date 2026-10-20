#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Subsystems/WorldSubsystem.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "GuLiHybridReviewSubsystem.generated.h"

/** Opt-in local acceptance IPC. Helpers provide correctness state, never performance captures. */
UCLASS(Config=Game)
class GULISTRIKE_API UGuLiHybridReviewSubsystem final : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    /** Development evidence from actual authored calls, independent of unit definitions. */
    void ObserveBlueprintExecution(const UObject& Context,FName Function,TOptional<bool> PresentationDisabled={});
    /** Opt-in acceptance preparation only. Exported credits use the ordinary
     * client-owned economy; production profiles contain no supply rules. */
    UPROPERTY(Config) bool bEnableResourceSupply=false;
    /** The opt-in acceptance fixture holds autonomous mining until its input
     * phase. Ordinary matches do not create this subsystem. */
    bool IsAutonomousMiningEnabled() const { return bAutonomousMining && (Phase==TEXT("gameplay") || Phase==TEXT("steady") || Phase==TEXT("capture") || Phase==TEXT("finish_capture")); }
    /** Explicit fixture gate only; defaults open and is never serialized as gameplay. */
    bool IsProductionEnabled(FGuid Object) const { return !bProductionPreparationGate || PreparedProduction.Contains(Object); }
private:
    bool bProductionPreparationGate=false;
    TSet<FGuid> PreparedProduction;
    FString Directory,Run,Phase=TEXT("prepare"),Failure;
    FString Scenario,StageId=TEXT("prepare");
    uint32 StageNumber=0;
    void BeginReviewStage(const FString& Id,uint32 Number);
    struct FSceneAction
    {
        FGuid Id,Player,Request;
        FString Kind,Status=TEXT("queued"),Error;
        TSharedPtr<class FJsonObject> Arguments,Result;
        uint64 SubmittedAt=0,SubmittedEpoch=0;
    };
    TMap<FGuid,FSceneAction> SceneActions;
    TWeakObjectPtr<class UGuLiCommanderSkillComponent> SceneSkills;
    TMap<FGuid,FGuLiActiveSkillReply> SceneSkillReplies;
    TMap<FGuid,uint32> SceneFinalSkillReplyCounts;
    TWeakObjectPtr<UGuLiShipBuildComponent> SceneBuild;
    TMap<FGuid,FGuLiShipChoiceResult> SceneChoiceReplies;
    TMap<FGuid,uint32> SceneChoiceReplyCounts;
    struct FBlueprintExecution { FString Class,Function;uint64 Calls=0,Disabled=0,Enabled=0; };
    TMap<FString,FBlueprintExecution> BlueprintExecutions;
    UFUNCTION() void SceneSkillResolved(const FGuLiActiveSkillReply& Reply);
    UFUNCTION() void SceneChoiceResolved(const FGuLiShipChoiceResult& Reply);
    void ReadSceneActions(const class FJsonObject& Control);
    void AdvanceSceneActions();
    void AppendSceneEvidence(const TSharedRef<class FJsonObject>& Json) const;
    struct FObservedEffect
    {
        uint64 Launched=0,Terminated=0;
        double MinimumSpeed=DBL_MAX,MaximumSpeed=0;
        TMap<uint8,uint64> EndReasons;
    };
    TMap<FString,FObservedEffect> ObservedEffects;
    TSet<FGuid> ObservedLaunches,ObservedTerminations;
    TMap<FGuid,FString> ObservedEffectSources;
    bool bEffectObserversBound=false;
    void ObserveEffect(const struct FGuLiCombatEffectState& State,bool Reliable);
    double NextRead=0,NextWrite=0,Started=0,Finished=0;
    uint64 Samples=0;
    double GroundStarted=-1,GroundMinimumFuel=0;
    FVector GroundStart=FVector::ZeroVector;
    double GroundStartFuel=0;
    bool bGroundThrust=false,bGroundDone=false,bGroundPassed=false;
    uint64 GroundReviewTick=MAX_uint64,GroundReviewEpoch=0;
    double ShipStarted=-1,ShipStartYaw=0,ShipMaximumSpeed=0;
    FVector ShipStart=FVector::ZeroVector;
    uint64 ShipReviewTick=MAX_uint64,ShipReviewEpoch=0;
    bool bShipBoost=false,bShipDone=false,bShipPassed=false;
    bool bStandby=false,bCapture=false,bWritten=false,bAutonomousMining=false;
    int32 RoleExpectedRed=0,RoleExpectedBlue=0;
    bool Write(const TCHAR* Name,const TSharedRef<class FJsonObject>& Json);
    TFuture<bool> StateWrite;
    void ReadControl();
    void AdvanceGroundReview();
    struct FGroundSceneInput { FGuid Player; FVector2D Axis; FVector Aim; bool Sprint=false,Fire=false,Rocket=false; };
    TArray<FGroundSceneInput> GroundSceneInputs;
    struct FShipSceneInput {FGuid Player;FVector Thrust=FVector::ZeroVector;float Turn=0;bool Boost=false;};
    TArray<FShipSceneInput> ShipSceneInputs;
    bool bSceneShipInputApplied=false;
    void ApplyShipSceneInput();
    uint64 SceneGroundInputsAccepted=0,SceneGroundInputsRejected=0;
    bool bSceneGroundInputApplied=false;
    void ApplyGroundSceneInput();
    void AdvanceShipReview();
    FGuid EquipmentRequest;FName EquipmentNode;bool EquipmentDone=false,EquipmentCommitted=false;
    int64 EquipmentBuildRevision=0;
    void AdvanceEquipmentReview();
    void AppendWingmanEvidence(const TSharedRef<class FJsonObject>& Json) const;
    struct FEngineerSceneOrder
    {
        FGuid Player;
        uint32 Command=0;
        FVector Destination=FVector::ZeroVector;
        TArray<uint32> Actors;
        FName Intent=TEXT("Move");
        uint16 Cluster=0;
        bool SelectionSent=false,TaskSent=false,Resolved=false;
        FGuLiCommandAck Ack;
        uint32 Receipts=0;
    };
    TMap<uint32,FEngineerSceneOrder> EngineerOrders;
    TWeakObjectPtr<class UGuLiCommanderNetSyncComponent> EngineerSync;
    void AdvanceEngineerOrders();
    void EngineerCommandResolved(const FGuLiCommandAck& Ack);
    struct FBuildingSceneOrder
    {
        FGuid Player;
        FVector Location=FVector::ZeroVector;
        double Yaw=0;
        int32 DefinitionId=0;
        bool Submitted=false;
        FVector Candidate=FVector::ZeroVector;
    };
    TMap<FString,FBuildingSceneOrder> BuildingOrders;
    void AdvanceBuildingOrders();
    void AppendResourceEvidence(const TSharedRef<class FJsonObject>& Json) const;
};
