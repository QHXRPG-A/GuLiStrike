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

void UGuLiEngineeringCrowdFollowingComponent::ConfigureFootprint(const FBox& LocalBounds)
{
	check(LocalBounds.IsValid);
	const FVector Reach = LocalBounds.Min.GetAbs().ComponentMax(LocalBounds.Max.GetAbs());
	FootprintRadius = FMath::Max(650.0f, float(Reach.Size2D()));
	SetCrowdCollisionQueryRange(PredictionDistance, false);
	SetCrowdPathOptimizationRange(OptimizationDistance, false);
	SetCrowdSeparationWeight(VehicleSeparationWeight, false);
	UpdateCrowdAgentParams();
}

void UGuLiEngineeringCrowdFollowingComponent::GetCrowdAgentCollisions(float& Radius, float& HalfHeight) const
{
	Super::GetCrowdAgentCollisions(Radius, HalfHeight);
	Radius = FootprintRadius;
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
	// recreate that entry, otherwise factory manoeuvres can still receive steering.
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
	Super::OnPathFinished(Result);
	// RequestMove aborts the previous path while installing a replacement.
	if (!(Result.Flags & FPathFollowingResultFlags::NewRequest)
		&& CastChecked<AAIController>(GetOwner())->GetPawn()) // Cleanup can follow UnPossess.
		RefreshParticipation();
}

FVector UGuLiEngineeringCrowdFollowingComponent::GetCrowdAgentVelocity() const
{
	// Scripted factory movement has no MoveTo request but still has real velocity.
	return CastChecked<AAIController>(GetOwner())->GetPawn()->GetVelocity();
}

FString UGuLiEngineeringCrowdFollowingComponent::GetAvoidanceDebug() const
{
	const auto* Manager = UCrowdManager::GetCurrent(GetWorld());
	return FString::Printf(TEXT("Mode=%d Registered=%d Radius=%.1f Query=%.1f Neighbours=%d Path=%d Nav=%s"),
		int32(SimulationState), Manager && Manager->IsAgentValid(this), FootprintRadius, CollisionQueryRange,
		Manager ? Manager->GetNumNearbyAgents(this) : 0, int32(GetStatus()),
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
	CastChecked<UGuLiEngineeringCrowdFollowingComponent>(GetPathFollowingComponent())->ConfigureFootprint(
		CastChecked<IGuLiEngineeringVehicle>(GetPawn())->GetEngineeringTravelBounds());
}

void AGuLiEngineeringAIController::RefreshParticipation()
{
	CastChecked<UGuLiEngineeringCrowdFollowingComponent>(GetPathFollowingComponent())->RefreshParticipation();
}

void AGuLiEngineeringAIController::OnVehicleMovementModeChanged(ACharacter*, EMovementMode, uint8)
{
	RefreshParticipation();
}
