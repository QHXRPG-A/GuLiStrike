#include "GuLiInitialArmyAuthoring.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Framework/GuLiCommanderDeploymentPoint.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingSpawner.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "GameFramework/Volume.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool Ground(UWorld& World, const FVector2D& XY, FVector& Out, float& Slope)
	{
		FHitResult Hit;
		if (!World.LineTraceSingleByChannel(Hit, FVector(XY, 1000000), FVector(XY, -1000000),
			ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(GuLiInitialArmyGround), false))) return false;
		Out = Hit.ImpactPoint;
		Slope = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.0, 1.0)));
		return true;
	}
	FString SlotName(const FGuLiCommanderInitialSpawnSlot& Slot)
	{
		return FString::Printf(TEXT("team=%u formation=%d slot=%d type=%u"),
			static_cast<uint8>(Slot.Team), Slot.FormationIndex, Slot.SlotIndex, Slot.UnitTypeId);
	}
}

bool FGuLiInitialArmyAuthoring::Build(UWorld& World, FString& OutError)
{
	Slots.Reset(); Reservations.Reset(); OutError.Reset();
	for (TActorIterator<AGuLiCommanderDeploymentPoint> It(&World); It; ++It)
	{
		OutError = TEXT("Canonical 500-unit resource authoring requires default deployment; remove the overriding deployment point.");
		return false;
	}
	UDataTable* Table = GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
	if (!Table || Table->GetRowStruct() != FGuLiStrikeCommanderSoldiersRow::StaticStruct())
	{
		OutError = TEXT("Initial army authoring requires the configured Soldiers DataTable."); return false;
	}
	TArray<FGuLiSoldierDefinition> Mass;
	TMap<uint16, float> ActorRadii;
	TSet<uint16> Seen;
	const auto Fallback = FGuLiCommanderSoldierResolver::MakeFallbackDefinition();
	for (FName Name : Table->GetRowNames())
	{
		bool bValid = false;
		const auto Definition = FGuLiCommanderSoldierResolver::Resolve(Table, Name, Fallback, bValid);
		if (!bValid || Seen.Contains(Definition.UnitTypeId))
		{
			OutError = FString::Printf(TEXT("Initial army: invalid/duplicate Soldiers row %s."), *Name.ToString()); return false;
		}
		Seen.Add(Definition.UnitTypeId);
		if (Definition.UsesMass()) Mass.Add(Definition);
		else
		{
			const FBox Bounds = Definition.GetModelBoundsCentimeters();
			const FVector Extent = Bounds.IsValid ? Bounds.GetExtent() : FVector(180.0f);
			ActorRadii.Add(Definition.UnitTypeId, FMath::Max(180.0f, static_cast<float>(FVector2D(Extent).Size())));
		}
	}
	Mass.Sort([](const auto& A, const auto& B) { return A.UnitTypeId < B.UnitTypeId; });
	FVector Red, Blue;
	float Slope = 0;
	if (!Ground(World, FVector2D(0, GULI_RESOURCE_ASSEMBLY_ANCHOR_Y_CM), Red, Slope)
		|| !Ground(World, FVector2D(0, -GULI_RESOURCE_ASSEMBLY_ANCHOR_Y_CM), Blue, Slope))
	{
		OutError = TEXT("Initial army assembly anchors have no supporting ground."); return false;
	}
	if (!GetDefault<UGuLiBattleAuthoritySubsystem>()->BuildInitialArmySpawnLayout(Mass, Red, Blue, Slots, OutError)) return false;
	for (const auto& Slot : Slots)
		Reservations.Add({ FVector2D(Slot.Location), Slot.RadiusCentimeters,
			SlotName(Slot), false });
	const auto* Economy = LoadObject<UGuLiResourceEconomyConfig>(nullptr,
		TEXT("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy.DA_ResourceEconomy"));
	UGuLiBuildingCatalog* Buildings = UGuLiBuildingCatalog::LoadDefaultCatalog();
	const FGuLiBuildingDefinition* Outpost = Buildings ? Buildings->FindById(7) : nullptr;
	if (!Economy || !Outpost || !ActorRadii.Contains(Economy->MiningVehicleUnitTypeId) || !ActorRadii.Contains(4))
	{
		OutError = TEXT("Initial army authoring requires economy, outpost and engineering definitions."); return false;
	}
	for (const EGuLiTeam Team : { EGuLiTeam::Red, EGuLiTeam::Blue })
	{
		const FVector Assembly = Team == EGuLiTeam::Red ? Red : Blue;
		const float Sign = Team == EGuLiTeam::Red ? 1.0f : -1.0f;
		Reservations.Add({ FVector2D(0, Sign * GULI_RESOURCE_FACTORY_ANCHOR_Y_CM),
			GULI_RESOURCE_FACTORY_OBSTACLE_HALF_EXTENT_CM * UE_SQRT_2, TEXT("InitialFactory"), true });
		for (const bool bConstruction : { false, true })
		{
			const int32 Count = bConstruction ? Economy->InitialConstructionVehiclesPerTeam : Economy->InitialMiningVehiclesPerTeam;
			for (int32 Index = 0; Index < Count; ++Index)
				Reservations.Add({ FVector2D(Assembly + GuLiResources::InitialEngineeringVehicleOffset(Team, Index, bConstruction)),
					ActorRadii.FindChecked(bConstruction ? 4 : Economy->MiningVehicleUnitTypeId),
					FString::Printf(TEXT("team=%u %s=%d"), static_cast<uint8>(Team), bConstruction ? TEXT("builder") : TEXT("miner"), Index), true });
		}
	}
	for (int32 Row = 1; Row <= GULI_RESOURCE_BOARD_DIMENSION; ++Row)
		for (int32 Column = 1; Column <= GULI_RESOURCE_BOARD_DIMENSION; ++Column)
		{
			float Radius = FVector2D(Outpost->CollisionExtent).Size();
			if (GuLiResources::IsPlayableTeam(GuLiResources::GetInitialTerritoryOwner(Row, Column)))
				for (int32 Index = 0; Index < Outpost->FirstCaptureGiftIds.Num(); ++Index)
				{
					const auto* Gift = Buildings->FindById(Outpost->FirstCaptureGiftIds[Index]);
					if (!Gift) { OutError = TEXT("Initial outpost references an unknown gift building."); return false; }
					for (const auto& Candidate : GuLiBuildings::GetGiftPlacementCandidates(FVector::ZeroVector, Index))
						Radius = FMath::Max(Radius, static_cast<float>(Candidate.GetLocation().Size2D()
							+ FVector2D(Gift->CollisionExtent).Size()));
				}
			Reservations.Add({ FVector2D(GuLiResources::GetTerritoryCenter(Row, Column)), Radius,
				GuLiResources::MakeTerritoryId(Row, Column).ToString(), true });
		}
	return true;
}

bool FGuLiInitialArmyAuthoring::IsOreClear(const FVector2D& Center) const
{
	for (const auto& Reserve : Reservations)
		if (FVector2D::DistSquared(Center, Reserve.Center) < FMath::Square(
			Reserve.Radius + GULI_RESOURCE_CLUSTER_OBSTACLE_RADIUS_CM + GuLiCommanderInitialSpawn::MaximumProjectionCorrection)) return false;
	return true;
}

bool FGuLiInitialArmyAuthoring::Validate(UWorld& World, const UGuLiResourceMapDefinition& Definition,
	int32& OutValidSlots, FString& OutError) const
{
	OutValidSlots = 0; OutError.Reset();
	auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	const auto* NavData = Navigation ? Navigation->GetNavDataForAgentName(TEXT("CommanderSoldier")) : nullptr;
	if (!NavData || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(&World))
	{
		OutError = TEXT("Initial army audit requires finished CommanderSoldier navigation."); return false;
	}
	TArray<FVector> Projected;
	for (const auto& Slot : Slots)
	{
		FString Failure;
		FVector OnGround;
		float Slope = 0;
		FNavLocation Nav;
		const float Correction = GuLiCommanderInitialSpawn::MaximumProjectionCorrection;
		if (!Ground(World, FVector2D(Slot.Location), OnGround, Slope) || Slope > 15.0f)
			Failure = TEXT("ground missing or steeper than 15 degrees");
		else if (!Navigation->ProjectPointToNavigation(Slot.Location, Nav,
			FVector(Correction, Correction, GuLiCommanderInitialSpawn::ProjectionVerticalExtent), NavData)
			|| FVector::Dist2D(Slot.Location, Nav.Location) > Correction)
			Failure = TEXT("CommanderSoldier projection exceeds 150 cm");
		else
		{
			const FVector2D XY(Nav.Location);
			if (FMath::Abs(XY.X) + Slot.RadiusCentimeters > GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM
				|| FMath::Abs(XY.Y) + Slot.RadiusCentimeters > GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM)
				Failure = TEXT("slot extends outside battlefield");
			for (const auto& Cluster : Definition.Clusters)
				if (FVector2D::DistSquared(XY, FVector2D(Cluster.Center)) < FMath::Square(
					Slot.RadiusCentimeters + Cluster.ObstacleRadiusCentimeters + Correction))
				{
					Failure = FString::Printf(TEXT("overlaps ore cluster %u"), Cluster.ClusterId); break;
				}
			for (const auto& Reserve : Reservations)
				if (Reserve.bBlocksArmy && FVector2D::DistSquared(XY, Reserve.Center)
					< FMath::Square(Slot.RadiusCentimeters + Reserve.Radius + Correction))
				{
					Failure = TEXT("overlaps ") + Reserve.Label; break;
				}
			for (int32 Index = 0; Index < Projected.Num(); ++Index)
				if (FVector::DistSquared2D(Nav.Location, Projected[Index]) < FMath::Square(FMath::Max(
					GuLiCommanderInitialSpawn::MinimumSeparation, Slot.RadiusCentimeters + Slots[Index].RadiusCentimeters)))
				{
					Failure = TEXT("insufficient separation from ") + SlotName(Slots[Index]); break;
				}
			TArray<FOverlapResult> Overlaps;
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
			World.OverlapMultiByObjectType(Overlaps, OnGround + FVector(0, 0, Slot.RadiusCentimeters + 10.0f),
				FQuat::Identity, Objects, FCollisionShape::MakeSphere(Slot.RadiusCentimeters),
				FCollisionQueryParams(SCENE_QUERY_STAT(GuLiInitialArmyOccupancy), false));
			for (const auto& Overlap : Overlaps)
			{
				const AActor* Actor = Overlap.GetActor();
				if (Actor && !Actor->IsEditorOnly() && !Actor->IsA<ALandscapeProxy>() && !Actor->IsA<AVolume>())
				{
					Failure = TEXT("overlaps scene actor ") + Actor->GetPathName(); break;
				}
			}
		}
		if (!Failure.IsEmpty())
		{
			OutError = SlotName(Slot) + TEXT(": ") + Failure; return false;
		}
		Projected.Add(Nav.Location); ++OutValidSlots;
	}
	return OutValidSlots == GuLiCommanderInitialSpawn::Population;
}

FString FGuLiInitialArmyAuthoring::ToJson() const
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const auto& Slot : Slots)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("team"), static_cast<uint8>(Slot.Team));
		Row->SetNumberField(TEXT("unit_type_id"), Slot.UnitTypeId);
		Row->SetNumberField(TEXT("formation"), Slot.FormationIndex);
		Row->SetNumberField(TEXT("slot"), Slot.SlotIndex);
		Row->SetNumberField(TEXT("x"), Slot.Location.X); Row->SetNumberField(TEXT("y"), Slot.Location.Y); Row->SetNumberField(TEXT("z"), Slot.Location.Z);
		Row->SetNumberField(TEXT("radius_cm"), Slot.RadiusCentimeters);
		Row->SetNumberField(TEXT("yaw"), Slot.FacingYawDegrees);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("slots"), Rows); Rows.Reset();
	for (const auto& Reserve : Reservations)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("x"), Reserve.Center.X); Row->SetNumberField(TEXT("y"), Reserve.Center.Y);
		Row->SetNumberField(TEXT("radius_cm"), Reserve.Radius);
		Row->SetStringField(TEXT("label"), Reserve.Label);
		Row->SetBoolField(TEXT("blocks_army"), Reserve.bBlocksArmy);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("reservations"), Rows);
	FString Json; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json)); return Json;
}
