#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Gameplay/Building/GuLiHybridPlacementWork.h"
#include "Gameplay/Skills/GuLiHybridArmy.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridCommandRegistry.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace GuLiHybridPlacement
{
bool Available(const UWorld* World,int32 Definition)
{
    const auto* Owner=World?World->GetSubsystem<UGuLiHybridActorWorldSubsystem>():nullptr;
    const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();const auto* D=Catalog?Catalog->FindById(Definition):nullptr;
    return Owner && D && !GetGuLiHybridBuildingFamily(*D).IsNone()
        && Owner->GetManifest().Strings.FindRef(FString::Printf(TEXT("adapter/definition/building/%d"),Definition))==GetGuLiHybridBuildingFamily(*D).ToString();
}
void Intent(FGuLiHybridStateArchive& A,FGuLiBuildingPlacementRequest& R)
{
    uint32 Version=2; A.Value(Version); if (Version!=2) A.Reject();
    A.Value(R.ClientRequestId); A.Value(R.DefinitionId); FVector Location=R.DesiredGroundLocation;
    A.Value(Location); A.Value(R.CompressedYaw); if (A.IsReading()) R.DesiredGroundLocation=Location;
    if (!R.IsWellFormed() || R.DefinitionId<=0) A.Reject();
}
template<class T> void Pending(FGuLiHybridStateArchive& A,TArray<T>& Jobs)
{
    uint32 Version=2; A.Value(Version); if (Version!=2) A.Reject();
    TSet<FGuid> Commands,Events;
    A.Array(Jobs,64,[&](auto& Ar,auto& J)
    {
        Ar.Value(J.Command);Ar.Value(J.AdmitEvent);Ar.Value(J.Source);Ar.Value(J.Work);Ar.Value(J.StartedAt);
        Ar.Value(J.Definition);Ar.Value(J.Ground);Ar.Value(J.Yaw);Ar.Value(J.bSubmitted);
        if (!J.Command.IsValid() || !J.AdmitEvent.IsValid() || !J.Source.IsValid()
            || J.Source.Kind!=EGuLiHybridObjectKind::GroundPlayer || !J.Work.IsValid()
            || J.Work.Kind!=EGuLiHybridObjectKind::BusinessWork || J.Work.StableId!=J.Command
            || J.Definition<1 || !J.bSubmitted || Commands.Contains(J.Command) || Events.Contains(J.AdmitEvent)) Ar.Reject();
        Commands.Add(J.Command);Events.Add(J.AdmitEvent);
    });
}
}

bool UGuLiBuildingPlacementComponent::RouteHybridPlacement(const FGuLiBuildingPlacementRequest& Request)
{
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); const auto* Pawn=GetPlacementPawn();
    const auto* Binding=UGuLiHybridActorComponent::Find(Pawn);
    const auto* Player=GetPlacementPlayer();FGuLiHybridObjectState Source;
    if (!Hybrid || Hybrid->IsBattlePaused() || !Hybrid->CanPublishFacts() || !Request.IsWellFormed()
        || !Player || !Player->IsBattleReady()) return false;
    if (Player->IsCommander())
    {if (!GuLiHybridArmy::FindRoot(*GetWorld(),Player->GetTeam(),Source)) return false;}
    else if (Player->GetBattleRole()==EGuLiCommanderRole::Ground && Binding && Binding->GetObjectKey().Kind==EGuLiHybridObjectKind::GroundPlayer)
        Source=Binding->GetState();
    else return false;
    if (!Hybrid->HasControl(Source.Key)) return false;
    const auto* D=ResolveCatalog()?(Request.DefinitionId>0?CachedCatalog->FindById(Request.DefinitionId):CachedCatalog->FindDefinition(Request.Type)):nullptr;
    if (!D || !GuLiHybridPlacement::Available(GetWorld(),D->DefinitionId)) { EmitRejectedFeedback(EGuLiBuildingPlacementRejectReason::AssetUnavailable); return false; }
    FGuLiHybridCommand Command; Command.Event=FGuid::NewGuid(); Command.LogicalTick=Hybrid->GetLogicalTick(); Command.Operation=TEXT("BuildingPlace");
    Command.Members.Add({Source.Key,Source.ControlEpoch,Source.OwnerEpoch,0,FVector::ZeroVector});
    FGuLiHybridStateArchive A(128); auto R=Request;R.DefinitionId=D->DefinitionId;GuLiHybridPlacement::Intent(A,R);
    if (!A.Finish(Command.Arguments) || !Hybrid->RouteCommand(Command)) return false;
    HybridClientRequests.Add(GuLiHybridPlacementEvent(*GetWorld(),Command.Event,TEXT("reserve")),Request.ClientRequestId);
    HybridClientRequests.Add(GuLiHybridPlacementEvent(*GetWorld(),Command.Event,TEXT("spawn")),Request.ClientRequestId);
    HybridClientRequests.Add(GuLiHybridPlacementEvent(*GetWorld(),Command.Event,TEXT("admit")),Request.ClientRequestId);
    const auto Refund=GuLiHybridPlacementEvent(*GetWorld(),Command.Event,TEXT("refund"));
    HybridClientRequests.Add(Refund,Request.ClientRequestId);HybridRefundEvents.Add(Refund);
    HybridCommandRequests.Add(Command.Event,Request.ClientRequestId);
    return true;
}
void UGuLiBuildingPlacementComponent::HandleHybridPlacementCommand(const FGuLiHybridCommandResult& Result)
{
    const auto* ClientId=HybridCommandRequests.Find(Result.Event); if (!ClientId) return;
    const uint32 Id=*ClientId; HybridCommandRequests.Remove(Result.Event);
    if (!Result.Members.IsEmpty() && Result.Members.ContainsByPredicate([](const auto& M){return M.bAccepted;})) return;
    FGuLiBuildingPlacementResult Receipt; Receipt.ClientRequestId=Id; Receipt.RejectReason=EGuLiBuildingPlacementRejectReason::SpawnFailed;
    if (auto* PC=GetOwningPlayerController(); PC && PC->IsLocalController()) ApplyPlacementResult(Receipt);
    for (auto It=HybridClientRequests.CreateIterator();It;++It) if (It.Value()==Id) { HybridRefundEvents.Remove(It.Key()); It.RemoveCurrent(); }
}
bool UGuLiBuildingPlacementComponent::ExecuteHybridPlacement(const FGuLiHybridCommand& Command,const FGuLiHybridCommandMember& Member)
{
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    if (!Hybrid || Command.Operation!=TEXT("BuildingPlace") || !Command.Event.IsValid() || !Binding || Member.Key!=Binding->GetObjectKey()
        || !UGuLiHybridActorComponent::HasSimulation(GetOwner()) || HybridPlacements.Num()>=64)
    {
        UE_LOG(LogTemp,Warning,TEXT("Hybrid placement command %s rejected before validation: binding=%d simulation=%d pending=%d"),
            *Command.Event.ToString(),Binding!=nullptr,UGuLiHybridActorComponent::HasSimulation(GetOwner()),HybridPlacements.Num());
        return false;
    }
    for (const auto& J : HybridPlacements) if (J.Command==Command.Event) return true;
    FGuLiBuildingPlacementRequest R; FGuLiHybridStateArchive A(Command.Arguments,128); GuLiHybridPlacement::Intent(A,R);
    if (!A.Done()) return false;
    ResolveCatalog(); FTransform Transform; const FGuLiBuildingDefinition* D=nullptr; AActor* Support=nullptr;
    const auto Validation=ValidatePlacementRequest(R,Transform,D,Support);
    if (Validation!=EGuLiBuildingPlacementRejectReason::None || !D || !GuLiHybridPlacement::Available(GetWorld(),D->DefinitionId))
    {
        UE_LOG(LogTemp,Warning,TEXT("Hybrid placement command %s definition=%d rejected by validation=%d at %s"),
            *Command.Event.ToString(),R.DefinitionId,int32(Validation),*FVector(R.DesiredGroundLocation).ToString());
        return false;
    }
    FHybridPlacement J;J.Command=Command.Event;J.Source=Binding->GetObjectKey();J.Definition=D->DefinitionId;
    J.AdmitEvent=GuLiHybridPlacementEvent(*GetWorld(),Command.Event,TEXT("admit"));
    J.Work.Kind=EGuLiHybridObjectKind::BusinessWork;J.Work.StableId=Command.Event;J.Work.LifeGeneration=1;
    // A routed input carries the controller's clock. Admission starts on the
    // executing object's age, which remains valid after ownership changes.
    J.StartedAt=Hybrid->GetObjectLogicalTick(J.Source);J.Ground=Transform.GetLocation()-FVector(0,0,D->CollisionExtent.Z);J.Yaw=Transform.Rotator().Yaw;
    FGuLiHybridObjectRequest Admit;Admit.Source=J.Source;Admit.Target=J.Work;Admit.Event=J.AdmitEvent;
    Admit.Rule=FName(*FString::Printf(TEXT("admit/work/place/%d"),J.Definition));Admit.Operation=EGuLiHybridObjectOperation::Spawn;
    Admit.Location=J.Ground;Admit.FacingYaw=J.Yaw;Admit.StartedAt=J.StartedAt;
    J.bSubmitted=Hybrid->SubmitObjectOperation(Admit).IsValid();
    if (!J.bSubmitted)
    {
        UE_LOG(LogTemp,Warning,TEXT("Hybrid placement command %s definition=%d failed to submit work admission"),
            *Command.Event.ToString(),J.Definition);
        return false;
    }
    HybridPlacements.Add(J);
    return true;
}
void UGuLiBuildingPlacementComponent::ApplyHybridReceipt(const FGuLiHybridTransactionResult& Result)
{
    if (Result.Status==EGuLiHybridTransactionStatus::Deferred || Result.Status==EGuLiHybridTransactionStatus::Paused) return;
    HybridPlacements.RemoveAll([&](const auto& J){return J.AdmitEvent==Result.Event;});
}
void UGuLiBuildingPlacementComponent::HandleHybridTransaction(const FGuLiHybridTransactionResult& Result)
{
    ApplyHybridReceipt(Result);
    const auto* ClientId=HybridClientRequests.Find(Result.Event); if (!ClientId) return;
    if (Result.Status==EGuLiHybridTransactionStatus::Deferred || Result.Status==EGuLiHybridTransactionStatus::Paused) return;
    const bool Refunded=HybridRefundEvents.Contains(Result.Event);
    if (Result.IsCommitted() && !Refunded && (Result.ObjectOperation!=EGuLiHybridObjectOperation::Spawn
        || Result.Target.Kind==EGuLiHybridObjectKind::BusinessWork)) return;
    FGuLiBuildingPlacementResult Placement; Placement.ClientRequestId=*ClientId;
    Placement.RejectReason=Refunded?EGuLiBuildingPlacementRejectReason::SpawnFailed:Result.IsCommitted()?EGuLiBuildingPlacementRejectReason::None
        : Result.Status==EGuLiHybridTransactionStatus::InsufficientResources?EGuLiBuildingPlacementRejectReason::InsufficientResources
        : EGuLiBuildingPlacementRejectReason::SpawnFailed;
    if (auto* PC=GetOwningPlayerController(); PC && PC->IsLocalController()) ApplyPlacementResult(Placement);
    const uint32 ID=*ClientId;
    for (auto It=HybridClientRequests.CreateIterator();It;++It) if (It.Value()==ID) {HybridRefundEvents.Remove(It.Key());It.RemoveCurrent();}
}
void UGuLiBuildingPlacementComponent::AdvanceHybridPlacement()
{
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!Hybrid) return;
    // Admission requests already belong to the world's immutable reliable
    // transaction journal. This component only observes them; the admitted
    // BusinessWork adapter owns payment, spawning and refunds.
    TArray<FGuLiHybridTransactionResult> Receipts;
    for (const auto& J:HybridPlacements)
    {
        FGuLiHybridTransactionResult R;if (Hybrid->FindTransactionResult(J.AdmitEvent,R)) Receipts.Add(R);
    }
    for (const auto& R:Receipts) ApplyHybridReceipt(R);
}
bool UGuLiBuildingPlacementComponent::CaptureHybridPlacement(TArray<uint8>& Bytes) const
{
    auto Jobs=HybridPlacements; FGuLiHybridStateArchive A(65536); GuLiHybridPlacement::Pending(A,Jobs); return A.Finish(Bytes);
}
bool UGuLiBuildingPlacementComponent::ValidateHybridPlacement(TConstArrayView<uint8> Bytes) const
{
    TArray<FHybridPlacement> Jobs; FGuLiHybridStateArchive A(Bytes,65536); GuLiHybridPlacement::Pending(A,Jobs);
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner()); if (!A.Done() || !Binding) return false;
    for (const auto& J : Jobs) if (J.Source!=Binding->GetObjectKey() || !GuLiHybridPlacement::Available(GetWorld(),J.Definition)) return false;
    return true;
}
bool UGuLiBuildingPlacementComponent::RestoreHybridPlacement(TConstArrayView<uint8> Bytes)
{
    if (!ValidateHybridPlacement(Bytes)) return false;
    FGuLiHybridStateArchive A(Bytes,65536); GuLiHybridPlacement::Pending(A,HybridPlacements); return A.Done();
}

namespace
{
// Ground uses its own simulated pawn. A Commander camera is local input only;
// the registered team authority routes its frozen placement to the responsible
// peer, where the same formal ground/clearance/capacity checks run.
const FGuLiHybridCommandRegistration PlacementCommand({TEXT("BuildingPlace"),2,
    [](UWorld& World,const FGuLiHybridObjectState& Source,const FGuLiHybridCommand& Command,const FGuLiHybridCommandMember& Member)
    {
        auto* H=World.GetSubsystem<UGuLiHybridWorldSubsystem>();
        if (!H || Member.Key!=Source.Key || !H->HasSimulation(Source.Key) || !H->CanPublishFacts())
        {
            UE_LOG(LogTemp,Warning,TEXT("Hybrid placement command %s rejected by source context: match=%d simulation=%d publishing=%d"),
                *Command.Event.ToString(),Member.Key==Source.Key,H && H->HasSimulation(Source.Key),H && H->CanPublishFacts());
            return false;
        }
        if (auto* Actor=H->FindBoundActor(Source.Key))
            if (auto* Placement=Actor->FindComponentByClass<UGuLiBuildingPlacementComponent>()) return Placement->ExecuteHybridPlacement(Command,Member);
        const auto* Actors=World.GetSubsystem<UGuLiHybridActorWorldSubsystem>();
        const auto* Session=World.GetGameInstance()?World.GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;
        if (!Actors || !Session || Source.Definition.ToString()!=Actors->GetManifest().Strings.FindRef(TEXT("army/root_definition"))
            || !Session->GetMembers().ContainsByPredicate([&](const auto& M)
            {return M.Player==Source.Controller && M.Team==Source.Team && M.Role==EGuLiCommanderRole::Commander && M.bOnline && M.bReady;}))
        {
            UE_LOG(LogTemp,Warning,TEXT("Hybrid placement command %s rejected: definition=%s has no placement component or Commander root"),
                *Command.Event.ToString(),*Source.Definition.ToString());
            return false;
        }
        FGuLiBuildingPlacementRequest Request;FGuLiHybridStateArchive A(Command.Arguments,128);GuLiHybridPlacement::Intent(A,Request);
        const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();
        const auto* D=Catalog?Catalog->FindById(Request.DefinitionId):nullptr;
        const float Yaw=GuLiBuildingPlacementPolicy::SnapYaw(Request.GetYawDegrees());
        FString Rejection;
        if (!A.Done()) Rejection=TEXT("invalid-intent");
        else if (!D || !GuLiHybridPlacement::Available(&World,D->DefinitionId)) Rejection=TEXT("definition-unavailable");
        else UGuLiBuildingPlacementComponent::ValidateAdmittedPlacement(World,*D,Request.DesiredGroundLocation,Yaw,Source.Controller,&Rejection);
        if (!Rejection.IsEmpty())
        {
            UE_LOG(LogTemp,Warning,TEXT("Hybrid placement command %s definition=%d at %s rejected: %s"),
                *Command.Event.ToString(),Request.DefinitionId,*FVector(Request.DesiredGroundLocation).ToString(),*Rejection);
            return false;
        }
        FGuLiHybridObjectRequest Admit;Admit.Source=Source.Key;Admit.Target.Kind=EGuLiHybridObjectKind::BusinessWork;
        Admit.Target.StableId=Command.Event;Admit.Target.LifeGeneration=1;Admit.Event=GuLiHybridPlacementEvent(World,Command.Event,TEXT("admit"));
        Admit.Rule=FName(*FString::Printf(TEXT("admit/work/place/%d/commander"),D->DefinitionId));
        Admit.Operation=EGuLiHybridObjectOperation::Spawn;Admit.Location=Request.DesiredGroundLocation;Admit.FacingYaw=Yaw;
        Admit.StartedAt=H->GetObjectLogicalTick(Source.Key);
        // Once admitted, the ordinary independent PlacementWork owns payment,
        // spawn and refund. No player/controller UObject callback owns the job.
        const bool Submitted=H->SubmitObjectOperation(Admit).IsValid();
        if (!Submitted) UE_LOG(LogTemp,Warning,TEXT("Hybrid placement command %s failed work admission: %s"),
            *Command.Event.ToString(),*Admit.Rule.ToString());
        return Submitted;
    }});
}
