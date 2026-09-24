#include "Gameplay/Units/GuLiEngineeringAIController.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/CrowdManager.h"
#include "NavigationData.h"

UGuLiEngineeringCrowdFollowingComponent::UGuLiEngineeringCrowdFollowingComponent(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	bEnableAnticipateTurns = true;
	bEnableObstacleAvoidance = true;
	bEnableSeparation = true;
	bEnableOptimizeVisibility = true;
	bEnableOptimizeTopology = true;
	bEnableSlowdownAtGoal = true;
	bRotateToVelocity = true;
	AvoidanceQuality = ECrowdAvoidanceQuality::High;
}

void UGuLiEngineeringCrowdFollowingComponent::ConfigureNavigation()
{
	// Crowd uses the same capsule as physical movement; cosmetic mining arms do
	// not enlarge the body or prevent it from reaching the factory floor.
	SetCrowdCollisionQueryRange(PredictionDistance, false);
	SetCrowdPathOptimizationRange(OptimizationDistance, false);
	SetCrowdSeparationWeight(VehicleSeparationWeight, false);
	UpdateCrowdAgentParams();
}

void UGuLiEngineeringCrowdFollowingComponent::RefreshParticipation()
{
	const auto* Character = CastChecked<ACharacter>(CastChecked<AAIController>(GetOwner())->GetPawn());
	const auto* Control = Character->FindComponentByClass<UGuLiExternalUnitControlComponent>();
	check(Control);
	const ECrowdSimulationState Desired = Control->IsPhased() || !Character->GetActorEnableCollision()
		? ECrowdSimulationState::Disabled
		: (Control->AreActionsLocked() || Character->GetCharacterMovement()->MovementMode == MOVE_None
			|| (GetStatus() != EPathFollowingStatus::Moving && !bPausedForControl)
			? ECrowdSimulationState::ObstacleOnly : ECrowdSimulationState::Enabled);
	ChangeParticipation(Desired);
}

void UGuLiEngineeringCrowdFollowingComponent::ChangeParticipation(ECrowdSimulationState Desired)
{
	if (Desired == SimulationState) return;

	if (SimulationState == ECrowdSimulationState::Enabled && GetStatus() == EPathFollowingStatus::Moving)
	{
		PauseMove();
		bPausedForControl = true;
	}
	UCrowdManager* Manager = UCrowdManager::GetCurrent(GetWorld());
	check(Manager);
	if (bRegisteredWithCrowdSimulation) Manager->UnregisterAgent(this);
	// UE caches whether an agent is simulated at registration. A mode switch must
	// recreate that entry so external control cannot leave stale steering active.
	SimulationState = Desired;
	bRegisteredWithCrowdSimulation = Desired != ECrowdSimulationState::Disabled;
	if (bRegisteredWithCrowdSimulation) Manager->RegisterAgent(this);
	if (Desired == ECrowdSimulationState::Enabled && bPausedForControl)
	{
		bPausedForControl = false;
		if (GetStatus() == EPathFollowingStatus::Paused)
		{
			ResumeMove();
			// The recreated crowd entry has no corridor yet.
			SetMoveSegment(DetermineStartingPathPoint(nullptr));
		}
	}
}

void UGuLiEngineeringCrowdFollowingComponent::OnPathfindingQuery(FPathFindingQuery& Query)
{
	// Idle bodies need no walkable polygon. Enable corridor steering before the
	// query so UE builds the unstring-pulled path required by CrowdFollowing.
	ChangeParticipation(ECrowdSimulationState::Enabled);
	Super::OnPathfindingQuery(Query);
}

void UGuLiEngineeringCrowdFollowingComponent::OnPathFinished(const FPathFollowingResult& Result)
{
	if (Result.Flags & FPathFollowingResultFlags::NewRequest)
	{
		// RequestMove installs the prepared corridor immediately after this callback.
		// UCrowdFollowingComponent also clears Detour's velocity, even when the base
		// path follower preserves character velocity. Keep both for this handover.
		UPathFollowingComponent::OnPathFinished(Result);
		return;
	}
	Super::OnPathFinished(Result);
	if (CastChecked<AAIController>(GetOwner())->GetPawn()) // Cleanup can follow UnPossess.
		RefreshParticipation();
}

void UGuLiEngineeringCrowdFollowingComponent::UpdatePathSegment()
{
	// An avoidance turn away from the goal is not arrival. Use the path follower's
	// normal acceptance test, without Crowd's direction-only overshoot shortcut.
	bCanCheckMovingTooFar = false;
	bCheckMovementAngle = false;
	Super::UpdatePathSegment();
}

FString UGuLiEngineeringCrowdFollowingComponent::GetAvoidanceDebug() const
{
	const auto* Manager = UCrowdManager::GetCurrent(GetWorld());
	float Radius = 0, HalfHeight = 0;
	GetCrowdAgentCollisions(Radius, HalfHeight);
	return FString::Printf(TEXT("Mode=%d Registered=%d Radius=%.1f Query=%.1f Neighbours=%d Path=%d Group=%d Ignore=%d Nav=%s"),
		int32(SimulationState), Manager && Manager->IsAgentValid(this), Radius, CollisionQueryRange,
		Manager ? Manager->GetNumNearbyAgents(this) : 0, int32(GetStatus()),
		GetCrowdAgentAvoidanceGroup(), GetCrowdAgentGroupsToIgnore(),
		Manager ? *GetNameSafe(Manager->GetNavData()) : TEXT("None"));
}

AGuLiEngineeringAIController::AGuLiEngineeringAIController(const FObjectInitializer& Initializer)
	: Super(Initializer.SetDefaultSubobjectClass<UGuLiEngineeringCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{}

void AGuLiEngineeringAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	auto* VehicleCharacter = CastChecked<ACharacter>(InPawn);
	check(Cast<IGuLiEngineeringVehicle>(InPawn));
	VehicleCharacter->MovementModeChangedDelegate.AddDynamic(this, &ThisClass::OnVehicleMovementModeChanged);
	auto* Control = VehicleCharacter->FindComponentByClass<UGuLiExternalUnitControlComponent>();
	check(Control);
	Control->OnStateApplied.AddUObject(this, &ThisClass::RefreshParticipation);
	RefreshParticipation();
}

void AGuLiEngineeringAIController::OnUnPossess()
{
	if (auto* VehicleCharacter = Cast<ACharacter>(GetPawn()))
	{
		VehicleCharacter->MovementModeChangedDelegate.RemoveDynamic(this, &ThisClass::OnVehicleMovementModeChanged);
		VehicleCharacter->FindComponentByClass<UGuLiExternalUnitControlComponent>()->OnStateApplied.RemoveAll(this);
	}
	Super::OnUnPossess();
}

void AGuLiEngineeringAIController::ConfigureVehicleNavigation()
{
	CastChecked<UGuLiEngineeringCrowdFollowingComponent>(GetPathFollowingComponent())->ConfigureNavigation();
}

void AGuLiEngineeringAIController::RefreshParticipation()
{
	CastChecked<UGuLiEngineeringCrowdFollowingComponent>(GetPathFollowingComponent())->RefreshParticipation();
}

void AGuLiEngineeringAIController::OnVehicleMovementModeChanged(ACharacter*, EMovementMode, uint8)
{
	RefreshParticipation();
}
