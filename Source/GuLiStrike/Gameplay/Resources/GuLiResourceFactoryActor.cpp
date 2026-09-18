#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
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
	Presentation = CreateDefaultSubobject<UChildActorComponent>(TEXT("FactoryPresentation"));
	Presentation->SetupAttachment(CollisionBox);
	// Door bones and ramp geometry retain source units beneath this presentation parent.
	Presentation->SetRelativeScale3D(FVector(0.2f));
}

void AGuLiResourceFactoryActor::BeginPlay()
{
	Super::BeginPlay();
	OnRep_DoorState();
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
	ReleaseDock(Vehicle);
	AssignedVehicles.Remove(&Vehicle);
	UpdateDoorTarget();
}

FGuLiFactoryDockRoute AGuLiResourceFactoryActor::GetDockRoute() const
{
	FGuLiFactoryDockRoute Route;
	Route.Entry.X = GetActorTransform().InverseTransformPosition(FVector(DockPoint)).X;
	Route.Exit.X = Route.Entry.X;
	return Route;
}

void AGuLiResourceFactoryActor::ConfigureDoorPresentation()
{
	if (!DoorMesh.IsValid()) return;
	// User contract: both open and closed door poses are presentation, never an obstacle.
	DoorMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DoorMesh->SetGenerateOverlapEvents(false);
	DoorMesh->SetCanEverAffectNavigation(false);
}

bool AGuLiResourceFactoryActor::TryReserveDock(AActor& Vehicle)
{
	const auto* Engineering = Cast<IGuLiEngineeringVehicle>(&Vehicle);
	if (!HasAuthority() || !Lifecycle->IsCompleted() || !Engineering || Engineering->GetTeam() != Team
		|| !AssignedVehicles.Contains(&Vehicle)) return false;
	DockSessions.FindOrAdd(&Vehicle, false);
	return true;
}

void AGuLiResourceFactoryActor::ReleaseDock(AActor& Vehicle)
{
	DockSessions.Remove(&Vehicle);
}

bool AGuLiResourceFactoryActor::UploadCargo(AActor& Vehicle, const FGuLiResourceAmounts& Cargo)
{
	bool* Uploaded = DockSessions.Find(&Vehicle);
	const auto* Engineering = Cast<IGuLiEngineeringVehicle>(&Vehicle);
	if (!HasAuthority() || !Engineering || Engineering->GetTeam() != Team || !Uploaded || *Uploaded) return false;
	if (!EnqueueCargo(Cargo)) return false;
	*Uploaded = true;
	return true;
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

