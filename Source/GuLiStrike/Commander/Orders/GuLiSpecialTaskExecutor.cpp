#include "Commander/Orders/GuLiSpecialTaskExecutor.h"
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

bool UGuLiMiningSpecialTaskExecutor::SupportsDefinition(FGameplayTag Tag, const FGuLiSoldierDefinition& Unit) const
{ return Tag.GetTagName() == TEXT("Task.Special.Mining") && Unit.ActorClass && Unit.ActorClass->IsChildOf(AGuLiMiningVehiclePawn::StaticClass()); }
bool UGuLiMiningSpecialTaskExecutor::Validate(UWorld& World, const FGuLiTaskUnitContext& Unit, const FGuLiUnitTaskCommand& Command, FString& Error) const
{
	const auto* Resources = World.GetSubsystem<UGuLiResourceWorldSubsystem>();
	const bool bValid = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()) && Command.ClusterId && Resources && Resources->IsRuntimeReady()
		&& Resources->CanTeamMineAt(Unit.Team, Command.ClusterId) && !Resources->IsClusterEmpty(Command.ClusterId);
	if (!bValid) Error = TEXT("无可采集的目标矿区"); return bValid;
}
bool UGuLiMiningSpecialTaskExecutor::BuildAutomatic(UWorld&, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand& Command) const
{ return Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()) != nullptr; }
EGuLiTaskStatus UGuLiMiningSpecialTaskExecutor::Start(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task) const
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
EGuLiTaskStatus UGuLiMiningSpecialTaskExecutor::Poll(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task) const
{
	const auto* Miner = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()); if (!Miner) return EGuLiTaskStatus::Failed;
	if (Task.bAutomatic && Task.bYieldRequested && Miner->GetTaskState() == EGuLiMiningTaskState::Idle
		&& Miner->GetCargo().Blue + Miner->GetCargo().Red == 0) return EGuLiTaskStatus::WorkUnitComplete;
	if (Task.bAutomatic && !Miner->IsManagedTaskComplete() && Miner->GetTaskState() == EGuLiMiningTaskState::Idle)
		return EGuLiTaskStatus::Waiting;
	if (!Miner->IsManagedTaskComplete()) return EGuLiTaskStatus::Running;
	if (!Task.bAutomatic && Miner->GetCargo().Blue + Miner->GetCargo().Red == 0
		&& World.GetSubsystem<UGuLiResourceWorldSubsystem>()->IsClusterEmpty(Task.Command.ClusterId)) return EGuLiTaskStatus::Completed;
	if (Miner->DidManagedTaskFail()) { Task.Error = TEXT("采矿或返厂路径失效"); return EGuLiTaskStatus::Failed; }
	return EGuLiTaskStatus::WorkUnitComplete;
}
bool UGuLiMiningSpecialTaskExecutor::Cancel(UWorld&, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&) const
{ auto* Miner = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get()); return !Miner || Miner->StopManagedTask(); }

bool UGuLiConstructionSpecialTaskExecutor::SupportsDefinition(FGameplayTag Tag, const FGuLiSoldierDefinition& Unit) const
{ return Tag.GetTagName() == TEXT("Task.Special.Construction") && Unit.ActorClass && Unit.ActorClass->IsChildOf(AGuLiConstructionVehiclePawn::StaticClass()); }
bool UGuLiConstructionSpecialTaskExecutor::Validate(UWorld& World, const FGuLiTaskUnitContext& Unit, const FGuLiUnitTaskCommand& Command, FString& Error) const
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get());
	auto* Building = World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Command.BuildingId);
	FVector Position; float Length;
	const bool bTargetValid = Builder && Building && Building->GetTeam() == Unit.Team
		&& Building->GetState().Phase == EGuLiBuildingPhase::UnderConstruction;
	// An in-flight vehicle has no authoritative ground starting point until it lands.
	const bool bValid = bTargetValid && (Builder->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit()
		|| Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->PrepareBuilding(*Building, Position, Length));
	if (!bValid) Error = TEXT("工地无效或无可达施工位置"); return bValid;
}
bool UGuLiConstructionSpecialTaskExecutor::BuildAutomatic(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand& Command) const
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get()); if (!Builder) return false;
	const auto* Work = Builder->FindComponentByClass<UGuLiConstructionWorkComponent>();
	TArray<UGuLiBuildingLifecycleComponent*> Buildings; World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
	float Best = TNumericLimits<float>::Max();
	for (const auto* Building : Buildings)
	{
		FVector Position; float Length;
		if (!Work->PrepareBuilding(*Building, Position, Length)) continue;
		if (Length < Best || (FMath::IsNearlyEqual(Length, Best) && Building->GetState().InstanceId < Command.BuildingId))
		{ Best = Length; Command.BuildingId = Building->GetState().InstanceId; Command.Target = Position; }
	}
	return Command.BuildingId != 0;
}
EGuLiTaskStatus UGuLiConstructionSpecialTaskExecutor::Start(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task) const
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get());
	auto* Building = World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Task.Command.BuildingId);
	if (Builder && UGuLiExternalUnitControlComponent::AreActorActionsLocked(Builder)) return EGuLiTaskStatus::Waiting;
	return Builder && Building && Builder->IssueConstruction(Building) ? EGuLiTaskStatus::Running : EGuLiTaskStatus::Failed;
}
EGuLiTaskStatus UGuLiConstructionSpecialTaskExecutor::Poll(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task) const
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get());
	auto* Building = World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Task.Command.BuildingId);
	if (!Builder || !Building || Building->GetTeam() != Unit.Team || Building->GetState().Phase == EGuLiBuildingPhase::Destroyed)
	{ Task.Error = TEXT("工地已失效"); return EGuLiTaskStatus::Failed; }
	if (Building->IsCompleted()) return EGuLiTaskStatus::WorkUnitComplete;
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(Builder)) return EGuLiTaskStatus::Running;
	if (!Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->GetTarget())
	{ Task.Error = TEXT("施工已中断"); return EGuLiTaskStatus::Failed; }
	return EGuLiTaskStatus::Running;
}
bool UGuLiConstructionSpecialTaskExecutor::Cancel(UWorld&, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&) const
{
	auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Unit.Pawn.Get()); if (!Builder) return true;
	Builder->FindComponentByClass<UGuLiConstructionWorkComponent>()->StopWork();
	return Builder->FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
}

bool UGuLiStrongholdAdvanceSpecialTaskExecutor::SupportsDefinition(FGameplayTag Tag, const FGuLiSoldierDefinition& Unit) const
{ return Tag.GetTagName() == TEXT("Task.Special.StrongholdAdvance") && Unit.UsesMass(); }
bool UGuLiStrongholdAdvanceSpecialTaskExecutor::BuildAutomatic(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand&) const
{
	const auto* Resources = World.GetSubsystem<UGuLiResourceWorldSubsystem>();
	return !Unit.Unit.bActor && Resources && Resources->IsRuntimeReady() && Resources->IsResourceWorldActive();
}
EGuLiTaskStatus UGuLiStrongholdAdvanceSpecialTaskExecutor::Start(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&) const
{
	World.GetSubsystem<UGuLiArmyAdvanceSubsystem>()->ActivateTaskMember(Unit.Team, FGuLiSoldierId(Unit.Unit.Id), Unit.SourceTerritory);
	return EGuLiTaskStatus::Running;
}
EGuLiTaskStatus UGuLiStrongholdAdvanceSpecialTaskExecutor::Poll(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution& Task) const
{ return Task.WorkSerial ? EGuLiTaskStatus::WorkUnitComplete : Task.bWaitingForTarget ? EGuLiTaskStatus::Waiting : EGuLiTaskStatus::Running; }
bool UGuLiStrongholdAdvanceSpecialTaskExecutor::Cancel(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution&) const
{ const FGuLiSoldierId Id(Unit.Unit.Id); World.GetSubsystem<UGuLiBattleAuthoritySubsystem>()->StopTaskSoldiers(MakeArrayView(&Id, 1)); return true; }
