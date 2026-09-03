// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Behavior/GuLiWingmanGroupBehaviorRunner.h"

#include "Components/StateTreeComponent.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "StateTree.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanGroupBehaviorRunner)

AGuLiWingmanGroupBehaviorRunner::AGuLiWingmanGroupBehaviorRunner()
{
	bReplicates = false;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("GroupBehaviorStateTree"));
	StateTreeComponent->SetStartLogicAutomatically(false);
}

bool AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(const ENetMode NetMode)
{
	return NetMode == NM_Client || NetMode == NM_Standalone || NetMode == NM_ListenServer;
}

bool AGuLiWingmanGroupBehaviorRunner::CanRunForOwnerContext(
	const ENetMode NetMode,
	const bool bIsLocalLeaseOwner)
{
	return bIsLocalLeaseOwner && CanRunInNetMode(NetMode);
}

bool AGuLiWingmanGroupBehaviorRunner::InitializeRunner(
	UGuLiWingmanSimulationSubsystem* InSimulation,
	const FGuLiWingmanGroupHandle& InGroup,
	UStateTree* OptionalStateTree)
{
	Simulation = InSimulation;
	Group = InGroup;
	bUsingStateTree = false;
	bUsingControlledFallback = false;
	PolicyEvaluationAccumulator = 0.0f;
	StateTreePolicyApplyCount = 0u;
	StateTreeSignalCount = 0u;

	const UWorld* World = GetWorld();
	const bool bIsLocalLeaseOwner = Simulation && Simulation->HasOwnedGroup(Group);
	if (!Simulation || !Group.IsValid() || !World
		|| !CanRunForOwnerContext(World->GetNetMode(), bIsLocalLeaseOwner))
	{
		return false;
	}
	ObservedPolicy = Simulation->EvaluateBehaviorPolicy(Group);

	if (OptionalStateTree && OptionalStateTree->IsReadyToRun() && StateTreeComponent)
	{
		// Tasks execute synchronously from StartLogic(). Mark the intended path
		// before entry conditions/tasks consult the runner.
		bUsingStateTree = true;
		StateTreeComponent->SetStateTree(OptionalStateTree);
		StateTreeComponent->StartLogic();
		bUsingStateTree = StateTreeComponent->IsRunning();
	}

	bUsingControlledFallback = !bUsingStateTree;
	if (bUsingControlledFallback)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Wingman group %s entered controlled C++ fallback: authored StateTree is %s."),
			*Group.ShipInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			OptionalStateTree
				? TEXT("present but not compiled/runnable")
				: TEXT("missing or failed to load"));
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("Wingman group %s is driven by authored StateTree %s."),
			*Group.ShipInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetPathNameSafe(OptionalStateTree));
	}
	// Async FlightNav request/poll coordination is required even when the
	// authored StateTree owns low-frequency mode policy.
	SetActorTickEnabled(true);
	return true;
}

void AGuLiWingmanGroupBehaviorRunner::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bUsingStateTree && Simulation && StateTreeComponent)
	{
		PolicyEvaluationAccumulator += FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
		if (PolicyEvaluationAccumulator >= 0.1f)
		{
			PolicyEvaluationAccumulator = FMath::Fmod(PolicyEvaluationAccumulator, 0.1f);
			const EGuLiWingmanBehaviorPolicy NewPolicy = Simulation->EvaluateBehaviorPolicy(Group);
			if (NewPolicy != ObservedPolicy)
			{
				ObservedPolicy = NewPolicy;
				const FGameplayTag Signal = GuLiWingmanBehaviorTags::GetSignalTag(NewPolicy);
				if (Signal.IsValid())
				{
					StateTreeComponent->SendStateTreeEvent(Signal, FConstStructView(), TEXT("WingmanPolicy"));
					++StateTreeSignalCount;
				}
			}
		}
	}
	if (bUsingControlledFallback && Simulation)
	{
		Simulation->TickFallbackBehavior(Group, DeltaSeconds);
	}
	if (Simulation)
	{
		Simulation->TickNavigationBehavior(Group, DeltaSeconds);
	}
}

bool AGuLiWingmanGroupBehaviorRunner::ShouldSelectPolicy(
	const EGuLiWingmanBehaviorPolicy Policy) const
{
	return bUsingStateTree && Simulation && Policy == Simulation->EvaluateBehaviorPolicy(Group);
}

bool AGuLiWingmanGroupBehaviorRunner::ApplyStateTreePolicy(
	const EGuLiWingmanBehaviorPolicy Policy,
	const float DeltaSeconds)
{
	if (!bUsingStateTree || !Simulation || !ShouldSelectPolicy(Policy))
	{
		return false;
	}
	ObservedPolicy = Policy;
	if (!Simulation->ApplyStateTreePolicy(Group, Policy, DeltaSeconds))
	{
		return false;
	}
	++StateTreePolicyApplyCount;
	return true;
}

void AGuLiWingmanGroupBehaviorRunner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(TEXT("Wingman group runner ending"));
	}
	Simulation = nullptr;
	bUsingStateTree = false;
	bUsingControlledFallback = false;
	Super::EndPlay(EndPlayReason);
}
