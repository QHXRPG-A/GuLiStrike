#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Gameplay/CommanderSkills/GuLiPointSkill.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Network/GuLiHybridCommandRegistry.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Engine/World.h"

namespace
{
constexpr uint32 Version=1;
struct FSkillInput {bool HasGround=false;FVector Ground=FVector::ZeroVector;FGuid Batch;TArray<int32> Allocations;};
void SerializeSkillInput(FGuLiHybridStateArchive& A,FSkillInput& I)
{
    uint32 SerializedVersion=Version;A.Value(SerializedVersion);A.Value(I.HasGround);A.Value(I.Ground);A.Value(I.Batch);A.Array(I.Allocations,2048,[](auto& A,int32& V){A.Value(V);});
    if (SerializedVersion!=Version || !I.Batch.IsValid() || I.Allocations.IsEmpty()) A.Reject();for (auto V:I.Allocations) if (V<INDEX_NONE || V>4096) A.Reject();
}
bool Execute(UWorld& W,const FGuLiHybridObjectState& O,const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M)
{
    if (O.Key.Kind!=EGuLiHybridObjectKind::Soldier) return false;FSkillInput I;FGuLiHybridStateArchive A(C.Arguments);SerializeSkillInput(A,I);
    if (!A.Done() || !I.Allocations.IsValidIndex(int32(M.Slot))) return false;
    FVector Ground=FVector::ZeroVector;const bool Valid=I.HasGround && GuLiSkillTargeting::ResolveGround(W,I.Ground,Ground);
    auto* Authority=W.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    return Authority && Authority->ExecuteHybridUnitSkill(O.Key,C.Event,Valid,Ground,I.Allocations[M.Slot],I.Batch).Code==EGuLiActiveSkillResultCode::Succeeded;
}
const FGuLiHybridCommandRegistration UnitSkills({TEXT("UnitSkill"),Version,Execute});
}
FGuid UGuLiCommanderSkillComponent::RequestHybridUnitSkills(bool HasGround,FVector Ground)
{
    auto* PC=Commander();const auto* GS=GetWorld()->GetGameState<AGuLiBattleGameState>();
    const auto* Sync=PC?PC->GetCommanderNetSyncComponent():nullptr;
    if (!GS || !Sync) return {};
    auto Request=MakeLocalRequest(HasGround,Ground);Request.SelectionRevision=Sync->GetSelectionState().SelectionRevision;
    return RouteHybridUnitSkillRequest(Request);
}
FGuid UGuLiCommanderSkillComponent::RouteHybridUnitSkillRequest(const FGuLiActiveSkillRequest& Request)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();auto* PC=Commander();
    if (!H || !PC || !PC->IsLocalController() || !PC->CanIssueCommanderOrders() || !SynchronizeEpoch()
        || !Request.RequestId.IsValid() || Request.MatchEpoch!=RuntimeEpoch || !Request.GlobalSkillId.IsNone()
        || Request.Command!=EGuLiActiveSkillCommand::Activate || Request.CastId.IsValid() || Request.GroundPoint.ContainsNaN()) return {};
    if (const auto* Receipt=Receipts.Find(Request.RequestId))
    {
        if (!Receipt->Request.HasSameContent(Request)) return {};
        ClientReceiveReply(Receipt->Reply);return Request.RequestId;
    }
    if (const auto* Pending=HybridPending.Find(Request.RequestId)) return Pending->Request.HasSameContent(Request)?Request.RequestId:FGuid{};
    if (!H->CanPublishFacts() || HybridPending.Num()>=64 || Receipts.Num()>=4096) return {};
    auto* Sync=PC->GetCommanderNetSyncComponent();if (!Sync || Sync->HasUnresolvedSelectionIntent()) return {};
    if (Request.SelectionRevision!=Sync->GetSelectionState().SelectionRevision) return {};
    H->OnCommandCompleted.RemoveAll(this);H->OnCommandCompleted.AddUObject(this,&ThisClass::HandleHybridSkillResult);
    TSet<uint32> Selected;for (const auto& Group:Sync->GetSelectionState().Cohorts) for (auto Id:Group.MemberIds) Selected.Add(Id.Value);
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);Objects.Sort([](const auto& A,const auto& B){return A.Ordinal<B.Ordinal;});
    FGuLiHybridCommand C;C.Event=Request.RequestId;C.LogicalTick=H->GetLogicalTick();C.Operation=TEXT("UnitSkill");
    FSkillInput I;I.HasGround=Request.bHasGroundPoint;I.Ground=Request.GroundPoint;I.Batch=C.Event;
    FHybridPendingSkill Pending;Pending.Request=Request;TMap<FName,int32> Remaining;
    const auto* Army=GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
    for (const auto& O:Objects)
    {
        if (O.Key.Kind!=EGuLiHybridObjectKind::Soldier || !Selected.Contains(O.Ordinal)) continue;
        if (!H->HasControl(O.Key) || O.Team!=PlayerState().GetTeam()) return {};
        uint16 Type=0;const FString Def=O.Definition.ToString();if (!Def.StartsWith(TEXT("unit/")) || !LexTryParseString(Type,*Def.RightChop(5))) return {};
        const auto* Skill=Catalog->FindAvailableUnitSkill(*GetWorld(),O.Team,Type);if (!Skill) continue;
        int32 Allocation=INDEX_NONE;
        if (const auto* Point=Cast<UGuLiPointSkillConfiguration>(Skill->Configuration);Point && Point->MaxProjectilesPerActivation>0)
        {
            auto* Available=Remaining.Find(Skill->SkillId);if (!Available) Available=&Remaining.Add(Skill->SkillId,Point->MaxProjectilesPerActivation);
            const auto* Weapon=Army?Army->FindResolvedSkill(O.Team,Type,Point->SourceWeaponSlot):nullptr;
            const int32 Wanted=Weapon?Weapon->ProjectileCount:1;Allocation=*Available>=Wanted?Wanted:0;if (Allocation) *Available-=Wanted;
        }
        auto& Member=C.Members.AddDefaulted_GetRef();Member.Key=O.Key;Member.ControlEpoch=O.ControlEpoch;Member.OwnerEpoch=O.OwnerEpoch;Member.Slot=I.Allocations.Num();
        I.Allocations.Add(Allocation);FGuLiActiveSkillUnitResult Result;Result.SoldierId=FGuLiSoldierId(O.Ordinal);Result.SkillId=Skill->SkillId;
        Pending.Units.Add(O.Key.StableId,Result);Pending.Waiting.Add(O.Key.StableId);
    }
    if (C.Members.IsEmpty()) return {};FGuLiHybridStateArchive Archive;SerializeSkillInput(Archive,I);if (!Archive.Finish(C.Arguments)) return {};
    HybridPending.Add(C.Event,MoveTemp(Pending));if (!H->RouteCommand(C)) {HybridPending.Remove(C.Event);return {};}return C.Event;
}
void UGuLiCommanderSkillComponent::HandleHybridSkillResult(const FGuLiHybridCommandResult& R)
{
    RejectHybridGlobalCommand(R);
    auto* P=HybridPending.Find(R.Event);if (!P) return;
    for (const auto& M:R.Members) if (auto* U=P->Units.Find(M.Key.StableId))
    {U->Code=M.bAccepted?EGuLiActiveSkillResultCode::Succeeded:EGuLiActiveSkillResultCode::ExecutionFailed;U->Execution.bSucceeded=M.bAccepted;P->Waiting.Remove(M.Key.StableId);}
    if (!P->Waiting.IsEmpty()) return;FGuLiActiveSkillReply Reply;Reply.RequestId=R.Event;
    Reply.MatchEpoch=P->Request.MatchEpoch;P->Units.GenerateValueArray(Reply.Units);
    Reply.Units.Sort([](const auto& A,const auto& B){return A.SoldierId.Value<B.SoldierId.Value;});
    Reply.Code=Reply.Units.ContainsByPredicate([](const auto& U){return U.Code==EGuLiActiveSkillResultCode::Succeeded;})?EGuLiActiveSkillResultCode::Succeeded:EGuLiActiveSkillResultCode::ExecutionFailed;
    Receipts.Add(R.Event,{P->Request,Reply});HybridPending.Remove(R.Event);ClientReceiveReply(Reply);
}
void UGuLiCommanderSkillComponent::RebindHybridRequests()
{
    RebindHybridGlobals();
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H) return;
    H->OnCommandCompleted.RemoveAll(this);H->OnCommandCompleted.AddUObject(this,&ThisClass::HandleHybridSkillResult);
    TArray<FGuid> Events;HybridPending.GetKeys(Events);
    for (const auto& Event:Events)
    {
        FGuLiHybridCommandResult R;
        if (H->FindCommandResult(Event,R)) HandleHybridSkillResult(R);
        // Keep listening across ownership changes; only the final result completes the request.
    }
}
TArray<FGuid> UGuLiCommanderSkillComponent::GetPendingHybridRequests() const
{
    TArray<FGuid> Out,Globals;HybridPending.GetKeys(Out);HybridGlobalRequests.GetKeys(Globals);
    for (const auto& Event:Globals) Out.AddUnique(Event);return Out;
}
