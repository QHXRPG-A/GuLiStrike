#include "Gameplay/Building/GuLiGiftBuildingClearance.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Navigation/GuLiLandingGround.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"
#include "NavRelevantComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

struct FGuLiGiftBuildingClearance::FState
{
	struct FActorState { TWeakObjectPtr<AActor> Actor; bool bCollision = true; };
	struct FPrimitiveState { TWeakObjectPtr<UPrimitiveComponent> Component; bool bNavigation = false; };
	struct FNavState { TWeakObjectPtr<UNavRelevantComponent> Component; bool bRelevant = false; };
	struct FUnit
	{
		FGuLiSoldierId Soldier;
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<UGuLiExternalUnitControlComponent> Control;
		FTransform Original, Landing;
		float Radius = 0, HalfHeight = 0;
		bool bFlying = false;
		bool bMove = false;
		FVector Center(bool bLanding) const
		{
			return (bLanding ? Landing : Original).GetLocation() + (Soldier.IsValid() ? FVector(0,0,HalfHeight + 3) : FVector::ZeroVector);
		}
	};
	UWorld& World;
	FDelegateHandle SpawnHandle;
	TArray<FActorState> Actors;
	TArray<FPrimitiveState> Primitives;
	TArray<FNavState> Navigation;
	bool bCommitted = false;

	explicit FState(UWorld& InWorld) : World(InWorld) {}
	void StageComponents(AActor& Actor)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components(&Actor);
		for (auto* Component : Components)
		{
			auto* Existing = Primitives.FindByPredicate([Component](const auto& Item) { return Item.Component == Component; });
			if (!Existing) Primitives.Add({Component, Component->CanEverAffectNavigation()});
			else Existing->bNavigation |= Component->CanEverAffectNavigation();
			Component->SetCanEverAffectNavigation(false);
		}
		TInlineComponentArray<UNavRelevantComponent*> NavComponents(&Actor);
		for (auto* Component : NavComponents)
		{
			if (!Navigation.ContainsByPredicate([Component](const auto& Item) { return Item.Component == Component; }))
				Navigation.Add({Component, Component->IsNavigationRelevant()});
			Component->SetNavigationRelevancy(false);
		}
	}
	void Stage(AActor* Actor)
	{
		if (!Actor) return;
		Actors.Add({Actor, Actor->GetActorEnableCollision()});
		StageComponents(*Actor);
		Actor->SetActorEnableCollision(false);
	}
	void StopWatching()
	{
		if (SpawnHandle.IsValid()) { World.RemoveOnActorPreSpawnInitialization(SpawnHandle); SpawnHandle.Reset(); }
	}
};

FGuLiGiftBuildingClearance::FGuLiGiftBuildingClearance(UWorld& World) : State(MakeUnique<FState>(World))
{
	State->SpawnHandle = World.AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateLambda(
		[this](AActor* Actor) { State->Stage(Actor); }));
}

FGuLiGiftBuildingClearance::~FGuLiGiftBuildingClearance()
{
	State->StopWatching();
	if (!State->bCommitted)
		for (int32 Index = State->Actors.Num() - 1; Index >= 0; --Index)
			if (auto* Actor = State->Actors[Index].Actor.Get()) Actor->Destroy();
}

bool FGuLiGiftBuildingClearance::Commit(AActor& Building, const AActor& SupportingActor, FString& OutFailure)
{
	auto& S = *State;
	S.StopWatching();
	TArray<FBox> Footprints;
	FBox Bounds(ForceInit);
	for (const auto& Entry : S.Actors)
	{
		auto* Actor = Entry.Actor.Get();
		if (!Actor) continue;
		S.StageComponents(*Actor); // Includes Blueprint components and terrain-fitted ramps.
		Actor->SetActorEnableCollision(false);
		if (!Entry.bCollision) continue;
		TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
		for (const auto* Component : Components)
			// GetCollisionEnabled includes the Actor override, which is intentionally off while staging.
			if (Component->BodyInstance.GetCollisionEnabled(false) != ECollisionEnabled::NoCollision
				&& Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
			{
				const FBox Box = Component->CalcBounds(Component->GetComponentTransform()).GetBox();
				if (Box.IsValid) { Footprints.Add(Box); Bounds += Box; }
			}
	}
	if (!Bounds.IsValid) { OutFailure = TEXT("Gift has no valid blocking footprint."); return false; }
	const double Clearance = GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters;
	auto Intersects = [&Footprints, Clearance](const FState::FUnit& Unit, FVector Center)
	{
		for (const FBox& Box : Footprints)
			if (Box.ExpandBy(FVector(Unit.Radius + Clearance, Unit.Radius + Clearance, Unit.HalfHeight + Clearance)).IsInsideOrOn(Center)) return true;
		return false;
	};
	TArray<FState::FUnit> Units;
	auto* Authority = S.World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	if (Authority)
	{
		TArray<FGuLiMassExternalUnit> Soldiers;
		Authority->CollectExternalUnitsForClearance(Bounds.ExpandBy(25000.0), Soldiers);
		for (const auto& Soldier : Soldiers)
		{
			auto& Unit = Units.AddDefaulted_GetRef(); Unit.Soldier = Soldier.Id;
			Unit.Original = Unit.Landing = Soldier.Transform; Unit.Radius = Unit.HalfHeight = Soldier.Radius;
			Unit.bMove = Intersects(Unit, Unit.Center(false));
			if (Unit.bMove && (Authority->IsSoldierExternallyLocked(Soldier.Id) || Authority->IsSoldierPhased(Soldier.Id)))
			{ OutFailure = TEXT("An overlapping Mass unit is externally controlled."); return false; }
		}
	}
	for (TActorIterator<APawn> It(&S.World); It; ++It)
	{
		auto* Pawn = *It;
		auto* Body = Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
		auto* Control = Pawn->FindComponentByClass<UGuLiExternalUnitControlComponent>();
		const bool bExternallyControlled = Control && (Control->AreActionsLocked() || Control->IsPhased());
		if (!Body || ((!Pawn->GetActorEnableCollision() || Body->GetCollisionEnabled() == ECollisionEnabled::NoCollision) && !bExternallyControlled)
			|| !Bounds.ExpandBy(25000.0).IsInsideOrOn(Pawn->GetActorLocation())) continue;
		const auto* Health = Pawn->FindComponentByClass<UGuLiCombatHealthComponent>();
		if (Health && !Health->IsAlive()) continue;
		auto& Unit = Units.AddDefaulted_GetRef(); Unit.Pawn = Pawn;
		Unit.Control = Control;
		Unit.Original = Unit.Landing = Pawn->GetActorTransform();
		if (const auto* Capsule = Cast<UCapsuleComponent>(Body)) Capsule->GetScaledCapsuleSize(Unit.Radius, Unit.HalfHeight);
		else { const FVector Extent = Body->Bounds.BoxExtent; Unit.Radius = FVector2D(Extent.X, Extent.Y).Size(); Unit.HalfHeight = FMath::Max(Unit.Radius, float(Extent.Z)); }
		if (const auto* Character = Cast<ACharacter>(Pawn)) Unit.bFlying = Character->GetCharacterMovement()->IsFlying();
		Unit.bMove = Intersects(Unit, Unit.Center(false));
		if (Unit.bMove && (!Unit.Control.IsValid() || bExternallyControlled))
		{ OutFailure = TEXT("An overlapping Pawn cannot accept an external displacement."); return false; }
	}
	Units.StableSort([](const auto& A, const auto& B) { return A.Radius > B.Radius; });
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiGiftClearance), false);
	for (const auto& Entry : S.Actors) if (Entry.Actor.IsValid()) Query.AddIgnoredActor(Entry.Actor.Get());
	for (const auto& Unit : Units) if (Unit.bMove && Unit.Pawn.IsValid()) Query.AddIgnoredActor(Unit.Pawn.Get());
	// Check attachments too: the old spawn preflight covered only the catalog's root box.
	FCollisionQueryParams BuildingQuery = Query; BuildingQuery.AddIgnoredActor(&SupportingActor);
	for (const FBox& Box : Footprints)
		if (S.World.OverlapBlockingTestByChannel(Box.GetCenter(), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(Box.GetExtent()), BuildingQuery))
		{ OutFailure = TEXT("Gift attachment footprint overlaps an immovable obstacle."); return false; }
	TArray<int32> Planned;
	int32 RemainingProjections = 256;
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		auto& Unit = Units[Index]; if (!Unit.bMove) continue;
		TArray<FVector> Candidates;
		const FVector Source = Unit.Original.GetLocation();
		const double Spacing = FMath::Max(100.0, Unit.Radius * 2.0 + Clearance);
		for (int32 Ring = 0; Ring < 8; ++Ring)
		{
			const FBox RingBox = Bounds.ExpandBy(Unit.Radius + Clearance + 5.0 + Ring * Spacing);
			for (const double X : {RingBox.Min.X, RingBox.Max.X})
			{
				Candidates.Add(FVector(X,FMath::Clamp(Source.Y,RingBox.Min.Y,RingBox.Max.Y),Source.Z));
				for (double Y = RingBox.Min.Y; Y <= RingBox.Max.Y; Y += Spacing) Candidates.Add(FVector(X,Y,Source.Z));
			}
			for (const double Y : {RingBox.Min.Y, RingBox.Max.Y})
			{
				Candidates.Add(FVector(FMath::Clamp(Source.X,RingBox.Min.X,RingBox.Max.X),Y,Source.Z));
				for (double X = RingBox.Min.X; X <= RingBox.Max.X; X += Spacing) Candidates.Add(FVector(X,Y,Source.Z));
			}
		}
		Candidates.StableSort([Source](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A,Source) < FVector::DistSquared2D(B,Source); });
		bool bFound = false;
		for (int32 CandidateIndex = 0; CandidateIndex < FMath::Min(512, Candidates.Num()); ++CandidateIndex)
		{
			FVector Destination = Candidates[CandidateIndex];
			if (!Unit.bFlying)
			{
				if (--RemainingProjections < 0)
				{ OutFailure = TEXT("Gift clearance projection budget exhausted; retry another candidate."); return false; }
				FVector Projected;
				double SurfaceHeight = 0;
				if (!GuLiLandingGround::Resolve(S.World,Destination,Projected,&SurfaceHeight,Unit.Pawn.Get())) continue;
				Destination = Unit.Soldier.IsValid() ? Projected
					: FVector(Projected.X,Projected.Y,SurfaceHeight+Unit.HalfHeight+3);
			}
			const FVector Center = Destination + (Unit.Soldier.IsValid() ? FVector(0,0,Unit.HalfHeight+3) : FVector::ZeroVector);
			if (Intersects(Unit,Center) || S.World.OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,
				FCollisionShape::MakeCapsule(Unit.Radius,Unit.HalfHeight),Query)) continue;
			bool bOccupied = false;
			for (int32 OtherIndex = 0; OtherIndex < Units.Num(); ++OtherIndex)
			{
				if (OtherIndex == Index || (Units[OtherIndex].bMove && !Planned.Contains(OtherIndex))) continue;
				const auto& Other = Units[OtherIndex]; const FVector OtherCenter = Other.Center(Other.bMove);
				if (FMath::Abs(Center.Z-OtherCenter.Z) < Unit.HalfHeight+Other.HalfHeight
					&& FVector::DistSquared2D(Center,OtherCenter) < FMath::Square(Unit.Radius+Other.Radius+Clearance)) { bOccupied = true; break; }
			}
			if (bOccupied) continue;
			Unit.Landing.SetLocation(Destination); Planned.Add(Index); bFound = true; break;
		}
		if (!bFound) { OutFailure = TEXT("No mutually clear, navigable exit for every overlapping unit."); return false; }
	}
	const FGuid Token = FGuid::NewGuid();
	TArray<FGuLiMassExternalUnit> Mass, OriginalMass;
	for (int32 Index : Planned)
	{
		const auto& Unit = Units[Index];
		if (Unit.Soldier.IsValid()) { Mass.Add({Unit.Soldier,Unit.Landing,Unit.Radius}); OriginalMass.Add({Unit.Soldier,Unit.Original,Unit.Radius}); }
		else if (!Unit.Control.IsValid() || Unit.Control->AreActionsLocked() || !Unit.Pawn.IsValid())
		{ OutFailure = TEXT("Gift clearance participant changed before commit."); return false; }
	}
	if (!Mass.IsEmpty() && (!Authority || !Authority->CanApplyExternalUnitState(Mass,Token)))
	{ OutFailure = TEXT("Mass clearance batch cannot commit at this boundary."); return false; }
	if (!Mass.IsEmpty() && !Authority->ApplyExternalUnitState(Mass,Token,false,false,true)) return false;
	TArray<int32> AppliedActors;
	for (int32 Index : Planned)
	{
		auto& Unit = Units[Index]; if (Unit.Soldier.IsValid()) continue;
		if (!Unit.Control->ApplyServerState(Token,false,false,Unit.Landing,true))
		{
			for (int32 Applied : AppliedActors) Units[Applied].Control->ApplyServerState(Token,false,false,Units[Applied].Original,true);
			if (!OriginalMass.IsEmpty()) Authority->ApplyExternalUnitState(OriginalMass,Token,false,false,true);
			OutFailure = TEXT("External displacement rejected; gift aborted and positions restored."); return false;
		}
		AppliedActors.Add(Index);
		if (auto* Controller = Cast<AAIController>(Unit.Pawn->GetController())) Controller->StopMovement();
		if (auto* Work = Unit.Pawn->FindComponentByClass<UGuLiConstructionWorkComponent>()) Work->StopWork();
		if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Unit.Pawn.Get())) Miner->CancelTaskForExternalDisplacement();
	}
	for (const auto& Entry : S.Actors) if (auto* Actor = Entry.Actor.Get()) Actor->SetActorEnableCollision(Entry.bCollision);
	for (const auto& Entry : S.Primitives) if (auto* Component = Entry.Component.Get()) Component->SetCanEverAffectNavigation(Entry.bNavigation);
	for (const auto& Entry : S.Navigation) if (auto* Component = Entry.Component.Get())
	{
		// Initialization may cache the failsafe bounds while staging has collision and
		// navigation disabled. Recompute from the restored bodies before publishing.
		Component->UpdateNavigationBounds();
		Component->SetNavigationRelevancy(Entry.bRelevant);
	}
	S.bCommitted = true;
	Building.ForceNetUpdate();
	UE_LOG(LogTemp,Display,TEXT("[GULI_GIFT_CLEARANCE] building=%s moved=%d mass=%d actors=%d"),*Building.GetName(),Planned.Num(),Mass.Num(),AppliedActors.Num());
	return true;
}
