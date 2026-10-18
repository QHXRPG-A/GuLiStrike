#include "Development/GuLiHybridReviewSubsystem.h"
#include "Development/GuLiHybridNetworkReviewActor.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Gameplay/Cards/GuLiRogueCardPresentation.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiMiningSlots.h"
#include "Components/CapsuleComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Gameplay/Resources/GuLiResourceInventorySubsystem.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include <cfloat>

namespace
{
#if !UE_BUILD_SHIPPING
bool AppendResourceSupply(UWorld&,FGuLiHybridManifestData& M,FString& Error)
{
    if (!GetDefault<UGuLiHybridReviewSubsystem>()->bEnableResourceSupply) return true;
    const FString Definition=M.Strings.FindRef(TEXT("army/root_definition"));
    if (Definition.IsEmpty()) {Error=TEXT("Acceptance supply requires a registered team account source");return false;}
    M.Numbers.Add(TEXT("acceptance/resource_supply"),1);
    // Binary denominations permit exact top-ups without inflating the next
    // insufficient-resource or unpaid-supply fixture. No wire extension.
    for (const FString Color:{TEXT("blue"),TEXT("red")})
        for (int32 Amount=1;Amount<=1024;Amount*=2)
        {
            auto& R=M.Rules.AddDefaulted_GetRef();
            R.Id=FString::Printf(TEXT("acceptance/resource_supply/%s/%d"),*Color,Amount);
            R.SourceDefinition=Definition;R.Kind=EGuLiHybridRuleKind::Credit;
            if (Color==TEXT("blue")) R.Blue=Amount;else R.Red=Amount;
        }
    return true;
}
const FGuLiHybridManifestRegistration ResourceSupplyProvider(
    {TEXT("AcceptanceResourceSupply"),EGuLiHybridManifestPhase::Derived,{TEXT("ArmyState")},AppendResourceSupply});
#endif
TArray<TSharedPtr<FJsonValue>> Point(const FVector& P)
{return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};}
bool ReadPoint(const FJsonObject& J,const TCHAR* Name,FVector& P)
{
    const TArray<TSharedPtr<FJsonValue>>* V=nullptr;
    if (!J.TryGetArrayField(Name,V) || V->Num()!=3) return false;
    return (*V)[0]->TryGetNumber(P.X) && (*V)[1]->TryGetNumber(P.Y) && (*V)[2]->TryGetNumber(P.Z) && !P.ContainsNaN();
}
FString Encode(const FJsonObject& J)
{FString S;FJsonSerializer::Serialize(MakeShared<FJsonObject>(J),TJsonWriterFactory<>::Create(&S));return S;}
bool QueryPosition(UWorld& World,UGuLiHybridWorldSubsystem& H,const FGuLiHybridObjectState& O,FVector& P)
{
    if (const auto* Actor=H.FindBoundActor(O.Key)) {P=Actor->GetActorLocation();return true;}
    if (O.Key.Kind!=EGuLiHybridObjectKind::Soldier) return false;
    auto* A=World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();EGuLiTeam Team;uint16 Type;
    return A && A->GetTaskSoldierInfo(FGuLiSoldierId(O.Ordinal),Team,Type,P);
}
#if !UE_BUILD_SHIPPING
bool PreparePosition(UWorld& World,UGuLiHybridWorldSubsystem& H,const FGuLiHybridObjectState& O,
    const FVector& Requested,const FGuid& Token,bool Locked,bool FindNearby=false)
{
    if (O.Key.Kind==EGuLiHybridObjectKind::Ship)
    {
        auto* Ship=Cast<AGuLiStrikeShip>(H.FindBoundActor(O.Key));
        auto* Movement=Ship?Ship->GetShipMovement():nullptr;
        if (Locked || !Ship || !Ship->HasAuthority() || !Movement) return false;
        // Reuse the production displacement path, including velocity reset,
        // prediction barrier and canonical-history invalidation. No vitals,
        // member lives, ownership or ability receipts are changed.
        Ship->SubmitGameplayFlightInput(FVector::ZeroVector,0,false);
        Movement->ApplyExternalDisplacement(FTransform(Ship->GetActorRotation(),Requested));
        return Ship->GetActorLocation().Equals(Requested,1.0);
    }
    if (!Locked && O.Key.Kind==EGuLiHybridObjectKind::Miner)
    {
        auto* Miner=Cast<AGuLiMiningVehiclePawn>(H.FindBoundActor(O.Key));FTransform WorkPose;
        if (Miner && Miner->HasAuthority() && Miner->GetReservedMiningPose(WorkPose)
            && FVector::DistSquared2D(Requested,WorkPose.GetLocation())<=1.)
        {
            auto* Travel=Miner->FindComponentByClass<UGuLiEngineeringTravelComponent>();
            if (!Travel || Travel->IsRouting()) return false;
            // The reservation already supplied the legal vehicle pose. Rebase
            // the existing work path after teleport instead of leaving the old
            // path-following segment active. Ore, task, lease and cargo remain real.
            WorkPose.SetScale3D(Miner->GetActorScale3D());
            Miner->GetCharacterMovement()->StopMovementImmediately();
            if (!Miner->SetActorTransform(WorkPose,false,nullptr,ETeleportType::TeleportPhysics)) return false;
            return Travel->BeginWorkMove(WorkPose.GetLocation(),GULI_MINING_SLOT_ARRIVAL_RADIUS_CM);
        }
    }
    auto* Army=World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();FVector Surface;
    if (!Army) return false;
    if (!Army->ProjectExternalUnitLocation(Requested,Surface))
    {
        if (!FindNearby) return false;
        bool Found=false;
        // Development preparation only: an earlier avoidance position can
        // lie just outside the navigation polygon. Never change play rules.
        for (const double Radius:{100.,200.,400.,800.})
        {
            for (int32 I=0;I<8 && !Found;++I)
            {
                const double Angle=I*UE_DOUBLE_PI/4;
                Found=Army->ProjectExternalUnitLocation(Requested+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0),Surface);
            }
            if (Found) break;
        }
        if (!Found) return false;
    }
    if (O.Key.Kind==EGuLiHybridObjectKind::Soldier)
    {
        FGuLiMassExternalUnit Unit;Unit.Id=FGuLiSoldierId(O.Ordinal);
        Unit.Transform=FTransform(FRotator(0,O.FacingYawDegrees,0),Surface);
        return Army->ApplyExternalUnitState(TArray<FGuLiMassExternalUnit>{Unit},Token,false,Locked,true);
    }
    if (Locked) return false;
    if (O.Key.Kind==EGuLiHybridObjectKind::GroundPlayer)
    {
        auto* Ground=Cast<AGuLiGroundMechCharacter>(H.FindBoundActor(O.Key));
        auto* Movement=Ground?Cast<UGuLiExternalCharacterMovementComponent>(Ground->GetCharacterMovement()):nullptr;
        if (!Movement || !Ground->HasAuthority()) return false;
        Surface.Z+=Ground->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5;
        Movement->ApplyExternalDisplacement(FTransform(Ground->GetActorRotation(),Surface));return true;
    }
    if (O.Key.Kind==EGuLiHybridObjectKind::Engineer || O.Key.Kind==EGuLiHybridObjectKind::Miner)
    {
        auto* Character=Cast<ACharacter>(H.FindBoundActor(O.Key));
        if (!Character || !Character->HasAuthority() || !Character->GetCharacterMovement()) return false;
        Surface.Z+=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5;
        Character->GetCharacterMovement()->StopMovementImmediately();
        return Character->SetActorLocation(Surface,false,nullptr,ETeleportType::TeleportPhysics);
    }
    return false;
}
#endif
}

void UGuLiHybridReviewSubsystem::BeginReviewStage(const FString& Id,uint32 Number)
{
    // Only test-driver state is reset. Entities, task queues, callbacks and
    // outstanding ledger work remain governed by the production lifecycle.
    if (auto* PC=GetWorld()->GetFirstPlayerController())
    {
        if (auto* G=Cast<AGuLiGroundMechCharacter>(PC->GetPawn()))
            G->SubmitGameplayInput(FVector2D::ZeroVector,G->GetActorLocation()+FVector(1000,0,0),false,false,false);
        if (auto* S=Cast<AGuLiStrikeShip>(PC->GetPawn())) S->SubmitGameplayFlightInput(FVector::ZeroVector,0,false);
    }
    StageId=Id;StageNumber=Number;NextWrite=0;
    if (!bEffectObserversBound)
    {
        if (auto* Pool=GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>()) Pool->OnState.AddUObject(this,&ThisClass::ObserveEffect);
        if (auto* Effects=GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>()) Effects->OnState.AddUObject(this,&ThisClass::ObserveEffect);
        bEffectObserversBound=true;
    }
    GroundStarted=ShipStarted=-1;GroundReviewTick=ShipReviewTick=MAX_uint64;
    GroundReviewEpoch=ShipReviewEpoch=0;ShipMaximumSpeed=0;
    bGroundThrust=bGroundDone=bGroundPassed=false;
    bShipBoost=bShipDone=bShipPassed=false;
    bAutonomousMining=false;
    GroundSceneInputs.Reset();bSceneGroundInputApplied=false;
    ShipSceneInputs.Reset();bSceneShipInputApplied=false;
    EquipmentRequest.Invalidate();EquipmentNode=NAME_None;EquipmentDone=EquipmentCommitted=false;
    EngineerOrders.Reset();BuildingOrders.Reset();SceneActions.Reset();SceneSkillReplies.Reset();
    SceneFinalSkillReplyCounts.Reset();SceneChoiceReplies.Reset();SceneChoiceReplyCounts.Reset();
}

void UGuLiHybridReviewSubsystem::SceneSkillResolved(const FGuLiActiveSkillReply& Reply)
{
    if (!Reply.RequestId.IsValid()) return;
    if (!SceneSkillReplies.Contains(Reply.RequestId) && SceneSkillReplies.Num()>=512)
    {Failure=TEXT("Skill receipt evidence capacity exceeded");return;}
    SceneSkillReplies.Add(Reply.RequestId,Reply);
    if (Reply.Code!=EGuLiActiveSkillResultCode::PendingCommit && !Reply.Global.bPendingCommit)
        ++SceneFinalSkillReplyCounts.FindOrAdd(Reply.RequestId);
}
void UGuLiHybridReviewSubsystem::SceneChoiceResolved(const FGuLiShipChoiceResult& Reply)
{
    if (!Reply.RequestId.IsValid()) return;
    if (!SceneChoiceReplies.Contains(Reply.RequestId) && SceneChoiceReplies.Num()>=512)
    {Failure=TEXT("Choice receipt evidence capacity exceeded");return;}
    SceneChoiceReplies.Add(Reply.RequestId,Reply);++SceneChoiceReplyCounts.FindOrAdd(Reply.RequestId);
}

void UGuLiHybridReviewSubsystem::ObserveEffect(const FGuLiCombatEffectState& S,bool Reliable)
{
    if (!Reliable || S.bAcceptanceAdditional || Scenario.IsEmpty() || Scenario==TEXT("performance")) return;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H) return;
    FString Key;FGuLiHybridObjectKey Source;FGuLiHybridObjectState Object;
    if (H->ResolveTarget(S.Source,Source) && H->GetObject(Source,Object))
    {
        if (!H->HasSimulation(Source,true)) return;
        Key=Object.Definition.ToString()+TEXT("/")+S.SlotId.ToString();
    }
    else if (const auto* Frozen=ObservedEffectSources.Find(S.EffectId)) Key=*Frozen;
    else return;
    if (ObservedLaunches.Num()+ObservedTerminations.Num()>=262144) {Failure=TEXT("Gameplay effect evidence overflow");return;}
    auto& Count=ObservedEffects.FindOrAdd(Key);
    if (S.Phase==EGuLiCombatEffectPhase::Active && !ObservedLaunches.Contains(S.EffectId))
    {
        ObservedLaunches.Add(S.EffectId);ObservedEffectSources.Add(S.EffectId,Key);++Count.Launched;
        Count.MinimumSpeed=FMath::Min(Count.MinimumSpeed,double(S.Velocity.Size()));Count.MaximumSpeed=FMath::Max(Count.MaximumSpeed,double(S.Velocity.Size()));
    }
    else if (S.Phase!=EGuLiCombatEffectPhase::Active && !ObservedTerminations.Contains(S.EffectId))
    {ObservedTerminations.Add(S.EffectId);++Count.Terminated;++Count.EndReasons.FindOrAdd(uint8(S.EndReason));}
}

void UGuLiHybridReviewSubsystem::ReadSceneActions(const FJsonObject& Control)
{
    ShipSceneInputs.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Inputs=nullptr;
    if (Control.TryGetArrayField(TEXT("ship_inputs"),Inputs))
    {
        if (Inputs->Num()>6) {Failure=TEXT("Too many Ship inputs");return;}
        for (const auto& V:*Inputs)
        {
            const auto J=V->AsObject();FShipSceneInput I;FString Player;double Turn=0;
            if (!J || !J->TryGetStringField(TEXT("player"),Player) || !FGuid::Parse(Player,I.Player)
                || !ReadPoint(*J,TEXT("thrust"),I.Thrust) || I.Thrust.GetAbsMax()>1 || !J->TryGetNumberField(TEXT("turn"),Turn)
                || !FMath::IsFinite(Turn) || FMath::Abs(Turn)>1 || !J->TryGetBoolField(TEXT("boost"),I.Boost))
            {Failure=TEXT("Malformed Ship semantic input");return;}
            I.Turn=float(Turn);ShipSceneInputs.Add(I);
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if (!Control.TryGetArrayField(TEXT("actions"),Values)) return;
    if (Values->Num()>16) {Failure=TEXT("Too many review input actions");return;}
    for (const auto& V:*Values)
    {
        const auto J=V->AsObject();FSceneAction A;FString Id,Player;
        if (!J || !J->TryGetStringField(TEXT("id"),Id) || !FGuid::Parse(Id,A.Id) || !A.Id.IsValid()
            || !J->TryGetStringField(TEXT("player"),Player) || !FGuid::Parse(Player,A.Player) || !A.Player.IsValid()
            || !J->TryGetStringField(TEXT("kind"),A.Kind) || A.Kind.IsEmpty() || A.Kind.Len()>64)
        {Failure=TEXT("Malformed review input action");return;}
        if (const auto* Old=SceneActions.Find(A.Id))
        {if (Encode(*Old->Arguments)!=Encode(*J)) Failure=TEXT("Review operation identity changed arguments");continue;}
        if (SceneActions.Num()>=128) {Failure=TEXT("Review operation capacity exceeded");return;}
        A.Arguments=J;A.Result=MakeShared<FJsonObject>();SceneActions.Add(A.Id,MoveTemp(A));
    }
}

void UGuLiHybridReviewSubsystem::ApplyShipSceneInput()
{
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H || H->IsBattlePaused()) return;
    auto* PC=GetWorld()->GetFirstPlayerController();const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* Ship=PC?Cast<AGuLiStrikeShip>(PC->GetPawn()):nullptr;if (!Ship || !PS) return;
    const auto* I=Phase==TEXT("gameplay")?ShipSceneInputs.FindByPredicate([&](const auto& V){return V.Player==PS->GetPlayerGuid();}):nullptr;
    if (!I && !bSceneShipInputApplied) return;
    if (!Ship->SubmitGameplayFlightInput(I?I->Thrust:FVector::ZeroVector,I?I->Turn:0,I && I->Boost))
        Failure=TEXT("Ship semantic input was rejected");
    bSceneShipInputApplied=I!=nullptr;
}

void UGuLiHybridReviewSubsystem::AdvanceSceneActions()
{
    if (bStandby || Phase!=TEXT("gameplay")) return;
    auto* PC=GetWorld()->GetFirstPlayerController();auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!PS || !H) return;
    // Fault release is a wall-clock network operation and must work while battle advancement is stopped.
    if (auto* Session=GetWorld()->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>())
    for (auto& [Id,A]:SceneActions)
    {
        if (A.Player==PS->GetPlayerGuid() && A.Kind==TEXT("leave_match") && (A.Status==TEXT("queued") || A.Status==TEXT("submitted")))
        {
            if (A.Status==TEXT("queued"))
            {
                if (!Session->RequestLeave()) {A.Status=TEXT("rejected");A.Error=TEXT("Formal leave could not reach the control link");continue;}
                A.Status=TEXT("submitted");
            }
            const auto* LocalMember=Session->GetMembers().FindByPredicate([&](const auto& M){return M.Player==A.Player;});
            if (!LocalMember || LocalMember->bOnline || H->IsBattlePaused()) continue;
            auto Receipt=MakeShared<FJsonObject>();
            Receipt->SetStringField(TEXT("id"),Id.ToString(EGuidFormats::Digits));
            Receipt->SetStringField(TEXT("player"),A.Player.ToString(EGuidFormats::Digits));
            Receipt->SetStringField(TEXT("kind"),A.Kind);Receipt->SetStringField(TEXT("status"),TEXT("completed"));
            Receipt->SetStringField(TEXT("stage_id"),StageId);Receipt->SetStringField(TEXT("scenario"),Scenario);
            Receipt->SetBoolField(TEXT("departure_committed"),true);
            const FString File=TEXT("leave-")+Id.ToString(EGuidFormats::Digits).ToLower()+TEXT(".json");
            if (!Write(*File,Receipt)) {A.Status=TEXT("rejected");A.Error=TEXT("Formal quit receipt could not be persisted");continue;}
            A.Status=TEXT("completed");NextWrite=0;PC->ConsoleCommand(TEXT("quit"),true);continue;
        }
        if (A.Player!=PS->GetPlayerGuid() || A.Status!=TEXT("queued") || !A.Kind.StartsWith(TEXT("fault_"))) continue;
        bool Accepted=false;
        if (A.Kind==TEXT("fault_load_report") && (Scenario==TEXT("gameplay_merged") || Scenario.StartsWith(TEXT("fault_"))))
        {double Cost;if (A.Arguments->TryGetNumberField(TEXT("milliseconds"),Cost)) Accepted=Session->SetReviewLoadCost(Cost);}
        if (Scenario.StartsWith(TEXT("fault_")))
        {
            FString Mode,Operation;double Count=1,Kind=0,Business=-1,TargetKind=0;
            if (A.Kind==TEXT("fault_simulation_rate"))
            {double Rate;if (A.Arguments->TryGetNumberField(TEXT("rate"),Rate)) Accepted=H->SetReviewSimulationRate(Rate);}
            if (A.Kind==TEXT("fault_arm") && A.Arguments->TryGetStringField(TEXT("mode"),Mode))
            {
                A.Arguments->TryGetNumberField(TEXT("count"),Count);A.Arguments->TryGetNumberField(TEXT("message_kind"),Kind);
                A.Arguments->TryGetNumberField(TEXT("business_kind"),Business);
                A.Arguments->TryGetNumberField(TEXT("target_kind"),TargetKind);
                if (Count==FMath::FloorToDouble(Count) && Count>=1 && Count<=256 && Kind==FMath::FloorToDouble(Kind) && Kind>=0 && Kind<=MAX_uint16
                    && Business==FMath::FloorToDouble(Business) && Business>=-1 && Business<=MAX_int32
                    && TargetKind==FMath::FloorToDouble(TargetKind) && TargetKind>=0 && TargetKind<=uint8(EGuLiHybridObjectKind::BusinessWork))
                {
                    FString SourceText,PriorText;FGuid Source,Prior;double PriorEpoch=0;
                    const bool ValidSource=!A.Arguments->TryGetStringField(TEXT("source"),SourceText) || FGuid::Parse(SourceText,Source);
                    const bool HasPrior=A.Arguments->TryGetStringField(TEXT("prior_match"),PriorText);
                    const bool ValidPrior=!HasPrior || (FGuid::Parse(PriorText,Prior) && Prior.IsValid()
                        && A.Arguments->TryGetNumberField(TEXT("prior_epoch"),PriorEpoch) && PriorEpoch>=1 && PriorEpoch<=MAX_uint32 && PriorEpoch==FMath::FloorToDouble(PriorEpoch));
                    if (ValidSource && ValidPrior) Accepted=A.Error==TEXT("Recorded business replay pending")
                        || Session->ArmReviewFault(Id,FName(Mode),int32(Count),uint16(Kind),int32(Business),Source,uint8(TargetKind),Prior,uint64(PriorEpoch));
                    FString ReplayText;
                    if (Accepted && A.Arguments->TryGetStringField(TEXT("replay_event"),ReplayText))
                    {
                        FGuid Replay;
                        Accepted=Mode==TEXT("business_duplicate") && FGuid::Parse(ReplayText,Replay)
                            && H->ReplayReviewBusinessRequest(Replay,int32(Count),A.Result,A.Error);
                        if (!Accepted && A.Error==TEXT("Recorded business replay pending")) continue;
                        Session->ReleaseReviewFault(Id);
                    }
                }
            }
            if (A.Kind==TEXT("fault_release") && A.Arguments->TryGetStringField(TEXT("operation"),Operation))
            {FGuid F;if (FGuid::Parse(Operation,F)) Accepted=Session->ReleaseReviewFault(F);}
        }
        A.Status=Accepted?TEXT("completed"):TEXT("rejected");if (!Accepted && A.Error.IsEmpty()) A.Error=TEXT("Review network fault operation denied");
    }
    if (!H->IsWorldReady() || H->IsBattlePaused() || !H->CanPublishFacts()) return;
    auto* Sync=PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>();auto* Skills=PS->GetCommanderSkills();
    if (SceneSkills.Get()!=Skills)
    {
        if (SceneSkills.IsValid()) SceneSkills->OnSkillReply.RemoveDynamic(this,&ThisClass::SceneSkillResolved);
        SceneSkills=Skills;
        if (Skills) Skills->OnSkillReply.AddUniqueDynamic(this,&ThisClass::SceneSkillResolved);
    }
    auto* Teleport=PC->FindComponentByClass<UGuLiTeleportInputComponent>();
    auto* Build=PS->FindComponentByClass<UGuLiShipBuildComponent>();
    if (SceneBuild.Get()!=Build)
    {
        if (SceneBuild.IsValid()) SceneBuild->OnChoiceResolved.RemoveDynamic(this,&ThisClass::SceneChoiceResolved);
        SceneBuild=Build;if (Build) Build->OnChoiceResolved.AddUniqueDynamic(this,&ThisClass::SceneChoiceResolved);
    }
    auto* Cards=PC->FindComponentByClass<UGuLiRogueCardPresentation>();
    for (auto& [Id,A]:SceneActions)
    {
        if (A.Player!=PS->GetPlayerGuid() || A.Status==TEXT("completed") || A.Status==TEXT("rejected")) continue;
        auto Reject=[&](const FString& Why){A.Status=TEXT("rejected");A.Error=Why;};
        if (A.Status==TEXT("queued"))
        {
            A.SubmittedAt=H->GetLogicalTick();A.SubmittedEpoch=GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
            FVector P;const bool HasPoint=ReadPoint(*A.Arguments,TEXT("point"),P);
            if (A.Kind==TEXT("fixture_construction_progress"))
            {
#if !UE_BUILD_SHIPPING
                const auto* Actors=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();FString Text;FGuid ObjectId;
                if (!bEnableResourceSupply || !Actors || (Scenario!=TEXT("gameplay_merged") && !Scenario.StartsWith(TEXT("fault_")))
                    || Actors->GetManifest().Numbers.FindRef(TEXT("acceptance/resource_supply"))!=1
                    || !A.Arguments->TryGetStringField(TEXT("object"),Text) || !FGuid::Parse(Text,ObjectId))
                {Reject(TEXT("Construction work preparation requires the explicit development profile"));continue;}
                TArray<FGuLiHybridObjectState> All;H->GetObjects(All);
                const auto* O=All.FindByPredicate([&](const auto& V){return V.Key.StableId==ObjectId && V.LifeState==1 && H->HasSimulation(V.Key);});
                auto* Actor=O?H->FindBoundActor(O->Key):nullptr;
                auto* Life=Actor?Actor->FindComponentByClass<UGuLiBuildingLifecycleComponent>():nullptr;
                if (!O || O->Key.Kind!=EGuLiHybridObjectKind::Building || !Life || Life->IsCompleted()
                    || Life->GetDefinition().ConstructionWork<=0)
                {Reject(TEXT("Construction work preparation requires a local unfinished building"));continue;}
                const float Before=Life->GetState().WorkDone,Required=Life->GetDefinition().ConstructionWork;
                Life->AddConstructionWork(FMath::Max(0.f,Required-Before));
                A.Result->SetStringField(TEXT("object"),Text);A.Result->SetNumberField(TEXT("before"),Before);
                A.Result->SetNumberField(TEXT("applied"),Life->GetState().WorkDone);A.Result->SetNumberField(TEXT("required"),Required);
                A.Result->SetStringField(TEXT("scope"),TEXT("Construction work is supplied as preparation. Committed completion, replacement and population transfer use normal gameplay. This is not construction-duration evidence."));
                A.Status=TEXT("completed");
#else
                Reject(TEXT("Development work preparation is unavailable"));continue;
#endif
            }
            else if (A.Kind==TEXT("fixture_positions"))
            {
#if !UE_BUILD_SHIPPING
                const auto* Actors=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
                const TArray<TSharedPtr<FJsonValue>>* Positions=nullptr;
                if (!bEnableResourceSupply || !Actors || (Scenario!=TEXT("gameplay_merged") && !Scenario.StartsWith(TEXT("fault_")))
                    || Actors->GetManifest().Numbers.FindRef(TEXT("acceptance/resource_supply"))!=1
                    || !A.Arguments->TryGetArrayField(TEXT("positions"),Positions) || Positions->IsEmpty() || Positions->Num()>64)
                {Reject(TEXT("Batch positions require the explicit development profile and 1..64 stable objects"));continue;}
                TArray<FGuLiHybridObjectState> All;H->GetObjects(All);TArray<TSharedPtr<FJsonValue>> Applied;
                bool FindNearby=false;A.Arguments->TryGetBoolField(TEXT("find_nearby"),FindNearby);
                bool AllowTerminal=false;A.Arguments->TryGetBoolField(TEXT("allow_terminal"),AllowTerminal);
                TSet<FGuid> Seen;bool Valid=true;
                for (const auto& Value:*Positions)
                {
                    const TSharedPtr<FJsonObject>* J=nullptr;FString Text;FGuid ObjectId,Token=Id;FVector Requested,Before,After;bool Locked=false;
                    if (!Value->TryGetObject(J) || !J || !(*J)->TryGetStringField(TEXT("object"),Text) || !FGuid::Parse(Text,ObjectId)
                        || Seen.Contains(ObjectId) || !ReadPoint(**J,TEXT("point"),Requested)) {Valid=false;break;}
                    (*J)->TryGetBoolField(TEXT("locked"),Locked);FString TokenText;
                    if ((*J)->TryGetStringField(TEXT("token"),TokenText) && (!FGuid::Parse(TokenText,Token) || !Token.IsValid())) {Valid=false;break;}
                    double Generation=0;const bool HasGeneration=(*J)->TryGetNumberField(TEXT("generation"),Generation);
                    if ((HasGeneration && (!FMath::IsFinite(Generation) || Generation<1 || Generation>MAX_uint32 || Generation!=FMath::FloorToDouble(Generation)))
                        || (AllowTerminal && (!HasGeneration || Locked))) {Valid=false;break;}
                    const auto* O=All.FindByPredicate([&](const auto& V){return V.Key.StableId==ObjectId
                        && (!HasGeneration || V.Key.LifeGeneration==uint32(Generation))
                        && ((V.LifeState==1 && H->HasSimulation(V.Key)) || (AllowTerminal && V.LifeState>=2));});
                    // A real death can commit after the driver's live readback.
                    // Cleanup acknowledges that exact terminal life without a
                    // relocation, lock mutation, resurrection or simulated owner.
                    if (O && AllowTerminal && O->LifeState>=2)
                    {
                        Seen.Add(ObjectId);auto Item=MakeShared<FJsonObject>();
                        Item->SetStringField(TEXT("object"),ObjectId.ToString(EGuidFormats::Digits));
                        Item->SetStringField(TEXT("token"),Token.ToString(EGuidFormats::Digits));
                        Item->SetNumberField(TEXT("generation"),O->Key.LifeGeneration);Item->SetNumberField(TEXT("life"),O->LifeState);
                        Item->SetStringField(TEXT("status"),TEXT("terminal_cleanup_skipped"));
                        Applied.Add(MakeShared<FJsonValueObject>(Item));continue;
                    }
                    if (!O || !QueryPosition(*GetWorld(),*H,*O,Before) || !PreparePosition(*GetWorld(),*H,*O,Requested,Token,Locked,FindNearby)
                        || !QueryPosition(*GetWorld(),*H,*O,After)) {Valid=false;break;}
                    Seen.Add(ObjectId);auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("object"),ObjectId.ToString(EGuidFormats::Digits));
                    Item->SetStringField(TEXT("token"),Token.ToString(EGuidFormats::Digits));Item->SetArrayField(TEXT("before"),Point(Before));
                    Item->SetArrayField(TEXT("requested"),Point(Requested));Item->SetArrayField(TEXT("applied"),Point(After));
                    Item->SetBoolField(TEXT("locked"),Locked);Applied.Add(MakeShared<FJsonValueObject>(Item));
                }
                A.Result->SetStringField(TEXT("fixture"),TEXT("development_positions"));A.Result->SetArrayField(TEXT("positions"),Applied);
                A.Result->SetStringField(TEXT("scope"),TEXT("Preparation positions only; numeric state, damage, population, work and gameplay results are unchanged"));
                if (!Valid) {Reject(TEXT("Batch preparation requires live objects at their unique simulator and valid navigation"));continue;}
                A.Status=TEXT("completed");
#else
                Reject(TEXT("Development position preparation is unavailable"));continue;
#endif
            }
            else if (A.Kind==TEXT("fixture_position"))
            {
#if !UE_BUILD_SHIPPING
                FString ObjectText;FGuid ObjectId,Token=Id;bool Locked=false;
                const auto* Actors=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
                if (!bEnableResourceSupply || !Actors || !HasPoint
                    || (Scenario!=TEXT("gameplay_merged") && !Scenario.StartsWith(TEXT("fault_")))
                    || Actors->GetManifest().Numbers.FindRef(TEXT("acceptance/resource_supply"))!=1
                    || !A.Arguments->TryGetStringField(TEXT("object"),ObjectText) || !FGuid::Parse(ObjectText,ObjectId))
                {Reject(TEXT("Position preparation requires the explicit development profile and a stable object"));continue;}
                FString TokenText;
                if (A.Arguments->TryGetStringField(TEXT("token"),TokenText) && (!FGuid::Parse(TokenText,Token) || !Token.IsValid()))
                {Reject(TEXT("Invalid preparation restore token"));continue;}
                A.Arguments->TryGetBoolField(TEXT("locked"),Locked);
                TArray<FGuLiHybridObjectState> All;H->GetObjects(All);
                const auto* O=All.FindByPredicate([&](const auto& V){return V.Key.StableId==ObjectId && V.LifeState==1 && H->HasSimulation(V.Key);});
                FVector Before;
                if (!O || !QueryPosition(*GetWorld(),*H,*O,Before))
                {Reject(TEXT("Position preparation must run at the live object's unique simulator"));continue;}
                bool FindNearby=false;A.Arguments->TryGetBoolField(TEXT("find_nearby"),FindNearby);
                const bool Good=PreparePosition(*GetWorld(),*H,*O,P,Token,Locked,FindNearby);
                if (!Good) {Reject(TEXT("Prepared position is unsupported, unavailable or outside navigation"));continue;}
                FVector Actual;
                if (!QueryPosition(*GetWorld(),*H,*O,Actual)) {Reject(TEXT("Prepared position readback failed"));continue;}
                A.Result->SetStringField(TEXT("fixture"),TEXT("development_position"));
                A.Result->SetStringField(TEXT("object"),ObjectId.ToString(EGuidFormats::Digits));
                A.Result->SetStringField(TEXT("token"),Token.ToString(EGuidFormats::Digits));
                A.Result->SetArrayField(TEXT("before"),Point(Before));A.Result->SetArrayField(TEXT("requested"),Point(P));
                A.Result->SetArrayField(TEXT("applied"),Point(Actual));A.Result->SetBoolField(TEXT("locked"),Locked);
                A.Result->SetNumberField(TEXT("health"),O->Health.Health);
                A.Result->SetStringField(TEXT("scope"),TEXT("Preparation only; no damage, population, task, receipt or gameplay assertion is fabricated"));
                A.Status=TEXT("completed");
#else
                Reject(TEXT("Development position preparation is unavailable"));continue;
#endif
            }
            else if (A.Kind==TEXT("resource_supply"))
            {
#if !UE_BUILD_SHIPPING
                const auto* Actors=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();FString Color,TeamText;double Amount=0;
                if (!bEnableResourceSupply || !Actors || !PS->IsCommander()
                    || (Scenario!=TEXT("gameplay_merged") && !Scenario.StartsWith(TEXT("fault_")))
                    || Actors->GetManifest().Numbers.FindRef(TEXT("acceptance/resource_supply"))!=1
                    || !A.Arguments->TryGetStringField(TEXT("color"),Color) || (Color!=TEXT("blue") && Color!=TEXT("red"))
                    || !A.Arguments->TryGetNumberField(TEXT("amount"),Amount) || Amount<1 || Amount>1024
                    || Amount!=FMath::FloorToDouble(Amount) || (int32(Amount)&(int32(Amount)-1))!=0)
                {Reject(TEXT("Resource supply requires the explicit development profile and an exact configured denomination"));continue;}
                EGuLiTeam SupplyTeam=PS->GetTeam();
                if (A.Arguments->TryGetStringField(TEXT("team"),TeamText))
                {
                    if (TeamText==TEXT("Red")) SupplyTeam=EGuLiTeam::Red;
                    else if (TeamText==TEXT("Blue")) SupplyTeam=EGuLiTeam::Blue;
                    else {Reject(TEXT("Development supply team must be Red or Blue"));continue;}
                }
                const auto& Manifest=Actors->GetManifest();FGuLiHybridObjectState SourceState;
                const FName Definition(*Manifest.Strings.FindRef(TEXT("army/root_definition")));
                const auto Root=H->GetTeamCapabilityKey(SupplyTeam,TEXT("army_state"));
                if (!H->GetObjectView(Root,SourceState) || SourceState.Definition!=Definition || SourceState.LifeState!=1
                    || SourceState.Team!=SupplyTeam || (!H->HasSimulation(Root) && !H->HasControl(Root)))
                {Reject(TEXT("Development supply requires the actual team army simulator or controller"));continue;}
                // The fixed Commander can use the existing controlled request
                // route while the indivisible resource group changes simulator.
                const auto* Source=&SourceState;
                const auto Before=H->GetTeamBalance(Source->Team);
                FGuLiHybridEconomyRequest Request;Request.Source=Source->Key;Request.Event=Id;
                Request.Rule=FName(*FString::Printf(TEXT("acceptance/resource_supply/%s/%d"),*Color,int32(Amount)));
                Request.Operation=EGuLiHybridEconomyOperation::Credit;
                A.Result->SetStringField(TEXT("fixture"),TEXT("development_resource_supply"));
                A.Result->SetStringField(TEXT("source"),Source->Key.StableId.ToString(EGuidFormats::Digits));
                A.Result->SetStringField(TEXT("supplier"),A.Player.ToString(EGuidFormats::Digits));
                A.Result->SetStringField(TEXT("color"),Color);A.Result->SetNumberField(TEXT("amount"),Amount);
                A.Result->SetNumberField(TEXT("before_blue"),Before.Blue);A.Result->SetNumberField(TEXT("before_red"),Before.Red);
                A.Request=H->SubmitEconomyNative(Request,{});
                if (!A.Request.IsValid()) {Reject(TEXT("Development supply request was not retained"));continue;}
                A.Status=TEXT("submitted");
#else
                Reject(TEXT("Development resource supply is unavailable"));continue;
#endif
            }
            else if (A.Kind==TEXT("selection_objects"))
            {
                const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;TArray<FGuLiHybridObjectKey> Keys;TArray<FGuLiHybridObjectState> All;H->GetObjects(All);
                bool Valid=PS->IsCommander() && A.Arguments->TryGetArrayField(TEXT("objects"),Values) && Values->Num()<=100000;
                if (Valid) for (const auto& V:*Values)
                {
                    FString Text;FGuid ObjectId;
                    if (!V->TryGetString(Text) || !FGuid::Parse(Text,ObjectId)) {Valid=false;break;}
                    const auto* O=All.FindByPredicate([&](const auto& O){return O.Key.StableId==ObjectId && O.Key.LifeGeneration>0 && H->HasControl(O.Key);});
                    if (!O) {Valid=false;break;}Keys.AddUnique(O->Key);
                }
                AGuLiHybridNetworkReviewActor* Entry=nullptr;
                for (TActorIterator<AGuLiHybridNetworkReviewActor> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("HybridNetworkReview"))) {Entry=*It;break;}
                if (!Valid || !Entry) {Reject(TEXT("Original map selection entry or controlled objects are missing"));continue;}
                bool CommandSelection=false;A.Arguments->TryGetBoolField(TEXT("command_selection"),CommandSelection);
                if (CommandSelection)
                {
#if !UE_BUILD_SHIPPING
                    TArray<FGuLiSoldierId> Soldiers;
                    Valid=Sync && GetDefault<UGuLiHybridReviewSubsystem>()->bEnableResourceSupply && !Keys.IsEmpty();
                    for (const auto& Key:Keys)
                    {
                        const auto* O=All.FindByPredicate([&](const auto& O){return O.Key==Key;});
                        if (!O || O->Key.Kind!=EGuLiHybridObjectKind::Soldier || O->LifeState!=1 || !O->Ordinal) {Valid=false;break;}
                        Soldiers.Add(FGuLiSoldierId(O->Ordinal));
                    }
                    if (!Valid || !Sync->PrepareReviewSoldierSelection(Soldiers,StageNumber*1000+SceneActions.Num()))
                    {Reject(TEXT("Development command selection could not confirm the live controlled members"));continue;}
                    A.Result->SetStringField(TEXT("fixture"),TEXT("development_command_selection"));
#else
                    Reject(TEXT("Development command selection is unavailable"));continue;
#endif
                }
                else Entry->SelectObjects(Keys);
                A.Result->SetNumberField(TEXT("selected"),Keys.Num());A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("selection_type"))
            {
                double Ordinal=0;EGuLiTeam Team;uint16 Type=0;FVector Position;
                auto* Army=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
                if (!Sync || !Army || !PS->IsCommander() || !A.Arguments->TryGetNumberField(TEXT("soldier"),Ordinal)
                    || Ordinal<1 || Ordinal>MAX_uint32 || Ordinal!=FMath::FloorToDouble(Ordinal)
                    || !Army->GetTaskSoldierInfo(FGuLiSoldierId(uint32(Ordinal)),Team,Type,Position) || Team!=PS->GetTeam())
                {Reject(TEXT("Invalid same-type selection anchor"));continue;}
                // Place the review camera ray at the current query position,
                // then use the player's same-type query and its normal limits.
                FGuLiSelectionRequest Request;Request.ClientRequestId=StageNumber*1000+SceneActions.Num();
                Request.Kind=EGuLiSelectionKind::SameType;Request.SeedSoldierId=FGuLiSoldierId(uint32(Ordinal));Request.Center=Position;
                Request.RayOrigin=Position+FVector(0,0,20000);Request.RayDirection=FVector::DownVector;
                Request.BoxTopLeftRay=FVector(-60000,-60000,-20000).GetSafeNormal();
                Request.BoxTopRightRay=FVector(60000,-60000,-20000).GetSafeNormal();
                Request.BoxBottomRightRay=FVector(60000,60000,-20000).GetSafeNormal();
                Request.BoxBottomLeftRay=FVector(-60000,60000,-20000).GetSafeNormal();
                Sync->SubmitOrderedSelection(Request);
                const bool Selected=Sync->GetSelectionState().Cohorts.ContainsByPredicate([&](const auto& C){return C.MemberIds.Contains(Request.SeedSoldierId);});
                if (!Selected) {Reject(TEXT("Same-type input did not confirm its anchor"));continue;}
                A.Result->SetNumberField(TEXT("selection_revision"),Sync->GetSelectionState().SelectionRevision);A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("selection_group"))
            {
                double Ordinal=0;
                if (!Sync || !PS->IsCommander() || !A.Arguments->TryGetNumberField(TEXT("soldier"),Ordinal)
                    || Ordinal<1 || Ordinal>MAX_uint32 || Ordinal!=FMath::FloorToDouble(Ordinal))
                {Reject(TEXT("Invalid portrait anchor"));continue;}
                FGuLiPanelSelectionRequest Request;Request.SoldierId=FGuLiSoldierId(uint32(Ordinal));
                Request.Action=EGuLiPanelSelectionAction::SingleGroup;
                if (!Sync->SubmitPanelSelection(Request)) {Reject(TEXT("Portrait selection rejected"));continue;}
                const auto& Selected=Sync->GetSelectionState();int32 Count=0;bool HasAnchor=false;
                for (const auto& C:Selected.Cohorts) {Count+=C.MemberIds.Num();HasAnchor|=C.MemberIds.Contains(Request.SoldierId);}
                if (!HasAnchor || Count>GULI_PANEL_PORTRAIT_GROUP_SIZE) {Reject(TEXT("Portrait did not confirm its selected group"));continue;}
                A.Result->SetNumberField(TEXT("selection_revision"),Selected.SelectionRevision);A.Result->SetNumberField(TEXT("selected"),Count);
                A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("selection_box"))
            {
                FVector Min,Max;if (!Sync || !ReadPoint(*A.Arguments,TEXT("min"),Min) || !ReadPoint(*A.Arguments,TEXT("max"),Max)) {Reject(TEXT("Invalid selection input"));continue;}
                if (Min.X>Max.X || Min.Y>Max.Y || Min.Z>Max.Z) {Reject(TEXT("Invalid selection bounds"));continue;}
                FGuLiSelectionRequest R;R.ClientRequestId=StageNumber*1000+SceneActions.Num();R.Kind=EGuLiSelectionKind::Box;
                R.Center=(Min+Max)*.5;
                const double Height=FMath::Max(20000.,Max.Z-Min.Z+1000.);
                R.RayOrigin=R.Center+FVector(0,0,Height);R.RayDirection=FVector::DownVector;
                // Project the complete three-dimensional bounds into the same
                // perspective frustum used by the real box-selection input.
                // Using only the mid-height rectangle clips elevated edge units.
                FVector2D ProjectMin(DBL_MAX,DBL_MAX),ProjectMax(-DBL_MAX,-DBL_MAX);
                for (const double Z:{Min.Z,Max.Z}) for (const double X:{Min.X,Max.X}) for (const double Y:{Min.Y,Max.Y})
                {
                    const double Scale=Height/(R.RayOrigin.Z-Z);
                    const FVector2D Project(R.Center.X+(X-R.Center.X)*Scale,R.Center.Y+(Y-R.Center.Y)*Scale);
                    ProjectMin.X=FMath::Min(ProjectMin.X,Project.X);ProjectMin.Y=FMath::Min(ProjectMin.Y,Project.Y);
                    ProjectMax.X=FMath::Max(ProjectMax.X,Project.X);ProjectMax.Y=FMath::Max(ProjectMax.Y,Project.Y);
                }
                auto Ray=[&](double X,double Y){return (FVector(X,Y,R.Center.Z)-R.RayOrigin).GetSafeNormal();};
                R.BoxTopLeftRay=Ray(ProjectMin.X,ProjectMin.Y);R.BoxTopRightRay=Ray(ProjectMax.X,ProjectMin.Y);
                R.BoxBottomRightRay=Ray(ProjectMax.X,ProjectMax.Y);R.BoxBottomLeftRay=Ray(ProjectMin.X,ProjectMax.Y);
                Sync->SubmitOrderedSelection(R);A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("unit_task"))
            {
                double Command=0;FString Intent;
                if (!Sync || !HasPoint || !A.Arguments->TryGetNumberField(TEXT("command"),Command) || Command<1 || Command>MAX_uint32
                    || Command!=FMath::FloorToDouble(Command) || !A.Arguments->TryGetStringField(TEXT("intent"),Intent)) {Reject(TEXT("Invalid task input"));continue;}
                bool GroundRay=false;A.Arguments->TryGetBoolField(TEXT("ground_ray"),GroundRay);
                if (GroundRay)
                {
                    auto* Commander=Cast<AGuLiCommanderPlayerController>(PC);FVector Ground;
                    if (!Commander || !Commander->TraceCommanderGroundRay(P+FVector(0,0,20000),FVector::DownVector,Ground))
                    {Reject(TEXT("The formal Commander ground ray did not hit terrain"));continue;}
                    A.Result->SetArrayField(TEXT("ground"),Point(Ground));P=Ground;
                }
                FGuLiUnitTaskCommand R;R.CommandId=uint32(Command);R.Target=P;R.TargetIntent=FName(Intent);
                R.bGroundMoveOnly=Intent==TEXT("Move");double Special=0;
                if (A.Arguments->TryGetNumberField(TEXT("special"),Special)) {R.Kind=EGuLiUnitTaskKind::Special;R.SpecialTaskId=int32(Special);}
                A.Result->SetNumberField(TEXT("command"),Command);A.Status=TEXT("submitted");Sync->SubmitOrderedTask(R);
            }
            else if (A.Kind==TEXT("unit_skill"))
            {if (!Skills) {Reject(TEXT("Missing skill input"));continue;}A.Request=Skills->RequestHybridUnitSkills(HasPoint,P);A.Status=TEXT("submitted");}
            else if (A.Kind==TEXT("global_skill"))
            {
                FString Skill;if (!Skills || !A.Arguments->TryGetStringField(TEXT("skill"),Skill)) {Reject(TEXT("Missing skill definition"));continue;}
                A.Request=Skills->RequestGlobalSkill(FName(Skill),HasPoint,P);A.Status=TEXT("submitted");
            }
            else if (A.Kind.StartsWith(TEXT("teleport_")))
            {
                if (!Teleport || (A.Kind!=TEXT("teleport_cancel") && !HasPoint)) {Reject(TEXT("Missing teleport input"));continue;}
                const auto Command=A.Kind==TEXT("teleport_source")?EGuLiTeleportCommand::Source:A.Kind==TEXT("teleport_destination")?EGuLiTeleportCommand::Destination:EGuLiTeleportCommand::Cancel;
                if (A.Kind!=TEXT("teleport_source") && A.Kind!=TEXT("teleport_destination") && A.Kind!=TEXT("teleport_cancel")) {Reject(TEXT("Unknown teleport input"));continue;}
                A.Request=Id;Teleport->ServerSubmit(Id,Command,Teleport->QueryState().CastId,P);A.Status=TEXT("submitted");
            }
            else if (A.Kind==TEXT("rogue_open"))
            {if (!Cards) {Reject(TEXT("Missing Commander choice session"));continue;}Cards->Open();A.Status=TEXT("submitted");}
            else if (A.Kind==TEXT("rogue_select") || A.Kind==TEXT("rogue_confirm"))
            {
                double Index=0;if (!Cards || !A.Arguments->TryGetNumberField(TEXT("index"),Index) || Index!=FMath::FloorToDouble(Index)
                    || Index<0 || Index>2 || !Cards->ActivateCandidate(int32(Index))) {Reject(TEXT("Card selection input was rejected"));continue;}
                A.Request=Cards->GetSessionId();A.Status=A.Kind==TEXT("rogue_select")?TEXT("completed"):TEXT("submitted");
            }
            else if (A.Kind==TEXT("rogue_cancel"))
            {if (!Cards) {Reject(TEXT("Missing card UI"));continue;}Cards->Cancel();A.Status=TEXT("completed");}
            else if (A.Kind==TEXT("rogue_reroll"))
            {
                if (!Cards) {Reject(TEXT("Missing card UI"));continue;}
                if (!Cards->CanReroll()) continue;
                A.Result->SetStringField(TEXT("previous_cards"),FString::Join(Cards->GetCandidateIds(),TEXT("|")));
                Cards->Reroll();A.Status=TEXT("submitted");
            }
            else if (A.Kind==TEXT("ui_rebind"))
            {
                auto* HUD=Cast<AGuLiCommanderHUD>(PC->GetHUD());
                if (!UGuLiNetworkSettings::CanPresent(GetWorld()) || !HUD) {Reject(TEXT("UI rebind requires the rendered local HUD"));continue;}
                HUD->RefreshLocalBindings();
                if (auto* Placement=PC->FindComponentByClass<UGuLiBuildingPlacementComponent>()) Placement->RebindHybridReceipts();
                if (Skills) Skills->RebindHybridRequests();
                if (auto* Ship=Cast<AGuLiStrikeShip>(PC->GetPawn()))
                    if (auto* Assembly=Ship->FindComponentByClass<UGuLiShipAssemblyComponent>();Assembly && Build)
                        if (!Build->AdoptExistingShip(*Assembly)) {Reject(TEXT("Air UI context could not rebind its existing assembly"));continue;}
                A.Result->SetNumberField(TEXT("bindings_generation"),double(HUD->GetLocalBindingGeneration()));A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("review_entry"))
            {
                AGuLiHybridNetworkReviewActor* Entry=nullptr;int32 Count=0;
                for (TActorIterator<AGuLiHybridNetworkReviewActor> It(GetWorld());It;++It)
                    if (It->ActorHasTag(TEXT("HybridNetworkReview"))) {Entry=*It;++Count;}
                if (Count!=1) {Reject(TEXT("Original map must contain exactly one saved review entry"));continue;}
                FString Arguments,Error;
                if (!Entry->BuildLaunchArguments(Arguments,Error)) {Reject(Error);continue;}
                TSharedPtr<FJsonObject> View;
                if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Entry->GetOwnershipStatus()),View) || !View)
                {Reject(TEXT("Saved map ownership query returned malformed data"));continue;}
                A.Result->SetStringField(TEXT("actor"),Entry->GetPathName());A.Result->SetStringField(TEXT("class"),Entry->GetClass()->GetPathName());
                A.Result->SetStringField(TEXT("launch_arguments"),Arguments);A.Result->SetStringField(TEXT("manifest"),Entry->Manifest.FilePath);
                A.Result->SetNumberField(TEXT("units"),Entry->Units);A.Result->SetNumberField(TEXT("additional_flights"),Entry->AdditionalFlights);
                A.Result->SetBoolField(TEXT("fixed_roles"),View->GetBoolField(TEXT("fixed_roles")));
                A.Result->SetNumberField(TEXT("object_count"),View->GetNumberField(TEXT("object_count")));
                TArray<TSharedPtr<FJsonValue>> OwnershipSamples;
                for (const auto& V:View->GetArrayField(TEXT("objects"))) if (OwnershipSamples.Num()<8) OwnershipSamples.Add(V);
                A.Result->SetArrayField(TEXT("ownership_samples"),OwnershipSamples);A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("capture_view"))
            {
                if (!UGuLiNetworkSettings::CanPresent(GetWorld())) {Reject(TEXT("Only the rendered endpoint can capture its viewport"));continue;}
                const FString Path=FPaths::ConvertRelativePathToFull(Directory/(TEXT("view-")+Id.ToString(EGuidFormats::Digits)+TEXT(".png")));
                FScreenshotRequest::RequestScreenshot(Path,true,false);A.Result->SetStringField(TEXT("image"),Path);A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("building_preview") || A.Kind==TEXT("building_cancel"))
            {
                auto* Placement=PC->FindComponentByClass<UGuLiBuildingPlacementComponent>();
                if (!Placement) {Reject(TEXT("Missing production placement UI"));continue;}
                if (A.Kind==TEXT("building_preview"))
                {
                    double Definition=0;
                    if (!A.Arguments->TryGetNumberField(TEXT("definition"),Definition) || Definition<1 || Definition>MAX_int32
                        || Definition!=FMath::FloorToDouble(Definition) || !Placement->SelectBuildingDefinition(int32(Definition)))
                    {Reject(TEXT("Catalog preview selection rejected"));continue;}
                }
                else if (!Placement->HandleCancelAction()) {Reject(TEXT("No active placement preview to cancel"));continue;}
                A.Result->SetBoolField(TEXT("active"),Placement->IsBuildModeActive());
                A.Result->SetNumberField(TEXT("committed_count"),double(Placement->GetCommittedPlacementCount()));
                A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("building_place"))
            {
#if WITH_EDITOR
                auto* Placement=PC->FindComponentByClass<UGuLiBuildingPlacementComponent>();double Definition=0,Yaw=0;bool Clear=true;
                A.Arguments->TryGetNumberField(TEXT("yaw"),Yaw);A.Arguments->TryGetBoolField(TEXT("find_clear"),Clear);
                if (!Placement || !HasPoint || !A.Arguments->TryGetNumberField(TEXT("definition"),Definition)
                    || Definition<1 || Definition>MAX_int32 || Definition!=FMath::FloorToDouble(Definition) || !FMath::IsFinite(Yaw))
                {Reject(TEXT("Invalid placement input"));continue;}
                FVector Ground=P;
                if (Clear && !Placement->EditorQAFindClearDefinition(int32(Definition),P,float(Yaw),Ground))
                {Reject(TEXT("No valid placement location near the requested point"));continue;}
                A.Result->SetNumberField(TEXT("previous_receipts"),double(Placement->GetPlacementReceiptCount()));
                A.Result->SetArrayField(TEXT("ground"),Point(Ground));A.Status=TEXT("submitted");
                if (!Placement->EditorQASubmitDefinition(int32(Definition),Ground,float(Yaw))) Reject(TEXT("Placement input rejected before submission"));
#else
                Reject(TEXT("Spatial placement authoring requires the authorized source Editor target"));
#endif
            }
            else if (A.Kind==TEXT("ship_target"))
            {
                auto* Ship=Cast<AGuLiStrikeShip>(PC->GetPawn());FString Target;FGuid Stable;FGuLiTargetHandle Handle;
                if (!Ship || !A.Arguments->TryGetStringField(TEXT("target"),Target) || !FGuid::Parse(Target,Stable)) {Reject(TEXT("Invalid Ship target input"));continue;}
                TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
                const auto* Object=Objects.FindByPredicate([&](const auto& O){return O.Key.StableId==Stable && O.LifeState==1;});
                if (!Object || !H->ResolveObjectTarget(Object->Key,Handle)) {Reject(TEXT("Target has no live query handle"));continue;}
                Ship->SetWingmanAttackTarget(Handle);
                const auto Applied=Ship->GetWingmanAttackTarget();
                if (!Applied.bSpecified || Applied.Target!=Handle) {Reject(TEXT("Ship target input was not applied by its controller"));continue;}
                A.Result->SetStringField(TEXT("target"),Stable.ToString(EGuidFormats::Digits));
                A.Result->SetNumberField(TEXT("target_revision"),Applied.Revision);A.Status=TEXT("completed");
            }
            else if (A.Kind==TEXT("inventory_delivery"))
            {
                FString SourceId,TargetId;FGuid Source,Target;
                if (!A.Arguments->TryGetStringField(TEXT("source"),SourceId) || !FGuid::Parse(SourceId,Source)
                    || !A.Arguments->TryGetStringField(TEXT("target"),TargetId) || !FGuid::Parse(TargetId,Target))
                {Reject(TEXT("Invalid inventory interaction input"));continue;}
                TArray<FGuLiHybridObjectState> SceneDirectory;H->GetObjects(SceneDirectory);
                const auto* S=SceneDirectory.FindByPredicate([&](const auto& O){return O.Key.StableId==Source && O.LifeState==1;});
                const auto* T=SceneDirectory.FindByPredicate([&](const auto& O){return O.Key.StableId==Target && O.LifeState==1;});
                auto* Inventories=GetWorld()->GetSubsystem<UGuLiResourceInventorySubsystem>();
                const auto* Actors=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();FVector From,To;
                const double Range=Actors?Actors->GetManifest().Numbers.FindRef(TEXT("economy/FactoryDockOffsetCentimeters")):0;
                if (!S || !T || S->Team!=T->Team || !H->HasSimulation(S->Key) || !Inventories || Range<=0
                    || !QueryPosition(*GetWorld(),*H,*S,From) || !QueryPosition(*GetWorld(),*H,*T,To)
                    || FVector::DistSquared2D(From,To)>FMath::Square(Range))
                {Reject(TEXT("Inventory interaction lacks authority or docking proximity"));continue;}
                A.Request=Inventories->RequestDelivery(S->Key,T->Key);
                if (!A.Request.IsValid()) {Reject(TEXT("Inventory delivery input was not retained"));continue;}
                A.Result->SetNumberField(TEXT("distance_cm"),FVector::Dist2D(From,To));A.Status=TEXT("submitted");
            }
            else if (A.Kind==TEXT("ship_equipment"))
            {
                auto* ChoiceBuild=PS->FindComponentByClass<UGuLiShipBuildComponent>();FString Node;
                if (!ChoiceBuild || !A.Arguments->TryGetStringField(TEXT("node"),Node)) {Reject(TEXT("Missing assembly input"));continue;}
                const auto& S=ChoiceBuild->GetBuildState();A.Request=Id;
                if (ChoiceBuild->RequestConfirmedChoice(Id,FName(Node),S.MatchEpoch,S.BuildRevision)!=Id) {Reject(TEXT("Assembly input was not retained"));continue;}
                A.Status=TEXT("submitted");
            }
            else if (A.Kind==TEXT("weapon_input"))
            {
                auto* Ship=Cast<AGuLiStrikeShip>(PC->GetPawn());FString Subject,Slot;bool Pressed=false;
                if (!Ship || !A.Arguments->TryGetStringField(TEXT("subject"),Subject) || !A.Arguments->TryGetStringField(TEXT("slot"),Slot)
                    || !A.Arguments->TryGetBoolField(TEXT("pressed"),Pressed)) {Reject(TEXT("Invalid weapon binding input"));continue;}
                const auto* Channel=Ship->GetGroupAbilityConfig().WeaponChannels.FindByPredicate([&](const auto& C)
                    {return C.bEnabled && C.Binding.SubjectId==FName(Subject) && C.Binding.SlotId==FName(Slot);});
                if (!Channel || !Ship->SubmitGameplayWeaponInput(Channel->Binding,Pressed)) {Reject(TEXT("Weapon input rejected by its gameplay capability"));continue;}
                A.Status=TEXT("completed");
            }
            else {Reject(TEXT("Unsupported formal input action: ")+A.Kind);continue;}
        }
        if (A.Status!=TEXT("submitted")) continue;
        if (A.Kind==TEXT("rogue_open") && Cards && Cards->GetSessionId().IsValid() && !Cards->GetCandidateIds().IsEmpty()) A.Status=TEXT("completed");
        if (A.Kind==TEXT("rogue_open") && Cards && A.Status==TEXT("submitted") && !Cards->IsOpen())
        {Reject(TEXT("Card UI closed before a valid offer arrived; inspect the catalogue and presentation contract"));continue;}
        if (A.Kind==TEXT("rogue_confirm") && Cards && Cards->GetLastCommittedSession()==A.Request) A.Status=TEXT("completed");
        if (A.Kind==TEXT("rogue_reroll") && Cards && !Cards->GetCandidateIds().IsEmpty()
            && FString::Join(Cards->GetCandidateIds(),TEXT("|"))!=A.Result->GetStringField(TEXT("previous_cards"))) A.Status=TEXT("completed");
        if (A.Kind==TEXT("ship_equipment"))
        {
            FGuLiShipChoiceResult R;
            if (const auto* ChoiceBuild=PS->FindComponentByClass<UGuLiShipBuildComponent>();ChoiceBuild && ChoiceBuild->FindChoiceResult(A.Request,R))
            {A.Result->SetBoolField(TEXT("committed"),R.bCommitted);A.Result->SetNumberField(TEXT("revision"),double(R.BuildRevision));A.Error=R.Reason;A.Status=TEXT("completed");}
        }
        if (A.Kind==TEXT("building_place"))
        {
            const auto* Placement=PC->FindComponentByClass<UGuLiBuildingPlacementComponent>();
            if (Placement && Placement->GetPlacementReceiptCount()>A.Result->GetNumberField(TEXT("previous_receipts")))
            {
                const auto& R=Placement->GetLastPlacementReceipt();A.Result->SetBoolField(TEXT("accepted"),R.WasAccepted());
                A.Result->SetNumberField(TEXT("request"),R.ClientRequestId);A.Result->SetNumberField(TEXT("reject_reason"),uint8(R.RejectReason));
                A.Status=TEXT("completed");
            }
        }
        if (A.Kind==TEXT("inventory_delivery") || A.Kind==TEXT("resource_supply"))
        {
            FGuLiHybridTransactionResult R;
            if (H->FindTransactionResult(A.Request,R) && R.Status!=EGuLiHybridTransactionStatus::Deferred && R.Status!=EGuLiHybridTransactionStatus::Paused)
            {
                A.Result->SetBoolField(TEXT("committed"),R.IsCommitted());A.Result->SetNumberField(TEXT("revision"),double(R.LedgerRevision));
                A.Result->SetNumberField(TEXT("balance_blue"),R.Balance.Blue);A.Result->SetNumberField(TEXT("balance_red"),R.Balance.Red);
                A.Status=TEXT("completed");
            }
        }
        if (Skills && A.Request.IsValid())
        {
            if (const auto* Receipt=SceneSkillReplies.Find(A.Request))
            {
                const auto& Reply=*Receipt;
                A.Result->SetNumberField(TEXT("code"),uint8(Reply.Code));A.Result->SetStringField(TEXT("cast"),Reply.Global.CastId.ToString(EGuidFormats::Digits));
                A.Result->SetNumberField(TEXT("launched"),Reply.Global.LaunchedProjectileCount);A.Result->SetNumberField(TEXT("units"),Reply.Units.Num());
                if (Reply.Code!=EGuLiActiveSkillResultCode::PendingCommit && !Reply.Global.bPendingCommit)
                {A.Status=TEXT("completed");A.Error=Reply.Error;}
            }
        }
    }
}

void UGuLiHybridReviewSubsystem::AppendSceneEvidence(const TSharedRef<FJsonObject>& Json) const
{
    if (Scenario.IsEmpty() || Scenario==TEXT("performance")) return;
    auto Review=MakeShared<FJsonObject>();Review->SetStringField(TEXT("phase"),Phase);Review->SetStringField(TEXT("failure"),Failure);
    if (const auto* ReviewPC=GetWorld()->GetFirstPlayerController())
        if (const auto* ReviewPS=ReviewPC->GetPlayerState<AGuLiBattlePlayerState>())
        {Review->SetStringField(TEXT("player"),ReviewPS->GetPlayerGuid().ToString(EGuidFormats::Digits));Review->SetBoolField(TEXT("battle_ready"),ReviewPS->IsBattleReady());}
    Json->SetObjectField(TEXT("scene_review"),Review);
    TArray<TSharedPtr<FJsonValue>> Actions,CommandResults;
    for (const auto& [Id,A]:SceneActions)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),Id.ToString(EGuidFormats::Digits));J->SetStringField(TEXT("kind"),A.Kind);
        J->SetStringField(TEXT("player"),A.Player.ToString(EGuidFormats::Digits));J->SetStringField(TEXT("status"),A.Status);
        J->SetStringField(TEXT("request"),A.Request.ToString(EGuidFormats::Digits));J->SetStringField(TEXT("error"),A.Error);
        J->SetNumberField(TEXT("submitted_tick"),double(A.SubmittedAt));J->SetNumberField(TEXT("submitted_epoch"),double(A.SubmittedEpoch));
        // Submitted actions can fill their result on a later frame. Detach the
        // field map before the snapshot is serialized by the evidence worker.
        J->SetObjectField(TEXT("result"),MakeShared<FJsonObject>(*A.Result));Actions.Add(MakeShared<FJsonValueObject>(J));
        double Command=0;
        if (A.Kind==TEXT("unit_task") && A.Arguments->TryGetNumberField(TEXT("command"),Command))
        {
            const FGuid Event=FGuLiHybridManifest::MakeStableKey(GetWorld()->GetOutermost()->GetName(),EGuLiHybridObjectKind::Soldier,
                A.Player.ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/command/%u"),uint32(Command))).StableId;
            if (const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
                CommandResults.Add(MakeShared<FJsonValueObject>(H->GetReviewCommandResult(Event)));
        }
    }
    Json->SetArrayField(TEXT("review_actions"),Actions);
    Json->SetArrayField(TEXT("command_results"),CommandResults);
    auto Effects=MakeShared<FJsonObject>();
    for (const auto& [Name,C]:ObservedEffects)
    {
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("launched"),double(C.Launched));J->SetNumberField(TEXT("terminated"),double(C.Terminated));
        J->SetNumberField(TEXT("minimum_speed"),C.Launched?C.MinimumSpeed:0);J->SetNumberField(TEXT("maximum_speed"),C.MaximumSpeed);
        auto Ends=MakeShared<FJsonObject>();for (const auto& [Reason,N]:C.EndReasons) Ends->SetNumberField(FString::FromInt(Reason),double(N));
        J->SetObjectField(TEXT("end_reasons"),Ends);Effects->SetObjectField(Name,J);
    }
    Json->SetObjectField(TEXT("observed_effects"),Effects);
    TArray<TSharedPtr<FJsonValue>> BlueprintCalls;
    for (const auto& [Key,V]:BlueprintExecutions)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("class"),V.Class);J->SetStringField(TEXT("function"),V.Function);
        J->SetNumberField(TEXT("calls"),double(V.Calls));J->SetNumberField(TEXT("disabled"),double(V.Disabled));
        J->SetNumberField(TEXT("enabled"),double(V.Enabled));BlueprintCalls.Add(MakeShared<FJsonValueObject>(J));
    }
    Json->SetArrayField(TEXT("blueprint_executions"),BlueprintCalls);
    const auto* PC=GetWorld()->GetFirstPlayerController();const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto UI=MakeShared<FJsonObject>();UI->SetBoolField(TEXT("can_present"),UGuLiNetworkSettings::CanPresent(GetWorld()));
    if (const auto* Commander=Cast<AGuLiCommanderPlayerController>(PC))
        UI->SetBoolField(TEXT("commander_can_issue_orders"),Commander->CanIssueCommanderOrders());
    UI->SetStringField(TEXT("controller"),PC?PC->GetClass()->GetPathName():TEXT(""));UI->SetBoolField(TEXT("hud"),PC && PC->GetHUD());
    UI->SetBoolField(TEXT("possessed"),PC && PC->GetPawn());
    const auto Ids=[](const TArray<FGuid>& Values)
    {TArray<TSharedPtr<FJsonValue>> Out;for (const auto& V:Values) Out.Add(MakeShared<FJsonValueString>(V.ToString(EGuidFormats::Digits)));return Out;};
    if (const auto* Placement=PC?PC->FindComponentByClass<UGuLiBuildingPlacementComponent>():nullptr)
    {
        UI->SetArrayField(TEXT("pending_placement_events"),Ids(Placement->GetPendingPlacementEvents()));
        UI->SetNumberField(TEXT("placement_receipt_count"),double(Placement->GetPlacementReceiptCount()));
        auto Last=MakeShared<FJsonObject>();const auto& Receipt=Placement->GetLastPlacementReceipt();
        Last->SetNumberField(TEXT("request"),Receipt.ClientRequestId);Last->SetBoolField(TEXT("accepted"),Receipt.WasAccepted());
        Last->SetNumberField(TEXT("rejection"),uint8(Receipt.RejectReason));UI->SetObjectField(TEXT("placement_receipt"),Last);
    }
    if (const auto* HUD=PC?Cast<AGuLiCommanderHUD>(PC->GetHUD()):nullptr)
    {
        UI->SetNumberField(TEXT("bindings_generation"),double(HUD->GetLocalBindingGeneration()));
        UI->SetNumberField(TEXT("building_feedback_receipts"),double(HUD->GetBuildingFeedbackReceiptCount()));
    }
    if (const auto* Skills=PS?PS->GetCommanderSkills():nullptr)
        UI->SetArrayField(TEXT("pending_skill_requests"),Ids(Skills->GetPendingHybridRequests()));
    TArray<TSharedPtr<FJsonValue>> SkillReplies,ChoiceReplies;
    for (const auto& [Event,Count]:SceneFinalSkillReplyCounts)
    {auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("event"),Event.ToString(EGuidFormats::Digits));V->SetNumberField(TEXT("final_count"),Count);SkillReplies.Add(MakeShared<FJsonValueObject>(V));}
    for (const auto& [Event,Count]:SceneChoiceReplyCounts)
    {
        auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("event"),Event.ToString(EGuidFormats::Digits));V->SetNumberField(TEXT("final_count"),Count);
        const auto& Reply=SceneChoiceReplies.FindChecked(Event);V->SetBoolField(TEXT("committed"),Reply.bCommitted);
        V->SetNumberField(TEXT("revision"),double(Reply.BuildRevision));ChoiceReplies.Add(MakeShared<FJsonValueObject>(V));
    }
    UI->SetArrayField(TEXT("skill_final_receipts"),SkillReplies);UI->SetArrayField(TEXT("choice_final_receipts"),ChoiceReplies);
    if (const auto* Ship=PC?Cast<AGuLiStrikeShip>(PC->GetPawn()):nullptr)
    {
        if (const auto* Assembly=Ship->FindComponentByClass<UGuLiShipAssemblyComponent>())
            UI->SetArrayField(TEXT("pending_choice_requests"),Ids(Assembly->GetPendingHybridChoices()));
        TArray<TSharedPtr<FJsonValue>> Channels,Receipts;
        for (const auto& C:Ship->GetGroupAbilityConfig().WeaponChannels)
        {
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("subject"),C.Binding.SubjectId.ToString());
            J->SetStringField(TEXT("slot"),C.Binding.SlotId.ToString());J->SetNumberField(TEXT("kind"),uint8(C.Kind));
            J->SetBoolField(TEXT("enabled"),C.bEnabled);J->SetStringField(TEXT("skill"),C.SkillId.ToString());
            const auto* Hangar=Ship->GetHangarCapability();const auto* Grant=Hangar?Hangar->FindConfiguredGrant(C.Binding):nullptr;
            J->SetBoolField(TEXT("manual_input"),Grant && Grant->InputTag.IsValid());
            J->SetStringField(TEXT("input_tag"),Grant?Grant->InputTag.ToString():TEXT(""));
            Channels.Add(MakeShared<FJsonValueObject>(J));
        }
        for (const auto& [Id,R]:Ship->GetGameplayWeaponResults())
        {
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),Id.ToString(EGuidFormats::Digits));
            J->SetNumberField(TEXT("launched"),R.LaunchedCount);J->SetNumberField(TEXT("rejection"),uint8(R.RejectReason));Receipts.Add(MakeShared<FJsonValueObject>(J));
        }
        UI->SetArrayField(TEXT("weapon_channels"),Channels);UI->SetArrayField(TEXT("weapon_receipts"),Receipts);
    }
    if (PS)
    {
        if (const auto* Build=PS->FindComponentByClass<UGuLiShipBuildComponent>())
        {
            TArray<TSharedPtr<FJsonValue>> Choices;for (const auto& C:Build->QueryChoices())
            {auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("node"),C.NodeId.ToString());V->SetBoolField(TEXT("available"),C.bAvailable);V->SetStringField(TEXT("reason"),C.Reason);Choices.Add(MakeShared<FJsonValueObject>(V));}
            UI->SetArrayField(TEXT("ship_choices"),Choices);UI->SetNumberField(TEXT("ship_build_revision"),double(Build->GetBuildState().BuildRevision));
        }
        if (const auto* Skills=PS->GetCommanderSkills())
        {
            TArray<TSharedPtr<FJsonValue>> Globals;for (const auto& S:Skills->GetGlobalSkills())
            {auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("skill"),S.SkillId.ToString());V->SetNumberField(TEXT("ready_at"),S.ReadyAt);V->SetStringField(TEXT("cast"),S.ActiveCastId.ToString(EGuidFormats::Digits));Globals.Add(MakeShared<FJsonValueObject>(V));}
            UI->SetArrayField(TEXT("global_skills"),Globals);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Widgets;TArray<UUserWidget*> ActiveWidgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(),ActiveWidgets,UUserWidget::StaticClass(),false);
    for (const auto* W:ActiveWidgets) if (W && W->IsVisible())
    {auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("class"),W->GetClass()->GetPathName());V->SetBoolField(TEXT("in_viewport"),W->IsInViewport());Widgets.Add(MakeShared<FJsonValueObject>(V));}
    UI->SetArrayField(TEXT("widgets"),Widgets);
    if (PC)
    {
        if (const auto* C=PC->FindComponentByClass<UGuLiRogueCardPresentation>())
        {
            UI->SetBoolField(TEXT("rogue_open"),C->IsOpen());UI->SetBoolField(TEXT("rogue_pending"),C->IsChoicePending());
            UI->SetNumberField(TEXT("rogue_phase"),C->GetPresentationPhase());
            UI->SetBoolField(TEXT("rogue_committed"),C->IsChoiceCommitted());UI->SetStringField(TEXT("rogue_session"),C->GetSessionId().ToString(EGuidFormats::Digits));
            TArray<TSharedPtr<FJsonValue>> Cards;for (const auto& Id:C->GetCandidateIds()) Cards.Add(MakeShared<FJsonValueString>(Id));UI->SetArrayField(TEXT("cards"),Cards);
        }
        if (const auto* T=PC->FindComponentByClass<UGuLiTeleportInputComponent>())
        {
            const auto S=T->QueryState();auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("cast"),S.CastId.ToString(EGuidFormats::Digits));
            V->SetNumberField(TEXT("phase"),uint8(S.Phase));V->SetNumberField(TEXT("members"),S.ParticipantCount);V->SetNumberField(TEXT("revision"),S.Revision);
            V->SetBoolField(TEXT("landed"),S.bLanded);V->SetArrayField(TEXT("source"),Point(S.Source));V->SetArrayField(TEXT("destination"),Point(S.Destination));UI->SetObjectField(TEXT("teleport"),V);
        }
        if (const auto* S=PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
        {
#if !UE_BUILD_SHIPPING
            UI->SetObjectField(TEXT("commands"),S->GetReviewCommandEvidence());
#endif
            TArray<TSharedPtr<FJsonValue>> Selection;for (const auto& C:S->GetSelectionState().Cohorts) for (const auto& Id:C.MemberIds) Selection.Add(MakeShared<FJsonValueNumber>(Id.Value));
            UI->SetArrayField(TEXT("selected_soldiers"),Selection);
            TArray<TSharedPtr<FJsonValue>> Actors;
            for (const auto Id:S->GetSelectionState().ActorIds) Actors.Add(MakeShared<FJsonValueNumber>(Id.Value));
            UI->SetArrayField(TEXT("selected_actors"),Actors);UI->SetNumberField(TEXT("selection_revision"),S->GetSelectionState().SelectionRevision);
            TArray<TSharedPtr<FJsonValue>> SelectedObjects;
            for (const auto& K:S->GetSelectionState().Objects) SelectedObjects.Add(MakeShared<FJsonValueString>(K.StableId.ToString(EGuidFormats::Digits)));
            UI->SetArrayField(TEXT("selected_objects"),SelectedObjects);
        }
    }
    Json->SetObjectField(TEXT("scene_ui"),UI);
    TArray<TSharedPtr<FJsonValue>> Fields;
    for (TActorIterator<AGuLiTeleportFieldActor> It(GetWorld());It;++It)
    {
        const auto S=It->GetCastState();auto F=MakeShared<FJsonObject>();
        F->SetStringField(TEXT("cast"),S.CastId.ToString(EGuidFormats::Digits));F->SetBoolField(TEXT("initialized"),S.CastId.IsValid() && S.Config.IsValid());
        F->SetNumberField(TEXT("phase"),uint8(S.Phase));F->SetNumberField(TEXT("members"),S.ParticipantCount);
        F->SetStringField(TEXT("class"),It->GetClass()->GetPathName());Fields.Add(MakeShared<FJsonValueObject>(F));
    }
    Json->SetArrayField(TEXT("scene_fields"),Fields);
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H) return;
    TSet<FString> BlueprintClasses;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (It->GetClass()->ClassGeneratedBy) BlueprintClasses.Add(It->GetClass()->GetPathName());
    TArray<TSharedPtr<FJsonValue>> AuthoredClasses;
    for (const auto& Class:BlueprintClasses) AuthoredClasses.Add(MakeShared<FJsonValueString>(Class));
    Json->SetArrayField(TEXT("authored_actor_classes"),AuthoredClasses);
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);TArray<TSharedPtr<FJsonValue>> ObjectViews;
    const auto* Inventories=GetWorld()->GetSubsystem<UGuLiResourceInventorySubsystem>();
    auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    for (const auto& O:Objects)
    {
        // Terminal transient effects/work are evidenced by the bounded result
        // journal; writing their entire historical directory every second grows
        // with match length and can itself stall a review session.
        if (O.LifeState==3 && (O.OwnershipPolicy==3 || O.OwnershipPolicy==5)) continue;
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),O.Key.StableId.ToString(EGuidFormats::Digits));J->SetNumberField(TEXT("kind"),uint8(O.Key.Kind));
        J->SetNumberField(TEXT("ownership_policy"),O.OwnershipPolicy);
        J->SetNumberField(TEXT("generation"),O.Key.LifeGeneration);J->SetNumberField(TEXT("ordinal"),O.Ordinal);J->SetStringField(TEXT("definition"),O.Definition.ToString());
        J->SetNumberField(TEXT("team"),uint8(O.Team));J->SetNumberField(TEXT("life"),O.LifeState);J->SetNumberField(TEXT("health"),O.Health.Health);
        J->SetNumberField(TEXT("shield"),O.Health.Shield);J->SetNumberField(TEXT("health_revision"),O.Health.Revision);
        J->SetStringField(TEXT("controller"),O.Controller.ToString(EGuidFormats::Digits));J->SetStringField(TEXT("simulator"),O.SimulationOwner.ToString(EGuidFormats::Digits));
        J->SetNumberField(TEXT("owner_version"),double(O.OwnerEpoch));J->SetNumberField(TEXT("control_version"),double(O.ControlEpoch));
        J->SetBoolField(TEXT("local_simulation"),H->HasSimulation(O.Key,true));J->SetBoolField(TEXT("completed"),O.bCompleted);
        J->SetBoolField(TEXT("supplied"),O.bSupplied);J->SetBoolField(TEXT("phased"),O.bPhased);J->SetBoolField(TEXT("dormant"),O.bDormant);
        J->SetNumberField(TEXT("born_at"),double(O.BornAt));J->SetStringField(TEXT("spawn_event"),O.SpawnEvent.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("parent"),O.Parent.StableId.ToString(EGuidFormats::Digits));
        auto* Actor=H->FindBoundActor(O.Key);FVector Location=Actor?Actor->GetActorLocation():O.Location;
        bool Entity=IsValid(Actor);
        J->SetStringField(TEXT("actor_class"),Actor?Actor->GetClass()->GetPathName():TEXT(""));
        if (O.Key.Kind==EGuLiHybridObjectKind::Soldier && Authority)
        {
            EGuLiTeam Team;uint16 Type;Entity=Authority->GetTaskSoldierInfo(FGuLiSoldierId(O.Ordinal),Team,Type,Location);
            J->SetBoolField(TEXT("preparation_locked"),Authority->IsSoldierExternallyLocked(FGuLiSoldierId(O.Ordinal)));
        }
        J->SetBoolField(TEXT("entity"),Entity);J->SetArrayField(TEXT("position"),Point(Location));
        if (Actor)
        {J->SetArrayField(TEXT("velocity"),Point(Actor->GetVelocity()));J->SetNumberField(TEXT("yaw"),Actor->GetActorRotation().Yaw);}
        auto Caps=MakeShared<FJsonObject>();for (const auto& [Name,S]:O.Capabilities)
        {auto C=MakeShared<FJsonObject>();C->SetNumberField(TEXT("version"),S.Version);C->SetStringField(TEXT("data"),FBase64::Encode(S.Data));Caps->SetObjectField(Name.ToString(),C);}
        J->SetObjectField(TEXT("capabilities"),Caps);
        if (const auto* Inv=Inventories?Inventories->FindInventory(O.Key):nullptr)
        {
            auto I=MakeShared<FJsonObject>();const auto Cargo=Inv->GetCargo();I->SetNumberField(TEXT("blue"),Cargo.Blue);I->SetNumberField(TEXT("red"),Cargo.Red);
            I->SetNumberField(TEXT("queued_raw"),Inv->GetQueuedRawAmount());I->SetBoolField(TEXT("cargo"),Inv->bCargo);I->SetBoolField(TEXT("processing"),Inv->bProcessing);
            I->SetStringField(TEXT("pending"),Inv->GetPendingEvent().ToString(EGuidFormats::Digits));I->SetNumberField(TEXT("revision"),double(Inv->GetAppliedRevision()));J->SetObjectField(TEXT("inventory"),I);
        }
        if (const auto* Production=Actor?Actor->FindComponentByClass<UGuLiBuildingProductionComponent>():nullptr)
        {J->SetNumberField(TEXT("production_seconds"),Production->GetCycleSeconds());J->SetBoolField(TEXT("producing"),Production->IsProducing());}
        ObjectViews.Add(MakeShared<FJsonValueObject>(J));
    }
    Json->SetArrayField(TEXT("scene_objects"),ObjectViews);
}
