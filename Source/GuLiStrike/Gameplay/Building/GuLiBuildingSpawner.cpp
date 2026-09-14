#include "Gameplay/Building/GuLiBuildingSpawner.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Engine/World.h"

AActor* GuLiBuildings::Spawn(UWorld& World, int32 DefinitionId, EGuLiTeam Team,
	const FTransform& GroundTransform, const AActor& SupportingActor, int32 TerritoryIndex, EGuLiBuildingOrigin Origin,
	bool bCompleted, const FGuid& Builder)
{
	check(World.GetNetMode() != NM_Client);
	const auto* Resolved = UGuLiBuildingCatalog::LoadDefaultCatalog()->FindById(DefinitionId);
	if (!Resolved)
	{
		UE_LOG(LogTemp,Warning,TEXT("Building spawn rejected: definition ID %d is absent from Buildings."),DefinitionId);
		return nullptr;
	}
	const auto& Definition = *Resolved;
	FCollisionQueryParams Clearance(SCENE_QUERY_STAT(GuLiBuildingSpawn), false, &SupportingActor);
	const FVector ClearanceCenter = GroundTransform.GetLocation() + FVector(0,0,Definition.CollisionExtent.Z + 20);
	if (World.OverlapBlockingTestByChannel(ClearanceCenter, GroundTransform.GetRotation(), ECC_Pawn,
		FCollisionShape::MakeBox(Definition.CollisionExtent), Clearance)) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (Definition.Category == EGuLiBuildingCategory::Factory)
	{
		auto& Resources = *World.GetSubsystem<UGuLiResourceWorldSubsystem>();
		auto* Factory = World.SpawnActor<AGuLiResourceFactoryActor>(AGuLiResourceFactoryActor::StaticClass(), GroundTransform, Params);
		if (!Factory) return nullptr;
		const auto& Config = *Resources.GetEconomyConfig();
		Factory->InitializeFactory(Team, Config, GroundTransform.TransformPosition(FVector(Config.FactoryDockOffsetCentimeters,0,0)),
			Resources.AllocateControllableActorId(), TerritoryIndex, Origin, bCompleted, Builder, DefinitionId);
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
