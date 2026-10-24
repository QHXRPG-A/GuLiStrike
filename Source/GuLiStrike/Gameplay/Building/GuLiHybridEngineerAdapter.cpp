#include "Battle/Combat/GuLiHybridTargetBinding.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Components/CapsuleComponent.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Presentation/GuLiEngineeringPoseComponent.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Units/GuLiEngineeringAIController.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Behavior/GuLiCommanderStateTreeComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Units/Private/GuLiHybridEngineeringState.h"
#include "Commander/Orders/Private/GuLiHybridTaskCommand.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "NavigationSystem.h"

namespace GuLiHybridEngineer
{
constexpr uint32 Version=3;
struct FPose
{
    FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector;
    FQuat Rotation=FQuat::Identity;
    uint8 Mode=MOVE_Walking,CustomMode=0;
};
struct FPrivate
{
    FGuLiHybridObjectKey Key;
    int32 Type=0;
    uint32 TransitRequest=0;
    FPose Pose;
    TArray<uint8> Travel,Work;
};
void Pose(FGuLiHybridStateArchive& A,FPose& V)
{A.Value(V.Position); A.Value(V.Rotation); A.Value(V.Velocity); A.Value(V.Mode); A.Value(V.CustomMode); if (V.Mode>=MOVE_MAX) A.Reject();}
void Private(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Schema=Version; A.Value(Schema); if (Schema!=Version) A.Reject();
    A.Value(V.Key); A.Value(V.Type); A.Value(V.TransitRequest); Pose(A,V.Pose);
    A.Bytes(V.Travel); A.Bytes(V.Work,65536);
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::Engineer || V.Type<=0 || V.Travel.IsEmpty() || V.Work.IsEmpty()) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V)
{FGuLiHybridStateArchive A(Bytes); Private(A,V); return A.Done();}
void Fact(FGuLiHybridStateArchive& A,FGuLiHybridConstructionFact& V)
{
    A.Value(V.Target); A.Value(V.Intent.Building); A.Value(V.Intent.Slot); A.Value(V.Intent.Generation); A.Value(V.Task);
    A.Value(V.bOccupy); A.Value(V.bReady);
    if ((V.Target.IsValid() && (V.Target.Kind!=EGuLiHybridObjectKind::Building || !V.Task))
        || (V.bOccupy && (!V.Target.IsValid() || !V.Intent.IsValid() || !V.Intent.Generation))
        || (V.bReady && !V.bOccupy) || V.Intent.Slot>=4 || V.Intent.Slot<INDEX_NONE) A.Reject();
}
int32 Type(FName Name)
{const FString Text=Name.ToString(); const FString Suffix=Text.RightChop(5); return Text.StartsWith(TEXT("unit/")) && Suffix.IsNumeric()?FCString::Atoi(*Suffix):0;}
bool Supported(const FGuLiSoldierDefinition& D)
{return D.UnitTypeId>0 && D.ActorClass && D.ActorClass->IsChildOf(AGuLiConstructionVehiclePawn::StaticClass());}
}

/** Production engineer Actor, private travel/work recipes, and authenticated work facts. */
class FGuLiHybridEngineerAdapter final : public IGuLiHybridSimulationAdapter, public TSharedFromThis<FGuLiHybridEngineerAdapter>
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<AGuLiConstructionVehiclePawn> Pawn;
    FGuLiHybridObjectState Record;
    bool bLocal=false,bPoseQueued=false;
    uint64 LastApplied=0,LastPublished=0;
    UWorld* World() const {return Owner.IsValid()?Owner->GetWorld():nullptr;}
    const FGuLiSoldierDefinition* Definition() const
    {const auto* Data=World()?World()->GetSubsystem<UGuLiUnitDataSubsystem>():nullptr; return Data?Data->FindDefinition(uint16(GuLiHybridEngineer::Type(Record.Definition))):nullptr;}
    bool Create()
    {
        if (Pawn.IsValid()) return true;
        auto* W=World(); const auto* D=Definition();
        if (!W || !D || !GuLiHybridEngineer::Supported(*D) || Record.Key.Kind!=EGuLiHybridObjectKind::Engineer || !Record.Ordinal) return false;
        const auto* Health=Owner->GetManifest().Numbers.Find(FString::Printf(TEXT("engineer_adapter/%u/health"),D->UnitTypeId));
        if (!Health || *Health!=D->MaxHealth || Record.Health.MaxHealth!=D->MaxHealth) return false;
        const FTransform Transform(FRotator(0,Record.FacingYawDegrees,0),Record.Location);
        auto* P=W->SpawnActorDeferred<AGuLiConstructionVehiclePawn>(D->ActorClass,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!P) return false; Pawn=P; P->AutoPossessAI=EAutoPossessAI::Disabled;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(P); P->AddInstanceComponent(Binding); Binding->Install(Record,bLocal); Binding->RegisterComponent();
        P->InitializeVehicle(Record.Team,FGuLiControllableActorId(Record.Ordinal),*D); P->FinishSpawning(Transform);
        P->SetReplicates(false); P->SetReplicateMovement(false); P->SetActorTickEnabled(false);
        P->GetCharacterMovement()->SetComponentTickEnabled(false); P->Travel->SetComponentTickEnabled(false); P->Work->SetComponentTickEnabled(false);
        P->SpawnDefaultController(); auto* AI=Cast<AGuLiEngineeringAIController>(P->GetController());
        if (!AI) {Release(); return false;}
        AI->SetActorTickEnabled(false); AI->GetPathFollowingComponent()->SetComponentTickEnabled(false);
        ApplyLedger(Record,0,false); return true;
    }
    GuLiHybridEngineer::FPose ReadPose() const
    {
        const auto* P=Pawn.Get(); const auto* M=P->GetCharacterMovement();
        return {P->GetActorLocation(),M->Velocity,P->GetActorQuat(),uint8(M->MovementMode),M->CustomMovementMode};
    }
    void ApplyPose(const GuLiHybridEngineer::FPose& V)
    {
        auto* P=Pawn.Get(); auto* M=P->GetCharacterMovement(); P->SetActorLocationAndRotation(V.Position,V.Rotation,false,nullptr,ETeleportType::TeleportPhysics);
        M->SetMovementMode(EMovementMode(V.Mode),V.CustomMode); M->Velocity=V.Velocity;
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        GuLiHybridEngineer::FPrivate V; if (!GuLiHybridEngineer::Decode(Bytes,V) || V.Key!=Record.Key || V.Type!=GuLiHybridEngineer::Type(Record.Definition)) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) {Release(); return true;}
        if (Stage==EGuLiHybridRestoreStage::Create) return Create();
        auto* P=Pawn.Get(); if (!P) return false;
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            ApplyPose(V.Pose); P->LastTransitRequestId=V.TransitRequest;
            if (!P->Travel->RestoreHybridTravel(V.Travel,false) || !P->Work->RestoreHybridWork(V.Work)) return false;
            P->MovementPresentation->ResetToAuthority();
            LastApplied=LastPublished=0;
        }
        if (Stage==EGuLiHybridRestoreStage::Activate)
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(Record,bLocal);
            if (bLocal && !P->Travel->RestoreHybridTravel(V.Travel,true)) return false;
            auto Fact=P->Work->GetHybridWorkFact(World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetLogicalTick());
            if (!P->Work->ApplyHybridWorkFact(Fact)) return false;
        }
        return true;
    }
public:
    explicit FGuLiHybridEngineerAdapter(UGuLiHybridActorWorldSubsystem& V):Owner(&V) {}
    ~FGuLiHybridEngineerAdapter() override {Release();}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return GuLiHybridEngineer::Version;}
    TArray<FName> RequiredSharedStateSystems() const override {return {TEXT("CommanderTasks"),TEXT("CommanderMass")};}
    bool Prepare(const FGuLiHybridObjectState& V) override {Record=V; return Create();}
    void SetAuthority(const FGuLiHybridObjectState& V,bool Local) override
    {
        Record=V; bLocal=Local; LastApplied=LastPublished=0;
        if (auto* P=Pawn.Get())
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(V,Local);
            P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->RefreshMassAuthority();
            P->MovementPresentation->ResetToAuthority();
            if (Local && V.LifeState==1) World()->GetSubsystem<UGuLiUnitTaskSubsystem>()->RegisterActor(*P);
        }
    }
    void Advance(uint64 At,double Delta) override
    {
        auto* P=Pawn.Get(); if (!P || !bLocal || Record.LifeState!=1 || !UGuLiHybridActorComponent::HasSimulation(P)) return;
        const float Dt=static_cast<float>(Delta);
        P->Travel->AdvanceHybrid(Dt); P->Work->AdvanceHybrid(Dt);
        P->Work->ApplyHybridWorkFact(P->Work->GetHybridWorkFact(At));
        GuLiGrid::ObserveRemoteBody(*World(),Record,P->GetActorLocation(),Definition()->FootprintCells,
            P->Travel->GetTransitState().Phase==EGuLiTransitPhase::Ground);
        if (At<LastPublished+6 || bPoseQueued) return;
        bPoseQueued=true;const uint64 Epoch=Record.OwnerEpoch;
        TWeakPtr<FGuLiHybridEngineerAdapter> Weak=StaticCastSharedRef<FGuLiHybridEngineerAdapter>(AsShared());
        World()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->QueueEngineeringPosePublish([Weak,At,Epoch]
        {
            if (auto Self=Weak.Pin())
            {Self->bPoseQueued=false;if (Self->Record.OwnerEpoch==Epoch) Self->PublishPose(At);}
        });
    }
    void PublishPose(uint64 At)
    {
        auto* P=Pawn.Get();if (!P || !bLocal || Record.LifeState!=1 || !UGuLiHybridActorComponent::HasSimulation(P)) return;
        if (At<LastPublished+6) return;
        FGuLiHybridStateArchive A(65536); uint32 Version=GuLiHybridEngineer::Version; A.Value(Version);
        auto Pose=ReadPose(); GuLiHybridEngineer::Pose(A,Pose); auto Transit=P->Travel->GetTransitState(); GuLiHybridEngineeringState::Transit(A,Transit);
        auto Fact=P->Work->GetHybridWorkFact(At); GuLiHybridEngineer::Fact(A,Fact);
        TArray<uint8> Body; if (A.Finish(Body) && World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->PublishMotion(Record.Key,Body)) LastPublished=At;
    }
    bool Execute(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M) override
    {
        auto* P=Pawn.Get(); if (!P || !bLocal || M.Key!=Record.Key) return false;
        FGuLiUnitTaskCommand Command; float Pitch=0,Radius=0;
        if (C.Operation==TEXT("UnitTask")) {if (!GuLiHybridState::DecodeTaskCommand(C.Arguments,Command,Pitch,Radius)) return false;}
        else if (C.Operation==TEXT("Move")) {Command.CommandId=GetTypeHash(C.Event)|1; Command.Kind=EGuLiUnitTaskKind::Move; Command.Target=M.Destination;}
        else return false;
        // Frozen formation slots are supplied by the controlling endpoint.
        if (Command.Kind==EGuLiUnitTaskKind::Move) Command.Target=M.Destination;
        AGuLiBattlePlayerState* Player=nullptr;
        for (APlayerState* Entry:World()->GetGameState<AGuLiBattleGameState>()->PlayerArray)
            if (auto* Candidate=Cast<AGuLiBattlePlayerState>(Entry); Candidate && Candidate->GetPlayerGuid()==Record.Controller) {Player=Candidate; break;}
        if (!Player) return false;
        FGuLiCommanderSelectionState Selection; Selection.SelectionRevision=Command.SelectionRevision=1;
        Selection.ActorIds.Add(FGuLiControllableActorId(Record.Ordinal));
        FString Message; int32 Accepted=0,Rejected=0;
        const bool Submitted=World()->GetSubsystem<UGuLiUnitTaskSubsystem>()->Submit(*Player,Selection,Command,Message,Accepted,Rejected) && Accepted==1;
        return Submitted;
    }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 At) override
    {
        auto* P=Pawn.Get(); if (!P || bLocal) return false; if (At<=LastApplied) return true;
        FGuLiHybridStateArchive A(Bytes,65536); uint32 Version=0; GuLiHybridEngineer::FPose Pose;
        FGuLiStrongholdTransitState Transit; FGuLiHybridConstructionFact Fact;
        A.Value(Version); GuLiHybridEngineer::Pose(A,Pose); GuLiHybridEngineeringState::Transit(A,Transit);
        GuLiHybridEngineer::Fact(A,Fact);
        if (Version!=GuLiHybridEngineer::Version || !A.Done()) return false;
        ApplyPose(Pose); P->Travel->ApplyHybridTransitMotion(Transit);
        GuLiGrid::ObserveRemoteBody(*World(),Record,Pose.Position,Definition()->FootprintCells,Transit.Phase==EGuLiTransitPhase::Ground);
        // Stream order uses producer ticks; freshness is measured on this receiver's logical clock.
        Fact.At=World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetLocalLogicalTick();
        if (!P->Work->ApplyHybridWorkFact(Fact)) return false;
        LastApplied=At; return true;
    }
    void ApplyLedger(const FGuLiHybridObjectState& V,uint64 Revision,bool Effects) override
    {
        Record=V; if (auto* P=Pawn.Get())
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(V,bLocal);
            P->FindComponentByClass<UGuLiEngineeringTravelComponent>()->RefreshMassAuthority();
            FGuLiTargetHandle Target; Target.Kind=EGuLiTargetKind::GroundActor; Target.AuthorityId=V.Key.StableId;
            Target.Generation=V.Key.LifeGeneration; Target.LocalId=V.Ordinal;
            P->FindComponentByClass<UGuLiCombatHealthComponent>()->ApplyHybridHealth(Target,V.Team,V.Health,
                World()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch(),Revision,Effects);
            if (V.LifeState!=1) P->GetCharacterMovement()->StopMovementImmediately();
        }
    }


    bool Capture(TArray<uint8>& Bytes) const override
    {
        const auto* P=Pawn.Get(); if (!P) return false; GuLiHybridEngineer::FPrivate V;
        V.Key=Record.Key; V.Type=P->UnitTypeId; V.TransitRequest=P->LastTransitRequestId; V.Pose=ReadPose();
        if (!P->Travel->CaptureHybridTravel(V.Travel) || !P->Work->CaptureHybridWork(V.Work)) return false;
        FGuLiHybridStateArchive A; GuLiHybridEngineer::Private(A,V); return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridEngineer::FPrivate V; const auto* P=Pawn.Get(); const auto* D=Definition();
        return P && D && GuLiHybridEngineer::Supported(*D) && GuLiHybridEngineer::Decode(Bytes,V) && V.Key==Record.Key
            && V.Type==D->UnitTypeId && P->Travel->ValidateHybridTravel(V.Travel) && P->Work->ValidateHybridWork(V.Work);
    }
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override {return RestoreImpl(B,S);}
    bool RestoreQuery(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override {return RestoreImpl(B,S);}
    void Release() override
    {
        if (auto* P=Pawn.Get()) {if (auto* AI=P->GetController()) AI->Destroy(); P->Destroy();}
        Pawn.Reset(); LastApplied=LastPublished=0;
    }
};

static const FGuLiHybridAdapterRegistration GGuLiEngineerAdapterFamily({TEXT("Engineer"),EGuLiHybridObjectKind::Engineer,GuLiHybridEngineer::Version,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridEngineerAdapter>(Owner);},nullptr,GuLiHybridTargets::Actor<EGuLiTargetKind::GroundActor,true>,EGuLiHybridOwnershipPolicy::SharedGroup,0,false,TEXT("match/resources")});
static bool AppendEngineerCatalog(UWorld& World,FGuLiHybridManifestData& Data,FString& Error)
{
    auto* Table=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
    if (!Table) {Error=TEXT("Missing engineering unit catalog"); return false;}
    int32 Count=0; TMap<uint16,FGuLiSoldierDefinition> Definitions;
    for (const auto Name:Table->GetRowNames())
    {
        bool Valid=false; const auto D=FGuLiCommanderSoldierResolver::Resolve(Table,Name,FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),Valid);
        if (!Valid) {Error=TEXT("Invalid engineering catalog row: ")+Name.ToString(); return false;}
        if (!GuLiHybridEngineer::Supported(D)) continue;
        if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,FString::Printf(TEXT("unit/%u"),D.UnitTypeId),TEXT("Engineer"),Error)) return false;
        const FString Prefix=FString::Printf(TEXT("engineer_adapter/%u/"),D.UnitTypeId);
        Data.Numbers.Add(Prefix+TEXT("health"),D.MaxHealth); Data.Numbers.Add(Prefix+TEXT("speed"),D.MovementSpeedCmPerSecond);
        Data.Strings.Add(Prefix+TEXT("class"),D.ActorClass->GetPathName()); ++Count;
        Definitions.Add(D.UnitTypeId,D);
    }
    if (!Count) {Error=TEXT("Production engineer definition is unavailable"); return false;}
    const auto* Map=LoadObject<UGuLiResourceMapDefinition>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
    const uint16 Type=static_cast<uint16>(Data.Numbers.FindRef(TEXT("economy/ConstructionVehicleUnitTypeId")));
    const int32 Initial=static_cast<int32>(Data.Numbers.FindRef(TEXT("economy/InitialConstructionVehiclesPerTeam")));
    const auto* D=Definitions.Find(Type); auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
    const auto* Prototype=D?Cast<ACharacter>(D->ActorClass->GetDefaultObject()):nullptr;
    const auto* NavData=Nav && Prototype?Nav->GetNavDataForProps(Prototype->GetNavAgentPropertiesRef()):nullptr;
    const auto* Grid=World.GetSubsystem<UGuLiGroundGridSubsystem>();
    if (!Map || !D || (!NavData && !(Grid && Grid->IsReady())) || Initial<0 || Initial>1024) {Error=TEXT("Invalid production engineer startup/configuration"); return false;}
    uint32 Ordinal=0;
    for (const auto& O:Data.Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Engineer || O.Key.Kind==EGuLiHybridObjectKind::Miner) Ordinal=FMath::Max(Ordinal,O.Ordinal);
    for (const auto Team:{EGuLiTeam::Red,EGuLiTeam::Blue}) for (int32 Index=0; Index<Initial; ++Index)
    {
        const FVector Assembly=Team==EGuLiTeam::Red?Map->SpawnAnchors.RedAssembly:Map->SpawnAnchors.BlueAssembly;
        const FVector Desired=Assembly+GuLiResources::InitialEngineeringVehicleOffset(Team,Index,true);
        FNavLocation Ground;
        if (!GuLiGrid::ProjectBody(World,Desired,D->FootprintCells,FVector(300,300,100000),Ground,NavData))
        {Error=FString::Printf(TEXT("Production engineer startup has no navigable ground: team=%u index=%d"),uint8(Team),Index); return false;}
        auto& O=Data.Objects.AddDefaulted_GetRef(); O.Team=Team; O.Ordinal=++Ordinal;
        O.Key=FGuLiHybridManifest::MakeStableKey(Data.MapPackage,EGuLiHybridObjectKind::Engineer,FString::Printf(TEXT("initial/engineer/%u/%d"),uint8(Team),Index));
        O.Definition=FString::Printf(TEXT("unit/%u"),Type);
        O.Location=Ground.Location+FVector(0,0,Prototype->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*D->GridDimensionScale);O.Health=D->MaxHealth;
    }
    Data.Numbers.Add(TEXT("startup/engineer_count"),Initial*2);
    Data.Strings.FindOrAdd(TEXT("startup/coverage"))+=TEXT(",Engineer");
    Error.Reset(); return true;
}

static const FGuLiHybridManifestRegistration EngineerManifestProvider({TEXT("Engineer"),EGuLiHybridManifestPhase::Definitions,{},AppendEngineerCatalog});
