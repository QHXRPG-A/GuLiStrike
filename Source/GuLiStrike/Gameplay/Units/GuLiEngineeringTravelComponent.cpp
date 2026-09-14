#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdGateComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdTransitPresentationComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiEngineeringTravelComponent::UGuLiEngineeringTravelComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}
void UGuLiEngineeringTravelComponent::BeginPlay()
{
	Super::BeginPlay();
	Control = GetOwner()->FindComponentByClass<UGuLiExternalUnitControlComponent>();
	check(Control && Cast<IGuLiEngineeringVehicle>(GetOwner()));
}
UGuLiResourceWorldSubsystem& UGuLiEngineeringTravelComponent::Resources() const
{ return *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>(); }
IGuLiEngineeringVehicle& UGuLiEngineeringTravelComponent::Vehicle() const
{ return *CastChecked<IGuLiEngineeringVehicle>(GetOwner()); }
AAIController& UGuLiEngineeringTravelComponent::Controller() const
{ return *CastChecked<AAIController>(CastChecked<APawn>(GetOwner())->GetController()); }
bool UGuLiEngineeringTravelComponent::MoveOnGround(const FVector& Target, float AcceptanceRadius)
{
	return Controller().MoveToLocation(Target, AcceptanceRadius, true, true, true, true, nullptr, false)
		!= EPathFollowingRequestResult::Failed;
}
bool UGuLiEngineeringTravelComponent::BeginMove(const FVector& Target, float AcceptanceRadius)
{
	check(GetOwner()->HasAuthority());
	if (Target.ContainsNaN() || AcceptanceRadius < 0 || Control->AreActionsLocked()) return false;
	State = {}; PassedNodes.Reset(); GroundAcceptance = AcceptanceRadius;
	State.FinalGroundTarget = Target;
	SourceTerritory = Resources().FindTerritoryIndex(GetOwner()->GetActorLocation());
	State.DestinationTerritory = Resources().FindTerritoryIndex(Target);
	const int32 Destination = State.DestinationTerritory;
	const auto* WorldState = Resources().GetResourceWorldState();
	if (!Resources().IsRuntimeReady() || SourceTerritory == INDEX_NONE || Destination == INDEX_NONE
		|| SourceTerritory == Destination || Resources().GetStrongholdTopology().IsAdjacent(SourceTerritory,Destination)
		|| WorldState->GetTerritoryOwner(Destination) != Vehicle().GetTeam())
	{
		PublishPhase(EGuLiTransitPhase::Ground); return MoveOnGround(Target,AcceptanceRadius);
	}
	const auto Route = Resources().GetStrongholdTopology().FindRoute(SourceTerritory,Destination,
		[this](int32 Node) { return Resources().CanUseStrongholdTransit(Node,Vehicle().GetTeam()); });
	auto* Gate = Resources().GetStrongholdGate(SourceTerritory);
	if (Route.IsEmpty() || !Gate->CanEnter(*CastChecked<APawn>(GetOwner()))
		|| !Gate->FindEntry(*CastChecked<APawn>(GetOwner()),GateEntry) || !MoveOnGround(GateEntry,150))
	{
		PublishPhase(EGuLiTransitPhase::Ground); return MoveOnGround(Target,AcceptanceRadius);
	}
	State.GateFieldId = Gate->GetConfig().Id;
	State.JourneyId = FGuid::NewGuid(); State.EntryPosition = GateEntry;
	PublishPhase(EGuLiTransitPhase::ApproachingGate); return true;
}
void UGuLiEngineeringTravelComponent::CancelApproach()
{
	check(GetOwner()->HasAuthority());
	if (State.Phase == EGuLiTransitPhase::ApproachingGate)
	{ Controller().StopMovement(); PublishPhase(EGuLiTransitPhase::Ground); }
}
void UGuLiEngineeringTravelComponent::PublishPhase(EGuLiTransitPhase Phase)
{
	State.Phase = Phase; OnRep_State(); GetOwner()->ForceNetUpdate();
	SetComponentTickEnabled(GetOwner()->HasAuthority() && Phase != EGuLiTransitPhase::Ground);
}
void UGuLiEngineeringTravelComponent::StartAirRoute(const TArray<int32>& Route, const FVector& From,
	double Now, float Ascent, float InitialSpeed)
{
	const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdGate(State.GateFieldId);
	State.Route.Reset(); State.EntryPosition = From; State.AscentSeconds = Ascent; State.StartServerTime = Now;
	if (Ascent == 0) State.Route.Add({INDEX_NONE,From});
	for (int32 Node : Route)
		State.Route.Add({Node,Resources().GetTerritoryGroundLocation(Node)+FVector(0,0,Config.LaneHeight)});
	if (Route.IsEmpty()) State.Route.Add({INDEX_NONE,FVector(State.ExitCenter)+FVector(0,0,Config.LaneHeight)});
	double Length = 0;
	for (int32 I = 1; I < State.Route.Num(); ++I) Length += FVector::Distance(State.Route[I-1].Position,State.Route[I].Position);
	const float Peak = Vehicle().GetEngineeringBaseSpeed()*Config.SpeedMultiplier;
	State.Timing.Initialize(Length,Peak,Config.AccelerationSeconds*(1-InitialSpeed/Peak),Config.DecelerationSeconds,InitialSpeed);
	State.FlashSeconds = Config.ExitFlashSeconds;
	PublishPhase(Ascent > 0 ? EGuLiTransitPhase::Ascending : EGuLiTransitPhase::Accelerating);
}
void UGuLiEngineeringTravelComponent::EnterGate(const TArray<int32>& Route)
{
	const auto& Config = Resources().GetStrongholdGate(SourceTerritory)->GetConfig();
	Controller().StopMovement();
	const bool bApplied = Control->ApplyServerState(State.JourneyId,true,true,GetOwner()->GetActorTransform(),false);
	check(bApplied);
	PassedNodes = {SourceTerritory}; State.ExitCenter = Resources().GetTerritoryGroundLocation(State.DestinationTerritory);
	OnTransportStarted.Broadcast();
	StartAirRoute(Route,GetOwner()->GetActorLocation(),GetWorld()->GetTimeSeconds(),Config.AscentSeconds,0);
}
void UGuLiEngineeringTravelComponent::ResolveDisruption(double Now)
{
	const FVector Current = State.SamplePosition(Now);
	const float Speed = State.Timing.SpeedAt(Now-State.StartServerTime-State.AscentSeconds);
	int32 Nearest = INDEX_NONE;
	double Best = TNumericLimits<double>::Max();
	for (int32 Node : PassedNodes)
	{
		if (Resources().GetResourceWorldState()->GetTerritoryOwner(Node) != Vehicle().GetTeam()) continue;
		const double Distance = FVector::DistSquared2D(Current,Resources().GetTerritoryGroundLocation(Node));
		if (Distance < Best) { Nearest = Node; Best = Distance; }
	}
	TArray<int32> Route;
	if (Nearest != INDEX_NONE && !State.bEmergencyExit)
		Route = Resources().GetStrongholdTopology().FindRoute(Nearest,State.DestinationTerritory,
			[this](int32 Node) { return Resources().CanUseStrongholdTransit(Node,Vehicle().GetTeam()); });
	if (Route.IsEmpty())
	{
		State.bEmergencyExit = true; State.DestinationTerritory = Nearest;
		if (Nearest != INDEX_NONE) { Route.Add(Nearest); State.ExitCenter = Resources().GetTerritoryGroundLocation(Nearest); }
		else State.ExitCenter = Resources().GetInitialBaseExit(Vehicle().GetTeam());
	}
	StartAirRoute(Route,Current,Now,0,Speed);
}
bool UGuLiEngineeringTravelComponent::FindExit(FTransform& Transform) const
{
	const auto& Pawn = *CastChecked<ACharacter>(GetOwner());
	const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdGate(State.GateFieldId);
	auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation) return false;
	const auto* Data = Navigation->GetNavDataForProps(Pawn.GetNavAgentPropertiesRef(),State.ExitCenter);
	if (!Data) return false;
	const FVector Toward = FVector(State.FinalGroundTarget)-FVector(State.ExitCenter);
	const double Angle = FMath::Atan2(Toward.Y,Toward.X);
	const FBox Bounds = Vehicle().GetEngineeringTravelBounds(); check(Bounds.IsValid);
	const float Radius = FMath::Max(Config.ExitRadius, float(Bounds.GetExtent().Size2D()+1800));
	TArray<FVector> Candidates;
	for (int32 Ring = 0; Ring < 3; ++Ring)
		for (int32 I = 0; I < 32; ++I)
		{
			const double Theta = Angle+I*UE_TWO_PI/32;
			Candidates.Add(FVector(State.ExitCenter)+FVector(FMath::Cos(Theta),FMath::Sin(Theta),0)*(Radius+Ring*750));
		}
	Candidates.StableSort([this](const FVector& A,const FVector& B)
	{ return FVector::DistSquared2D(A,State.FinalGroundTarget)<FVector::DistSquared2D(B,State.FinalGroundTarget); });
	FCollisionQueryParams Query(SCENE_QUERY_STAT(StrongholdExit),false,GetOwner());
	TInlineComponentArray<UChildActorComponent*> Children(GetOwner());
	for (auto* Child : Children) Query.AddIgnoredActor(Child->GetChildActor());
	TArray<FGuLiCombatTargetSnapshot> Occupants;
	GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->GetTargetSnapshots(Occupants);
	Occupants.RemoveAll([](const auto& Target) { return !Target.bAlive || Target.Handle.Kind != EGuLiTargetKind::CommanderSoldier; });
	for (const FVector& Candidate : Candidates)
	{
		FNavLocation Ground;
		if (!Navigation->ProjectPointToNavigation(Candidate,Ground,FVector(500,500,5000),Data)) continue;
		const float HalfHeight = Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FRotator Rotation = (FVector(State.FinalGroundTarget)-Ground.Location).Rotation();
		FTransform Pose(FRotator(0,Rotation.Yaw,0),Ground.Location);
		// Support the complete footprint on the highest ground contact before testing the body.
		const FVector Sole = Pose.TransformPosition(FVector(Bounds.GetCenter().X,Bounds.GetCenter().Y,0));
		FHitResult Support;
		if (!GetWorld()->SweepSingleByChannel(Support,Sole+FVector(0,0,1500),Sole-FVector(0,0,1500),
			Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(FVector(Bounds.GetExtent().X,Bounds.GetExtent().Y,1)),Query)
			|| Support.bStartPenetrating || Support.ImpactNormal.Z < Pawn.GetCharacterMovement()->GetWalkableFloorZ()
			|| FMath::Abs(Support.Location.Z-Ground.Location.Z)>500) continue;
		Pose.SetLocation(FVector(Ground.Location.X,Ground.Location.Y,Support.Location.Z+FMath::Max(HalfHeight,-Bounds.Min.Z)+5));
		if (GetWorld()->OverlapBlockingTestByChannel(Pose.TransformPosition(Bounds.GetCenter()),Pose.GetRotation(),
			ECC_Pawn,FCollisionShape::MakeBox(Bounds.GetExtent()),Query)) continue;
		if (GetWorld()->OverlapBlockingTestByChannel(Pose.GetLocation(),FQuat::Identity,ECC_Pawn,
			FCollisionShape::MakeCapsule(Pawn.GetCapsuleComponent()->GetScaledCapsuleRadius(),HalfHeight),Query)) continue;
		const bool bOccupied = Occupants.ContainsByPredicate([&](const auto& Target)
		{
			const FVector Local = Pose.InverseTransformPosition(Target.Location);
			const FVector Closest = Bounds.GetClosestPointTo(Local);
			return FVector::DistSquared2D(Local,Closest) < FMath::Square(Target.CollisionRadius+100)
				&& FMath::Abs(Local.Z-Closest.Z)<Target.CollisionRadius+100;
		});
		if (bOccupied) continue;
		Transform = Pose; return true;
	}
	return false;
}
void UGuLiEngineeringTravelComponent::ExitTransit(const FTransform& Transform, double Now)
{
	const bool bApplied = Control->ApplyServerState(State.JourneyId,false,false,Transform,true); check(bApplied);
	State.ExitPosition = Transform.GetLocation(); State.ExitServerTime = Now;
	PublishPhase(EGuLiTransitPhase::ExitFlash);
	MoveOnGround(State.FinalGroundTarget,GroundAcceptance);
	OnTransportEnded.Broadcast();
}
void UGuLiEngineeringTravelComponent::TickComponent(float Dt, ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt,TickType,TickFunction);
	check(GetOwner()->HasAuthority());
	const double Now = GetWorld()->GetTimeSeconds();
	if (State.Phase == EGuLiTransitPhase::ExitFlash)
	{
		if (Now >= State.ExitServerTime+State.FlashSeconds) PublishPhase(EGuLiTransitPhase::Ground);
		return;
	}
	if (State.Phase == EGuLiTransitPhase::ApproachingGate)
	{
		const auto Route = Resources().GetStrongholdTopology().FindRoute(SourceTerritory,State.DestinationTerritory,
			[this](int32 Node) { return Resources().CanUseStrongholdTransit(Node,Vehicle().GetTeam()); });
		if (Route.IsEmpty() || !Resources().GetStrongholdGate(SourceTerritory)->CanEnter(*CastChecked<APawn>(GetOwner())))
		{
			PublishPhase(EGuLiTransitPhase::Ground); MoveOnGround(State.FinalGroundTarget,GroundAcceptance); return;
		}
		const auto& Gate = *Resources().GetStrongholdGate(SourceTerritory);
		if (FVector::DistSquared2D(GetOwner()->GetActorLocation(),GateEntry) <= FMath::Square(1100.f)
			&& FVector::DistSquared2D(GetOwner()->GetActorLocation(),Gate.GetGroundLocation()) <= FMath::Square(Gate.GetConfig().Radius))
		{ EnterGate(Route); return; }
		if (Controller().GetMoveStatus() == EPathFollowingStatus::Idle)
		{ PublishPhase(EGuLiTransitPhase::Ground); MoveOnGround(State.FinalGroundTarget,GroundAcceptance); }
		return;
	}
	int32 Leg = 0; State.SamplePosition(Now,&Leg);
	for (int32 I = 0; I <= Leg; ++I)
		if (State.Route[I].TerritoryIndex != INDEX_NONE) PassedNodes.AddUnique(State.Route[I].TerritoryIndex);
	bool bBroken = false;
	if (State.bEmergencyExit)
		bBroken = State.DestinationTerritory != INDEX_NONE
			&& Resources().GetResourceWorldState()->GetTerritoryOwner(State.DestinationTerritory) != Vehicle().GetTeam();
	else
		for (int32 I = Leg; I < State.Route.Num(); ++I)
			if (State.Route[I].TerritoryIndex != INDEX_NONE && !Resources().CanUseStrongholdTransit(State.Route[I].TerritoryIndex,Vehicle().GetTeam()))
			{ bBroken = true; break; }
	if (bBroken) { ResolveDisruption(Now); return; }
	const double Time = Now-State.StartServerTime;
	EGuLiTransitPhase Phase;
	if (Time < State.AscentSeconds) Phase = EGuLiTransitPhase::Ascending;
	else if (Time < State.AscentSeconds+State.Timing.Acceleration) Phase = EGuLiTransitPhase::Accelerating;
	else if (Time < State.AscentSeconds+State.Timing.Acceleration+State.Timing.Cruise) Phase = EGuLiTransitPhase::Cruising;
	else if (Time < State.AscentSeconds+State.Timing.Duration()) Phase = EGuLiTransitPhase::Decelerating;
	else Phase = EGuLiTransitPhase::WaitingForExit;
	if (Phase != State.Phase) PublishPhase(Phase);
	if (Phase == EGuLiTransitPhase::WaitingForExit)
	{
		ExitQueryAccumulator += Dt;
		if (ExitQueryAccumulator >= .1f)
		{
			ExitQueryAccumulator = 0; FTransform Exit;
			if (FindExit(Exit)) ExitTransit(Exit,Now);
		}
	}
}
void UGuLiEngineeringTravelComponent::OnRep_State()
{
	Vehicle().SetEngineeringPresentationVisible(!State.IsPhased());
	if (GetNetMode() == NM_DedicatedServer || State.GateFieldId == 0) return;
	if (!Presentation)
	{
		Presentation = NewObject<UGuLiStrongholdTransitPresentationComponent>(GetOwner());
		Presentation->RegisterComponent();
	}
	Presentation->ApplyState(State);
}
void UGuLiEngineeringTravelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UGuLiEngineeringTravelComponent,State);
}
