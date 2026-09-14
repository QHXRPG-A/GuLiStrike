// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiResourceAuthoringLibrary.h"

#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapDensityMap.h"
#include "GuLiMapMarker.h"
#include "GuLiMapTypes.h"

#include "AI/Navigation/NavAgentInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/BrushComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "Engine/OverlapResult.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Volume.h"
#include "GameFramework/WorldSettings.h"
#include "LandscapeProxy.h"
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiResourceBake, Log, All);

namespace GuLiResourceAuthoring
{
	constexpr TCHAR CanonicalMapPackage[] = TEXT("/Game/Maps/LVL_CommanderMassPrototype");
	constexpr TCHAR MapDefinitionPath[] =
		TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap");
	constexpr TCHAR MapDefinitionPackage[] =
		TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap");
	constexpr TCHAR EconomyPath[] =
		TEXT("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy.DA_ResourceEconomy");
	constexpr TCHAR EconomyPackage[] =
		TEXT("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy");
	constexpr float TerritoryBoundaryMargin = 3000.0f;
	constexpr float MinimumClusterSpacing = 5500.0f;
	constexpr float OutpostExclusionRadius = 13000.0f;
	constexpr float SpawnExclusionRadius = 12000.0f;
	constexpr float MaximumSlopeDegrees = 15.0f;
	constexpr double Pi = UE_DOUBLE_PI;

	struct FClusterBakeSeed
	{
		uint8 TerritoryIndex = 0u;
		EGuLiResourceType ResourceType = EGuLiResourceType::Blue;
		FVector Center = FVector::ZeroVector;
		int32 SymmetryPair = INDEX_NONE;
		bool bMirrored = false;
	};

	struct FWeightedCandidate
	{
		FVector2D Location = FVector2D::ZeroVector;
		double Priority = 0.0;
	};

	struct FNodePattern
	{
		uint8 FamilyIndex = 0u;
		uint8 InitialAmount = 1u;
		FVector2D LocalOffset = FVector2D::ZeroVector;
		float YawDegrees = 0.0f;
		float UniformScale = 1.0f;
	};

	void AddIssue(FGuLiResourceBakeResult& Result, const FString& Message)
	{
		Result.Issues.Add(Message);
		if (Result.Message.IsEmpty()) Result.Message = Message;
	}

	FGuLiResourceBakeResult Fail(const FString& Message)
	{
		FGuLiResourceBakeResult Result;
		AddIssue(Result, Message);
		return Result;
	}

	UWorld* GetEditorWorld()
	{
		if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor) return nullptr;
		UWorld* World = GEditor->GetEditorWorldContext().World();
		return World && World->WorldType == EWorldType::Editor ? World : nullptr;
	}

	UGuLiMapAuthoringSubsystem* GetAuthoringSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>() : nullptr;
	}

	FString NormalizeMapPackage(const UWorld& World)
	{
		return UWorld::RemovePIEPrefix(World.GetOutermost()->GetName());
	}

	bool HashAuthoringFiles(const TMap<FString, FString>& Files, FString& OutHash)
	{
		if (Files.IsEmpty()) return false;
		FSHA1 Hash;
		TArray<FString> Names;
		Files.GetKeys(Names);
		Names.Sort();
		for (const FString& Name : Names)
		{
			const FString* Contents = Files.Find(Name);
			if (!Contents) return false;
			FTCHARToUTF8 NameUtf8(*Name);
			FTCHARToUTF8 ContentsUtf8(**Contents);
			const int32 NameLength = NameUtf8.Length();
			const int32 ContentsLength = ContentsUtf8.Length();
			Hash.Update(reinterpret_cast<const uint8*>(&NameLength), sizeof(NameLength));
			Hash.Update(reinterpret_cast<const uint8*>(NameUtf8.Get()), NameLength);
			Hash.Update(reinterpret_cast<const uint8*>(&ContentsLength), sizeof(ContentsLength));
			Hash.Update(reinterpret_cast<const uint8*>(ContentsUtf8.Get()), ContentsLength);
		}
		Hash.Final();
		uint8 Digest[FSHA1::DigestSize];
		Hash.GetHash(Digest);
		OutHash = BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
		return true;
	}

	void AppendMapIssues(const TArray<FGuLiMapIssue>& Issues, FGuLiResourceBakeResult& Result)
	{
		for (const FGuLiMapIssue& Issue : Issues)
		{
			const FString Prefix = Issue.Severity == EGuLiMapIssueSeverity::Error
				? TEXT("Error") : TEXT("Warning");
			AddIssue(Result, FString::Printf(TEXT("%s: %s"), *Prefix, *Issue.Message));
		}
	}

	bool BuildAuthoringFiles(
		FGuLiMapSnapshot& OutSnapshot,
		TMap<FString, FString>& OutFiles,
		FGuLiResourceBakeResult& Result)
	{
		UGuLiMapAuthoringSubsystem* Service = GetAuthoringSubsystem();
		if (!Service)
		{
			AddIssue(Result, TEXT("GuLiMapAuthoring editor subsystem is unavailable."));
			return false;
		}
		TArray<FGuLiMapIssue> Issues;
		if (!Service->CollectSnapshot(OutSnapshot, Issues, false)
			|| !GuLiMap::BuildFiles(OutSnapshot, OutFiles, Issues))
		{
			AppendMapIssues(Issues, Result);
			if (Result.Message.IsEmpty()) AddIssue(Result, TEXT("GuLiMapAuthoring snapshot validation failed."));
			return false;
		}
		AppendMapIssues(Issues, Result);
		return !GuLiMap::HasErrors(Issues);
	}

	bool EnsureProperty(
		FInstancedPropertyBag& Bag,
		const FName Name,
		const EPropertyBagPropertyType Type,
		FString& OutError)
	{
		if (const FPropertyBagPropertyDesc* Existing = Bag.FindPropertyDescByName(Name))
		{
			if (Existing->ValueType != Type || Existing->ContainerTypes.Num() != 0)
			{
				OutError = FString::Printf(TEXT("Outpost field %s has an incompatible type."), *Name.ToString());
				return false;
			}
			return true;
		}
		const EPropertyBagAlterationResult Alteration = Bag.AddProperty(Name, Type, nullptr, false);
		if (Alteration != EPropertyBagAlterationResult::Success)
		{
			OutError = FString::Printf(TEXT("Could not add Outpost field %s."), *Name.ToString());
			return false;
		}
		return true;
	}

	void ConfigureFieldRule(
		UGuLiMapTypeDefinition& Type,
		const FName FieldName,
		const FString& DisplayName,
		const bool bUseRange,
		const double Minimum,
		const double Maximum)
	{
		const FPropertyBagPropertyDesc* Desc = Type.DefaultParameters.FindPropertyDescByName(FieldName);
		if (!Desc) return;
		FGuLiMapFieldRule* Rule = Type.FieldRules.FindByPredicate(
			[&](const FGuLiMapFieldRule& Candidate) { return Candidate.FieldId == Desc->ID; });
		if (!Rule) Rule = &Type.FieldRules.AddDefaulted_GetRef();
		Rule->FieldId = Desc->ID;
		Rule->DisplayName = DisplayName;
		Rule->bRequired = true;
		Rule->bUseMinimum = bUseRange;
		Rule->Minimum = Minimum;
		Rule->bUseMaximum = bUseRange;
		Rule->Maximum = Maximum;
	}

	FGuLiMapRegionRecord MakeCaptureRegion(const FGuid ExistingId)
	{
		FGuLiMapRegionRecord Region;
		Region.RegionId = ExistingId.IsValid() ? ExistingId : FGuid::NewGuid();
		Region.RegionKey = TEXT("Capture");
		Region.DisplayName = TEXT("占领区");
		Region.bEnabled = true;
		FGuLiMapCylinder Cylinder;
		Cylinder.Radius = 10000.0;
		Cylinder.MinZ = -10000.0;
		Cylinder.MaxZ = 10000.0;
		Region.Geometry = FInstancedStruct::Make(Cylinder);
		return Region;
	}

	FGuLiMapRegionRecord MakeTerritoryRegion(const FGuid ExistingId)
	{
		FGuLiMapRegionRecord Region;
		Region.RegionId = ExistingId.IsValid() ? ExistingId : FGuid::NewGuid();
		Region.RegionKey = TEXT("Territory");
		Region.DisplayName = TEXT("据点辖区");
		Region.bEnabled = true;
		FGuLiMapPolygonPrism Polygon;
		Polygon.Vertices = {
			FVector2D(-GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM, -GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM),
			FVector2D( GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM, -GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM),
			FVector2D( GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM,  GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM),
			FVector2D(-GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM,  GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM)
		};
		Polygon.MinZ = -50000.0;
		Polygon.MaxZ = 50000.0;
		Region.Geometry = FInstancedStruct::Make(Polygon);
		return Region;
	}

	bool ConfigureOutpostType(UGuLiMapTypeDefinition& Type, FString& OutError)
	{
		Type.Modify();
		Type.DisplayName = TEXT("据点");
		Type.Color = FLinearColor(1.0f, 0.55f, 0.1f);
		Type.AllowedShapes.AddUnique(TEXT("Cylinder"));
		Type.AllowedShapes.AddUnique(TEXT("PolygonPrism"));
		if (!EnsureProperty(Type.DefaultParameters, TEXT("BoardRow"), EPropertyBagPropertyType::Int32, OutError)
			|| !EnsureProperty(Type.DefaultParameters, TEXT("BoardColumn"), EPropertyBagPropertyType::Int32, OutError)
			|| !EnsureProperty(Type.DefaultParameters, TEXT("InitialOwner"), EPropertyBagPropertyType::Name, OutError)
			|| !EnsureProperty(Type.DefaultParameters, TEXT("BlueClusterBudget"), EPropertyBagPropertyType::Int32, OutError)
			|| !EnsureProperty(Type.DefaultParameters, TEXT("RedClusterBudget"), EPropertyBagPropertyType::Int32, OutError))
		{
			return false;
		}
		Type.DefaultParameters.SetValueInt32(TEXT("BoardRow"), 1);
		Type.DefaultParameters.SetValueInt32(TEXT("BoardColumn"), 1);
		Type.DefaultParameters.SetValueName(TEXT("InitialOwner"), TEXT("Neutral"));
		Type.DefaultParameters.SetValueInt32(TEXT("BlueClusterBudget"), 0);
		Type.DefaultParameters.SetValueInt32(TEXT("RedClusterBudget"), 0);
		ConfigureFieldRule(Type, TEXT("BoardRow"), TEXT("棋盘行"), true, 1.0, 5.0);
		ConfigureFieldRule(Type, TEXT("BoardColumn"), TEXT("棋盘列"), true, 1.0, 5.0);
		ConfigureFieldRule(Type, TEXT("InitialOwner"), TEXT("初始归属"), false, 0.0, 0.0);
		ConfigureFieldRule(Type, TEXT("BlueClusterBudget"), TEXT("蓝矿簇预算"), true, 0.0, 10.0);
		ConfigureFieldRule(Type, TEXT("RedClusterBudget"), TEXT("红矿簇预算"), true, 0.0, 4.0);
		FGuid CaptureId;
		FGuid TerritoryId;
		if (const FGuLiMapRegionRecord* Existing = Type.DefaultRegions.FindByPredicate(
			[](const FGuLiMapRegionRecord& Region) { return Region.RegionKey == TEXT("Capture"); }))
		{
			CaptureId = Existing->RegionId;
		}
		if (const FGuLiMapRegionRecord* Existing = Type.DefaultRegions.FindByPredicate(
			[](const FGuLiMapRegionRecord& Region) { return Region.RegionKey == TEXT("Territory"); }))
		{
			TerritoryId = Existing->RegionId;
		}
		Type.DefaultRegions = { MakeCaptureRegion(CaptureId), MakeTerritoryRegion(TerritoryId) };
		Type.MarkPackageDirty();
		return true;
	}

	bool ParseCanonicalKey(const FName Key, int32& OutRow, int32& OutColumn)
	{
		const FString Value = Key.ToString();
		if (!Value.StartsWith(TEXT("Outpost_R"))) return false;
		int32 CIndex = INDEX_NONE;
		if (!Value.FindChar(TEXT('C'), CIndex) || CIndex <= 9) return false;
		OutRow = FCString::Atoi(*Value.Mid(9, CIndex - 9));
		OutColumn = FCString::Atoi(*Value.Mid(CIndex + 1));
		return GuLiResources::ToTerritoryIndex(OutRow, OutColumn) != INDEX_NONE
			&& Value == GuLiResources::MakeTerritoryId(OutRow, OutColumn).ToString();
	}

	bool TraceGround(UWorld& World, const FVector2D XY, FVector& OutLocation, FVector& OutNormal)
	{
		FHitResult Hit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiResourceBakeGround), false);
		const FVector Start(XY.X, XY.Y, 1000000.0f);
		const FVector End(XY.X, XY.Y, -1000000.0f);
		if (!World.LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams)
			|| !Hit.bBlockingHit || Hit.ImpactNormal.ContainsNaN())
		{
			return false;
		}
		const double Slope = FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(static_cast<double>(Hit.ImpactNormal.Z), -1.0, 1.0)));
		if (Slope > MaximumSlopeDegrees) return false;
		OutLocation = Hit.ImpactPoint;
		OutNormal = Hit.ImpactNormal;
		return true;
	}

	FVector ProjectGroundOrXY(UWorld& World, const FVector2D XY)
	{
		FVector Location;
		FVector Normal;
		return TraceGround(World, XY, Location, Normal) ? Location : FVector(XY, 0.0f);
	}

	bool IsExplicitlyExcluded(const FVector2D Location)
	{
		for (int32 Row = 1; Row <= GULI_RESOURCE_BOARD_DIMENSION; ++Row)
		{
			for (int32 Column = 1; Column <= GULI_RESOURCE_BOARD_DIMENSION; ++Column)
			{
				if (FVector2D::DistSquared(Location, FVector2D(GuLiResources::GetTerritoryCenter(Row, Column)))
					< FMath::Square(OutpostExclusionRadius)) return true;
			}
		}
		const FVector2D Anchors[] = {
			FVector2D(0.0, 252000.0), FVector2D(0.0, 196000.0),
			FVector2D(0.0, -252000.0), FVector2D(0.0, -196000.0)
		};
		for (const FVector2D Anchor : Anchors)
		{
			if (FVector2D::DistSquared(Location, Anchor) < FMath::Square(SpawnExclusionRadius)) return true;
		}
		return false;
	}

	bool IsSpatiallyClear(UWorld& World, const FVector& GroundLocation)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiResourceBakeOccupancy), false);
		const FVector ProbeCenter = GroundLocation + FVector(0.0, 0.0, 2400.0);
		World.OverlapMultiByObjectType(
			Overlaps,
			ProbeCenter,
			FQuat::Identity,
			Objects,
			FCollisionShape::MakeSphere(GULI_RESOURCE_CLUSTER_OBSTACLE_RADIUS_CM - 100.0f),
			Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const AActor* Actor = Overlap.GetActor();
			if (!Actor || Actor->IsA<ALandscapeProxy>() || Actor->IsA<AWorldSettings>()
				|| Actor->IsA<AVolume>() || Actor->IsA<AGuLiMapMarker>()
				|| Actor->IsA<AGuLiMapDensityMap>())
			{
				continue;
			}
			return false;
		}
		return true;
	}

	bool IsNavigable(UWorld& World, const FVector& Location)
	{
		const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!Navigation) return false;
		const FName AgentNames[] = { TEXT("Default"), TEXT("CommanderSoldier") };
		for (const FName AgentName : AgentNames)
		{
			const ANavigationData* NavData = Navigation->GetNavDataForAgentName(AgentName);
			if (!NavData) return false;
			FNavLocation Projected;
			if (!Navigation->ProjectPointToNavigation(
				Location, Projected, FVector(2500.0f, 2500.0f, 5000.0f), NavData)
				|| FVector::DistSquared2D(Projected.Location, Location) > FMath::Square(2500.0f))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateClusterCandidate(UWorld& World, const FVector2D XY, FVector& OutCenter)
	{
		FVector Normal;
		return !IsExplicitlyExcluded(XY)
			&& TraceGround(World, XY, OutCenter, Normal)
			&& IsNavigable(World, OutCenter)
			&& IsSpatiallyClear(World, OutCenter);
	}

	bool HasMinimumSpacing(const FVector2D Location, const TArray<FVector2D>& Accepted)
	{
		for (const FVector2D Other : Accepted)
		{
			if (FVector2D::DistSquared(Location, Other) < FMath::Square(MinimumClusterSpacing))
				return false;
		}
		return true;
	}

	uint32 CandidateHash(
		const int32 TerritoryIndex,
		const EGuLiResourceType Type,
		const FIntPoint Cell)
	{
		uint32 Hash = GetTypeHash(GULI_RESOURCE_BAKE_SEED);
		Hash = HashCombineFast(Hash, GetTypeHash(TerritoryIndex));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Type)));
		Hash = HashCombineFast(Hash, GetTypeHash(Cell.X));
		Hash = HashCombineFast(Hash, GetTypeHash(Cell.Y));
		return Hash;
	}

	bool BuildWeightedCandidates(
		const FGuLiMapDensityMapRecord& Density,
		const FGuLiMapDensityLayer& Layer,
		const int32 TerritoryIndex,
		const int32 MirrorTerritoryIndex,
		const EGuLiResourceType Type,
		TArray<FWeightedCandidate>& OutCandidates)
	{
		OutCandidates.Reset();
		const int32 Row = TerritoryIndex / GULI_RESOURCE_BOARD_DIMENSION + 1;
		const int32 Column = TerritoryIndex % GULI_RESOURCE_BOARD_DIMENSION + 1;
		const FVector2D Center(GuLiResources::GetTerritoryCenter(Row, Column));
		const double CellSize = Density.CellSizeCm;
		const int32 MinimumCellX = GuLiMap::WorldToDensityCell(
			Center.X - GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM + TerritoryBoundaryMargin, CellSize);
		const int32 MaximumCellX = GuLiMap::WorldToDensityCell(
			Center.X + GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - TerritoryBoundaryMargin - UE_DOUBLE_SMALL_NUMBER, CellSize);
		const int32 MinimumCellY = GuLiMap::WorldToDensityCell(
			Center.Y - GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM + TerritoryBoundaryMargin, CellSize);
		const int32 MaximumCellY = GuLiMap::WorldToDensityCell(
			Center.Y + GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - TerritoryBoundaryMargin - UE_DOUBLE_SMALL_NUMBER, CellSize);
		for (int32 CellY = MinimumCellY; CellY <= MaximumCellY; ++CellY)
		{
			for (int32 CellX = MinimumCellX; CellX <= MaximumCellX; ++CellX)
			{
				const FVector2D Location(
					(static_cast<double>(CellX) + 0.5) * CellSize,
					(static_cast<double>(CellY) + 0.5) * CellSize);
				const FVector2D Local = Location - Center;
				if (FMath::Abs(Local.X) > GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - TerritoryBoundaryMargin
					|| FMath::Abs(Local.Y) > GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - TerritoryBoundaryMargin)
				{
					continue;
				}
				if (TerritoryIndex == MirrorTerritoryIndex
					&& (Location.X < 0.0 || (FMath::IsNearlyZero(Location.X) && Location.Y <= 0.0)))
				{
					continue;
				}
				const FIntPoint Cell(CellX, CellY);
				const FIntPoint MirrorCell(
					GuLiMap::WorldToDensityCell(-Location.X, CellSize),
					GuLiMap::WorldToDensityCell(-Location.Y, CellSize));
				const int32 Weight = static_cast<int32>(GuLiMap::GetDensityCell(Layer, Cell))
					+ static_cast<int32>(GuLiMap::GetDensityCell(Layer, MirrorCell));
				if (Weight <= 0) continue;
				const double UnitRandom = (static_cast<double>(CandidateHash(TerritoryIndex, Type, Cell)) + 1.0)
					/ (static_cast<double>(MAX_uint32) + 2.0);
				FWeightedCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
				Candidate.Location = Location;
				Candidate.Priority = -FMath::Loge(UnitRandom) / static_cast<double>(Weight);
			}
		}
		OutCandidates.Sort([](const FWeightedCandidate& A, const FWeightedCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.Priority, B.Priority)) return A.Priority < B.Priority;
			if (!FMath::IsNearlyEqual(A.Location.Y, B.Location.Y)) return A.Location.Y > B.Location.Y;
			return A.Location.X < B.Location.X;
		});
		return !OutCandidates.IsEmpty();
	}

	const FGuLiMapDensityLayer* FindDensityLayer(
		const FGuLiMapDensityMapRecord& Density,
		const EGuLiResourceType Type)
	{
		const FName LayerKey = Type == EGuLiResourceType::Blue ? TEXT("BlueOre") : TEXT("RedOre");
		return Density.Layers.FindByPredicate(
			[&](const FGuLiMapDensityLayer& Layer) { return Layer.LayerKey == LayerKey; });
	}

	TArray<FNodePattern> MakeNodePattern(int32 PairId, EGuLiResourceType Type);

	bool ValidateNodePatternFootprint(
		UWorld& World,
		const FVector2D PrimaryCenter,
		const FVector2D MirrorCenter,
		const int32 PairId,
		const EGuLiResourceType Type)
	{
		const TArray<FNodePattern> Pattern = MakeNodePattern(PairId, Type);
		if (Pattern.Num() != GULI_RESOURCE_NODES_PER_CLUSTER)
		{
			return false;
		}
		for (const FNodePattern& Node : Pattern)
		{
			FVector Location;
			FVector Normal;
			if (!TraceGround(World, PrimaryCenter + Node.LocalOffset, Location, Normal)
				|| !TraceGround(World, MirrorCenter - Node.LocalOffset, Location, Normal))
			{
				return false;
			}
		}
		return true;
	}

	bool SampleClusters(
		UWorld& World,
		const FGuLiMapDensityMapRecord& Density,
		TArray<FClusterBakeSeed>& OutClusters,
		FString& OutError)
	{
		OutClusters.Reset();
		TArray<FVector2D> AcceptedLocations;
		int32 PairId = 0;
		for (int32 TerritoryIndex = 0; TerritoryIndex < GULI_RESOURCE_TERRITORY_COUNT; ++TerritoryIndex)
		{
			const int32 MirrorTerritoryIndex = GULI_RESOURCE_TERRITORY_COUNT - 1 - TerritoryIndex;
			if (TerritoryIndex > MirrorTerritoryIndex) continue;
			const int32 Row = TerritoryIndex / GULI_RESOURCE_BOARD_DIMENSION + 1;
			const int32 Column = TerritoryIndex % GULI_RESOURCE_BOARD_DIMENSION + 1;
			for (const EGuLiResourceType Type : { EGuLiResourceType::Blue, EGuLiResourceType::Red })
			{
				const int32 Budget = Type == EGuLiResourceType::Blue
					? GuLiResources::GetBlueClusterBudget(Row, Column)
					: GuLiResources::GetRedClusterBudget(Row, Column);
				if (Budget == 0) continue;
				const FGuLiMapDensityLayer* Layer = FindDensityLayer(Density, Type);
				if (!Layer)
				{
					OutError = TEXT("Required BlueOre/RedOre density layer is missing.");
					return false;
				}
				TArray<FWeightedCandidate> Candidates;
				if (!BuildWeightedCandidates(
					Density, *Layer, TerritoryIndex, MirrorTerritoryIndex, Type, Candidates))
				{
					OutError = FString::Printf(TEXT("%s has no painted candidates in R%dC%d."),
						Type == EGuLiResourceType::Blue ? TEXT("BlueOre") : TEXT("RedOre"), Row, Column);
					return false;
				}
				const int32 RequiredCandidatePairs = TerritoryIndex == MirrorTerritoryIndex
					? Budget / 2 : Budget;
				if (TerritoryIndex == MirrorTerritoryIndex && Budget % 2 != 0)
				{
					OutError = TEXT("The center Territory budget must be even for 180-degree pairing.");
					return false;
				}
				int32 AcceptedPairCount = 0;
				for (const FWeightedCandidate& Candidate : Candidates)
				{
					const FVector2D MirrorLocation = -Candidate.Location;
					if (!HasMinimumSpacing(Candidate.Location, AcceptedLocations)
						|| !HasMinimumSpacing(MirrorLocation, AcceptedLocations)
						|| FVector2D::DistSquared(Candidate.Location, MirrorLocation)
							< FMath::Square(MinimumClusterSpacing))
					{
						continue;
					}
					FVector Center;
					FVector MirrorCenter;
					if (!ValidateClusterCandidate(World, Candidate.Location, Center)
						|| !ValidateClusterCandidate(World, MirrorLocation, MirrorCenter)
						|| !ValidateNodePatternFootprint(
							World, Candidate.Location, MirrorLocation, PairId, Type))
					{
						continue;
					}
					FClusterBakeSeed& Primary = OutClusters.AddDefaulted_GetRef();
					Primary.TerritoryIndex = static_cast<uint8>(TerritoryIndex);
					Primary.ResourceType = Type;
					Primary.Center = Center;
					Primary.SymmetryPair = PairId;
					Primary.bMirrored = false;
					FClusterBakeSeed& Mirror = OutClusters.AddDefaulted_GetRef();
					Mirror.TerritoryIndex = static_cast<uint8>(MirrorTerritoryIndex);
					Mirror.ResourceType = Type;
					Mirror.Center = MirrorCenter;
					Mirror.SymmetryPair = PairId;
					Mirror.bMirrored = true;
					++PairId;
					AcceptedLocations.Add(Candidate.Location);
					AcceptedLocations.Add(MirrorLocation);
					if (++AcceptedPairCount == RequiredCandidatePairs) break;
				}
				if (AcceptedPairCount != RequiredCandidatePairs)
				{
					OutError = FString::Printf(
						TEXT("Bake rejected R%dC%d %s: accepted %d/%d symmetric candidates after ground, slope, nav, occupancy and spacing checks."),
						Row, Column, Type == EGuLiResourceType::Blue ? TEXT("BlueOre") : TEXT("RedOre"),
						AcceptedPairCount, RequiredCandidatePairs);
					return false;
				}
			}
		}
		if (OutClusters.Num() != GULI_RESOURCE_CLUSTER_COUNT)
		{
			OutError = FString::Printf(TEXT("Sampling produced %d/%d clusters."),
				OutClusters.Num(), GULI_RESOURCE_CLUSTER_COUNT);
			return false;
		}
		OutClusters.Sort([](const FClusterBakeSeed& A, const FClusterBakeSeed& B)
		{
			if (A.TerritoryIndex != B.TerritoryIndex) return A.TerritoryIndex < B.TerritoryIndex;
			if (A.ResourceType != B.ResourceType)
				return static_cast<uint8>(A.ResourceType) < static_cast<uint8>(B.ResourceType);
			if (!FMath::IsNearlyEqual(A.Center.Y, B.Center.Y)) return A.Center.Y > B.Center.Y;
			return A.Center.X < B.Center.X;
		});
		return true;
	}

	TArray<FNodePattern> MakeNodePattern(const int32 PairId, const EGuLiResourceType Type)
	{
		FRandomStream Stream(GULI_RESOURCE_BAKE_SEED
			^ (PairId + 1) * 196613
			^ (static_cast<int32>(Type) + 1) * 3145739);
		const float ClusterRotation = Stream.FRandRange(0.0f, 360.0f);
		TArray<FNodePattern> Pattern;
		Pattern.Reserve(GULI_RESOURCE_NODES_PER_CLUSTER);
		for (int32 Index = 0; Index < GULI_RESOURCE_NODES_PER_CLUSTER; ++Index)
		{
			FNodePattern& Node = Pattern.AddDefaulted_GetRef();
			Node.InitialAmount = Index < 3 ? 3u : (Index < 11 ? 2u : 1u);
			Node.FamilyIndex = static_cast<uint8>(Stream.RandRange(0, 3));
			const int32 RingIndex = Index < 3 ? Index : (Index < 11 ? Index - 3 : Index - 11);
			const int32 RingCount = Index < 3 ? 3 : (Index < 11 ? 8 : 15);
			const float MinimumRadius = Index < 3 ? 0.0f : (Index < 11 ? 850.0f : 1650.0f);
			const float MaximumRadius = Index < 3 ? 600.0f : (Index < 11 ? 1500.0f : 2450.0f);
			const float Radius = Index == 0 ? 0.0f : Stream.FRandRange(MinimumRadius, MaximumRadius);
			const float Angle = ClusterRotation + 360.0f * static_cast<float>(RingIndex) / RingCount
				+ Stream.FRandRange(-7.5f, 7.5f);
			Node.LocalOffset = FVector2D(
				FMath::Cos(FMath::DegreesToRadians(Angle)) * Radius,
				FMath::Sin(FMath::DegreesToRadians(Angle)) * Radius);
			Node.YawDegrees = Stream.FRandRange(0.0f, 360.0f);
			const float BaseScale = Node.InitialAmount == 3u ? 1.08f
				: (Node.InitialAmount == 2u ? 0.84f : 0.62f);
			Node.UniformScale = BaseScale * Stream.FRandRange(0.92f, 1.08f);
		}
		return Pattern;
	}

	bool BuildRuntimeDefinition(
		UWorld& World,
		const FGuLiMapSnapshot& Snapshot,
		const FString& SourceHash,
		UGuLiResourceMapDefinition& Definition,
		FString& OutError)
	{
		if (!Snapshot.DensityMap.IsSet()
			|| !FMath::IsNearlyEqual(Snapshot.DensityMap->CellSizeCm, 2500.0))
		{
			OutError = TEXT("A 2500 cm GuLiMap density map is required.");
			return false;
		}
		Definition.MapPackage = FName(*Snapshot.MapPackage);
		Definition.LayoutVersion = GULI_RESOURCE_LAYOUT_VERSION;
		Definition.DeterministicSeed = GULI_RESOURCE_BAKE_SEED;
		Definition.SourceHash = SourceHash;
		Definition.PlayableMinimum = FVector2D(-GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM);
		Definition.PlayableMaximum = FVector2D(GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM);
		Definition.SpawnAnchors.RedFactory = ProjectGroundOrXY(World, FVector2D(0.0, 252000.0));
		Definition.SpawnAnchors.RedAssembly = ProjectGroundOrXY(World, FVector2D(0.0, 196000.0));
		Definition.SpawnAnchors.BlueFactory = ProjectGroundOrXY(World, FVector2D(0.0, -252000.0));
		Definition.SpawnAnchors.BlueAssembly = ProjectGroundOrXY(World, FVector2D(0.0, -196000.0));

		TMap<int32, const FGuLiMapSnapshotEntry*> OutpostsByIndex;
		for (const FGuLiMapSnapshotEntry& Entry : Snapshot.Markers)
		{
			const UGuLiMapTypeDefinition* Type = Entry.Record.Type.LoadSynchronous();
			if (!Type || Type->TypeId != TEXT("Outpost")) continue;
			const TValueOrError<int32, EPropertyBagResult> RowValue =
				Entry.Record.Parameters.GetValueInt32(TEXT("BoardRow"));
			const TValueOrError<int32, EPropertyBagResult> ColumnValue =
				Entry.Record.Parameters.GetValueInt32(TEXT("BoardColumn"));
			if (!RowValue.HasValue() || !ColumnValue.HasValue())
			{
				OutError = TEXT("Every Outpost marker must contain BoardRow and BoardColumn fields.");
				return false;
			}
			const int32 Index = GuLiResources::ToTerritoryIndex(RowValue.GetValue(), ColumnValue.GetValue());
			if (Index == INDEX_NONE || OutpostsByIndex.Contains(Index))
			{
				OutError = TEXT("Outpost board coordinates are invalid or duplicated.");
				return false;
			}
			OutpostsByIndex.Add(Index, &Entry);
		}
		if (OutpostsByIndex.Num() != GULI_RESOURCE_TERRITORY_COUNT)
		{
			OutError = FString::Printf(TEXT("Expected 25 Outpost markers, found %d."), OutpostsByIndex.Num());
			return false;
		}

		Definition.Territories.Reset(GULI_RESOURCE_TERRITORY_COUNT);
		for (int32 Index = 0; Index < GULI_RESOURCE_TERRITORY_COUNT; ++Index)
		{
			const int32 Row = Index / GULI_RESOURCE_BOARD_DIMENSION + 1;
			const int32 Column = Index % GULI_RESOURCE_BOARD_DIMENSION + 1;
			const FGuLiMapSnapshotEntry* Entry = OutpostsByIndex.FindRef(Index);
			if (!Entry || Entry->Record.MarkerKey != GuLiResources::MakeTerritoryId(Row, Column))
			{
				OutError = TEXT("Outpost MarkerKey does not match its board coordinate.");
				return false;
			}
			const FGuLiMapRegionRecord* TerritoryRegion = Entry->Record.Regions.FindByPredicate(
				[](const FGuLiMapRegionRecord& Region) { return Region.RegionKey == TEXT("Territory"); });
			const FGuLiMapPolygonPrism* Polygon = TerritoryRegion
				? TerritoryRegion->Geometry.GetPtr<FGuLiMapPolygonPrism>() : nullptr;
			if (!Polygon || Polygon->Vertices.Num() != 4)
			{
				OutError = TEXT("Every Outpost requires a PolygonPrism Territory region.");
				return false;
			}
			FGuLiTerritoryDefinition& Territory = Definition.Territories.AddDefaulted_GetRef();
			Territory.TerritoryId = Entry->Record.MarkerKey;
			Territory.BoardRow = static_cast<uint8>(Row);
			Territory.BoardColumn = static_cast<uint8>(Column);
			Territory.InitialOwner = Row == 1 && Column == 3 ? EGuLiTeam::Red
				: (Row == 5 && Column == 3 ? EGuLiTeam::Blue : EGuLiTeam::Unassigned);
			Territory.BlueClusterBudget = GuLiResources::GetBlueClusterBudget(Row, Column);
			Territory.RedClusterBudget = GuLiResources::GetRedClusterBudget(Row, Column);
			Territory.Center = GuLiResources::GetTerritoryCenter(Row, Column);
			Territory.LocalPolygon = Polygon->Vertices;
		}

		TArray<FClusterBakeSeed> BakedClusters;
		if (!SampleClusters(World, Snapshot.DensityMap.GetValue(), BakedClusters, OutError)) return false;
		Definition.Clusters.Reset(BakedClusters.Num());
		for (int32 ClusterIndex = 0; ClusterIndex < BakedClusters.Num(); ++ClusterIndex)
		{
			const FClusterBakeSeed& Seed = BakedClusters[ClusterIndex];
			FGuLiResourceClusterDefinition& Cluster = Definition.Clusters.AddDefaulted_GetRef();
			Cluster.ClusterId = static_cast<uint16>(ClusterIndex + 1);
			Cluster.TerritoryIndex = Seed.TerritoryIndex;
			Cluster.ResourceType = Seed.ResourceType;
			Cluster.Center = Seed.Center;
			Cluster.ObstacleRadiusCentimeters = GULI_RESOURCE_CLUSTER_OBSTACLE_RADIUS_CM;
			Cluster.FirstNodeIndex = static_cast<uint32>(ClusterIndex * GULI_RESOURCE_NODES_PER_CLUSTER);
			Cluster.NodeCount = GULI_RESOURCE_NODES_PER_CLUSTER;
		}

		TMap<int32, TArray<FNodePattern>> PatternByPair;
		for (const FClusterBakeSeed& Cluster : BakedClusters)
		{
			if (!PatternByPair.Contains(Cluster.SymmetryPair))
			{
				PatternByPair.Add(Cluster.SymmetryPair,
					MakeNodePattern(Cluster.SymmetryPair, Cluster.ResourceType));
			}
		}
		Definition.Nodes.Reset(GULI_RESOURCE_NODE_COUNT);
		for (int32 ClusterIndex = 0; ClusterIndex < BakedClusters.Num(); ++ClusterIndex)
		{
			const FClusterBakeSeed& ClusterSeed = BakedClusters[ClusterIndex];
			const TArray<FNodePattern>* Pattern = PatternByPair.Find(ClusterSeed.SymmetryPair);
			if (!Pattern || Pattern->Num() != GULI_RESOURCE_NODES_PER_CLUSTER)
			{
				OutError = TEXT("Internal node pattern generation failed.");
				return false;
			}
			for (int32 LocalIndex = 0; LocalIndex < Pattern->Num(); ++LocalIndex)
			{
				const FNodePattern& PatternNode = (*Pattern)[LocalIndex];
				const FVector2D Offset = ClusterSeed.bMirrored
					? -PatternNode.LocalOffset : PatternNode.LocalOffset;
				const FVector2D NodeXY = FVector2D(ClusterSeed.Center) + Offset;
				FVector NodeLocation;
				FVector SurfaceNormal;
				if (!TraceGround(World, NodeXY, NodeLocation, SurfaceNormal))
				{
					OutError = FString::Printf(TEXT("Node ground/slope projection failed for cluster seed %d."),
						ClusterSeed.SymmetryPair);
					return false;
				}
				FGuLiResourceNodeDefinition& Node = Definition.Nodes.AddDefaulted_GetRef();
				Node.NodeId = static_cast<uint32>(Definition.Nodes.Num());
				Node.ClusterId = static_cast<uint16>(ClusterIndex + 1);
				Node.ResourceType = ClusterSeed.ResourceType;
				Node.FamilyIndex = PatternNode.FamilyIndex;
				Node.InitialAmount = PatternNode.InitialAmount;
				const float Yaw = FRotator::NormalizeAxis(PatternNode.YawDegrees
					+ (ClusterSeed.bMirrored ? 180.0f : 0.0f));
				Node.WorldTransform = FTransform(
					FRotator(0.0f, Yaw, 0.0f),
					NodeLocation,
					FVector(PatternNode.UniformScale));
			}
		}
		Definition.LayoutHash = Definition.CalculateLayoutHash();
		return Definition.ValidateDefinition(OutError);
	}

	template <typename TAsset>
	TAsset* FindOrCreateAsset(const TCHAR* ObjectPath, const TCHAR* PackagePath, bool& bOutCreated)
	{
		bOutCreated = false;
		if (TAsset* Existing = LoadObject<TAsset>(nullptr, ObjectPath)) return Existing;
		UPackage* Package = CreatePackage(PackagePath);
		if (!Package) return nullptr;
		const FName AssetName(*FPackageName::GetLongPackageAssetName(PackagePath));
		TAsset* Asset = NewObject<TAsset>(
			Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (Asset)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			bOutCreated = true;
		}
		return Asset;
	}

	bool SaveManagedAssets(
		UGuLiResourceMapDefinition& Definition,
		UGuLiResourceEconomyConfig& Economy,
		FString& OutError)
	{
		TArray<UPackage*> Packages = { Definition.GetOutermost(), Economy.GetOutermost() };
		if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true))
		{
			OutError = TEXT("Could not save the managed resource DataAssets.");
			return false;
		}
		return true;
	}

	bool ConfigureNavigationBounds(UWorld& World, FString& OutError)
	{
		TArray<ANavMeshBoundsVolume*> Volumes;
		for (TActorIterator<ANavMeshBoundsVolume> It(&World); It; ++It) Volumes.Add(*It);
		if (Volumes.Num() != 1)
		{
			OutError = FString::Printf(TEXT("Expected one NavMeshBoundsVolume, found %d."), Volumes.Num());
			return false;
		}
		ANavMeshBoundsVolume* Volume = Volumes[0];
		const FBox CurrentBounds = Volume->GetComponentsBoundingBox(true);
		if (!CurrentBounds.IsValid || CurrentBounds.GetExtent().X <= UE_SMALL_NUMBER
			|| CurrentBounds.GetExtent().Y <= UE_SMALL_NUMBER)
		{
			OutError = TEXT("NavMeshBoundsVolume has invalid bounds.");
			return false;
		}
		const bool bNeedsResize = !FVector2D(CurrentBounds.GetCenter()).IsNearlyZero(1.0)
			|| !FMath::IsNearlyEqual(
				CurrentBounds.GetExtent().X, GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM, 1.0f)
			|| !FMath::IsNearlyEqual(
				CurrentBounds.GetExtent().Y, GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM, 1.0f);
		if (bNeedsResize)
		{
			Volume->Modify();
			FVector Location = Volume->GetActorLocation();
			Location.X -= CurrentBounds.GetCenter().X;
			Location.Y -= CurrentBounds.GetCenter().Y;
			Volume->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			FVector Scale = Volume->GetActorScale3D();
			Scale.X *= GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM / CurrentBounds.GetExtent().X;
			Scale.Y *= GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM / CurrentBounds.GetExtent().Y;
			Volume->SetActorScale3D(Scale);
			Volume->MarkPackageDirty();
		}
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!Navigation)
		{
			OutError = TEXT("NavigationSystemV1 is unavailable in the editor world.");
			return false;
		}
		if (bNeedsResize)
		{
			Navigation->OnNavigationBoundsUpdated(Volume);
			Navigation->Build();
		}
		if (bNeedsResize || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(&World))
		{
			for (TActorIterator<ANavigationData> It(&World); It; ++It) It->EnsureBuildCompletion();
		}
		return true;
	}

	UGuLiMapTypeDefinition* FindOutpostType(UGuLiMapAuthoringSubsystem& Service)
	{
		for (UGuLiMapTypeDefinition* Type : Service.ListTypes())
		{
			if (Type && Type->TypeId == TEXT("Outpost")) return Type;
		}
		return nullptr;
	}

	bool PrepareMarkers(
		UWorld& World,
		UGuLiMapAuthoringSubsystem& Service,
		UGuLiMapTypeDefinition& OutpostType,
		FString& OutError)
	{
		TArray<AGuLiMapMarker*> ExistingOutposts;
		for (AGuLiMapMarker* Marker : Service.LoadedMarkers())
		{
			const UGuLiMapTypeDefinition* Type = Marker ? Marker->Record.Type.LoadSynchronous() : nullptr;
			if (Type && Type->TypeId == TEXT("Outpost")) ExistingOutposts.Add(Marker);
		}
		if (ExistingOutposts.Num() > GULI_RESOURCE_TERRITORY_COUNT)
		{
			OutError = FString::Printf(TEXT("Found %d Outpost markers; expected no more than 25."),
				ExistingOutposts.Num());
			return false;
		}

		TMap<int32, AGuLiMapMarker*> MarkerByIndex;
		TArray<AGuLiMapMarker*> LegacyMarkers;
		for (AGuLiMapMarker* Marker : ExistingOutposts)
		{
			int32 Row = 0;
			int32 Column = 0;
			if (ParseCanonicalKey(Marker->Record.MarkerKey, Row, Column))
			{
				const int32 Index = GuLiResources::ToTerritoryIndex(Row, Column);
				if (MarkerByIndex.Contains(Index))
				{
					OutError = TEXT("Duplicate canonical Outpost MarkerKey.");
					return false;
				}
				MarkerByIndex.Add(Index, Marker);
			}
			else LegacyMarkers.Add(Marker);
		}
		LegacyMarkers.Sort([](const AGuLiMapMarker& A, const AGuLiMapMarker& B)
		{
			const FVector LA = A.GetActorLocation();
			const FVector LB = B.GetActorLocation();
			if (!FMath::IsNearlyEqual(LA.Y, LB.Y)) return LA.Y > LB.Y;
			if (!FMath::IsNearlyEqual(LA.X, LB.X)) return LA.X < LB.X;
			return A.Record.MarkerId < B.Record.MarkerId;
		});
		TArray<int32> OddIntersections;
		for (const int32 Row : { 1, 3, 5 })
			for (const int32 Column : { 1, 3, 5 })
				if (!MarkerByIndex.Contains(GuLiResources::ToTerritoryIndex(Row, Column)))
					OddIntersections.Add(GuLiResources::ToTerritoryIndex(Row, Column));
		if (LegacyMarkers.Num() > OddIntersections.Num())
		{
			OutError = TEXT("Legacy Outpost markers exceed the available odd row/column migration slots.");
			return false;
		}
		for (int32 Index = 0; Index < LegacyMarkers.Num(); ++Index)
			MarkerByIndex.Add(OddIntersections[Index], LegacyMarkers[Index]);

		for (int32 Index = 0; Index < GULI_RESOURCE_TERRITORY_COUNT; ++Index)
		{
			const int32 Row = Index / GULI_RESOURCE_BOARD_DIMENSION + 1;
			const int32 Column = Index % GULI_RESOURCE_BOARD_DIMENSION + 1;
			AGuLiMapMarker* Marker = MarkerByIndex.FindRef(Index);
			if (!Marker)
			{
				Marker = Service.CreateMarker(&OutpostType, GuLiResources::GetTerritoryCenter(Row, Column));
				if (!Marker)
				{
					OutError = FString::Printf(TEXT("Could not create Outpost_R%dC%d."), Row, Column);
					return false;
				}
				MarkerByIndex.Add(Index, Marker);
			}
			Marker->Modify();
			FInstancedPropertyBag Migrated = Marker->Record.Parameters;
			if (!GuLiMap::MigrateFields(Migrated, OutpostType.DefaultParameters, true, OutError))
				return false;
			Marker->Record.Type = &OutpostType;
			Marker->Record.MarkerKey = GuLiResources::MakeTerritoryId(Row, Column);
			Marker->Record.DisplayName = FString::Printf(TEXT("R%dC%d 据点"), Row, Column);
			Marker->Record.bEnabled = true;
			Marker->Record.Parameters = MoveTemp(Migrated);
			Marker->Record.Parameters.SetValueInt32(TEXT("BoardRow"), Row);
			Marker->Record.Parameters.SetValueInt32(TEXT("BoardColumn"), Column);
			Marker->Record.Parameters.SetValueName(TEXT("InitialOwner"),
				Row == 1 && Column == 3 ? TEXT("Red")
				: (Row == 5 && Column == 3 ? TEXT("Blue") : TEXT("Neutral")));
			Marker->Record.Parameters.SetValueInt32(
				TEXT("BlueClusterBudget"), GuLiResources::GetBlueClusterBudget(Row, Column));
			Marker->Record.Parameters.SetValueInt32(
				TEXT("RedClusterBudget"), GuLiResources::GetRedClusterBudget(Row, Column));
			FGuid CaptureId;
			FGuid TerritoryId;
			if (const FGuLiMapRegionRecord* Existing = Marker->Record.Regions.FindByPredicate(
				[](const FGuLiMapRegionRecord& Region) { return Region.RegionKey == TEXT("Capture"); }))
				CaptureId = Existing->RegionId;
			if (const FGuLiMapRegionRecord* Existing = Marker->Record.Regions.FindByPredicate(
				[](const FGuLiMapRegionRecord& Region) { return Region.RegionKey == TEXT("Territory"); }))
				TerritoryId = Existing->RegionId;
			Marker->Record.Regions = { MakeCaptureRegion(CaptureId), MakeTerritoryRegion(TerritoryId) };
			const FVector Ground = ProjectGroundOrXY(World,
				FVector2D(GuLiResources::GetTerritoryCenter(Row, Column)));
			Marker->SetActorTransform(FTransform(FRotator::ZeroRotator, Ground, FVector::OneVector));
			Marker->SetActorLabel(Marker->Record.MarkerKey.ToString());
			Marker->SetFolderPath(TEXT("GuLi/MapAuthoring/Outposts"));
			Marker->EnsureRegionIdentities();
			Marker->RefreshVisuals();
			Marker->MarkPackageDirty();
		}
		return true;
	}

	bool IsInsideDensityExclusion(const FVector2D XY)
	{
		return IsExplicitlyExcluded(XY);
	}

	bool InitializeDensityIfEmpty(AGuLiMapDensityMap& DensityActor, FString& OutError)
	{
		FGuLiMapDensityMapRecord& Density = DensityActor.Record;
		if (!GuLiMap::IsDensityMapEmpty(Density)) return true;
		if (!FMath::IsNearlyEqual(Density.CellSizeCm, 2500.0))
		{
			OutError = TEXT("Empty density map must use a 2500 cm cell size.");
			return false;
		}
		FGuLiMapDensityLayer* Blue = Density.Layers.FindByPredicate(
			[](const FGuLiMapDensityLayer& Layer) { return Layer.LayerKey == TEXT("BlueOre"); });
		FGuLiMapDensityLayer* Red = Density.Layers.FindByPredicate(
			[](const FGuLiMapDensityLayer& Layer) { return Layer.LayerKey == TEXT("RedOre"); });
		if (!Blue || !Red)
		{
			OutError = TEXT("Density presets do not contain BlueOre and RedOre.");
			return false;
		}
		DensityActor.Modify();
		const int32 MinimumCell = GuLiMap::WorldToDensityCell(
			-GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM, Density.CellSizeCm);
		const int32 MaximumCell = GuLiMap::WorldToDensityCell(
			GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM - UE_DOUBLE_SMALL_NUMBER, Density.CellSizeCm);
		for (int32 CellY = MinimumCell; CellY <= MaximumCell; ++CellY)
		{
			for (int32 CellX = MinimumCell; CellX <= MaximumCell; ++CellX)
			{
				const FVector2D XY(
					(static_cast<double>(CellX) + 0.5) * Density.CellSizeCm,
					(static_cast<double>(CellY) + 0.5) * Density.CellSizeCm);
				const int32 Column = FMath::Clamp(FMath::FloorToInt(
					(XY.X + GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM) / GULI_RESOURCE_TERRITORY_SIZE_CM) + 1, 1, 5);
				const int32 Row = FMath::Clamp(FMath::FloorToInt(
					(GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM - XY.Y) / GULI_RESOURCE_TERRITORY_SIZE_CM) + 1, 1, 5);
				const FVector2D TerritoryCenter(GuLiResources::GetTerritoryCenter(Row, Column));
				const FVector2D Local = XY - TerritoryCenter;
				if (FMath::Abs(Local.X) > GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - TerritoryBoundaryMargin
					|| FMath::Abs(Local.Y) > GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - TerritoryBoundaryMargin
					|| IsInsideDensityExclusion(XY))
				{
					continue;
				}
				const double HorizontalCenter = 1.0 - FMath::Abs(XY.X) / GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM;
				const double MapCenter = 1.0 - FMath::Max(FMath::Abs(XY.X), FMath::Abs(XY.Y))
					/ GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM;
				const double LocalShape = 1.0 - 0.35 * FMath::Max(FMath::Abs(Local.X), FMath::Abs(Local.Y))
					/ (GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM - TerritoryBoundaryMargin);
				const uint8 BlueWeight = static_cast<uint8>(FMath::Clamp(
					FMath::RoundToInt(255.0 * (0.30 + 0.70 * HorizontalCenter) * LocalShape), 1, 255));
				const uint8 RedWeight = GuLiResources::GetRedClusterBudget(Row, Column) > 0
					? static_cast<uint8>(FMath::Clamp(
						FMath::RoundToInt(255.0 * (0.25 + 0.75 * MapCenter) * LocalShape), 1, 255))
					: 0u;
				GuLiMap::SetDensityCell(*Blue, FIntPoint(CellX, CellY), BlueWeight);
				if (RedWeight > 0u) GuLiMap::SetDensityCell(*Red, FIntPoint(CellX, CellY), RedWeight);
			}
		}
		GuLiMap::NormalizeDensityMap(Density);
		DensityActor.RefreshVisuals(true);
		DensityActor.MarkPackageDirty();
		return true;
	}
}

bool UGuLiResourceAuthoringLibrary::IsCanonicalResourceMap(const UWorld* World)
{
	return World && GuLiResourceAuthoring::NormalizeMapPackage(*World)
		== GuLiResourceAuthoring::CanonicalMapPackage;
}

bool UGuLiResourceAuthoringLibrary::CalculateCurrentSourceHash(
	FString& OutHash,
	TArray<FString>& OutIssues)
{
	OutHash.Reset();
	OutIssues.Reset();
	FGuLiResourceBakeResult Result;
	FGuLiMapSnapshot Snapshot;
	TMap<FString, FString> Files;
	if (!GuLiResourceAuthoring::BuildAuthoringFiles(Snapshot, Files, Result))
	{
		OutIssues = MoveTemp(Result.Issues);
		return false;
	}
	if (!GuLiResourceAuthoring::HashAuthoringFiles(Files, OutHash))
	{
		OutIssues.Add(TEXT("Could not hash GuLiMapAuthoring output."));
		return false;
	}
	return true;
}

FGuLiResourceBakeResult UGuLiResourceAuthoringLibrary::PrepareCanonicalAuthoringAndBake(
	const bool bInitializeDensityWhenEmpty,
	const bool bSavePackages)
{
	using namespace GuLiResourceAuthoring;
	UWorld* World = GetEditorWorld();
	if (!World || !IsCanonicalResourceMap(World))
		return Fail(TEXT("Load /Game/Maps/LVL_CommanderMassPrototype in a stopped editor before authoring resources."));
	UGuLiMapAuthoringSubsystem* Service = GetAuthoringSubsystem();
	if (!Service) return Fail(TEXT("GuLiMapAuthoring editor subsystem is unavailable."));
	const FGuLiMapResult Presets = Service->EnsurePresets();
	if (!Presets.bSuccess) return Fail(TEXT("GuLiMapAuthoring presets could not be created."));
	UGuLiMapTypeDefinition* OutpostType = FindOutpostType(*Service);
	if (!OutpostType) return Fail(TEXT("The Outpost type asset is unavailable after EnsurePresets."));
	FString Error;
	if (!ConfigureOutpostType(*OutpostType, Error)) return Fail(Error);
	if (!PrepareMarkers(*World, *Service, *OutpostType, Error)) return Fail(Error);
	const FGuLiMapResult DensityResult = Service->EnsureDensityMap(2500.0);
	if (!DensityResult.bSuccess) return Fail(TEXT("Could not ensure the canonical 2500 cm density map."));
	TArray<AGuLiMapDensityMap*> DensityMaps = Service->LoadedDensityMaps();
	if (DensityMaps.Num() != 1) return Fail(TEXT("Exactly one GuLiMap density actor is required."));
	if (bInitializeDensityWhenEmpty && !InitializeDensityIfEmpty(*DensityMaps[0], Error)) return Fail(Error);
	if (!ConfigureNavigationBounds(*World, Error)) return Fail(Error);
	if (bSavePackages && !Service->SaveAuthoringPackages())
		return Fail(TEXT("Could not save GuLiMapAuthoring packages or the current level."));
	return BakeCurrentMap(bSavePackages);
}

FGuLiResourceBakeResult UGuLiResourceAuthoringLibrary::BakeCurrentMap(const bool bSavePackages)
{
	using namespace GuLiResourceAuthoring;
	UWorld* World = GetEditorWorld();
	if (!World || !IsCanonicalResourceMap(World))
		return Fail(TEXT("The resource bake only supports /Game/Maps/LVL_CommanderMassPrototype."));
	FGuLiResourceBakeResult Result;
	FGuLiMapSnapshot Snapshot;
	TMap<FString, FString> Files;
	if (!BuildAuthoringFiles(Snapshot, Files, Result)) return Result;
	if (!HashAuthoringFiles(Files, Result.SourceHash))
	{
		AddIssue(Result, TEXT("Could not calculate the authoring source hash."));
		return Result;
	}
	bool bDefinitionCreated = false;
	UGuLiResourceMapDefinition* Definition = FindOrCreateAsset<UGuLiResourceMapDefinition>(
		MapDefinitionPath, MapDefinitionPackage, bDefinitionCreated);
	bool bEconomyCreated = false;
	UGuLiResourceEconomyConfig* Economy = FindOrCreateAsset<UGuLiResourceEconomyConfig>(
		EconomyPath, EconomyPackage, bEconomyCreated);
	if (!Definition || !Economy)
	{
		AddIssue(Result, TEXT("Could not create/load the managed resource DataAssets."));
		return Result;
	}
	Definition->Modify();
	Economy->Modify();
	FString Error;
	if (!BuildRuntimeDefinition(*World, Snapshot, Result.SourceHash, *Definition, Error))
	{
		AddIssue(Result, Error);
		return Result;
	}
	if (!Economy->ValidateConfig(Error))
	{
		AddIssue(Result, FString::Printf(TEXT("Economy config validation failed: %s"), *Error));
		return Result;
	}
	Definition->MarkPackageDirty();
	Economy->MarkPackageDirty();
	if (bSavePackages && !SaveManagedAssets(*Definition, *Economy, Error))
	{
		AddIssue(Result, Error);
		return Result;
	}
	Result.LayoutHash = Definition->LayoutHash;
	Result.TerritoryCount = Definition->Territories.Num();
	Result.ClusterCount = Definition->Clusters.Num();
	Result.NodeCount = Definition->Nodes.Num();
	Result.bSuccess = true;
	Result.Message = FString::Printf(
		TEXT("Baked %d territories, %d clusters and %d nodes. Source=%s Layout=%s"),
		Result.TerritoryCount, Result.ClusterCount, Result.NodeCount,
		*Result.SourceHash, *Result.LayoutHash);
	UE_LOG(LogGuLiResourceBake, Display, TEXT("%s"), *Result.Message);
	return Result;
}

FGuLiResourceBakeResult UGuLiResourceAuthoringLibrary::ValidateCurrentBake()
{
	using namespace GuLiResourceAuthoring;
	UWorld* World = GetEditorWorld();
	if (!World || !IsCanonicalResourceMap(World))
	{
		FGuLiResourceBakeResult NotApplicable;
		NotApplicable.bSuccess = true;
		return NotApplicable;
	}
	FGuLiResourceBakeResult Result;
	UGuLiResourceMapDefinition* Definition = LoadObject<UGuLiResourceMapDefinition>(nullptr, MapDefinitionPath);
	UGuLiResourceEconomyConfig* Economy = LoadObject<UGuLiResourceEconomyConfig>(nullptr, EconomyPath);
	if (!Definition || !Economy)
		return Fail(TEXT("Resource DataAssets are missing; run PrepareCanonicalAuthoringAndBake before PIE."));
	FString Error;
	if (!Definition->ValidateDefinition(Error))
		return Fail(FString::Printf(TEXT("Baked resource definition is invalid: %s"), *Error));
	if (!Economy->ValidateConfig(Error))
		return Fail(FString::Printf(TEXT("Resource economy config is invalid: %s"), *Error));
	TArray<FString> SourceIssues;
	if (!CalculateCurrentSourceHash(Result.SourceHash, SourceIssues))
	{
		Result.Issues = MoveTemp(SourceIssues);
		Result.Message = Result.Issues.IsEmpty()
			? TEXT("Could not validate current map-authoring source.") : Result.Issues[0];
		return Result;
	}
	if (!Result.SourceHash.Equals(Definition->SourceHash, ESearchCase::CaseSensitive))
	{
		return Fail(TEXT("GuLiMapAuthoring source changed after the last resource bake; rebake before PIE."));
	}
	Result.bSuccess = true;
	Result.Message = TEXT("Resource authoring source and baked runtime definition are current.");
	Result.LayoutHash = Definition->LayoutHash;
	Result.TerritoryCount = Definition->Territories.Num();
	Result.ClusterCount = Definition->Clusters.Num();
	Result.NodeCount = Definition->Nodes.Num();
	return Result;
}

bool UGuLiResourceAuthoringLibrary::ConfigureDedicatedServerPIE(const int32 ClientCount)
{
	if (ClientCount < 1 || ClientCount > 64)
	{
		UE_LOG(LogGuLiResourceBake, Error, TEXT("PIE client count must be between 1 and 64."));
		return false;
	}
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	if (!Settings)
	{
		UE_LOG(LogGuLiResourceBake, Error, TEXT("LevelEditorPlaySettings are unavailable."));
		return false;
	}
	Settings->bLaunchSeparateServer = false;
	Settings->SetRunUnderOneProcess(true);
	Settings->SetPlayNumberOfClients(ClientCount);
	Settings->SetPlayNetMode(PIE_Client);
	return true;
}

bool UGuLiResourceAuthoringLibrary::ConfigureListenServerPIE(const int32 ClientCount)
{
	if (ClientCount < 1 || ClientCount > 64)
	{
		UE_LOG(LogGuLiResourceBake, Error, TEXT("PIE client count must be between 1 and 64."));
		return false;
	}
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	if (!Settings)
	{
		UE_LOG(LogGuLiResourceBake, Error, TEXT("LevelEditorPlaySettings are unavailable."));
		return false;
	}
	Settings->bLaunchSeparateServer = false;
	Settings->SetRunUnderOneProcess(true);
	Settings->SetPlayNumberOfClients(ClientCount);
	Settings->SetPlayNetMode(PIE_ListenServer);
	return true;
}

bool UGuLiResourceAuthoringLibrary::NormalizeBlueprintLocalComponentHierarchy(
	const FString& BlueprintPath)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		UE_LOG(LogGuLiResourceBake, Error,
			TEXT("Could not load Blueprint SCS for hierarchy normalization: %s"), *BlueprintPath);
		return false;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	const TArray<USCS_Node*>& Nodes = SCS->GetAllNodes();
	TSet<USCS_Node*> LocallyNestedNodes;
	TSet<FName> LocalNodeNames;
	for (USCS_Node* Node : Nodes)
	{
		if (!Node) continue;
		LocalNodeNames.Add(Node->GetVariableName());
		for (USCS_Node* Child : Node->GetChildNodes())
		{
			if (Child) LocallyNestedNodes.Add(Child);
		}
	}

	const FName LocalOwnerClassName = Blueprint->GeneratedClass
		? Blueprint->GeneratedClass->GetFName() : NAME_None;
	int32 RepairedCount = 0;
	for (USCS_Node* Node : Nodes)
	{
		if (!Node) continue;
		const bool bReferencesLocalNode =
			Node->ParentComponentOrVariableName != NAME_None
			&& LocalNodeNames.Contains(Node->ParentComponentOrVariableName)
			&& (Node->ParentComponentOwnerClassName == NAME_None
				|| Node->ParentComponentOwnerClassName == LocalOwnerClassName);
		if (!LocallyNestedNodes.Contains(Node) && !bReferencesLocalNode) continue;
		if (Node->ParentComponentOrVariableName == NAME_None
			&& Node->ParentComponentOwnerClassName == NAME_None
			&& !Node->bIsParentComponentNative)
		{
			continue;
		}

		Node->Modify();
		Node->ParentComponentOrVariableName = NAME_None;
		Node->ParentComponentOwnerClassName = NAME_None;
		Node->bIsParentComponentNative = false;
		++RepairedCount;
	}

	if (RepairedCount > 0)
	{
		Blueprint->Modify();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
	}
	UE_LOG(LogGuLiResourceBake, Display,
		TEXT("Normalized %d duplicate local SCS parent reference(s) in %s."),
		RepairedCount, *BlueprintPath);
	return true;
}
