#include "Commander/Behavior/GuLiCommanderStateTreeComponent.h"
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Engine/World.h"

UGuLiCommanderStateTreeComponent::UGuLiCommanderStateTreeComponent()
{
	SetStartLogicAutomatically(false);
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

TSubclassOf<UStateTreeSchema> UGuLiCommanderStateTreeComponent::GetSchema() const
{
	return UGuLiCommanderActorStateTreeSchema::StaticClass();
}

void UGuLiCommanderStateTreeComponent::Configure(FGuLiTaskUnitId InUnit, UStateTree& Tree)
{
	if (IsRunning()) StopLogic(TEXT("Unit reconfigured"));
	bStartAttempted = false;
	Unit = InUnit;
	SetStateTree(&Tree);
}

void UGuLiCommanderStateTreeComponent::AdvanceBehavior(float DeltaSeconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (!bStartAttempted)
	{
		bStartAttempted = true;
		StartLogic();
	}
	else if (IsRunning())
	{
		Super::TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}
	if (!IsRunning())
		if (auto* Tasks = GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>())
			Tasks->ReportBehaviorError(Unit, TEXT("Actor StateTree failed to start or stopped unexpectedly."));
}
