// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanBehaviorStateTree.h"
#include "GameFramework/Actor.h"
#include "GuLiWingmanGroupBehaviorRunner.generated.h"

class UGuLiWingmanSimulationSubsystem;
class UStateTree;
class UStateTreeComponent;

/**
 * One non-replicated, client/standalone-only policy runner per locally owned group.
 * It never owns Mass transforms. An authored StateTree is preferred; if none is
 * configured, Tick drives the bounded C++ mode-policy fallback explicitly.
 * FlightNav coordination always ticks in either mode.
 */
UCLASS(NotBlueprintable, Transient)
class GULISTRIKE_API AGuLiWingmanGroupBehaviorRunner final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiWingmanGroupBehaviorRunner();

	bool InitializeRunner(UGuLiWingmanSimulationSubsystem* InSimulation,
		const FGuLiWingmanGroupHandle& InGroup, UStateTree* OptionalStateTree);
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool IsUsingStateTree() const { return bUsingStateTree; }
	bool IsUsingControlledFallback() const { return bUsingControlledFallback; }
	bool IsNavigationCoordinationTickEnabled() const { return IsActorTickEnabled(); }
	bool ShouldSelectPolicy(EGuLiWingmanBehaviorPolicy Policy) const;
	bool ApplyStateTreePolicy(EGuLiWingmanBehaviorPolicy Policy, float DeltaSeconds);
	EGuLiWingmanBehaviorPolicy GetObservedPolicy() const { return ObservedPolicy; }
	uint64 GetStateTreePolicyApplyCount() const { return StateTreePolicyApplyCount; }
	uint64 GetStateTreeSignalCount() const { return StateTreeSignalCount; }
	static bool CanRunForOwnerContext(ENetMode NetMode, bool bIsLocalLeaseOwner);
	static bool CanRunInNetMode(ENetMode NetMode);

private:
	UPROPERTY(VisibleAnywhere, Category="Wingman|Behavior")
	TObjectPtr<UStateTreeComponent> StateTreeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UGuLiWingmanSimulationSubsystem> Simulation;

	FGuLiWingmanGroupHandle Group;
	EGuLiWingmanBehaviorPolicy ObservedPolicy = EGuLiWingmanBehaviorPolicy::OwnerUnavailable;
	float PolicyEvaluationAccumulator = 0.0f;
	uint64 StateTreePolicyApplyCount = 0u;
	uint64 StateTreeSignalCount = 0u;
	bool bUsingStateTree = false;
	bool bUsingControlledFallback = false;
};
