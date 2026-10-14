#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/Private/GuLiHybridReflectedState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"
#include "Gameplay/Units/GuLiExternalControlRegistry.h"

namespace
{
constexpr uint32 Version=2;
struct FState
{
    FGuLiHybridObjectKey Key,Parent;TArray<uint8> Member;
    FGuLiWingmanAttackAuthorityState Attack;double NextScan=0;
};
void WingmanState(FGuLiHybridStateArchive& A,FState& S)
{
    uint32 Schema=Version;A.Value(Schema);if (Schema!=Version) A.Reject();
    A.Value(S.Key);A.Value(S.Parent);A.Bytes(S.Member,256*1024);FGuLiHybridReflectedState::Value(A,S.Attack);A.Value(S.NextScan);
    if (S.Key.Kind!=EGuLiHybridObjectKind::Wingman || !S.Key.IsValid() || S.Parent.Kind!=EGuLiHybridObjectKind::Ship || !S.Parent.IsValid()
        || S.NextScan<0 || S.Attack.AutomaticTargets.Num()>1 || S.Attack.Checkpoints.Num()>8) A.Reject();
}
bool ReadWingman(TConstArrayView<uint8> Bytes,FState& State)
{FGuLiHybridStateArchive A(Bytes,512*1024);WingmanState(A,State);return A.Done();}
}

/** The grant is per member. The existing flight integrator never advances its siblings. */
class FGuLiHybridWingmanAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<UGuLiCombatHealthComponent> Health;
    FGuLiHybridObjectState Record;FGuLiWingmanHandle Member;
    TSharedPtr<FGuLiWingmanLocalGroupRuntime> Runtime;
    bool Local=false;uint64 LastPublished=0,LastApplied=0;
    UWorld* World() const {return Owner.IsValid()?Owner->GetWorld():nullptr;}
    UGuLiHybridWorldSubsystem* Hybrid() const {return World()?World()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;}
    UGuLiWingmanSimulationSubsystem* Simulation() const {return World()?World()->GetSubsystem<UGuLiWingmanSimulationSubsystem>():nullptr;}
    AGuLiWingmanPawn* Pawn() const {return Runtime && Runtime->Pawns.Num()==1?Runtime->Pawns[0].Get():nullptr;}
    uint32 Epoch() const {const auto* B=World()?World()->GetGameState<AGuLiBattleGameState>():nullptr;return B?B->GetMatchEpoch():0;}
    bool Profile(FGuLiGroupAbilityConfigSnapshot& Configuration) const
    {
        const auto* Path=Owner->GetManifest().Strings.Find(Record.Definition.ToString()+TEXT("/configuration"));
        const auto* Definition=Path?LoadObject<UGuLiShipHangarDefinition>(nullptr,**Path):nullptr;
        const auto* CDO=GetDefault<UGuLiShipHangarCapabilityComponent>();
        if (!Definition || !Definition->AbilitySet || Owner->GetManifest().Strings.FindRef(Record.Definition.ToString()+TEXT("/configuration_hash"))!=LexToString(CDO->GetConfigurationChecksum(Definition))) return false;
        FGuLiShipAbilityProjectionContext Context;Context.ShipInstanceId=Member.Flight.Group.ShipInstanceId;
        Context.ShipGeneration=Member.Flight.Group.ShipGeneration;Context.GroupGeneration=Member.Flight.Group.GroupGeneration;
        Context.MatchEpoch=Epoch();Context.OwnerPlayerGuid=Record.Controller;Context.Team=Record.Team;
        Context.WingmanTypeId=Definition->AbilitySet->WingmanTypeId;Context.FormationCommandRevision=1;Context.EffectiveClientSimTick=1;
        if (!Context.OwnerPlayerGuid.IsValid()) return false;
        return UGuLiShipHangarCapabilityComponent::BuildInitialConfiguration(*Definition,Context,Configuration);
    }
    bool Create()
    {
        if (Pawn()) return true;
        if (!Owner.IsValid() || !Hybrid() || !Simulation() || !GuLiHybridWingman::Resolve(Owner->GetManifest(),Record,Member)) return false;
        FGuLiGroupAbilityConfigSnapshot Configuration;if (!Profile(Configuration)) return false;
        AActor* Carrier=Hybrid()->FindBoundActor(Record.Parent);FGuLiHybridObjectState Parent;
        if (!Hybrid()->GetObject(Record.Parent,Parent)) return false;
        const auto Pose=Carrier?Carrier->GetActorTransform():FTransform(FRotator(0,Parent.FacingYawDegrees,0),Parent.Location);
        FGuLiCarrierSourceRef Source;Source.CanonicalEpoch=Epoch();Source.MoveRevision=1;
        Runtime=Simulation()->CreateIndependentMember(Member,Configuration,Pose,Carrier?Carrier->GetVelocity():FVector::ZeroVector,Source,Record.Location,Carrier);
        auto* P=Pawn();if (!P) return false;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(P);P->AddInstanceComponent(Binding);Binding->RegisterComponent();Binding->Install(Record,Local);
        auto* H=NewObject<UGuLiCombatHealthComponent>(P);P->AddInstanceComponent(H);H->RegisterComponent();Health=H;
        if (!Hybrid()->BindActor(Record.Key,*P) || !Hybrid()->BindTarget(GuLiCombatTargets::MakeWingmanTargetHandle(Member),Record.Key)) {Release();return false;}
        if (!World()->GetSubsystem<UGuLiExternalControlRegistry>()->Bind(Record.Key,*Owner.Get(),
            [this](FGuLiExternalBody& Out)
            {auto* P=Pawn();if (!P || !Runtime) return false;Out.Actor=P;Out.Transform=P->GetActorTransform();Out.Parent=Record.Parent;Out.bGroundPivot=false;Out.bAirborne=Out.bFollowParent=true;
                Out.bLocked=Runtime->bExternalActionsLocked;Out.Radius=Out.HalfHeight=Runtime->AbilityConfig.FormationRuntime.AgentRadiusCentimeters;return true;},
            [this](const FGuLiExternalControl& In)
            {return Runtime && Simulation() && Simulation()->ApplyIndependentExternalControl(*Runtime,In.bPhased,In.bLocked,In.bDisplace?&In.Transform:nullptr);},
            [this](const FTransform& Carrier,TConstArrayView<FGuLiHybridObjectKey> Siblings,FTransform& Out)
            {
                if (!Runtime || !Pawn()) return false;TArray<int32> Slots;
                for (const auto K:Siblings)
                {
                    FGuLiHybridObjectState O;FGuLiWingmanHandle Other;
                    if (!Hybrid()->GetObject(K,O)) return false;
                    if (O.Definition!=Record.Definition) continue;
                    if (!GuLiHybridWingman::Resolve(Owner->GetManifest(),O,Other)) return false;
                    if (Other.Flight.Group==Member.Flight.Group) Slots.Add(Other.Flight.FlightIndex*GULI_WINGMAN_MEMBERS_PER_FLIGHT+Other.MemberIndex);
                }
                Slots.Sort();TArray<FVector> Positions;const int32 Slot=Member.Flight.FlightIndex*GULI_WINGMAN_MEMBERS_PER_FLIGHT+Member.MemberIndex;
                if (!UGuLiWingmanSimulationSubsystem::PlanExternalFormationPositions(World(),Carrier,Runtime->AbilityConfig.FormationRuntime,Positions,Slots)
                    || !Positions.IsValidIndex(Slot)) return false;
                Out=FTransform(Carrier.GetRotation(),Positions[Slot],Pawn()->GetActorScale3D());return true;
            })) {Release();return false;}
        ApplyLedger(Record,0,false);return true;
    }
    void RebaseProjection()
    {
        if (!Runtime || !Pawn()) return;
        auto& C=Runtime->AbilityConfig;C.MatchEpoch=Epoch();
        if (Record.Controller.IsValid()) C.OwnerPlayerGuid=Record.Controller;
        for (auto& Channel:C.WeaponChannels) {Channel.Binding.MatchEpoch=C.MatchEpoch;Channel.Binding.OwnerPlayerGuid=C.OwnerPlayerGuid;}
        C.RefreshHash();Pawn()->GetMutableRuntimeState().Attack.LeaseEpoch=uint32(Record.OwnerEpoch);
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage,bool Query)
    {
        FState S;if (!ReadWingman(Bytes,S) || S.Key!=Record.Key || S.Parent!=Record.Parent) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) {Release();return true;}
        if (Stage==EGuLiHybridRestoreStage::Create) return Create() && Validate(Bytes);
        if (!Pawn() || !Validate(Bytes)) return false;
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            if (!Simulation()->RestoreIndependentMember(*Runtime,S.Member,!Query)) return false;
            Runtime->CarrierActor=Hybrid()->FindBoundActor(Record.Parent);
            Runtime->HybridAttack=S.Attack;Runtime->NextHybridTargetScan=S.NextScan;
            LastPublished=LastApplied=0;RebaseProjection();Pawn()->ObserveHybridRestore();return true;
        }
        if (Stage!=EGuLiHybridRestoreStage::Activate) return false;
        ApplyLedger(Record,0,false);return true;
    }
public:
    explicit FGuLiHybridWingmanAdapter(UGuLiHybridActorWorldSubsystem& Value):Owner(&Value) {}
    ~FGuLiHybridWingmanAdapter() override {Release();}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return ::Version;}
    TArray<FGuLiHybridObjectKey> RestoreDependencies(TConstArrayView<uint8> Bytes) const override
    {FState S;return ReadWingman(Bytes,S)?TArray<FGuLiHybridObjectKey>{S.Parent}:TArray<FGuLiHybridObjectKey>{};}
    bool Prepare(const FGuLiHybridObjectState& State) override {Record=State;return State.LifeState==3 || Create();}
    void SetAuthority(const FGuLiHybridObjectState& State,bool IsLocal) override
    {
        Record=State;Local=IsLocal;
        if (State.LifeState==3) {Release();return;}
        if (!Create()) {Hybrid()->Suspend(TEXT("Wingman entity recreation failed"));return;}
        if (auto* P=Pawn()) {P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,Local);RebaseProjection();}
    }
    void Advance(uint64 Tick,double Delta) override
    {
        auto* P=Pawn();if (!P || !Local || !Hybrid()->HasSimulation(Record.Key) || Tick%2) return;
        auto& State=P->GetMutableRuntimeState();
        if (auto* Carrier=Hybrid()->FindBoundActor(Record.Parent))
        {State.Carrier.Transform=Carrier->GetActorTransform();State.Carrier.Velocity=Carrier->GetVelocity();State.Carrier.Source.CanonicalEpoch=Epoch();State.Carrier.Source.MoveRevision=uint32(FMath::Max<uint64>(1,Tick));Runtime->CarrierActor=Carrier;P->GetFlightMovement()->SetCarrierActor(Carrier);}
        Simulation()->AdvanceHybridAttack(*Runtime,uint32(Record.OwnerEpoch),Tick,double(Tick)/60.);
        if (!Simulation()->AdvanceIndependentMember(*Runtime,float(Delta*2.),double(Tick)/60.)) {Hybrid()->Suspend(TEXT("Independent Wingman integrator failed"));return;}
        P->ObserveHybridSimulation(Tick);
        if (Tick%6 || Tick==LastPublished) return;
        FGuLiHybridStateArchive A(700);uint32 Schema=1;A.Value(Schema);auto Pose=P->GetActorTransform();auto Velocity=State.Dynamics.Velocity;
        A.Value(Pose);A.Value(Velocity);TArray<uint8> Bytes;
        if (!A.Finish(Bytes) || !Hybrid()->PublishMotion(Record.Key,Bytes)) {Hybrid()->Suspend(TEXT("Wingman motion publication failed"));return;}
        LastPublished=Tick;
        P->ObserveHybridPublication(Tick);
    }
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override {return false;}
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 Tick) override
    {
        auto* P=Pawn();if (!P || Local || Tick<=LastApplied) return false;
        FGuLiHybridStateArchive A(Bytes,700);uint32 Schema=0;FTransform Pose;FVector Velocity;A.Value(Schema);A.Value(Pose);A.Value(Velocity);
        if (Schema!=1 || !A.Done()) return false;
        P->ApplyHybridQueryPose(Pose,Velocity);P->ObserveHybridReception(Tick);LastApplied=Tick;return true;
    }
    void ApplyLedger(const FGuLiHybridObjectState& State,uint64 Revision,bool Effects) override
    {
        Record=State;if (State.LifeState==3) {Release();return;}
        if (!Pawn() || !Health.IsValid()) return;
        Pawn()->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,Local);
        Health->ApplyHybridHealth(GuLiCombatTargets::MakeWingmanTargetHandle(Member),State.Team,State.Health,Epoch(),Revision,Effects);
        Pawn()->SetAlive(!State.Health.bDead);Pawn()->SetPhaseAppearance(State.bPhased);
    }
    bool Capture(TArray<uint8>& Bytes) const override
    {
        if (!Pawn() || !Simulation()) return false;
        FState S;S.Key=Record.Key;S.Parent=Record.Parent;S.Attack=Runtime->HybridAttack;S.NextScan=Runtime->NextHybridTargetScan;
        if (!Simulation()->CaptureIndependentMember(*Runtime,S.Member)) return false;
        FGuLiHybridStateArchive A(512*1024);WingmanState(A,S);return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {FState S;return ReadWingman(Bytes,S) && S.Key==Record.Key && S.Parent==Record.Parent && Simulation() && Simulation()->ValidateIndependentMember(S.Member,Member);}


    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override {return RestoreImpl(Bytes,Stage,false);}
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override {return RestoreImpl(Bytes,Stage,true);}
    void Release() override
    {
        if (World() && Owner.IsValid()) World()->GetSubsystem<UGuLiExternalControlRegistry>()->Unbind(Record.Key,*Owner.Get());
        if (auto* P=Pawn()) if (Hybrid()) Hybrid()->UnbindActor(Record.Key,*P);
        if (Runtime && Simulation()) Simulation()->ReleaseIndependentMember(*Runtime);
        Runtime.Reset();Health.Reset();
    }
};
namespace
{
const FGuLiHybridAdapterRegistration WingmanFamily({TEXT("WingmanMember"),EGuLiHybridObjectKind::Wingman,Version,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&) -> TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridWingmanAdapter>(Owner);},nullptr,
    [](const FGuLiHybridManifestData& Manifest,const FGuLiHybridObjectState& Object,uint32,FGuLiTargetHandle& Out)
    {
        FGuLiWingmanHandle Member;if (!GuLiHybridWingman::Resolve(Manifest,Object,Member)) return false;
        Out=GuLiCombatTargets::MakeWingmanTargetHandle(Member);return Out.IsValid();
    },EGuLiHybridOwnershipPolicy::ControllerPinned,2,true});
}
