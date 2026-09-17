#include "Gameplay/Building/GuLiBuildingSpawner.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Building/GuLiGroundAccessRampComponent.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Engine/World.h"

bool GuLiBuildings::ValidateGroundPlacement(UWorld& World, const FGuLiBuildingDefinition& Definition,
	const FTransform& GroundTransform, UClass* PresentationClass, const AActor* IgnoredActor, FString& OutReason)
{
	OutReason.Reset();
	const FVector Extent = Definition.CollisionExtent;
	if (GroundTransform.ContainsNaN() || Extent.ContainsNaN() || Extent.GetMin() <= 0)
	{
		OutReason = TEXT("Invalid building footprint or transform.");
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiBuildingGroundSupport), false, IgnoredActor);
	const double BaseZ = GroundTransform.GetLocation().Z;
	// Cover the interior as well as corners; large prefabs must not straddle a hole
	// or hillside merely because their center trace succeeded. Work remains bounded.
	const int32 StepsX = FMath::Clamp(FMath::CeilToInt(Extent.X * 2.0 / 1000.0), 2, 16);
	const int32 StepsY = FMath::Clamp(FMath::CeilToInt(Extent.Y * 2.0 / 1000.0), 2, 16);
	for (int32 Y = 0; Y <= StepsY; ++Y)
	{
		for (int32 X = 0; X <= StepsX; ++X)
		{
			const FVector Point = GroundTransform.TransformPosition(FVector(
				FMath::Lerp(-Extent.X, Extent.X, double(X) / StepsX),
				FMath::Lerp(-Extent.Y, Extent.Y, double(Y) / StepsY), 0));
			FHitResult Ground;
			if (!World.LineTraceSingleByObjectType(Ground,
				FVector(Point.X, Point.Y, BaseZ + UGuLiGroundAccessRampComponent::GroundTraceDistance),
				FVector(Point.X, Point.Y, BaseZ - UGuLiGroundAccessRampComponent::GroundTraceDistance),
				FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Ground.bStartPenetrating)
			{
				OutReason = FString::Printf(TEXT("No supporting ground inside building footprint at %s."), *Point.ToCompactString());
				return false;
			}
			if (Ground.GetActor() && Ground.GetActor()->FindComponentByClass<UGuLiBuildingLifecycleComponent>())
			{
				OutReason = TEXT("Another building cannot be used as supporting terrain.");
				return false;
			}
			if (FMath::Abs(Ground.ImpactPoint.Z - BaseZ) > UGuLiGroundAccessRampComponent::MaximumGroundRise
				|| !GuLiBuildingPlacementPolicy::IsSlopeAllowed(Ground.ImpactNormal))
			{
				OutReason = FString::Printf(TEXT("Uneven building footprint at %s (height delta %.1f cm, max %.0f; slope max %.0f degrees)."),
					*Ground.ImpactPoint.ToCompactString(), Ground.ImpactPoint.Z - BaseZ,
					UGuLiGroundAccessRampComponent::MaximumGroundRise, GuLiBuildingPlacementPolicy::MaximumSlopeDegrees);
				return false;
			}
		}
	}
	return !PresentationClass || UGuLiGroundAccessRampComponent::ValidatePresentationGround(
		World, PresentationClass, GroundTransform, Query, OutReason);
}

TArray<FTransform> GuLiBuildings::GetGiftPlacementCandidates(const FVector& OutpostGround, int32 SlotIndex)
{
	struct FCandidate { FTransform Transform; double DistanceSquared; int32 Order; };
	TArray<FCandidate> Candidates;
	const double PreferredYaw = SlotIndex * 90.0;
	const FVector Preferred = OutpostGround + FRotator(0, PreferredYaw, 0).Vector() * 7000.0;
	// The original slot stays first. All alternatives remain well inside a territory's 560 m half-width.
	for (const double Radius : { 7000.0, 10500.0, 14000.0, 17500.0, 21000.0 })
		for (const double AngleOffset : { 0.0, 45.0, -45.0, 90.0, -90.0, 135.0, -135.0, 180.0 })
		{
			const double Yaw = PreferredYaw + AngleOffset;
			const FVector Position = OutpostGround + FRotator(0, Yaw, 0).Vector() * Radius;
			Candidates.Add({ FTransform(FRotator(0, Yaw, 0), Position), FVector::DistSquared2D(Position, Preferred), Candidates.Num() });
		}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return FMath::IsNearlyEqual(A.DistanceSquared, B.DistanceSquared, 0.01)
			? A.Order < B.Order : A.DistanceSquared < B.DistanceSquared;
	});
	TArray<FTransform> Result;
	for (const auto& Candidate : Candidates)
		for (const double FacingOffset : { 0.0, 90.0, -90.0, 180.0 })
		{
			FTransform Transform = Candidate.Transform;
			Transform.SetRotation(FQuat(FRotator(0, Transform.Rotator().Yaw + FacingOffset, 0)));
			Result.Add(Transform);
		}
	return Result;
}

bool GuLiBuildings::ProjectPlacementCandidate(UWorld& World, const FTransform& Candidate, const AActor* IgnoredActor,
	FTransform& OutGroundTransform, AActor*& OutSupportingActor)
{
	OutSupportingActor = nullptr;
	FHitResult Ground;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiGiftGround), false, IgnoredActor);
	const FVector Position = Candidate.GetLocation();
	if (!World.LineTraceSingleByObjectType(Ground,
		Position + FVector(0, 0, UGuLiGroundAccessRampComponent::GroundTraceDistance),
		Position - FVector(0, 0, UGuLiGroundAccessRampComponent::GroundTraceDistance),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Ground.bStartPenetrating || !Ground.GetActor()
		|| Ground.GetActor()->FindComponentByClass<UGuLiBuildingLifecycleComponent>()) return false;
	OutGroundTransform = Candidate;
	OutGroundTransform.SetLocation(Ground.ImpactPoint);
	OutSupportingActor = Ground.GetActor();
	return true;
}

AActor* GuLiBuildings::Spawn(UWorld& World, int32 DefinitionId, EGuLiTeam Team,
	const FTransform& GroundTransform, const AActor& SupportingActor, int32 TerritoryIndex, EGuLiBuildingOrigin Origin,
	bool bCompleted, const FGuid& Builder, FString* OutFailure)
{
	check(World.GetNetMode() != NM_Client);
	const auto* Catalog = UGuLiBuildingCatalog::LoadDefaultCatalog();
	const auto* Resolved = Catalog ? Catalog->FindById(DefinitionId) : nullptr;
	if (!Resolved)
	{
		UE_LOG(LogTemp,Warning,TEXT("Building spawn rejected: definition ID %d is absent from Buildings."),DefinitionId);
		if (OutFailure) *OutFailure = TEXT("Building definition is unavailable.");
		return nullptr;
	}
	const auto& Definition = *Resolved;
	const auto* Resources = World.GetSubsystem<UGuLiResourceWorldSubsystem>();
	const auto* Config = Resources ? Resources->GetEconomyConfig() : nullptr;
	UClass* PresentationClass = nullptr;
	if (Definition.Category == EGuLiBuildingCategory::Factory)
	{
		PresentationClass = Config ? Config->FactoryPresentationClass.LoadSynchronous() : nullptr;
		if (!PresentationClass)
		{
			if (OutFailure) *OutFailure = TEXT("Factory presentation/configuration is unavailable.");
			return nullptr;
		}
	}
	FString GroundFailure;
	if (!ValidateGroundPlacement(World, Definition, GroundTransform, PresentationClass, nullptr, GroundFailure))
	{
		if (OutFailure) *OutFailure = MoveTemp(GroundFailure);
		return nullptr;
	}
	FCollisionQueryParams Clearance(SCENE_QUERY_STAT(GuLiBuildingSpawn), false, &SupportingActor);
	const FVector ClearanceCenter = GroundTransform.GetLocation() + FVector(0,0,Definition.CollisionExtent.Z + 20);
	if (World.OverlapBlockingTestByChannel(ClearanceCenter, GroundTransform.GetRotation(), ECC_Pawn,
		FCollisionShape::MakeBox(Definition.CollisionExtent), Clearance))
	{
		if (OutFailure) *OutFailure = TEXT("Building footprint is occupied.");
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (Definition.Category == EGuLiBuildingCategory::Factory)
	{
		auto* Factory = World.SpawnActor<AGuLiResourceFactoryActor>(AGuLiResourceFactoryActor::StaticClass(), GroundTransform, Params);
		if (!Factory) return nullptr;
		Factory->InitializeFactory(Team, *Config, GroundTransform.TransformPosition(FVector(Config->FactoryDockOffsetCentimeters,0,0)),
			World.GetSubsystem<UGuLiResourceWorldSubsystem>()->AllocateControllableActorId(), TerritoryIndex, Origin, bCompleted, Builder, DefinitionId);
		return Factory;
	}
	FTransform Transform = GroundTransform;
	Params.bDeferConstruction = true;
	Transform.AddToTranslation(FVector(0,0,Definition.CollisionExtent.Z));
	auto* Building = World.SpawnActor<AGuLiPlacedBuilding>(AGuLiPlacedBuilding::StaticClass(), Transform, Params);
	if (!Building) return nullptr;
	Building->InitializeFromDefinition(DefinitionId, Team, Builder, TerritoryIndex, Origin, bCompleted);
	Building->FinishSpawning(Transform);
	return Building;
}
