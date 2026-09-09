// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanPawn.h"

#include "Components/SphereComponent.h"
#include "Components/StateTreeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"
#include "StateTree.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanPawn)

AGuLiWingmanPawn::AGuLiWingmanPawn()
{
	bReplicates = false;
	bAlwaysRelevant = false;
	SetReplicateMovement(false);
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	PrimaryActorTick.bCanEverTick = false;

	CollisionRoot = CreateDefaultSubobject<USphereComponent>(TEXT("WingmanCollision"));
	CollisionRoot->InitSphereRadius(1500.0f);
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionRoot->SetCollisionObjectType(ECC_Pawn);
	CollisionRoot->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionRoot->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionRoot->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	// Wingmen are presentation aircraft, not mutually blocking physics bodies.
	// Their paths may cross during a dense attack without generating Pawn contacts.
	CollisionRoot->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	SetRootComponent(CollisionRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WingmanMesh"));
	VisualMesh->SetupAttachment(CollisionRoot);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCanEverAffectNavigation(false);
	VisualMesh->CastShadow = true;

	FlightMovement = CreateDefaultSubobject<UGuLiWingmanFlightMovementComponent>(TEXT("FlightMovement"));
	FlightMovement->SetUpdatedComponent(CollisionRoot);

	StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("MemberBehaviorStateTree"));
	StateTreeComponent->SetStartLogicAutomatically(false);
}

void AGuLiWingmanPawn::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		FGuLiWingmanQAInvariantRegistry::Add(TEXT("SERVER_WINGMAN_PAWN_CREATED"));
		Destroy();
	}
}

void AGuLiWingmanPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopStateTree(TEXT("Wingman Pawn ending"));
	Super::EndPlay(EndPlayReason);
}

UPawnMovementComponent* AGuLiWingmanPawn::GetMovementComponent() const
{
	return FlightMovement;
}

bool AGuLiWingmanPawn::InitializeOwnerSimulation(
	const FGuLiWingmanRuntimeState& InitialState,
	UStateTree* BehaviorStateTree,
	UStaticMesh* Mesh)
{
	if (GetNetMode() == NM_DedicatedServer || !InitialState.Identity.Handle.IsValid()
		|| !InitialState.Tuning.Formation.IsWellFormed())
	{
		return false;
	}

	StopStateTree(TEXT("Reinitializing Wingman Pawn"));
	Runtime = InitialState;
	PawnMode = EGuLiWingmanPawnMode::OwnerSimulation;
	bOwnerSimulationActive = true;
	bPresentationInteractable = true;
	bEmergencyRebasePending = false;
	ObservedBehavior = EvaluateDesiredBehavior();
	SelectedBehavior = ObservedBehavior;
	BehaviorObservationAccumulator = 0.0f;
	RebaseRetryAfterServerTimeSeconds = 0.0;

	ConfigureMesh(Mesh);
	CollisionRoot->SetSphereRadius(Runtime.Tuning.Formation.AgentRadiusCentimeters, false);
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	FlightMovement->InitializeForOwner(*this);

	if (BehaviorStateTree && BehaviorStateTree->IsReadyToRun() && StateTreeComponent)
	{
		StateTreeComponent->SetStateTree(BehaviorStateTree);
		StateTreeComponent->StartLogic();
		bUsingStateTree = StateTreeComponent->IsRunning();
	}
	if (!bUsingStateTree)
	{
#if WITH_DEV_AUTOMATION_TESTS
		const UWorld* World = GetWorld();
		if (World && (World->HasAnyFlags(RF_Transient)
			|| (World->GetOutermost()
				&& World->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")))))
		{
			return true;
		}
#endif
		UE_LOG(LogTemp, Error,
			TEXT("Wingman %u/%u could not start native member StateTree"),
			Runtime.Identity.Handle.Flight.FlightIndex,
			Runtime.Identity.Handle.MemberIndex);
		return false;
	}
	return true;
}

bool AGuLiWingmanPawn::InitializeRemotePresentation(
	const FGuLiWingmanHandle& Handle,
	UStaticMesh* Mesh)
{
	if (GetNetMode() == NM_DedicatedServer || !Handle.IsValid())
	{
		return false;
	}
	StopStateTree(TEXT("Entering remote presentation"));
	Runtime = FGuLiWingmanRuntimeState{};
	Runtime.Identity.Handle = Handle;
	Runtime.Dynamics.bAlive = true;
	PawnMode = EGuLiWingmanPawnMode::RemotePresentation;
	bOwnerSimulationActive = false;
	bPresentationInteractable = false;
	bEmergencyRebasePending = false;
	ConfigureMesh(Mesh);
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorEnableCollision(false);
	SetActorHiddenInGame(false);
	SetActorTickEnabled(false);
	return true;
}

void AGuLiWingmanPawn::ResetForPool()
{
	StopStateTree(TEXT("Returned to Wingman presentation pool"));
	Runtime = FGuLiWingmanRuntimeState{};
	PawnMode = EGuLiWingmanPawnMode::RemotePresentation;
	bOwnerSimulationActive = false;
	bPresentationInteractable = false;
	bEmergencyRebasePending = false;
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);
}

void AGuLiWingmanPawn::UpdateBehaviorObservation(const float DeltaSeconds)
{
	if (!bUsingStateTree || !StateTreeComponent)
	{
#if WITH_DEV_AUTOMATION_TESTS
		// Transient automation Worlds may omit the cooked asset. This is a test
		// seam only; production owner Pawns require the native StateTree above.
		SelectedBehavior = EvaluateDesiredBehavior();
#endif
		return;
	}
	BehaviorObservationAccumulator += FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
	if (BehaviorObservationAccumulator < 0.1f)
	{
		return;
	}
	BehaviorObservationAccumulator = FMath::Fmod(BehaviorObservationAccumulator, 0.1f);
	const EGuLiWingmanMemberBehavior Desired = EvaluateDesiredBehavior();
	if (Desired != ObservedBehavior)
	{
		ObservedBehavior = Desired;
		const FGameplayTag Signal = GuLiWingmanMemberBehaviorTags::GetSignalTag(Desired);
		if (Signal.IsValid())
		{
			StateTreeComponent->SendStateTreeEvent(
				Signal, FConstStructView(), TEXT("WingmanMemberBehavior"));
		}
	}
}

EGuLiWingmanMemberBehavior AGuLiWingmanPawn::EvaluateDesiredBehavior() const
{
	if (!Runtime.Dynamics.bAlive)
	{
		return EGuLiWingmanMemberBehavior::Dead;
	}
	if (Runtime.Avoidance.bControlledRecovery)
	{
		return EGuLiWingmanMemberBehavior::EmergencyAvoid;
	}
	if (Runtime.Attack.bGuiding && Runtime.Attack.Target.IsValid())
	{
		return Runtime.Attack.Target.bGround
			? EGuLiWingmanMemberBehavior::GroundAttack
			: EGuLiWingmanMemberBehavior::AirAttack;
	}
	const float Distance = static_cast<float>(FVector::Distance(
		GetActorLocation(), Runtime.Carrier.Transform.GetLocation()));
	if (Runtime.Dynamics.Mode == EGuLiWingmanFlightMode::CatchUp
		|| Runtime.Dynamics.Mode == EGuLiWingmanFlightMode::Recover
		|| Runtime.Navigation.bHasPath
		|| Distance > Runtime.Tuning.Formation.SwarmOrbit.OuterSoftRadiusCentimeters)
	{
		return EGuLiWingmanMemberBehavior::Rejoin;
	}
	return EGuLiWingmanMemberBehavior::EscortOrbit;
}

bool AGuLiWingmanPawn::ShouldSelectBehavior(const EGuLiWingmanMemberBehavior Behavior) const
{
	// StateTree evaluates entry conditions synchronously inside StartLogic(), before
	// InitializeOwnerSimulation() can observe IsRunning().  Requiring the cached
	// running flag here makes every initial child unselectable.
	return IsOwnerSimulationPawn()
		&& Behavior == EvaluateDesiredBehavior();
}

bool AGuLiWingmanPawn::ApplyStateTreeBehavior(const EGuLiWingmanMemberBehavior Behavior)
{
	if (!ShouldSelectBehavior(Behavior))
	{
		return false;
	}
	SelectedBehavior = Behavior;
	switch (Behavior)
	{
	case EGuLiWingmanMemberBehavior::Dead:
		Runtime.Dynamics.Mode = EGuLiWingmanFlightMode::Stale;
		break;
	case EGuLiWingmanMemberBehavior::EmergencyAvoid:
		Runtime.Dynamics.Mode = EGuLiWingmanFlightMode::Recover;
		break;
	case EGuLiWingmanMemberBehavior::Rejoin:
		Runtime.Dynamics.Mode = EGuLiWingmanFlightMode::CatchUp;
		break;
	case EGuLiWingmanMemberBehavior::EscortOrbit:
		Runtime.Dynamics.Mode = EGuLiWingmanFlightMode::Orbit;
		break;
	default:
		break;
	}
	return true;
}

void AGuLiWingmanPawn::ApplyRemotePresentation(
	const FTransform& Transform,
	const float Opacity,
	const bool bInteractable,
	const bool bAuthorityRebase)
{
	if (IsOwnerSimulationPawn() || Transform.ContainsNaN())
	{
		return;
	}
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	bPresentationInteractable = bInteractable;
	const float SafeOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	VisualMesh->SetScalarParameterValueOnMaterials(TEXT("WingmanOpacity"), SafeOpacity);
	SetActorHiddenInGame(SafeOpacity <= 0.0f);
}

void AGuLiWingmanPawn::ApplyAuthorityRebase(
	const FTransform& Transform,
	const FVector& InitialVelocity)
{
	if (!IsOwnerSimulationPawn() || Transform.ContainsNaN() || InitialVelocity.ContainsNaN())
	{
		return;
	}
	FlightMovement->ApplyAuthorityRebase(Transform, InitialVelocity);
	Runtime.Dynamics.bStale = false;
	bOwnerSimulationActive = true;
	bPresentationInteractable = true;
	bEmergencyRebasePending = false;
	RebaseRetryAfterServerTimeSeconds = 0.0;
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetActorEnableCollision(true);
	SetActorHiddenInGame(false);
}

void AGuLiWingmanPawn::MarkRebaseRejected(
	const bool bNoSafePoint,
	const double RetryAfterServerTimeSeconds)
{
	bEmergencyRebasePending = false;
	Runtime.Avoidance.bAwaitingRebase = false;
	Runtime.Avoidance.bRebaseRequested = false;
	RebaseRetryAfterServerTimeSeconds = FMath::Max(0.0, RetryAfterServerTimeSeconds);
	// A server-side compatibility rebase result cannot suspend the owning client's
	// aircraft. Local obstacle recovery keeps integrating from the last safe pose.
	Runtime.Dynamics.bStale = false;
	bOwnerSimulationActive = true;
	bPresentationInteractable = true;
	SetActorHiddenInGame(false);
	if (bNoSafePoint)
	{
		Runtime.Dynamics.Mode = EGuLiWingmanFlightMode::Recover;
		Runtime.Avoidance.bControlledRecovery = true;
	}
}

void AGuLiWingmanPawn::QueueStaleRebaseRetry(
	const double EstimatedServerTimeSeconds)
{
	if (!IsOwnerSimulationPawn() || !Runtime.Dynamics.bAlive
		|| !Runtime.Dynamics.bStale || bEmergencyRebasePending
		|| !FMath::IsFinite(EstimatedServerTimeSeconds)
		|| EstimatedServerTimeSeconds < RebaseRetryAfterServerTimeSeconds)
	{
		return;
	}
	PendingRebaseReason = EGuLiWingmanEmergencyRebaseReason::MovementDeadlock;
	bEmergencyRebasePending = true;
	Runtime.Avoidance.bRebaseRequested = true;
	RebaseRetryAfterServerTimeSeconds = EstimatedServerTimeSeconds + 1.0;
}

void AGuLiWingmanPawn::RequestEmergencyRebase(
	const EGuLiWingmanEmergencyRebaseReason Reason)
{
	if (!IsOwnerSimulationPawn() || bEmergencyRebasePending
		|| !Runtime.Dynamics.bAlive)
	{
		return;
	}
	PendingRebaseReason = Reason;
	bEmergencyRebasePending = true;
	Runtime.Avoidance.bRebaseRequested = true;
}

bool AGuLiWingmanPawn::ConsumeEmergencyRebaseRequest(
	EGuLiWingmanEmergencyRebaseReason& OutReason)
{
	if (!bEmergencyRebasePending)
	{
		return false;
	}
	OutReason = PendingRebaseReason;
	// This compatibility request never pauses local flight while awaiting a reply.
	bEmergencyRebasePending = false;
	return true;
}

void AGuLiWingmanPawn::CancelFrozenAttackForRecovery()
{
	if (!GuLiWingmanAttack::IsFrozenGroundExecutionPhase(Runtime.Attack.Phase))
	{
		return;
	}
	Runtime.Attack.Phase = EGuLiWingmanAttackPhase::Idle;
	Runtime.Attack.bGuiding = false;
	Runtime.Attack.PreferredVelocity = FVector::ZeroVector;
	Runtime.Attack.LastCancelReason = 9u;
}

void AGuLiWingmanPawn::SetAlive(const bool bAlive)
{
	Runtime.Dynamics.bAlive = bAlive;
	bPresentationInteractable = bAlive;
	CollisionRoot->SetCollisionEnabled(bAlive && IsOwnerSimulationPawn()
		? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	SetActorHiddenInGame(!bAlive);
}

void AGuLiWingmanPawn::ConfigureMesh(UStaticMesh* Mesh)
{
	if (VisualMesh && Mesh)
	{
		VisualMesh->SetStaticMesh(Mesh);
	}
}

void AGuLiWingmanPawn::StopStateTree(const TCHAR* Reason)
{
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(Reason);
	}
	bUsingStateTree = false;
}

#if WITH_DEV_AUTOMATION_TESTS
void AGuLiWingmanPawn::TickStateTreeForTests(const float DeltaSeconds)
{
	if (bUsingStateTree && StateTreeComponent
		&& FMath::IsFinite(DeltaSeconds) && DeltaSeconds >= 0.0f)
	{
		StateTreeComponent->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}
}
#endif
