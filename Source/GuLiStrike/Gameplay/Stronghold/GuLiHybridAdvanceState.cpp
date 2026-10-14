#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"

class FGuLiHybridAdvanceState final : public IGuLiHybridSharedStateAdapter
{
    using FGroup=UGuLiArmyAdvanceSubsystem::FAdvanceGroup;
    struct FMember { FGuLiHybridObjectKey Key; uint32 Id=0; };
    struct FSnapshot
    {
        TArray<FMember> Members;
        TArray<FGroup> Groups;
        TArray<EGuLiTeam> Owners;
        uint32 Cursor=0;
        int32 Queries=4;
        uint64 Tick=0;
    };
    TWeakObjectPtr<UGuLiArmyAdvanceSubsystem> Owner;
    static constexpr uint32 Schema=2,Limit=4*1024*1024;
    static void Encode(FGuLiHybridStateArchive& A,FSnapshot& S)
    {
        uint32 V=Schema; A.Value(V); if (V!=Schema) A.Reject();
        A.Value(S.Cursor); A.Value(S.Queries); A.Value(S.Tick);
        if (S.Queries<0 || S.Queries>4) A.Reject();
        A.Array(S.Members,10000,[](auto& B,auto& M){B.Value(M.Key);B.Value(M.Id);if (!M.Key.IsValid() || !M.Id) B.Reject();});
        A.Array(S.Owners,4096,[](auto& B,auto& T){B.Enum(T,uint8(EGuLiTeam::Blue));});
        A.Array(S.Groups,10000,[](auto& B,FGroup& G)
        {
            B.Enum(G.Team,uint8(EGuLiTeam::Blue));B.Value(G.Source);B.Value(G.Target);
            B.Array(G.Soldiers,10000,[](auto& C,auto& Id){C.Value(Id.Value);if (!Id.IsValid()) C.Reject();});
            TArray<int32> Rejected;if (!B.IsReading()) {Rejected=G.Unreachable.Array();Rejected.Sort();}
            B.Array(Rejected,4096,[](auto& C,auto& Id){C.Value(Id);if (Id<0) C.Reject();});
            if (B.IsReading()) G.Unreachable=TSet<int32>(Rejected);
            B.Value(G.NextDecisionTime);B.Enum(G.Phase,uint8(EGuLiCommanderWorkPhase::ConstructionCancelled));
            B.Enum(G.Result,uint8(EGuLiCommanderWorkResult::FactoryUnreachable));B.Value(G.Center);B.Value(G.Approach);
            B.Value(G.PendingPlan);B.Value(G.PendingPlanEpoch);B.Value(G.bPlanCommitted);B.Value(G.bMoving);B.Value(G.bLocked);
            if (G.Team==EGuLiTeam::Unassigned || G.Soldiers.IsEmpty() || G.NextDecisionTime<0
                || G.Source<INDEX_NONE || G.Target<INDEX_NONE || (G.PendingPlan && !G.PendingPlanEpoch)
                || G.Phase<EGuLiCommanderWorkPhase::AdvanceSelecting || G.Phase>EGuLiCommanderWorkPhase::AdvanceWaiting) B.Reject();
        });
    }
    static bool Decode(TConstArrayView<uint8> Bytes,FSnapshot& S)
    {
        FGuLiHybridStateArchive A(Bytes,Limit);Encode(A,S);if (!A.Done()) return false;
        TSet<uint32> Members,Grouped;TSet<FGuLiHybridObjectKey> Keys;
        for (const auto& M:S.Members)
        {if (Members.Contains(M.Id) || Keys.Contains(M.Key)) return false;Members.Add(M.Id);Keys.Add(M.Key);}
        if (S.Groups.IsEmpty()?S.Cursor!=0:S.Cursor>=uint32(S.Groups.Num())) return false;
        for (const auto& G:S.Groups)
        {
            if (G.Source>=S.Owners.Num() || G.Target>=S.Owners.Num()) return false;
            for (int32 Index:G.Unreachable) if (Index>=S.Owners.Num()) return false;
            for (auto Id:G.Soldiers)
            {if (!Members.Contains(Id.Value) || Grouped.Contains(Id.Value)) return false;Grouped.Add(Id.Value);}
        }
        return true;
    }
    UGuLiHybridWorldSubsystem* Hybrid() const {return Owner.IsValid()?Owner->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;}
public:
    explicit FGuLiHybridAdvanceState(UGuLiArmyAdvanceSubsystem& V):Owner(&V) {}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return Schema;}


    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    {
        auto* Self=Owner.Get();auto* H=Hybrid();if (!Self || !H) return false;
        FSnapshot S;S.Cursor=Self->DecisionCursor;S.Queries=Self->RemainingMoveQueries;S.Owners=Self->ObservedOwners;S.Tick=Self->LastHybridTick;
        TSet<uint32> Selected;
        for (const auto& K:Objects)
        {FGuLiHybridObjectState O;if (K.Kind!=EGuLiHybridObjectKind::Soldier || !H->GetObject(K,O) || !O.Ordinal) return false;S.Members.Add({K,O.Ordinal});Selected.Add(O.Ordinal);}
        // Rotate the filtered snapshot so the next decision survives a change
        // in local ownership without an index into a discarded group array.
        S.Cursor=0;
        for (int32 Index=0;Index<Self->Groups.Num();++Index)
        {
            auto G=Self->Groups[(Self->DecisionCursor+Index)%Self->Groups.Num()];
            G.NextDecisionTime=FMath::Max(0.,G.NextDecisionTime-H->GetLogicalSeconds());
            G.Soldiers.RemoveAll([&](auto Id){return !Selected.Contains(Id.Value);});
            if (!G.Soldiers.IsEmpty()) S.Groups.Add(MoveTemp(G));
        }
        FGuLiHybridStateArchive A(Limit);Encode(A,S);return A.Finish(Bytes);
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local) const override
    {
        auto* H=Hybrid();if (!H) return false;TSet<FGuLiHybridObjectKey> Claims;TSet<uint32> Ids;
        for (const auto& P:Pieces)
        {
            FSnapshot S;if (P.Name!=TEXT("ArmyAdvance") || P.Version!=Schema || !Decode(P.Bytes,S) || S.Members.Num()!=P.Objects.Num()) return false;
            for (const auto& M:S.Members)
            {
                FGuLiHybridObjectState O;
                if (!P.Objects.Contains(M.Key) || Claims.Contains(M.Key) || Ids.Contains(M.Id) || !H->GetObject(M.Key,O) || O.Ordinal!=M.Id) return false;
                Claims.Add(M.Key);Ids.Add(M.Id);
            }
        }
        for (const auto& K:Local) if (!Claims.Contains(K)) return false;return true;
    }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local,EGuLiHybridRestoreStage Stage) override
    {
        auto* Self=Owner.Get();auto* H=Hybrid();if (!Self || !H) return false;
        TSet<uint32> Selected;for (const auto& K:Local) {FGuLiHybridObjectState O;if (!H->GetObject(K,O)) return false;Selected.Add(O.Ordinal);}
        if (Stage==EGuLiHybridRestoreStage::Detach)
        {
            for (auto& G:Self->Groups) G.Soldiers.RemoveAll([&](auto Id){return Selected.Contains(Id.Value);});
            Self->Groups.RemoveAll([](const auto& G){return G.Soldiers.IsEmpty();});
            Self->DecisionCursor=Self->Groups.IsEmpty()?0:Self->DecisionCursor%Self->Groups.Num();return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Create)
        {
            for (const auto& P:Pieces)
            {
                FSnapshot S;if (!Decode(P.Bytes,S)) return false;
                for (int32 Index=0;Index<S.Groups.Num();++Index)
                {
                    auto G=S.Groups[(S.Cursor+Index)%S.Groups.Num()];
                    G.Soldiers.RemoveAll([&](auto Id){return !Selected.Contains(Id.Value);});
                    G.NextDecisionTime+=H->GetLogicalSeconds();G.PendingPlan=0;G.PendingPlanEpoch=0;G.bPlanCommitted=false;
                    if (!G.Soldiers.IsEmpty()) Self->Groups.Add(MoveTemp(G));
                }
            }
        }
        return true;
    }
};
void RegisterGuLiHybridAdvanceState(UGuLiArmyAdvanceSubsystem& Self)
{
    auto* H=Self.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (H && !H->RegisterSharedStateSystem(TEXT("ArmyAdvance"),MakeShared<FGuLiHybridAdvanceState>(Self)))
        H->Suspend(TEXT("Automatic army advance checkpoint registration failed"));
}
