#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Gameplay/Resources/GuLiMiningPresentationComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiActorDamageReceiverComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"

AGuLiMiningVehiclePawn::AGuLiMiningVehiclePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGuLiExternalCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	CreateDefaultSubobject<UGuLiUnitFeedbackComponent>(TEXT("UnitFeedback"));
	CreateDefaultSubobject<UGuLiEngineeringTravelComponent>(TEXT("Travel"));
	CreateDefaultSubobject<UGuLiExternalUnitControlComponent>(TEXT("ExternalControl"));
	CombatHealth = CreateDefaultSubobject<UGuLiCombatHealthComponent>(TEXT("CombatHealth"));
	CreateDefaultSubobject<UGuLiActorDamageReceiverComponent>(TEXT("DamageReceiver"));
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.0f);
	// ACharacter requires HalfHeight >= Radius. Keep a real Actor collision envelope
	// while selecting the deliberately conservative CommanderSoldier nav agent.
	GetCapsuleComponent()->InitCapsuleSize(130.0f, 130.0f);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	GetCapsuleComponent()->SetMaskFilterOnBodyInstance(GuLiEngineeringCollision::VehicleMask);
	GetCapsuleComponent()->SetMoveIgnoreMask(GuLiEngineeringCollision::VehicleMask);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    VehiclePresentation = CreateDefaultSubobject<UChildActorComponent>(TEXT("VehiclePresentation"));
    VehiclePresentation->SetupAttachment(GetCapsuleComponent());
    VehiclePresentation->OnChildActorCreated().AddStatic(&GuLiUnitRenderPolicy::ApplyToActor);
    MiningPresentation = CreateDefaultSubobject<UGuLiMiningPresentationComponent>(TEXT("MiningPresentation"));
	AIControllerClass = AGuLiMiningVehicleAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 240.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 900.0f;
	GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
	GetCharacterMovement()->NavAgentProps.AgentRadius = GULI_RESOURCE_MINING_VEHICLE_NAV_RADIUS_CM;
	GetCharacterMovement()->NavAgentProps.AgentHeight = 28.8f;
}

void AGuLiMiningVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(HasAuthority());
	GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->RegisterVehicle(*this);
	FindComponentByClass<UGuLiEngineeringTravelComponent>()->OnTransportStarted.AddWeakLambda(this, [this]() { SetMiningVisual(false); });
	FindComponentByClass<UGuLiEngineeringTravelComponent>()->OnTransportEnded.AddWeakLambda(this, [this]()
	{
		if (!FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsWorkJourney()) BeginGrace();
	});
}

void AGuLiMiningVehiclePawn::InitializeVehicle(
	const EGuLiTeam InTeam,
	const FGuLiControllableActorId InStableActorId,
	const FGuLiSoldierDefinition& Unit,
	const UGuLiResourceEconomyConfig& Config,
	AGuLiResourceFactoryActor& InFactory)
{
	Team = InTeam;
	StableActorId = InStableActorId;
	UnitTypeId = Unit.UnitTypeId;
	FGuLiTargetHandle CombatTarget;
	CombatTarget.Kind = EGuLiTargetKind::GroundActor;
	CombatTarget.AuthorityId = FGuid::NewGuid();
	CombatTarget.Generation = 1;
	CombatTarget.LocalId = InStableActorId.Value;
	CombatHealth->ConfigureServerTarget(CombatTarget, Team);
	CombatHealth->InitializeServerHealth(Unit.MaxHealth);
	Factory = &InFactory;
	SpeedCentimetersPerSecond = Unit.MovementSpeedCmPerSecond;
	MiningRatePerSecond = Config.MiningRatePerSecond;
	CargoCapacity = Config.CargoCapacity;
	DockingSeconds = Config.DockingSeconds;
	GraceSeconds = Config.PlayerOrderGraceSeconds;
	AutoRetrySeconds = Config.AutoRetrySeconds;
	GetCharacterMovement()->MaxWalkSpeed = SpeedCentimetersPerSecond;
    PresentationDefinition.ActorClass = Unit.PresentationClass;
    PresentationDefinition.Scale = Unit.PresentationScale;
    OnRep_Presentation();
    InFactory.RegisterMiningVehicle(*this);
	if (HasAuthority() && !GetController()) SpawnDefaultController();
	if (HasAuthority()) CastChecked<AGuLiEngineeringAIController>(GetController())->ConfigureVehicleNavigation();
	if (HasAuthority())
	{
		bTaskManaged = true; ControlMode = EGuLiMiningControlMode::PlayerOrder;
		GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->RegisterActor(*this);
	}
	ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)) return;
    if (FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsRouting()) return;
    const auto* Resources = GetResourceSubsystem();
    if (!Resources || !Resources->IsRuntimeReady() || BehaviorResult != EGuLiCommanderWorkResult::Running) return;
    // Only execute the action chosen by StateTree. Every completion is reported, never followed by another action here.
    switch (TaskState)
    {
    case EGuLiMiningTaskState::MovingToCluster: TickMovingToCluster(); break;
    case EGuLiMiningTaskState::Mining: TickMining(DeltaSeconds); break;
    case EGuLiMiningTaskState::ReturningToFactory: TickReturningToFactory(); break;
    case EGuLiMiningTaskState::Docking: TickDocking(DeltaSeconds); break;
    case EGuLiMiningTaskState::PlayerMoving: TickPlayerMoving(); break;
    default: break;
    }
}

UGuLiResourceWorldSubsystem* AGuLiMiningVehiclePawn::GetResourceSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>() : nullptr;
}

bool AGuLiMiningVehiclePawn::BeginMoveTo(const FVector& Target, const float AcceptanceRadius)
{
	if (!GetController()) SpawnDefaultController();
	return FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginMove(Target, AcceptanceRadius);
}

void AGuLiMiningVehiclePawn::SetEngineeringPresentationVisible(bool bVisible)
{
	if (AActor* Child = VehiclePresentation->GetChildActor()) Child->SetActorHiddenInGame(!bVisible);
}

void AGuLiMiningVehiclePawn::TickMovingToCluster()
{
	if (!GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->ValidateReservation(*this,MiningSlot))
	{
		FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
		BehaviorResult=EGuLiCommanderWorkResult::OutOfRange; return;
	}
    const auto* Resources = GetResourceSubsystem();
    const auto* Map = Resources ? Resources->GetMapDefinition() : nullptr;
    const auto* Cluster = Map ? Map->FindCluster(TargetClusterId) : nullptr;
    if (!Resources || !Cluster || !Resources->CanTeamMineAt(Team, TargetClusterId) || Resources->IsClusterEmpty(TargetClusterId))
    { BehaviorResult = EGuLiCommanderWorkResult::TargetLost; return; }
    auto* Travel = FindComponentByClass<UGuLiEngineeringTravelComponent>();
    if (Travel->GetMoveStatus() == EGuLiEngineeringMoveStatus::Failed)
    { BehaviorResult = EGuLiCommanderWorkResult::Failed; return; }
    if (Travel->GetMoveStatus() != EGuLiEngineeringMoveStatus::Arrived) return;
    FTransform Pose;
    if (!GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->GetSlotPose(*this,MiningSlot,Pose))
    { BehaviorResult = EGuLiCommanderWorkResult::TargetLost; return; }
    if (!IsAtReservedMiningSlot())
    { BehaviorResult = EGuLiCommanderWorkResult::Failed; return; }
    SetActorRotation(Pose.Rotator());
    if (SelectMiningTarget()) BehaviorResult = EGuLiCommanderWorkResult::CanMine;
    else if (!bWaitingForMiningSlot) BehaviorResult = EGuLiCommanderWorkResult::OutOfRange;
}

bool AGuLiMiningVehiclePawn::SelectMiningTarget()
{
    auto* Manager = GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>();
    const auto Status = Manager->AssignCoveredNode(*this,MiningSlot,GetActorTransform(),TargetNodeId,MiningTarget);
    if (Status == EGuLiWorkPositionAvailability::Available) return true;
    SetMiningVisual(false);
    if (Status == EGuLiWorkPositionAvailability::Occupied)
    {
        // Reuse the tree's existing 5 Hz position-wait state, retaining this parked slot.
        bWaitingForMiningSlot = true;
        TaskState = EGuLiMiningTaskState::MovingToCluster;
        BehaviorResult = EGuLiCommanderWorkResult::WaitingPosition;
        NextAutoRetryServerTime = GetWorld()->GetTimeSeconds()+.2f;
        MiningAccumulator = 0;
    }
    return false;
}

bool AGuLiMiningVehiclePawn::IsAtReservedMiningSlot() const
{
    FTransform Pose;
    return GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->GetSlotPose(*this,MiningSlot,Pose)
        && FVector::DistSquared2D(GetActorLocation(),Pose.GetLocation()) <= FMath::Square(GULI_MINING_SLOT_WORK_TOLERANCE_CM);
}

bool AGuLiMiningVehiclePawn::CanMineTargetFrom(const FVector& Target, const FTransform& Pose) const
{
    if (Target.ContainsNaN() || !Pose.IsValid()) return false;
    FVector Left, Right;
    if (!MiningPresentation->CalculateMuzzles(Target, Pose, Left, Right)) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiMiningLineOfSight), true, this);
    Params.AddIgnoredActor(VehiclePresentation->GetChildActor());
    GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->IgnoreVehicles(Params);
    GetResourceSubsystem()->IgnoreMiningNavigationProxies(Params);
    for (const FVector& Muzzle : {Left, Right})
    {
        // Distance does not limit extraction. The reserved slot gates work; beams reach the target.
        if (GetWorld()->LineTraceTestByChannel(Muzzle, Target, ECC_Visibility, Params)) return false;
    }
    return true;
}

void AGuLiMiningVehiclePawn::SetMiningVisual(const bool bActive)
{
    const uint32 Node = bActive ? TargetNodeId : 0;
    const FVector Target = bActive ? MiningTarget : FVector::ZeroVector;
    if (MiningVisual.bActive == bActive && MiningVisual.NodeId == Node && FVector(MiningVisual.Target).Equals(Target, 0.05f)) return;
    MiningVisual.bActive = bActive;
    MiningVisual.NodeId = Node;
    MiningVisual.Target = Target;
    OnRep_MiningVisual();
	SetEngineeringPresentationVisible(!FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit());
    ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::TickMining(const float DeltaSeconds)
{
    UGuLiResourceWorldSubsystem* Resources = GetResourceSubsystem();
    if (!Resources->CanTeamMineAt(Team, TargetClusterId))
    { BehaviorResult = EGuLiCommanderWorkResult::TargetLost; return; }
    if (Resources->IsClusterEmpty(TargetClusterId))
    { SetMiningVisual(false); BehaviorResult = EGuLiCommanderWorkResult::Depleted; return; }
    if (!IsAtReservedMiningSlot())
    {
        SetMiningVisual(false); MiningAccumulator = 0;
        BehaviorResult = EGuLiCommanderWorkResult::Failed; return;
    }
    if (!SelectMiningTarget())
    {
        if (!bWaitingForMiningSlot) BehaviorResult = EGuLiCommanderWorkResult::OutOfRange;
        return;
    }
    SetMiningVisual(true);
    MiningAccumulator += DeltaSeconds * MiningRatePerSecond;
    while (MiningAccumulator >= 1.0f && GetCargoTotal() < CargoCapacity)
    {
        EGuLiResourceType Type;
        if (!Resources->MineNodeRaw(Team, TargetClusterId, TargetNodeId, Type))
        { SetMiningVisual(false); BehaviorResult = EGuLiCommanderWorkResult::TargetLost; return; }
        Cargo.Add(Type, 1);
        MiningAccumulator -= 1.0f;
        ForceNetUpdate();
        if (GetCargoTotal() >= CargoCapacity)
        { SetMiningVisual(false); BehaviorResult = EGuLiCommanderWorkResult::Full; return; }
        if (Resources->IsClusterEmpty(TargetClusterId))
        { SetMiningVisual(false); BehaviorResult = EGuLiCommanderWorkResult::Depleted; return; }
        if (!SelectMiningTarget())
        {
            if (!bWaitingForMiningSlot) BehaviorResult = EGuLiCommanderWorkResult::OutOfRange;
            return;
        }
        SetMiningVisual(true);
    }
}

void AGuLiMiningVehiclePawn::TickReturningToFactory()
{
	if (!IsValid(Factory) || Factory->GetTeam() != Team
		|| !Factory->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->IsCompleted())
	{ BehaviorResult = EGuLiCommanderWorkResult::FactoryLost; return; }
	const auto Status = FindComponentByClass<UGuLiEngineeringTravelComponent>()->GetMoveStatus();
	if (Status == EGuLiEngineeringMoveStatus::Arrived) BehaviorResult = EGuLiCommanderWorkResult::AtFactory;
	else if (Status == EGuLiEngineeringMoveStatus::Failed) BehaviorResult = EGuLiCommanderWorkResult::Failed;
}

void AGuLiMiningVehiclePawn::TickDocking(const float DeltaSeconds)
{
    if (!IsValid(Factory) || Factory->GetTeam() != Team
		|| !Factory->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->IsCompleted())
	{ BehaviorResult = EGuLiCommanderWorkResult::FactoryLost; return; }
    DockingAccumulator += DeltaSeconds;
    if (DockingAccumulator < DockingSeconds) return;
    if (GetCargoTotal() > 0 && Factory->GetTeam() == Team)
    {
        if (!Factory->UploadCargo(*this, Cargo)) { BehaviorResult = EGuLiCommanderWorkResult::FactoryLost; return; }
        Cargo = FGuLiResourceAmounts{};
    }
    BehaviorResult = EGuLiCommanderWorkResult::Unloaded;
}

void AGuLiMiningVehiclePawn::TickPlayerMoving()
{
    const auto Status=FindComponentByClass<UGuLiEngineeringTravelComponent>()->GetMoveStatus();
    if (Status==EGuLiEngineeringMoveStatus::Arrived) BeginGrace(true);
    else if (Status==EGuLiEngineeringMoveStatus::Failed) BeginGrace(false);
}

bool AGuLiMiningVehiclePawn::IssuePlayerCommand(const FGuLiMiningCommand& Command, EGuLiTeam RequestingTeam)
{
	auto* Resources = GetResourceSubsystem();
	if (!HasAuthority() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(this) || !Resources || !Resources->IsRuntimeReady()
		|| RequestingTeam != Team || !Command.IsWellFormed()) return false;
	if (Command.RequestId == LastCompatibilityRequestId) return true;
	if (LastCompatibilityRequestId && int32(Command.RequestId - LastCompatibilityRequestId) <= 0) return false;
	if (Command.Type == EGuLiMiningOrderType::MineCluster && !Resources->CanTeamMineAt(Team, Command.ClusterId)) return false;
	if (Command.Type == EGuLiMiningOrderType::Move && !Resources->GetPlayableBounds().IsInside(FVector2D(Command.Target))) return false;
	FGuLiUnitTaskCommand Task;
	Task.CommandId = Command.RequestId; Task.SelectionRevision = Command.SelectionRevision;
	Task.Target = Command.Target; Task.ClusterId = Command.ClusterId;
	switch (Command.Type)
	{
	case EGuLiMiningOrderType::MineCluster: Task.Kind = EGuLiUnitTaskKind::Special; Task.SpecialTaskId = 1; break;
	case EGuLiMiningOrderType::ReturnToFactory: Task.Kind = EGuLiUnitTaskKind::ReturnToFactory; break;
	case EGuLiMiningOrderType::Cancel: Task.Disposition = EGuLiTaskDisposition::Stop; break;
	default: break;
	}
	if (!GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->SubmitActorCommand(*this, Task)) return false;
	LastCompatibilityRequestId = Command.RequestId; ActivePlayerRequestId = Command.RequestId;
	ForceNetUpdate(); return true;
}

EGuLiTransitOrderResult AGuLiMiningVehiclePawn::IssueStrongholdTransit(
	const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam)
{
	using Result = EGuLiTransitOrderResult;
	if (!HasAuthority() || RequestingTeam != Team) return Result::Unauthorized;
	if (!Order.IsWellFormed()) return Result::InvalidRequest;
	if (uint32(Order.RequestId) == LastCompatibilityRequestId) return LastTransitResult;
	if (LastCompatibilityRequestId && int32(uint32(Order.RequestId)-LastCompatibilityRequestId) <= 0) return Result::StaleRequest;
	FGuLiPreparedTransit Prepared;
	LastTransitResult = FindComponentByClass<UGuLiEngineeringTravelComponent>()->PrepareTransport(Order, Prepared);
	if (LastTransitResult != Result::Accepted) return LastTransitResult;
	FGuLiUnitTaskCommand Task; Task.CommandId = uint32(Order.RequestId); Task.SelectionRevision = uint32(Order.SelectionRevision);
	Task.Kind = EGuLiUnitTaskKind::Transit; Task.TerritoryId = Order.TerritoryId; Task.Target = Order.ClickLocation;
	if (!GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->SubmitActorCommand(*this, Task)) return Result::InvalidRequest;
	LastCompatibilityRequestId = uint32(Order.RequestId); ActivePlayerRequestId = uint32(Order.RequestId);
	LastTransitResult = Result::Accepted;
	ForceNetUpdate(); return LastTransitResult;
}

bool AGuLiMiningVehiclePawn::IssuePlayerCommandByValue(
	const int32 RequestId,
	const EGuLiMiningOrderType Type,
	const FVector Target,
	const int32 ClusterId,
	const int32 SelectionRevision,
	const EGuLiTeam RequestingTeam)
{
	if (RequestId <= 0 || ClusterId < 0 || ClusterId > MAX_uint16 || SelectionRevision <= 0)
	{
		return false;
	}
	FGuLiMiningCommand Command;
	Command.RequestId = static_cast<uint32>(RequestId);
	Command.Type = Type;
	Command.Target = Target;
	Command.ClusterId = static_cast<uint16>(ClusterId);
	Command.SelectionRevision = static_cast<uint32>(SelectionRevision);
	return IssuePlayerCommand(Command, RequestingTeam);
}

void AGuLiMiningVehiclePawn::ForceAutomaticControl()
{
	// Kept for old callers. Automatic behavior and persistent Stop belong to the tree's task owner.
	if (HasAuthority() && !bTaskManaged)
		GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->RegisterActor(*this);
}

void AGuLiMiningVehiclePawn::CancelTaskForExternalDisplacement()
{
	if (!HasAuthority()) return;
	BeginGrace();
}

void AGuLiMiningVehiclePawn::BeginGrace(bool bSuccess)
{
	FinishCurrentTarget();
	ReturnFactories.Reset(); NextReturnFactory = 0; UnloadPointAttempts = 0;
	if (AAIController* AIController = Cast<AAIController>(GetController())) AIController->StopMovement();
	ControlMode = bTaskManaged ? EGuLiMiningControlMode::PlayerOrder : EGuLiMiningControlMode::Grace;
	TaskState = EGuLiMiningTaskState::Idle;
	GraceEndServerTime = bTaskManaged ? 0 : GetWorld()->GetTimeSeconds() + GraceSeconds;
	if (bTaskManaged) { bManagedTaskComplete = true; bManagedTaskFailed = !bSuccess; }
	BehaviorResult = bSuccess ? EGuLiCommanderWorkResult::Complete : EGuLiCommanderWorkResult::Failed;
	bManualReturnOrder = false;
	ForceNetUpdate();
}

bool AGuLiMiningVehiclePawn::StartManagedTask(const FGuLiMiningCommand& Command, bool bAutomatic)
{
    ++MiningTaskVersion;
    if (!HasAuthority() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)) return false;
    bTaskManaged = true; bManagedTaskComplete = false; bManagedTaskFailed = false;
    FinishCurrentTarget();
    ControlMode = bAutomatic ? EGuLiMiningControlMode::Auto : EGuLiMiningControlMode::PlayerOrder;
    TaskState = EGuLiMiningTaskState::Idle; BehaviorResult = EGuLiCommanderWorkResult::None;
    GraceEndServerTime = 0; NextAutoRetryServerTime = 0;
    bManualReturnOrder = Command.Type == EGuLiMiningOrderType::ReturnToFactory;
    if (!bAutomatic) ActivePlayerRequestId = Command.RequestId;
    if (bManualReturnOrder && GetCargoTotal() == 0) { BeginGrace(true); return true; }
    if (Command.Type == EGuLiMiningOrderType::Move)
    {
        PlayerMoveTarget = Command.Target;
        if (!BeginMoveTo(PlayerMoveTarget, 100)) { BeginGrace(false); return true; }
        TaskState = EGuLiMiningTaskState::PlayerMoving; BehaviorResult = EGuLiCommanderWorkResult::Running;
        ForceNetUpdate();
    }
    return true;
}
bool AGuLiMiningVehiclePawn::StopManagedTask()
{
	if (FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit()) return false;
	BeginGrace(true);
	FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
	return true;
}

bool AGuLiMiningVehiclePawn::StartPreparedManagedMove(
	const FGuLiMiningCommand& Command, const FGuLiPreparedGroundMove& Prepared)
{
	if (!HasAuthority() || Command.Type != EGuLiMiningOrderType::Move
		|| UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)) return false;
	FinishCurrentTarget();
	++MiningTaskVersion;
	if (!FindComponentByClass<UGuLiEngineeringTravelComponent>()->CommitGroundMove(Prepared)) return false;
	bTaskManaged = true; bManagedTaskComplete = false; bManagedTaskFailed = false;
	bManualReturnOrder = false;
	ControlMode = EGuLiMiningControlMode::PlayerOrder;
	GraceEndServerTime = 0; ActivePlayerRequestId = Command.RequestId;
	PlayerMoveTarget = Command.Target; TaskState = EGuLiMiningTaskState::PlayerMoving; BehaviorResult = EGuLiCommanderWorkResult::Running;
	ForceNetUpdate();
	return true;
}

void AGuLiMiningVehiclePawn::FinishCurrentTarget()
{
    GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->ReleaseSlot(*this,MiningSlot);
    MiningSlot = {}; bWaitingForMiningSlot = false;
    FindComponentByClass<UGuLiEngineeringTravelComponent>()->CancelGroundMove();
    SetMiningVisual(false);
    TargetNodeId = 0;
	GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->ReleaseNode(*this);
	TargetClusterId = 0u;
}

float AGuLiMiningVehiclePawn::GetGraceSecondsRemaining() const
{
	if (ControlMode != EGuLiMiningControlMode::Grace || GraceEndServerTime <= 0.0f) return 0.0f;
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float Now = State ? State->GetServerWorldTimeSeconds()
		: (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
	return FMath::Max(0.0f, GraceEndServerTime - Now);
}

FGuLiMiningVehiclePrivateState AGuLiMiningVehiclePawn::MakePrivateState() const
{
	FGuLiMiningVehiclePrivateState State;
	State.StableActorId = StableActorId;
	State.Cargo = Cargo;
	State.ControlMode = ControlMode;
	State.TaskState = TaskState;
	State.TargetClusterId = TargetClusterId;
	State.GraceEndServerTime = GraceEndServerTime;
	State.ActivePlayerRequestId = ActivePlayerRequestId;
	return State;
}

void AGuLiMiningVehiclePawn::OnRep_Presentation()
{
    check(PresentationDefinition.ActorClass);
    VehiclePresentation->SetRelativeScale3D(FVector(PresentationDefinition.Scale));
    VehiclePresentation->SetRelativeLocation(FVector(-17.62378f * PresentationDefinition.Scale, 0,
        -GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 3.2254f * PresentationDefinition.Scale));
    VehiclePresentation->SetChildActorClass(PresentationDefinition.ActorClass);
    const bool bInitialized = MiningPresentation->InitializePresentation(VehiclePresentation->GetChildActor());
    checkf(bInitialized, TEXT("Mining presentation must provide both collectors, muzzles and StartMiningAt/StopMining."));
    TravelBounds = FBox(ForceInit);
    TInlineComponentArray<UMeshComponent*> Meshes(VehiclePresentation->GetChildActor());
    for (UMeshComponent* VisualMesh : Meshes)
    {
        GuLiUnitRenderPolicy::Apply(*VisualMesh);
        TravelBounds += VisualMesh->CalcBounds(VisualMesh->GetComponentTransform().GetRelativeTransform(GetActorTransform())).GetBox();
    }
    OnRep_MiningVisual();
    SetEngineeringPresentationVisible(!FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit());
}

void AGuLiMiningVehiclePawn::OnRep_MiningVisual()
{
    if (MiningPresentation->IsReady()) MiningPresentation->ApplyMining(MiningVisual.bActive, MiningVisual.Target);
}

void AGuLiMiningVehiclePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        FinishCurrentTarget();
        if (IsValid(Factory)) Factory->UnregisterMiningVehicle(*this);
    }
    GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->UnregisterVehicle(*this);
    Super::EndPlay(EndPlayReason);
}

void AGuLiMiningVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiMiningVehiclePawn, Team);
	DOREPLIFETIME(AGuLiMiningVehiclePawn, LastTransitResult);
	DOREPLIFETIME(AGuLiMiningVehiclePawn, UnitTypeId);
    DOREPLIFETIME(AGuLiMiningVehiclePawn, StableActorId);
    DOREPLIFETIME(AGuLiMiningVehiclePawn, MiningVisual);
    DOREPLIFETIME_CONDITION(AGuLiMiningVehiclePawn, PresentationDefinition, COND_InitialOnly);
}

bool AGuLiMiningVehiclePawn::FindReachableMiningApproach(uint32 NodeId, FVector& OutApproach, float& OutPathLength) const
{
    // Legacy diagnostic: only reports the already reserved position; never performs an unbudgeted query.
    FTransform Pose;
    if (NodeId != TargetNodeId || !GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->GetSlotPose(*this,MiningSlot,Pose)) return false;
    OutApproach = Pose.GetLocation();
    OutPathLength = FVector::Dist2D(GetActorLocation(),OutApproach);
    return true;
}
