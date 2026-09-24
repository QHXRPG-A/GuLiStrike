#include "GuLiConstructionReviewLibrary.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Engine/World.h"
#include "Editor.h"

namespace
{
	bool AuthorityPIE(UWorld* World) { return World && World->WorldType == EWorldType::PIE && World->GetNetMode() != NM_Client; }
}
bool UGuLiConstructionReviewLibrary::FundReview(UWorld* World)
{
	if (!AuthorityPIE(World)) return false;
	auto* Economy = World->GetSubsystem<UGuLiTeamEconomySubsystem>();
	if (!Economy || !Economy->IsMatchActive()) return false;
	for (auto Team : {EGuLiTeam::Red, EGuLiTeam::Blue})
		for (auto Type : {EGuLiResourceType::Blue, EGuLiResourceType::Red}) Economy->Credit(Team, Type, 5000);
	return true;
}
AActor* UGuLiConstructionReviewLibrary::SpawnSample(UWorld* World, int32 Id, FVector Ground, float Yaw, bool bCompleted)
{
	if (!AuthorityPIE(World)) return nullptr;
	const auto* Catalog = UGuLiBuildingCatalog::LoadDefaultCatalog(); const auto* D = Catalog ? Catalog->FindById(Id) : nullptr;
	auto* Resources = World->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (!D || !Resources || !Resources->IsRuntimeReady()) return nullptr;
	FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FRotator Rotation(0, Yaw, 0); const int32 Territory = Resources->FindTerritoryIndex(Ground);
	if (D->Category == EGuLiBuildingCategory::Factory)
	{
		auto* Factory = World->SpawnActor<AGuLiResourceFactoryActor>(Ground, Rotation, Params);
		if (!Factory) return nullptr;
		const auto* Config = Resources->GetEconomyConfig();
		Factory->InitializeFactory(EGuLiTeam::Red, *Config, Ground + Rotation.RotateVector(FVector(Config->FactoryDockOffsetCentimeters,0,0)),
			Resources->AllocateControllableActorId(), Territory, EGuLiBuildingOrigin::Manual, bCompleted, FGuid::NewGuid(), Id);
		return Factory;
	}
	auto* Building = World->SpawnActor<AGuLiPlacedBuilding>(Ground + FVector(0,0,D->CollisionExtent.Z), Rotation, Params);
	if (Building) Building->InitializeFromDefinition(Id, EGuLiTeam::Red, FGuid::NewGuid(), Territory, EGuLiBuildingOrigin::Manual, bCompleted);
	return Building;
}
bool UGuLiConstructionReviewLibrary::AdvanceSample(UGuLiBuildingLifecycleComponent* Lifecycle, float Progress)
{
	if (!Lifecycle || !AuthorityPIE(Lifecycle->GetWorld()) || !FMath::IsFinite(Progress)) return false;
	const float Work = FMath::Clamp(Progress, 0.f, 1.f) * Lifecycle->GetDefinition().ConstructionWork;
	Lifecycle->AddConstructionWork(FMath::Max(0.f, Work - Lifecycle->GetState().WorkDone)); return true;
}

bool UGuLiConstructionReviewLibrary::JoinReviewClient()
{
	if (!GEditor || !GEditor->PlayWorld) return false;
	GEditor->RequestLateJoin(); return true;
}
