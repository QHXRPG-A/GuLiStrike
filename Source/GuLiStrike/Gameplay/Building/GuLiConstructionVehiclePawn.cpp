#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiConstructionPresentationComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiActorDamageReceiverComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/MeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Gameplay/Units/GuLiEngineeringAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

AGuLiConstructionVehiclePawn::AGuLiConstructionVehiclePawn(const FObjectInitializer& Initializer)
	: Super(Initializer.SetDefaultSubobjectClass<UGuLiExternalCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	bReplicates = true; bAlwaysRelevant = true; SetReplicateMovement(true);
	GetCapsuleComponent()->InitCapsuleSize(130,130);
	GetCapsuleComponent()->SetMaskFilterOnBodyInstance(GuLiEngineeringCollision::VehicleMask);
	GetCapsuleComponent()->SetMoveIgnoreMask(GuLiEngineeringCollision::VehicleMask);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
	GetCharacterMovement()->NavAgentProps.AgentRadius = GULI_RESOURCE_MINING_VEHICLE_NAV_RADIUS_CM;
	GetCharacterMovement()->NavAgentProps.AgentHeight = 28.8f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	bUseControllerRotationYaw = false;
	AIControllerClass = AGuLiEngineeringAIController::StaticClass(); AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	CreateDefaultSubobject<UGuLiCombatHealthComponent>(TEXT("CombatHealth"));
	CreateDefaultSubobject<UGuLiActorDamageReceiverComponent>(TEXT("DamageReceiver"));
	CreateDefaultSubobject<UGuLiExternalUnitControlComponent>(TEXT("ExternalControl"));
	Travel = CreateDefaultSubobject<UGuLiEngineeringTravelComponent>(TEXT("Travel"));
	Work = CreateDefaultSubobject<UGuLiConstructionWorkComponent>(TEXT("ConstructionWork"));
	ConstructionPresentation = CreateDefaultSubobject<UGuLiConstructionPresentationComponent>(TEXT("ConstructionPresentation"));
	Presentation = CreateDefaultSubobject<UChildActorComponent>(TEXT("Presentation"));
	Presentation->SetupAttachment(GetCapsuleComponent());
	Presentation->OnChildActorCreated().AddStatic(&GuLiUnitRenderPolicy::ApplyToActor);
}
void AGuLiConstructionVehiclePawn::InitializeVehicle(EGuLiTeam InTeam, FGuLiControllableActorId InId,
	const FGuLiSoldierDefinition& Definition)
{
	check(HasAuthority());
	Team = InTeam; StableId = InId; BaseSpeed = Definition.MovementSpeedCmPerSecond;
	UnitTypeId = Definition.UnitTypeId;
	GetCharacterMovement()->MaxWalkSpeed = BaseSpeed;
	PresentationClass = Definition.PresentationClass; PresentationScale = Definition.PresentationScale; OnRep_Definition();
	auto& Health = *FindComponentByClass<UGuLiCombatHealthComponent>();
	FGuLiTargetHandle Handle; Handle.Kind = EGuLiTargetKind::GroundActor; Handle.AuthorityId = FGuid::NewGuid();
	Handle.Generation = 1; Handle.LocalId = StableId.Value;
	Health.ConfigureServerTarget(Handle, Team); Health.InitializeServerHealth(Definition.MaxHealth);
	if (!GetController()) SpawnDefaultController();
	CastChecked<AGuLiEngineeringAIController>(GetController())->ConfigureVehicleNavigation();
	GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->RegisterActor(*this);
	ForceNetUpdate();
}
void AGuLiConstructionVehiclePawn::OnRep_Definition()
{
	if (!PresentationClass) return; // The replicated class may arrive before the scalar fields.
	Presentation->SetChildActorClass(PresentationClass);
	Presentation->SetRelativeScale3D(FVector(PresentationScale));
	Presentation->SetRelativeLocation(FVector(0,0,-130));
	AActor* Child = Presentation->GetChildActor(); check(Child);
	Child->SetActorEnableCollision(false);
	TravelBounds = FBox(ForceInit);
	TInlineComponentArray<UMeshComponent*> Meshes(Child);
	for (auto* VisualMesh : Meshes)
	{
		VisualMesh->SetCanEverAffectNavigation(false);
		GuLiUnitRenderPolicy::Apply(*VisualMesh);
		TravelBounds += VisualMesh->CalcBounds(VisualMesh->GetComponentTransform().GetRelativeTransform(GetActorTransform())).GetBox();
	}
	SetEngineeringPresentationVisible(!Travel->IsInTransit());
	ConstructionPresentation->InitializePresentation(Child);
}
void AGuLiConstructionVehiclePawn::SetEngineeringPresentationVisible(bool bVisible)
{
	if (AActor* Child = Presentation->GetChildActor()) Child->SetActorHiddenInGame(!bVisible);
	if (!bVisible) ConstructionPresentation->HideBeams();
}
EGuLiTransitOrderResult AGuLiConstructionVehiclePawn::IssueStrongholdTransit(
	const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam)
{
	using Result = EGuLiTransitOrderResult;
	if (!HasAuthority() || RequestingTeam != Team) return Result::Unauthorized;
	if (!Order.IsWellFormed()) return Result::InvalidRequest;
	if (uint32(Order.RequestId) == LastTransitRequestId) return Result::Accepted;
	if (LastTransitRequestId != 0 && int32(uint32(Order.RequestId)-LastTransitRequestId) <= 0) return Result::StaleRequest;
	FGuLiPreparedTransit Prepared;
	const auto Decision = Travel->PrepareTransport(Order,Prepared);
	if (Decision != Result::Accepted) return Decision;
	FGuLiUnitTaskCommand Task; Task.CommandId = uint32(Order.RequestId); Task.SelectionRevision = uint32(Order.SelectionRevision);
	Task.Kind = EGuLiUnitTaskKind::Transit; Task.TerritoryId = Order.TerritoryId; Task.Target = Order.ClickLocation;
	if (!GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->SubmitActorCommand(*this, Task)) return Result::InvalidRequest;
	LastTransitRequestId = uint32(Order.RequestId);
	return Result::Accepted;
}
bool AGuLiConstructionVehiclePawn::IssueMove(const FVector& Target)
{
	if (!HasAuthority() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)) return false;
	Work->StopWork(); return Travel->BeginMove(Target,100);
}
bool AGuLiConstructionVehiclePawn::IssueConstruction(UGuLiBuildingLifecycleComponent* Building, bool bAutomatic)
{
	if (!HasAuthority() || !IsValid(Building) || UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)) return false;
	return Work->AssignBuilding(*Building, bAutomatic);
}
void AGuLiConstructionVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGuLiConstructionVehiclePawn, Team); DOREPLIFETIME(AGuLiConstructionVehiclePawn, StableId);
	DOREPLIFETIME(AGuLiConstructionVehiclePawn, PresentationClass); DOREPLIFETIME(AGuLiConstructionVehiclePawn, PresentationScale);
	DOREPLIFETIME(AGuLiConstructionVehiclePawn, BaseSpeed);
	DOREPLIFETIME(AGuLiConstructionVehiclePawn, UnitTypeId);
}
