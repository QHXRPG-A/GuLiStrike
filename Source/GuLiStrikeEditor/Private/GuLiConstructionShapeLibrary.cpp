#include "GuLiConstructionShapeLibrary.h"
#include "Gameplay/Building/GuLiConstructionShape.h"
#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingVisuals.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "ConstrainedDelaunay2.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace GuLiConstructionBake
{
using namespace UE::Geometry;
constexpr TCHAR Root[] = TEXT("/Game/GuLiStrike/Buildings/Construction/Shapes/");
struct FSource { UStaticMesh* Mesh = nullptr; FTransform Transform; };
struct FTriangle { FVector P[3]; };

bool Save(UObject* Asset)
{
	if (!Asset || !Asset->GetPackage()->GetName().StartsWith(Root)) return false;
	const FString File = FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Asset->GetPackage(), Asset, *File, Args);
}

template<class T> T* Asset(const FString& Package)
{
	if (auto* Existing = LoadObject<T>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)))) return Existing;
	auto* Created = NewObject<T>(CreatePackage(*Package), *FPackageName::GetShortName(Package), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Created); return Created;
}

FString Signature(const TArray<FSource>& Sources)
{
	FString Text = TEXT("ConstructionShape-v5:render-vertices;band=.1,10,50;tol=1..8;");
	for (const auto& Source : Sources)
	{
		Text += Source.Mesh->GetPathName() + Source.Transform.ToString() + Source.Mesh->GetLightingGuid().ToString();
		if (const auto* Render = Source.Mesh->GetRenderData()) Text += Render->DerivedDataKey;
	}
	return FMD5::HashAnsiString(*Text);
}

void DefinitionSources(const FGuLiBuildingDefinition& D, TArray<FSource>& Out)
{
	if (D.Category != EGuLiBuildingCategory::Factory)
	{
		if (D.Mesh) Out.Add({D.Mesh, FTransform(FQuat::Identity, D.VisualOffset, D.MeshScale)});
		return;
	}
	// Resolve the real presentation through its existing gameplay configuration, never the placement cube.
	const auto* Config = LoadObject<UGuLiResourceEconomyConfig>(nullptr,
		TEXT("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy.DA_ResourceEconomy"));
	auto* Class = Config ? Cast<UBlueprintGeneratedClass>(Config->FactoryPresentationClass.LoadSynchronous()) : nullptr;
	if (!Class || !Class->SimpleConstructionScript) return;
	TArray<FSource> Candidates, Explicit;
	TFunction<void(USCS_Node*, const FTransform&)> Visit = [&](USCS_Node* Node, const FTransform& Parent)
	{
		auto* Scene = Cast<USceneComponent>(Node->GetActualComponentTemplate(Class));
		const FTransform Transform = Scene ? Scene->GetRelativeTransform() * Parent : Parent;
		if (auto* Mesh = Cast<UStaticMeshComponent>(Scene); Mesh && Mesh->GetStaticMesh()
			&& !Mesh->ComponentHasTag(TEXT("ConstructionFootprintExclude")))
		{
			Candidates.Add({Mesh->GetStaticMesh(), Transform});
			if (Mesh->ComponentHasTag(TEXT("ConstructionFootprintSource"))) Explicit.Add(Candidates.Last());
		}
		for (auto* Child : Node->GetChildNodes()) Visit(Child, Transform);
	};
	for (auto* Node : Class->SimpleConstructionScript->GetRootNodes())
		Visit(Node, FTransform(FQuat::Identity, FVector::ZeroVector, FVector(GuLiBuildingVisuals::FactoryScale)));
	if (!Explicit.IsEmpty()) { Out = MoveTemp(Explicit); return; }
	// A composed industrial prefab's main structural shell is its largest solid component.
	// This default excludes the animated door and small access ramp without relying on names.
	Candidates.Sort([](const FSource& A, const FSource& B)
	{
		return A.Mesh->GetBoundingBox().TransformBy(A.Transform).GetVolume() > B.Mesh->GetBoundingBox().TransformBy(B.Transform).GetVolume();
	});
	if (!Candidates.IsEmpty()) Out.Add(Candidates[0]);
}

bool Geometry(const TArray<FSource>& Sources, TArray<FTriangle>& Triangles, FBox& Bounds)
{
	for (const FSource& Source : Sources)
	{
		const auto* Render = Source.Mesh->GetRenderData();
		if (!Render || Render->LODResources.IsEmpty()) return false;
		FMeshDescription Description;
		Source.Mesh->ExportStaticMeshLOD(Render->LODResources[0], Description);
		if (Description.IsEmpty()) return false;
		const auto Positions = FStaticMeshConstAttributes(Description).GetVertexPositions();
		for (FTriangleID Id : Description.Triangles().GetElementIDs())
		{
			auto& Triangle = Triangles.AddDefaulted_GetRef(); int32 I = 0;
			for (FVertexID Vertex : Description.GetTriangleVertices(Id))
			{
				// Read indexed render vertices only. Bounds/extensions must never deform the footprint.
				const FVector P(Positions[Vertex]);
				Triangle.P[I] = Source.Transform.TransformPosition(P); Bounds += Triangle.P[I++];
			}
		}
	}
	return !Triangles.IsEmpty() && Bounds.IsValid;
}

TArray<FVector> Clip(const TArray<FVector>& Points, double Z, bool bAbove)
{
	TArray<FVector> Out;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const FVector A = Points[I], B = Points[(I + 1) % Points.Num()];
		const bool InA = bAbove ? A.Z >= Z : A.Z <= Z, InB = bAbove ? B.Z >= Z : B.Z <= Z;
		if (InA) Out.Add(A);
		if (InA != InB) Out.Add(FMath::Lerp(A, B, (Z - A.Z) / (B.Z - A.Z)));
	}
	return Out;
}

void AddSections(const TArray<FTriangle>& Triangles, double Z, TArray<FGeneralPolygon2d>& Polygons)
{
	TMap<FIntPoint, int32> VertexMap; TArray<FVector2d> Vertices; TArray<TArray<int32>> Adj;
	TSet<FIntPoint> Edges;
	auto Vertex = [&](FVector P)
	{
		FIntPoint Key(FMath::RoundToInt(P.X * 10), FMath::RoundToInt(P.Y * 10));
		if (auto* Found = VertexMap.Find(Key)) return *Found;
		const int32 Id = Vertices.Add(FVector2d(Key.X / 10., Key.Y / 10.)); VertexMap.Add(Key, Id); Adj.AddDefaulted(); return Id;
	};
	for (const auto& T : Triangles)
	{
		TArray<FVector, TInlineAllocator<3>> Hits;
		for (int32 I = 0; I < 3; ++I)
		{
			const FVector A = T.P[I], B = T.P[(I + 1) % 3];
			if ((A.Z < Z) != (B.Z < Z)) Hits.Add(FMath::Lerp(A, B, (Z - A.Z) / (B.Z - A.Z)));
		}
		if (Hits.Num() != 2) continue;
		const int32 A = Vertex(Hits[0]), B = Vertex(Hits[1]);
		const FIntPoint Edge(FMath::Min(A, B), FMath::Max(A, B));
		if (A == B || Edges.Contains(Edge)) continue;
		Edges.Add(Edge); Adj[A].Add(B); Adj[B].Add(A);
	}
	TArray<FPolygon2d> Loops; TSet<FIntPoint> Used;
	for (const FIntPoint& Edge : Edges)
	{
		if (Used.Contains(Edge)) continue;
		TArray<FVector2d> Points; int32 Previous = Edge.X, Current = Edge.Y;
		Points.Add(Vertices[Previous]); Used.Add(Edge);
		for (int32 Step = 0; Step <= Edges.Num(); ++Step)
		{
			if (Current == Edge.X) break;
			Points.Add(Vertices[Current]); int32 Next = INDEX_NONE;
			for (int32 Candidate : Adj[Current])
			{
				const FIntPoint K(FMath::Min(Current, Candidate), FMath::Max(Current, Candidate));
				if (Candidate != Previous && !Used.Contains(K)) { Next = Candidate; Used.Add(K); break; }
			}
			if (Next == INDEX_NONE) break;
			Previous = Current; Current = Next;
		}
		if (Current == Edge.X && Points.Num() >= 3)
		{
			FPolygon2d Loop(Points); Loop.Simplify(.05, .2);
			if (FMath::Abs(Loop.SignedArea()) > 1.) Loops.Add(MoveTemp(Loop));
		}
	}
	Loops.Sort([](const auto& A, const auto& B) { return FMath::Abs(A.SignedArea()) > FMath::Abs(B.SignedArea()); });
	TArray<int32> Depth, Parent; Depth.Init(0, Loops.Num()); Parent.Init(INDEX_NONE, Loops.Num());
	for (int32 I = 0; I < Loops.Num(); ++I)
		for (int32 J = I - 1; J >= 0; --J)
			if (Loops[J].Contains(Loops[I].GetVertices()[0])) { Parent[I] = J; Depth[I] = Depth[J] + 1; break; }
	for (int32 I = 0; I < Loops.Num(); ++I)
	{
		if (Depth[I] % 2) continue;
		if (Loops[I].IsClockwise()) Loops[I].Reverse();
		FGeneralPolygon2d Polygon(Loops[I]);
		for (int32 J = I + 1; J < Loops.Num(); ++J)
			if (Parent[J] == I)
			{
				if (!Loops[J].IsClockwise()) Loops[J].Reverse();
				Polygon.AddHole(Loops[J], false, false);
			}
		Polygons.Add(MoveTemp(Polygon));
	}
}

bool Footprint(const TArray<FTriangle>& Triangles, const FBox& Bounds, TArray<FGeneralPolygon2d>& Result)
{
	const double Low = Bounds.Min.Z, Band = FMath::Clamp(Bounds.GetSize().Z * .1, 10., 50.);
	TArray<FGeneralPolygon2d> Projected;
	for (const auto& T : Triangles)
	{
		TArray<FVector> Points{T.P[0], T.P[1], T.P[2]}; Points = Clip(Clip(Points, Low - .01, true), Low + Band, false);
		if (Points.Num() < 3) continue;
		TArray<FVector2d> XY; for (const auto& P : Points) XY.Add(FVector2d(P.X, P.Y));
		FPolygon2d Polygon(XY);
		if (FMath::Abs(Polygon.SignedArea()) < .001) continue;
		if (Polygon.IsClockwise()) Polygon.Reverse();
		Projected.Add(FGeneralPolygon2d(Polygon));
	}
	// Closed sections recover the interior of shells whose underside is intentionally open.
	for (int32 Slice = 0; Slice < 5; ++Slice) AddSections(Triangles, Low + Band * (.02 + Slice * .24), Projected);
	if (Projected.IsEmpty() || !PolygonsUnion(Projected, Result, false)) return false;
	TArray<FGeneralPolygon2d> Clean;
	for (const auto& Polygon : Result)
	{
		FPolygon2d Outer = Polygon.GetOuter(); Outer.Simplify(.1, 1.);
		if (Outer.VertexCount() < 3 || FMath::Abs(Outer.SignedArea()) < 1.) continue;
		FGeneralPolygon2d P(Outer);
		for (auto Hole : Polygon.GetHoles())
		{
			Hole.Simplify(.1, 1.); if (Hole.VertexCount() >= 3 && FMath::Abs(Hole.SignedArea()) >= 1.) P.AddHole(Hole, false, false);
		}
		Clean.Add(MoveTemp(P));
	}
	Result = MoveTemp(Clean); return !Result.IsEmpty();
}

UStaticMesh* MakeMesh(const FString& Package, const TArray<FVector>& Vertices, const TArray<FVector2D>& UVs)
{
	if (Vertices.IsEmpty()) return nullptr;
	FMeshDescription Description; FStaticMeshAttributes Attr(Description); Attr.Register();
	auto Positions = Attr.GetVertexPositions(); auto Normals = Attr.GetVertexInstanceNormals();
	auto Tangents = Attr.GetVertexInstanceTangents(); auto Signs = Attr.GetVertexInstanceBinormalSigns();
	auto Tex = Attr.GetVertexInstanceUVs(); Tex.SetNumChannels(1);
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	for (int32 I = 0; I < Vertices.Num(); I += 3)
	{
		const FVector Normal = FVector::CrossProduct(Vertices[I + 1] - Vertices[I], Vertices[I + 2] - Vertices[I]).GetSafeNormal();
		const FVector Tangent = (Vertices[I + 1] - Vertices[I]).GetSafeNormal();
		TArray<FVertexInstanceID> Instances;
		for (int32 J = 0; J < 3; ++J)
		{
			const FVertexID V = Description.CreateVertex(); Positions[V] = FVector3f(Vertices[I + J]);
			const auto VI = Description.CreateVertexInstance(V); Instances.Add(VI);
			Normals[VI] = FVector3f(Normal); Tangents[VI] = FVector3f(Tangent); Signs[VI] = 1; Tex.Set(VI, 0, FVector2f(UVs[I + J]));
		}
		Description.CreateTriangle(Group, Instances);
	}
	auto* Mesh = Asset<UStaticMesh>(Package); Mesh->Modify(); Mesh->SetNumSourceModels(1);
	*Mesh->CreateMeshDescription(0) = MoveTemp(Description); Mesh->CommitMeshDescription(0);
	Mesh->GetStaticMaterials().Reset(); Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
	Mesh->GetSourceModel(0).BuildSettings.bRecomputeNormals = false;
	Mesh->GetSourceModel(0).BuildSettings.bRecomputeTangents = false;
	Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs = false;
	Mesh->Build(false); TArray<UStaticMesh*> Pending{Mesh}; FStaticMeshCompilingManager::Get().FinishCompilation(Pending);
	return Save(Mesh) ? Mesh : nullptr;
}

UGuLiConstructionShape* Bake(const TArray<FSource>& Sources, const FString& Package, const FString& Version)
{
	TArray<FTriangle> Triangles; FBox Bounds(ForceInit); TArray<FGeneralPolygon2d> Polygons;
	if (!Geometry(Sources, Triangles, Bounds) || !Footprint(Triangles, Bounds, Polygons))
	{
		UE_LOG(LogTemp, Error, TEXT("[ConstructionShape] Cannot extract base geometry: %s"), *Package); return nullptr;
	}
	int32 OuterEdges = 0; for (const auto& P : Polygons) OuterEdges += P.GetOuter().VertexCount();
	// Very detailed bases retain their silhouette while dropping sub-grid bevels and bolt-sized turns.
	for (double Tolerance = 2.; OuterEdges > 192 && Tolerance <= 8.; Tolerance *= 2.)
	{
		OuterEdges = 0;
		for (auto& P : Polygons)
		{
			auto Outer = P.GetOuter(); Outer.Simplify(.1, Tolerance);
			FGeneralPolygon2d Simplified(Outer);
			for (auto Hole : P.GetHoles())
			{
				Hole.Simplify(.1, Tolerance); if (Hole.VertexCount() >= 3) Simplified.AddHole(Hole, false, false);
			}
			P = MoveTemp(Simplified); OuterEdges += P.GetOuter().VertexCount();
		}
	}
	if (OuterEdges > 256) { UE_LOG(LogTemp, Error, TEXT("[ConstructionShape] %s has %d outer edges; provide a simpler explicit profile."), *Package, OuterEdges); return nullptr; }
	TArray<FGuLiConstructionContour> Contours;
	TArray<FVector> WallVertices, BaseVertices; TArray<FVector2D> WallUVs, BaseUVs;
	for (const auto& P : Polygons)
	{
		auto AddContour = [&](const FPolygon2d& Polygon, bool bHole)
		{
			auto& C = Contours.AddDefaulted_GetRef(); C.bHole = bHole;
			for (const auto& XY : Polygon.GetVertices()) C.Points.Add(FVector2D(XY));
			if (bHole) return;
			for (int32 E = 0; E < C.Points.Num(); ++E)
			{
				const FVector A(C.Points[E], 0), B(C.Points[(E + 1) % C.Points.Num()], 0), Up(0, 0, 100);
				WallVertices.Append({A, B, B + Up, A, B + Up, A + Up});
				WallUVs.Append({FVector2D(0,1), FVector2D(1,1), FVector2D(1,0), FVector2D(0,1), FVector2D(1,0), FVector2D(0,0)});
			}
		};
		AddContour(P.GetOuter(), false); for (const auto& Hole : P.GetHoles()) AddContour(Hole, true);
		FConstrainedDelaunay2d CDT; CDT.bOutputCCW = true; CDT.bSplitBowties = true;
		if (!CDT.AddWithIntersectionResolution(P) || !CDT.Triangulate() || CDT.Triangles.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("[ConstructionShape] Floor triangulation failed: %s, outer=%d holes=%d"),
				*Package, P.GetOuter().VertexCount(), P.GetHoles().Num()); return nullptr;
		}
		for (const FIndex3i& T : CDT.Triangles)
			for (int32 J = 0; J < 3; ++J)
			{
				const FVector2D XY(CDT.Vertices[T[J]]); BaseVertices.Add(FVector(XY, 0));
				BaseUVs.Add(FVector2D((XY.X - Bounds.Min.X) / FMath::Max(1., Bounds.GetSize().X), (XY.Y - Bounds.Min.Y) / FMath::Max(1., Bounds.GetSize().Y)));
			}
	}
	const FString Stem = FPackageName::GetShortName(Package).Replace(TEXT("DA_"), TEXT("SM_"));
	auto* Wall = MakeMesh(FString(Root) + Stem + TEXT("_Wall"), WallVertices, WallUVs);
	auto* Base = MakeMesh(FString(Root) + Stem + TEXT("_Base"), BaseVertices, BaseUVs);
	if (!Wall || !Base) return nullptr;
	auto* Shape = Asset<UGuLiConstructionShape>(Package); Shape->Modify();
	Shape->Contours = MoveTemp(Contours); Shape->Bounds = Bounds; Shape->SourceVersion = Version;
	Shape->WallMesh = Wall; Shape->BaseMesh = Base; Shape->SourceAssets.Reset();
	for (const auto& Source : Sources) Shape->SourceAssets.AddUnique(FSoftObjectPath(Source.Mesh));
	return Save(Shape) ? Shape : nullptr;
}
}

FString UGuLiConstructionShapeLibrary::PrepareConstructionShapes(bool bForce)
{
	using namespace GuLiConstructionBake;
	static bool bRunning = false;
	if (bRunning) return TEXT("{\"success\":false,\"error\":\"reentrant bake\"}");
	TGuardValue<bool> Guard(bRunning, true);
	auto Report = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Entries; bool bSuccess = true;
	auto* Buildings = UGuLiBuildingCatalog::LoadDefaultCatalog();
	if (!Buildings || !Buildings->ResolveTable(true)) return TEXT("{\"success\":false,\"error\":\"building table unavailable\"}");
	const FString CatalogPackage = GetDefault<UGuLiBuildingConstructionSettings>()->ShapeCatalog.ToSoftObjectPath().GetLongPackageName();
	if (!CatalogPackage.StartsWith(Root)) return TEXT("{\"success\":false,\"error\":\"invalid owned shape catalog path\"}");
	auto* Catalog = Asset<UGuLiConstructionShapeCatalog>(CatalogPackage); bool bCatalogChanged = false;
	// Deleted or temporary definitions must not leave stale cache references in cooked data.
	for (auto It = Catalog->Shapes.CreateIterator(); It; ++It)
		if (!Buildings->FindById(It.Key())) { Catalog->Modify(); It.RemoveCurrent(); bCatalogChanged = true; }
	for (const auto& D : Buildings->Definitions)
	{
		TArray<FSource> Sources; DefinitionSources(D, Sources);
		TArray<UStaticMesh*> Pending; for (const auto& Source : Sources) Pending.AddUnique(Source.Mesh);
		FStaticMeshCompilingManager::Get().FinishCompilation(Pending);
		const FString Version = Signature(Sources), Package = FString(Root) + FString::Printf(TEXT("DA_ConstructionShape_%d"), D.DefinitionId);
		auto* Shape = LoadObject<UGuLiConstructionShape>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
		const bool bStale = bForce || !Shape || !Shape->IsUsable() || Shape->SourceVersion != Version || !Shape->WallMesh || !Shape->BaseMesh;
		if (bStale) Shape = Sources.IsEmpty() ? nullptr : Bake(Sources, Package, Version);
		auto Entry = MakeShared<FJsonObject>(); Entry->SetNumberField(TEXT("definition"), D.DefinitionId);
		Entry->SetBoolField(TEXT("success"), Shape != nullptr); Entry->SetBoolField(TEXT("rebuilt"), bStale);
		if (Shape)
		{
			Entry->SetStringField(TEXT("asset"), Shape->GetPathName()); Entry->SetNumberField(TEXT("contours"), Shape->Contours.Num());
			Entry->SetStringField(TEXT("bounds"), Shape->Bounds.ToString());
			if (Catalog->Shapes.FindRef(D.DefinitionId) != Shape) { Catalog->Modify(); Catalog->Shapes.Add(D.DefinitionId, Shape); bCatalogChanged = true; }
		}
		else { bSuccess = false; Catalog->Shapes.Remove(D.DefinitionId); bCatalogChanged = true; }
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}
	if (bCatalogChanged || Catalog->GetPackage()->IsDirty()) bSuccess &= Save(Catalog);
	Report->SetBoolField(TEXT("success"), bSuccess); Report->SetArrayField(TEXT("entries"), Entries);
	FString Json; const auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Report, Writer); return Json;
}

UGuLiConstructionShape* UGuLiConstructionShapeLibrary::BakeConstructionActor(AActor* Actor, const FString& AssetName)
{
	using namespace GuLiConstructionBake;
	if (!Actor || AssetName.IsEmpty() || !FChar::IsAlpha(AssetName[0]) || AssetName.Contains(TEXT("/")) || AssetName.Contains(TEXT("."))) return nullptr;
	TArray<UMeshComponent*> Components;
	if (Actor->Implements<UGuLiConstructionShapeProvider>())
	{
		if (auto* Override = IGuLiConstructionShapeProvider::Execute_GetConstructionShapeOverride(Actor)) return Override;
		Components = IGuLiConstructionShapeProvider::Execute_GetConstructionFootprintSources(Actor);
	}
	if (Components.IsEmpty())
	{
		TArray<AActor*> Actors; Actor->GetAllChildActors(Actors, true); Actors.AddUnique(Actor);
		for (auto* Child : Actors) { TInlineComponentArray<UMeshComponent*> Meshes(Child); Components.Append(Meshes); }
	}
	const auto* Lifecycle = Actor->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	const FVector Ground = Lifecycle ? Lifecycle->GetGroundLocation() : Actor->GetActorLocation();
	const FTransform GroundFrame(Actor->GetActorQuat(), Ground, Actor->GetActorScale3D());
	TArray<FSource> Sources; TArray<UStaticMesh*> Pending;
	for (auto* Component : Components)
		if (auto* Mesh = Cast<UStaticMeshComponent>(Component); Mesh && Mesh->GetStaticMesh() && Mesh->IsVisible()
			&& !Mesh->ComponentHasTag(TEXT("ConstructionFootprintExclude")) && !Mesh->ComponentHasTag(TEXT("GuLiConstructionProxy")))
		{
			Sources.Add({Mesh->GetStaticMesh(), Mesh->GetComponentTransform().GetRelativeTransform(GroundFrame)}); Pending.AddUnique(Mesh->GetStaticMesh());
		}
	FStaticMeshCompilingManager::Get().FinishCompilation(Pending);
	return Bake(Sources, FString(Root) + AssetName, Signature(Sources));
}
