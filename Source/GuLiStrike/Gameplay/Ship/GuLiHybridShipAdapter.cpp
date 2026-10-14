#include "Battle/Combat/GuLiHybridTargetBinding.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Ship/Private/GuLiHybridShipInput.h"
#include "Network/Private/GuLiHybridReflectedState.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"

namespace GuLiHybridShip
{
constexpr uint32 StateVersion=7;
struct FPose
{
    FTransform Transform=FTransform::Identity;
    FVector Velocity=FVector::ZeroVector;
    float YawVelocity=0,Bank=0;
    uint8 Mode=MOVE_Flying,CustomMode=0;
};
struct FPart { FName Socket; FString Class; };
struct FSalvoDecision { FGuid Request; FGuLiWingmanMissileSalvoResult Result; };
struct FPrivate
{
    FGuLiHybridObjectKey Key;
    FPose Pose;
    FGuLiHybridShipInput Simulation,ControllerInput;
    bool bSemantic=false,bDeathHandled=false;
    FGuLiShipMovementConfig Config;
    uint32 LoadoutRevision=0,ConfigRevision=0,Barrier=0,CanonicalEpoch=0,CanonicalRevision=0;
    uint64 PoseTick=0;
    TArray<FPart> Parts;
    TArray<FGuLiStrikeStatModifier> Modifiers;
    FGuLiShipGMRuntimeReplicatedState GM;
    TArray<FGuLiShipCanonicalMoveState> History;
    FGuLiWingmanAttackTarget ManualTarget;
    TArray<FSalvoDecision> Salvos;
};
void Pose(FGuLiHybridStateArchive& A,FPose& V)
{
    A.Value(V.Transform); A.Value(V.Velocity); A.Value(V.YawVelocity); A.Value(V.Bank); A.Value(V.Mode); A.Value(V.CustomMode);
    if (V.Mode>=MOVE_MAX) A.Reject();
}
void Configuration(FGuLiHybridStateArchive& A,FGuLiShipMovementConfig& V)
{
    A.Value(V.MaxFlySpeed); A.Value(V.MaxAcceleration); A.Value(V.BrakingDecelerationFlying); A.Value(V.BoostThrustMultiplier);
    A.Value(V.YawRate); A.Value(V.YawResponseSpeed); A.Value(V.YawStopDamping); A.Value(V.MaxBankAngle); A.Value(V.BankInterpSpeed);
    A.Value(V.OrientTurnSpeed); A.Value(V.OrientMinForwardDot); A.Value(V.bOrientToMovement); if (!V.IsValid()) A.Reject();
}
void Private(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Version=StateVersion; A.Value(Version); if (Version!=StateVersion) A.Reject();
    A.Value(V.Key); Pose(A,V.Pose); Input(A,V.Simulation); Input(A,V.ControllerInput);
    A.Value(V.bSemantic); A.Value(V.bDeathHandled); Configuration(A,V.Config);
    A.Value(V.LoadoutRevision); A.Value(V.ConfigRevision); A.Value(V.Barrier);
    A.Value(V.CanonicalEpoch); A.Value(V.CanonicalRevision);
    A.Value(V.PoseTick);
    A.Array(V.Parts,128,[](auto& Ar,auto& P){ Ar.Value(P.Socket); Ar.String(P.Class); if (P.Socket.IsNone() || P.Class.IsEmpty()) Ar.Reject(); });
    A.Array(V.Modifiers,256,[](auto& Ar,auto& M)
    { Ar.Value(M.Name); Ar.Value(M.MaxSpeedMultiplier); Ar.Value(M.AccelerationMultiplier);
      if (M.Name.IsNone() || M.MaxSpeedMultiplier<0 || M.AccelerationMultiplier<0) Ar.Reject(); });
    A.Value(V.GM.MaxSpeedMultiplier); A.Value(V.GM.AccelerationMultiplier); A.Value(V.GM.Revision); A.Value(V.GM.bActive);
    A.Array(V.History,256,[](auto& Ar,auto& S)
    { Ar.Value(S.CanonicalEpoch); Ar.Value(S.MoveRevision); Ar.Value(S.MovementConfigRevision); Ar.Value(S.BarrierGeneration);
      Ar.Value(S.ServerWorldTimeSeconds); Ar.Value(S.Transform); Ar.Value(S.Velocity); if (!S.IsValid()) Ar.Reject(); });
    FGuLiHybridReflectedState::Value(A,V.ManualTarget);
    A.Array(V.Salvos,256,[](auto& Ar,auto& S)
    {
        Ar.Value(S.Request);Ar.Enum(S.Result.RejectReason,uint8(EGuLiWingmanRejectReason::AtomicBatchExpired));Ar.Value(S.Result.FlightIndex);
        Ar.Value(S.Result.LaunchedCount);Ar.Value(S.Result.bSharedCooldownStarted);
        FGuLiHybridReflectedState::Value(Ar,S.Result.SelectedTarget);
        if (!S.Request.IsValid() || S.Result.LaunchedCount<0 || S.Result.LaunchedCount>GULI_WINGMAN_MEMBERS_PER_FLIGHT) Ar.Reject();
    });
    TSet<FGuid> Requests;for (const auto& S:V.Salvos) {if (Requests.Contains(S.Request)) A.Reject();Requests.Add(S.Request);}
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::Ship || !V.LoadoutRevision || !V.ConfigRevision || !V.Barrier
        || V.GM.MaxSpeedMultiplier<0 || V.GM.AccelerationMultiplier<0) A.Reject();
    TSet<FName> Sockets,Names;
    for (const auto& P : V.Parts) { if (Sockets.Contains(P.Socket)) A.Reject(); Sockets.Add(P.Socket); }
    for (const auto& M : V.Modifiers) { if (Names.Contains(M.Name)) A.Reject(); Names.Add(M.Name); }
    uint32 Revision=0; double At=-1;
    for (const auto& S : V.History)
    {
        if (S.CanonicalEpoch!=V.CanonicalEpoch || S.MoveRevision<=Revision || S.MoveRevision>V.CanonicalRevision || S.ServerWorldTimeSeconds<At) A.Reject();
        Revision=S.MoveRevision; At=S.ServerWorldTimeSeconds;
    }
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V)
{ FGuLiHybridStateArchive A(Bytes); Private(A,V); return A.Done(); }
}

/** World-owned adapter for the existing production Air character and native CMC integrator. */
class FGuLiHybridShipAdapter final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<AGuLiStrikeShip> Pawn;
    FGuLiHybridObjectState Record;
    FGuLiHybridShipInput Simulation;
    bool bLocal=false;
    uint64 LastPublished=0,LastApplied=0;
    UWorld* World() const { return Owner.IsValid()?Owner->GetWorld():nullptr; }
    AGuLiBattlePlayerState* Player() const
    {
        const auto* Battle=World()?World()->GetGameState<AGuLiBattleGameState>():nullptr;
        if (Battle) for (APlayerState* Entry : Battle->PlayerArray)
            if (auto* PS=Cast<AGuLiBattlePlayerState>(Entry); PS && PS->GetPlayerGuid()==Record.Controller) return PS;
        return nullptr;
    }
    void BindPlayer()
    {
        auto* P=Pawn.Get(); auto* W=World(); if (!P || !W) return; auto* PS=Player(); P->SetPlayerState(PS);
        const auto* Session=W->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>(); auto* PC=W->GetFirstPlayerController();
        if (PC && Session && PS && PS->GetBattleRole()==EGuLiCommanderRole::Air && Record.Controller==Session->GetLocalPlayerId())
        { if (PC->GetPawn()!=P) PC->Possess(P); }
        else if (PC && PC->GetPawn()==P) PC->UnPossess();
    }
    bool Create()
    {
        if (Pawn.IsValid()) return true;
        auto* W=World(); const auto* ClassPath=Owner.IsValid()?Owner->GetManifest().Strings.Find(TEXT("startup/ship_class")):nullptr;
        UClass* Class=ClassPath?LoadClass<AGuLiStrikeShip>(nullptr,**ClassPath):nullptr;
        const auto* CDO=Class?Class->GetDefaultObject<AGuLiStrikeShip>():nullptr;
        if (!W || !Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) || Record.Definition!=TEXT("player/ship")
            || !CDO->GetShipAssembly()->ValidateHybridCapabilityContracts(Owner->GetManifest())) return false;
        const FTransform Transform(FRotator(0,Record.FacingYawDegrees,0),Record.Location);
        auto* P=W->SpawnActorDeferred<AGuLiStrikeShip>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!P) return false; Pawn=P; P->SetPlayerState(Player());
        P->AutoPossessPlayer=EAutoReceiveInput::Disabled; P->AutoPossessAI=EAutoPossessAI::Disabled;
        P->GetCharacterMovement()->bRunPhysicsWithNoController=true;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(P); P->AddInstanceComponent(Binding); Binding->RegisterComponent(); Binding->Install(Record,bLocal);
        P->FinishSpawning(Transform); P->SetReplicates(false);
        if (!P->GetShipMovement() || !P->IsShipReady()) { Release(); return false; }
        P->GetShipMovement()->SetComponentTickEnabled(false); BindPlayer();
        P->RefreshServerCombatRegistration(); ApplyLedger(Record,0,false); if (Record.bDormant) ResetControlInput(); return true;
    }
    GuLiHybridShip::FPose ReadPose() const
    {
        GuLiHybridShip::FPose V; auto* P=Pawn.Get(); auto* M=P->GetShipMovement();
        V.Transform=P->GetActorTransform(); V.Velocity=M->Velocity; V.YawVelocity=M->YawVelocity; V.Bank=M->CurrentBankRoll;
        V.Mode=M->MovementMode; V.CustomMode=M->CustomMovementMode; return V;
    }
    void ApplyPose(const GuLiHybridShip::FPose& V)
    {
        auto* P=Pawn.Get(); auto* M=P->GetShipMovement(); P->SetActorTransform(V.Transform,false,nullptr,ETeleportType::TeleportPhysics);
        M->Velocity=V.Velocity; M->YawVelocity=V.YawVelocity; M->CurrentBankRoll=V.Bank; M->SetMovementMode(EMovementMode(V.Mode),V.CustomMode);
        P->SetBase(nullptr); M->bJustTeleported=true;
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        GuLiHybridShip::FPrivate V; if (!GuLiHybridShip::Decode(Bytes,V) || V.Key!=Record.Key) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) { Release(); return true; }
        if (Stage==EGuLiHybridRestoreStage::Create) return Create() && Validate(Bytes);
        if (!Validate(Bytes)) return false;
        auto* P=Pawn.Get(); if (!P) return false;
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            BindPlayer(); auto* M=P->GetShipMovement();
            TGuardValue<bool> Editing(P->bEditingLoadout,true);
            TArray<FName> Old; for (const auto& Part : P->InstalledParts)
                if (!P->ShipAssembly->OwnsPart(Part.Part.Get())) Old.Add(Part.SocketName);
            for (const auto Socket : Old) P->UninstallPartLocally(Socket);
            for (const auto& Part : V.Parts)
                if (!P->InstallPartLocally(LoadClass<UGuLiStrikeShipPartComponent>(nullptr,*Part.Class),Part.Socket)) return false;
            P->StatModifiers=V.Modifiers; P->GMRuntimeState=V.GM; P->bShipDeathHandled=V.bDeathHandled;
            P->LoadoutState.Revision=P->AppliedLoadoutRevision=V.LoadoutRevision;
            P->LoadoutState.Parts.Reset(); for (const auto& Part : P->InstalledParts)
                if (Part.Part.IsValid()) P->LoadoutState.Parts.Add({Part.SocketName,Part.Part->GetClass()});
            P->bLoadoutDirty=false;
            M->MovementSyncState.Config=V.Config; M->MovementSyncState.ConfigRevision=V.ConfigRevision;
            M->MovementSyncState.RequiredLoadoutRevision=V.LoadoutRevision; M->MovementSyncState.BarrierGeneration=V.Barrier;
            M->AppliedLoadoutRevision=V.LoadoutRevision; M->ApplyMovementParameters(V.Config);
            M->CanonicalMoveHistory.States=V.History; M->CanonicalMoveHistory.CanonicalEpoch=V.CanonicalEpoch;
            M->CanonicalMoveHistory.LatestRevision=V.CanonicalRevision; M->CanonicalMoveHistory.MaxEntries=256;
            ApplyPose(V.Pose); Simulation=V.Simulation; P->HybridInput=V.ControllerInput; P->bSemanticFlightInput=V.bSemantic;
            P->HybridPoseTick=V.PoseTick;
            P->WingmanAttackTarget=V.ManualTarget;
            P->WingmanMissileRequestResults.Reset();P->WingmanMissileRequestOrder.Reset();
            for (const auto& S:V.Salvos) {P->WingmanMissileRequestOrder.Add(S.Request);P->WingmanMissileRequestResults.Add(S.Request,S.Result);}
            P->bHybridInputSent=false; LastPublished=LastApplied=0; return true;
        }
        if (Stage!=EGuLiHybridRestoreStage::Activate) return false;
        if (Record.bDormant) ResetControlInput();
        if (bLocal && (!Simulation.Thrust.IsNearlyZero() || !FMath::IsNearlyZero(Simulation.Turn))) ++P->HybridFlightRestoreCount;
        return true;
    }
public:
    explicit FGuLiHybridShipAdapter(UGuLiHybridActorWorldSubsystem& InOwner):Owner(&InOwner) {}
    ~FGuLiHybridShipAdapter() override { Release(); }
    UObject* LifetimeOwner() const override { return Owner.Get(); }
    uint32 Version() const override { return GuLiHybridShip::StateVersion; }
    TArray<FName> RequiredSharedStateSystems() const override { return {TEXT("ShipControls")}; }
    bool Prepare(const FGuLiHybridObjectState& State) override { Record=State; return Create(); }
    void SetAuthority(const FGuLiHybridObjectState& State,bool Local) override
    { Record=State; bLocal=Local; if (auto* P=Pawn.Get()) { P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,Local); P->SetCarrierDormant(State.bDormant); } BindPlayer(); if (State.bDormant) ResetControlInput(); }
    void ResetControlInput() override
    {
        if (auto* P=Pawn.Get())
        {
            Simulation={}; P->HybridInput={}; P->bSemanticFlightInput=false; P->bHybridInputSent=false;
            P->ConsumeMovementInputVector();
            auto* M=P->GetShipMovement(); M->Velocity=FVector::ZeroVector; M->YawVelocity=0;
        }
    }
    void Advance(uint64 Tick,double Delta) override
    {
        auto* P=Pawn.Get(); if (!P || !bLocal || !UGuLiHybridActorComponent::HasSimulation(P)) return;
        P->AdvanceHybridFlight(Delta,Simulation); P->HybridPoseTick=Tick; if (Tick%2 || Tick==LastPublished) return;
        FGuLiHybridStateArchive A(700); uint32 Schema=1; A.Value(Schema); auto Pose=ReadPose(); GuLiHybridShip::Pose(A,Pose);
        TArray<uint8> Body;
        if (!A.Finish(Body)) { ++P->HybridMotionEncodingFailures; return; }
        if (World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->PublishMotion(Record.Key,Body)) LastPublished=Tick;
    }
    bool Execute(const FGuLiHybridCommand& Command,const FGuLiHybridCommandMember& Member) override
    {
        FGuLiHybridShipInput Input;
        if (!Pawn.IsValid() || !bLocal || Member.Key!=Record.Key || Command.Operation!=TEXT("ShipFlightInput") || !GuLiHybridShip::Decode(Command.Arguments,Input)) return false;
        Simulation=Input; return true;
    }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 Tick) override
    {
        if (!Pawn.IsValid() || bLocal) return false; if (Tick<=LastApplied) return true;
        FGuLiHybridStateArchive A(Bytes,700); uint32 Schema=0; A.Value(Schema); GuLiHybridShip::FPose V; GuLiHybridShip::Pose(A,V);
        if (Schema!=1 || !A.Done()) return false; ApplyPose(V); Pawn->HybridPoseTick=Tick; LastApplied=Tick; return true;
    }
    void ApplyLedger(const FGuLiHybridObjectState& State,uint64 Revision,bool Effects) override
    {
        Record=State; auto* P=Pawn.Get(); if (!P) return;
        P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,bLocal);
        FGuLiTargetHandle Target; Target.Kind=EGuLiTargetKind::Ship; Target.AuthorityId=State.Key.StableId; Target.Generation=State.Key.LifeGeneration;
        const auto* Battle=World()->GetGameState<AGuLiBattleGameState>();
        P->CombatHealth->ApplyHybridHealth(Target,State.Team,State.Health,Battle?Battle->GetMatchEpoch():0,Revision,Effects);
        if (State.bDormant) ResetControlInput();
    }
    bool Capture(TArray<uint8>& Bytes) const override
    {
        auto* P=Pawn.Get(); if (!P || !P->IsShipReady()) return false; const auto* M=P->GetShipMovement(); const auto* Assembly=P->ShipAssembly.Get();
        if (Assembly->bPrepared || Assembly->bCommitting) return false;
        GuLiHybridShip::FPrivate V; V.Key=Record.Key; V.Pose=ReadPose(); V.Simulation=Simulation; V.ControllerInput=P->HybridInput;
        V.bSemantic=P->bSemanticFlightInput; V.bDeathHandled=P->bShipDeathHandled; V.Config=M->EffectiveConfig;
        V.LoadoutRevision=P->LoadoutState.Revision; V.ConfigRevision=M->MovementSyncState.ConfigRevision; V.Barrier=M->MovementSyncState.BarrierGeneration;
        V.CanonicalEpoch=M->CanonicalMoveHistory.CanonicalEpoch; V.CanonicalRevision=M->CanonicalMoveHistory.LatestRevision; V.History=M->CanonicalMoveHistory.States;
        V.PoseTick=P->HybridPoseTick;
        V.ManualTarget=P->WingmanAttackTarget;
        for (const auto Request:P->WingmanMissileRequestOrder)
        {
            const auto* Result=P->WingmanMissileRequestResults.Find(Request);if (!Result) return false;
            V.Salvos.Add({Request,*Result});
        }
        for (const auto& Part : P->InstalledParts)
        { if (!Part.Part.IsValid()) return false; if (!Assembly->OwnsPart(Part.Part.Get())) V.Parts.Add({Part.SocketName,Part.Part->GetClass()->GetPathName()}); }
        V.Modifiers=P->StatModifiers; V.GM=P->GMRuntimeState;
        FGuLiHybridStateArchive A; GuLiHybridShip::Private(A,V); return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridShip::FPrivate V; if (!GuLiHybridShip::Decode(Bytes,V) || V.Key!=Record.Key) return false;
        const auto* Path=Owner.IsValid()?Owner->GetManifest().Strings.Find(TEXT("startup/ship_class")):nullptr;
        auto* Class=Path?LoadClass<AGuLiStrikeShip>(nullptr,**Path):nullptr; const auto* CDO=Class?Class->GetDefaultObject<AGuLiStrikeShip>():nullptr;
        if (!CDO) return false;
        for (const auto& Part : V.Parts) if (!CDO->IsKnownPartClass(LoadClass<UGuLiStrikeShipPartComponent>(nullptr,*Part.Class))) return false;
        return true;
    }
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override { return RestoreImpl(Bytes,Stage); }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override { return RestoreImpl(Bytes,Stage); }
    void Release() override { if (auto* P=Pawn.Get()) P->Destroy(); Pawn.Reset(); }
};

class FGuLiHybridShipControls final : public IGuLiHybridSharedStateAdapter
{
    struct FInput { FGuLiHybridObjectKey Key; FGuLiHybridShipInput Value; bool bSemantic=false; };
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    static void Serialize(FGuLiHybridStateArchive& A,FGuid& Player,TArray<FInput>& Inputs)
    {
        uint32 Version=1; A.Value(Version); if (Version!=1) A.Reject(); A.Value(Player); if (!Player.IsValid()) A.Reject();
        A.Array(Inputs,16,[](auto& Ar,auto& I)
        { Ar.Value(I.Key); GuLiHybridShip::Input(Ar,I.Value); Ar.Value(I.bSemantic);
          if (!I.Key.IsValid() || I.Key.Kind!=EGuLiHybridObjectKind::Ship) Ar.Reject(); });
    }
public:
    explicit FGuLiHybridShipControls(UGuLiHybridActorWorldSubsystem& InOwner):Owner(&InOwner) {}
    UObject* LifetimeOwner() const override { return Owner.Get(); }
    uint32 Version() const override { return 1; }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    {
        if (!Owner.IsValid()) return false; auto* W=Owner->GetWorld(); const auto* Session=W->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
        const auto* Hybrid=W->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!Session || !Hybrid) return false;
        FGuid Player=Session->GetLocalPlayerId(); TArray<FInput> Inputs;
        for (TActorIterator<AGuLiStrikeShip> It(W);It;++It) if (const auto* Binding=UGuLiHybridActorComponent::Find(*It); Binding && Objects.Contains(Binding->GetObjectKey()) && Hybrid->HasControl(Binding->GetObjectKey()))
            Inputs.Add({Binding->GetObjectKey(),It->HybridInput,It->bSemanticFlightInput});
        FGuLiHybridStateArchive A; Serialize(A,Player,Inputs); return A.Finish(Bytes);
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>&) const override
    {
        if (Pieces.IsEmpty()) return false; TSet<FGuid> Players; TSet<FGuLiHybridObjectKey> Keys;
        for (const auto& Piece : Pieces)
        {
            FGuid Player; TArray<FInput> Inputs; FGuLiHybridStateArchive A(Piece.Bytes); Serialize(A,Player,Inputs);
            if (!A.Done() || Piece.Version!=Version() || Player!=Piece.Player || Players.Contains(Player)) return false; Players.Add(Player);
            for (const auto& I : Inputs) { if (Keys.Contains(I.Key)) return false; Keys.Add(I.Key); }
        }
        return true;
    }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    {
        if (!Owner.IsValid() || !Validate(Pieces,Objects)) return false; if (Stage!=EGuLiHybridRestoreStage::Activate) return true;
        auto* W=Owner->GetWorld(); const auto* Session=W->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
        const auto* Hybrid=W->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!Session || !Hybrid) return false;
        for (const auto& Piece : Pieces) if (Piece.Player==Session->GetLocalPlayerId())
        {
            FGuid Player; TArray<FInput> Inputs; FGuLiHybridStateArchive A(Piece.Bytes); Serialize(A,Player,Inputs);
            for (const auto& I : Inputs) for (TActorIterator<AGuLiStrikeShip> It(W);It;++It)
                if (const auto* Binding=UGuLiHybridActorComponent::Find(*It); Binding && Binding->GetObjectKey()==I.Key && Hybrid->HasControl(I.Key))
                { It->HybridInput=I.Value; It->bSemanticFlightInput=I.bSemantic; It->bHybridInputSent=false; }
        }
        return true;
    }
};

static const FGuLiHybridAdapterRegistration GGuLiShipAdapterFamily({TEXT("ShipPlayer"),EGuLiHybridObjectKind::Ship,GuLiHybridShip::StateVersion,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridShipAdapter>(Owner);},
    [](UGuLiHybridActorWorldSubsystem& Owner)
    {return Owner.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->RegisterSharedStateSystem(TEXT("ShipControls"),MakeShared<FGuLiHybridShipControls>(Owner));},GuLiHybridTargets::Actor<EGuLiTargetKind::Ship>,EGuLiHybridOwnershipPolicy::ControllerPinned,2,false});
