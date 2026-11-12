#include "Gameplay/Performance/GuLiMovementCapture.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "NavigationSystem.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMemory.h"
#include "HAL/IConsoleManager.h"
#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Navigation/GuLiCircleAvoidance.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Building/GuLiShieldGeneratorActor.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

namespace
{
#if WITH_EDITOR
TAutoConsoleVariable<int32> MovementFixtureHold(TEXT("gs.Perf.MovementFixtureHold"),0,
    TEXT("Explicit disposable movement capture only: 1 holds unrelated territory changes, 2 holds automatic engineering/mining/production work. Does not remove units. Default 0."));
#endif
bool FixtureHold(const UWorld* World,int32 Bit)
{
#if WITH_EDITOR
    return World&&World->IsGameWorld()&&World->GetMapName().EndsWith(TEXT("LVL_CommanderMassPrototype"))
        &&(MovementFixtureHold.GetValueOnGameThread()&Bit)!=0;
#else
    return false;
#endif
}
}
bool GuLiMovementCapture::HoldTerritoryChanges(const UWorld* World) {return FixtureHold(World,1);}
bool GuLiMovementCapture::HoldAutomaticWork(const UWorld* World,const AActor* Worker)
{return FixtureHold(World,2) && (!Worker || !Worker->ActorHasTag(TEXT("MovementCaptureActiveWorker")));}

// The fixture is deliberately independent of the avoidance implementation. It
// replays ordinary commander moves and observes applied navigation state.
struct FGuLiMovementCapture
{
    struct FMember
    {
        FGuLiSoldierId Id;
        FVector Start=FVector::ZeroVector,Last=FVector::ZeroVector;
        FVector Goal=FVector::ZeroVector;
        uint16 Type=0;
        float Radius=0;
        EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground;
        double Arrival=-1,LastProgress=0,MaximumStall=0;
        double BestGoalDistance=TNumericLimits<double>::Max();
        uint32 CompletedBefore=0,FailedBefore=0;
        bool Blocked=false;
        bool Commanded=false;
        TWeakObjectPtr<ACharacter> Worker;
    };
    struct FCommand
    {
        double At=0;TArray<FGuLiSoldierId> Members;FVector Target=FVector::ZeroVector;uint32 Id=0;
        FString Kind=TEXT("move");uint16 Cluster=0;
        TWeakObjectPtr<UGuLiBuildingLifecycleComponent> Building;
    };
    struct FOreEvent {double At=0;uint32 Node=0;int32 Remaining=0;};
    TArray<FMember> Members;
    TArray<FCommand> Commands;
    TArray<FOreEvent> OreEvents;
    TArray<TSharedPtr<FJsonValue>> EventReceipts;
    TArray<TSharedPtr<FJsonValue>> Samples;
    TArray<TSharedPtr<FJsonValue>> Applications;
    TArray<FGuLiGroundMassBody> PreviousBodies;
    TMap<FString,double> PreviousCounters;
    TSet<uint32> ActiveInStep;
    uint64 DynamicSweepHits=0,StaticSweepHits=0,ApplicationsObserved=0;
    TSharedPtr<FJsonObject> Recipe,Before,After;
    FString Error;
    TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Army;
    TWeakObjectPtr<UGuLiCommanderNetSyncComponent> Sync;
    TWeakObjectPtr<ACameraActor> Camera;
    double Started=0,Elapsed=0;
    uint64 LastStep=0,StartStep=0;
    uint64 MemoryBefore=0,MemoryAfter=0;
    int32 NextCommand=0;
    int32 NextOreEvent=0;
    FDelegateHandle AckHandle;
    TMap<uint32,int32> AckCounts;
    bool Prepared=false,Running=false,Finished=false;
    ~FGuLiMovementCapture() {if (Sync.IsValid()) Sync->OnCommandAckChanged.Remove(AckHandle);}
};

namespace
{
bool ReadVector(const FJsonObject& J,const TCHAR* Field,FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* V=nullptr;
    if (!J.TryGetArrayField(Field,V)||V->Num()!=3) return false;
    double X,Y,Z;
    if (!(*V)[0]->TryGetNumber(X)||!(*V)[1]->TryGetNumber(Y)||!(*V)[2]->TryGetNumber(Z)) return false;
    Out={X,Y,Z};return !Out.ContainsNaN();
}
bool ReadId(const FJsonValue& V,FGuLiSoldierId& Out)
{
    double N=0;if (!V.TryGetNumber(N)||!FMath::IsFinite(N)||N<1||N>MAX_uint32||N!=FMath::FloorToDouble(N)) return false;
    Out=FGuLiSoldierId(uint32(N));return true;
}
TArray<TSharedPtr<FJsonValue>> Point(FVector V)
{return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};}

ACharacter* FindWorker(UWorld* World,FGuLiSoldierId Id)
{
    for (TActorIterator<ACharacter> It(World);It;++It)
        if (const auto* Travel=It->FindComponentByClass<UGuLiEngineeringTravelComponent>();Travel && Travel->GetMassEntityId()==Id.Value)
            return *It;
    return nullptr;
}

bool ReadMember(UGuLiBattleAuthoritySubsystem& Army,FGuLiMovementCapture::FMember& M,EGuLiTeam& Team,FVector& Location)
{
    if (auto* Worker=M.Worker.Get())
    {
        const auto* Vehicle=Cast<IGuLiEngineeringVehicle>(Worker);FGuLiSoldierNavigationDebug N;
        if (!Vehicle || !Army.TryGetSoldierNavigationDebug(M.Id,N)) return false;
        M.Type=uint16(Vehicle->GetUnitTypeId());Team=Vehicle->GetTeam();Location=N.Location;return true;
    }
    return Army.GetTaskSoldierInfo(M.Id,Team,M.Type,Location);
}
}

FString UGuLiPerformanceSubsystem::PrepareMovementBuilding(int32 DefinitionId,FVector Preferred)
{
#if WITH_EDITOR
    auto* W=GetWorld();auto* PC=W?W->GetFirstPlayerController():nullptr;
    auto* Placement=PC?PC->FindComponentByClass<UGuLiBuildingPlacementComponent>():nullptr;
    if (!W || W->WorldType!=EWorldType::PIE || !FixtureHold(W,2) || IsPerformanceCaptureBusy() || !Placement)
        return TEXT("{\"error\":\"Disposable idle movement fixture required\"}");
    FVector Ground;
    if (!Placement->EditorQAFindClearDefinition(DefinitionId,Preferred,0,Ground))
        return TEXT("{\"error\":\"No legal construction fixture placement\"}");
    auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("submitted"),Placement->EditorQASubmitDefinition(DefinitionId,Ground,0));
    J->SetArrayField(TEXT("ground"),Point(Ground));J->SetNumberField(TEXT("definition"),DefinitionId);
    FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));return Json;
#else
    return TEXT("{\"error\":\"Editor fixture only\"}");
#endif
}

FString UGuLiPerformanceSubsystem::PrepareMovementScaleCapture(int32 MovingUnits,bool StationaryWall)
{
#if !UE_BUILD_SHIPPING
    auto* Army=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    auto* NavData=GuLiCommanderNavigationPolicy::ResolveRequiredNavigationData(*Navigation);
    auto Project=[&](const FVector& Point,FVector& Surface)
    {
        FNavLocation Nav;
        if (!Army->ProjectExternalUnitLocation(Point,Surface,2)
            || !Navigation->ProjectPointToNavigation(Surface,Nav,FVector(20,20,300),NavData)) return false;
        Surface=Nav.Location;return true;
    };
    TArray<FVector> Positions;
    for (const int32 Side:{-1,1})
    {
        int32 Added=0;
        for (int32 I=0;I<5000 && Added<MovingUnits/2;++I)
        {
            FVector Surface;
            if (Project(FVector(-35000+Side*(4500+(I/25)*900),62000+(I%25-12)*900,900),Surface))
            {Positions.Add(Surface);++Added;}
        }
    }
    if (Positions.Num()!=MovingUnits) return TEXT("{\"error\":\"Insufficient walkable fixture positions\"}");
    if (StationaryWall) for (int32 I=0;I<200;++I)
    {
        FVector Surface;
        if (Project(FVector(-35000+(I/25-4)*900,62000+(I%25-12)*900,900),Surface)) Positions.Add(Surface);
    }
    TArray<FGuLiSoldierId> Ids;
    if (!Army->SpawnMovementReviewUnits(Positions,Ids)) return TEXT("{\"error\":\"Movement population preparation failed\"}");
    auto Recipe=MakeShared<FJsonObject>();Recipe->SetStringField(TEXT("schema"),TEXT("guli-movement-capture/v1"));
    Recipe->SetStringField(TEXT("scenario"),StationaryWall?TEXT("hold"):TEXT("crossing"));
    Recipe->SetStringField(TEXT("map"),TEXT("/Game/Maps/LVL_CommanderMassPrototype"));
    Recipe->SetBoolField(TEXT("authority_fixture"),true);Recipe->SetBoolField(TEXT("automatic_fire"),false);
    Recipe->SetNumberField(TEXT("moving_units"),MovingUnits);
    TArray<TSharedPtr<FJsonValue>> Members,Commands;
    for (int32 I=0;I<Ids.Num();++I)
    {
        auto M=MakeShared<FJsonObject>();M->SetNumberField(TEXT("id"),Ids[I].Value);
        M->SetNumberField(TEXT("unit_type"),1);M->SetArrayField(TEXT("position"),Point(Positions[I]));
        Members.Add(MakeShared<FJsonValueObject>(M));
    }
    for (int32 I=0;I<MovingUnits;I+=25)
    {
        const int32 Side=I<MovingUnits/2?-1:1;
        auto Command=MakeShared<FJsonObject>();Command->SetNumberField(TEXT("at"),.5);
        Command->SetStringField(TEXT("kind"),TEXT("authority_move"));
        Command->SetArrayField(TEXT("target"),Point(FVector(-35000-Side*12000,62000,900)));
        TArray<TSharedPtr<FJsonValue>> Group;for (int32 J=I;J<I+25;++J) Group.Add(MakeShared<FJsonValueNumber>(Ids[J].Value));
        Command->SetArrayField(TEXT("members"),Group);Commands.Add(MakeShared<FJsonValueObject>(Command));
    }
    Recipe->SetArrayField(TEXT("members"),Members);Recipe->SetArrayField(TEXT("commands"),Commands);
    FString Json;FJsonSerializer::Serialize(Recipe,TJsonWriterFactory<>::Create(&Json));
    return PrepareMovementCapture(Json);
#else
    return TEXT("{\"error\":\"development only\"}");
#endif
}

FString UGuLiPerformanceSubsystem::PrepareMovementCapture(const FString& RecipeJson)
{
#if !UE_BUILD_SHIPPING
    if (IsPerformanceCaptureBusy()||bMovementCapturePending) return TEXT("{\"error\":\"Another capture is active\"}");
    MovementCapture=MakeShared<FGuLiMovementCapture>();auto& C=*MovementCapture;
    auto Fail=[&](const TCHAR* Why){C.Error=Why;return GetMovementCaptureJson();};
    auto* W=GetWorld();auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Catalog=W?W->GetSubsystem<UGuLiUnitDataSubsystem>():nullptr;
    auto* PC=W?W->GetFirstPlayerController():nullptr;
    C.Army=W?W->GetSubsystem<UGuLiBattleAuthoritySubsystem>():nullptr;
    C.Sync=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
    if (!W||W->WorldType!=EWorldType::PIE||!H||!H->CanPublishFacts()||!Catalog||!C.Army.IsValid()
        ||!C.Sync.IsValid()||!C.Sync->IsSoldierStreamReady()) return Fail(TEXT("Ready disposable commander PIE required"));
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RecipeJson),C.Recipe)||!C.Recipe)
        return Fail(TEXT("Invalid recipe JSON"));
    const TArray<TSharedPtr<FJsonValue>>* Members=nullptr;const TArray<TSharedPtr<FJsonValue>>* Commands=nullptr;
    if (!C.Recipe->TryGetArrayField(TEXT("members"),Members)||Members->IsEmpty()||Members->Num()>2000
        ||!C.Recipe->TryGetArrayField(TEXT("commands"),Commands)||Commands->IsEmpty()||Commands->Num()>128)
        return Fail(TEXT("Recipe needs 1..2000 members and 1..128 commands"));
    TSet<uint32> Seen;TArray<FGuLiMassExternalUnit> Placements;TArray<FGuLiSoldierId> Ids;
    for (const auto& Value:*Members)
    {
        const auto Row=Value->AsObject();FGuLiMovementCapture::FMember M;
        FVector Desired=FVector::ZeroVector,Previous=FVector::ZeroVector;EGuLiTeam Team=EGuLiTeam::Unassigned;
        if (!Row||!Row->HasField(TEXT("id"))||!ReadId(*Row->Values[TEXT("id")],M.Id)||Seen.Contains(M.Id.Value)
            ||!ReadVector(*Row,TEXT("position"),Desired))
            return Fail(TEXT("Invalid or unavailable member"));
        M.Worker=FindWorker(W,M.Id);
        if (!ReadMember(*C.Army,M,Team,Previous)) return Fail(TEXT("Fixture member has no live applied state"));
        FGuLiHybridObjectKey Key;
        const auto* Binding=M.Worker.IsValid()?UGuLiHybridActorComponent::Find(M.Worker.Get()):nullptr;
        if (Binding) Key=Binding->GetObjectKey();
        else H->ResolveTarget(C.Army->MakeSoldierTargetHandle(M.Id),Key);
        if (!C.Recipe->HasField(TEXT("authority_fixture")) && (!Key.IsValid()||!H->HasSimulation(Key)))
            return Fail(TEXT("A fixture member is not locally simulated"));
        const auto* Definition=Catalog->FindDefinition(M.Type);FVector Surface;
        const FVector RequestedGround=Desired-FVector(0,0,C.Recipe->HasField(TEXT("prepared")) && Definition?Definition->AltitudeCentimeters:0.f);
        const bool AuthorityFixture=C.Recipe->HasField(TEXT("authority_fixture"));
        if (AuthorityFixture) Surface=RequestedGround;
        if (!Definition||(!AuthorityFixture && !C.Army->ProjectExternalUnitLocation(RequestedGround,Surface,Definition->FootprintCells)))
            return Fail(TEXT("Fixture member has no valid navigation surface"));
        M.Start=Surface+FVector(0,0,Definition->AltitudeCentimeters);M.Last=M.Start;
        M.Radius=Definition->MassAvoidanceRadiusCm;M.Layer=Definition->MovementLayer;
        // A frozen replay must not silently project to a different location.
        if (C.Recipe->HasField(TEXT("prepared"))&&!M.Start.Equals(Desired,1.))
            return Fail(TEXT("Frozen placement no longer projects to the same position"));
        FGuLiMassExternalUnit U;U.Id=M.Id;U.Transform=FTransform(FRotator::ZeroRotator,M.Start);
        Placements.Add(U);Ids.Add(M.Id);Seen.Add(M.Id.Value);C.Members.Add(M);
    }
    double PreviousAt=-1;
    for (int32 I=0;I<Commands->Num();++I)
    {
        const auto Row=(*Commands)[I]->AsObject();FGuLiMovementCapture::FCommand Command;
        const TArray<TSharedPtr<FJsonValue>>* CommandMembers=nullptr;
        if (!Row||!Row->TryGetNumberField(TEXT("at"),Command.At)||!FMath::IsFinite(Command.At)
            ||Command.At<0||Command.At>=30||Command.At<PreviousAt||!ReadVector(*Row,TEXT("target"),Command.Target)
            ||!Row->TryGetArrayField(TEXT("members"),CommandMembers)||CommandMembers->IsEmpty()||CommandMembers->Num()>25)
            return Fail(TEXT("Invalid bounded command timeline"));
        TSet<uint32> CommandSeen;
        Row->TryGetStringField(TEXT("kind"),Command.Kind);
        if (Command.Kind!=TEXT("move") && Command.Kind!=TEXT("authority_move") && Command.Kind!=TEXT("attack_move") && Command.Kind!=TEXT("mine")
            && Command.Kind!=TEXT("return") && Command.Kind!=TEXT("construct") && Command.Kind!=TEXT("worker_move"))
            return Fail(TEXT("Unknown movement fixture command kind"));
        if (Command.Kind==TEXT("mine"))
        {
            int32 Cluster=0;const auto* Resources=W->GetSubsystem<UGuLiResourceWorldSubsystem>();
            if (!Row->TryGetNumberField(TEXT("cluster"),Cluster) || Cluster<1 || Cluster>MAX_uint16
                || !Resources || !Resources->GetMapDefinition()->FindCluster(uint16(Cluster)))
                return Fail(TEXT("Invalid mining cluster"));
            Command.Cluster=uint16(Cluster);
        }
        if (Command.Kind==TEXT("construct"))
        {
            // Match semantic location, because startup gift instance allocation can vary.
            for (TActorIterator<AActor> It(W);It;++It)
                if (auto* Life=It->FindComponentByClass<UGuLiBuildingLifecycleComponent>();Life
                    && Life->GetGroundLocation().Equals(Command.Target,1.) && !Life->IsCompleted())
                {Command.Building=Life;break;}
            if (!Command.Building.IsValid()) return Fail(TEXT("Construction target not prepared"));
        }
        for (const auto& V:*CommandMembers)
        {
            FGuLiSoldierId Id;if (!ReadId(*V,Id)||!Seen.Contains(Id.Value)||CommandSeen.Contains(Id.Value))
                return Fail(TEXT("Command refers to a missing or duplicate member"));
            Command.Members.Add(Id);CommandSeen.Add(Id.Value);
            const auto* Member=C.Members.FindByPredicate([&](const auto& M){return M.Id==Id;});
            const bool NeedsWorker=Command.Kind!=TEXT("move") && Command.Kind!=TEXT("authority_move") && Command.Kind!=TEXT("attack_move");
            if (!Member || Member->Worker.IsValid()!=NeedsWorker) return Fail(TEXT("Command/member kind mismatch"));
        }
        PreviousAt=Command.At;Command.Id=950001+I;C.Commands.Add(MoveTemp(Command));
    }
    if (!C.Army->CanApplyExternalUnitState(Placements,FGuid::NewGuid())) return Fail(TEXT("Spatial fixture is unavailable"));
    C.Army->StopTaskSoldiers(Ids);
    if (!C.Army->ApplyExternalUnitState(Placements,FGuid::NewGuid(),false,false,true)
        ||!C.Army->SetReviewAutomaticFire(Ids,false)) return Fail(TEXT("Spatial/fire preparation rejected"));
    for (auto& M:C.Members) if (auto* Worker=M.Worker.Get())
    {
        Worker->Tags.Remove(TEXT("MovementCaptureActiveWorker"));
        Worker->FindComponentByClass<UGuLiEngineeringTravelComponent>()->CancelGroundMove();
        Worker->SetActorLocation(M.Start+FVector(0,0,Worker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),false,nullptr,ETeleportType::TeleportPhysics);
        C.Army->SyncEngineeringMass(M.Id);
    }
    const TArray<TSharedPtr<FJsonValue>>* OreEvents=nullptr;
    if (C.Recipe->TryGetArrayField(TEXT("ore_projection_events"),OreEvents))
    {
        if (OreEvents->Num()>128) return Fail(TEXT("Too many ore projection events"));
        double PreviousTime=-1;const auto* Resources=W->GetSubsystem<UGuLiResourceWorldSubsystem>();
        for (const auto& Value:*OreEvents)
        {
            const auto Row=Value->AsObject();FGuLiMovementCapture::FOreEvent Event;int32 Node=0;
            if (!Row || !Row->TryGetNumberField(TEXT("at"),Event.At) || !FMath::IsFinite(Event.At)
                || Event.At<PreviousTime || Event.At<0 || Event.At>=30 || !Row->TryGetNumberField(TEXT("node"),Node)
                || !Row->TryGetNumberField(TEXT("remaining"),Event.Remaining) || Event.Remaining<0 || !Resources
                || !Resources->GetMapDefinition()->Nodes.IsValidIndex(Node-1)
                || Event.Remaining>Resources->GetMapDefinition()->Nodes[Node-1].InitialAmount)
                return Fail(TEXT("Invalid ore projection timeline"));
            Event.Node=uint32(Node);PreviousTime=Event.At;C.OreEvents.Add(Event);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Frozen;
    for (const auto& M:C.Members)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),M.Id.Value);
        Row->SetNumberField(TEXT("unit_type"),M.Type);Row->SetArrayField(TEXT("position"),Point(M.Start));
        Frozen.Add(MakeShared<FJsonValueObject>(Row));
    }
    C.Recipe->SetArrayField(TEXT("members"),Frozen);C.Recipe->SetBoolField(TEXT("prepared"),true);
    C.Before=H->GetSimulationCensus();
    const TWeakPtr<FGuLiMovementCapture> Weak=MovementCapture;
    C.AckHandle=C.Sync->OnCommandAckChanged.AddLambda([Weak](const FGuLiCommandAck& Ack)
    {
        if (const auto State=Weak.Pin();State&&State->Running&&Ack.CommandKind==EGuLiCommandKind::Move
            &&Ack.ClientCommandId>=950001&&Ack.ClientCommandId<950001+uint32(State->Commands.Num()))
        {
            ++State->AckCounts.FindOrAdd(Ack.ClientCommandId);
            if (Ack.Result!=EGuLiCommandAckResult::Accepted)
                State->Error=FString::Printf(TEXT("Movement command %u rejected/partial, result=%u: %s"),Ack.ClientCommandId,
                    uint8(Ack.Result),State->Sync.IsValid()?*State->Sync->GetLastTaskFeedback():TEXT("input unavailable"));
        }
    });
    FActorSpawnParameters CameraParameters;CameraParameters.ObjectFlags|=RF_Transient;
    CameraParameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Camera=W->SpawnActor<ACameraActor>(FVector(-54485.1674985583,62000,14413.904208077727),
        FRotator(-34.33727264404301,0,0),CameraParameters);
    if (!Camera) return Fail(TEXT("Cannot freeze capture camera"));
    if (const auto* Previous=PC->GetViewTarget()?PC->GetViewTarget()->FindComponentByClass<UCameraComponent>():nullptr)
        Camera->GetCameraComponent()->SetFieldOfView(Previous->FieldOfView);
    PC->SetViewTarget(Camera);C.Camera=Camera;
    C.Prepared=true;return GetMovementCaptureJson();
#else
    return TEXT("{\"error\":\"development only\"}");
#endif
}

bool UGuLiPerformanceSubsystem::StartMovementCapture()
{
#if !UE_BUILD_SHIPPING
    if (!MovementCapture||!MovementCapture->Prepared||MovementCapture->Running||MovementCapture->Finished
        ||IsPerformanceCaptureBusy()||!MovementCapture->Error.IsEmpty()) return false;
    auto& C=*MovementCapture;
    // Prepare/warmup is outside the measured window. Refuse any drift instead
    // of counting a different starting configuration as a paired capture.
    for (auto& M:C.Members)
    {
        EGuLiTeam Team;FVector P;
        if (!ReadMember(*C.Army,M,Team,P)||!P.Equals(M.Start,1.))
        {C.Error=TEXT("A prepared member drifted before the timing window");return false;}
    }
    C.Started=FPlatformTime::Seconds();C.MemoryBefore=FPlatformMemory::GetStats().UsedPhysical;
    C.LastStep=C.StartStep=C.Army->GetPerformanceCounters().Steps;
    uint32 Epoch,Sequence;double Simulation;
    C.Army->BuildGroundCollisionSnapshot(C.PreviousBodies,Epoch,Sequence,Simulation);
    C.Running=true;bMovementCapturePending=true;BeginCapture();return true;
#else
    return false;
#endif
}

void UGuLiPerformanceSubsystem::ObserveMovementApplication(bool Immediate,double ApplicationMilliseconds)
{
#if !UE_BUILD_SHIPPING
    if (!IsObservingMovement() || !MovementCapture || !MovementCapture->Army.IsValid()) return;
    const double ApplicationObservationStarted=FPlatformTime::Seconds();auto& C=*MovementCapture;
    if (C.Applications.Num()>=8192) {C.Error=TEXT("Movement application observation overflow");return;}
    TArray<FGuLiGroundMassBody> Current;uint32 Epoch,Sequence;double Simulation;
    C.Army->BuildGroundCollisionSnapshot(Current,Epoch,Sequence,Simulation);
    TMap<uint32,int32> ById;for (int32 I=0;I<Current.Num();++I) ById.Add(Current[I].SoldierId.Value,I);
    TArray<GuLiCircleAvoidance::FCircle> Circles;Circles.Reserve(Current.Num());
    for (const auto& Before:C.PreviousBodies)
    {
        const int32* Found=ById.Find(Before.SoldierId.Value);if (!Found) continue;
        const auto& After=Current[*Found];
        GuLiCircleAvoidance::FCircle Circle;Circle.Id=Before.SoldierId.Value;Circle.Center=Before.Location;
        Circle.Radius=FMath::Max(.01f,Before.RadiusCentimeters-1.f);
        Circle.LayerMask=uint8(1u<<uint8(Before.MovementLayer));Circle.StepDelta=After.Location-Before.Location;
        Circles.Add(Circle);
        if (Circle.StepDelta.SizeSquared2D()>.0001) C.ActiveInStep.Add(Before.SoldierId.Value);
    }
    GuLiCircleAvoidance::FIndex Empty,Index;Index.Rebuild(Circles);
    GuLiCircleAvoidance::FQueryScratch Scratch;GuLiCircleAvoidance::FCounters Counts;
    const auto* Catalog=GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>();
    auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
    TSharedPtr<const FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe> Obstacles;
    if (Registry) Obstacles=Registry->GetSnapshot();
    TArray<TSharedPtr<FJsonValue>> Moves;int32 DynamicHits=0,StaticHits=0;
    for (const auto& M:C.Members)
    {
        const auto* Before=Index.Find(M.Id.Value);if (!Before || Before->StepDelta.SizeSquared2D()<=.0001) continue;
        GuLiCircleAvoidance::FRequest Request;Request.SelfId=M.Id.Value;Request.Position=Before->Center;Request.LayerMask=Before->LayerMask;
        const auto Hit=GuLiCircleAvoidance::Sweep(Empty,Index,Request,Before->StepDelta,Before->Radius,true,Scratch,Counts);
        DynamicHits+=Hit.Blocked?1:0;
        FVector Ground=Before->Center;
        if (const auto* Definition=Catalog?Catalog->FindDefinition(M.Type):nullptr) Ground.Z-=Definition->AltitudeCentimeters;
        const bool StaticHit=Obstacles && !Obstacles->IsSegmentClear(Ground,Ground+Before->StepDelta,FMath::Max(.01f,M.Radius-2.f),false,M.Layer,0,0);
        StaticHits+=StaticHit?1:0;
        auto Move=MakeShared<FJsonObject>();Move->SetNumberField(TEXT("id"),M.Id.Value);
        Move->SetArrayField(TEXT("from"),Point(Before->Center));Move->SetArrayField(TEXT("to"),Point(Before->Center+Before->StepDelta));
        Move->SetNumberField(TEXT("dynamic_hit"),Hit.Blocked?double(Hit.Circle.Id):0.);
        Move->SetNumberField(TEXT("hit_fraction"),Hit.Fraction);Move->SetBoolField(TEXT("static_hit"),StaticHit);
        Move->SetBoolField(TEXT("static_initial_overlap_escape"),StaticHit && Obstacles->IsSegmentClear(
            Ground,Ground+Before->StepDelta,FMath::Max(.01f,M.Radius-2.f),true,M.Layer,0,0));
        Moves.Add(MakeShared<FJsonValueObject>(Move));
    }
    auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("seconds"),ApplicationObservationStarted-C.Started);
    Row->SetNumberField(TEXT("qpc"),double(FPlatformTime::Cycles64()));Row->SetNumberField(TEXT("engine_frame"),double(GFrameCounter));
    Row->SetBoolField(TEXT("immediate"),Immediate);Row->SetNumberField(TEXT("application_ms"),ApplicationMilliseconds);
    Row->SetArrayField(TEXT("moves"),Moves);Row->SetNumberField(TEXT("dynamic_sweep_hits"),DynamicHits);Row->SetNumberField(TEXT("static_sweep_hits"),StaticHits);
    if (!Immediate)
    {
        auto Totals=MakeShared<FJsonObject>();
        for (const auto& Pair:Counters)
            if (Pair.Key.StartsWith(TEXT("Movement.")) || Pair.Key.StartsWith(TEXT("Avoidance."))
                || Pair.Key.StartsWith(TEXT("Navigation.")) || Pair.Key.StartsWith(TEXT("Ore.")))
            {
                double& Previous=C.PreviousCounters.FindOrAdd(Pair.Key);
                Totals->SetNumberField(Pair.Key,Pair.Value.Total-Previous);Previous=Pair.Value.Total;
            }
        Row->SetObjectField(TEXT("step_counters"),Totals);
        Row->SetNumberField(TEXT("active_units"),C.ActiveInStep.Num());C.ActiveInStep.Reset();
    }
    C.Applications.Add(MakeShared<FJsonValueObject>(Row));C.PreviousBodies=MoveTemp(Current);
    C.DynamicSweepHits+=DynamicHits;C.StaticSweepHits+=StaticHits;++C.ApplicationsObserved;
    Record(TEXT("Capture.MovementApplicationObservationMs"),(FPlatformTime::Seconds()-ApplicationObservationStarted)*1000.);
    if (!Immediate) ObserveMovementStep();
#endif
}

FString UGuLiPerformanceSubsystem::GetMovementSceneJson() const
{
    auto Root=MakeShared<FJsonObject>();
#if !UE_BUILD_SHIPPING
    const auto* W=GetWorld();
    const auto* Resources=W?W->GetSubsystem<UGuLiResourceWorldSubsystem>():nullptr;
    const auto* Map=Resources?Resources->GetMapDefinition():nullptr;
    if (!W || !Map) Root->SetStringField(TEXT("error"),TEXT("Ready resource world required"));
    else
    {
        Root->SetStringField(TEXT("layout_hash"),Map->LayoutHash);
        Root->SetStringField(TEXT("source_hash"),Map->SourceHash);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for (const auto& Node:Map->Nodes)
        {
            auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Node.NodeId);
            Row->SetNumberField(TEXT("cluster"),Node.ClusterId);
            Row->SetNumberField(TEXT("remaining"),Resources->GetNodeRemainingRaw(Node.NodeId));
            Nodes.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("ore"),Nodes);
        TArray<TSharedPtr<FJsonValue>> Buildings;
        for (TActorIterator<AActor> It(W);It;++It)
        {
            const auto* Life=It->FindComponentByClass<UGuLiBuildingLifecycleComponent>();if (!Life) continue;
            const auto& State=Life->GetState();const auto& Definition=Life->GetDefinition();
            auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),State.InstanceId);
            Row->SetNumberField(TEXT("definition"),State.DefinitionId);Row->SetNumberField(TEXT("phase"),uint8(State.Phase));
            Row->SetNumberField(TEXT("work"),State.WorkDone);Row->SetNumberField(TEXT("team"),uint8(Life->GetTeam()));
            Row->SetArrayField(TEXT("position"),Point(It->GetActorLocation()));
            Row->SetArrayField(TEXT("rotation"),Point(It->GetActorRotation().Euler()));
            Row->SetArrayField(TEXT("scale"),Point(It->GetActorScale3D()));
            Row->SetArrayField(TEXT("ground"),Point(Life->GetGroundLocation()));
            Row->SetArrayField(TEXT("collision_extent"),Point(Definition.CollisionExtent));
            Row->SetNumberField(TEXT("presentation_scale"),Definition.PresentationScale);
            TArray<TSharedPtr<FJsonValue>> Hulls;
            if (State.DefinitionId==6)
            {
                TArray<AActor*> Parts;It->GetAllChildActors(Parts,true);Parts.Add(*It);
                for (const auto* Part:Parts)
                {
                    TInlineComponentArray<UStaticMeshComponent*> Meshes(Part);
                    for (const auto* Mesh:Meshes)
                    {
                        const UStaticMesh* Asset=Mesh->GetStaticMesh();const auto* Body=Asset?Asset->GetBodySetup():nullptr;
                        if (!Body || Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
                        for (const auto& Hull:Body->AggGeom.ConvexElems)
                        {
                            auto H=MakeShared<FJsonObject>();H->SetStringField(TEXT("mesh"),Asset->GetPathName());
                            H->SetStringField(TEXT("name"),Hull.GetName().ToString());
                            TArray<TSharedPtr<FJsonValue>> Vertices;
                            for (const auto& V:Hull.VertexData)
                            {
                                const FVector World=Mesh->GetComponentTransform().TransformPosition(Hull.GetTransform().TransformPosition(V));
                                Vertices.Add(MakeShared<FJsonValueArray>(Point(It->GetActorTransform().InverseTransformPosition(World))));
                            }
                            H->SetArrayField(TEXT("vertices"),Vertices);Hulls.Add(MakeShared<FJsonValueObject>(H));
                        }
                    }
                }
            }
            Row->SetArrayField(TEXT("collision_hulls"),Hulls);Buildings.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("buildings"),Buildings);
        auto Quality=MakeShared<FJsonObject>();
        for (const TCHAR* Name:{TEXT("r.ScreenPercentage"),TEXT("r.VSync"),TEXT("t.MaxFPS"),TEXT("sg.ViewDistanceQuality"),
            TEXT("sg.AntiAliasingQuality"),TEXT("sg.ShadowQuality"),TEXT("sg.GlobalIlluminationQuality"),TEXT("sg.ReflectionQuality"),
            TEXT("sg.PostProcessQuality"),TEXT("sg.TextureQuality"),TEXT("sg.EffectsQuality"),TEXT("sg.FoliageQuality"),TEXT("sg.ShadingQuality")})
            if (const auto* CVar=IConsoleManager::Get().FindConsoleVariable(Name)) Quality->SetNumberField(Name,CVar->GetFloat());
        Root->SetObjectField(TEXT("quality"),Quality);
        if (const auto* PC=W->GetFirstPlayerController())
        {
            FVector Position;FRotator Rotation;PC->GetPlayerViewPoint(Position,Rotation);
            Root->SetArrayField(TEXT("camera_position"),Point(Position));Root->SetArrayField(TEXT("camera_rotation"),Point(Rotation.Euler()));
            int32 X,Y;PC->GetViewportSize(X,Y);Root->SetNumberField(TEXT("viewport_width"),X);Root->SetNumberField(TEXT("viewport_height"),Y);
        }
    }
#endif
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}

void UGuLiPerformanceSubsystem::ObserveMovementStep()
{
#if !UE_BUILD_SHIPPING
    if (!MovementCapture || !MovementCapture->Running) return;auto& C=*MovementCapture;
    C.Elapsed=FPlatformTime::Seconds()-C.Started;
    const uint64 CompletedStep=C.Army.IsValid()?C.Army->GetPerformanceCounters().Steps:C.LastStep;
    if (CompletedStep!=C.LastStep)
    {
        const double MovementObservationStarted=FPlatformTime::Seconds();
        if (CompletedStep!=C.LastStep+1) C.Error=TEXT("Logical steps were missed by the observation window");
        C.LastStep=CompletedStep;
        auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("seconds"),C.Elapsed);
        Sample->SetNumberField(TEXT("logical_step"),double(CompletedStep-C.StartStep));
        TArray<TSharedPtr<FJsonValue>> Positions;int32 Overlaps=0,SweptOverlaps=0;
        TArray<FVector> Previous;Previous.Reserve(C.Members.Num());
        for (const auto& M:C.Members) Previous.Add(M.Last);
        for (auto& M:C.Members)
        {
            FGuLiSoldierNavigationDebug N;
            if (!C.Army.IsValid()||!C.Army->TryGetSoldierNavigationDebug(M.Id,N))
            {C.Error=TEXT("Observed member disappeared");break;}
            const double Distance=FVector::Dist2D(N.Location,M.Goal);
            if (M.Commanded&&Distance+2.<M.BestGoalDistance) {M.BestGoalDistance=Distance;M.LastProgress=C.Elapsed;}
            if (M.Commanded&&M.Arrival<0) M.MaximumStall=FMath::Max(M.MaximumStall,C.Elapsed-M.LastProgress);
            if (M.Commanded&&M.Arrival<0&&(N.State==EGuLiSoldierNavigationState::Arrived
                ||(N.LastCompletedOrderId&&N.LastCompletedOrderId!=M.CompletedBefore))) M.Arrival=C.Elapsed;
            M.Blocked|=M.Commanded&&N.LastFailedOrderId&&N.LastFailedOrderId!=M.FailedBefore;
            M.Last=N.Location;auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("id"),M.Id.Value);
            P->SetNumberField(TEXT("state"),uint8(N.State));P->SetNumberField(TEXT("order"),N.ActiveOrderId);
            P->SetNumberField(TEXT("completed_order"),N.LastCompletedOrderId);
            P->SetNumberField(TEXT("failed_order"),N.LastFailedOrderId);
            P->SetNumberField(TEXT("native_no_progress_seconds"),N.NoProgressSeconds);
            P->SetArrayField(TEXT("position"),Point(N.Location));Positions.Add(MakeShared<FJsonValueObject>(P));
        }
        // Large CPU fixtures retain swept-query observations but skip the quadratic pair audit.
        const bool AuditPairs=!C.Recipe->HasField(TEXT("authority_fixture"));
        if (AuditPairs) for (int32 I=0;I<C.Members.Num();++I) for (int32 J=I+1;J<C.Members.Num();++J)
        {
            const auto& A=C.Members[I];const auto& B=C.Members[J];
            if (A.Layer!=B.Layer) continue;
            const double RadiusSquared=FMath::Square(FMath::Max(0.f,A.Radius+B.Radius-2.f));
            if (FVector::DistSquared2D(A.Last,B.Last)<RadiusSquared) ++Overlaps;
            const FVector RelativeStart=(Previous[I]-Previous[J])*FVector(1,1,0);
            const FVector RelativeDelta=(A.Last-Previous[I]-B.Last+Previous[J])*FVector(1,1,0);
            const double T=RelativeDelta.SizeSquared()>UE_SMALL_NUMBER
                ?FMath::Clamp(-FVector::DotProduct(RelativeStart,RelativeDelta)/RelativeDelta.SizeSquared(),0.,1.):0.;
            if ((RelativeStart+RelativeDelta*T).SizeSquared()<RadiusSquared) ++SweptOverlaps;
        }
        Sample->SetArrayField(TEXT("members"),Positions);Sample->SetNumberField(TEXT("overlap_pairs"),AuditPairs?Overlaps:-1);
        Sample->SetNumberField(TEXT("swept_overlap_pairs"),AuditPairs?SweptOverlaps:-1);
        TArray<TSharedPtr<FJsonValue>> Work;
        for (const auto& M:C.Members) if (const auto* Worker=M.Worker.Get())
        {
            auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),M.Id.Value);
            if (const auto* Miner=Cast<AGuLiMiningVehiclePawn>(Worker))
            {
                Row->SetNumberField(TEXT("task_state"),uint8(Miner->GetTaskState()));
                Row->SetNumberField(TEXT("cargo"),Miner->GetCargo().Blue+Miner->GetCargo().Red);
                Row->SetNumberField(TEXT("node"),Miner->GetMiningNodeId());Row->SetBoolField(TEXT("mining"),Miner->IsMiningLaserActive());
                Row->SetBoolField(TEXT("failed"),Miner->DidManagedTaskFail());
            }
            if (const auto* Builder=Worker->FindComponentByClass<UGuLiConstructionWorkComponent>())
            {
                Row->SetNumberField(TEXT("work_phase"),uint8(Builder->GetBehaviorPhase()));
                Row->SetNumberField(TEXT("work_result"),uint8(Builder->GetBehaviorResult()));
                if (const auto* Building=Builder->GetTarget())
                {Row->SetNumberField(TEXT("work_done"),Building->GetState().WorkDone);Row->SetBoolField(TEXT("completed"),Building->IsCompleted());}
            }
            Work.Add(MakeShared<FJsonValueObject>(Row));
        }
        Sample->SetArrayField(TEXT("work"),Work);
        C.Samples.Add(MakeShared<FJsonValueObject>(Sample));
        RecordSample(TEXT("Capture.MovementObservationMs"),(FPlatformTime::Seconds()-MovementObservationStarted)*1000.);
    }
#endif
}

void UGuLiPerformanceSubsystem::TickMovementCapture()
{
#if !UE_BUILD_SHIPPING
    if (!MovementCapture||!MovementCapture->Running) return;auto& C=*MovementCapture;
    C.Elapsed=FPlatformTime::Seconds()-C.Started;
    while (C.Error.IsEmpty()&&C.NextCommand<C.Commands.Num()&&C.Elapsed>=C.Commands[C.NextCommand].At)
    {
        const auto& Command=C.Commands[C.NextCommand];
        bool Accepted=true;
        if (Command.Kind==TEXT("move"))
        {
            if (!C.Sync.IsValid()||!C.Sync->PrepareReviewSoldierSelection(Command.Members,Command.Id))
            {C.Error=TEXT("Frozen commander selection rejected");break;}
            FGuLiUnitTaskCommand Task;Task.CommandId=Command.Id;Task.Target=Command.Target;
            Task.TargetIntent=TEXT("Move");Task.bGroundMoveOnly=true;C.Sync->SubmitOrderedTask(Task);
        }
        else
        {
            if (Command.Kind==TEXT("authority_move"))
            {
                const auto* Owner=GetWorld()->GetFirstPlayerController()->GetPlayerState<AGuLiBattlePlayerState>();
                FGuLiCommanderSelectionState Selection;FGuLiCommandAck Ack;
                Accepted=C.Army->ResolveReviewSoldierSelection(*Owner,Command.Members,Command.Id,Selection);
                FGuLiMoveRequest Request;Request.ClientCommandId=Command.Id;
                Request.SelectionRevision=Selection.SelectionRevision;Request.Target=Command.Target;
                Accepted=Accepted && C.Army->BeginMovePlanning(*Owner,Request,Selection,Ack);
            }
            else if (Command.Kind==TEXT("attack_move"))
            {
                EGuLiTeam Team;uint16 Type;FVector Position;
                Accepted=C.Army->GetTaskSoldierInfo(Command.Members[0],Team,Type,Position)
                    && C.Army->SetReviewAutomaticFire(Command.Members,true)
                    && C.Army->IssueAttackMove(Team,Command.Members,Command.Target);
            }
            else for (const auto Id:Command.Members)
            {
                auto* M=C.Members.FindByPredicate([&](const auto& Member){return Member.Id==Id;});
                auto* Worker=M?M->Worker.Get():nullptr;bool MemberAccepted=false;
                if (Worker)
                {
                    Worker->Tags.AddUnique(TEXT("MovementCaptureActiveWorker"));
                    if (Command.Kind==TEXT("mine") || Command.Kind==TEXT("return"))
                    {
                        auto* Miner=Cast<AGuLiMiningVehiclePawn>(Worker);
                        FGuLiMiningCommand Order;Order.RequestId=Command.Id;Order.SelectionRevision=1;
                        Order.Type=Command.Kind==TEXT("mine")?EGuLiMiningOrderType::MineCluster:EGuLiMiningOrderType::ReturnToFactory;
                        Order.Target=Command.Target;Order.ClusterId=Command.Cluster;
                        MemberAccepted=Miner && Miner->IssuePlayerCommand(Order,Miner->GetTeam());
                    }
                    else
                    {
                        auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
                        FGuLiUnitTaskCommand Task;Task.CommandId=Command.Id;Task.SelectionRevision=1;Task.Target=Command.Target;
                        Task.bGroundMoveOnly=Command.Kind==TEXT("worker_move");
                        if (Command.Kind==TEXT("construct"))
                        {
                            FGuLiCommanderSelectionState Selection;
                            Selection.ActorIds.Add(CastChecked<IGuLiEngineeringVehicle>(Worker)->GetStableActorId());
                            MemberAccepted=Tasks->BuildContextCommand(Selection,Task) && Tasks->SubmitActorCommand(*Worker,Task);
                        }
                        else MemberAccepted=Tasks->SubmitActorCommand(*Worker,Task);
                    }
                }
                Accepted&=MemberAccepted;
            }
            // These are normal authority API return values, not fabricated network ACKs.
            auto Receipt=MakeShared<FJsonObject>();Receipt->SetStringField(TEXT("kind"),Command.Kind);
            Receipt->SetStringField(TEXT("receipt_source"),TEXT("authority_gameplay_api"));
            Receipt->SetNumberField(TEXT("command"),Command.Id);Receipt->SetNumberField(TEXT("seconds"),C.Elapsed);
            Receipt->SetBoolField(TEXT("accepted"),Accepted);C.EventReceipts.Add(MakeShared<FJsonValueObject>(Receipt));
            if (Accepted) C.AckCounts.Add(Command.Id,1);else C.Error=TEXT("Authority gameplay command rejected: ")+Command.Kind;
        }
        for (auto& M:C.Members) if (Command.Members.Contains(M.Id))
        {
            M.Commanded=true;M.LastProgress=C.Elapsed;M.Arrival=-1;M.Goal=Command.Target;
            M.BestGoalDistance=FVector::Dist2D(M.Last,M.Goal);
            FGuLiSoldierNavigationDebug Before;
            if (C.Army->TryGetSoldierNavigationDebug(M.Id,Before))
            {M.CompletedBefore=Before.LastCompletedOrderId;M.FailedBefore=Before.LastFailedOrderId;}
        }
        ++C.NextCommand;
    }
    while (C.Error.IsEmpty() && C.NextOreEvent<C.OreEvents.Num() && C.Elapsed>=C.OreEvents[C.NextOreEvent].At)
    {
        const auto& Event=C.OreEvents[C.NextOreEvent];auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
        auto* State=Resources?Resources->GetResourceWorldState():nullptr;
        const int32 Before=Resources?Resources->GetNodeRemainingRaw(Event.Node):-1;
        const bool Applied=State && State->ApplyHybridNodeRemaining(Event.Node,Event.Remaining);
        auto Receipt=MakeShared<FJsonObject>();Receipt->SetStringField(TEXT("kind"),TEXT("ore_local_projection"));
        Receipt->SetNumberField(TEXT("node"),Event.Node);Receipt->SetNumberField(TEXT("before"),Before);
        Receipt->SetNumberField(TEXT("requested"),Event.Remaining);Receipt->SetNumberField(TEXT("seconds"),C.Elapsed);
        Receipt->SetBoolField(TEXT("applied"),Applied);C.EventReceipts.Add(MakeShared<FJsonValueObject>(Receipt));
        if (!Applied) C.Error=TEXT("Ore local projection event failed");
        ++C.NextOreEvent;
    }
    if (!C.Army.IsValid()) C.Error=TEXT("Movement authority disappeared");
    ObserveMovementStep();
    if (C.Elapsed>=30||!C.Error.IsEmpty())
    {
        EndCapture();C.Running=false;C.Finished=true;bMovementCapturePending=false;
        if (C.Error.IsEmpty())
        {
            if (C.AckCounts.Num()!=C.Commands.Num()) C.Error=TEXT("Incomplete command receipts");
            for (const auto& Pair:C.AckCounts) if (Pair.Value!=1) C.Error=TEXT("Repeated command receipt");
        }
        C.MemoryAfter=FPlatformMemory::GetStats().UsedPhysical;
        if (const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) C.After=H->GetSimulationCensus();
    }
#endif
}

FString UGuLiPerformanceSubsystem::GetMovementCaptureJson() const
{
    auto J=MakeShared<FJsonObject>();
    if (!MovementCapture) J->SetStringField(TEXT("error"),TEXT("No prepared movement capture"));
    else
    {
        const auto& C=*MovementCapture;J->SetStringField(TEXT("error"),C.Error);
        J->SetNumberField(TEXT("observation_schema"),3);
        J->SetArrayField(TEXT("applications"),C.Applications);
        J->SetNumberField(TEXT("applications_observed"),double(C.ApplicationsObserved));
        J->SetNumberField(TEXT("dynamic_sweep_hits"),double(C.DynamicSweepHits));
        J->SetNumberField(TEXT("static_sweep_hits"),double(C.StaticSweepHits));
        J->SetBoolField(TEXT("prepared"),C.Prepared);J->SetBoolField(TEXT("running"),C.Running);J->SetBoolField(TEXT("finished"),C.Finished);
        J->SetNumberField(TEXT("seconds"),C.Elapsed);J->SetNumberField(TEXT("commands_sent"),C.NextCommand);
        J->SetNumberField(TEXT("observed_logical_steps"),double(C.LastStep-C.StartStep));
        J->SetNumberField(TEXT("commands_acknowledged"),C.AckCounts.Num());
        J->SetArrayField(TEXT("event_receipts"),C.EventReceipts);
        J->SetNumberField(TEXT("ore_projection_events_applied"),C.NextOreEvent);
        if (C.Recipe) J->SetObjectField(TEXT("recipe"),C.Recipe);
        if (C.Before) J->SetObjectField(TEXT("census_before"),C.Before);
        if (C.After) J->SetObjectField(TEXT("census_after"),C.After);
        J->SetNumberField(TEXT("memory_before_bytes"),double(C.MemoryBefore));J->SetNumberField(TEXT("memory_after_bytes"),double(C.MemoryAfter));
        J->SetArrayField(TEXT("observations"),C.Samples);TArray<TSharedPtr<FJsonValue>> Members;
        for (const auto& M:C.Members)
        {
            auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("id"),M.Id.Value);R->SetBoolField(TEXT("commanded"),M.Commanded);
            R->SetNumberField(TEXT("arrival_seconds"),M.Arrival);R->SetNumberField(TEXT("maximum_stall_seconds"),M.MaximumStall);
            R->SetBoolField(TEXT("blocked"),M.Blocked);
            R->SetNumberField(TEXT("radius_cm"),M.Radius);R->SetArrayField(TEXT("final_position"),Point(M.Last));
            Members.Add(MakeShared<FJsonValueObject>(R));
        }
        J->SetArrayField(TEXT("members"),Members);
    }
    FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));return Json;
}
