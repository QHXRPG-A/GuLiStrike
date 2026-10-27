#include "Battle/Combat/GuLiHybridTargetBinding.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/GroundMech/Network/Private/GuLiHybridGroundInput.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkComponent.h"
#include "Gameplay/GroundMech/Camera/GuLiGroundMechCameraComponent.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponAnimInstance.h"
#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGroundMechRocketComponent.h"
#include "Gameplay/GroundMech/Abilities/Shared/GuLiGroundMechAttributes.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"

namespace GuLiHybridGround
{
constexpr uint32 StateVersion=11;
struct FPose
{
    FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector;
    FQuat Rotation=FQuat::Identity;
    uint8 Mode=MOVE_Walking,CustomMode=0;
    uint16 AimYaw=0;
    float DisplayAimYaw=0,Fuel=0,MaxFuel=0;
    bool bThrusting=false,bJet=false;
    bool bFaceAim=false;
    bool bFaceCamera=false;
    uint16 CameraYaw=0;
    FGuLiGroundMechTurnState Turn;
    FGuLiGroundMechWeaponAimState WeaponAim;
    FGuLiGroundMechBlinkState Blink;
    FVector2D MoveAxis=FVector2D::ZeroVector;
};
struct FPrivate
{
    FGuLiHybridObjectKey Key;
    FPose Pose;
    FGuLiHybridGroundInput Simulation,ControllerInput;
    bool bSemantic=false,bSprint=false,bAbility=false,bRocketHeld=false,bRocketRequested=false,bRocketThrusting=false,bRocketAllowed=false;
    float FuelRecoveryElapsed=0;
    FGuLiMassSupportState Support;
    FString Upgrade;
    double NextShot=0;
    int32 Shots=0;
    FVector WeaponAim=FVector::ZeroVector;
    bool bFire=false,bHasAim=false;
    float RecoilAge=100,RecoilStart=0,RecoilRest=0,RecoilAlpha=0,RecoilOffset=0;
    TArray<FGuid> SeenShots;
    TArray<uint8> Placement;
};
void Pose(FGuLiHybridStateArchive& A,FPose& V)
{
    A.Value(V.Position); A.Value(V.Rotation); A.Value(V.Velocity); A.Value(V.Mode); A.Value(V.CustomMode);
    A.Value(V.AimYaw); A.Value(V.DisplayAimYaw); A.Value(V.Fuel); A.Value(V.MaxFuel); A.Value(V.bThrusting); A.Value(V.bJet);
    A.Value(V.bFaceAim); A.Value(V.bFaceCamera); A.Value(V.CameraYaw); auto& T=V.Turn;
    A.Value(T.Serial); A.Value(T.Direction); A.Value(T.Time); A.Value(T.Duration);
    A.Value(T.StartYaw); A.Value(T.TargetDegrees); A.Value(T.Progress);
    A.Value(V.MoveAxis); A.Value(V.WeaponAim.bForward); A.Value(V.WeaponAim.ReturnAlpha);
    A.Value(V.WeaponAim.RightStart); A.Value(V.WeaponAim.LeftStart);
    auto& B=V.Blink;
    A.Value(B.Phase); A.Value(B.Serial); A.Value(B.LastRequest); A.Value(B.Elapsed); A.Value(B.Cooldown);
    A.Value(B.Start); A.Value(B.End); A.Value(B.Direction); A.Value(B.Yaw);
    if (B.Phase>3 || B.Serial<0 || B.Elapsed<0 || B.Elapsed>1.1f || B.Cooldown<0 || B.Cooldown>60
        || !B.Direction.IsNormalized() || FMath::Abs(B.Direction.Z)>.001 || FVector::DistSquared(B.Start,B.End)>FMath::Square(5501.f)) A.Reject();
    if (V.MoveAxis.SizeSquared()>1.00001 || V.WeaponAim.ReturnAlpha<0 || V.WeaponAim.ReturnAlpha>1) A.Reject();
    if (T.Serial<0 || T.Direction< -1 || T.Direction>1 || T.Time<0 || T.Duration<0 || T.Time>T.Duration+.001f
        || FMath::Abs(T.TargetDegrees)>180 || T.Progress<0 || T.Progress>1) A.Reject();
    if (V.Mode>=MOVE_MAX || V.Fuel<0 || V.MaxFuel<=0 || V.Fuel>V.MaxFuel+.001f) A.Reject();
}
void Private(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Version=StateVersion; A.Value(Version); if (Version!=StateVersion) A.Reject();
    A.Value(V.Key); Pose(A,V.Pose); Input(A,V.Simulation); Input(A,V.ControllerInput);
    A.Value(V.bSemantic); A.Value(V.bSprint); A.Value(V.bAbility); A.Value(V.bRocketHeld); A.Value(V.bRocketRequested);
    A.Value(V.bRocketThrusting); A.Value(V.bRocketAllowed); A.Value(V.FuelRecoveryElapsed);
    auto& S=V.Support; A.Value(S.SoldierId.Value); A.Value(S.Epoch); A.Value(S.DisplacementRevision);
    A.Value(S.BodyLocation); A.Value(S.RelativeLocation); A.Value(S.TopZ); A.Value(S.Radius); A.Value(S.SimulationSeconds);
    A.Value(S.SourceLocation); A.Value(S.SourceTopZ); A.Value(S.bHasSourceReference);
    A.String(V.Upgrade,128); A.Value(V.NextShot); A.Value(V.Shots); A.Value(V.WeaponAim); A.Value(V.bFire); A.Value(V.bHasAim);
    A.Value(V.RecoilAge); A.Value(V.RecoilStart); A.Value(V.RecoilRest); A.Value(V.RecoilAlpha); A.Value(V.RecoilOffset);
    A.Array(V.SeenShots,8192,[](auto& Ar,auto& ID){ Ar.Value(ID); if (!ID.IsValid()) Ar.Reject(); });
    A.Bytes(V.Placement,65536);
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::GroundPlayer || V.FuelRecoveryElapsed<0
        || S.Radius<0 || S.SimulationSeconds<0
        || (V.bAbility && !V.Simulation.bRocket) || V.Upgrade.IsEmpty() || V.NextShot<0 || V.Shots<0 || V.RecoilAge<0
        || V.RecoilStart<0 || V.RecoilStart>1 || V.RecoilAlpha<0 || V.RecoilAlpha>1) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V)
{ FGuLiHybridStateArchive A(Bytes); Private(A,V); return A.Done(); }
}

/** Adapts the production Ground character. It is deliberately separate from Mass WM01. */
class FGuLiHybridGroundAdapter final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<AGuLiGroundMechCharacter> Pawn;
    FGuLiHybridObjectState Record;
    bool bLocal=false;
    uint64 LastPublished=0,LastApplied=0;
    UWorld* World() const { return Owner.IsValid()?Owner->GetWorld():nullptr; }
    UGuLiHybridActorComponent* Identity() const
    { return Pawn.IsValid()?Pawn->FindComponentByClass<UGuLiHybridActorComponent>():nullptr; }
    UGuLiGroundMechMovementComponent* Movement() const
    { return Pawn.IsValid()?Cast<UGuLiGroundMechMovementComponent>(Pawn->GetCharacterMovement()):nullptr; }
    bool Create()
    {
        if (Pawn.IsValid()) return true;
        auto* W=World(); if (!W || !Owner.IsValid()) return false;
        const auto* Path=Owner->GetManifest().Strings.Find(TEXT("startup/ground_class"));
        auto* Class=Path?LoadClass<AGuLiGroundMechCharacter>(nullptr,**Path):nullptr;
        if (!Class || Record.Definition!=TEXT("player/ground")) return false;
        const FTransform Transform(FRotator(0,Record.FacingYawDegrees,0),Record.Location);
        auto* NewPawn=W->SpawnActorDeferred<AGuLiGroundMechCharacter>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!NewPawn) return false;
        Pawn=NewPawn; NewPawn->AutoPossessAI=EAutoPossessAI::Disabled; NewPawn->AutoPossessPlayer=EAutoReceiveInput::Disabled;
        // Restore and query pawns are created before possession is rebuilt.
        Movement()->bRunPhysicsWithNoController=true;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(NewPawn); NewPawn->AddInstanceComponent(Binding);
        Binding->Install(Record,bLocal); Binding->RegisterComponent();
        auto* Placement=NewObject<UGuLiBuildingPlacementComponent>(NewPawn); NewPawn->AddInstanceComponent(Placement); Placement->RegisterComponent();
        NewPawn->FinishSpawning(Transform); NewPawn->SetReplicates(false);
        const auto* Configuration=Owner->GetManifest().Strings.Find(TEXT("ground/mobility_configuration"));
        if (!NewPawn->MobilityConfiguration.bReady || !Configuration || *Configuration!=NewPawn->MobilityConfiguration.Fingerprint)
        { World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Ground mobility configuration differs from the network manifest; reimport tuning and export manifests")); Release(); return false; }
        if (!NewPawn->Weapon->IsWeaponReady() || !NewPawn->RocketJump->IsConfigured()) { Release(); return false; }
        // The fixed logical step refreshes both skeletal poses, including NullRHI gameplay sockets.
        NewPawn->GetMesh()->SetComponentTickEnabled(false); NewPawn->Machinegun->SetComponentTickEnabled(false);
        Movement()->SetComponentTickEnabled(false);
        BindPlayer(); ApplyLedger(Record,0,false); NewPawn->Weapon->RegisterSource(); if (Record.bDormant) ResetControlInput(); return true;
    }
    void BindPlayer()
    {
        auto* W=World(); auto* P=Pawn.Get(); if (!W || !P) return;
        AGuLiBattlePlayerState* Player=nullptr;
        if (const auto* Battle=W->GetGameState<AGuLiBattleGameState>())
            for (APlayerState* Entry : Battle->PlayerArray)
                if (auto* PS=Cast<AGuLiBattlePlayerState>(Entry); PS && PS->GetPlayerGuid()==Record.Controller) { Player=PS; break; }
        P->SetPlayerState(Player);
        const auto* Session=W->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
        auto* Controller=W->GetFirstPlayerController();
        if (Controller && Session && Record.Controller==Session->GetLocalPlayerId())
        { if (Controller->GetPawn()!=P) Controller->Possess(P); }
        else if (Controller && Controller->GetPawn()==P) Controller->UnPossess();
    }
    GuLiHybridGround::FPose ReadPose() const
    {
        GuLiHybridGround::FPose V; const auto* P=Pawn.Get(); const auto* M=Movement();
        V.Position=P->GetActorLocation(); V.Rotation=P->GetActorQuat(); V.Velocity=M->Velocity;
        V.Mode=M->MovementMode; V.CustomMode=M->CustomMovementMode; V.AimYaw=P->AimYaw; V.DisplayAimYaw=P->DisplayAimYaw;
        V.bFaceAim=P->SimulationInput.bFaceAim; V.Turn=P->TurnState;
        V.WeaponAim=P->WeaponAimState; V.MoveAxis=P->SimulationInput.Axis; V.Blink=P->GetBlinkComponent()->GetState();
        V.bFaceCamera=P->SimulationInput.bFaceCamera; V.CameraYaw=P->SimulationInput.CameraYaw;
        V.Fuel=P->AbilitySystem->GetNumericAttribute(UGuLiGroundMechAttributeSet::GetFuelAttribute());
        V.MaxFuel=P->AbilitySystem->GetNumericAttribute(UGuLiGroundMechAttributeSet::GetMaxFuelAttribute());
        V.bThrusting=P->RocketJump->bReplicatedThrusting; V.bJet=P->RocketJump->bReplicatedJetActive; return V;
    }
    void ApplyPose(const GuLiHybridGround::FPose& V)
    {
        auto* P=Pawn.Get(); auto* M=Movement();
        if (Record.LifeState>=2 || Record.Health.bDead)
        {
            M->StopMovementImmediately();M->SetMovementMode(MOVE_None);
            P->SimulationInput={};P->HybridInput={};P->bSprintHeld=false;
            P->WeaponAimState={}; P->GetBlinkComponent()->Interrupt();
            P->RocketJump->bReplicatedThrusting=false;P->RocketJump->bReplicatedJetActive=false;
            P->RocketJump->ApplyJetPresentation();return;
        }
        P->SetActorLocationAndRotation(V.Position,V.Rotation,false,nullptr,ETeleportType::TeleportPhysics);
        M->Velocity=V.Velocity; M->SetMovementMode(EMovementMode(V.Mode),V.CustomMode);
        P->AimYaw=V.AimYaw; P->DisplayAimYaw=V.DisplayAimYaw;
        P->SimulationInput.bFaceAim=V.bFaceAim; P->TurnState=V.Turn;
        P->WeaponAimState=V.WeaponAim; P->SimulationInput.Axis=V.MoveAxis; P->GetBlinkComponent()->ApplySnapshot(V.Blink);
        P->SimulationInput.WeaponAimMode=V.WeaponAim.bForward?EGuLiGroundWeaponAimMode::Forward:EGuLiGroundWeaponAimMode::Target;
        P->SimulationInput.bFaceCamera=V.bFaceCamera; P->SimulationInput.CameraYaw=V.CameraYaw;
        P->UpperBodyPivot->SetWorldRotation(FRotator(0,V.DisplayAimYaw-90,0));
        P->AbilitySystem->SetNumericAttributeBase(UGuLiGroundMechAttributeSet::GetMaxFuelAttribute(),V.MaxFuel);
        P->AbilitySystem->SetNumericAttributeBase(UGuLiGroundMechAttributeSet::GetFuelAttribute(),V.Fuel);
        M->RocketState.Fuel=V.Fuel;
        P->RocketJump->bReplicatedThrusting=V.bThrusting; P->RocketJump->bReplicatedJetActive=V.bJet;
        P->RocketJump->ApplyJetPresentation(); M->ObstacleUpdateAccumulator=1.f; M->UpdateGroundMechObstacle(0);
        // Pose updates are not animation clock ticks. Character::Tick advances query animation
        // once per presentation frame, after motion/aim are applied, without resetting its time.
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        GuLiHybridGround::FPrivate V; if (!GuLiHybridGround::Decode(Bytes,V) || V.Key!=Record.Key) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) { Release(); return true; }
        if (Stage==EGuLiHybridRestoreStage::Create) return Create();
        auto* P=Pawn.Get(); auto* M=Movement(); if (!P || !M) return false;
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            BindPlayer(); auto* Weapon=P->Weapon.Get(); auto* Rocket=P->RocketJump.Get();
            if (!Weapon->LoadConfiguration(V.Upgrade)) return false;
            ApplyPose(V.Pose);
            P->GetBlinkComponent()->PrepareRestore();
            P->SimulationInput=V.Simulation; P->HybridInput=V.ControllerInput; P->bSemanticInput=V.bSemantic;
            P->bSprintHeld=V.bSprint; P->bHybridInputSent=false; P->LastHybridInputTick=0;
            P->bShoulderAimHeld=V.ControllerInput.bFaceAim && !V.bSemantic;
            M->SupportState=V.Support;
            if (M->SupportState.IsValid()) M->SupportState.Epoch=World()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
            M->MassSupportSoldierId=V.Support.SoldierId;
            M->RocketState={V.Pose.Fuel,V.FuelRecoveryElapsed};
            M->bRocketHeld=V.bRocketHeld; M->bRocketRequested=V.bRocketRequested;
            M->bRocketThrusting=V.bRocketThrusting; M->bRocketAllowedThisMove=V.bRocketAllowed;
            // Ephemeral collision snapshots and navigation/obstacle handles are rebuilt from the new World epoch.
            M->ActiveMove={}; M->LastMoveContext={}; M->PreparedMove={}; M->PendingMassContact={}; M->CandidateBodies.Reset();
            M->bMoveContextActive=false; M->bSweepStepActive=false; M->bPreparedMove=false; M->PredictionCacheGeneration=0;
            Rocket->bInputHeld=V.Simulation.bRocket; Rocket->bAbilityActive=false;
            // Activation runs after the new authority/permit is installed, without charging fuel at restore time.
            bRestoreAbility=V.bAbility;
            Weapon->CurrentUpgradeId=V.Upgrade; Weapon->NextShotTime=V.NextShot; Weapon->ShotsFired=V.Shots;
            Weapon->AimPoint=V.WeaponAim; Weapon->bHasAim=V.bHasAim; Weapon->bFireHeld=V.bFire;
            Weapon->SeenShots.Reset(); for (const auto& ID : V.SeenShots) Weapon->SeenShots.Add(ID);
            Weapon->RegisterSource(); Weapon->AlignGun();
            if (!P->FindComponentByClass<UGuLiBuildingPlacementComponent>()->RestoreHybridPlacement(V.Placement)) return false;
            if (auto* Anim=Weapon->ConfiguredAnim.Get())
            { Anim->Age=V.RecoilAge; Anim->StartAlpha=V.RecoilStart; Anim->RestZ=V.RecoilRest; Anim->RecoilAlpha=V.RecoilAlpha; Anim->RecoilOffset=V.RecoilOffset; }
            LastApplied=0; LastPublished=0; return true;
        }
        if (Stage!=EGuLiHybridRestoreStage::Activate) return false;
        if (Record.bDormant) ResetControlInput();
        return true;
    }
    bool bRestoreAbility=false;
public:
    explicit FGuLiHybridGroundAdapter(UGuLiHybridActorWorldSubsystem& InOwner):Owner(&InOwner) {}
    ~FGuLiHybridGroundAdapter() override { Release(); }
    UObject* LifetimeOwner() const override { return Owner.Get(); }
    uint32 Version() const override { return GuLiHybridGround::StateVersion; }
    bool Prepare(const FGuLiHybridObjectState& State) override { Record=State; return Create(); }
    void SetAuthority(const FGuLiHybridObjectState& State,bool Local) override
    { Record=State; bLocal=Local; if (auto* Binding=Identity()) Binding->Install(State,Local); BindPlayer(); if (auto* P=Pawn.Get()) P->SetCarrierDormant(State.bDormant); if (State.bDormant) ResetControlInput(); }
    void ResetControlInput() override
    {
        auto* P=Pawn.Get(); auto* M=Movement(); if (!P || !M) return;
        P->GetBlinkComponent()->ResetControl();
        P->SimulationInput={}; P->HybridInput={}; P->bSemanticInput=false; P->bSprintHeld=false;
        P->GetCameraController()->ResetBlink();
        P->CameraMoveAxis=FVector2D::ZeroVector;
        P->bShoulderAimHeld=false; P->TurnState.Direction=0; P->TurnState.Progress=0; ++P->TurnState.Serial;
        P->WeaponAimState={};
        P->bHybridInputSent=false; P->LastHybridInputTick=0; P->ConsumeMovementInputVector();
        P->RocketJump->Interrupt(); bRestoreAbility=false;
        M->Velocity=FVector::ZeroVector; M->Acceleration=FVector::ZeroVector;
        M->bRocketHeld=M->bRocketRequested=M->bRocketThrusting=M->bRocketAllowedThisMove=false;
        P->Weapon->bFireHeld=false;
    }
    void Advance(uint64 At,double Delta) override
    {
        auto* P=Pawn.Get(); if (!P) return;
        P->FindComponentByClass<UGuLiBuildingPlacementComponent>()->AdvanceHybridPlacement();
        if (Record.bDormant) { if (auto* M=Movement()) M->AdvanceDormantRecovery(Delta); return; }
        if (!bLocal || !UGuLiHybridActorComponent::HasSimulation(P)) return;
        if (!P->GetBlinkComponent()->CompletePendingRestore())
        {
            World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Ground blink ability could not be restored"));
            return;
        }
        if (bRestoreAbility)
        {
            bRestoreAbility=false; P->RocketJump->bInputHeld=false;
            P->RocketJump->ApplyHybridInput(P->SimulationInput.bRocket);
            if (!P->RocketJump->bAbilityActive) { World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Ground rocket ability could not be restored")); return; }
            ++P->HybridRocketRestoreCount;
        }
        P->AdvanceHybrid(Delta);
        if (At%2 || At==LastPublished) return;
        LastPublished=At; FGuLiHybridStateArchive A(1024); uint32 Schema=6; A.Value(Schema); auto Pose=ReadPose(); GuLiHybridGround::Pose(A,Pose);
        auto Aim=FVector(P->Weapon->AimPoint); A.Value(Aim);
        TArray<uint8> Body; if (A.Finish(Body)) World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->PublishMotion(Record.Key,Body);
    }
    bool Execute(const FGuLiHybridCommand& Command,const FGuLiHybridCommandMember& Member) override
    {
        if (Pawn.IsValid() && bLocal && Command.Operation==TEXT("BuildingPlace"))
            return Pawn->FindComponentByClass<UGuLiBuildingPlacementComponent>()->ExecuteHybridPlacement(Command,Member);
        FGuLiHybridGroundInput Input;
        if (!Pawn.IsValid() || !bLocal || Command.Operation!=TEXT("GroundInput") || Member.Key!=Record.Key
            || !GuLiHybridGround::Decode(Command.Arguments,Input)) return false;
        Pawn->SimulationInput=Input; return true;
    }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 At) override
    {
        if (!Pawn.IsValid() || bLocal) return false; if (At<=LastApplied) return true;
        FGuLiHybridStateArchive A(Bytes,1024); uint32 Schema=0; A.Value(Schema);
        GuLiHybridGround::FPose V; GuLiHybridGround::Pose(A,V); FVector Aim; A.Value(Aim);
        if (Schema!=6 || !A.Done()) return false;
        ApplyPose(V);
        GuLiGrid::ObserveRemoteBody(*World(),Record,V.Position,1,V.Mode==MOVE_Walking || V.Mode==MOVE_NavWalking);
        Pawn->Weapon->AimPoint=Aim; Pawn->Weapon->AlignGun(); LastApplied=At; return true;
    }
    void ApplyLedger(const FGuLiHybridObjectState& State,uint64 Revision,bool Effects) override
    {
        Record=State; if (auto* Binding=Identity()) Binding->Install(State,bLocal);
        if (auto* P=Pawn.Get())
        {
            FGuLiTargetHandle Target; Target.Kind=EGuLiTargetKind::GroundActor; Target.AuthorityId=State.Key.StableId;
            Target.Generation=State.Key.LifeGeneration; Target.LocalId=State.Ordinal;
            const auto* Battle=World()->GetGameState<AGuLiBattleGameState>();
            if (!P->CombatHealth->ApplyHybridHealth(Target,State.Team,State.Health,Battle?Battle->GetMatchEpoch():0,Revision,Effects))
                World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Ground health could not apply a committed state"));
        }
        if (State.bDormant || State.LifeState>=2) ResetControlInput();
    }
    bool Capture(TArray<uint8>& Bytes) const override
    {
        auto* P=Pawn.Get(); auto* M=Movement(); if (!P || !M || M->bMoveContextActive || M->bSweepStepActive) return false;
        GuLiHybridGround::FPrivate V; V.Key=Record.Key; V.Pose=ReadPose();
        V.Simulation=P->SimulationInput; V.ControllerInput=P->HybridInput; V.bSemantic=P->bSemanticInput; V.bSprint=P->bSprintHeld;
        V.bAbility=P->RocketJump->bAbilityActive; V.bRocketHeld=M->bRocketHeld; V.bRocketRequested=M->bRocketRequested;
        V.bRocketThrusting=M->bRocketThrusting; V.bRocketAllowed=M->bRocketAllowedThisMove;
        V.FuelRecoveryElapsed=M->RocketState.RecoveryElapsed; V.Support=M->SupportState;
        const auto* Weapon=P->Weapon.Get(); V.Upgrade=Weapon->CurrentUpgradeId; V.NextShot=Weapon->NextShotTime; V.Shots=Weapon->ShotsFired;
        V.WeaponAim=Weapon->AimPoint; V.bFire=Weapon->bFireHeld; V.bHasAim=Weapon->bHasAim; V.SeenShots=Weapon->SeenShots.Array();
        if (const auto* Anim=Weapon->ConfiguredAnim.Get())
        { V.RecoilAge=Anim->Age; V.RecoilStart=Anim->StartAlpha; V.RecoilRest=Anim->RestZ; V.RecoilAlpha=Anim->RecoilAlpha; V.RecoilOffset=Anim->RecoilOffset; }
        if (!P->FindComponentByClass<UGuLiBuildingPlacementComponent>()->CaptureHybridPlacement(V.Placement)) return false;
        FGuLiHybridStateArchive A; GuLiHybridGround::Private(A,V); return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridGround::FPrivate V; const auto* P=Pawn.Get();
        const auto* Placement=P?P->FindComponentByClass<UGuLiBuildingPlacementComponent>():nullptr;
        return GuLiHybridGround::Decode(Bytes,V) && V.Key==Record.Key && Placement && Placement->ValidateHybridPlacement(V.Placement);
    }
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override { return RestoreImpl(Bytes,Stage); }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override { return RestoreImpl(Bytes,Stage); }
    void Release() override
    { if (auto* P=Pawn.Get()) { P->Destroy(); } Pawn.Reset(); }
};

class FGuLiHybridGroundControls final : public IGuLiHybridSharedStateAdapter
{
    struct FInput
    { FGuLiHybridObjectKey Key; FGuLiHybridGroundInput Value; bool bSemantic=false; };
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    static void Serialize(FGuLiHybridStateArchive& A,FGuid& Player,TArray<FInput>& Inputs)
    {
        uint32 Schema=6; A.Value(Schema); if (Schema!=6) A.Reject(); A.Value(Player); if (!Player.IsValid()) A.Reject();
        TSet<FGuLiHybridObjectKey> Seen;
        A.Array(Inputs,32,[&](auto& Ar,FInput& V)
        {
            Ar.Value(V.Key); GuLiHybridGround::Input(Ar,V.Value); Ar.Value(V.bSemantic);
            if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::GroundPlayer || Seen.Contains(V.Key)) Ar.Reject();
            Seen.Add(V.Key);
        });
    }
public:
    explicit FGuLiHybridGroundControls(UGuLiHybridActorWorldSubsystem& InOwner):Owner(&InOwner) {}
    UObject* LifetimeOwner() const override { return Owner.Get(); }
    uint32 Version() const override { return 6; }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    {
        if (!Owner.IsValid()) return false; auto* World=Owner->GetWorld();
        auto* Session=World->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
        auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!Session || !Hybrid) return false;
        auto Player=Session->GetLocalPlayerId(); TArray<FInput> Inputs;
        for (TActorIterator<AGuLiGroundMechCharacter> It(World); It; ++It)
            if (const auto* Binding=UGuLiHybridActorComponent::Find(*It); Binding && Objects.Contains(Binding->GetObjectKey()) && Hybrid->HasControl(Binding->GetObjectKey()))
                Inputs.Add({Binding->GetObjectKey(),It->HybridInput,It->bSemanticInput});
        FGuLiHybridStateArchive A; Serialize(A,Player,Inputs); return A.Finish(Bytes);
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>&) const override
    {
        if (Pieces.IsEmpty()) return false; TSet<FGuid> Players; TSet<FGuLiHybridObjectKey> Keys;
        for (const auto& Piece : Pieces)
        {
            FGuid Player; TArray<FInput> Inputs; FGuLiHybridStateArchive A(Piece.Bytes); Serialize(A,Player,Inputs);
            if (!A.Done() || Piece.Version!=Version() || Player!=Piece.Player || Players.Contains(Player)) return false;
            Players.Add(Player);
            for (const auto& V : Inputs) { if (Keys.Contains(V.Key)) return false; Keys.Add(V.Key); }
        }
        return true;
    }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    {
        if (!Owner.IsValid() || !Validate(Pieces,Objects)) return false;
        if (Stage!=EGuLiHybridRestoreStage::Activate) return true;
        auto* World=Owner->GetWorld(); auto* Session=World->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
        auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!Session || !Hybrid) return false;
        for (const auto& Piece : Pieces) if (Piece.Player==Session->GetLocalPlayerId())
        {
            FGuid Player; TArray<FInput> Inputs; FGuLiHybridStateArchive A(Piece.Bytes); Serialize(A,Player,Inputs);
            for (const auto& V : Inputs) for (TActorIterator<AGuLiGroundMechCharacter> It(World); It; ++It)
                if (const auto* Binding=UGuLiHybridActorComponent::Find(*It);
                    Binding && Binding->GetObjectKey()==V.Key && Hybrid->HasControl(V.Key))
                { It->HybridInput=V.Value; It->bSemanticInput=V.bSemantic; It->bShoulderAimHeld=V.Value.bFaceAim && !V.bSemantic; It->bHybridInputSent=false; }
        }
        return true;
    }
};

static const FGuLiHybridAdapterRegistration GGuLiGroundAdapterFamily({TEXT("GroundPlayer"),EGuLiHybridObjectKind::GroundPlayer,GuLiHybridGround::StateVersion,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridGroundAdapter>(Owner);},
    [](UGuLiHybridActorWorldSubsystem& Owner)
    {return Owner.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->RegisterSharedStateSystem(TEXT("GroundControls"),MakeShared<FGuLiHybridGroundControls>(Owner));},GuLiHybridTargets::Actor<EGuLiTargetKind::GroundActor,true>,EGuLiHybridOwnershipPolicy::ControllerPinned,1,false});
