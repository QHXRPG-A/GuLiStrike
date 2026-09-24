#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiEngineeringAIController.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdTransitPresentationComponent.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Gameplay/Navigation/GuLiEngineeringPathSubsystem.h"
#include "Gameplay/Navigation/GuLiLandingGround.h"
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
	if (GetOwner()->HasAuthority()) SetComponentTickEnabled(true);
}
UGuLiResourceWorldSubsystem& UGuLiEngineeringTravelComponent::Resources() const
{ return *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>(); }
IGuLiEngineeringVehicle& UGuLiEngineeringTravelComponent::Vehicle() const
{ return *CastChecked<IGuLiEngineeringVehicle>(GetOwner()); }
AAIController& UGuLiEngineeringTravelComponent::Controller() const
{ return *CastChecked<AAIController>(CastChecked<APawn>(GetOwner())->GetController()); }
EGuLiTransitOrderResult UGuLiEngineeringTravelComponent::PrepareTransport(
	const FGuLiStrongholdTransitOrder& Order, FGuLiPreparedTransit& Out) const
{
	check(GetOwner()->HasAuthority());
	using Result = EGuLiTransitOrderResult;
	if (!Order.IsWellFormed() || !Resources().IsRuntimeReady()) return Result::InvalidRequest;
	if (!GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()->IsAlive()) return Result::InvalidVehicle;
	if (Control->AreActionsLocked()) return Result::ActionsLocked;
	const EGuLiTeam Team = Vehicle().GetTeam();
	const int32 Source = Resources().FindTerritoryIndex(GetOwner()->GetActorLocation());
	const int32 Target = Resources().FindTerritoryIndexById(Order.TerritoryId);
	const auto& WorldState = *Resources().GetResourceWorldState();
	if (Target == INDEX_NONE || WorldState.GetTerritoryOwner(Target) != Team) return Result::InvalidTarget;
	if (Source == INDEX_NONE || WorldState.GetTerritoryOwner(Source) != Team) return Result::InvalidSource;
	if (Source == Target) return Result::SameTerritory;
	const auto& Network = Resources().GetTransportNetwork();
	Out.Route = Network.FindRoute(Source,Target,Team);
	if (Out.Route.IsEmpty()) return Result::NoRoute;
	Out.FieldId = Network.GetSnapshot().FindNode(Source)->TransitFieldId;
	Out.ClickLocation = Order.ClickLocation;
	return Result::Accepted;
}
void UGuLiEngineeringTravelComponent::BeginTransport(const FGuLiPreparedTransit& Prepared)
{
	check(GetOwner()->HasAuthority() && Prepared.Route.Num() >= 2 && !Control->AreActionsLocked());
	State = {}; State.JourneyId = FGuid::NewGuid(); State.TransitFieldId = Prepared.FieldId;
	State.DestinationTerritory = Prepared.Route.Last(); State.FinalGroundTarget = Prepared.ClickLocation;
	State.ExitCenter = Resources().GetTerritoryGroundLocation(State.DestinationTerritory);
	PassedNodes = {Prepared.Route[0]}; ExitQueryAccumulator = 0; ExitCandidateCursor=0;
	Controller().StopMovement();
	const bool bApplied = Control->ApplyServerState(State.JourneyId,true,true,GetOwner()->GetActorTransform(),false);
	check(bApplied);
	OnTransportStarted.Broadcast();
	const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdTransit(State.TransitFieldId);
	StartAirRoute(Prepared.Route,GetOwner()->GetActorLocation(),GetWorld()->GetTimeSeconds(),Config.AscentSeconds,0);
}
void UGuLiEngineeringTravelComponent::PublishPhase(EGuLiTransitPhase Phase)
{
	State.Phase = Phase; OnRep_State(); GetOwner()->ForceNetUpdate();
	SetComponentTickEnabled(GetOwner()->HasAuthority());
}
void UGuLiEngineeringTravelComponent::StartAirRoute(const TArray<int32>& Route, const FVector& From,
	double Now, float Ascent, float InitialSpeed)
{
	const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdTransit(State.TransitFieldId);
	State.Route.Reset(); State.EntryPosition = From; State.AscentSeconds = Ascent; State.StartServerTime = Now;
	// The ascent is vertical at the vehicle; the merge to the source node belongs to the timed polyline.
	State.Route.Add({INDEX_NONE,From + FVector(0,0,Ascent > 0 ? Config.LaneHeight : 0)});
	State.NetworkRevision = Resources().GetTransportNetwork().GetSnapshot().Revision;
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
void UGuLiEngineeringTravelComponent::ResolveDisruption(double Now)
{
	const FVector Current = State.SamplePosition(Now);
	const float Speed = State.Timing.SpeedAt(Now-State.StartServerTime-State.AscentSeconds);
	int32 NearestOwned = INDEX_NONE, NearestConnected = INDEX_NONE;
	double OwnedDistance = TNumericLimits<double>::Max(), ConnectedDistance = TNumericLimits<double>::Max();
	const int32 CurrentTerritory=Resources().FindTerritoryIndex(Current);
	const int32 CurrentRegion=Resources().GetTransportRegion(Vehicle().GetTeam(),CurrentTerritory);
	for (int32 Node : PassedNodes)
	{
		if (Resources().GetResourceWorldState()->GetTerritoryOwner(Node) != Vehicle().GetTeam()
			|| CurrentRegion==INDEX_NONE || Resources().GetTransportRegion(Vehicle().GetTeam(),Node)!=CurrentRegion) continue;
		const double Distance = FVector::DistSquared2D(Current,Resources().GetTerritoryGroundLocation(Node));
		if (Distance < OwnedDistance) { NearestOwned = Node; OwnedDistance = Distance; }
		if (Distance < ConnectedDistance && Resources().CanUseStrongholdTransit(Node,Vehicle().GetTeam()))
		{ NearestConnected = Node; ConnectedDistance = Distance; }
	}
	TArray<int32> Route;
	if (NearestConnected != INDEX_NONE && !State.bEmergencyExit)
		Route = Resources().GetTransportNetwork().FindRoute(NearestConnected,State.DestinationTerritory,Vehicle().GetTeam());
	if (Route.IsEmpty())
	{
		State.bEmergencyExit = true; State.DestinationTerritory = NearestOwned;
		if (NearestOwned != INDEX_NONE) { Route.Add(NearestOwned); State.ExitCenter = Resources().GetTerritoryGroundLocation(NearestOwned); }
		else
		{
			const double GroundZ=CurrentTerritory!=INDEX_NONE ? Resources().GetTerritoryGroundLocation(CurrentTerritory).Z
				: GetOwner()->GetActorLocation().Z;
			State.ExitCenter=FVector(Current.X,Current.Y,GroundZ);
		}
		WorkLegs.Reset();
	}
	StartAirRoute(Route,Current,Now,0,Speed);
}
bool UGuLiEngineeringTravelComponent::FindExit(FTransform& Transform) const
{
	const auto& Pawn = *CastChecked<ACharacter>(GetOwner());
	const auto& Config = *GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdTransit(State.TransitFieldId);
	auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation) return false;
	const auto* Data = Navigation->GetNavDataForProps(Pawn.GetNavAgentPropertiesRef(),State.ExitCenter);
	if (!Data) return false;
	const FVector Toward = FVector(State.FinalGroundTarget)-FVector(State.ExitCenter);
	const double Angle = FMath::Atan2(Toward.Y,Toward.X);
	const FBox Bounds = Vehicle().GetEngineeringTravelBounds(); check(Bounds.IsValid);
	const float Radius = FMath::Max(Config.ExitRadius, float(Bounds.GetExtent().Size2D()+360));
	TArray<FVector> Candidates;
	for (int32 Ring = 0; Ring < 3; ++Ring)
		for (int32 I = 0; I < 32; ++I)
		{
			const double Theta = Angle+I*UE_TWO_PI/32;
			Candidates.Add(FVector(State.ExitCenter)+FVector(FMath::Cos(Theta),FMath::Sin(Theta),0)*(Radius+Ring*150));
		}
	Candidates.StableSort([this](const FVector& A,const FVector& B)
	{ return FVector::DistSquared2D(A,State.FinalGroundTarget)<FVector::DistSquared2D(B,State.FinalGroundTarget); });
	FCollisionQueryParams Query(SCENE_QUERY_STAT(StrongholdExit),false,GetOwner());
	Query.IgnoreMask = GuLiEngineeringCollision::VehicleMask;
	TInlineComponentArray<UChildActorComponent*> Children(GetOwner());
	for (auto* Child : Children) Query.AddIgnoredActor(Child->GetChildActor());
	TArray<FGuLiGroundMassBody> Occupants;
	const FVector2D Center(State.ExitCenter);
	const double Extent=Radius+300+Bounds.GetExtent().Size2D();
	GetWorld()->GetSubsystem<UGuLiGroundMassContactSubsystem>()->QueryMassBodies(
		FBox2D(Center-FVector2D(Extent),Center+FVector2D(Extent)),0,Occupants);
	for (int32 Attempt=0; Attempt<8; ++Attempt)
	{
		const FVector Candidate=Candidates[ExitCandidateCursor++ % Candidates.Num()];
		FNavLocation Ground;
		if (!Navigation->ProjectPointToNavigation(Candidate,Ground,FVector(100,100,5000),Data)) continue;
		const float HalfHeight = Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FRotator Rotation = (FVector(State.FinalGroundTarget)-Ground.Location).Rotation();
		FTransform Pose(FRotator(0,Rotation.Yaw,0),Ground.Location);
		// Support the complete footprint on the highest ground contact before testing the body.
		const FVector Sole = Pose.TransformPosition(FVector(Bounds.GetCenter().X,Bounds.GetCenter().Y,0));
		FHitResult Support;
		if (!GetWorld()->SweepSingleByChannel(Support,Sole+FVector(0,0,1500),Sole-FVector(0,0,1500),
			Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(FVector(Bounds.GetExtent().X,Bounds.GetExtent().Y,0.2)),Query)
			|| !GuLiLandingGround::IsWalkableSupport(Support, Pawn.GetCharacterMovement())
			|| FMath::Abs(Support.Location.Z-Ground.Location.Z)>100) continue;
		Pose.SetLocation(FVector(Ground.Location.X,Ground.Location.Y,Support.Location.Z+FMath::Max(HalfHeight,-Bounds.Min.Z)+1));
		if (GetWorld()->OverlapBlockingTestByChannel(Pose.TransformPosition(Bounds.GetCenter()),Pose.GetRotation(),
			ECC_Pawn,FCollisionShape::MakeBox(Bounds.GetExtent()),Query)) continue;
		if (GetWorld()->OverlapBlockingTestByChannel(Pose.GetLocation(),FQuat::Identity,ECC_Pawn,
			FCollisionShape::MakeCapsule(Pawn.GetCapsuleComponent()->GetScaledCapsuleRadius(),HalfHeight),Query)) continue;
		const bool bOccupied = Occupants.ContainsByPredicate([&](const auto& Target)
		{
			const FVector Local = Pose.InverseTransformPosition(Target.Location);
			const FVector Closest = Bounds.GetClosestPointTo(Local);
			return FVector::DistSquared2D(Local,Closest) < FMath::Square(Target.RadiusCentimeters+20)
				&& Target.TopZ>Pose.GetLocation().Z+Bounds.Min.Z-20
				&& Target.BottomZ<Pose.GetLocation().Z+Bounds.Max.Z+20;
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
	OnTransportEnded.Broadcast();
}
void UGuLiEngineeringTravelComponent::TickComponent(float Dt, ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt,TickType,TickFunction);
	check(GetOwner()->HasAuthority());
	const double Now = GetWorld()->GetTimeSeconds();
	if (State.Phase == EGuLiTransitPhase::Ground) { TickGroundMove(); return; }
	if (State.Phase == EGuLiTransitPhase::ExitFlash)
	{
		if (Now >= State.ExitServerTime+State.FlashSeconds) { PublishPhase(EGuLiTransitPhase::Ground); if (bWorkJourney) AdvanceWorkRoute(); }
		return;
	}
	int32 Leg = 0; State.SamplePosition(Now,&Leg);
	for (int32 I = 0; I <= Leg; ++I)
		if (State.Route[I].TerritoryIndex != INDEX_NONE) PassedNodes.AddUnique(State.Route[I].TerritoryIndex);
	const auto& Network = Resources().GetTransportNetwork();
	bool bBroken = false;
	if (State.NetworkRevision != Network.GetSnapshot().Revision)
	{
		if (State.bEmergencyExit)
		{
			const int32 CurrentRegion=Resources().GetTransportRegion(Vehicle().GetTeam(),Resources().FindTerritoryIndex(State.SamplePosition(Now)));
			bBroken = State.DestinationTerritory != INDEX_NONE && (CurrentRegion==INDEX_NONE
				|| Resources().GetTransportRegion(Vehicle().GetTeam(),State.DestinationTerritory)!=CurrentRegion
				|| Resources().GetResourceWorldState()->GetTerritoryOwner(State.DestinationTerritory) != Vehicle().GetTeam());
		}
		else
			for (int32 I = Leg; I < State.Route.Num(); ++I)
			{
				const int32 Node = State.Route[I].TerritoryIndex;
				if (Node == INDEX_NONE) continue;
				if (!Resources().CanUseStrongholdTransit(Node,Vehicle().GetTeam())) { bBroken = true; break; }
				if (I+1 < State.Route.Num() && State.Route[I+1].TerritoryIndex != INDEX_NONE
					&& !Network.HasEdge(Node,State.Route[I+1].TerritoryIndex,Vehicle().GetTeam())) { bBroken = true; break; }
			}
		State.NetworkRevision = Network.GetSnapshot().Revision;
	}
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
	if (GetNetMode() == NM_DedicatedServer || State.TransitFieldId == 0) return;
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
