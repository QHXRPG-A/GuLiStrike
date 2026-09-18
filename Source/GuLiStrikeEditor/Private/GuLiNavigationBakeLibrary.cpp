#include "GuLiNavigationBakeLibrary.h"

#include "AI/Navigation/NavRelevantInterface.h"
#include "AI/NavDataGenerator.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "Detour/DetourNavMesh.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GuLiFlightNavigationCookGate.h"
#include "GuLiFlightNavigationCookSettings.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationEditorLibrary.h"
#include "GuLiFlightNavigationVolume.h"
#include "GuLiNavigationSourceHash.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "NavAreas/NavArea.h"
#include "Navigation/NavLinkProxy.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavModifierComponent.h"
#include "NavModifierVolume.h"
#include "NavRelevantComponent.h"
#include "NavigationSystem.h"
#include "Settings/ProjectPackagingSettings.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiNavigationBake, Log, All);

namespace GuLiNavigationBake
{
	constexpr const TCHAR* SourceKey = TEXT("GuLi.GroundNavigation.Source.v1");
	constexpr const TCHAR* PayloadKey = TEXT("GuLi.GroundNavigation.Payload.v1");
	bool bPreparing = false;
	TSet<TWeakObjectPtr<UWorld>> PendingSaveWorlds;
	TSet<TWeakObjectPtr<UWorld>> ScaleMigrationWorlds;

	FString Hex(uint64 Value) { return FString::Printf(TEXT("%016llx"), Value); }

	bool CompleteGround(ARecastNavMesh* Navigation, FString& Error)
	{
		FNavDataGenerator* Generator = Navigation->GetGenerator();
		if (!Generator) { Error = TEXT("Ground navigation has no generator."); return false; }
		const double Start = FPlatformTime::Seconds();
		double LastLog = Start, LastProgress = Start;
		int32 PreviousRemaining = Generator->GetNumRemaningBuildTasks();
		while (Generator->GetNumRemaningBuildTasks() > 0)
		{
			Generator->TickAsyncBuild(.01f);
			const double Now = FPlatformTime::Seconds();
			const int32 Remaining = Generator->GetNumRemaningBuildTasks();
			const int32 Running = Generator->GetNumRunningBuildTasks();
			if (Remaining != PreviousRemaining) { PreviousRemaining = Remaining; LastProgress = Now; }
			if (Now - LastLog > 10)
			{
				UE_LOG(LogGuLiNavigationBake, Display, TEXT("[GULI_NAV_PREPARE] stage=GroundProgress object=%s remaining=%d running=%d elapsed=%.1f"),
					*Navigation->GetPathName(), Remaining, Running, Now - Start);
				LastLog = Now;
			}
			if ((Running == 0 && Remaining > 0 && Now - LastProgress > 30) || Now - Start > 1200)
			{
				Generator->CancelBuild();
				Error = FString::Printf(TEXT("Ground bake did not complete: %s remaining=%d running=%d elapsed=%.1fs. Nothing certified or saved."),
					*Navigation->GetPathName(), Remaining, Running, Now - Start);
				return false;
			}
			FPlatformProcess::Sleep(.002f);
		}
		return true;
	}

	bool RebuildGround(ARecastNavMesh* Navigation, FString& Error)
	{
		// Fully asynchronous gathering is intentionally limited to one tile job in UE 5.7.
		// Editor preparation already blocks Play: gather on the game thread and use UE's
		// configured worker count for rasterization. Restore the runtime setting before hashing/saving.
		const bool bAsyncGathering = Navigation->bDoFullyAsyncNavDataGathering;
		Navigation->bDoFullyAsyncNavDataGathering = false;
		UE_LOG(LogGuLiNavigationBake, Display, TEXT("[GULI_NAV_PREPARE] stage=GroundBuild object=%s"), *Navigation->GetPathName());
		Navigation->RebuildAll();
		const bool bComplete = CompleteGround(Navigation, Error);
		Navigation->bDoFullyAsyncNavDataGathering = bAsyncGathering;
		return bComplete;
	}

	UWorld* ResolveWorld(UObject* Context)
	{
		return GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	}

	TArray<ARecastNavMesh*> GroundData(UWorld* World)
	{
		TArray<ARecastNavMesh*> Result;
		for (TActorIterator<ARecastNavMesh> It(World); It; ++It) Result.Add(*It);
		Result.Sort([](const ARecastNavMesh& A, const ARecastNavMesh& B) { return A.GetPathName() < B.GetPathName(); });
		return Result;
	}

	bool HasMissingGroundAgent(UWorld* World, const UNavigationSystemV1* System)
	{
		// Cook creates a physics scene without a navigation system. Still require all
		// configured agents; one surviving NavData must not conceal a missing second agent.
		if (!System) System = GetDefault<UNavigationSystemV1>();
		const auto Ground = GroundData(World);
		const auto& Agents = System->GetSupportedAgents();
		for (int32 Index = 0; Index < Agents.Num(); ++Index)
		{
			if (!System->GetSupportedAgentsMask().Contains(Index)) continue;
			const FNavDataConfig& Agent = Agents[Index];
			if (!Agent.IsValid()) continue;
			const UClass* AgentClass = Agent.GetNavDataClass<ANavigationData>();
			if (!AgentClass) return true;
			const bool bFound = Ground.ContainsByPredicate([&](const ARecastNavMesh* Data)
			{
				return FMath::IsNearlyEqual(Data->GetConfig().AgentRadius, Agent.AgentRadius)
					&& FMath::IsNearlyEqual(Data->GetConfig().AgentHeight, Agent.AgentHeight)
					&& Data->IsA(AgentClass);
			});
			if (!bFound) return true;
		}
		return false;
	}

	TArray<AGuLiFlightNavigationVolume*> FlightVolumes(UWorld* World)
	{
		TArray<AGuLiFlightNavigationVolume*> Result;
		for (TActorIterator<AGuLiFlightNavigationVolume> It(World); It; ++It)
			if (It->bNavigationEnabled) Result.Add(*It);
		Result.Sort([](const AGuLiFlightNavigationVolume& A, const AGuLiFlightNavigationVolume& B)
		{ return A.GetPathName() < B.GetPathName(); });
		return Result;
	}

	/** Hash persistent Detour geometry, excluding runtime links, salts and allocator layout. */
	bool GroundPayload(const ARecastNavMesh* Navigation, FString& OutHash, FString& OutError)
	{
		const dtNavMesh* Mesh = Navigation->GetRecastMesh();
		if (!Navigation->HasValidNavmesh() || !Mesh)
		{
			OutError = TEXT("Missing or incompatible Recast tile data.");
			return false;
		}
		TArray<const dtMeshTile*> Tiles;
		for (int32 Index = 0; Index < Mesh->getMaxTiles(); ++Index)
		{
			const dtMeshTile* Tile = Mesh->getTile(Index);
			if (Tile && Tile->header && Tile->header->polyCount > 0) Tiles.Add(Tile);
		}
		if (Tiles.IsEmpty())
		{
			OutError = TEXT("The ground navigation contains no walkable tiles.");
			return false;
		}
		Tiles.Sort([](const dtMeshTile& A, const dtMeshTile& B)
		{
			if (A.header->x != B.header->x) return A.header->x < B.header->x;
			if (A.header->y != B.header->y) return A.header->y < B.header->y;
			return A.header->layer < B.header->layer;
		});
		FGuLiNavigationSourceHash Hash;
		Hash.AddString(TEXT("GuLiGroundPayload-v1"));
		Hash.AddUInt64(Tiles.Num());
		for (const dtMeshTile* Tile : Tiles)
		{
			const dtMeshHeader& Header = *Tile->header;
			if (Header.version != DT_NAVMESH_VERSION
				|| Header.vertCount <= 0 || !Tile->verts || !Tile->polys)
			{
				OutError = TEXT("Invalid Recast tile header or missing polygon geometry.");
				return false;
			}
			Hash.AddUInt64(Header.x); Hash.AddUInt64(Header.y); Hash.AddUInt64(Header.layer);
			Hash.AddUInt64(Header.resolution);
			Hash.AddBytes(Header.bmin, sizeof(Header.bmin)); Hash.AddBytes(Header.bmax, sizeof(Header.bmax));
			Hash.AddUInt64(Header.vertCount); Hash.AddUInt64(Header.polyCount);
			for (int32 Vertex = 0; Vertex < Header.vertCount; ++Vertex)
			{
				const FVector Position(Tile->verts[Vertex * 3], Tile->verts[Vertex * 3 + 1], Tile->verts[Vertex * 3 + 2]);
				if (Position.ContainsNaN()) { OutError = TEXT("Non-finite Recast vertex."); return false; }
				Hash.AddVector(Position);
			}
			for (int32 Index = 0; Index < Header.polyCount; ++Index)
			{
				const dtPoly& Poly = Tile->polys[Index];
				if (Poly.vertCount > DT_VERTS_PER_POLYGON) { OutError = TEXT("Invalid Recast polygon."); return false; }
				Hash.AddUInt64(Poly.vertCount); Hash.AddUInt64(Poly.flags);
				Hash.AddUInt64(Poly.getArea()); Hash.AddUInt64(Poly.getType());
				for (int32 Vertex = 0; Vertex < Poly.vertCount; ++Vertex)
				{
					if (Poly.verts[Vertex] >= Header.vertCount) { OutError = TEXT("Invalid Recast vertex index."); return false; }
					Hash.AddUInt64(Poly.verts[Vertex]); Hash.AddUInt64(Poly.neis[Vertex]);
				}
			}
			Hash.AddUInt64(Header.detailVertCount); Hash.AddUInt64(Header.detailTriCount);
			if ((Header.detailVertCount && !Tile->detailVerts) || (Header.detailTriCount && !Tile->detailTris)
				|| (Header.detailMeshCount && !Tile->detailMeshes) || (Header.bvNodeCount && !Tile->bvTree)
				|| (Header.offMeshConCount && !Tile->offMeshCons))
			{ OutError = TEXT("Missing Recast detail, acceleration or link data."); return false; }
			if (Header.detailVertCount > 0) Hash.AddBytes(Tile->detailVerts, Header.detailVertCount * 3ll * sizeof(*Tile->detailVerts));
			if (Header.detailTriCount > 0) Hash.AddBytes(Tile->detailTris, Header.detailTriCount * 4ll);
			Hash.AddUInt64(Header.detailMeshCount);
			for (int32 Index = 0; Index < Header.detailMeshCount; ++Index)
			{
				const dtPolyDetail& Detail = Tile->detailMeshes[Index];
				if (Detail.vertBase + Detail.vertCount > Header.detailVertCount || Detail.triBase + Detail.triCount > Header.detailTriCount)
				{ OutError = TEXT("Invalid Recast detail mesh range."); return false; }
				Hash.AddUInt64(Detail.vertBase); Hash.AddUInt64(Detail.vertCount);
				Hash.AddUInt64(Detail.triBase); Hash.AddUInt64(Detail.triCount);
			}
			Hash.AddUInt64(Header.bvNodeCount);
			for (int32 Index = 0; Index < Header.bvNodeCount; ++Index)
			{
				const dtBVNode& Node = Tile->bvTree[Index];
				Hash.AddBytes(Node.bmin, sizeof(Node.bmin)); Hash.AddBytes(Node.bmax, sizeof(Node.bmax)); Hash.AddUInt64(Node.i);
			}
			Hash.AddUInt64(Header.offMeshBase); Hash.AddUInt64(Header.offMeshConCount);
			for (int32 Index = 0; Index < Header.offMeshConCount; ++Index)
			{
				const dtOffMeshConnection& Link = Tile->offMeshCons[Index];
				Hash.AddBytes(Link.pos, sizeof(Link.pos)); Hash.AddBytes(&Link.rad, sizeof(Link.rad)); Hash.AddBytes(&Link.height, sizeof(Link.height));
				Hash.AddUInt64(Link.userId); Hash.AddUInt64(Link.poly); Hash.AddUInt64(Link.side); Hash.AddUInt64(Link.flags);
			}
		}
		OutHash = Hex(Hash.Get());
		return true;
	}

	bool CheckGround(UWorld* World, ARecastNavMesh* Navigation, FGuLiNavigationBakeEntry& Entry)
	{
		const double Start = FPlatformTime::Seconds();
		Entry.Kind = TEXT("Ground");
		Entry.ObjectPath = Navigation->GetPathName();
		Entry.Status = TEXT("Stale");
		Entry.SourceHash = Hex(UGuLiNavigationBakeLibrary::ComputeGroundSourceHash(World, Navigation));
		FMetaData& Metadata = Navigation->GetPackage()->GetMetaData();
		bool bValid = Entry.SourceHash == Metadata.GetValue(Navigation, SourceKey);
		if (!bValid) Entry.Message = TEXT("Source geometry/settings changed, or no versioned bake record exists.");
		if (bValid)
		{
			FString Payload;
			bValid = GroundPayload(Navigation, Payload, Entry.Message);
			if (bValid && Payload != Metadata.GetValue(Navigation, PayloadKey))
			{
				bValid = false;
				Entry.Message = TEXT("Ground tile checksum differs from the saved bake record.");
			}
		}
		Entry.CheckSeconds = FPlatformTime::Seconds() - Start;
		if (bValid) { Entry.Status = TEXT("Hit"); Entry.Message = TEXT("Validated saved ground tiles."); }
		return bValid;
	}

	bool CheckFlight(AGuLiFlightNavigationVolume* Volume, FGuLiNavigationBakeEntry& Entry)
	{
		const double Start = FPlatformTime::Seconds();
		Entry.Kind = TEXT("Flight");
		Entry.ObjectPath = Volume->GetPathName();
		FText Error;
		bool bValid = UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, Volume->AuthoringBakeSettings, Error);
		if (bValid)
		{
			FString PayloadError;
			bValid = Volume->NavigationData->ValidateSerializedPayload(PayloadError);
			if (!bValid) Error = FText::FromString(PayloadError);
		}
		Entry.SourceHash = Hex(UGuLiFlightNavigationEditorLibrary::ComputeSourceGeometrySignature(
			Volume->GetWorld(), Volume, Volume->AuthoringBakeSettings));
		Entry.Status = bValid ? TEXT("Hit") : TEXT("Stale");
		Entry.Message = bValid ? TEXT("Validated saved flight graph.") : Error.ToString();
		Entry.CheckSeconds = FPlatformTime::Seconds() - Start;
		return bValid;
	}

	bool CheckWorld(UWorld* World, FString& Error)
	{
		if (!World || World->IsGameWorld())
		{ Error = TEXT("Navigation preparation requires an editor source world, before PIE/cook."); return false; }
		if (World->IsPartitionedWorld())
		{ Error = TEXT("Partitioned navigation requires its complete World Partition builder; partial-world baking is refused."); return false; }
		for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming && !Streaming->GetLoadedLevel())
			{ Error = TEXT("Load all source sublevels before preparing navigation."); return false; }
		}
		const auto* Settings = GetDefault<UGuLiFlightNavigationCookSettings>();
		if (Settings->RequiredWorldPackages.Contains(World->GetOutermost()->GetFName()) && FlightVolumes(World).IsEmpty())
		{ Error = TEXT("This required flight map has no enabled navigation volume."); return false; }
		return true;
	}

	void LogResult(const FGuLiNavigationBakeResult& Result, UWorld* World)
	{
		for (const FGuLiNavigationBakeEntry& Entry : Result.Entries)
			UE_LOG(LogGuLiNavigationBake, Display, TEXT("[GULI_NAV_PREPARE] kind=%s status=%s object=%s hash=%s check_s=%.6f build_s=%.6f reason=%s"),
				*Entry.Kind, *Entry.Status, *Entry.ObjectPath, *Entry.SourceHash, Entry.CheckSeconds, Entry.BuildSeconds, *Entry.Message);
		UE_LOG(LogGuLiNavigationBake, Display,
			TEXT("[GULI_NAV_PREPARE] result=%s world=%s ground_rebuilds=%d flight_rebuilds=%d save_s=%.6f total_s=%.6f reason=%s"),
			Result.bSuccess ? TEXT("Ready") : TEXT("Failed"), *GetPathNameSafe(World),
			Result.GroundRebuilds, Result.FlightRebuilds, Result.SaveSeconds, Result.TotalSeconds, *Result.Message);
	}
}

uint64 UGuLiNavigationBakeLibrary::ComputeGroundSourceHash(UWorld* World, const ARecastNavMesh* Navigation)
{
	if (!World || !Navigation) return 0;
	FGuLiNavigationSourceHash Hash;
	Hash.AddString(TEXT("GuLiGroundSource-v1"));
	Hash.AddString(FString::Printf(TEXT("UE-%d.%d.%d-Detour-%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION, DT_NAVMESH_VERSION));
	Hash.AddString(World->GetOutermost()->GetName());
	Hash.AddString(Navigation->GetPathName());
	Hash.AddString(Navigation->GetClass()->GetPathName());
	Hash.AddTransform(Navigation->GetActorTransform());
	const FNavDataConfig& Agent = Navigation->GetConfig();
	Hash.AddVector(FVector(Agent.AgentRadius, Agent.AgentHeight, Agent.AgentStepHeight));
	Hash.AddUInt64(Agent.bCanCrouch); Hash.AddUInt64(Agent.bCanJump);
	Hash.AddUInt64(Agent.bCanWalk); Hash.AddUInt64(Agent.bCanSwim); Hash.AddUInt64(Agent.bCanFly);
	Hash.AddString(GetPathNameSafe(Agent.GetNavDataClass<ANavigationData>()));
	for (TFieldIterator<FProperty> It(Navigation->GetClass()); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient)) continue;
		const FString Category = It->GetMetaData(TEXT("Category"));
		if (Category.Contains(TEXT("Generation")) || Category.Contains(TEXT("Agent"))
			|| It->GetFName() == TEXT("RuntimeGeneration"))
			Hash.AddProperty(Navigation, *It->GetName());
	}
	TArray<AActor*> Actors;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (!It->IsEditorOnly() && !It->IsA<ANavigationData>() && !It->IsA<AGuLiFlightNavigationVolume>()) Actors.Add(*It);
	Actors.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });
	for (AActor* Actor : Actors)
	{
		if (Actor->IsA<ANavMeshBoundsVolume>() || Actor->IsA<ANavModifierVolume>() || Actor->IsA<ANavLinkProxy>())
		{
			Hash.AddString(Actor->GetPathName());
			Hash.AddTransform(Actor->GetActorTransform());
			const FBox Bounds = Actor->GetComponentsBoundingBox(true);
			Hash.AddVector(Bounds.Min); Hash.AddVector(Bounds.Max);
			for (const TCHAR* Name : { TEXT("SupportedAgents"), TEXT("AreaClass"), TEXT("PointLinks"), TEXT("SegmentLinks"), TEXT("bSmartLinkIsRelevant") })
				Hash.AddProperty(Actor, Name);
		}
		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		Components.Sort([](const UActorComponent& A, const UActorComponent& B) { return A.GetPathName() < B.GetPathName(); });
		for (const UActorComponent* Component : Components)
		{
			if (!Component->IsRegistered() || Component->IsEditorOnly()) continue;
			if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				if (Primitive->CanEverAffectNavigation() && Primitive->IsNavigationRelevant()) Hash.AddCollisionSource(Primitive);
			}
			if (const UNavRelevantComponent* Relevant = Cast<UNavRelevantComponent>(Component))
			{
				if (!Relevant->IsNavigationRelevant()) continue;
				Hash.AddString(Relevant->GetPathName());
				Hash.AddTransform(Actor->GetActorTransform());
				const FBox Bounds = Relevant->GetNavigationBounds();
				Hash.AddVector(Bounds.Min); Hash.AddVector(Bounds.Max);
				for (TFieldIterator<FProperty> It(Relevant->GetClass()); It; ++It)
					if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
						&& !It->GetName().Contains(TEXT("Draw")) && !It->GetName().Contains(TEXT("Color")))
						Hash.AddProperty(Relevant, *It->GetName());
				if (const UNavModifierComponent* Modifier = Cast<UNavModifierComponent>(Relevant))
				{
					if (Modifier->AreaClass)
						for (const TCHAR* Name : { TEXT("DefaultCost"), TEXT("FixedAreaEnteringCost"), TEXT("SupportedAgents"), TEXT("AreaFlags") })
							Hash.AddProperty(Modifier->AreaClass->GetDefaultObject(), Name);
				}
			}
		}
	}
	return Hash.Get();
}

FGuLiNavigationBakeResult UGuLiNavigationBakeLibrary::ValidateWorldNavigation(UObject* WorldContextObject)
{
	using namespace GuLiNavigationBake;
	const double Start = FPlatformTime::Seconds();
	FGuLiNavigationBakeResult Result;
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!CheckWorld(World, Result.Message)) return Result;
	Result.bSuccess = true;
	const TArray<ARecastNavMesh*> Ground = GroundData(World);
	if (HasMissingGroundAgent(World, FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)))
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{ Result.bSuccess = false; Result.Message = TEXT("Navigation bounds exist but a required ground agent NavData is missing."); break; }
	for (ARecastNavMesh* Navigation : Ground)
		Result.bSuccess &= CheckGround(World, Navigation, Result.Entries.AddDefaulted_GetRef());
	for (AGuLiFlightNavigationVolume* Volume : FlightVolumes(World))
		Result.bSuccess &= CheckFlight(Volume, Result.Entries.AddDefaulted_GetRef());
	if (!Result.bSuccess && Result.Message.IsEmpty())
		for (const auto& Entry : Result.Entries)
			if (Entry.Status != TEXT("Hit")) { Result.Message = Entry.ObjectPath + TEXT(": ") + Entry.Message; break; }
	Result.TotalSeconds = FPlatformTime::Seconds() - Start;
	return Result;
}

FGuLiNavigationBakeResult UGuLiNavigationBakeLibrary::PrepareWorldNavigation(UObject* WorldContextObject, const bool bSavePackages)
{
	using namespace GuLiNavigationBake;
	const double Start = FPlatformTime::Seconds();
	FGuLiNavigationBakeResult Result;
	UWorld* World = ResolveWorld(WorldContextObject);
	const auto Finish = [&]()
	{
		Result.TotalSeconds = FPlatformTime::Seconds() - Start;
		LogResult(Result, World);
		return Result;
	};
	if (!IsInGameThread() || bPreparing) { Result.Message = TEXT("Navigation preparation is already active or not on the game thread."); return Finish(); }
	TGuardValue<bool> Guard(bPreparing, true);
	if (!CheckWorld(World, Result.Message)) return Finish();
	FScopedSlowTask Progress(3, NSLOCTEXT("GuLiNavigation", "Preparing", "Checking and preparing map navigation"));
	Progress.MakeDialog(true);
	UE_LOG(LogGuLiNavigationBake, Display, TEXT("[GULI_NAV_PREPARE] stage=AssetCompilation world=%s"), *World->GetPathName());
	FAssetCompilingManager::Get().FinishAllCompilation();
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	bool bRecoveredGroundData = false;
	if (NavigationSystem)
	{
		if (NavigationSystem->IsThereAnywhereToBuildNavigation() && HasMissingGroundAgent(World, NavigationSystem))
		{
			// Public Build creates and registers missing agent instances as well as generating their tiles.
			NavigationSystem->Build();
			bRecoveredGroundData = true;
			Result.GroundRebuilds = GroundData(World).Num();
		}
		// Process changes made immediately before Play before fingerprinting source collision.
		NavigationSystem->Tick(0.0f);
		for (ARecastNavMesh* Navigation : GroundData(World))
		{
			if (ScaleMigrationWorlds.Contains(World))
			{
				// These jobs were queued with the old agent/generator configuration while
				// loading. They cannot certify the migrated source. CheckGround below will
				// request a fresh rebuild with the new settings and commit its signature.
				Navigation->CancelBuild();
				UE_LOG(LogGuLiNavigationBake, Display, TEXT("[GULI_NAV_PREPARE] stage=CancelStaleScaleQueue object=%s"), *Navigation->GetPathName());
				continue;
			}
			UE_LOG(LogGuLiNavigationBake, Display, TEXT("[GULI_NAV_PREPARE] stage=SettleGround object=%s asyncGather=%d maxJobs=%d"),
				*Navigation->GetPathName(), Navigation->bDoFullyAsyncNavDataGathering,
				Navigation->GetMaxSimultaneousTileGenerationJobsCount());
			// Completion can also process an already queued editor rebuild. Use the same
			// gathering contract as RebuildGround, not UE's serial async-gather path.
			const bool bPreviousAsyncGathering = Navigation->bDoFullyAsyncNavDataGathering;
			Navigation->bDoFullyAsyncNavDataGathering = false;
			const bool bComplete = CompleteGround(Navigation, Result.Message);
			Navigation->bDoFullyAsyncNavDataGathering = bPreviousAsyncGathering;
			if (!bComplete) return Finish();
		}
	}
	TArray<ARecastNavMesh*> Ground = GroundData(World);
	if (Ground.IsEmpty() && NavigationSystem && NavigationSystem->IsThereAnywhereToBuildNavigation())
	{
		NavigationSystem->Build();
		Ground = GroundData(World);
	}
	if (Ground.IsEmpty())
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{ Result.Message = TEXT("Cannot create required ground navigation data."); return Finish(); }
	TArray<ARecastNavMesh*> ChangedGround;
	TArray<UPackage*> Packages;
	if (PendingSaveWorlds.Contains(World)) Packages.AddUnique(World->GetPackage());
	Progress.EnterProgressFrame(1);
	for (ARecastNavMesh* Navigation : Ground)
	{
		UE_LOG(LogGuLiNavigationBake, Display, TEXT("[GULI_NAV_PREPARE] stage=CheckGround object=%s"), *Navigation->GetPathName());
		FGuLiNavigationBakeEntry& Entry = Result.Entries.AddDefaulted_GetRef();
		if (CheckGround(World, Navigation, Entry))
		{
			if (bRecoveredGroundData) { Entry.Status = TEXT("Rebuilt"); ChangedGround.Add(Navigation); Packages.AddUnique(Navigation->GetPackage()); }
			continue;
		}
		if (Progress.ShouldCancel()) { Result.Message = TEXT("Navigation preparation cancelled."); return Finish(); }
		if (!NavigationSystem || NavigationSystem->IsNavigationBuildingLocked(static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)))
		{ Result.Message = TEXT("Ground navigation is locked by another operation."); return Finish(); }
		const double BuildStart = FPlatformTime::Seconds();
		if (!bRecoveredGroundData)
		{
			if (!RebuildGround(Navigation, Result.Message)) return Finish();
			++Result.GroundRebuilds;
		}
		Entry.BuildSeconds = FPlatformTime::Seconds() - BuildStart;
		FString Payload;
		if (Entry.SourceHash != Hex(ComputeGroundSourceHash(World, Navigation)) || !GroundPayload(Navigation, Payload, Result.Message))
		{ if (Result.Message.IsEmpty()) Result.Message = TEXT("Ground source changed during baking."); return Finish(); }
		Entry.Status = TEXT("Rebuilt");
		ChangedGround.Add(Navigation);
		Packages.AddUnique(Navigation->GetPackage());
	}
	Progress.EnterProgressFrame(1);
	for (AGuLiFlightNavigationVolume* Volume : FlightVolumes(World))
	{
		const UGuLiFlightNavigationData* Existing = Volume->NavigationData;
		const bool bOwnedByAnotherSource = Existing && !Existing->Metadata.SourceWorldPackage.IsNone()
			&& (Existing->Metadata.SourceWorldPackage != World->GetOutermost()->GetFName()
				|| Existing->Metadata.SourceVolumePath != Volume->GetPathName());
		if (!Existing || bOwnedByAnotherSource)
		{
			// Duplicated maps/volumes must not overwrite another source world's shared graph.
			const FString MapPackage = World->GetOutermost()->GetName();
			const FString AssetName = FString::Printf(TEXT("DA_Flight_%s_%08x"),
				*FPackageName::GetShortName(MapPackage), FCrc::StrCrc32(*Volume->GetPathName()));
			const FString PackageName = FPackageName::GetLongPackagePath(MapPackage) + TEXT("/Navigation/") + AssetName;
			UGuLiFlightNavigationData* Data = LoadObject<UGuLiFlightNavigationData>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn);
			if (!Data)
			{
				UPackage* Package = CreatePackage(*PackageName);
				Data = NewObject<UGuLiFlightNavigationData>(Package, FName(*AssetName), RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(Data);
				Data->MarkPackageDirty();
			}
			Volume->Modify();
			Volume->NavigationData = Data;
			Volume->MarkPackageDirty();
			Packages.AddUnique(Volume->GetPackage());
		}
		if (Volume->NavigationData->GetPackage()->IsDirty())
		{
			Packages.AddUnique(Volume->NavigationData->GetPackage());
			Packages.AddUnique(Volume->GetPackage());
		}
		FGuLiNavigationBakeEntry& Entry = Result.Entries.AddDefaulted_GetRef();
		if (CheckFlight(Volume, Entry)) continue;
		if (Progress.ShouldCancel()) { Result.Message = TEXT("Navigation preparation cancelled."); return Finish(); }
		const double BuildStart = FPlatformTime::Seconds();
		FText Error;
		if (!UGuLiFlightNavigationEditorLibrary::BakeVolume(Volume, Volume->AuthoringBakeSettings, Error))
		{ Result.Message = Volume->GetPathName() + TEXT(": ") + Error.ToString(); return Finish(); }
		Entry.BuildSeconds = FPlatformTime::Seconds() - BuildStart;
		++Result.FlightRebuilds;
		if (Entry.SourceHash != Hex(Volume->NavigationData->Metadata.GeometrySignature)
			|| !UGuLiFlightNavigationEditorLibrary::ValidateVolume(Volume, Volume->AuthoringBakeSettings, Error))
		{ Result.Message = TEXT("Flight source changed during baking: ") + Error.ToString(); return Finish(); }
		Entry.Status = TEXT("Rebuilt");
		Packages.AddUnique(Volume->NavigationData->GetPackage());
		Packages.AddUnique(Volume->GetPackage());
	}
	if (Progress.ShouldCancel()) { Result.Message = TEXT("Navigation preparation cancelled."); return Finish(); }
	// Commit metadata only after every bake has succeeded. Metadata and ground tiles share a package.
	for (ARecastNavMesh* Navigation : ChangedGround)
	{
		FString Payload;
		if (!GroundPayload(Navigation, Payload, Result.Message)) return Finish();
		FMetaData& Metadata = Navigation->GetPackage()->GetMetaData();
		Metadata.SetValue(Navigation, SourceKey, *Hex(ComputeGroundSourceHash(World, Navigation)));
		Metadata.SetValue(Navigation, PayloadKey, *Payload);
		Navigation->MarkPackageDirty();
	}
	if (!Packages.IsEmpty())
		for (ULevel* Level : World->GetLevels()) if (Level) Packages.AddUnique(Level->GetPackage());
	Progress.EnterProgressFrame(1);
	if (bSavePackages && !Packages.IsEmpty())
	{
		PendingSaveWorlds.Add(World);
		const double SaveStart = FPlatformTime::Seconds();
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		Result.SaveSeconds = FPlatformTime::Seconds() - SaveStart;
		if (!bSaved)
		{
			// A retry must save again; never turn a failed persistent write into an in-memory hit.
			for (ARecastNavMesh* Navigation : ChangedGround)
				Navigation->GetPackage()->GetMetaData().RemoveValue(Navigation, SourceKey);
			Result.Message = TEXT("Navigation save failed; PIE/cook is blocked. Check package writability.");
			return Finish();
		}
		PendingSaveWorlds.Remove(World);
	}
	const FGuLiNavigationBakeResult Validation = ValidateWorldNavigation(World);
	Result.bSuccess = Validation.bSuccess;
	Result.Message = Validation.bSuccess ? TEXT("All source navigation is ready.") : Validation.Message;
	if (Result.bSuccess) ScaleMigrationWorlds.Remove(World);
	return Finish();
}

TArray<FName> UGuLiNavigationBakeLibrary::GetPreparationWorldPackages()
{
	TArray<FName> Result = GetDefault<UGuLiFlightNavigationCookSettings>()->RequiredWorldPackages;
	for (const FFilePath& Map : GetDefault<UProjectPackagingSettings>()->MapsToCook)
		if (!Map.FilePath.IsEmpty()) Result.AddUnique(FName(*Map.FilePath));
	Result.Sort(FNameLexicalLess());
	return Result;
}

FGuLiNavigationBakeResult UGuLiNavigationBakeLibrary::MigrateObjectScale020(UObject* Context)
{
	using namespace GuLiNavigationBake;
	FGuLiNavigationBakeResult Result;
	UWorld* World = ResolveWorld(Context);
	if (!CheckWorld(World, Result.Message)) return Result;
	const auto& Agents = GetDefault<UNavigationSystemV1>()->GetSupportedAgents();
	for (ARecastNavMesh* Navigation : GroundData(World))
	{
		const FNavDataConfig Before = Navigation->GetConfig();
		const auto* Target = Agents.FindByPredicate([&](const FNavDataConfig& A) { return A.Name == Before.Name; });
		if (!Target) { Result.Message = TEXT("Unrecognized ground Agent; no guessed scale."); return Result; }
		Navigation->Modify();
		Navigation->SetConfig(*Target);
		for (uint8 Index = 0; Index < static_cast<uint8>(ENavigationDataResolution::MAX); ++Index)
		{
			const auto Resolution = static_cast<ENavigationDataResolution>(Index);
			// Horizontal cells and tiles deliberately stay at the authored resolution.
			Navigation->SetCellHeight(Resolution, 5.0f);
			Navigation->SetAgentMaxStepHeight(Resolution, Target->AgentStepHeight);
		}
		Navigation->MarkPackageDirty();
		auto& Entry = Result.Entries.AddDefaulted_GetRef();
		Entry.ObjectPath = Navigation->GetPathName(); Entry.Kind = TEXT("GroundScale020");
		Entry.Status = TEXT("Configured");
		Entry.Message = FString::Printf(TEXT("Radius %.3f -> %.3f; height %.3f -> %.3f; cellZ=5; step=%.3f; XY unchanged"),
			Before.AgentRadius, Target->AgentRadius, Before.AgentHeight, Target->AgentHeight, Target->AgentStepHeight);
	}
	for (AGuLiFlightNavigationVolume* Volume : FlightVolumes(World))
	{
		auto& Settings = Volume->AuthoringBakeSettings;
		const float BeforeRadius = Settings.AgentRadius;
		const float BeforePortal = Settings.MinimumPortalSpan;
		if (!FMath::IsNearlyEqual(BeforeRadius, 1500.0f) && !FMath::IsNearlyEqual(BeforeRadius, 300.0f))
		{ Result.Message = TEXT("Unexpected Flight Agent radius; manual merge required."); return Result; }
		Volume->Modify();
		Settings.AgentRadius = 300.0f;
		if (FMath::IsNearlyEqual(BeforePortal, 25.0f)) Settings.MinimumPortalSpan = 5.0f;
		else if (FMath::IsNearlyEqual(BeforePortal, 10.0f)) Settings.MinimumPortalSpan = 2.0f;
		else if (!FMath::IsNearlyEqual(BeforePortal, 5.0f) && !FMath::IsNearlyEqual(BeforePortal, 2.0f))
		{ Result.Message = TEXT("Unexpected Flight portal span; manual merge required."); return Result; }
		Volume->MarkPackageDirty();
		auto& Entry = Result.Entries.AddDefaulted_GetRef();
		Entry.ObjectPath = Volume->GetPathName(); Entry.Kind = TEXT("FlightScale020"); Entry.Status = TEXT("Configured");
		Entry.Message = FString::Printf(TEXT("Radius %.3f -> %.3f; portal %.3f -> %.3f; bounds/cells unchanged"),
			BeforeRadius, Settings.AgentRadius, BeforePortal, Settings.MinimumPortalSpan);
	}
	ScaleMigrationWorlds.Add(World);
	Result.bSuccess = true; Result.Message = TEXT("Scale020 configured; rebuild and validation still required.");
	return Result;
}
