#include "Battle/Combat/GuLiHybridTargetBinding.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Units/Private/GuLiHybridEngineeringState.h"
#include "Commander/Orders/Private/GuLiHybridTaskCommand.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Battle/Combat/GuLiActorDamageReceiverComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"

namespace GuLiHybridMiner
{
constexpr uint32 Version=4;
struct FPrivate
{
    FGuLiHybridObjectKey Key,Factory;
    FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector,Move=FVector::ZeroVector,Aim=FVector::ZeroVector,Approach=FVector::ZeroVector;
    FQuat Rotation=FQuat::Identity;
    uint8 Mode=MOVE_Walking,Custom=0,PendingOperation=0;
    int32 Type=0,NextFactory=0,UnloadAttempts=0;
    EGuLiMiningControlMode Control=EGuLiMiningControlMode::Auto;
    EGuLiMiningTaskState Task=EGuLiMiningTaskState::Idle;
    EGuLiCommanderWorkResult Result=EGuLiCommanderWorkResult::None;
    EGuLiTransitOrderResult TransitResult=EGuLiTransitOrderResult::InvalidRequest;
    uint16 Cluster=0;
    float GraceAt=0,MiningAccumulator=0,DockingAccumulator=0,RetryAt=0;
    uint32 ActiveRequest=0,CompatibilityRequest=0,Node=0,TaskVersion=1;
    bool bManualReturn=false,bManaged=false,bComplete=false,bFailed=false,bWaiting=false;
    FGuLiMiningSlotReservation Slot;
    TArray<FGuLiHybridObjectKey> Returns;
    FGuid PendingEvent;
    uint64 PendingAt=0,NextExtraction=0,ResourceLedger=0;
    TArray<uint8> Travel;
};
void Private(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Schema=Version; A.Value(Schema); if (Schema!=Version) A.Reject();
    A.Value(V.Key); A.Value(V.Factory); A.Value(V.Type); A.Value(V.Position); A.Value(V.Rotation); A.Value(V.Velocity);
    A.Value(V.Mode); A.Value(V.Custom); A.Value(V.Move); A.Value(V.Aim); A.Value(V.Approach);
    A.Enum(V.Control,uint8(EGuLiMiningControlMode::Grace));
    A.Enum(V.Task,uint8(EGuLiMiningTaskState::Blocked)); A.Enum(V.Result,uint8(EGuLiCommanderWorkResult::FactoryUnreachable));
    A.Enum(V.TransitResult,uint8(EGuLiTransitOrderResult::StaleRequest));
    A.Value(V.Cluster); A.Value(V.GraceAt); A.Value(V.MiningAccumulator); A.Value(V.DockingAccumulator); A.Value(V.RetryAt);
    A.Value(V.ActiveRequest); A.Value(V.CompatibilityRequest); A.Value(V.Node); A.Value(V.TaskVersion);
    A.Value(V.bManualReturn); A.Value(V.bManaged); A.Value(V.bComplete); A.Value(V.bFailed); A.Value(V.bWaiting);
    A.Value(V.Slot.Profile); A.Value(V.Slot.Cluster); A.Value(V.Slot.Generation); A.Value(V.Slot.Task); A.Value(V.Slot.Sample); A.Value(V.Slot.Token);
    A.Array(V.Returns,256,[](auto& Ar,auto& K){Ar.Value(K); if (K.IsValid() && K.Kind!=EGuLiHybridObjectKind::Building) Ar.Reject();});
    A.Value(V.NextFactory); A.Value(V.UnloadAttempts); A.Value(V.PendingEvent); A.Value(V.PendingAt); A.Value(V.PendingOperation);
    A.Value(V.NextExtraction); A.Value(V.ResourceLedger); A.Bytes(V.Travel);
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::Miner || V.Type<=0 || V.Mode>=MOVE_MAX
        || (V.Factory.IsValid() && V.Factory.Kind!=EGuLiHybridObjectKind::Building)
        || V.MiningAccumulator<0 || V.DockingAccumulator<0 || !V.TaskVersion || V.NextFactory<0 || V.NextFactory>V.Returns.Num()
        || V.UnloadAttempts<0 || V.UnloadAttempts>4 || V.Travel.IsEmpty()
        || (V.Slot.Token.IsValid() && (!V.Slot.Cluster || !V.Slot.Task || V.Slot.Sample>=8 || V.Slot.Profile!=V.Type))
        || (V.PendingEvent.IsValid() ? V.PendingOperation<1 || V.PendingOperation>2 : V.PendingOperation!=0)) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V) {FGuLiHybridStateArchive A(Bytes); Private(A,V); return A.Done();}
int32 Type(FName Name)
{const auto Text=Name.ToString(); const auto Suffix=Text.RightChop(5); return Text.StartsWith(TEXT("unit/")) && Suffix.IsNumeric()?FCString::Atoi(*Suffix):0;}
}

/** Formal miner Actor. Only its assigned simulator advances the production
 * StateTree, navigation and geometry; money and cargo wait for the ledger. */
class FGuLiHybridMinerAdapter final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<AGuLiMiningVehiclePawn> Pawn;
    FGuLiHybridObjectState Record;
    bool bLocal=false;
    uint64 LastApplied=0,LastPublished=0;
    UWorld* World() const {return Owner.IsValid()?Owner->GetWorld():nullptr;}
    const FGuLiSoldierDefinition* Definition() const
    {const auto* Data=World()?World()->GetSubsystem<UGuLiUnitDataSubsystem>():nullptr; return Data?Data->FindDefinition(uint16(GuLiHybridMiner::Type(Record.Definition))):nullptr;}
    bool Create()
    {
        if (Pawn.IsValid()) return true;
        auto* W=World(); const auto* D=Definition(); auto* Resources=W?W->GetSubsystem<UGuLiResourceWorldSubsystem>():nullptr;
        const auto* Config=Resources?Resources->GetEconomyConfig():nullptr;
        if (!D || !Config || !D->ActorClass || !D->ActorClass->IsChildOf(AGuLiMiningVehiclePawn::StaticClass()) || !Record.Ordinal) return false;
        auto* Factory=Resources->FindNearestFriendlyFactory(Record.Team,Record.Location);
        if (!Factory)
        {
            auto* H=W->GetSubsystem<UGuLiHybridWorldSubsystem>();
            for (const auto& O:Owner->GetManifest().Objects) if (O.Team==Record.Team
                && Owner->GetManifest().Strings.FindRef(TEXT("adapter/definition/")+O.Definition)==TEXT("ResourceFactory"))
                if (auto* Candidate=Cast<AGuLiResourceFactoryActor>(H->FindBoundActor(O.Key))) {Factory=Candidate; break;}
        }
        if (!Factory) return false; // The directory is retried after factory preparation.
        const auto* Health=Owner->GetManifest().Numbers.Find(FString::Printf(TEXT("miner_adapter/%u/health"),D->UnitTypeId));
        if (!Health || *Health!=D->MaxHealth || Record.Health.MaxHealth!=D->MaxHealth) return false;
        const FTransform Transform(FRotator(0,Record.FacingYawDegrees,0),Record.Location);
        auto* P=W->SpawnActorDeferred<AGuLiMiningVehiclePawn>(D->ActorClass,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!P) return false; Pawn=P; P->AutoPossessAI=EAutoPossessAI::Disabled;
        P->FindComponentByClass<UGuLiActorDamageReceiverComponent>()->bDestroyOwnerOnDeath=false;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(P); P->AddInstanceComponent(Binding); Binding->Install(Record,bLocal); Binding->RegisterComponent();
        P->InitializeVehicle(Record.Team,FGuLiControllableActorId(Record.Ordinal),*D,*Config,*Factory); P->FinishSpawning(Transform);
        P->SetReplicates(false); P->SetReplicateMovement(false); P->SetActorTickEnabled(false);
        P->GetCharacterMovement()->SetComponentTickEnabled(false); P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->SetComponentTickEnabled(false);
        if (!P->GetController()) P->SpawnDefaultController(); auto* AI=Cast<AGuLiEngineeringAIController>(P->GetController());
        if (!AI) {Release(); return false;}
        AI->ConfigureVehicleNavigation(); AI->SetActorTickEnabled(false); AI->GetPathFollowingComponent()->SetComponentTickEnabled(false);
        Resources->EnableHybridMiningSimulation(); ApplyLedger(Record,0,false); return true;
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        GuLiHybridMiner::FPrivate V;
        if (!GuLiHybridMiner::Decode(Bytes,V) || V.Key!=Record.Key || V.Type!=GuLiHybridMiner::Type(Record.Definition)) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) {Release(); return true;}
        if (Stage==EGuLiHybridRestoreStage::Create) return Create();
        auto* P=Pawn.Get(); if (!P) return false;
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            P->SetActorLocationAndRotation(V.Position,V.Rotation,false,nullptr,ETeleportType::TeleportPhysics);
            P->GetCharacterMovement()->SetMovementMode(EMovementMode(V.Mode),V.Custom); P->GetCharacterMovement()->Velocity=V.Velocity;
            auto* H=World()->GetSubsystem<UGuLiHybridWorldSubsystem>();
            if (IsValid(P->Factory)) P->Factory->UnregisterMiningVehicle(*P);
            P->Factory=V.Factory.IsValid()?Cast<AGuLiResourceFactoryActor>(H->FindBoundActor(V.Factory)):nullptr;
            if (V.Factory.IsValid() && !P->Factory) return false;
            if (P->Factory) P->Factory->RegisterMiningVehicle(*P);
            P->ControlMode=V.Control; P->TaskState=V.Task; P->BehaviorResult=V.Result;
            P->LastTransitResult=V.TransitResult; P->TargetClusterId=V.Cluster; P->GraceEndServerTime=V.GraceAt;
            P->MiningAccumulator=V.MiningAccumulator; P->DockingAccumulator=V.DockingAccumulator; P->NextAutoRetryServerTime=V.RetryAt;
            P->ActivePlayerRequestId=V.ActiveRequest; P->LastCompatibilityRequestId=V.CompatibilityRequest; P->TargetNodeId=V.Node; P->MiningTaskVersion=V.TaskVersion;
            P->bManualReturnOrder=V.bManualReturn; P->bTaskManaged=V.bManaged; P->bManagedTaskComplete=V.bComplete; P->bManagedTaskFailed=V.bFailed;
            P->bWaitingForMiningSlot=V.bWaiting; P->PlayerMoveTarget=V.Move; P->MiningTarget=V.Aim; P->BehaviorApproach=V.Approach;
            P->MiningSlot=V.Slot; P->MiningSlot.Slot=INDEX_NONE;
            P->ReturnFactories.Reset(); for (const auto& K:V.Returns)
            {
                auto* F=K.IsValid()?Cast<AGuLiResourceFactoryActor>(H->FindBoundActor(K)):nullptr;
                if (K.IsValid() && !F) return false; P->ReturnFactories.Add(F);
            }
            P->NextReturnFactory=V.NextFactory; P->UnloadPointAttempts=V.UnloadAttempts;
            P->PendingResourceEvent=V.PendingEvent; P->PendingResourceOperation=V.PendingOperation; P->PendingResourceAt=V.PendingAt;
            P->NextExtractionTick=V.NextExtraction; P->LastResourceLedger=V.ResourceLedger;
            if (!P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->RestoreHybridTravel(V.Travel,false)) return false;
            LastApplied=LastPublished=0;
        }
        if (Stage==EGuLiHybridRestoreStage::Activate)
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(Record,bLocal);
            if (bLocal && !P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->RestoreHybridTravel(V.Travel,true)) return false;
            P->SetMiningVisual(P->TaskState==EGuLiMiningTaskState::Mining);
        }
        return true;
    }
public:
    explicit FGuLiHybridMinerAdapter(UGuLiHybridActorWorldSubsystem& V):Owner(&V) {}
    ~FGuLiHybridMinerAdapter() override {Release();}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return GuLiHybridMiner::Version;}
    TArray<FName> RequiredSharedStateSystems() const override {return {TEXT("CommanderTasks"),TEXT("MiningSlots")};}
    TArray<FGuLiHybridObjectKey> RestoreDependencies(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridMiner::FPrivate V;TArray<FGuLiHybridObjectKey> Out;
        if (GuLiHybridMiner::Decode(Bytes,V))
        {if (V.Factory.IsValid()) Out.Add(V.Factory);for (const auto& K:V.Returns) if (K.IsValid()) Out.AddUnique(K);}
        return Out;
    }
    bool Prepare(const FGuLiHybridObjectState& V) override {Record=V; return Create();}
    void SetAuthority(const FGuLiHybridObjectState& V,bool Local) override
    {
        Record=V; bLocal=Local; LastApplied=LastPublished=0;
        if (auto* P=Pawn.Get())
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(V,Local);
            if (Local && V.LifeState==1) {P->bTaskManaged=true; P->ControlMode=EGuLiMiningControlMode::PlayerOrder; World()->GetSubsystem<UGuLiUnitTaskSubsystem>()->RegisterActor(*P);}
        }
    }
    void Advance(uint64 At,double Delta) override
    {
        auto* P=Pawn.Get(); if (!P || !bLocal || Record.LifeState!=1 || !UGuLiHybridActorComponent::HasSimulation(P)) return;
        if (!P->PrepareHybridMiningStep()) return;
        const float Dt=static_cast<float>(Delta);
        auto* AI=CastChecked<AGuLiEngineeringAIController>(P->GetController());
        auto* Follow=CastChecked<UGuLiEngineeringCrowdFollowingComponent>(AI->GetPathFollowingComponent()); Follow->RefreshParticipation();
        Follow->TickComponent(Dt,LEVELTICK_All,nullptr); P->GetCharacterMovement()->TickComponent(Dt,LEVELTICK_All,nullptr);
        P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->AdvanceHybrid(Dt); P->AdvanceHybridMining(Dt);
        if (At<LastPublished+6) return;
        FGuLiHybridStateArchive A(65536); uint32 Schema=Version(); A.Value(Schema);
        auto Position=P->GetActorLocation(); auto Rotation=P->GetActorQuat(); auto Velocity=P->GetCharacterMovement()->Velocity;
        A.Value(Position); A.Value(Rotation); A.Value(Velocity); auto Transit=P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->GetTransitState(); GuLiHybridEngineeringState::Transit(A,Transit);

        bool Mining=P->MiningVisual.bActive; uint32 Node=P->MiningVisual.NodeId; FVector Aim=P->MiningVisual.Target;
        A.Value(Mining); A.Value(Node); A.Value(Aim); TArray<uint8> Body;
        if (A.Finish(Body) && World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->PublishMotion(Record.Key,Body)) LastPublished=At;
    }
    bool Execute(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M) override
    {
        auto* P=Pawn.Get(); if (!P || !bLocal || M.Key!=Record.Key) return false;
        FGuLiUnitTaskCommand Command; float Pitch=0,Radius=0;
        if (C.Operation==TEXT("UnitTask")) {if (!GuLiHybridState::DecodeTaskCommand(C.Arguments,Command,Pitch,Radius)) return false;}
        else if (C.Operation==TEXT("Move")) {Command.CommandId=GetTypeHash(C.Event)|1;Command.Kind=EGuLiUnitTaskKind::Move;Command.Target=M.Destination;}
        else return false;
        if (Command.Kind==EGuLiUnitTaskKind::Move) Command.Target=M.Destination;
        AGuLiBattlePlayerState* Player=nullptr;
        for (APlayerState* Entry:World()->GetGameState<AGuLiBattleGameState>()->PlayerArray)
            if (auto* Candidate=Cast<AGuLiBattlePlayerState>(Entry); Candidate && Candidate->GetPlayerGuid()==Record.Controller) {Player=Candidate;break;}
        if (!Player) return false;
        FGuLiCommanderSelectionState Selection; Selection.SelectionRevision=Command.SelectionRevision=1;Selection.ActorIds.Add(FGuLiControllableActorId(Record.Ordinal));
        FString Message;int32 Accepted=0,Rejected=0;return World()->GetSubsystem<UGuLiUnitTaskSubsystem>()->Submit(*Player,Selection,Command,Message,Accepted,Rejected) && Accepted==1;
    }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 At) override
    {
        auto* P=Pawn.Get();if (!P || bLocal) return false;if (At<=LastApplied) return true;
        FGuLiHybridStateArchive A(Bytes,65536);uint32 Schema=0;FVector Position,Velocity,Aim;FQuat Rotation;FGuLiStrongholdTransitState Transit;
        bool Mining=false;uint32 Node=0;
        A.Value(Schema);A.Value(Position);A.Value(Rotation);A.Value(Velocity);GuLiHybridEngineeringState::Transit(A,Transit);
        A.Value(Mining);A.Value(Node);A.Value(Aim);if (Schema!=Version() || !A.Done()) return false;
        P->SetActorLocationAndRotation(Position,Rotation,false,nullptr,ETeleportType::TeleportPhysics);P->GetCharacterMovement()->Velocity=Velocity;
        P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->ApplyHybridTransitMotion(Transit);
        P->MiningVisual={};P->MiningVisual.bActive=Mining;P->MiningVisual.NodeId=Node;P->MiningVisual.Target=Aim;P->OnRep_MiningVisual();LastApplied=At;return true;
    }
    void ApplyLedger(const FGuLiHybridObjectState& V,uint64 Revision,bool Effects) override
    {
        Record=V;if (auto* P=Pawn.Get())
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(V,bLocal);
            FGuLiTargetHandle Target;Target.Kind=EGuLiTargetKind::GroundActor;Target.AuthorityId=V.Key.StableId;Target.Generation=V.Key.LifeGeneration;Target.LocalId=V.Ordinal;
            P->CombatHealth->ApplyHybridHealth(Target,V.Team,V.Health,World()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch(),Revision,Effects);
            if (V.LifeState!=1) {P->GetCharacterMovement()->StopMovementImmediately();P->SetMiningVisual(false);}
        }
    }


    bool Capture(TArray<uint8>& Bytes) const override
    {
        const auto* P=Pawn.Get();if (!P) return false;GuLiHybridMiner::FPrivate V;V.Key=Record.Key;V.Type=P->UnitTypeId;
        auto Key=[](const AActor* Actor){const auto* B=UGuLiHybridActorComponent::Find(Actor);return B?B->GetObjectKey():FGuLiHybridObjectKey();};
        V.Factory=Key(P->Factory);V.Position=P->GetActorLocation();V.Rotation=P->GetActorQuat();V.Velocity=P->GetCharacterMovement()->Velocity;
        V.Mode=uint8(P->GetCharacterMovement()->MovementMode);V.Custom=P->GetCharacterMovement()->CustomMovementMode;
        V.Control=P->ControlMode;V.Task=P->TaskState;V.Result=P->BehaviorResult;V.TransitResult=P->LastTransitResult;
        V.Cluster=P->TargetClusterId;V.GraceAt=P->GraceEndServerTime;V.MiningAccumulator=P->MiningAccumulator;V.DockingAccumulator=P->DockingAccumulator;V.RetryAt=P->NextAutoRetryServerTime;
        V.ActiveRequest=P->ActivePlayerRequestId;V.CompatibilityRequest=P->LastCompatibilityRequestId;V.Node=P->TargetNodeId;V.TaskVersion=P->MiningTaskVersion;
        V.bManualReturn=P->bManualReturnOrder;V.bManaged=P->bTaskManaged;V.bComplete=P->bManagedTaskComplete;V.bFailed=P->bManagedTaskFailed;V.bWaiting=P->bWaitingForMiningSlot;
        V.Move=P->PlayerMoveTarget;V.Aim=P->MiningTarget;V.Approach=P->BehaviorApproach;V.Slot=P->MiningSlot;
        for (const auto& F:P->ReturnFactories) V.Returns.Add(Key(F.Get()));V.NextFactory=P->NextReturnFactory;V.UnloadAttempts=P->UnloadPointAttempts;
        V.PendingEvent=P->PendingResourceEvent;V.PendingOperation=P->PendingResourceOperation;V.PendingAt=P->PendingResourceAt;V.NextExtraction=P->NextExtractionTick;V.ResourceLedger=P->LastResourceLedger;
        if (!P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->CaptureHybridTravel(V.Travel)) return false;
        FGuLiHybridStateArchive A;GuLiHybridMiner::Private(A,V);return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridMiner::FPrivate V;const auto* D=Definition();const auto* P=Pawn.Get();
        if (!D || !P || !GuLiHybridMiner::Decode(Bytes,V) || V.Key!=Record.Key || V.Type!=D->UnitTypeId
            || !P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->ValidateHybridTravel(V.Travel)) return false;
        const auto* H=World()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        auto Reference=[&](const FGuLiHybridObjectKey& K)
        {
            FGuLiHybridObjectState O;
            return !K.IsValid() || (H->GetObject(K,O) && O.Team==Record.Team && O.Key.Kind==EGuLiHybridObjectKind::Building
                && Owner->GetManifest().Strings.FindRef(TEXT("adapter/definition/")+O.Definition.ToString())==TEXT("ResourceFactory"));
        };
        if (!Reference(V.Factory)) return false;for (const auto& K:V.Returns) if (!Reference(K)) return false;return true;
    }
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override {return RestoreImpl(B,S);}
    bool RestoreQuery(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override {return RestoreImpl(B,S);}
    void Release() override {if (auto* P=Pawn.Get()) {if (auto* AI=P->GetController()) AI->Destroy();P->Destroy();}Pawn.Reset();LastApplied=LastPublished=0;}
};
static const FGuLiHybridAdapterRegistration GGuLiMinerAdapterFamily({TEXT("Miner"),EGuLiHybridObjectKind::Miner,GuLiHybridMiner::Version,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridMinerAdapter>(Owner);},nullptr,GuLiHybridTargets::Actor<EGuLiTargetKind::GroundActor,true>,EGuLiHybridOwnershipPolicy::SharedGroup,0,false,TEXT("match/resources")});
