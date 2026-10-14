#include "Development/GuLiHybridReviewSubsystem.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UGuLiHybridReviewSubsystem::AdvanceEquipmentReview()
{
    if (bStandby || Phase!=TEXT("ship_equipment") || EquipmentDone) return;
    auto* PC=GetWorld()->GetFirstPlayerController();auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Player || Player->GetBattleRole()!=EGuLiCommanderRole::Air || !H || !H->IsWorldReady() || H->IsBattlePaused()) return;
    auto* Ship=Cast<AGuLiStrikeShip>(PC->GetPawn());auto* Assembly=Ship?Ship->GetShipAssembly():nullptr;
    auto* Build=Player->FindComponentByClass<UGuLiShipBuildComponent>();
    if (!Ship || !Assembly || !Assembly->Catalog || !Build) {Failure=TEXT("Air equipment review is missing its production assembly");return;}
    if (!EquipmentRequest.IsValid())
    {
        for (const auto& Choice:Build->QueryChoices()) if (Choice.bAvailable)
        {
            const auto* Node=Assembly->Catalog->Upgrades.FindByPredicate([&](const auto& N){return N.NodeId==Choice.NodeId;});
            const auto* Group=Node?Assembly->Catalog->Groups.FindByPredicate([&](const auto& G){return G.bExecutable && G.ConfigurationId==Node->GroupConfigurationId;}):nullptr;
            if (!Group || !Group->Capabilities.ContainsByPredicate([](const auto& C)
                {return C.ComponentClass && C.ComponentClass->IsChildOf(UGuLiShipHangarCapabilityComponent::StaticClass());})) continue;
            EquipmentNode=Choice.NodeId;EquipmentRequest=FGuid::NewGuid();
            const auto& State=Build->GetBuildState();
            if (Build->RequestConfirmedChoice(EquipmentRequest,EquipmentNode,State.MatchEpoch,State.BuildRevision)!=EquipmentRequest)
                Failure=TEXT("Production equipment choice rejected before admission");
            return;
        }
        Failure=TEXT("No executable Hangar choice is available from the production catalog");return;
    }
    FGuLiShipChoiceResult Result;if (!Build->FindChoiceResult(EquipmentRequest,Result)) return;
    EquipmentDone=true;EquipmentCommitted=Result.bCommitted;EquipmentBuildRevision=Result.BuildRevision;
    if (!EquipmentCommitted) Failure=TEXT("Shared ledger rejected the production equipment choice: ")+Result.Reason;
}

void UGuLiHybridReviewSubsystem::AppendWingmanEvidence(const TSharedRef<FJsonObject>& Json) const
{
    Json->SetBoolField(TEXT("ship_equipment_complete"),EquipmentDone);
    Json->SetBoolField(TEXT("ship_equipment_committed"),EquipmentCommitted);
    Json->SetStringField(TEXT("ship_equipment_event"),EquipmentRequest.ToString(EGuidFormats::Digits));
    Json->SetStringField(TEXT("ship_equipment_node"),EquipmentNode.ToString());
    Json->SetNumberField(TEXT("ship_equipment_revision"),double(EquipmentBuildRevision));
    TArray<TSharedPtr<FJsonValue>> Hangars;
    for (TActorIterator<AGuLiStrikeShip> It(GetWorld());It;++It)
    {
        TArray<UGuLiShipHangarCapabilityComponent*> Capabilities;It->GetComponents(Capabilities);
        for (const auto* C:Capabilities)
        {
            auto V=MakeShared<FJsonObject>();const auto* Binding=UGuLiHybridActorComponent::Find(*It);
            V->SetStringField(TEXT("ship"),Binding?Binding->GetObjectKey().StableId.ToString(EGuidFormats::Digits):It->GetName());
            V->SetBoolField(TEXT("enabled"),C->IsCapabilityEnabled());V->SetStringField(TEXT("member_status"),C->GetHybridMemberStatus());
            V->SetNumberField(TEXT("advance_tick"),double(C->GetHybridAdvanceTick()));V->SetNumberField(TEXT("pending"),C->GetHybridPendingMemberCount());
            Hangars.Add(MakeShared<FJsonValueObject>(V));
        }
    }
    Json->SetArrayField(TEXT("hangar_states"),Hangars);
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H) return;
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);TArray<TSharedPtr<FJsonValue>> Members;
    for (const auto& O:Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Wingman)
    {
        auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("stable_id"),O.Key.StableId.ToString(EGuidFormats::Digits));
        V->SetStringField(TEXT("parent"),O.Parent.StableId.ToString(EGuidFormats::Digits));V->SetNumberField(TEXT("life"),O.Key.LifeGeneration);
        V->SetNumberField(TEXT("life_state"),O.LifeState);V->SetStringField(TEXT("team"),O.Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"));
        V->SetStringField(TEXT("controller"),O.Controller.ToString(EGuidFormats::Digits));
        V->SetStringField(TEXT("simulator"),O.SimulationOwner.ToString(EGuidFormats::Digits));
        V->SetNumberField(TEXT("owner_version"),double(O.OwnerEpoch));V->SetNumberField(TEXT("control_version"),double(O.ControlEpoch));
        V->SetBoolField(TEXT("local_simulation"),H->HasSimulation(O.Key));
        V->SetBoolField(TEXT("dormant"),O.bDormant);V->SetNumberField(TEXT("health"),O.Health.Health);
        const auto* P=Cast<AGuLiWingmanPawn>(H->FindBoundActor(O.Key));V->SetBoolField(TEXT("entity"),P!=nullptr);
        if (P)
        {
            const auto& A=P->GetHybridActivity();const auto Pose=P->GetActorLocation();TArray<TSharedPtr<FJsonValue>> XYZ;
            for (auto N:{Pose.X,Pose.Y,Pose.Z}) XYZ.Add(MakeShared<FJsonValueNumber>(N));V->SetArrayField(TEXT("position"),XYZ);
            V->SetNumberField(TEXT("simulation_samples"),double(A.SimulationSamples));V->SetNumberField(TEXT("published"),double(A.Published));
            V->SetNumberField(TEXT("received"),double(A.Received));V->SetNumberField(TEXT("first_publish_tick"),double(A.FirstPublishTick));
            V->SetNumberField(TEXT("last_publish_tick"),double(A.LastPublishTick));V->SetNumberField(TEXT("pose_tick"),double(A.PoseTick));
            V->SetNumberField(TEXT("cadence_errors"),A.CadenceErrors);V->SetNumberField(TEXT("restores"),A.Restores);
        }
        Members.Add(MakeShared<FJsonValueObject>(V));
    }
    Json->SetArrayField(TEXT("wingman_states"),Members);
}
