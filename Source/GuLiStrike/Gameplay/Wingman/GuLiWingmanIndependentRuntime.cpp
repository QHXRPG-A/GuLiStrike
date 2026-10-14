#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"
#include "Gameplay/Wingman/Private/GuLiWingmanNavigationRuntime.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/Private/GuLiHybridReflectedState.h"
#include "Components/StateTreeComponent.h"
#include "Engine/World.h"

namespace
{
constexpr uint32 Version=1,MaximumBytes=256*1024;
struct FState
{
    FGuLiWingmanRuntimeState Runtime;FGuLiGroupAbilityConfigSnapshot Configuration;
    FTransform Transform=FTransform::Identity,Previous=FTransform::Identity,Current=FTransform::Identity;
    EGuLiWingmanMemberBehavior Observed=EGuLiWingmanMemberBehavior::EscortOrbit,Selected=EGuLiWingmanMemberBehavior::EscortOrbit;
    float BehaviorAccumulator=0,NavigationAccumulator=0;double RebaseRetry=0;
    EGuLiWingmanEmergencyRebaseReason RebaseReason=EGuLiWingmanEmergencyRebaseReason::MovementDeadlock;
    bool UsingTree=false,EmergencyPending=false,CombatAuthorized=true,ExternalLocked=false;
    uint32 LastAttackTick=0;
    struct FShot {uint8 Flight=0;FGuLiWingmanAttackFireRecord Record;};TArray<FShot> Shots;
    TArray<FGuLiWingmanFlightNavigationRuntime> Navigation;
};
void Runtime(FGuLiHybridStateArchive& A,FGuLiWingmanRuntimeState& R)
{
    FGuLiHybridReflectedState::Value(A,R.Identity.Handle);A.Value(R.Identity.WingmanTypeId);
    auto& B=R.Ability;A.Value(B.AbilitySetRevision);A.Value(B.LoadoutRevision);A.Value(B.FormationCommandRevision);A.Value(B.FormationDefinitionChecksum);
    FGuLiHybridReflectedState::Value(A,R.Tuning.Formation);FGuLiHybridReflectedState::Value(A,R.Tuning.BasicWeapon);
    A.Value(R.Carrier.Transform);A.Value(R.Carrier.Velocity);FGuLiHybridReflectedState::Value(A,R.Carrier.Source);
    auto& F=R.FormationSlot;A.Value(F.RadiusCentimeters);A.Value(F.HeightCentimeters);A.Value(F.PhaseRadians);
    A.Value(F.AngularSpeedRadiansPerSecond);A.Value(F.bClockwise);
    auto& S=R.SwarmAgent;A.Value(S.FlightSeed);A.Value(S.AgentSeed);A.Value(S.FlowSimulationTick);A.Value(S.FlowStepAccumulator);
    A.Value(S.PreferredRadiusBaseCentimeters);A.Value(S.PreferredVerticalBiasCentimeters);A.Value(S.BaseOrbitAxis);A.Value(S.NoiseDomainOffset);A.Value(S.SwirlSign);
    auto& G=R.Guidance;A.Value(G.DesiredPosition);A.Value(G.DesiredForward);A.Value(G.PreferredVelocity);
    A.Value(G.DesiredSpeedCentimetersPerSecond);A.Value(G.bUsesVelocityField);A.Value(G.bAttackGuidance);
    auto& N=R.Navigation;A.Value(N.Waypoint);A.Value(N.PathGoal);A.Value(N.AbilitySetRevision);A.Value(N.FormationCommandRevision);
    A.Value(N.RequestSerial);A.Value(N.PathPointIndex);A.Value(N.bHasPath);A.Value(N.bUsingSafeFallback);
    auto& V=R.Avoidance;A.Value(V.SafeDirection);A.Value(V.NextStepSafeDirection);A.Value(V.LastVerifiedSafePoint);A.Value(V.RecoveryEscapeDirection);
    A.Value(V.ConsecutiveBlockedSeconds);A.Value(V.ConsecutiveClearSeconds);A.Value(V.RecoveryPointValidationAccumulator);
    A.Value(V.NavigationSpeedLimitCentimetersPerSecond);A.Value(V.NoProgressSeconds);A.Value(V.NormalProgressSeconds);
    A.Value(V.VerifiedWaypointRequestSerial);A.Value(V.VerifiedWaypointPathPointIndex);A.Value(V.HeadingProbeCount);
    A.Value(V.bHasSafeDirection);A.Value(V.bHasNextStepSafeDirection);A.Value(V.bHasNavigationSpeedLimit);A.Value(V.bHasVerifiedSafePoint);
    A.Value(V.bHasRecoveryEscapeDirection);A.Value(V.bRecoveryPointCurrentlyValid);A.Value(V.bControlledRecovery);A.Value(V.bAwaitingRebase);
    A.Value(V.bRebaseRequested);A.Value(V.bDetectedWorldStatic);A.Value(V.bDetectedWorldDynamic);A.Value(V.bDetectedFlightNavBoundary);
    auto& D=R.Dynamics;A.Value(D.Velocity);A.Value(D.BankDegrees);A.Value(D.FixedStepAccumulator);A.Value(D.CaptureSimulationTick);
    A.Value(D.ModeEvaluationAccumulator);A.Enum(D.Mode,uint8(EGuLiWingmanFlightMode::Stale));A.Value(D.bAlive);A.Value(D.bStale);
    auto& K=R.Attack;A.Enum(K.Phase,uint8(EGuLiWingmanAttackPhase::AirOrbitCooldown));
    FGuLiHybridReflectedState::Value(A,K.Path);FGuLiHybridReflectedState::Value(A,K.Target);A.Value(K.SlotId);A.Value(K.SkillId);
    A.Value(K.DefinitionChecksum);A.Value(K.EntityGeneration);A.Value(K.ProfileRevision);A.Value(K.LeaseEpoch);
    A.Value(K.RunId);A.Value(K.CompletedGroundRuns);A.Value(K.NextShotIndex);A.Value(K.StartTime);A.Value(K.PhaseStartTime);A.Value(K.RetryAfter);
    A.Value(K.LastCancelReason);A.Value(K.GroundPathFailureMask);A.Value(K.GroundNavigationFailureMask);A.Value(K.PreferredVelocity);A.Value(K.bGuiding);
    auto& W=R.Weapon;A.Value(W.NextBasicFireSeconds);A.Value(W.DomainFireSequence);
    for (int32 I=0;I<GULI_MAX_WINGMAN_WEAPON_CHANNELS;++I) {A.Value(W.WeaponSlotIds[I]);A.Value(W.NextFireSecondsByWeaponSlot[I]);}
    FGuLiHybridReflectedState::Value(A,W.Target);FGuLiHybridReflectedState::Value(A,W.SourceAcceptedState);
    A.Value(W.SourceAcceptedAbilitySetRevision);A.Value(W.SourceAcceptedFormationCommandRevision);A.Value(W.SourceAcceptedFormationDefinitionChecksum);
    if (!R.Identity.Handle.IsValid() || R.Identity.WingmanTypeId.IsNone() || !R.Tuning.Formation.IsWellFormed()
        || !B.AbilitySetRevision || !B.LoadoutRevision || !B.FormationCommandRevision || !B.FormationDefinitionChecksum
        || !S.FlightSeed || !S.AgentSeed || !D.CaptureSimulationTick || D.FixedStepAccumulator<0 || D.FixedStepAccumulator>.2f
        || S.FlowStepAccumulator<0 || S.FlowStepAccumulator>.2f || K.NextShotIndex<0 || K.EntityGeneration!=R.Identity.Handle.EntityGeneration) A.Reject();
    TSet<FName> Slots;for (const auto Slot:W.WeaponSlotIds) if (!Slot.IsNone()) {if (Slots.Contains(Slot)) A.Reject();Slots.Add(Slot);}
}
void SerializeIndependentState(FGuLiHybridStateArchive& A,FState& S)
{
    uint32 Schema=Version;A.Value(Schema);if (Schema!=Version) A.Reject();
    Runtime(A,S.Runtime);FGuLiHybridReflectedState::Value(A,S.Configuration);
    A.Value(S.Transform);A.Value(S.Previous);A.Value(S.Current);
    A.Enum(S.Observed,uint8(EGuLiWingmanMemberBehavior::EscortOrbit));A.Enum(S.Selected,uint8(EGuLiWingmanMemberBehavior::EscortOrbit));
    A.Value(S.BehaviorAccumulator);A.Value(S.NavigationAccumulator);A.Value(S.RebaseRetry);
    A.Enum(S.RebaseReason,uint8(EGuLiWingmanEmergencyRebaseReason::CarrierBoundaryDeadlock));
    A.Value(S.UsingTree);A.Value(S.EmergencyPending);A.Value(S.CombatAuthorized);A.Value(S.ExternalLocked);A.Value(S.LastAttackTick);
    A.Array(S.Shots,64,[](auto& Ar,auto& V){Ar.Value(V.Flight);FGuLiHybridReflectedState::Value(Ar,V.Record);if (V.Flight>=GULI_WINGMAN_FLIGHT_COUNT) Ar.Reject();});
    A.Array(S.Navigation,GULI_WINGMAN_FLIGHT_COUNT,[](auto& Ar,auto& N)
    {
        Ar.Array(N.PathPoints,4096,[](auto& R,auto& Point){R.Value(Point);});
        Ar.Value(N.RequestedStart);Ar.Value(N.RequestedGoal);Ar.Value(N.ActivePathGoal);Ar.Value(N.NextRequestSeconds);
        Ar.Value(N.RequestSerial);Ar.Value(N.PendingRequestSerial);Ar.Value(N.RequestedAbilitySetRevision);Ar.Value(N.RequestedFormationCommandRevision);
        Ar.Value(N.NextPathPointIndex);Ar.Value(N.bRequestPending);Ar.Value(N.bUsingSafeFallback);
        if ((N.NextPathPointIndex!=INDEX_NONE && !N.PathPoints.IsValidIndex(N.NextPathPointIndex))
            || (N.bRequestPending && (!N.RequestSerial || N.PendingRequestSerial!=N.RequestSerial
                || !N.RequestedAbilitySetRevision || !N.RequestedFormationCommandRevision))) Ar.Reject();
    });
    const auto& R=S.Runtime;const auto& C=S.Configuration;
    if (!C.IsUsableByLeaseOwner() || C.ShipInstanceId!=R.Identity.Handle.Flight.Group.ShipInstanceId
        || C.ShipGeneration!=R.Identity.Handle.Flight.Group.ShipGeneration || C.GroupGeneration!=R.Identity.Handle.Flight.Group.GroupGeneration
        || C.WingmanTypeId!=R.Identity.WingmanTypeId || C.AbilitySetRevision!=R.Ability.AbilitySetRevision
        || C.LoadoutRevision!=R.Ability.LoadoutRevision || C.FormationCommandRevision!=R.Ability.FormationCommandRevision
        || C.FormationDefinitionChecksum!=R.Ability.FormationDefinitionChecksum || S.Navigation.Num()!=GULI_WINGMAN_FLIGHT_COUNT
        || S.BehaviorAccumulator<0 || S.NavigationAccumulator<0) A.Reject();
}
bool Read(TConstArrayView<uint8> Bytes,FState& S)
{FGuLiHybridStateArchive A(Bytes,MaximumBytes);SerializeIndependentState(A,S);return A.Done();}
}

TSharedPtr<FGuLiWingmanLocalGroupRuntime> UGuLiWingmanSimulationSubsystem::CreateIndependentMember(const FGuLiWingmanHandle& Member,
    const FGuLiGroupAbilityConfigSnapshot& Configuration,const FTransform& Carrier,const FVector& CarrierVelocity,
    const FGuLiCarrierSourceRef& Source,const FVector& Position,AActor* CarrierActor)
{
    const auto& Group=Member.Flight.Group;
    if (!CanOwnSimulation() || !Member.IsValid() || !Configuration.IsUsableByLeaseOwner() || !Source.IsValid()
        || Configuration.ShipInstanceId!=Group.ShipInstanceId || Configuration.ShipGeneration!=Group.ShipGeneration
        || Configuration.GroupGeneration!=Group.GroupGeneration || Carrier.ContainsNaN() || CarrierVelocity.ContainsNaN()
        || Position.ContainsNaN() || IndependentPawns.Contains(Member)) return {};
    auto Value=MakeShared<FGuLiWingmanLocalGroupRuntime>();Value->AbilityConfig=Configuration;Value->CarrierActor=CarrierActor;
    for (uint8 I=0;I<GULI_WINGMAN_FLIGHT_COUNT;++I) Value->FlightNavigation.Add(MakeShared<FGuLiWingmanFlightNavigationRuntime>());
    FActorSpawnParameters Parameters;Parameters.ObjectFlags|=RF_Transient;Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Pawn=GetWorld()->SpawnActor<AGuLiWingmanPawn>(AGuLiWingmanPawn::StaticClass(),FTransform(Position),Parameters);
    const int32 Slot=Member.Flight.FlightIndex*GULI_WINGMAN_MEMBERS_PER_FLIGHT+Member.MemberIndex;
    if (!Pawn || !InitializeOwnedPawn(*Pawn,Group,Slot,Configuration,Position,Carrier,CarrierVelocity,Source,CarrierActor))
    {if (Pawn) Pawn->Destroy();return {};}
    Pawn->Runtime.Identity.Handle=Member;Pawn->Runtime.Attack.EntityGeneration=Member.EntityGeneration;
    Value->Pawns.Add(Pawn);IndependentPawns.Add(Member,Pawn);IndependentRuntimes.Add(Member,Value);return Value;
}
bool UGuLiWingmanSimulationSubsystem::AdvanceIndependentMember(FGuLiWingmanLocalGroupRuntime& Runtime,float DeltaSeconds,double LogicalSeconds)
{return Runtime.Pawns.Num()==1 && FMath::IsFinite(LogicalSeconds) && AdvanceRuntime(Runtime,DeltaSeconds,LogicalSeconds,true);}
bool UGuLiWingmanSimulationSubsystem::ApplyIndependentExternalControl(FGuLiWingmanLocalGroupRuntime& Runtime,bool Phased,bool Locked,const FTransform* Destination)
{
    auto* P=Runtime.Pawns.Num()==1?Runtime.Pawns[0].Get():nullptr;auto* M=P?P->GetFlightMovement():nullptr;
    if (!P || !M || (Destination && Destination->ContainsNaN())) return false;
    if (Locked || Destination) CancelNavigationForRuntime(Runtime,true);
    if (Locked) {P->Runtime.Dynamics.Velocity=FVector::ZeroVector;P->Runtime.Guidance.PreferredVelocity=FVector::ZeroVector;}
    Runtime.bExternalActionsLocked=Locked;
    // Pause the existing flight integrator and discard its interpolation history
    // at a committed displacement; resuming cannot snap to a pre-teleport pose.
    if (Destination)
    {
        P->SetActorTransform(*Destination,false,nullptr,ETeleportType::TeleportPhysics);
        M->PreviousSimulationTransform=M->CurrentSimulationTransform=*Destination;
        P->Runtime.Guidance.DesiredPosition=Destination->GetLocation();P->Runtime.Navigation.bHasPath=false;
        P->Runtime.Avoidance.bHasVerifiedSafePoint=false;P->Runtime.Avoidance.bHasSafeDirection=false;
    }
    P->SetPhaseAppearance(Phased);return true;
}
void UGuLiWingmanSimulationSubsystem::ReleaseIndependentMember(FGuLiWingmanLocalGroupRuntime& Runtime)
{
    CancelNavigationForRuntime(Runtime,true);
    for (const auto& Entry:Runtime.Pawns) if (auto* Pawn=Entry.Get()) {IndependentRuntimes.Remove(Pawn->GetWingmanHandle());IndependentPawns.Remove(Pawn->GetWingmanHandle());Pawn->Destroy();}
    Runtime.Pawns.Reset();Runtime.FlightNavigation.Reset();Runtime.PendingAttackShots.Reset();
}
bool UGuLiWingmanSimulationSubsystem::CaptureIndependentMember(const FGuLiWingmanLocalGroupRuntime& Runtime,TArray<uint8>& Bytes) const
{
    const auto* Pawn=Runtime.Pawns.Num()==1?Runtime.Pawns[0].Get():nullptr;const auto* Movement=Pawn?Pawn->GetFlightMovement():nullptr;
    if (!Pawn || !Movement || !Pawn->IsOwnerSimulationPawn() || Runtime.FlightNavigation.Num()!=GULI_WINGMAN_FLIGHT_COUNT) return false;
    // UE normalizes a restored scene quaternion. Keep the integrator's exact cut
    // as the canonical pose; scene reconstruction may differ by a final ULP.
    if (!Pawn->GetActorTransform().Equals(Movement->CurrentSimulationTransform,1.e-8)) return false;
    FState S;S.Runtime=Pawn->Runtime;S.Configuration=Runtime.AbilityConfig;S.Transform=Movement->CurrentSimulationTransform;
    S.Previous=Movement->PreviousSimulationTransform;S.Current=Movement->CurrentSimulationTransform;
    S.Observed=Pawn->ObservedBehavior;S.Selected=Pawn->SelectedBehavior;S.BehaviorAccumulator=Pawn->BehaviorObservationAccumulator;
    S.RebaseRetry=Pawn->RebaseRetryAfterServerTimeSeconds;S.RebaseReason=Pawn->PendingRebaseReason;
    S.UsingTree=Pawn->bUsingStateTree;S.EmergencyPending=Pawn->bEmergencyRebasePending;
    S.CombatAuthorized=Runtime.bCombatAuthorizationValid;S.ExternalLocked=Runtime.bExternalActionsLocked;
    S.NavigationAccumulator=Runtime.NavigationEvaluationAccumulator;S.LastAttackTick=Runtime.LastAttackTick;
    for (const auto& Shot:Runtime.PendingAttackShots) S.Shots.Add({Shot.Key,Shot.Value});
    for (const auto& N:Runtime.FlightNavigation) {if (!N) return false;S.Navigation.Add(*N);}
    FGuLiHybridStateArchive A(MaximumBytes);SerializeIndependentState(A,S);return A.Finish(Bytes);
}
bool UGuLiWingmanSimulationSubsystem::ValidateIndependentMember(TConstArrayView<uint8> Bytes,const FGuLiWingmanHandle& Member) const
{FState S;return Read(Bytes,S) && S.Runtime.Identity.Handle==Member;}
bool UGuLiWingmanSimulationSubsystem::RestoreIndependentMember(FGuLiWingmanLocalGroupRuntime& Runtime,TConstArrayView<uint8> Bytes,bool ReissueNavigation)
{
    auto* Pawn=Runtime.Pawns.Num()==1?Runtime.Pawns[0].Get():nullptr;auto* Movement=Pawn?Pawn->GetFlightMovement():nullptr;FState S;
    if (!Pawn || !Movement || !Read(Bytes,S) || S.Runtime.Identity.Handle!=Pawn->GetWingmanHandle()) return false;
    CancelNavigationForRuntime(Runtime,false);Pawn->Runtime=S.Runtime;Pawn->SetActorTransform(S.Transform,false,nullptr,ETeleportType::TeleportPhysics);
    Movement->PreviousSimulationTransform=S.Previous;Movement->CurrentSimulationTransform=S.Current;
    Pawn->ObservedBehavior=S.Observed;Pawn->SelectedBehavior=S.Selected;Pawn->BehaviorObservationAccumulator=S.BehaviorAccumulator;
    Pawn->RebaseRetryAfterServerTimeSeconds=S.RebaseRetry;Pawn->PendingRebaseReason=S.RebaseReason;Pawn->bEmergencyRebasePending=S.EmergencyPending;
    if (Pawn->bUsingStateTree!=S.UsingTree) return false;
    Runtime.AbilityConfig=S.Configuration;Runtime.bCombatAuthorizationValid=S.CombatAuthorized;Runtime.bExternalActionsLocked=S.ExternalLocked;
    Runtime.NavigationEvaluationAccumulator=S.NavigationAccumulator;Runtime.LastAttackTick=S.LastAttackTick;Runtime.PendingAttackShots.Reset();
    for (const auto& Shot:S.Shots) Runtime.PendingAttackShots.Add({Shot.Flight,Shot.Record});
    Runtime.FlightNavigation.Reset();
    const auto* Navigation=GetWorld()->GetSubsystem<UGuLiFlightNavigationSubsystem>();
    for (auto& N:S.Navigation)
    {
        auto Restored=MakeShared<FGuLiWingmanFlightNavigationRuntime>();*Restored=MoveTemp(N);
        if (Restored->bRequestPending && ReissueNavigation)
        {
            if (!Navigation) return false;
            Restored->CancellationToken=MakeShared<FGuLiFlightNavCancellationToken,ESPMode::ThreadSafe>();
            FGuLiFlightNavPathQueryOptions Options;Options.AgentRadius=S.Runtime.Tuning.Formation.AgentRadiusCentimeters;
            Options.MaximumExpandedNodes=GuLiWingmanNavigationMaximumExpandedNodes;Options.bSmoothPath=true;
            auto Future=Navigation->FindPathAsync(Restored->RequestedStart,Restored->RequestedGoal,Options,Restored->CancellationToken);
            Restored->PendingResult=MakeShared<TFuture<FGuLiFlightNavPathResult>>(MoveTemp(Future));
        }
        Runtime.FlightNavigation.Add(MoveTemp(Restored));
    }
    return true;
}
