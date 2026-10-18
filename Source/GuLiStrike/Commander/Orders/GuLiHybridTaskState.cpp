#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Commander/Behavior/GuLiCommanderStateTreeComponent.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Units/GuLiGroundCrowdManager.h"

namespace GuLiHybridTasks
{
constexpr uint32 Version=3, MaximumBytes=16*1024*1024;
const FName SystemName(TEXT("CommanderTasks"));
struct FUnit
{
    FGuLiHybridObjectKey Key;
    FGuLiTaskUnitId Id;
    FGuid Owner;
    bool bPresent=false,bInitialized=false;
    FGuLiUnitTaskState State;
};
struct FRequest
{
    FGuLiTaskUnitId Unit;
    EGuLiCommanderBehaviorStep Step=EGuLiCommanderBehaviorStep::Wait;
    uint64 Version=0;
    FGuLiCommanderOperationToken Token;
};
struct FSnapshot
{
    uint32 NextExecution=1;
    uint64 Round=0;
    double Accumulator=0;
    TArray<TSharedPtr<FGuLiSharedMoveIntent>> Intents;
    TArray<FUnit> Units;
    TArray<FRequest> Requests;
};
void UnitId(FGuLiHybridStateArchive& A,FGuLiTaskUnitId& V)
{ A.Value(V.Id); A.Value(V.Generation); A.Value(V.bActor); if (!V.IsValid()) A.Reject(); }
void Token(FGuLiHybridStateArchive& A,FGuLiCommanderOperationToken& V)
{ A.Value(V.Version); A.Value(V.ExecutionId); A.Value(V.Serial); }
void Intent(FGuLiHybridStateArchive& A,TSharedPtr<FGuLiSharedMoveIntent>& P)
{
    if (A.IsReading()) P=MakeShared<FGuLiSharedMoveIntent>();
    if (!P || !P->Goal) { if (!A.IsReading()) { A.Reject(); return; } P->Goal=MakeShared<FGuLiSharedRouteGoal>(); }
    auto& V=*P; A.Value(V.Click); A.Value(V.Goal->Target);
    A.Array(V.Points,10000,[](auto& Ar,auto& N){ Ar.Value(N.Location); });
    TArray<uint32> Members;
    if (!A.IsReading()) { V.AssignedPoints.GenerateKeyArray(Members); Members.Sort(); }
    A.Array(Members,10000,[&](auto& Ar,uint32& K)
    {
        Ar.Value(K); FVector Location=Ar.IsReading()?FVector::ZeroVector:V.AssignedPoints.FindChecked(K).Location;
        Ar.Value(Location);
        if (!K || (Ar.IsReading() && V.AssignedPoints.Contains(K))) Ar.Reject();
        if (Ar.IsReading() && Ar.Good()) V.AssignedPoints.Add(K,FNavLocation(Location));
    });
    A.Array(V.UnassignedMembers,10000,[](auto& Ar,uint32& K){ Ar.Value(K); if (!K) Ar.Reject(); });
    TArray<uint16> Types;
    if (!A.IsReading()) { V.WidthByUnitType.GenerateKeyArray(Types); Types.Sort(); }
    A.Array(Types,4096,[&](auto& Ar,uint16& K)
    {
        Ar.Value(K); float Width=Ar.IsReading()?0:V.WidthByUnitType.FindChecked(K); Ar.Value(Width);
        if (!K || Width<=0 || (Ar.IsReading() && V.WidthByUnitType.Contains(K))) Ar.Reject();
        if (Ar.IsReading() && Ar.Good()) V.WidthByUnitType.Add(K,Width);
    });
    A.Random(V.Random); A.Value(V.Pitch); A.Value(V.GeneratedRadius); A.Value(V.WantedPoints);
    A.Value(V.MetadataCursor); A.Value(V.Ring); A.Value(V.Step); A.Value(V.CoverageGeneration);
    A.Value(V.bMetadataReady); A.Value(V.bExhausted);
    if (V.Pitch<0 || V.GeneratedRadius<0 || V.WantedPoints<0 || V.WantedPoints>10000
        || V.MetadataCursor<0 || V.Ring<0 || V.Step<0) A.Reject();
}
void Command(FGuLiHybridStateArchive& A,FGuLiUnitTaskCommand& V,FSnapshot& Snapshot)
{
    uint32 Index=0;
    if (!A.IsReading() && V.SharedMoveIntent)
    {
        int32 Found=Snapshot.Intents.IndexOfByPredicate([&](const auto& I){ return I==V.SharedMoveIntent; });
        if (Found==INDEX_NONE) { A.Reject(); return; } Index=Found+1;
    }
    A.Value(Index);
    if (Index>uint32(Snapshot.Intents.Num())) { A.Reject(); return; }
    if (A.IsReading()) V.SharedMoveIntent=Index?Snapshot.Intents[Index-1]:nullptr;
    A.Value(V.CommandId); A.Value(V.SelectionRevision);
    A.Enum(V.Disposition,uint8(EGuLiTaskDisposition::Stop)); A.Enum(V.Kind,uint8(EGuLiUnitTaskKind::Transit));
    A.Value(V.SpecialTaskId); FVector Target=V.Target; A.Value(Target); if (A.IsReading()) V.Target=Target;
    A.Value(V.BuildingId); A.Value(V.ClusterId); A.Value(V.TerritoryId); A.Value(V.bGroundMoveOnly); A.Value(V.TargetIntent);
    if (!V.IsWellFormed()) A.Reject();
}
void Execution(FGuLiHybridStateArchive& A,TOptional<FGuLiTaskExecution>& Optional,FSnapshot& Snapshot)
{
    bool Present=Optional.IsSet(); A.Value(Present);
    if (!Present) { if (A.IsReading()) Optional.Reset(); return; }
    if (A.IsReading()) Optional.Emplace(); auto& V=*Optional;
    Command(A,V.Command,Snapshot); A.Value(V.Version); A.Value(V.ExecutionId); A.Value(V.WorkSerial);
    A.Value(V.CurrentObjective); A.Value(V.bAutomatic); A.Value(V.bStarted); A.Value(V.bYieldRequested);
    A.Value(V.bPlanning); A.Value(V.bWaitingForTarget); A.Enum(V.Status,uint8(EGuLiTaskStatus::Stopped));
    A.Value(V.bTerminalObserved); A.Value(V.bFinishRequested); A.String(V.Error);
    if (!V.ExecutionId) A.Reject();
}
void Snapshot(FGuLiHybridStateArchive& A,FSnapshot& V)
{
    uint32 Schema=Version; A.Value(Schema); if (Schema!=Version) A.Reject();
    A.Value(V.NextExecution); A.Value(V.Round); A.Value(V.Accumulator);
    if (!V.NextExecution || V.Accumulator<0 || V.Accumulator>.5) A.Reject();
    A.Array(V.Intents,4096,[](auto& Ar,auto& I){ Intent(Ar,I); });
    A.Array(V.Units,10000,[&](auto& Ar,FUnit& U)
    {
        Ar.Value(U.Key); UnitId(Ar,U.Id); Ar.Value(U.Owner); Ar.Value(U.bPresent); Ar.Value(U.bInitialized);
        if (!U.Key.IsValid() || U.Key.LifeGeneration!=U.Id.Generation) Ar.Reject();
        if (!U.bPresent) return;
        auto& S=U.State; S.Context.Unit=U.Id;
        Ar.Enum(S.Context.Team,uint8(EGuLiTeam::Blue)); Ar.Value(S.Context.UnitTypeId); Ar.Value(S.Context.Location); Ar.Value(S.Context.SourceTerritory);
        Ar.Array(S.AutomaticBehaviors,64,[](auto& B,auto& Grant)
        { B.Value(Grant.BehaviorId); B.Enum(Grant.Lifetime,uint8(EGuLiTaskLifetime::Persistent)); B.Value(Grant.bConsumed); });
        Ar.Value(S.bBehaviorStepDone); Ar.Value(S.bUnregisterPending); Ar.Value(S.LastBehaviorRequestRound);
        Ar.Value(S.NextOperationSerial); Ar.Value(S.LastOperationRequestRound); Token(Ar,S.ActiveOperation);
        Token(Ar,S.OperationReceipt.Token); Ar.Enum(S.OperationReceipt.Result,uint8(EGuLiCommanderOperationResult::Stale));
        Ar.Enum(S.OperationControl,uint8(EGuLiCommanderControlState::ReplacementMove));
        Ar.Array(S.Queue,256,[&](auto& B,auto& C){ Command(B,C,V); });
        Execution(Ar,S.Active,V); Execution(Ar,S.PendingMove,V);
        Ar.Value(S.Version); Ar.Value(S.bStopped); Ar.Value(S.bCancelPending); Ar.Value(S.NextAutomaticTime); Ar.String(S.Error);
    });
    A.Array(V.Requests,65536,[](auto& Ar,FRequest& R)
    { UnitId(Ar,R.Unit); Ar.Enum(R.Step,uint8(EGuLiCommanderBehaviorStep::FinishOrder)); Ar.Value(R.Version); Token(Ar,R.Token); });
}
bool Decode(TConstArrayView<uint8> Bytes,FSnapshot& Out)
{
    FGuLiHybridStateArchive A(Bytes,MaximumBytes); Snapshot(A,Out);
    if (!A.Done()) return false;
    TSet<FGuLiHybridObjectKey> Keys; TSet<FGuLiTaskUnitId> Units;
    for (const auto& U : Out.Units)
    { if (Keys.Contains(U.Key) || Units.Contains(U.Id)) return false; Keys.Add(U.Key); Units.Add(U.Id); }
    for (const auto& R : Out.Requests) if (!Units.Contains(R.Unit)) return false;
    return true;
}
}

class FGuLiHybridTaskState final : public IGuLiHybridSharedStateAdapter
{
    TWeakObjectPtr<UGuLiUnitTaskSubsystem> Tasks;
    bool Resolve(const FGuLiHybridObjectKey& Key,FGuLiTaskUnitId& Out) const
    {
        auto* Self=Tasks.Get(); auto* Hybrid=Self?Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
        FGuLiHybridObjectState Object;
        if (!Hybrid || !Hybrid->GetObject(Key,Object) || !Object.Ordinal) return false;
        Out.Id=Object.Ordinal;Out.Generation=Key.LifeGeneration;Out.bActor=Hybrid->FindBoundActor(Key)!=nullptr;return true;
    }
public:
    explicit FGuLiHybridTaskState(UGuLiUnitTaskSubsystem& V) : Tasks(&V) {}
    UObject* LifetimeOwner() const override { return Tasks.Get(); }
    uint32 Version() const override { return GuLiHybridTasks::Version; }
    void Advance(uint64 LocalTick,double Delta) override
    {
        auto* Self=Tasks.Get();if (!Self) return;
        // Shared schedulers advance once per LOCAL world step. A transferred
        // object's age can be ahead of every other object and must never become
        // the deduplication clock for their tasks or navigation.
        Self->AdvanceHybrid(LocalTick,float(Delta));
        if (auto* Crowd=Cast<UGuLiGroundCrowdManager>(UCrowdManager::GetCurrent(Self->GetWorld())))
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_CrowdAdvance);
            Crowd->AdvanceHybrid(LocalTick,float(Delta));
        }
    }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    {
        using namespace GuLiHybridTasks;
        auto* Self=Tasks.Get(); if (!Self) return false; FSnapshot V;
        V.NextExecution=Self->NextExecutionId; V.Round=Self->BehaviorRound; V.Accumulator=Self->Accumulator;
        TSet<FGuLiTaskUnitId> Selected;TSet<uint32> SelectedMembers;
        for (const auto& K:Objects) {FGuLiTaskUnitId Id;if (!Resolve(K,Id)) return false;Selected.Add(Id);SelectedMembers.Add(Id.Id);}
        TMap<const FGuLiSharedMoveIntent*,TSharedPtr<FGuLiSharedMoveIntent>> Copies;
        auto Collect=[&](FGuLiUnitTaskCommand& C)
        {
            if (!C.SharedMoveIntent) return;
            const auto* Original=C.SharedMoveIntent.Get();auto* Existing=Copies.Find(Original);
            if (!Existing)
            {
                auto Copy=MakeShared<FGuLiSharedMoveIntent>(*C.SharedMoveIntent);
                for (auto It=Copy->AssignedPoints.CreateIterator();It;++It) if (!SelectedMembers.Contains(It.Key())) It.RemoveCurrent();
                Copy->UnassignedMembers.RemoveAll([&](uint32 Id){return !SelectedMembers.Contains(Id);});
                // Assigned formation slots are preserved. Navigation handles and unused world-wide slots are rebuilt locally.
                if (Copy->UnassignedMembers.IsEmpty()) Copy->Points.Reset();
                Copy->Goal=MakeShared<FGuLiSharedRouteGoal>();Copy->Goal->Target=Original->Goal->Target;
                Copies.Add(Original,Copy);V.Intents.Add(Copy);Existing=Copies.Find(Original);
            }
            C.SharedMoveIntent=*Existing;
        };
        for (const auto& Key : Objects)
        {
            FUnit U; U.Key=Key; if (!Resolve(Key,U.Id)) return false;
            U.bInitialized=Self->InitializedUnits.Contains(U.Id); Selected.Add(U.Id);
            if (const auto* State=Self->States.Find(U.Id))
            {
                U.bPresent=true; U.State=*State;
                U.State.NextAutomaticTime=FMath::Max(0.,State->NextAutomaticTime-Self->GetTaskSeconds());
                if (State->Owner.IsValid()) U.Owner=State->Owner->GetPlayerGuid();
                for (auto& C : U.State.Queue) Collect(C);
                if (U.State.Active) Collect(U.State.Active->Command);
                if (U.State.PendingMove) Collect(U.State.PendingMove->Command);
            }
            V.Units.Add(MoveTemp(U));
        }
        for (const auto& R : Self->BehaviorRequests) if (Selected.Contains(R.Unit)) V.Requests.Add({R.Unit,R.Step,R.Version,R.Token});
        FGuLiHybridStateArchive A(MaximumBytes); Snapshot(A,V); return A.Finish(Bytes);
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local) const override
    {
        using namespace GuLiHybridTasks;
        TSet<FGuLiHybridObjectKey> Keys;
        for (const auto& P : Pieces)
        {
            FSnapshot V;
            if (P.Name!=SystemName || P.Version!=Version() || !Decode(P.Bytes,V) || V.Units.Num()!=P.Objects.Num()) return false;
            TSet<FGuLiHybridObjectKey> Claims(P.Objects);
            for (const auto& U : V.Units)
            {
                FGuLiTaskUnitId Id;
                if (!Claims.Contains(U.Key) || Keys.Contains(U.Key) || !Resolve(U.Key,Id) || !(Id==U.Id)) return false;
                Keys.Add(U.Key);
            }
        }
        for (const auto& K : Local) if (!Keys.Contains(K)) return false;
        return true;
    }


    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local,EGuLiHybridRestoreStage Stage) override
    {
        using namespace GuLiHybridTasks;
        auto* Self=Tasks.Get(); if (!Self) return false;
        TSet<FGuLiTaskUnitId> Scope;
        TMap<FGuLiTaskUnitId,FGuLiHybridObjectKey> ObjectKeys;
        const TSet<FGuLiHybridObjectKey> SelectedKeys(Local);
        // Object adapters detach their Actors before shared systems. Use the
        // validated identity in the fragment, never a transient Actor lookup.
        for (const auto& P:Pieces)
        {
            FSnapshot V;if (!Decode(P.Bytes,V)) return false;
            for (const auto& U:V.Units) if (SelectedKeys.Contains(U.Key)) {Scope.Add(U.Id);ObjectKeys.Add(U.Id,U.Key);}
        }
        if (Scope.Num()!=SelectedKeys.Num()) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach)
        {
            for (const auto& Id:Scope) {Self->States.Remove(Id);Self->InitializedUnits.Remove(Id);Self->AdmissionQueued.Remove(Id);}
            Self->BehaviorRequests.RemoveAll([&](const auto& R){return Scope.Contains(R.Unit);});
            Self->ReadyAdmissions.RemoveAll([&](const auto& Id){return Scope.Contains(Id);});
            for (auto& Batch:Self->Planning)
                for (const auto& Id:Scope) {Batch.Versions.Remove(Id);Batch.Replacements.Remove(Id);}
            Self->Planning.RemoveAll([](const auto& Batch){return Batch.Versions.IsEmpty();});
            for (auto& [Player,Queue]:Self->AdmissionQueues)
            {
                Queue.Units.RemoveAt(0,Queue.Cursor,EAllowShrinking::No);Queue.Cursor=0;
                Queue.Units.RemoveAll([&](const auto& Id){return Scope.Contains(Id);});
            }
            return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Create)
        {
            TSet<FGuLiHybridObjectKey> Selected(Local);
            for (const auto& P : Pieces)
            {
                FSnapshot V; if (!Decode(P.Bytes,V)) return false;
                Self->NextExecutionId=FMath::Max(Self->NextExecutionId,V.NextExecution);

                TSet<FGuLiTaskUnitId> Units;
                for (auto& U : V.Units) if (Selected.Contains(U.Key))
                {
                    Units.Add(U.Id); if (U.bInitialized) Self->InitializedUnits.Add(U.Id);
                    if (U.bPresent)
                    {
                        auto* Battle=Self->GetWorld()->GetGameState<AGuLiBattleGameState>();
                        for (APlayerState* Entry : Battle->PlayerArray)
                            if (auto* PS=Cast<AGuLiBattlePlayerState>(Entry); PS && PS->GetPlayerGuid()==U.Owner) { U.State.Owner=PS; break; }
                        U.State.NextAutomaticTime+=Self->GetTaskSeconds();
                        U.State.LastBehaviorRequestRound=U.State.LastBehaviorRequestRound==V.Round?Self->BehaviorRound:0;
                        U.State.LastOperationRequestRound=U.State.LastOperationRequestRound==V.Round?Self->BehaviorRound:0;
                        Self->States.Add(U.Id,MoveTemp(U.State));
                    }
                }
                for (const auto& R : V.Requests) if (Units.Contains(R.Unit)) Self->BehaviorRequests.Add({R.Unit,R.Step,R.Version,R.Token});
            }
            return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Self->GetWorld());
            const ANavigationData* Data=Nav?Nav->GetNavDataForAgentName(TEXT("CommanderSoldier")):nullptr;
            if (!Nav || !Data) return false;
            TSet<FGuLiSharedMoveIntent*> Linked;
            auto Link=[&](const FGuLiUnitTaskCommand& C)->bool
            {
                auto P=C.SharedMoveIntent; if (!P || Linked.Contains(P.Get())) return true; Linked.Add(P.Get());
                auto Project=[&](FNavLocation& N)
                {
                    FNavLocation New; if (!Nav->ProjectPointToNavigation(N.Location,New,FVector(150,150,5000),Data)
                        || FVector::DistSquared2D(New.Location,N.Location)>FMath::Square(150.)) return false;
                    N=New; return true;
                };
                for (auto& N : P->Points) if (!Project(N)) return false;
                for (auto& [Id,N] : P->AssignedPoints) if (!Project(N)) return false;
                P->CoverageGeneration=0; return true;
            };
            for (auto& [Id,S] : Self->States)
            {
                if (!Scope.Contains(Id)) continue;
                if (Id.bActor)
                {
                    auto* H=Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
                    // Ordinals are local to an object family. Ground, Ship and
                    // task Actors can share one; only the captured stable key binds this task.
                    auto* Pawn=Cast<APawn>(H->FindBoundActor(ObjectKeys.FindChecked(Id)));
                    if (!Pawn) return false; S.Context.Pawn=Pawn;
                    const auto* D=Self->GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>()->FindDefinition(S.Context.UnitTypeId);
                    if (!D || !D->StateTreeAsset) return false;
                    auto* Tree=Pawn->FindComponentByClass<UGuLiCommanderStateTreeComponent>();
                    if (!Tree) {Tree=NewObject<UGuLiCommanderStateTreeComponent>(Pawn); Pawn->AddInstanceComponent(Tree); Tree->RegisterComponent();}
                    Tree->Configure(Id,*D->StateTreeAsset); S.ActorTree=Tree;
                }
                if (!Self->RefreshContext(S)) continue; // A retired entity has no navigation to rebuild.
                for (const auto& C : S.Queue) if (!Link(C)) return false;
                if (S.Active && !Link(S.Active->Command)) return false;
                if (S.PendingMove && !Link(S.PendingMove->Command)) return false;
            }
        }
        if (Stage==EGuLiHybridRestoreStage::Activate)
            for (auto& [Id,S] : Self->States)
            {
                if (!Scope.Contains(Id)) continue;
                // Planning caches hold UE handles. Rebuild from the captured task/slots.
                auto* Move=S.PendingMove ? &*S.PendingMove : S.Active ? &*S.Active : nullptr;
                if (Move && Move->bPlanning) { Move->bPlanning=false; Move->bStarted=false; Self->QueueMoveAdmission(Id); }
            }
        return true;
    }
};
void RegisterGuLiHybridTaskState(UGuLiUnitTaskSubsystem& Tasks)
{
    auto* Hybrid=Tasks.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Hybrid || !Hybrid->RegisterSharedStateSystem(GuLiHybridTasks::SystemName,MakeShared<FGuLiHybridTaskState>(Tasks)))
        if (Hybrid) Hybrid->Suspend(TEXT("Commander task checkpoint registration failed"));
}
