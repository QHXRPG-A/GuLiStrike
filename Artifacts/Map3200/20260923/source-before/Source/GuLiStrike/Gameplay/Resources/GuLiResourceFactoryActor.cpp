#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Gameplay/Building/GuLiBuildingVisuals.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TimelineComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"
#include "Net/UnrealNetwork.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiActorDamageReceiverComponent.h"

AGuLiResourceFactoryActor::AGuLiResourceFactoryActor()
{
	PrimaryActorTick.bCanEverTick = true;
	CreateDefaultSubobject<UGuLiCombatHealthComponent>(TEXT("CombatHealth"));
	Lifecycle = CreateDefaultSubobject<UGuLiBuildingLifecycleComponent>(TEXT("Lifecycle"));
	CreateDefaultSubobject<UGuLiBuildingConstructionVisualComponent>(TEXT("ConstructionVisual"));
	CreateDefaultSubobject<UGuLiActorDamageReceiverComponent>(TEXT("DamageReceiver"))->bDestroyOwnerOnDeath = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FactoryCollision"));
	RootComponent = CollisionBox;
	CollisionBox->SetBoxExtent(FVector(
		GULI_RESOURCE_FACTORY_OBSTACLE_HALF_EXTENT_CM,
		GULI_RESOURCE_FACTORY_OBSTACLE_HALF_EXTENT_CM,
		360.0f));
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBox->SetCollisionObjectType(ECC_WorldStatic);
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionBox->SetCanEverAffectNavigation(true);
	DockPointComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MiningDockPoint"));
	DockPointComponent->SetupAttachment(CollisionBox);
	// The Scale-0.2 factory deck is 23 cm high, with walls outside these four positions.
	const FVector Points[] = {FVector(-100,-60,24), FVector(-100,60,24), FVector(80,-60,24), FVector(80,60,24)};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
	{
		auto* Point = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("UnloadPoint%d"), Index));
		Point->SetupAttachment(CollisionBox);
		Point->SetRelativeLocation(Points[Index]);
		UnloadPoints.Add(Point);
	}
	Presentation = CreateDefaultSubobject<UChildActorComponent>(TEXT("FactoryPresentation"));
	Presentation->SetupAttachment(CollisionBox);
	// Door bones and ramp geometry retain source units beneath this presentation parent.
	Presentation->SetRelativeScale3D(FVector(GuLiBuildingVisuals::FactoryScale));
}

void AGuLiResourceFactoryActor::BeginPlay()
{
	Super::BeginPlay();
	Lifecycle->OnConstructionStateChanged.AddUObject(this, &ThisClass::RefreshConstructionCollision);
	RefreshConstructionCollision();
	OnRep_DoorState();
}

void AGuLiResourceFactoryActor::RefreshConstructionCollision()
{
	if (!Lifecycle->GetState().InstanceId) return;
	const bool bBlockFootprint = Lifecycle->GetState().Phase == EGuLiBuildingPhase::UnderConstruction;
	const auto Collision = bBlockFootprint ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	// On completion the existing shell, deck and ramp provide the walkable interior.
	// Change navigation only at a lifecycle boundary, never for each construction tick.
	// Gift placement temporarily disables the entire Actor. Read the body's own
	// setting so that override cannot hide a still-blocking completed footprint.
	const bool bChanged = CollisionBox->BodyInstance.GetCollisionEnabled(false) != Collision;
	if (bChanged)
	{
		if (GetActorEnableCollision()) CollisionBox->SetCollisionEnabled(Collision);
		else
		{
			// UPrimitiveComponent's setter also checks the Actor override and would
			// skip NoCollision here. Persist the body setting while gift clearance
			// stages the Actor; restoring Actor collision then uses this final setting.
			CollisionBox->BodyInstance.SetCollisionEnabled(Collision);
		}
	}
	if (CollisionBox->CanEverAffectNavigation() != bBlockFootprint) CollisionBox->SetCanEverAffectNavigation(bBlockFootprint);
	if (bChanged && HasAuthority())
		if (auto* Obstacles = GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
			Obstacles->NotifyStaticGeometryChanged(GetComponentsBoundingBox(true, true).ExpandBy(1000));
}

void AGuLiResourceFactoryActor::InitializeFactory(
	const EGuLiTeam InTeam,
	const UGuLiResourceEconomyConfig& Config,
	const FVector& InDockPoint, FGuLiControllableActorId InId, int32 TerritoryIndex, EGuLiBuildingOrigin Origin,
	bool bCompleted, const FGuid& Builder, int32 DefinitionId)
{
	Team = InTeam;
	StableActorId = InId.IsValid() ? InId : FGuLiControllableActorId(InTeam == EGuLiTeam::Red ? 101u : 102u);
	Lifecycle->InitializeBuilding(DefinitionId, TerritoryIndex, Origin, bCompleted, Builder);
	RefreshConstructionCollision();
	DockPoint = InDockPoint;
	OnRep_DockPoint();
	ProcessingRatePerSecond = Config.FactoryProcessingRatePerSecond;
	PresentationClass = Config.FactoryPresentationClass.LoadSynchronous();
	check(PresentationClass);
	OnRep_PresentationClass();
	GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->RegisterFactory(*this);
	ForceNetUpdate();
}

void AGuLiResourceFactoryActor::SetBuildingTeamAuthority(EGuLiTeam NewTeam)
{
	check(HasAuthority()); Team = NewTeam; Lifecycle->RefreshTeam(); ForceNetUpdate();
}

bool AGuLiResourceFactoryActor::EnqueueCargo(const FGuLiResourceAmounts& Cargo)
{
	if (!HasAuthority() || !Lifecycle->IsCompleted() || !GuLiResources::IsPlayableTeam(Team)
		|| Cargo.Blue < 0 || Cargo.Red < 0 || Cargo.Blue + Cargo.Red <= 0)
	{
		return false;
	}
	auto Append = [this](const EGuLiResourceType Type, const int32 Amount)
	{
		if (Amount <= 0) return;
		if (!Queue.IsEmpty() && Queue.Last().ResourceType == Type && Queue.Last().SettlementTeam == Team)
		{
			Queue.Last().Amount += Amount;
		}
		else
		{
			FGuLiFactoryQueueEntry& Entry = Queue.AddDefaulted_GetRef();
			Entry.ResourceType = Type;
			Entry.Amount = Amount;
			Entry.SettlementTeam = Team;
		}
	};
	Append(EGuLiResourceType::Blue, Cargo.Blue);
	Append(EGuLiResourceType::Red, Cargo.Red);
	ForceNetUpdate();
	return true;
}

bool AGuLiResourceFactoryActor::EnqueueRaw(
	const EGuLiResourceType ResourceType,
	const int32 Amount)
{
	FGuLiResourceAmounts Cargo;
	if (!Cargo.Add(ResourceType, Amount))
	{
		return false;
	}
	return EnqueueCargo(Cargo);
}

void AGuLiResourceFactoryActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyDoorPose();
	if (!HasAuthority() || !Lifecycle->IsCompleted() || Queue.IsEmpty())
	{
		return;
	}
	ProcessingAccumulator += FMath::Max(0.0f, DeltaSeconds) * ProcessingRatePerSecond;
	while (ProcessingAccumulator >= 1.0f && !Queue.IsEmpty())
	{
		FGuLiFactoryQueueEntry& Entry = Queue[0];
		if (Entry.Amount <= 0)
		{
			Queue.RemoveAt(0, 1, EAllowShrinking::No);
			continue;
		}
		GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->CreditFactoryOutput(Entry.SettlementTeam, Entry.ResourceType, 1);
		--Entry.Amount;
		ProcessingAccumulator -= 1.0f;
		if (Entry.Amount == 0) Queue.RemoveAt(0, 1, EAllowShrinking::No);
		ForceNetUpdate();
	}
}

int32 AGuLiResourceFactoryActor::GetQueuedRawAmount() const
{
	int32 Total = 0;
	for (const FGuLiFactoryQueueEntry& Entry : Queue) Total += FMath::Max(0, Entry.Amount);
	return Total;
}

FGuLiResourceFactoryPrivateState AGuLiResourceFactoryActor::MakePrivateState() const
{
	FGuLiResourceFactoryPrivateState State;
	State.StableActorId = StableActorId;
	State.QueuedRawAmount = GetQueuedRawAmount();
	return State;
}

void AGuLiResourceFactoryActor::OnRep_DockPoint()
{
	DockPointComponent->SetWorldLocation(FVector(DockPoint));
}

void AGuLiResourceFactoryActor::OnRep_PresentationClass()
{
	check(PresentationClass);
	Presentation->SetChildActorClass(PresentationClass);
	AActor* Child = Presentation->GetChildActor();
	check(Child);
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Child);
	for (USkeletalMeshComponent* Mesh : Meshes)
		if (Mesh->GetFName() == TEXT("Door")) DoorMesh = Mesh;
	check(DoorMesh.IsValid());
	ConfigureDoorPresentation();
	DoorMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// The facility owns runtime door progress; the standalone Blueprint timeline remains usable in its showcase.
	TInlineComponentArray<UTimelineComponent*> Timelines(Child);
	for (UTimelineComponent* Timeline : Timelines) Timeline->Stop();
	ApplyDoorPose();
	FindComponentByClass<UGuLiBuildingConstructionVisualComponent>()->RefreshVisualSources();
}

float AGuLiResourceFactoryActor::GetDoorAlpha() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	const float Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Travel = FMath::Max(0.0f, Now - DoorState.StartServerTime) / DoorState.Duration;
	return FMath::Clamp(DoorState.StartAlpha + (DoorState.bOpen ? Travel : -Travel), 0.0f, 1.0f);
}

void AGuLiResourceFactoryActor::ApplyDoorPose()
{
	// Class and door state can arrive in either order during replication.
	if (DoorMesh.IsValid()) DoorMesh->SetPosition(GetDoorAlpha() * 3.0f, false);
}

void AGuLiResourceFactoryActor::OnRep_DoorState()
{
	SetActorTickEnabled(true);
	ApplyDoorPose();
}

void AGuLiResourceFactoryActor::UpdateDoorTarget()
{
	const bool bOpen = !AssignedVehicles.IsEmpty();
	if (DoorState.bOpen == bOpen) return;
	DoorState.StartAlpha = GetDoorAlpha();
	DoorState.bOpen = bOpen;
	DoorState.StartServerTime = GetWorld()->GetTimeSeconds();
	OnRep_DoorState();
	ForceNetUpdate();
}

void AGuLiResourceFactoryActor::RegisterMiningVehicle(AActor& Vehicle)
{
	check(HasAuthority());
	AssignedVehicles.Add(&Vehicle);
	UpdateDoorTarget();
}

void AGuLiResourceFactoryActor::UnregisterMiningVehicle(AActor& Vehicle)
{
	check(HasAuthority());
	AssignedVehicles.Remove(&Vehicle);
	UpdateDoorTarget();
}

TArray<FVector> AGuLiResourceFactoryActor::GetUnloadPoints() const
{
	TArray<FVector> Points;
	Points.Reserve(UnloadPoints.Num());
	for (const auto& Point : UnloadPoints) Points.Add(Point->GetComponentLocation());
	return Points;
}

void AGuLiResourceFactoryActor::ConfigureDoorPresentation()
{
	if (!DoorMesh.IsValid()) return;
	// User contract: both open and closed door poses are presentation, never an obstacle.
	DoorMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DoorMesh->SetGenerateOverlapEvents(false);
	DoorMesh->SetCanEverAffectNavigation(false);
}

bool AGuLiResourceFactoryActor::UploadCargo(AActor& Vehicle, const FGuLiResourceAmounts& Cargo)
{
	const auto* Engineering = Cast<IGuLiEngineeringVehicle>(&Vehicle);
	return HasAuthority() && Engineering && Engineering->GetTeam() == Team && EnqueueCargo(Cargo);
}

void AGuLiResourceFactoryActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiResourceFactoryActor, Team);
	DOREPLIFETIME(AGuLiResourceFactoryActor, StableActorId);
	DOREPLIFETIME(AGuLiResourceFactoryActor, DockPoint);
	DOREPLIFETIME(AGuLiResourceFactoryActor, DoorState);
	DOREPLIFETIME_CONDITION(AGuLiResourceFactoryActor, PresentationClass, COND_InitialOnly);
}

