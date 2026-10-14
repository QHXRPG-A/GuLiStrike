#include "Development/GuLiHybridReviewSubsystem.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

void UGuLiHybridReviewSubsystem::AppendResourceEvidence(const TSharedRef<FJsonObject>& Json) const
{
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
    const auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
    TArray<TSharedPtr<FJsonValue>> Miners,Factories;
    for (TActorIterator<AGuLiMiningVehiclePawn> It(GetWorld());It;++It)
    {
        auto V=MakeShared<FJsonObject>();const auto* B=UGuLiHybridActorComponent::Find(*It);
        V->SetStringField(TEXT("stable_id"),B?B->GetObjectKey().StableId.ToString(EGuidFormats::Digits):It->GetName());
        V->SetNumberField(TEXT("ordinal"),It->GetStableActorId().Value);V->SetNumberField(TEXT("team"),uint8(It->GetTeam()));
        V->SetNumberField(TEXT("generation"),B?B->GetObjectKey().LifeGeneration:1);V->SetNumberField(TEXT("type"),It->GetUnitTypeId());
        V->SetBoolField(TEXT("local_simulation"),B && H?H->HasSimulation(B->GetObjectKey(),true):It->HasAuthority());
        V->SetBoolField(TEXT("advancing"),B?B->CanSimulate():It->HasAuthority());
        V->SetStringField(TEXT("simulator"),B?B->GetState().SimulationOwner.ToString(EGuidFormats::Digits):TEXT(""));
        V->SetStringField(TEXT("pending_event"),It->GetPendingResourceEvent().ToString(EGuidFormats::Digits));
        V->SetNumberField(TEXT("resource_revision"),double(It->GetAppliedResourceRevision()));
        V->SetNumberField(TEXT("work_seconds"),UGuLiHybridActorComponent::BattleSeconds(*It));
        TArray<TSharedPtr<FJsonValue>> XYZ;const auto Position=It->GetActorLocation();
        for (auto Number:{Position.X,Position.Y,Position.Z}) XYZ.Add(MakeShared<FJsonValueNumber>(Number));V->SetArrayField(TEXT("position"),XYZ);
        V->SetNumberField(TEXT("velocity_cm_s"),It->GetVelocity().Size());V->SetNumberField(TEXT("cargo_blue"),It->GetCargo().Blue);V->SetNumberField(TEXT("cargo_red"),It->GetCargo().Red);
        V->SetNumberField(TEXT("task_state"),uint8(It->GetTaskState()));V->SetNumberField(TEXT("control_mode"),uint8(It->GetControlMode()));
        V->SetNumberField(TEXT("behavior_result"),uint8(It->GetBehaviorResult()));V->SetNumberField(TEXT("cluster"),It->GetTargetClusterId());
        V->SetNumberField(TEXT("node"),It->GetMiningNodeId());V->SetBoolField(TEXT("mining"),It->IsMiningLaserActive());
        const auto* Travel=It->FindComponentByClass<UGuLiEngineeringTravelComponent>();V->SetStringField(TEXT("travel"),Travel->GetTravelDebug());
        FGuLiHybridMiningLease Lease;if (B && H && H->GetMiningLease(B->GetObjectKey(),Lease))
        {
            auto L=MakeShared<FJsonObject>();L->SetNumberField(TEXT("cluster"),Lease.ClusterId);L->SetNumberField(TEXT("sample"),Lease.Sample);
            L->SetNumberField(TEXT("task"),double(Lease.Task));L->SetNumberField(TEXT("node"),Lease.NodeId);L->SetStringField(TEXT("token"),Lease.Token.ToString(EGuidFormats::Digits));V->SetObjectField(TEXT("lease"),L);
        }
        const auto* State=Tasks?Tasks->FindState(FGuLiTaskUnitId::Actor(It->GetStableActorId(),GetWorld())):nullptr;
        V->SetNumberField(TEXT("active_command"),State && State->Active.IsSet()?State->Active->Command.CommandId:0);
        V->SetBoolField(TEXT("task_started"),State && State->Active.IsSet() && State->Active->bStarted);
        V->SetBoolField(TEXT("managed"),It->HasManagedTaskOwner());V->SetBoolField(TEXT("work_complete"),It->IsManagedTaskComplete());
        V->SetNumberField(TEXT("behavior_facts"),State?Tasks->GetBehaviorFacts(State->Context.Unit):0);
        V->SetNumberField(TEXT("task_version"),State?double(State->Version):0);V->SetStringField(TEXT("task_error"),State?State->Error:TEXT(""));
        Miners.Add(MakeShared<FJsonValueObject>(V));
    }
    for (TActorIterator<AGuLiResourceFactoryActor> It(GetWorld());It;++It)
    {
        auto V=MakeShared<FJsonObject>();const auto* B=UGuLiHybridActorComponent::Find(*It);
        const auto Key=Resources->GetSettlementKey(*It);
        V->SetStringField(TEXT("stable_id"),Key.IsValid()?Key.StableId.ToString(EGuidFormats::Digits):It->GetName());
        V->SetNumberField(TEXT("generation"),Key.LifeGeneration);
        V->SetNumberField(TEXT("ordinal"),It->GetStableActorId().Value);V->SetNumberField(TEXT("team"),uint8(It->GetTeam()));
        V->SetBoolField(TEXT("local_simulation"),B && H?H->HasSimulation(Key,true):It->HasAuthority());
        V->SetNumberField(TEXT("building_ordinal"),It->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->GetState().InstanceId);
        V->SetNumberField(TEXT("definition"),It->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->GetState().DefinitionId);
        V->SetBoolField(TEXT("advancing"),B?B->CanSimulate():It->HasAuthority());
        V->SetStringField(TEXT("pending_event"),It->GetPendingResourceEvent().ToString(EGuidFormats::Digits));
        V->SetNumberField(TEXT("resource_revision"),double(It->GetAppliedResourceRevision()));V->SetNumberField(TEXT("queued_raw"),It->GetQueuedRawAmount());
        TArray<TSharedPtr<FJsonValue>> Queue;
        for (const auto& E:It->GetDeliveryQueue())
        {
            auto Q=MakeShared<FJsonObject>();Q->SetNumberField(TEXT("type"),uint8(E.ResourceType));Q->SetNumberField(TEXT("amount"),E.Amount);
            Q->SetNumberField(TEXT("beneficiary"),uint8(E.SettlementTeam));Q->SetStringField(TEXT("delivery"),E.DeliveryEvent.ToString(EGuidFormats::Digits));
            Q->SetStringField(TEXT("source"),E.Source.StableId.ToString(EGuidFormats::Digits));Queue.Add(MakeShared<FJsonValueObject>(Q));
        }
        V->SetArrayField(TEXT("queue"),Queue);Factories.Add(MakeShared<FJsonValueObject>(V));
    }
    Json->SetBoolField(TEXT("mining_enabled"),IsAutonomousMiningEnabled());Json->SetArrayField(TEXT("miner_states"),Miners);Json->SetArrayField(TEXT("factory_states"),Factories);
    Json->SetBoolField(TEXT("mining_ready"),Resources && Resources->IsMiningRuntimeReady());
    if (const auto* Manager=GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()) Json->SetStringField(TEXT("mining_slots"),Manager->GetSlotDebug());
    TArray<TSharedPtr<FJsonValue>> Clusters;
    if (const auto* Map=Resources?Resources->GetMapDefinition():nullptr) for (const auto& C:Map->Clusters)
    {
        auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("id"),C.ClusterId);
        V->SetNumberField(TEXT("remaining"),Resources->GetClusterRemainingRaw(C.ClusterId));
        V->SetBoolField(TEXT("red_allowed"),Resources->CanTeamMineAt(EGuLiTeam::Red,C.ClusterId));
        V->SetBoolField(TEXT("blue_allowed"),Resources->CanTeamMineAt(EGuLiTeam::Blue,C.ClusterId));
        Clusters.Add(MakeShared<FJsonValueObject>(V));
    }
    Json->SetArrayField(TEXT("mining_clusters"),Clusters);
}
