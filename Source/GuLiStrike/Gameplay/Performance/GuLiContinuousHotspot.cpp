#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

struct FGuLiContinuousHotspot
{
    FString Directory,Error;
    double Started=0,Duration=0,NextMove=5,NextMining=0,NextCombat=20,NextCheckpoint=300;
    int32 Window=0,CombatWaves=0,MiningAccepted=0,MiningRejected=0;
    uint32 NextCommand=1200000,LastCommand=0;
    bool WindowOpen=false,Complete=false;
    FVector MoveTargets[2];
    EGuLiTeam Team=EGuLiTeam::Unassigned;
    TArray<double> Windows;
    TArray<TSharedPtr<FJsonValue>> Checkpoints,Actions,WindowEvidence;
    TWeakObjectPtr<UGuLiCommanderNetSyncComponent> Sync;
    TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Army;
    FDelegateHandle PositionHandle,AckHandle;
    TMap<FGuLiSoldierId,FVector> Positions;
    TMap<uint32,uint32> AckCounts;
    TSet<uint32> MovedCommands;
    TMap<FGuLiControllableActorId,double> MiningAttempts;
    uint32 AcceptedMoves=0,RejectedMoves=0;
    ~FGuLiContinuousHotspot()
    {
        if (Sync.IsValid()) Sync->OnCommandAckChanged.Remove(AckHandle);
#if !UE_BUILD_SHIPPING
        if (Army.IsValid()) Army->OnPositionApplied.Remove(PositionHandle);
#endif
    }
};

namespace
{
FString EncodeContinuous(const TSharedRef<FJsonObject>& J)
{FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));return Text;}
bool SaveContinuous(const FString& Directory,const TCHAR* Name,const FString& Text)
{return FFileHelper::SaveStringToFile(Text,*(Directory/Name),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
TSharedRef<FJsonObject> ContinuousCheckpoint(UGuLiHybridWorldSubsystem& H,const FGuLiContinuousHotspot& C,double Age)
{
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("age_seconds"),Age);
    J->SetObjectField(TEXT("transactions"),H.GetTransactionCaptureSnapshot());J->SetObjectField(TEXT("simulation_census"),H.GetSimulationCensus());
    J->SetNumberField(TEXT("accepted_moves"),C.AcceptedMoves);J->SetNumberField(TEXT("displaced_commands"),C.MovedCommands.Num());
    // Count actual unit identities retained in the directory, including normal
    // casualties, and prove their parent is a producer simulated on this client.
    // This checkpoint is outside the short timing windows.
    TArray<FGuLiHybridObjectState> Objects;H.GetObjects(Objects);TArray<TSharedPtr<FJsonValue>> Children;
    for (const auto& O:Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && O.Parent.IsValid())
    {
        FGuLiHybridObjectState Parent;
        if (!H.HasSimulation(O.Parent) || !H.GetObjectView(O.Parent,Parent) || !Parent.Definition.ToString().StartsWith(TEXT("building/"))) continue;
        auto Child=MakeShared<FJsonObject>();Child->SetStringField(TEXT("object"),O.Key.StableId.ToString(EGuidFormats::Digits));
        Child->SetNumberField(TEXT("generation"),O.Key.LifeGeneration);Child->SetNumberField(TEXT("life"),O.LifeState);
        Child->SetStringField(TEXT("parent"),O.Parent.StableId.ToString(EGuidFormats::Digits));Child->SetStringField(TEXT("definition"),O.Definition.ToString());
        Children.Add(MakeShared<FJsonValueObject>(Child));
    }
    J->SetArrayField(TEXT("children_of_local_producers"),Children);
    if (auto* R=H.GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>())
    {
        const auto Balance=R->GetTeamInventory(C.Team);auto B=MakeShared<FJsonObject>();
        B->SetNumberField(TEXT("blue"),Balance.Blue);B->SetNumberField(TEXT("red"),Balance.Red);J->SetObjectField(TEXT("team_inventory"),B);
    }
    return J;
}
}

void UGuLiPerformanceSubsystem::Deinitialize()
{
    StopFrameIntervals();
    if (bContinuousHotspot && ContinuousHotspot)
    {
        ContinuousHotspot->Error=TEXT("World ended before the continuous workload completed");
        ContinuousHotspot->Complete=false;
        if (ContinuousHotspot->WindowOpen && GEngine) GEngine->Exec(GetWorld(),TEXT("Trace.Stop"));
        bContinuousHotspot=false;ObservationDeadline=0;bCapturing=false;
        SaveContinuous(ContinuousHotspot->Directory,TEXT("observation.json"),GetLongObservationJson());
    }
    ContinuousHotspot.Reset();Hotspot.Reset();Super::Deinitialize();
}

bool UGuLiPerformanceSubsystem::BeginContinuousHotspot(const FString& OutputDirectory,float Seconds)
{
#if !UE_BUILD_SHIPPING
    if (bCapturing || bHotspotPending || bContinuousHotspot || ObservationDeadline>0
        || !FMath::IsFinite(Seconds) || Seconds<60 || Seconds>2700 || FPaths::IsRelative(OutputDirectory)
        || IFileManager::Get().DirectoryExists(*OutputDirectory)) return false;
    auto* W=GetWorld();auto* PC=W?W->GetFirstPlayerController():nullptr;
    auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* Sync=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
    auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Army=W?W->GetSubsystem<UGuLiBattleAuthoritySubsystem>():nullptr;
    if (!Player || !Player->IsCommander() || !Sync || !Sync->IsSoldierStreamReady() || !H || !H->CanPublishFacts() || !Army) return false;
    auto C=MakeShared<FGuLiContinuousHotspot>();C->Directory=OutputDirectory;C->Duration=Seconds;
    C->Sync=Sync;C->Army=Army;C->Team=Player->GetTeam();
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
    Objects.Sort([](const auto& A,const auto& B){return A.Ordinal<B.Ordinal;});
    TArray<FGuLiSoldierId> Probe;FVector Origin=FVector::ZeroVector;
    for (const auto& O:Objects)
        if (O.Definition==TEXT("unit/1") && O.Team==C->Team && O.LifeState==1 && H->HasSimulation(O.Key) && H->HasControl(O.Key))
        {
            EGuLiTeam Team;uint16 Type=0;FVector Position;
            const FGuLiSoldierId Id(O.Ordinal);
            if (!Army->GetTaskSoldierInfo(Id,Team,Type,Position)) return false;
            Probe.Add(Id);C->Positions.Add(Id,Position);Origin+=Position;if (Probe.Num()==24) break;
        }
    if (Probe.Num()!=24) return false;Origin/=Probe.Num();
    for (int32 I=0;I<2;++I)
        if (!Army->ProjectExternalUnitLocation(Origin+FVector(I?1200:-1200,0,0),C->MoveTargets[I])) return false;
    if (!Sync->PrepareReviewSoldierSelection(Probe,1199999)) return false;
    Sync->SubmitControlGroup(9,true,false,false,false);
    if (!IFileManager::Get().MakeDirectory(*C->Directory,true)) return false;
    const TWeakPtr<FGuLiContinuousHotspot> Weak=C;
    C->PositionHandle=Army->OnPositionApplied.AddLambda([Weak](FGuLiSoldierId Soldier,uint32 Command,const FVector& Position,double)
    {
        if (auto S=Weak.Pin();S && Command>=1200000 && Command<=S->LastCommand)
            if (auto* Previous=S->Positions.Find(Soldier))
            {if (FVector::DistSquared2D(*Previous,Position)>4.) S->MovedCommands.Add(Command);*Previous=Position;}
    });
    C->AckHandle=Sync->OnCommandAckChanged.AddLambda([Weak](const FGuLiCommandAck& Ack)
    {
        if (auto S=Weak.Pin();S && Ack.ClientCommandId>=1200000 && Ack.ClientCommandId<=S->LastCommand)
        {
            if (++S->AckCounts.FindOrAdd(Ack.ClientCommandId)!=1) S->Error=TEXT("Repeated final movement receipt in continuous workload");
            if (Ack.IsAccepted()) ++S->AcceptedMoves;else ++S->RejectedMoves;
        }
    });
    for (double At=30;At+5<Seconds;At+=300) C->Windows.Add(At);
    if (C->Windows.IsEmpty() || Seconds-10-C->Windows.Last()>20) C->Windows.Add(Seconds-10);
    C->Started=FPlatformTime::Seconds();ContinuousHotspot=C;bContinuousHotspot=true;
    if (!BeginLongObservation(Seconds)) {bContinuousHotspot=false;ContinuousHotspot.Reset();return false;}
    // The schedule and its proof must share the exact start boundary; ending
    // against an earlier clock must never truncate the requested observation.
    C->Started=ObservationStarted;
    C->Checkpoints.Add(MakeShared<FJsonValueObject>(ContinuousCheckpoint(*H,*C,0)));
    return true;
#else
    return false;
#endif
}

void UGuLiPerformanceSubsystem::AppendContinuousHotspot(const TSharedRef<FJsonObject>& Root) const
{
    if (!ContinuousHotspot) return;const auto& C=*ContinuousHotspot;auto J=MakeShared<FJsonObject>();
    J->SetStringField(TEXT("schema"),TEXT("hybrid-continuous-business/v1"));
    J->SetBoolField(TEXT("running"),bContinuousHotspot);J->SetBoolField(TEXT("complete"),C.Complete);
    J->SetStringField(TEXT("error"),C.Error);J->SetNumberField(TEXT("requested_seconds"),C.Duration);
    J->SetNumberField(TEXT("move_period_seconds"),20);J->SetNumberField(TEXT("combat_period_seconds"),300);
    J->SetNumberField(TEXT("combat_units_per_wave"),24);J->SetNumberField(TEXT("checkpoint_period_seconds"),300);
    J->SetNumberField(TEXT("accepted_moves"),C.AcceptedMoves);J->SetNumberField(TEXT("rejected_moves"),C.RejectedMoves);
    J->SetNumberField(TEXT("displaced_commands"),C.MovedCommands.Num());J->SetNumberField(TEXT("combat_waves"),C.CombatWaves);
    J->SetNumberField(TEXT("mining_orders_accepted"),C.MiningAccepted);J->SetNumberField(TEXT("mining_orders_rejected"),C.MiningRejected);
    J->SetArrayField(TEXT("checkpoints"),C.Checkpoints);J->SetArrayField(TEXT("actions"),C.Actions);
    J->SetArrayField(TEXT("windows"),C.WindowEvidence);
    J->SetStringField(TEXT("activity_basis"),TEXT("Real committed transaction deltas and actual command displacement; attempted inputs alone never establish sustained business."));
    Root->SetObjectField(TEXT("continuous_workload"),J);
}

void UGuLiPerformanceSubsystem::TickContinuousHotspot()
{
#if !UE_BUILD_SHIPPING
    if (!bContinuousHotspot || !ContinuousHotspot) return;
    auto C=ContinuousHotspot;const double Age=FPlatformTime::Seconds()-C->Started;
    auto* W=GetWorld();auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (!H || !H->CanPublishFacts() || !C->Sync.IsValid() || !C->Army.IsValid()) C->Error=TEXT("Continuous workload lost its ready world or input owner");
    if (C->WindowOpen && (!bCapturing || !C->Error.IsEmpty()))
    {
        if (bCapturing) EndCapture();if (GEngine) GEngine->Exec(W,TEXT("Trace.Stop"));
        const FString Folder=C->Directory/FString::Printf(TEXT("window-%02d"),C->Window);
        if (!SaveContinuous(Folder,TEXT("profile.json"),GetCaptureJson())) C->Error=TEXT("Cannot save native capture");
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("directory"),FString::Printf(TEXT("window-%02d"),C->Window));
        J->SetNumberField(TEXT("age_seconds"),Age);J->SetBoolField(TEXT("capture_valid"),IsCaptureValid());
        C->WindowEvidence.Add(MakeShared<FJsonValueObject>(J));C->WindowOpen=false;++C->Window;
    }
    if (bCapturing) return;
    if (H && (Age>=C->NextCheckpoint || Age>=C->Duration || !C->Error.IsEmpty()))
    {
        C->Checkpoints.Add(MakeShared<FJsonValueObject>(ContinuousCheckpoint(*H,*C,Age)));C->NextCheckpoint+=300;
        auto Progress=MakeShared<FJsonObject>();AppendContinuousHotspot(Progress);
        if (!SaveContinuous(C->Directory,TEXT("progress.json"),EncodeContinuous(Progress))) C->Error=TEXT("Cannot save continuous checkpoint");
    }
    if (Age>=C->Duration || !C->Error.IsEmpty())
    {
        if (ObservationDeadline>0) {CaptureNetworkSample(true);ObservationDeadline=0;}
        bContinuousHotspot=false;C->Complete=Age>=C->Duration && C->Error.IsEmpty();
        if (!SaveContinuous(C->Directory,TEXT("observation.json"),GetLongObservationJson()))
        {C->Complete=false;C->Error=TEXT("Cannot save continuous observation");}
        return;
    }
    if (Age>=C->NextMove)
    {
        C->Sync->SubmitControlGroup(9,false,false,false,false);
        FGuLiUnitTaskCommand Command;Command.CommandId=C->NextCommand++;C->LastCommand=Command.CommandId;
        Command.Target=C->MoveTargets[(Command.CommandId-1200000)%2];Command.TargetIntent=TEXT("Move");Command.bGroundMoveOnly=true;
        C->Sync->SubmitOrderedTask(Command);C->NextMove+=20;
    }
    if (Age>=C->NextMining)
    {
        auto* Resources=W->GetSubsystem<UGuLiResourceWorldSubsystem>();const auto* Map=Resources?Resources->GetMapDefinition():nullptr;
        if (!Resources || !Map) {C->Error=TEXT("Continuous mining has no resource map");return;}
        TArray<AGuLiMiningVehiclePawn*> Miners;
        for (TActorIterator<AGuLiMiningVehiclePawn> It(W);It;++It)
            if (const auto* Identity=UGuLiHybridActorComponent::Find(*It);Identity && Identity->CanSimulate() && Identity->CanControl() && It->GetTeam()==C->Team)
                Miners.Add(*It);
        Miners.Sort([](const auto& A,const auto& B){return A.GetStableActorId().Value<B.GetStableActorId().Value;});
        for (int32 I=0;I<Miners.Num();++I)
        {
            auto* Miner=Miners[I];const auto Cargo=Miner->GetCargo();const auto Id=Miner->GetStableActorId();
            if (Cargo.Blue+Cargo.Red>0 || (C->MiningAttempts.Contains(Id)&&Age-C->MiningAttempts[Id]<30)) continue;
            const auto Color=(I%2)?EGuLiResourceType::Red:EGuLiResourceType::Blue;
            const auto* Current=Map->FindCluster(uint16(Miner->GetTargetClusterId()));
            if (Current && Current->ResourceType==Color && !Resources->IsClusterEmpty(Current->ClusterId) && !Miner->IsManagedTaskComplete()) continue;
            const FGuLiResourceClusterDefinition* Chosen=nullptr;double Distance=DBL_MAX;
            for (const auto& Cluster:Map->Clusters)
                if (Cluster.ResourceType==Color && !Resources->IsClusterEmpty(Cluster.ClusterId) && Resources->CanTeamMineAt(C->Team,Cluster.ClusterId))
                {
                    const double D=FVector::DistSquared2D(Miner->GetActorLocation(),Cluster.Center);
                    if (D<Distance) {Chosen=&Cluster;Distance=D;}
                }
            if (!Chosen) continue;
            FGuLiMiningCommand Command;Command.RequestId=1300000+uint32(Age)*32+uint32(I);Command.SelectionRevision=1;
            Command.Type=EGuLiMiningOrderType::MineCluster;Command.ClusterId=Chosen->ClusterId;Command.Target=Chosen->Center;
            const bool Accepted=Miner->IssuePlayerCommand(Command,C->Team);C->MiningAttempts.Add(Id,Age);
            if (Accepted) ++C->MiningAccepted;else ++C->MiningRejected;
        }
        C->NextMining=Age+2;
    }
    if (Age>=C->NextCombat)
    {
        const FString Prepared=PrepareHotspotCombat(24,1,24);TSharedPtr<FJsonObject> J;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Prepared),J) || !J)
        {C->Error=TEXT("Continuous combat returned invalid preparation");return;}
        J->SetNumberField(TEXT("age_seconds"),Age);C->Actions.Add(MakeShared<FJsonValueObject>(J));
        if (J->HasField(TEXT("error"))) {C->Error=TEXT("Continuous combat cannot prepare a real live wave: ")+J->GetStringField(TEXT("error"));return;}
        ++C->CombatWaves;C->NextCombat+=300;
    }
    if (C->Window<C->Windows.Num() && Age>=C->Windows[C->Window])
    {
        const FString Folder=C->Directory/FString::Printf(TEXT("window-%02d"),C->Window);
        if (!IFileManager::Get().MakeDirectory(*Folder,true) || !GEngine) {C->Error=TEXT("Cannot create native trace window");return;}
        GEngine->Exec(W,*FString::Printf(TEXT("Trace.File \"%s\" cpu,frame,bookmark"),*(Folder/TEXT("session.utrace"))));
        if (!BeginTimedCapture(5)) {GEngine->Exec(W,TEXT("Trace.Stop"));C->Error=TEXT("Cannot start native trace window");return;}
        C->WindowOpen=true;
    }
#endif
}
