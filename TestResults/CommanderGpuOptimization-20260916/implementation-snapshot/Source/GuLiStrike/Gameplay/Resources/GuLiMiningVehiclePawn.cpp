#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
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
	GetCapsuleComponent()->InitCapsuleSize(650.0f, 650.0f);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
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
	GetCharacterMovement()->MaxWalkSpeed = 4500.0f;
	GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
	GetCharacterMovement()->NavAgentProps.AgentRadius = GULI_RESOURCE_MINING_VEHICLE_NAV_RADIUS_CM;
	GetCharacterMovement()->NavAgentProps.AgentHeight = 144.0f;
}

void AGuLiMiningVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(HasAuthority());
	GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->RegisterVehicle(*this);
	FindComponentByClass<UGuLiEngineeringTravelComponent>()->OnTransportStarted.AddWeakLambda(this, [this]() { SetMiningVisual(false); });
	FindComponentByClass<UGuLiEngineeringTravelComponent>()->OnTransportEnded.AddWeakLambda(this, [this]()
	{
		if (bPendingAutomatic) { bPendingAutomatic = false; ForceAutomaticControl(); }
		else BeginGrace();
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
	MiningDistanceCentimeters = Config.MiningDistanceCentimeters;
	MiningRatePerSecond = Config.MiningRatePerSecond;
	CargoCapacity = Config.CargoCapacity;
	DockingSeconds = Config.DockingSeconds;
	GraceSeconds = Config.PlayerOrderGraceSeconds;
	AutoRetrySeconds = Config.AutoRetrySeconds;
	GetCharacterMovement()->MaxWalkSpeed = SpeedCentimetersPerSecond;
    PresentationDefinition.ActorClass = Unit.PresentationClass;
    PresentationDefinition.Scale = Unit.PresentationScale;
    ManeuverSpeed = Config.FactoryManeuverSpeedCentimetersPerSecond;
    OnRep_Presentation();
    InFactory.RegisterMiningVehicle(*this);
	if (HasAuthority() && !GetController()) SpawnDefaultController();
	if (HasAuthority()) CastChecked<AGuLiEngineeringAIController>(GetController())->ConfigureVehicleNavigation();
	ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)) return;
	if (FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsRouting()) return;
	UGuLiResourceWorldSubsystem* Resources = GetResourceSubsystem();
	if (!Resources || !Resources->IsRuntimeReady()) return;

	if (ControlMode == EGuLiMiningControlMode::Grace)
	{
		if (GetWorld()->GetTimeSeconds() >= GraceEndServerTime) ForceAutomaticControl();
		return;
	}
	switch (TaskState)
	{
	case EGuLiMiningTaskState::Idle: TickAutomatic(); break;
	case EGuLiMiningTaskState::MovingToCluster: TickMovingToCluster(); break;
	case EGuLiMiningTaskState::Mining: TickMining(DeltaSeconds); break;
	case EGuLiMiningTaskState::ReturningToFactory: TickReturningToFactory(); break;
    case EGuLiMiningTaskState::WaitingForFactoryDoor:
    case EGuLiMiningTaskState::EnteringFactory:
    case EGuLiMiningTaskState::TurningInFactory:
    case EGuLiMiningTaskState::ExitingFactory: TickFactoryManeuver(DeltaSeconds); break;
    case EGuLiMiningTaskState::Docking: TickDocking(DeltaSeconds); break;
	case EGuLiMiningTaskState::PlayerMoving: TickPlayerMoving(); break;
	default:
		if (ControlMode == EGuLiMiningControlMode::PlayerOrder) BeginGrace();
		else { TaskState = EGuLiMiningTaskState::Idle; NextAutoRetryServerTime = GetWorld()->GetTimeSeconds() + AutoRetrySeconds; }
		break;
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

void AGuLiMiningVehiclePawn::TickAutomatic()
{
	if (ControlMode != EGuLiMiningControlMode::Auto) return;
	if (GetCargoTotal() > 0)
	{
		BeginReturnToFactory(false);
		return;
	}
	if (GetWorld()->GetTimeSeconds() < NextAutoRetryServerTime) return;
	uint16 ClusterId = 0u;
	FVector Center = FVector::ZeroVector;
	float PathLength = 0.0f;
	auto* Manager = GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>();
	if (Manager->AssignNode(*this, 0, ClusterId, TargetNodeId, Center, PathLength))
	{
		TargetClusterId = ClusterId;
		if (BeginMoveTo(Center, 100.0f))
		{
			TaskState = EGuLiMiningTaskState::MovingToCluster;
			ForceNetUpdate();
			return;
		}
		FinishCurrentTarget();
	}
	NextAutoRetryServerTime = GetWorld()->GetTimeSeconds() + AutoRetrySeconds;
}

void AGuLiMiningVehiclePawn::TickMovingToCluster()
{
	UGuLiResourceWorldSubsystem* Resources = GetResourceSubsystem();
	const UGuLiResourceMapDefinition* Map = Resources ? Resources->GetMapDefinition() : nullptr;
	const FGuLiResourceClusterDefinition* Cluster = Map ? Map->FindCluster(TargetClusterId) : nullptr;
	if (!Resources || !Cluster || !Resources->CanTeamMineAt(Team, TargetClusterId)
		|| Resources->IsClusterEmpty(TargetClusterId))
	{
		if (GetCargoTotal() > 0) BeginReturnToFactory(false);
		else if (ControlMode == EGuLiMiningControlMode::PlayerOrder) BeginGrace();
		else { FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle; }
		return;
	}
	if (SelectMiningTarget() && CanMineTargetFrom(MiningTarget, GetActorTransform()))
	{
		if (AAIController* AIController = Cast<AAIController>(GetController())) AIController->StopMovement();
		TaskState = EGuLiMiningTaskState::Mining;
		MiningAccumulator = 0.0f;
        SetMiningVisual(true);
		ForceNetUpdate();
		return;
	}
	if (const AAIController* AIController = Cast<AAIController>(GetController());
		AIController && AIController->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		if (ControlMode == EGuLiMiningControlMode::PlayerOrder) BeginGrace();
		else { FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle; NextAutoRetryServerTime = GetWorld()->GetTimeSeconds() + AutoRetrySeconds; }
	}
}

bool AGuLiMiningVehiclePawn::SelectMiningTarget()
{
    auto* Resources = GetResourceSubsystem();
    auto* Manager = GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>();
    FVector Target;
    if (!Manager->OwnsNode(*this, TargetNodeId) || !Resources->GetNodeMiningTarget(TargetNodeId, Target))
    {
        SetMiningVisual(false);
        FVector Approach; float PathLength;
        if (!Manager->AssignNode(*this, TargetClusterId, TargetClusterId, TargetNodeId, Approach, PathLength)) return false;
        if (!Resources->GetNodeMiningTarget(TargetNodeId, Target)) return false;
    }
    MiningTarget = Target;
    return true;
}

bool AGuLiMiningVehiclePawn::CanMineTargetFrom(const FVector& Target, const FTransform& Pose) const
{
    FVector Left, Right;
    if (!MiningPresentation->CalculateMuzzles(Target, Pose, Left, Right)) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiMiningLineOfSight), true, this);
    Params.AddIgnoredActor(VehiclePresentation->GetChildActor());
    GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->IgnoreVehicles(Params);
    GetResourceSubsystem()->IgnoreMiningNavigationProxies(Params);
    for (const FVector& Muzzle : {Left, Right})
    {
        if (FVector::DistSquared(Muzzle, Target) > FMath::Square(MiningDistanceCentimeters)) return false;
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
    if (!Resources->CanTeamMineAt(Team, TargetClusterId) || !SelectMiningTarget())
    {
        if (GetCargoTotal() > 0) BeginReturnToFactory(false);
        else if (ControlMode == EGuLiMiningControlMode::PlayerOrder) BeginGrace();
        else { FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle; }
        return;
    }
    if (!CanMineTargetFrom(MiningTarget, GetActorTransform()))
    {
        SetMiningVisual(false);
        MiningAccumulator = 0;
        FVector Approach;
        float Length;
        if (FindReachableMiningApproach(TargetNodeId, Approach, Length)
            && BeginMoveTo(Approach, 100.0f)) TaskState = EGuLiMiningTaskState::MovingToCluster;
        else if (ControlMode == EGuLiMiningControlMode::PlayerOrder) BeginGrace();
        else { FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle; NextAutoRetryServerTime = GetWorld()->GetTimeSeconds() + AutoRetrySeconds; }
        return;
    }
    SetMiningVisual(true);
    MiningAccumulator += DeltaSeconds * MiningRatePerSecond;
    while (MiningAccumulator >= 1.0f && GetCargoTotal() < CargoCapacity)
    {
        EGuLiResourceType Type;
        if (!Resources->MineNodeRaw(Team, TargetClusterId, TargetNodeId, Type)) break;
        Cargo.Add(Type, 1);
        MiningAccumulator -= 1.0f;
        ForceNetUpdate();
        if (!SelectMiningTarget() || !CanMineTargetFrom(MiningTarget, GetActorTransform())) break;
        SetMiningVisual(true);
    }
    if (GetCargoTotal() >= CargoCapacity || Resources->IsClusterEmpty(TargetClusterId)) BeginReturnToFactory(false);
    else if (!CanMineTargetFrom(MiningTarget, GetActorTransform())) SetMiningVisual(false);
}

void AGuLiMiningVehiclePawn::BeginReturnToFactory(const bool bFromManualOrder)
{
    FinishCurrentTarget();
	AGuLiResourceFactoryActor* Destination = GetResourceSubsystem()->FindNearestFriendlyFactory(Team, GetActorLocation());
	if (Destination != Factory)
	{
		if (IsValid(Factory)) { Factory->ReleaseDock(*this); Factory->UnregisterMiningVehicle(*this); }
		Factory = Destination;
		if (Factory) Factory->RegisterMiningVehicle(*this);
	}
    bManualReturnOrder = bFromManualOrder || ControlMode == EGuLiMiningControlMode::PlayerOrder;
    if (!IsValid(Factory) || !BeginMoveTo(
        Factory->GetActorTransform().TransformPosition(Factory->GetDockRoute().Entry), 100.0f))
    {
        if (ControlMode == EGuLiMiningControlMode::PlayerOrder) BeginGrace();
        else { TaskState = EGuLiMiningTaskState::Blocked; NextAutoRetryServerTime = GetWorld()->GetTimeSeconds() + AutoRetrySeconds; }
        return;
    }
    TaskState = EGuLiMiningTaskState::ReturningToFactory;
    ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::TickReturningToFactory()
{
    if (!IsValid(Factory) || Factory->GetTeam() != Team) { BeginReturnToFactory(false); return; }
    const FVector Entry = Factory->GetActorTransform().TransformPosition(Factory->GetDockRoute().Entry);
    if (FVector::Dist2D(GetActorLocation(), Entry) <= 100.0f + GULI_RESOURCE_MINING_VEHICLE_NAV_RADIUS_CM + 50.0f)
    {
        CastChecked<AAIController>(GetController())->StopMovement();
        TaskState = EGuLiMiningTaskState::WaitingForFactoryDoor;
        ForceNetUpdate();
    }
    else if (CastChecked<AAIController>(GetController())->GetMoveStatus() == EPathFollowingStatus::Idle)
    {
        if (ControlMode == EGuLiMiningControlMode::PlayerOrder) BeginGrace();
        else TaskState = EGuLiMiningTaskState::Blocked;
    }
}

bool AGuLiMiningVehiclePawn::IsFactoryManeuverActive() const
{
    return TaskState == EGuLiMiningTaskState::WaitingForFactoryDoor || TaskState == EGuLiMiningTaskState::EnteringFactory
        || TaskState == EGuLiMiningTaskState::Docking || TaskState == EGuLiMiningTaskState::TurningInFactory
        || TaskState == EGuLiMiningTaskState::ExitingFactory;
}

bool AGuLiMiningVehiclePawn::CalculateFactoryGroundPose(const FVector& Position, const float Yaw, FTransform& OutPose) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiFactoryGround), false, this);
    Params.AddIgnoredActor(Factory);
    Params.AddIgnoredActor(VehiclePresentation->GetChildActor());
    GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->IgnoreVehicles(Params);
    const float BaseZ = Factory->GetActorLocation().Z;
    auto GroundAt = [&](const FVector& Point, FHitResult& Hit)
    {
        return GetWorld()->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, BaseZ + 500),
            FVector(Point.X, Point.Y, BaseZ - 5000), ECC_Visibility, Params);
    };
    const FVector Center = TravelBounds.GetCenter();
    const FVector Extent = TravelBounds.GetExtent();
    const FQuat FlatRotation = FRotator(0, Yaw, 0).Quaternion();
    float Heights[3][3];
    for (int32 X = 0; X < 3; ++X)
        for (int32 Y = 0; Y < 3; ++Y)
        {
            FHitResult Hit;
            const FVector Offset(Center.X + (X - 1) * Extent.X, Center.Y + (Y - 1) * Extent.Y, 0);
            if (!GroundAt(Position + FlatRotation.RotateVector(Offset), Hit)) return false;
            Heights[X][Y] = Hit.ImpactPoint.Z;
        }
    const float ForwardSlope = (Heights[2][1] - Heights[0][1]) / (2 * Extent.X);
    const float SideSlope = (Heights[1][2] - Heights[1][0]) / (2 * Extent.Y);
    const FVector Up = FlatRotation.RotateVector(FVector(-ForwardSlope, -SideSlope, 1).GetSafeNormal());
    const FQuat Rotation = FRotationMatrix::MakeFromZX(Up, FlatRotation.GetAxisX()).ToQuat();
    // Support the full model on the sampled surface, including the apron-to-floor transition.
    float Height = -TNumericLimits<float>::Max();
    for (int32 X = 0; X < 3; ++X)
        for (int32 Y = 0; Y < 3; ++Y)
        {
            const FVector Offset = Rotation.RotateVector(FVector(
                Center.X + (X - 1) * Extent.X, Center.Y + (Y - 1) * Extent.Y, TravelBounds.Min.Z));
            FHitResult Hit;
            if (!GroundAt(Position + Offset, Hit)) return false;
            Height = FMath::Max(Height, static_cast<float>(Hit.ImpactPoint.Z - Offset.Z));
        }
    OutPose = FTransform(Rotation, FVector(Position.X, Position.Y, Height + 3.0f));
    // A thin sweep covers the complete footprint, including convex seams between sampled points.
    const FVector Sole = OutPose.TransformPosition(FVector(Center.X, Center.Y, TravelBounds.Min.Z + 1.0f));
    FHitResult Support;
    if (!GetWorld()->SweepSingleByChannel(Support, Sole + FVector(0,0,50), Sole - FVector(0,0,50),
        Rotation, ECC_Visibility, FCollisionShape::MakeBox(FVector(Extent.X, Extent.Y, 1.0f)), Params)
        || Support.bStartPenetrating || Support.ImpactNormal.Z < GetCharacterMovement()->GetWalkableFloorZ()) return false;
    OutPose.AddToTranslation(FVector(0,0,Support.Location.Z - Sole.Z + 3.0f));
    return true;
}

bool AGuLiMiningVehiclePawn::MoveFactoryStep(const FVector& LocalTarget, const float LocalYaw, const float DeltaSeconds)
{
    const FTransform FactoryTransform = Factory->GetActorTransform();
    const FVector Goal = FactoryTransform.TransformPosition(LocalTarget);
    const FVector Previous = GetActorLocation();
    const FVector Next = FMath::VInterpConstantTo(Previous, FVector(Goal.X, Goal.Y, Previous.Z), DeltaSeconds, ManeuverSpeed);
    const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw,
        Factory->GetActorRotation().Yaw + LocalYaw, 90.0f * DeltaSeconds);
    FTransform Pose;
    if (!CalculateFactoryGroundPose(Next, Yaw, Pose)) return false;
    const FQuat Rotation = Pose.GetRotation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiFactoryManeuver), false, this);
    Params.AddIgnoredActor(Factory);
    Params.AddIgnoredActor(VehiclePresentation->GetChildActor());
    GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->IgnoreVehicles(Params);
    const FVector NextCenter = Pose.TransformPosition(TravelBounds.GetCenter());
    const FVector PreviousCenter = GetActorTransform().TransformPosition(TravelBounds.GetCenter());
    const FCollisionShape Body = FCollisionShape::MakeBox(TravelBounds.GetExtent());
    if (GetWorld()->SweepTestByChannel(PreviousCenter, NextCenter, Rotation, ECC_Pawn, Body, Params)
        || GetWorld()->OverlapBlockingTestByChannel(NextCenter, Rotation, ECC_Pawn, Body, Params))
    {
        GetCharacterMovement()->Velocity = FVector::ZeroVector;
        return false;
    }
    SetActorTransform(Pose, false);
    GetCharacterMovement()->Velocity = (Pose.GetLocation() - Previous) / DeltaSeconds;
    const bool bReached = FVector::Dist2D(Next, Goal) < 2.0f
        && FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, Factory->GetActorRotation().Yaw + LocalYaw)) < 0.5f;
    if (bReached) GetCharacterMovement()->Velocity = FVector::ZeroVector;
    return bReached;
}

void AGuLiMiningVehiclePawn::TickFactoryManeuver(const float DeltaSeconds)
{
    if (!IsValid(Factory)) { FinishFactoryManeuver(); return; }
    const FGuLiFactoryDockRoute Route = Factory->GetDockRoute();
    if (TaskState == EGuLiMiningTaskState::WaitingForFactoryDoor)
    {
		if (Factory->GetTeam() != Team) { FinishFactoryManeuver(); BeginReturnToFactory(false); return; }
        if (!Factory->TryReserveDock(*this) || Factory->GetDoorAlpha() < 1.0f) return;
        GetCharacterMovement()->StopMovementImmediately();
        FTransform GroundPose;
        if (!CalculateFactoryGroundPose(GetActorLocation(), GetActorRotation().Yaw, GroundPose)) return;
        FCollisionQueryParams Clearance(SCENE_QUERY_STAT(GuLiFactoryEntry), false, this);
        Clearance.AddIgnoredActor(Factory);
        Clearance.AddIgnoredActor(VehiclePresentation->GetChildActor());
    GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->IgnoreVehicles(Clearance);
        if (GetWorld()->OverlapBlockingTestByChannel(GroundPose.TransformPosition(TravelBounds.GetCenter()),
            GroundPose.GetRotation(), ECC_Pawn, FCollisionShape::MakeBox(TravelBounds.GetExtent()), Clearance)) return;
        SetActorTransform(GroundPose, false);
        GetCharacterMovement()->DisableMovement();
        GetCapsuleComponent()->IgnoreActorWhenMoving(Factory, true);
        bDockAligned = false;
        TaskState = EGuLiMiningTaskState::EnteringFactory;
        ForceNetUpdate();
    }
    // Small motion steps also bound the angular gap in the full-vehicle collision sweep.
    float Remaining = DeltaSeconds;
    while (Remaining > 0.0f)
    {
        const float Step = FMath::Min(Remaining, 1.0f / 60.0f);
        Remaining -= Step;
        if (TaskState == EGuLiMiningTaskState::EnteringFactory)
        {
            if (!bDockAligned) { bDockAligned = MoveFactoryStep(Route.Entry, 180, Step); continue; }
            if (MoveFactoryStep(Route.Unload, 180, Step))
            {
                TaskState = EGuLiMiningTaskState::Docking;
                DockingAccumulator = 0;
                ForceNetUpdate();
                break;
            }
        }
        else if (TaskState == EGuLiMiningTaskState::ExitingFactory && MoveFactoryStep(Route.Exit, 0, Step))
        {
            FinishFactoryManeuver();
            break;
        }
    }
}

void AGuLiMiningVehiclePawn::TickDocking(const float DeltaSeconds)
{
    if (!IsValid(Factory)) { FinishFactoryManeuver(); return; }
    DockingAccumulator += DeltaSeconds;
    if (DockingAccumulator < DockingSeconds) return;
    if (GetCargoTotal() > 0 && Factory->GetTeam() == Team)
    {
        if (!Factory->UploadCargo(*this, Cargo)) return;
        Cargo = FGuLiResourceAmounts{};
    }
    FTransform ExitPose;
    if (!CalculateFactoryGroundPose(GetActorLocation(), Factory->GetActorRotation().Yaw, ExitPose)) return;
    SetActorTransform(ExitPose, false);
    GetCharacterMovement()->Velocity = FVector::ZeroVector;
    TaskState = EGuLiMiningTaskState::ExitingFactory;
    ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::FinishFactoryManeuver()
{
    if (IsValid(Factory))
    {
        Factory->ReleaseDock(*this);
        GetCapsuleComponent()->IgnoreActorWhenMoving(Factory, false);
    }
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    TaskState = EGuLiMiningTaskState::Idle;
    if (PendingCommand.IsSet())
    {
        const FPendingEngineeringCommand Pending = PendingCommand.GetValue();
        PendingCommand.Reset();
        if (!Pending.Transit.TerritoryId.IsNone())
        {
            FGuLiPreparedTransit Prepared;
            LastTransitResult = FindComponentByClass<UGuLiEngineeringTravelComponent>()->PrepareTransport(Pending.Transit,Prepared);
            if (LastTransitResult == EGuLiTransitOrderResult::Accepted) ExecuteTransit(Pending.Transit,Prepared);
            else
            {
                UE_LOG(LogTemp, Display, TEXT("StrongholdTransit deferred vehicle=%u result=%s"),StableActorId.Value,*GuLiEngineeringCommands::Describe(LastTransitResult));
                BeginGrace();
            }
        }
        else if (Pending.Mining.Type == EGuLiMiningOrderType::MineCluster && !GetResourceSubsystem()->CanTeamMineAt(Team, Pending.Mining.ClusterId)) BeginGrace();
        else ExecutePlayerCommand(Pending.Mining);
    }
    else if (GetCargoTotal() > 0 && (!IsValid(Factory) || Factory->GetTeam() != Team)) BeginReturnToFactory(false);
    else if (bPendingAutomatic) { bPendingAutomatic = false; ForceAutomaticControl(); }
    else if (ControlMode == EGuLiMiningControlMode::PlayerOrder || bManualReturnOrder) BeginGrace();
    else bManualReturnOrder = false;
    ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::TickPlayerMoving()
{
	if (FVector::Dist2D(GetActorLocation(), PlayerMoveTarget) <= 600.0f)
	{
		BeginGrace();
		return;
	}
	if (const AAIController* AIController = Cast<AAIController>(GetController());
		AIController && AIController->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		BeginGrace();
	}
}

bool AGuLiMiningVehiclePawn::IssuePlayerCommand(
	const FGuLiMiningCommand& Command,
	const EGuLiTeam RequestingTeam)
{
	UGuLiResourceWorldSubsystem* Resources = GetResourceSubsystem();
	if (!HasAuthority() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(this) || !Resources || !Resources->IsRuntimeReady()
		|| RequestingTeam != Team || !Command.IsWellFormed())
	{
		return false;
	}
	if (Command.RequestId == ActivePlayerRequestId) return true;
	if (ActivePlayerRequestId != 0u
		&& static_cast<int32>(Command.RequestId - ActivePlayerRequestId) <= 0) return false;
	if (Command.Type == EGuLiMiningOrderType::MineCluster
		&& !Resources->CanTeamMineAt(Team, Command.ClusterId))
	{
		return false;
	}
	const FBox2D Bounds = Resources->GetPlayableBounds();
	if (Command.Type == EGuLiMiningOrderType::Move
		&& !Bounds.IsInside(FVector2D(Command.Target)))
	{
		return false;
	}

    ActivePlayerRequestId = Command.RequestId;
    if (IsFactoryManeuverActive())
    {
        PendingCommand.Emplace(Command);
        bPendingAutomatic = false;
        ForceNetUpdate();
    }
    else ExecutePlayerCommand(Command);
    return true;
}

EGuLiTransitOrderResult AGuLiMiningVehiclePawn::IssueStrongholdTransit(
	const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam)
{
	using Result = EGuLiTransitOrderResult;
	if (!HasAuthority() || RequestingTeam != Team) return Result::Unauthorized;
	if (!Order.IsWellFormed()) return Result::InvalidRequest;
	if (uint32(Order.RequestId) == ActivePlayerRequestId) return LastTransitResult;
	if (ActivePlayerRequestId != 0 && int32(uint32(Order.RequestId)-ActivePlayerRequestId) <= 0) return Result::StaleRequest;
	FGuLiPreparedTransit Prepared;
	LastTransitResult = FindComponentByClass<UGuLiEngineeringTravelComponent>()->PrepareTransport(Order,Prepared);
	if (LastTransitResult != Result::Accepted) return LastTransitResult;
	ActivePlayerRequestId = uint32(Order.RequestId);
	if (IsFactoryManeuverActive())
	{
		PendingCommand.Emplace(Order); bPendingAutomatic = false;
		LastTransitResult = Result::DeferredUntilFactoryExit;
	}
	else ExecuteTransit(Order,Prepared);
	ForceNetUpdate();
	return LastTransitResult;
}
void AGuLiMiningVehiclePawn::ExecuteTransit(const FGuLiStrongholdTransitOrder& Order, const FGuLiPreparedTransit& Prepared)
{
	FinishCurrentTarget();
	PendingCommand.Reset(); bPendingAutomatic = false; bManualReturnOrder = false;
	ActivePlayerRequestId = uint32(Order.RequestId);
	ControlMode = EGuLiMiningControlMode::PlayerOrder; TaskState = EGuLiMiningTaskState::Idle;
	GraceEndServerTime = 0;
	FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginTransport(Prepared);
}

void AGuLiMiningVehiclePawn::ExecutePlayerCommand(const FGuLiMiningCommand& Command)
{
	UGuLiResourceWorldSubsystem* Resources = GetResourceSubsystem();
	FinishCurrentTarget();
	if (AAIController* AIController = Cast<AAIController>(GetController())) AIController->StopMovement();
	ControlMode = EGuLiMiningControlMode::PlayerOrder;
	GraceEndServerTime = 0.0f;
	ActivePlayerRequestId = Command.RequestId;
	bManualReturnOrder = false;
	switch (Command.Type)
	{
	case EGuLiMiningOrderType::Move:
		PlayerMoveTarget = Command.Target;
		if (!BeginMoveTo(PlayerMoveTarget, 500.0f)) { BeginGrace(); return; }
		TaskState = EGuLiMiningTaskState::PlayerMoving;
		break;
	case EGuLiMiningOrderType::MineCluster:
		{
			FVector Approach; float PathLength;
			if (!GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->AssignNode(
				*this, Command.ClusterId, TargetClusterId, TargetNodeId, Approach, PathLength)
				|| !BeginMoveTo(Approach, 100.0f)) BeginGrace();
			else TaskState = EGuLiMiningTaskState::MovingToCluster;
		}
		break;
	case EGuLiMiningOrderType::ReturnToFactory:
		BeginReturnToFactory(true);
		break;
	case EGuLiMiningOrderType::Cancel:
		BeginGrace();
		break;
	default:
		return;
	}
	ForceNetUpdate();
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
    if (!HasAuthority()) return;
    if (IsFactoryManeuverActive() || FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsInTransit())
    { PendingCommand.Reset(); bPendingAutomatic = true; return; }
	FinishCurrentTarget();
	if (AAIController* AIController = Cast<AAIController>(GetController())) AIController->StopMovement();
	ControlMode = EGuLiMiningControlMode::Auto;
	TaskState = EGuLiMiningTaskState::Idle;
	GraceEndServerTime = 0.0f;
	bManualReturnOrder = false;
	NextAutoRetryServerTime = 0.0f;
	ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::BeginGrace()
{
	FinishCurrentTarget();
	if (AAIController* AIController = Cast<AAIController>(GetController())) AIController->StopMovement();
	ControlMode = EGuLiMiningControlMode::Grace;
	TaskState = EGuLiMiningTaskState::Idle;
	GraceEndServerTime = GetWorld()->GetTimeSeconds() + GraceSeconds;
	bManualReturnOrder = false;
	ForceNetUpdate();
}

void AGuLiMiningVehiclePawn::FinishCurrentTarget()
{
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
    MiningDistanceCentimeters = 591.6596f * PresentationDefinition.Scale * 3.0f;
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

bool AGuLiMiningVehiclePawn::FindReachableMiningApproach(
    const uint32 NodeId, FVector& OutApproach, float& OutPathLength) const
{
    const auto* Resources = GetResourceSubsystem();
    const auto* Map = Resources->GetMapDefinition();
    FVector Target;
    if (!Resources->GetNodeMiningTarget(NodeId, Target)) return false;
    const auto& Cluster = *Map->FindCluster(Map->Nodes[NodeId - 1].ClusterId);
    if (CanMineTargetFrom(Target, GetActorTransform()))
    {
        OutApproach = GetActorLocation(); OutPathLength = 0; return true;
    }
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Navigation) return false;
    const ANavigationData* NavData = Navigation->GetNavDataForProps(GetNavAgentPropertiesRef(), GetNavAgentLocation());
    if (!NavData) return false;
    const float Radius = Cluster.ObstacleRadiusCentimeters + GetNavAgentPropertiesRef().AgentRadius + 150.0f;
    const FVector Direction = (GetActorLocation() - Cluster.Center).GetSafeNormal2D();
    OutPathLength = TNumericLimits<float>::Max();
    bool bFound = false;
    for (int32 Sample = 0; Sample < 8; ++Sample)
    {
        const FVector Desired = Cluster.Center + Direction.RotateAngleAxis(Sample * 45.0f, FVector::UpVector) * Radius;
        FNavLocation Projected;
        if (!Navigation->ProjectPointToNavigation(Desired, Projected, FVector(400,400,5000), NavData)) continue;
        const FVector VehiclePosition = Projected.Location + FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FTransform Pose(FRotator(0, (Target - VehiclePosition).Rotation().Yaw, 0), VehiclePosition);
        if (!CanMineTargetFrom(Target, Pose)) continue;
        UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
            GetWorld(), GetActorLocation(), Projected.Location, const_cast<AGuLiMiningVehiclePawn*>(this));
        if (Path && Path->IsValid() && !Path->IsPartial() && Path->GetPathLength() < OutPathLength)
        {
            OutPathLength = Path->GetPathLength();
            OutApproach = Projected.Location;
            bFound = true;
        }
    }
    return bFound;
}

