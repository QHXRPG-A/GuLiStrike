#include "Commander/Behavior/GuLiCommanderAbilityBridge.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Engine/World.h"
#include "AIController.h"

static bool MiningValidate(UWorld& World, const FGuLiTaskUnitContext& Unit, const FGuLiUnitTaskCommand& Command, FString& Error)
{
	const auto* Resources = World.GetSubsystem<UGuLiResourceWorldSubsystem>();
	const bool bValid = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()) && Command.ClusterId && Resources && Resources->IsRuntimeReady()
		&& Resources->CanTeamMineAt(Unit.Team, Command.ClusterId) && !Resources->IsClusterEmpty(Command.ClusterId);
	if (!bValid) Error = GuLiGameText::Text(TEXT("UI.SpecialTaskExecutor.128")); return bValid;
}
static bool MiningBuildAutomatic(UWorld&, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand& Command)
{ return Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()) != nullptr; }
static EGuLiTaskStatus MiningStart(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task)
{
	auto* Miner = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()); if (!Miner) return EGuLiTaskStatus::Failed;
	FGuLiMiningCommand Command; Command.Type = EGuLiMiningOrderType::MineCluster; Command.ClusterId = Task.Command.ClusterId;
	if (!Task.bAutomatic && World.GetSubsystem<UGuLiResourceWorldSubsystem>()->IsClusterEmpty(Task.Command.ClusterId))
	{
		if (Miner->GetCargo().Blue + Miner->GetCargo().Red == 0) return EGuLiTaskStatus::Completed;
		Command.Type = EGuLiMiningOrderType::ReturnToFactory;
	}
	Command.RequestId = Task.ExecutionId; Command.SelectionRevision = 1;
	return Miner->StartManagedTask(Command, Task.bAutomatic) ? EGuLiTaskStatus::Running : EGuLiTaskStatus::Waiting;
}
static EGuLiTaskStatus MiningPoll(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task)
{
	const auto* Miner = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()); if (!Miner) return EGuLiTaskStatus::Failed;
	if (Task.bAutomatic && Task.bYieldRequested && Miner->GetTaskState() == EGuLiMiningTaskState::Idle
		&& Miner->GetCargo().Blue + Miner->GetCargo().Red == 0) return EGuLiTaskStatus::WorkUnitComplete;
	if (Task.bAutomatic && !Miner->IsManagedTaskComplete() && Miner->GetTaskState() == EGuLiMiningTaskState::Idle)
		return EGuLiTaskStatus::Waiting;
	if (!Miner->IsManagedTaskComplete()) return EGuLiTaskStatus::Running;
	if (!Task.bAutomatic && Miner->GetCargo().Blue + Miner->GetCargo().Red == 0
		&& World.GetSubsystem<UGuLiResourceWorldSubsystem>()->IsClusterEmpty(Task.Command.ClusterId)) return EGuLiTaskStatus::Completed;
	if (Miner->DidManagedTaskFail()) { Task.Error = GuLiGameText::Text(TEXT("UI.SpecialTaskExecutor.129")); return EGuLiTaskStatus::Failed; }
	return EGuLiTaskStatus::WorkUnitComplete;
}
static bool MiningCancel(UWorld&, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&)
{ auto* Miner = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()); return !Miner || Miner->StopManagedTask(); }

static bool ConstructionValidate(UWorld& World, const FGuLiTaskUnitContext& Unit, const FGuLiUnitTaskCommand& Command, FString& Error)
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get());
	auto* Building = World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Command.BuildingId);
	FVector Position; float Length;
	const bool bTargetValid = Builder && Building && Building->GetTeam() == Unit.Team
		&& Building->GetState().Phase == EGuLiBuildingPhase::UnderConstruction;
	// An in-flight vehicle has no authoritative ground starting point until it lands.
	const bool bValid = bTargetValid && (Builder->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit()
		|| Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->PrepareBuilding(*Building, Position, Length));
	if (!bValid) Error = GuLiGameText::Text(TEXT("UI.SpecialTaskExecutor.130")); return bValid;
}
static bool ConstructionBuildAutomatic(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand& Command)
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get()); if (!Builder) return false;
	const auto* Work = Builder->FindComponentByClass<UGuLiConstructionWorkComponent>();
	if (Work->GetBehaviorPhase() == EGuLiCommanderWorkPhase::ConstructionWorking) return false;
	TArray<UGuLiBuildingLifecycleComponent*> Buildings; World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
    const int32 Current = World.GetSubsystem<UGuLiResourceWorldSubsystem>()->FindTerritoryIndex(Builder->GetActorLocation());
    // Each vehicle pulls work independently: its current territory first, then
    // elsewhere. Registry order is creation order, never route/distance order.
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        for (const auto* Building : Buildings)
        {
            if ((Building->GetState().TerritoryIndex == Current) != (Pass == 0)) continue;
            FVector Position; float UnusedLength;
            if (!Work->PrepareBuilding(*Building, Position, UnusedLength)) continue;
            Command.BuildingId = Building->GetState().InstanceId;
            Command.Target = Position;
            return true;
        }
    }
	return Command.BuildingId != 0;
}
static EGuLiTaskStatus ConstructionStart(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task)
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get());
	auto* Building = World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Task.Command.BuildingId);
	if (Builder && UGuLiExternalUnitControlComponent::AreActorActionsLocked(Builder)) return EGuLiTaskStatus::Waiting;
	if (Builder && Building && Builder->IssueConstruction(Building, Task.bAutomatic)) return EGuLiTaskStatus::Running;
    Task.Error = TEXT("工地已无可用施工位，退单");
    return Task.bAutomatic ? EGuLiTaskStatus::WorkUnitComplete : EGuLiTaskStatus::Failed;
}
static EGuLiTaskStatus ConstructionPoll(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task)
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get());
    const auto* Work = Builder ? Builder->FindComponentByClass<UGuLiConstructionWorkComponent>() : nullptr;
    if (Work && Work->IsOrderCancelled())
    {
        if (const auto* Target = Work->GetTarget(); Target && Target->IsCompleted()) return EGuLiTaskStatus::WorkUnitComplete;
        Task.Error = Work->GetOrderReason();
        return Task.bAutomatic ? EGuLiTaskStatus::WorkUnitComplete : EGuLiTaskStatus::Failed;
    }
	auto* Building = World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Task.Command.BuildingId);
	if (!Builder || !Building || Building->GetTeam() != Unit.Team || Building->GetState().Phase == EGuLiBuildingPhase::Destroyed)
	{ Task.Error = GuLiGameText::Text(TEXT("UI.SpecialTaskExecutor.131")); return EGuLiTaskStatus::Failed; }
	if (Building->IsCompleted()) return EGuLiTaskStatus::WorkUnitComplete;
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(Builder)) return EGuLiTaskStatus::Running;
	if (Task.bAutomatic && Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->AreAllPositionsRejected(*Building))
		return EGuLiTaskStatus::WorkUnitComplete;
	if (!Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->GetTarget()
        || Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->IsTerminalFailure())
	{ Task.Error = GuLiGameText::Text(TEXT("UI.SpecialTaskExecutor.132")); return EGuLiTaskStatus::Failed; }
	return EGuLiTaskStatus::Running;
}
static bool ConstructionCancel(UWorld&, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&)
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get()); if (!Builder) return true;
	Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->StopWork();
	return Builder->FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
}

static bool StrongholdAdvanceBuildAutomatic(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand&)
{
	const auto* Resources = World.GetSubsystem<UGuLiResourceWorldSubsystem>();
	return !Unit.Unit.bActor && Resources && Resources->IsRuntimeReady() && Resources->IsResourceWorldActive();
}
static EGuLiTaskStatus StrongholdAdvanceStart(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&)
{
	World.GetSubsystem<UGuLiArmyAdvanceSubsystem>()->ActivateTaskMember(Unit.Team, FGuLiSoldierId(Unit.Unit.Id), Unit.SourceTerritory);
	return EGuLiTaskStatus::Running;
}
static EGuLiTaskStatus StrongholdAdvancePoll(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution& Task)
{ return Task.WorkSerial ? EGuLiTaskStatus::WorkUnitComplete : Task.bWaitingForTarget ? EGuLiTaskStatus::Waiting : EGuLiTaskStatus::Running; }
static bool StrongholdAdvanceCancel(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&)
{ const FGuLiSoldierId Id(Unit.Unit.Id); World.GetSubsystem<UGuLiBattleAuthoritySubsystem>()->StopTaskSoldiers(MakeArrayView(&Id, 1)); return true; }

bool GuLiCommanderAbilities::Validate(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, const FGuLiUnitTaskCommand& Command, FString& Error)
{
	switch (Behavior)
	{
	case EGuLiCommanderBehavior::Mining: return MiningValidate(World, Unit, Command, Error);
	case EGuLiCommanderBehavior::Construction: return ConstructionValidate(World, Unit, Command, Error);
	case EGuLiCommanderBehavior::StrongholdAdvance: return true;
	default: return false;
	}
}

bool GuLiCommanderAbilities::BuildAutomatic(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand& Command)
{
	switch (Behavior)
	{
	case EGuLiCommanderBehavior::Mining: return MiningBuildAutomatic(World, Unit, Command);
	case EGuLiCommanderBehavior::Construction: return ConstructionBuildAutomatic(World, Unit, Command);
	case EGuLiCommanderBehavior::StrongholdAdvance: return StrongholdAdvanceBuildAutomatic(World, Unit, Command);
	default: return false;
	}
}

EGuLiTaskStatus GuLiCommanderAbilities::Start(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task)
{
	switch (Behavior)
	{
	case EGuLiCommanderBehavior::Mining: return MiningStart(World, Unit, Task);
	case EGuLiCommanderBehavior::Construction: return ConstructionStart(World, Unit, Task);
	case EGuLiCommanderBehavior::StrongholdAdvance: return StrongholdAdvanceStart(World, Unit, Task);
	default: return EGuLiTaskStatus::Failed;
	}
}

EGuLiTaskStatus GuLiCommanderAbilities::Poll(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task)
{
	switch (Behavior)
	{
	case EGuLiCommanderBehavior::Mining: return MiningPoll(World, Unit, Task);
	case EGuLiCommanderBehavior::Construction: return ConstructionPoll(World, Unit, Task);
	case EGuLiCommanderBehavior::StrongholdAdvance: return StrongholdAdvancePoll(World, Unit, Task);
	default: return EGuLiTaskStatus::Failed;
	}
}

bool GuLiCommanderAbilities::Cancel(EGuLiCommanderBehavior Behavior, UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task)
{
	switch (Behavior)
	{
	case EGuLiCommanderBehavior::Mining: return MiningCancel(World, Unit, Task);
	case EGuLiCommanderBehavior::Construction: return ConstructionCancel(World, Unit, Task);
	case EGuLiCommanderBehavior::StrongholdAdvance: return StrongholdAdvanceCancel(World, Unit, Task);
	default: return true;
	}
}
