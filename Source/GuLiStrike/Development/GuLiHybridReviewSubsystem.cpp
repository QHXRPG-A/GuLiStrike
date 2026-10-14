#include "Development/GuLiHybridReviewSubsystem.h"
#include "Development/GuLiHybridNetworkReviewActor.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "DynamicRHI.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechRocketComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/CombatEffects/GuLiFlightAcceptanceSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Async/Async.h"

namespace
{
bool PersistReviewJson(const FString& File,const TSharedRef<FJsonObject>& Json)
{
    FString Text;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiReview_SerializeJson);
        if (!FJsonSerializer::Serialize(Json,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))) return false;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiReview_WriteFile);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);const FString Temporary=File+TEXT(".pending");
    return FFileHelper::SaveStringToFile(Text,*Temporary,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && IFileManager::Get().Move(*File,*Temporary,true);
}
}

bool UGuLiHybridReviewSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
    FString RunId; const auto* World=Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),RunId) && !RunId.IsEmpty();
#else
    return false;
#endif
}
void UGuLiHybridReviewSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),Run);
    FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceDirectory="),Directory);
    Directory=FPaths::ConvertRelativePathToFull(Directory); FPaths::NormalizeDirectoryName(Directory);
    FGuid Id;
    if (!FGuid::Parse(Run,Id) || !Id.IsValid() || !FPaths::IsUnderDirectory(Directory,FPaths::ProjectDir()/TEXT("TestResults")))
    { Failure=TEXT("Acceptance identity or output directory is invalid"); return; }
    bStandby=FParse::Param(FCommandLine::Get(),TEXT("GuLiAcceptanceStandby"));
}
TStatId UGuLiHybridReviewSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiHybridReview,STATGROUP_Tickables); }
void UGuLiHybridReviewSubsystem::ObserveBlueprintExecution(const UObject& Context,FName Function,TOptional<bool> Disabled)
{
    static const bool Enabled=[]()
    {
        FString Case;
        return FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),Case) && Case!=TEXT("performance");
    }();
    if (!Enabled || !Context.GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)) return;
    const FString Class=Context.GetClass()->GetPathName(),Name=Function.ToString(),Key=Class+TEXT("::")+Name;
    if (!BlueprintExecutions.Contains(Key) && BlueprintExecutions.Num()>=256)
    {Failure=TEXT("Authored-call evidence capacity exceeded");return;}
    auto& V=BlueprintExecutions.FindOrAdd(Key);V.Class=Class;V.Function=Name;++V.Calls;
    if (Disabled.IsSet()) {if (Disabled.GetValue()) ++V.Disabled;else ++V.Enabled;}
}
bool UGuLiHybridReviewSubsystem::Write(const TCHAR* Name,const TSharedRef<FJsonObject>& Json)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiReview_WriteJson);
    Json->SetNumberField(TEXT("schema"),1); Json->SetStringField(TEXT("run_id"),Run);
    Json->SetNumberField(TEXT("pid"),FPlatformProcess::GetCurrentProcessId());
    return PersistReviewJson(Directory/Name,Json);
}
void UGuLiHybridReviewSubsystem::ReadControl()
{
    const FString File=FPaths::GetPath(Directory)/TEXT("control.json");
    // The launcher publishes whole files by replacement. Allow that replacement
    // while reading and obtain both length and bytes from the same file handle.
    // A transient sharing/open failure is retried on the next network tick.
    auto Opened=FPlatformFileManager::Get().GetPlatformFile().OpenRead(*File,IPlatformFile::EOpenReadFlags::AllowWrite|IPlatformFile::EOpenReadFlags::AllowDelete);
    if (!Opened.HasValue()) return;
    auto Handle=Opened.StealValue(); const int64 Size=Handle->Size();
    if (Size<=0 || Size>32768) { Failure=TEXT("Acceptance control size is invalid"); return; }
    TArray<uint8> Bytes; Bytes.SetNumUninitialized(int32(Size));
    if (!Handle->Read(Bytes.GetData(),Size)) return;
    Handle.Reset();
    const int32 Bom=Size>=3 && Bytes[0]==0xef && Bytes[1]==0xbb && Bytes[2]==0xbf ? 3 : 0;
    FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+Bom),int32(Size)-Bom);
    FString Text; TSharedPtr<FJsonObject> Json; double Schema=0; FString Identity,Next;
    Text=FString(Decoded.Length(),Decoded.Get());
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json) || !Json
        || !Json->TryGetNumberField(TEXT("schema"),Schema) || (Schema!=1 && Schema!=2) || !Json->TryGetStringField(TEXT("run_id"),Identity)
        || Identity!=Run || !Json->TryGetStringField(TEXT("phase"),Next))
    { Failure=TEXT("Stale or malformed acceptance control"); return; }
    if (Next!=TEXT("ship_equipment") && Next!=TEXT("prepare") && Next!=TEXT("roles") && Next!=TEXT("ground_combat") && Next!=TEXT("ground_movement") && Next!=TEXT("ship_movement") && Next!=TEXT("gameplay") && Next!=TEXT("steady") && Next!=TEXT("capture")
        && Next!=TEXT("flights") && Next!=TEXT("handoff")
        && Next!=TEXT("finish_capture") && Next!=TEXT("stop")) { Failure=TEXT("Unsupported acceptance phase"); return; }
    if (Schema==2)
    {
        FString NewStage,NewScenario;double Number=0;
        if (!Json->TryGetStringField(TEXT("stage_id"),NewStage) || NewStage.IsEmpty() || NewStage.Len()>96
            || !Json->TryGetStringField(TEXT("scenario"),NewScenario) || NewScenario.IsEmpty() || NewScenario.Len()>64
            || !Json->TryGetNumberField(TEXT("stage_number"),Number) || Number<StageNumber || Number>10000
            || Number!=FMath::FloorToDouble(Number) || (Number==StageNumber && NewStage!=StageId))
        {Failure=TEXT("Stale or malformed integrated review stage");return;}
        if (Number>StageNumber) BeginReviewStage(NewStage,uint32(Number));
        Scenario=NewScenario;
    }
    Phase=Next;
    ReadSceneActions(*Json);
    Json->TryGetBoolField(TEXT("enable_mining"),bAutonomousMining);
    const TArray<TSharedPtr<FJsonValue>>* BuildingRequests=nullptr;
    if (Json->TryGetArrayField(TEXT("building_orders"),BuildingRequests))
    {
        if (BuildingRequests->Num()>8) {Failure=TEXT("Too many building scene orders"); return;}
        for (const auto& Value:*BuildingRequests)
        {
            auto Object=Value->AsObject(); FBuildingSceneOrder Order; FString PlayerText,Operation; double Type=0;
            if (!Object || !Object->TryGetStringField(TEXT("player"),PlayerText) || !FGuid::Parse(PlayerText,Order.Player) || !Order.Player.IsValid()
                || !Object->TryGetStringField(TEXT("operation"),Operation) || Operation.IsEmpty() || Operation.Len()>64
                || !Object->TryGetNumberField(TEXT("type"),Type) || Type<1 || Type>MAX_int32 || Type!=FMath::FloorToDouble(Type)
                || !Object->TryGetNumberField(TEXT("x"),Order.Location.X) || !Object->TryGetNumberField(TEXT("y"),Order.Location.Y)
                || !Object->TryGetNumberField(TEXT("z"),Order.Location.Z) || Order.Location.ContainsNaN()
                || !Object->TryGetNumberField(TEXT("yaw"),Order.Yaw) || !FMath::IsFinite(Order.Yaw))
            {Failure=TEXT("Malformed production building scene input"); return;}
            Order.DefinitionId=int32(Type);
            if (const auto* Old=BuildingOrders.Find(Operation))
            {
                if (Old->Player!=Order.Player || Old->Location!=Order.Location || Old->Yaw!=Order.Yaw || Old->DefinitionId!=Order.DefinitionId)
                {Failure=TEXT("Building scene identity changed arguments"); return;}
            }
            else if (BuildingOrders.Num()<32) BuildingOrders.Add(Operation,Order);
            else {Failure=TEXT("Building scene operation capacity exceeded"); return;}
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* EngineerRequests=nullptr;
    if (Json->TryGetArrayField(TEXT("engineer_orders"),EngineerRequests) || Json->TryGetArrayField(TEXT("unit_orders"),EngineerRequests))
    {
        if (EngineerRequests->Num()>8) {Failure=TEXT("Too many engineering scene orders"); return;}
        for (const auto& Value:*EngineerRequests)
        {
            auto Object=Value->AsObject(); FEngineerSceneOrder Order; FString PlayerText; double Command=0;
            const TArray<TSharedPtr<FJsonValue>>* Actors=nullptr;
            if (!Object || !Object->TryGetStringField(TEXT("player"),PlayerText) || !FGuid::Parse(PlayerText,Order.Player) || !Order.Player.IsValid()
                || !Object->TryGetNumberField(TEXT("command"),Command) || Command<1 || Command>MAX_uint32 || Command!=FMath::FloorToDouble(Command)
                || !Object->TryGetNumberField(TEXT("x"),Order.Destination.X) || !Object->TryGetNumberField(TEXT("y"),Order.Destination.Y)
                || !Object->TryGetNumberField(TEXT("z"),Order.Destination.Z) || Order.Destination.ContainsNaN()
                || !Object->TryGetArrayField(TEXT("actors"),Actors) || Actors->IsEmpty() || Actors->Num()>64)
            {Failure=TEXT("Malformed engineering scene order"); return;}
            Order.Command=uint32(Command);
            FString Intent;double Cluster=0;
            if (Object->TryGetStringField(TEXT("intent"),Intent))
            {
                if (Intent!=TEXT("Move") && Intent!=TEXT("Mine") && Intent!=TEXT("Return")) {Failure=TEXT("Unsupported formal unit input");return;}
                Order.Intent=FName(Intent);
            }
            if (Object->TryGetNumberField(TEXT("cluster"),Cluster))
            {
                if (Cluster<1 || Cluster>MAX_uint16 || Cluster!=FMath::FloorToDouble(Cluster) || Order.Intent!=TEXT("Mine"))
                {Failure=TEXT("Malformed formal mining target");return;}
                Order.Cluster=uint16(Cluster);
            }
            for (const auto& Actor:*Actors)
            {
                double Id=0; if (!Actor->TryGetNumber(Id) || Id<1 || Id>MAX_uint32 || Id!=FMath::FloorToDouble(Id) || Order.Actors.Contains(uint32(Id)))
                {Failure=TEXT("Malformed engineering scene selection"); return;}
                Order.Actors.Add(uint32(Id));
            }
            Order.Actors.Sort();
            if (const auto* Old=EngineerOrders.Find(Order.Command))
            {
                if (Old->Player!=Order.Player || Old->Destination!=Order.Destination || Old->Actors!=Order.Actors || Old->Intent!=Order.Intent || Old->Cluster!=Order.Cluster)
                {Failure=TEXT("Engineering command identity changed arguments"); return;}
            }
            else if (EngineerOrders.Num()<64) EngineerOrders.Add(Order.Command,MoveTemp(Order));
            else {Failure=TEXT("Engineering scene order capacity exceeded"); return;}
        }
    }

    if (Phase==TEXT("roles"))
    {
        double Red=0,Blue=0;
        if (Json->TryGetNumberField(TEXT("expected_red"),Red) && Json->TryGetNumberField(TEXT("expected_blue"),Blue)
            && Red>=1 && Red<=3 && Blue>=1 && Blue<=3 && Red==FMath::FloorToDouble(Red) && Blue==FMath::FloorToDouble(Blue))
        { RoleExpectedRed=int32(Red); RoleExpectedBlue=int32(Blue); }
    }
    GroundSceneInputs.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Inputs=nullptr;
    if (Phase==TEXT("ground_combat") && Json->TryGetArrayField(TEXT("ground_inputs"),Inputs))
    {
        if (Inputs->Num()>6) { Failure=TEXT("Too many scene Ground inputs"); return; }
        TSet<FGuid> Players;
        for (const auto& Value:*Inputs)
        {
            const auto V=Value->AsObject(); FGroundSceneInput Input; FString PlayerText;
            if (!V || !V->TryGetStringField(TEXT("player"),PlayerText) || !FGuid::Parse(PlayerText,Input.Player)
                || !Input.Player.IsValid() || Players.Contains(Input.Player)
                || !V->TryGetNumberField(TEXT("move_x"),Input.Axis.X) || !V->TryGetNumberField(TEXT("move_y"),Input.Axis.Y)
                || !V->TryGetNumberField(TEXT("aim_x"),Input.Aim.X) || !V->TryGetNumberField(TEXT("aim_y"),Input.Aim.Y)
                || !V->TryGetNumberField(TEXT("aim_z"),Input.Aim.Z) || Input.Axis.ContainsNaN() || Input.Aim.ContainsNaN()
                || Input.Axis.SizeSquared()>1.0001 || !V->TryGetBoolField(TEXT("sprint"),Input.Sprint)
                || !V->TryGetBoolField(TEXT("fire"),Input.Fire) || !V->TryGetBoolField(TEXT("rocket"),Input.Rocket))
            { Failure=TEXT("Malformed scene Ground input"); return; }
            Players.Add(Input.Player); GroundSceneInputs.Add(Input);
        }
    }
}
void UGuLiHybridReviewSubsystem::EngineerCommandResolved(const FGuLiCommandAck& Ack)
{
    if (auto* Order=EngineerOrders.Find(Ack.ClientCommandId); Order && Ack.CommandKind==EGuLiCommandKind::Move)
    {Order->Ack=Ack; Order->Resolved=true; ++Order->Receipts;}
    for (auto& [Id,A]:SceneActions)
    {
        double Command=0;
        if (A.Kind!=TEXT("unit_task") || A.Status!=TEXT("submitted") || !A.Result->TryGetNumberField(TEXT("command"),Command)
            || Command!=Ack.ClientCommandId) continue;
        A.Status=TEXT("completed");A.Result->SetNumberField(TEXT("code"),uint8(Ack.Result));
        A.Result->SetBoolField(TEXT("accepted"),Ack.IsAccepted());
        if (const auto* PC=GetWorld()->GetFirstPlayerController())
            if (const auto* Sync=PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
                A.Result->SetStringField(TEXT("feedback"),Sync->GetLastTaskFeedback());
        TArray<TSharedPtr<FJsonValue>> Cohorts;int32 AcceptedSoldiers=0;
        for (const auto& C:Ack.CohortResults)
        {
            auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("accepted"),C.GetAcceptedMemberCount());V->SetNumberField(TEXT("requested"),C.MemberCount);
            AcceptedSoldiers+=C.GetAcceptedMemberCount();
            Cohorts.Add(MakeShared<FJsonValueObject>(V));
        }
        A.Result->SetArrayField(TEXT("cohorts"),Cohorts);
        A.Result->SetNumberField(TEXT("accepted_soldiers"),AcceptedSoldiers);
    }
}
void UGuLiHybridReviewSubsystem::AdvanceEngineerOrders()
{
    if (bStandby || Phase!=TEXT("gameplay") || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    auto* PC=GetWorld()->GetFirstPlayerController(); const auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* Sync=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
    if (!Player || !Player->IsCommander() || !Sync || !Sync->IsSoldierStreamReady()) return;
    if (const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); H && (H->IsBattlePaused() || !H->CanPublishFacts())) return;
    if (EngineerSync.Get()!=Sync)
    {
        if (EngineerSync.IsValid()) EngineerSync->OnCommandAckChanged.RemoveAll(this);
        EngineerSync=Sync; Sync->OnCommandAckChanged.AddUObject(this,&ThisClass::EngineerCommandResolved);
    }
    for (auto& Pair:EngineerOrders)
    {
        auto& Order=Pair.Value; if (Order.Player!=Player->GetPlayerGuid() || Order.TaskSent) continue;
        if (!Order.SelectionSent)
        {
            FBox Bounds(ForceInit); int32 Found=0;
            for (TActorIterator<APawn> It(GetWorld());It;++It)
                if (const auto* Vehicle=Cast<IGuLiEngineeringVehicle>(*It);Vehicle && Order.Actors.Contains(Vehicle->GetStableActorId().Value) && Vehicle->GetTeam()==Player->GetTeam())
                {Bounds+=It->GetActorLocation(); ++Found;}
            if (Found!=Order.Actors.Num()) {Failure=TEXT("Engineering scene selection has missing real actors"); return;}
            Bounds=Bounds.ExpandBy(FVector(400,400,0)); const FVector Center=Bounds.GetCenter();
            FGuLiSelectionRequest Request; Request.ClientRequestId=Order.Command; Request.Kind=EGuLiSelectionKind::Box;
            Request.Center=Center; Request.RayOrigin=Center+FVector(0,0,20000); Request.RayDirection=FVector::DownVector;
            auto Ray=[&](double X,double Y){return (FVector(X,Y,Center.Z)-Request.RayOrigin).GetSafeNormal();};
            Request.BoxTopLeftRay=Ray(Bounds.Min.X,Bounds.Min.Y); Request.BoxTopRightRay=Ray(Bounds.Max.X,Bounds.Min.Y);
            Request.BoxBottomRightRay=Ray(Bounds.Max.X,Bounds.Max.Y); Request.BoxBottomLeftRay=Ray(Bounds.Min.X,Bounds.Max.Y);
            Order.SelectionSent=true; Sync->SubmitOrderedSelection(Request); return;
        }
        TArray<uint32> Selected; for (auto Id:Sync->GetSelectionState().ActorIds) Selected.Add(Id.Value); Selected.Sort();
        // A combined scene can put builders or newly produced units inside
        // the same box. Narrow that confirmed selection through the ordinary
        // portrait input instead of waiting forever for an exact rectangle.
        for (const uint32 Id:Selected) if (!Order.Actors.Contains(Id))
        {
            FGuLiPanelSelectionRequest Remove;Remove.ActorId=FGuLiControllableActorId(Id);
            Remove.Action=EGuLiPanelSelectionAction::Remove;
            if (!Sync->SubmitPanelSelection(Remove)) Failure=TEXT("Could not narrow the confirmed engineering selection");
            return;
        }
        for (const auto& Cohort:Sync->GetSelectionState().Cohorts) if (!Cohort.MemberIds.IsEmpty())
        {
            FGuLiPanelSelectionRequest Remove;Remove.SoldierId=Cohort.MemberIds[0];
            Remove.Action=EGuLiPanelSelectionAction::RemoveGroup;
            if (!Sync->SubmitPanelSelection(Remove)) Failure=TEXT("Could not remove the unrelated Mass portrait group");
            return;
        }
        if (!Sync->GetSelectionState().Objects.IsEmpty())
        {
            FGuLiPanelSelectionRequest Remove;Remove.Object=Sync->GetSelectionState().Objects[0];
            Remove.Action=EGuLiPanelSelectionAction::Remove;
            if (!Sync->SubmitPanelSelection(Remove)) Failure=TEXT("Could not remove the unrelated object portrait");
            return;
        }
        if (Selected!=Order.Actors)
        {Failure=FString::Printf(TEXT("Engineering selection missed a requested actor for command %u"),Order.Command);return;}
        FGuLiUnitTaskCommand Command; Command.CommandId=Order.Command;
        Command.Kind=Order.Intent==TEXT("Return")?EGuLiUnitTaskKind::ReturnToFactory:EGuLiUnitTaskKind::Move;
        Command.Target=Order.Destination;Command.ClusterId=Order.Cluster;Command.TargetIntent=Order.Intent;Command.bGroundMoveOnly=Order.Intent==TEXT("Move");
        if (Order.Intent==TEXT("Mine"))
        {
            const auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();const auto* Config=Resources?Resources->GetEconomyConfig():nullptr;
            const auto* Policy=Config?GuLiCommanderBehavior::FindPolicy(*GetWorld(),Config->MiningVehicleUnitTypeId):nullptr;
            if (!Policy || Policy->Behavior!=EGuLiCommanderBehavior::Mining) {Failure=TEXT("Formal mining StateTree policy is unavailable");return;}
            Command.Kind=EGuLiUnitTaskKind::Special;Command.SpecialTaskId=Policy->GetWireId();
        }
        Order.TaskSent=true; Sync->SubmitOrderedTask(Command); return;
    }
}
void UGuLiHybridReviewSubsystem::AdvanceBuildingOrders()
{
#if WITH_EDITOR
    if (bStandby || Phase!=TEXT("gameplay") || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    auto* PC=GetWorld()->GetFirstPlayerController(); const auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    APawn* Pawn=PC?PC->GetPawn():nullptr;
    auto* Placement=Pawn?Pawn->FindComponentByClass<UGuLiBuildingPlacementComponent>():nullptr;
    if (!Placement && PC) Placement=PC->FindComponentByClass<UGuLiBuildingPlacementComponent>();
    if (!Player || !Player->IsBattleReady() || !Placement) return;
    if (const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); H && (H->IsBattlePaused() || !H->CanPublishFacts())) return;
    for (auto& Pair:BuildingOrders)
    {
        auto& Order=Pair.Value;
        if (Order.Player==Player->GetPlayerGuid() && !Order.Submitted)
        {
            if (!Placement->EditorQAFindClearDefinition(Order.DefinitionId,Order.Location,float(Order.Yaw),Order.Candidate))
            {Failure=TEXT("Production spatial policy found no legal Ground placement within range"); return;}
            Order.Submitted=Placement->EditorQASubmitDefinition(Order.DefinitionId,Order.Candidate,float(Order.Yaw));
        }
    }
#endif
}
void UGuLiHybridReviewSubsystem::AdvanceGroundReview()
{
    if (bStandby || GetWorld()->GetNetMode()==NM_DedicatedServer || Phase!=TEXT("ground_movement") || bGroundDone) return;
    auto* PC=GetWorld()->GetFirstPlayerController(); auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (Hybrid && GroundReviewTick==MAX_uint64)
    { GroundReviewTick=Hybrid->GetLogicalTick(); GroundReviewEpoch=GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch(); }

    if (!Player || Player->GetBattleRole()!=EGuLiCommanderRole::Ground) return;
    auto* Pawn=Cast<AGuLiGroundMechCharacter>(PC->GetPawn());
    if (!Player->IsBattleReady() || (Hybrid && Hybrid->IsBattlePaused())) return;
    if (!Pawn || !Pawn->GetRocketJump()->IsConfigured()) { Failure=TEXT("Ground role is missing its production character/rocket configuration"); return; }
    const double Now=UGuLiHybridActorComponent::BattleSeconds(Pawn);
    if (GroundStarted<0)
    { GroundStarted=Now; GroundStart=Pawn->GetActorLocation(); GroundStartFuel=GroundMinimumFuel=Pawn->GetRocketJump()->GetFuel(); }
    const double Elapsed=Now-GroundStarted;
    GroundMinimumFuel=FMath::Min(GroundMinimumFuel,double(Pawn->GetRocketJump()->GetFuel()));
    bGroundThrust|=Pawn->GetRocketJump()->IsThrusting();
    const auto Axis=Elapsed<5?FVector2D(0,1):FVector2D::ZeroVector;
    if (!Pawn->SubmitGameplayInput(Axis,Pawn->GetActorLocation()+FVector(10000,0,0),true,false,Elapsed>=1 && Elapsed<3))
    { Failure=TEXT("Production Ground semantic input was rejected"); return; }
    if (Elapsed<8) return;
    bGroundDone=true;
    bGroundPassed=FVector::DistSquared2D(GroundStart,Pawn->GetActorLocation())>100.*100.
        && bGroundThrust && GroundMinimumFuel<GroundStartFuel-.001;
    if (!bGroundPassed) Failure=TEXT("Ground movement/rocket fuel/thrust assertions failed");
}
void UGuLiHybridReviewSubsystem::ApplyGroundSceneInput()
{
    if (bStandby || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    auto* PC=GetWorld()->GetFirstPlayerController(); const auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* Ground=PC?Cast<AGuLiGroundMechCharacter>(PC->GetPawn()):nullptr;
    if (!Player || !Ground || Player->GetBattleRole()!=EGuLiCommanderRole::Ground) return;
    if (const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); Hybrid && Hybrid->IsBattlePaused()) return;
    const auto* Input=Phase==TEXT("ground_combat")?GroundSceneInputs.FindByPredicate([&](const auto& I){return I.Player==Player->GetPlayerGuid();}):nullptr;
    if (!Input && !bSceneGroundInputApplied) return;
    const bool Accepted=Ground->SubmitGameplayInput(Input?Input->Axis:FVector2D::ZeroVector,Input?Input->Aim:Ground->GetActorLocation()+FVector(1000,0,0),
        Input && Input->Sprint,Input && Input->Fire,Input && Input->Rocket);
    if (Accepted) ++SceneGroundInputsAccepted; else ++SceneGroundInputsRejected;
    bSceneGroundInputApplied=Input!=nullptr;
}
void UGuLiHybridReviewSubsystem::AdvanceShipReview()
{
    if (bStandby || GetWorld()->GetNetMode()==NM_DedicatedServer || Phase!=TEXT("ship_movement") || bShipDone) return;
    auto* PC=GetWorld()->GetFirstPlayerController(); auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (Hybrid && ShipReviewTick==MAX_uint64)
    { ShipReviewTick=Hybrid->GetLogicalTick(); ShipReviewEpoch=GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch(); }

    if (!Player || Player->GetBattleRole()!=EGuLiCommanderRole::Air) return;
    if (!Player->IsBattleReady() || (Hybrid && Hybrid->IsBattlePaused())) return;
    auto* Pawn=Cast<AGuLiStrikeShip>(PC->GetPawn());
    if (!Pawn || !Pawn->IsShipReady()) { Failure=TEXT("Air role is missing its configured production Ship"); return; }
    const double Now=UGuLiHybridActorComponent::BattleSeconds(Pawn);
    if (ShipStarted<0) { ShipStarted=Now; ShipStart=Pawn->GetActorLocation(); ShipStartYaw=Pawn->GetActorRotation().Yaw; }
    const double Elapsed=Now-ShipStarted;
    ShipMaximumSpeed=FMath::Max(ShipMaximumSpeed,Pawn->GetVelocity().Size()); bShipBoost|=Pawn->GetShipMovement()->IsBoostActive();
    const FVector Thrust=Elapsed<5?FVector(1,0,0):FVector::ZeroVector;
    if (!Pawn->SubmitGameplayFlightInput(Thrust,Elapsed<5?.25f:0.f,Elapsed>=1 && Elapsed<3))
    { Failure=TEXT("Production Air semantic flight input was rejected"); return; }
    if (Elapsed<8) return;
    bShipDone=true; bShipPassed=FVector::DistSquared(ShipStart,Pawn->GetActorLocation())>100.*100.
        && FMath::Abs(FMath::FindDeltaAngleDegrees(ShipStartYaw,double(Pawn->GetActorRotation().Yaw)))>2 && ShipMaximumSpeed>10 && bShipBoost;
    if (!bShipPassed) Failure=TEXT("Air thrust, turn, velocity or boost assertions failed");
}

void UGuLiHybridReviewSubsystem::Tick(float DeltaSeconds)
{
    if (Directory.IsEmpty()) return;
    const double Now=FPlatformTime::Seconds();
    if (Now>=NextRead) { NextRead=Now+.1; ReadControl(); }

    AdvanceGroundReview();
    ApplyGroundSceneInput();
    AdvanceShipReview();
    AdvanceEquipmentReview();
    AdvanceEngineerOrders();
    AdvanceBuildingOrders();
    AdvanceSceneActions();
    ApplyShipSceneInput();

    auto* Performance=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>();
    if (Phase==TEXT("capture") && !bCapture && !bWritten && UGuLiNetworkSettings::CanCapturePerformance(GetWorld()))
    {
        if (!Performance) Failure=TEXT("Monitored endpoint has no performance subsystem");
        else { Performance->BeginCapture(); bCapture=true; Started=Now; Samples=0; }
    }
    if (bCapture && Performance)
    {
        ++Samples; Performance->Record(TEXT("frame_wall_ms"),double(DeltaSeconds)*1000.);
        if (UGuLiNetworkSettings::CanPresent(GetWorld()))
            Performance->Record(TEXT("gpu_frame_ms"),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
    }
    if (Phase==TEXT("finish_capture") && bCapture && Performance)
    {
        Performance->EndCapture(); bCapture=false; Finished=Now;
        TSharedPtr<FJsonObject> Result;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Performance->GetCaptureJson()),Result) || !Result)
            Failure=TEXT("Native performance serialization failed");
        else
        {
            Result->SetNumberField(TEXT("capture_seconds"),Finished-Started); Result->SetNumberField(TEXT("sample_count"),double(Samples));
            Result->SetStringField(TEXT("failure"),Failure); bWritten=Write(TEXT("performance.json"),Result.ToSharedRef());
            if (!bWritten) Failure=TEXT("Cannot persist native performance evidence");
        }
    }
    if (Now<NextWrite && Phase!=TEXT("stop")) return;
    if (StateWrite.IsValid())
    {
        if (!StateWrite.IsReady() && Phase!=TEXT("stop")) return;
        if (!StateWrite.Get()) Failure=TEXT("Cannot persist runtime correctness evidence");
    }
    NextWrite=Now+1.;
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiReview_CollectEvidence);
    const uint64 SnapshotStarted=FPlatformTime::Cycles64();
    TSharedPtr<FJsonObject> Json=MakeShared<FJsonObject>();
    if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
        Json=Hybrid->GetReviewEvidenceSnapshot();
    else Json->SetBoolField(TEXT("ready"),bStandby);
    if (!Json) { Failure=TEXT("Invalid native runtime evidence"); Json=MakeShared<FJsonObject>(); }
    if (auto* Session=GetWorld()->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>())
        if (UGuLiNetworkSettings::IsHybrid(GetWorld())) Json->SetObjectField(TEXT("network"),Session->GetNetworkCaptureSnapshot());
    FString ReviewCase; FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),ReviewCase);

    int32 ExpectedAdditional=0;
    FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceExtraFlights="),ExpectedAdditional);
    if (auto* Load=GetWorld()->GetSubsystem<UGuLiFlightAcceptanceSubsystem>())
    {
        TSharedPtr<FJsonObject> LoadStats;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Load->GetLoadStatsJson()),LoadStats) && LoadStats)
            Json->SetObjectField(TEXT("flight_load"),LoadStats);
        if (!Load->GetLoadFailure().IsEmpty()) Failure=Load->GetLoadFailure();
    }
    if (const auto* State=GetWorld()->GetGameState())
        if (const auto* Channel=State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>())
        {
            const int32 Additional=Channel->GetAdditionalActiveFlightCount();
            Json->SetNumberField(TEXT("active_additional_flights"),Additional);
            Json->SetNumberField(TEXT("active_ordinary_flights"),Channel->GetActiveFlightCount()-Additional);
            if (Phase==TEXT("flights"))
            {
                TArray<TSharedPtr<FJsonValue>> Receipts;
                for (const auto& [Id,Receipt]:Channel->GetAdditionalFlightReceipts())
                {
                    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("id"),Id.ToString(EGuidFormats::Digits));
                    R->SetNumberField(TEXT("sequence"),Receipt.X);R->SetNumberField(TEXT("observed"),Receipt.Y);
                    Receipts.Add(MakeShared<FJsonValueObject>(R));
                }
                Json->SetArrayField(TEXT("additional_flight_receipts"),Receipts);
            }
            if (ReviewCase==TEXT("flights") || ReviewCase==TEXT("handoff") || Phase==TEXT("flights") || Phase==TEXT("handoff"))
            {
                TArray<FGuLiCombatEffectState> Flights; Channel->AppendAdditionalFlightSnapshot(Flights);
                TArray<TSharedPtr<FJsonValue>> Queries;
                for (const auto& Flight:Flights)
                {
                    auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("id"),Flight.EffectId.ToString(EGuidFormats::Digits));
                    V->SetNumberField(TEXT("start"),Flight.StartTime); V->SetNumberField(TEXT("end"),Flight.EndTime);
                    V->SetNumberField(TEXT("sequence"),Flight.Sequence); V->SetNumberField(TEXT("sample"),Flight.SampleTime);
                    V->SetNumberField(TEXT("phase"),uint8(Flight.Phase)); Queries.Add(MakeShared<FJsonValueObject>(V));
                }
                Json->SetArrayField(TEXT("additional_flight_queries"),Queries);
            }
            // Replica display clocks need not coincide. The launcher verifies
            // the actual pool load summed over the unique simulation owners.
        }
    Json->SetBoolField(TEXT("participating"),!bStandby); Json->SetStringField(TEXT("phase"),Phase);
    Json->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());
    Json->SetBoolField(TEXT("capturing"),bCapture);
    Json->SetStringField(TEXT("scenario"),Scenario);Json->SetStringField(TEXT("stage_id"),StageId);
    Json->SetNumberField(TEXT("stage_number"),StageNumber);
    AppendSceneEvidence(Json.ToSharedRef());
    auto Checks=MakeShared<FJsonObject>(); Checks->SetBoolField(TEXT("ground_player"),bGroundDone && bGroundPassed);
    Checks->SetBoolField(TEXT("ship"),bShipDone && bShipPassed);
    Json->SetObjectField(TEXT("checks"),Checks); Json->SetBoolField(TEXT("ground_complete"),bGroundDone);
    Json->SetNumberField(TEXT("ground_minimum_fuel"),GroundMinimumFuel); Json->SetNumberField(TEXT("ground_start_fuel"),GroundStartFuel);
    Json->SetBoolField(TEXT("ground_thrust_observed"),bGroundThrust);
    Json->SetNumberField(TEXT("ground_initial_epoch"),double(GroundReviewEpoch));
    Json->SetNumberField(TEXT("scene_ground_inputs_accepted"),double(SceneGroundInputsAccepted));
    Json->SetNumberField(TEXT("scene_ground_inputs_rejected"),double(SceneGroundInputsRejected));
    const auto* AuthorityWorld=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    TArray<TSharedPtr<FJsonValue>> GroundStates;
    for (TActorIterator<AGuLiGroundMechCharacter> It(GetWorld()); It; ++It)
    {
        auto Ground=MakeShared<FJsonObject>(); const auto* Binding=UGuLiHybridActorComponent::Find(*It);
        Ground->SetStringField(TEXT("stable_id"),Binding?Binding->GetObjectKey().StableId.ToString(EGuidFormats::Digits):It->GetName());
        Ground->SetBoolField(TEXT("local_simulation"),Binding && AuthorityWorld && AuthorityWorld->HasSimulation(Binding->GetObjectKey(),true));
        Ground->SetBoolField(TEXT("can_advance"),Binding && Binding->CanSimulate());
        const auto Position=It->GetActorLocation(); TArray<TSharedPtr<FJsonValue>> XYZ;
        for (auto Value : {Position.X,Position.Y,Position.Z}) XYZ.Add(MakeShared<FJsonValueNumber>(Value));
        Ground->SetArrayField(TEXT("position"),XYZ); Ground->SetNumberField(TEXT("fuel"),It->GetRocketJump()->GetFuel());
        Ground->SetNumberField(TEXT("movement_mode"),It->GetCharacterMovement()->MovementMode);
        Ground->SetNumberField(TEXT("rocket_restores"),It->GetHybridRocketRestoreCount());
        Ground->SetBoolField(TEXT("dormant"),Binding?Binding->GetState().bDormant:It->IsCarrierDormant());
        Ground->SetNumberField(TEXT("velocity_cm_s"),It->GetVelocity().Size());
        const auto* Health=It->GetCombatHealthComponent(); const auto& Vitals=Health->GetHealthState();
        Ground->SetNumberField(TEXT("team"),uint8(Health->GetCombatTeam()));
        Ground->SetNumberField(TEXT("health"),Vitals.Health); Ground->SetNumberField(TEXT("max_health"),Vitals.MaxHealth);
        Ground->SetNumberField(TEXT("health_revision"),Vitals.Revision); Ground->SetBoolField(TEXT("alive"),Health->IsAlive());
        Ground->SetStringField(TEXT("health_target"),Health->GetTargetHandle().AuthorityId.ToString(EGuidFormats::Digits));
        Ground->SetNumberField(TEXT("shots_fired"),It->GetWeapon()->ShotsFired);
        Ground->SetStringField(TEXT("upgrade"),It->GetWeapon()->CurrentUpgradeId);
        Ground->SetStringField(TEXT("controller"),It->GetPlayerState<AGuLiBattlePlayerState>()?It->GetPlayerState<AGuLiBattlePlayerState>()->GetPlayerGuid().ToString(EGuidFormats::Digits):TEXT(""));
        GroundStates.Add(MakeShared<FJsonValueObject>(Ground));
    }
    Json->SetArrayField(TEXT("ground_states"),GroundStates);
    Json->SetBoolField(TEXT("ship_complete"),bShipDone); Json->SetBoolField(TEXT("ship_boost_observed"),bShipBoost);
    Json->SetNumberField(TEXT("ship_maximum_speed"),ShipMaximumSpeed); Json->SetNumberField(TEXT("ship_initial_epoch"),double(ShipReviewEpoch));
    TArray<TSharedPtr<FJsonValue>> ShipStates;
    for (TActorIterator<AGuLiStrikeShip> It(GetWorld());It;++It)
    {
        auto Ship=MakeShared<FJsonObject>(); const auto* Binding=UGuLiHybridActorComponent::Find(*It);
        Ship->SetStringField(TEXT("stable_id"),Binding?Binding->GetObjectKey().StableId.ToString(EGuidFormats::Digits):It->GetName());
        Ship->SetBoolField(TEXT("local_simulation"),Binding && AuthorityWorld && AuthorityWorld->HasSimulation(Binding->GetObjectKey(),true));
        Ship->SetBoolField(TEXT("can_advance"),Binding && Binding->CanSimulate());
        TArray<TSharedPtr<FJsonValue>> XYZ; const auto Position=It->GetActorLocation();
        for (auto Value : {Position.X,Position.Y,Position.Z}) XYZ.Add(MakeShared<FJsonValueNumber>(Value));
        Ship->SetArrayField(TEXT("position"),XYZ); Ship->SetNumberField(TEXT("pose_tick"),double(It->GetHybridPoseTick()));
        Ship->SetNumberField(TEXT("speed_limit"),It->GetShipMovement()->MaxFlySpeed);
        Ship->SetNumberField(TEXT("flight_restores"),It->GetHybridFlightRestoreCount());
        Ship->SetNumberField(TEXT("motion_encoding_failures"),double(It->GetHybridMotionEncodingFailures()));
        Ship->SetBoolField(TEXT("dormant"),Binding?Binding->GetState().bDormant:It->IsCarrierDormant());
        Ship->SetNumberField(TEXT("velocity_cm_s"),It->GetVelocity().Size());
        Ship->SetStringField(TEXT("controller"),It->GetPlayerState<AGuLiBattlePlayerState>()?It->GetPlayerState<AGuLiBattlePlayerState>()->GetPlayerGuid().ToString(EGuidFormats::Digits):TEXT(""));
        ShipStates.Add(MakeShared<FJsonValueObject>(Ship));
    }
    Json->SetArrayField(TEXT("ship_states"),ShipStates);
    AppendWingmanEvidence(Json.ToSharedRef());
    TArray<TSharedPtr<FJsonValue>> EngineerStates;
    const auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
    for (TActorIterator<AGuLiConstructionVehiclePawn> It(GetWorld());It;++It)
    {
        auto Value=MakeShared<FJsonObject>(); const auto* Binding=UGuLiHybridActorComponent::Find(*It);
        Value->SetStringField(TEXT("stable_id"),Binding?Binding->GetObjectKey().StableId.ToString(EGuidFormats::Digits):It->GetName());
        Value->SetNumberField(TEXT("ordinal"),It->GetStableActorId().Value); Value->SetNumberField(TEXT("team"),uint8(It->GetTeam()));
        Value->SetNumberField(TEXT("generation"),Binding?Binding->GetObjectKey().LifeGeneration:1);
        const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        Value->SetBoolField(TEXT("local_simulation"),Binding && Hybrid?Hybrid->HasSimulation(Binding->GetObjectKey(),true):It->HasAuthority());
        Value->SetBoolField(TEXT("advancing"),Binding?Binding->CanSimulate():It->HasAuthority());
        Value->SetStringField(TEXT("simulator"),Binding?Binding->GetState().SimulationOwner.ToString(EGuidFormats::Digits):TEXT(""));
        TArray<TSharedPtr<FJsonValue>> XYZ; const auto Position=It->GetActorLocation();
        for (const auto Number:{Position.X,Position.Y,Position.Z}) XYZ.Add(MakeShared<FJsonValueNumber>(Number));
        Value->SetArrayField(TEXT("position"),XYZ); Value->SetNumberField(TEXT("velocity_cm_s"),It->GetVelocity().Size());
        const auto* Travel=It->FindComponentByClass<UGuLiEngineeringTravelComponent>(); const auto* Work=It->FindComponentByClass<UGuLiConstructionWorkComponent>();
        Value->SetNumberField(TEXT("move_status"),uint8(Travel->GetMoveStatus())); Value->SetStringField(TEXT("travel"),Travel->GetTravelDebug());
        Value->SetStringField(TEXT("work"),Work->GetConstructionDebug());
        FGuLiHybridConstructionFact Fact;uint64 FactAge=MAX_uint64;
        const bool FactFresh=Work->GetObservedHybridFact(Fact,&FactAge);
        auto FactJson=MakeShared<FJsonObject>();
        FactJson->SetStringField(TEXT("target"),Fact.Target.StableId.ToString(EGuidFormats::Digits));
        FactJson->SetNumberField(TEXT("building"),Fact.Intent.Building);FactJson->SetNumberField(TEXT("slot"),Fact.Intent.Slot);
        FactJson->SetNumberField(TEXT("generation"),Fact.Intent.Generation);FactJson->SetNumberField(TEXT("task"),Fact.Task);
        FactJson->SetBoolField(TEXT("occupy"),Fact.bOccupy);FactJson->SetBoolField(TEXT("ready"),Fact.bReady);
        FactJson->SetBoolField(TEXT("fresh"),FactFresh);FactJson->SetNumberField(TEXT("received_age_ticks"),FactAge==MAX_uint64?-1:double(FactAge));
        Value->SetObjectField(TEXT("construction_fact"),FactJson);
        const auto* State=Tasks?Tasks->FindState(FGuLiTaskUnitId::Actor(It->GetStableActorId(),GetWorld())):nullptr;
        Value->SetNumberField(TEXT("task_version"),State?double(State->Version):0);
        Value->SetNumberField(TEXT("active_command"),State && State->Active.IsSet()?State->Active->Command.CommandId:0);
        Value->SetNumberField(TEXT("task_status"),State && State->Active.IsSet()?uint8(State->Active->Status):-1);
        Value->SetStringField(TEXT("task_error"),State?State->Error:TEXT(""));
        EngineerStates.Add(MakeShared<FJsonValueObject>(Value));
    }
    Json->SetArrayField(TEXT("engineer_states"),EngineerStates);
    AppendResourceEvidence(Json.ToSharedRef());
    TArray<TSharedPtr<FJsonValue>> Orders;
    for (const auto& Pair:EngineerOrders)
    {
        const auto& Order=Pair.Value; auto Value=MakeShared<FJsonObject>(); Value->SetNumberField(TEXT("command"),Order.Command);
        Value->SetStringField(TEXT("intent"),Order.Intent.ToString());Value->SetNumberField(TEXT("cluster"),Order.Cluster);
        Value->SetBoolField(TEXT("selection_sent"),Order.SelectionSent); Value->SetBoolField(TEXT("task_sent"),Order.TaskSent);
        Value->SetBoolField(TEXT("resolved"),Order.Resolved); Value->SetNumberField(TEXT("result"),uint8(Order.Ack.Result));
        Value->SetNumberField(TEXT("receipts"),Order.Receipts);
        TArray<TSharedPtr<FJsonValue>> Accepted;
        for (const auto& Result:Order.Ack.EngineeringResults) if (Result.Result==EGuLiTransitOrderResult::Accepted)
            Accepted.Add(MakeShared<FJsonValueNumber>(Result.ActorId.Value));
        Value->SetArrayField(TEXT("accepted_actors"),Accepted); Orders.Add(MakeShared<FJsonValueObject>(Value));
    }
    Json->SetArrayField(TEXT("engineer_orders"),Orders);
    TArray<TSharedPtr<FJsonValue>> Buildings;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
    {
        const auto* Life=It->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
        if (!Life || !Life->GetState().InstanceId || Life->GetState().Origin!=EGuLiBuildingOrigin::Manual) continue;
        auto Value=MakeShared<FJsonObject>(); const auto* Binding=UGuLiHybridActorComponent::Find(*It); const auto& State=Life->GetState();
        Value->SetStringField(TEXT("stable_id"),Binding?Binding->GetObjectKey().StableId.ToString(EGuidFormats::Digits):It->GetName());
        Value->SetNumberField(TEXT("ordinal"),State.InstanceId); Value->SetNumberField(TEXT("team"),uint8(Life->GetTeam()));
        Value->SetNumberField(TEXT("definition"),State.DefinitionId); Value->SetNumberField(TEXT("phase"),uint8(State.Phase));
        const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        Value->SetBoolField(TEXT("local_simulation"),Binding && Hybrid?Hybrid->HasSimulation(Binding->GetObjectKey(),true):It->HasAuthority());
        Value->SetBoolField(TEXT("advancing"),Binding?Binding->CanSimulate():It->HasAuthority());
        Value->SetStringField(TEXT("simulator"),Binding?Binding->GetState().SimulationOwner.ToString(EGuidFormats::Digits):TEXT(""));
        Value->SetStringField(TEXT("builder"),State.BuilderGuid.ToString(EGuidFormats::Digits));
        Value->SetNumberField(TEXT("work_done"),State.WorkDone); Value->SetNumberField(TEXT("work_required"),Life->GetDefinition().ConstructionWork);
        Value->SetNumberField(TEXT("occupied_slots"),Life->GetReservedConstructionSlots());
        Value->SetNumberField(TEXT("slot_generation"),Life->GetConstructionSlotGeneration());
        Value->SetBoolField(TEXT("slots_ready"),Life->AreConstructionSlotsReady()); Value->SetBoolField(TEXT("active_builders"),State.bHasActiveBuilders);
        TArray<TSharedPtr<FJsonValue>> Contributors;
        for (const auto& Key:Life->GetConstructionContributors()) Contributors.Add(MakeShared<FJsonValueString>(Key.StableId.ToString(EGuidFormats::Digits)));
        Value->SetArrayField(TEXT("contributors"),Contributors);
        TArray<TSharedPtr<FJsonValue>> SlotPositions;
        for (const auto& Pose:Life->GetConstructionSlotPoses())
        {
            TArray<TSharedPtr<FJsonValue>> XYZ;const auto P=Pose.GetLocation();
            for (const auto Number:{P.X,P.Y,P.Z}) XYZ.Add(MakeShared<FJsonValueNumber>(Number));
            SlotPositions.Add(MakeShared<FJsonValueArray>(XYZ));
        }
        Value->SetArrayField(TEXT("slot_positions"),SlotPositions);
        Buildings.Add(MakeShared<FJsonValueObject>(Value));
    }
    Json->SetArrayField(TEXT("building_states"),Buildings);
    TArray<TSharedPtr<FJsonValue>> BuildingInputs;
    for (const auto& Pair:BuildingOrders)
    {
        auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("operation"),Pair.Key);
        Value->SetBoolField(TEXT("submitted"),Pair.Value.Submitted); BuildingInputs.Add(MakeShared<FJsonValueObject>(Value));
    }
    Json->SetArrayField(TEXT("building_orders"),BuildingInputs);
    if (auto* PC=GetWorld()->GetFirstPlayerController())
    {
        auto* Placement=PC->GetPawn()?PC->GetPawn()->FindComponentByClass<UGuLiBuildingPlacementComponent>():nullptr;
        if (!Placement) Placement=PC->FindComponentByClass<UGuLiBuildingPlacementComponent>();
        if (Placement)
        {
            const auto& Receipt=Placement->GetLastPlacementReceipt();
            Json->SetNumberField(TEXT("placement_receipts"),double(Placement->GetPlacementReceiptCount()));
            Json->SetNumberField(TEXT("placement_request"),Receipt.ClientRequestId);
            Json->SetNumberField(TEXT("placement_result"),uint8(Receipt.RejectReason));
        }
    }
    auto Economy=MakeShared<FJsonObject>(); const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const auto* LocalEconomy=GetWorld()->GetSubsystem<UGuLiTeamEconomySubsystem>();
    for (const auto Team:{EGuLiTeam::Red,EGuLiTeam::Blue})
    {
        const auto Balance=H?H->GetTeamBalance(Team):LocalEconomy?LocalEconomy->GetTeamBalance(Team):FGuLiResourceAmounts();
        auto Value=MakeShared<FJsonObject>(); Value->SetNumberField(TEXT("red"),Balance.Red); Value->SetNumberField(TEXT("blue"),Balance.Blue);
        Economy->SetObjectField(Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"),Value);
    }
    Json->SetObjectField(TEXT("economy"),Economy);
    if (H)
    {
        int32 Nodes=0; int64 Remaining=0; uint64 ResourceVersion=0;
        const bool Configured=H->GetResourceQuerySummary(Nodes,Remaining,ResourceVersion);
        auto Resource=MakeShared<FJsonObject>(); Resource->SetBoolField(TEXT("catalog_committed"),Configured);
        Resource->SetNumberField(TEXT("nodes"),Nodes); Resource->SetNumberField(TEXT("remaining_raw"),Remaining);
        Resource->SetNumberField(TEXT("revision"),ResourceVersion);
        int32 MiningLeases=0; uint64 MiningRevision=0;
        Resource->SetBoolField(TEXT("mining_book_committed"),H->GetMiningReservationSummary(MiningLeases,MiningRevision));
        Resource->SetNumberField(TEXT("mining_leases"),MiningLeases); Resource->SetNumberField(TEXT("mining_revision"),MiningRevision);
        const auto* ResourceWorld=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
        int32 Clusters=0,Enabled=0,VisualComponents=0;
        if (ResourceWorld) ResourceWorld->GetHybridQuerySceneCounts(Clusters,Enabled,VisualComponents);
        const bool SceneReady=ResourceWorld && ResourceWorld->IsHybridQuerySceneReady();
        const bool NavReady=SceneReady && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld());
        Resource->SetBoolField(TEXT("query_scene_ready"),SceneReady); Resource->SetBoolField(TEXT("navigation_ready"),NavReady);
        if (auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            Resource->SetBoolField(TEXT("navigation_locked"),Navigation->IsNavigationBuildingLocked());
            Resource->SetBoolField(TEXT("navigation_dirty"),Navigation->HasDirtyAreasQueued());
            Resource->SetNumberField(TEXT("navigation_remaining"),Navigation->GetNumRemainingBuildTasks());
            Resource->SetNumberField(TEXT("navigation_running"),Navigation->GetNumRunningBuildTasks());
            Resource->SetBoolField(TEXT("navigation_data_present"),Navigation->GetDefaultNavDataInstance()!=nullptr);
        }
        TArray<TSharedPtr<FJsonValue>> NavigationConfigs;
        for (TActorIterator<ARecastNavMesh> It(GetWorld());It;++It)
        {
            auto Value=MakeShared<FJsonObject>();Value->SetStringField(TEXT("name"),It->GetName());
            Value->SetBoolField(TEXT("gather_on_game_thread"),It->ShouldGatherDataOnGameThread());
            Value->SetNumberField(TEXT("maximum_tile_jobs"),It->GetMaxSimultaneousTileGenerationJobsCount());
            NavigationConfigs.Add(MakeShared<FJsonValueObject>(Value));
        }
        Resource->SetArrayField(TEXT("navigation_configs"),NavigationConfigs);
        Resource->SetNumberField(TEXT("collision_clusters"),Clusters); Resource->SetNumberField(TEXT("enabled_clusters"),Enabled);
        Resource->SetNumberField(TEXT("visual_components"),VisualComponents);
        Json->SetBoolField(TEXT("ready"),Json->GetBoolField(TEXT("ready")) && NavReady);
        if (ResourceWorld && ResourceWorld->HasFatalInitializationError()) Failure=ResourceWorld->GetInitializationError();
        // Query collision/visual reconstruction does not certify mining/factory adapters.
        Json->SetObjectField(TEXT("resource_query"),Resource);
    }
    auto Payments=MakeShared<FJsonObject>();
    for (const auto Team:{EGuLiTeam::Red,EGuLiTeam::Blue})
    {
        double Count=0,Blue=0,Red=0;
        for (TActorIterator<APlayerController> It(GetWorld());It;++It)
        {
            const auto* Player=It->GetPlayerState<AGuLiBattlePlayerState>();
            const auto* Placement=It->FindComponentByClass<UGuLiBuildingPlacementComponent>();
            if (Player && Player->GetTeam()==Team && Placement)
            {
                const auto Cost=Placement->GetCommittedPlacementCost();Count+=double(Placement->GetCommittedPlacementCount());Blue+=Cost.Blue;Red+=Cost.Red;
            }
        }
        auto Value=MakeShared<FJsonObject>();Value->SetNumberField(TEXT("count"),Count);Value->SetNumberField(TEXT("blue"),Blue);Value->SetNumberField(TEXT("red"),Red);
        Payments->SetObjectField(Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"),Value);
    }
    Json->SetObjectField(TEXT("placement_payments"),Payments);
    if (UGuLiNetworkSettings::IsLogicOnly() && Performance) Failure=TEXT("Logic-only helper unexpectedly has a performance subsystem");
    if (!Failure.IsEmpty()) Json->SetStringField(TEXT("failure"),Failure);
    // Readiness requires actual local entities; ordinary peer delay never pauses the world.
    bool StateReady=bStandby;
    if (const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {
        const TSharedPtr<FJsonObject>* Resource=nullptr;
        Json->TryGetObjectField(TEXT("resource_query"),Resource);
        StateReady=Hybrid->IsWorldReady() && !Hybrid->IsBattlePaused()
            && Json->GetNumberField(TEXT("waiting_adapters"))==0 && Json->GetNumberField(TEXT("pending_assignments"))==0
            && Json->GetStringField(TEXT("failure")).IsEmpty()
            && Resource && (*Resource)->GetBoolField(TEXT("query_scene_ready")) && (*Resource)->GetBoolField(TEXT("navigation_ready"));
    }
    Json->SetBoolField(TEXT("state_ready"),StateReady);
    Json->SetStringField(TEXT("snapshot_started_qpc"),LexToString(SnapshotStarted));
    Json->SetStringField(TEXT("snapshot_finished_qpc"),LexToString(FPlatformTime::Cycles64()));
    Json->SetNumberField(TEXT("qpc_seconds_per_tick"),FPlatformTime::GetSecondsPerCycle64());
    if (Phase==TEXT("stop"))
    {
        if (!Write(TEXT("state.json"),Json.ToSharedRef())) Failure=TEXT("Cannot persist runtime correctness evidence");
        FPlatformMisc::RequestExit(false);
    }
    else
    {
        Json->SetNumberField(TEXT("schema"),1);Json->SetStringField(TEXT("run_id"),Run);
        Json->SetNumberField(TEXT("pid"),FPlatformProcess::GetCurrentProcessId());
        // Snapshot values are immutable after handoff. The worker owns no UObject
        // and there is only one pending write: slow disk cannot grow a queue.
        StateWrite=Async(EAsyncExecution::ThreadPool,[File=Directory/TEXT("state.json"),Snapshot=Json.ToSharedRef()]
        {return PersistReviewJson(File,Snapshot);});
    }
}
void UGuLiHybridReviewSubsystem::Deinitialize()
{
    if (StateWrite.IsValid()) StateWrite.Get();
    if (SceneSkills.IsValid()) SceneSkills->OnSkillReply.RemoveDynamic(this,&ThisClass::SceneSkillResolved);
    if (EngineerSync.IsValid()) EngineerSync->OnCommandAckChanged.RemoveAll(this);
    if (auto* Pool=GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>()) Pool->OnState.RemoveAll(this);
    if (auto* Effects=GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>()) Effects->OnState.RemoveAll(this);
    if (bCapture) if (auto* Performance=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>()) Performance->EndCapture();
    Super::Deinitialize();
}
