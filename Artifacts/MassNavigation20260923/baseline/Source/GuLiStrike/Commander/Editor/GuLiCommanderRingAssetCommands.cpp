// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionSphereMask.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "TimerManager.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace GuLiCommanderRingAssetCommands
{
	constexpr TCHAR MeshPackagePath[] = TEXT("/Game/Commander/Units/SM_CommanderUnitRing");
	constexpr TCHAR MeshObjectPath[] =
		TEXT("/Game/Commander/Units/SM_CommanderUnitRing.SM_CommanderUnitRing");
	constexpr TCHAR MaterialObjectPath[] =
		TEXT("/Game/Commander/UI/M_CommanderUnitRing.M_CommanderUnitRing");
	constexpr TCHAR LegacyMaterialPackagePath[] =
		TEXT("/Game/Commander/QA/M_CommanderUnitRing_LegacyBenchmark");
	constexpr TCHAR LegacyMaterialObjectPath[] =
		TEXT("/Game/Commander/QA/M_CommanderUnitRing_LegacyBenchmark.M_CommanderUnitRing_LegacyBenchmark");
	constexpr int32 SegmentCount = 64;
	constexpr float OuterRadius = 48.0f;
	constexpr float InnerRadius = 34.0f;
	constexpr float EmissiveMultiplier = 3.0f;
	constexpr float OpacityMultiplier = 0.58f;
	const FName MaterialSlotName(TEXT("CommanderUnitRing"));
	bool bBuildQueuedOrRunning = false;

	bool IsLegacyCylinderMaterial(const UMaterial* Material)
	{
		if (!Material || Material->BlendMode != BLEND_Translucent || !Material->TwoSided
			|| !Material->bDisableDepthTest || !Material->bUsedWithInstancedStaticMeshes)
		{
			return false;
		}
		int32 SphereMaskCount = 0;
		for (const UMaterialExpression* Expression : Material->GetExpressions())
		{
			SphereMaskCount += Cast<UMaterialExpressionSphereMask>(Expression) ? 1 : 0;
		}
		return SphereMaskCount >= 2;
	}

	bool PreserveLegacyBenchmark(UMaterial* Material, FString& OutError)
	{
		// Never replace this copy on a repeat build: it is the original full-cylinder
		// shader for an A/B run in one PIE world, with identical instances and camera.
		UObject* ExistingAsset = LoadObject<UObject>(nullptr, LegacyMaterialObjectPath);
		UMaterial* LegacyMaterial = Cast<UMaterial>(ExistingAsset);
		if (ExistingAsset)
		{
			if (!LegacyMaterial
				|| !LegacyMaterial->GetPathName().Equals(LegacyMaterialObjectPath, ESearchCase::IgnoreCase)
				|| !IsLegacyCylinderMaterial(LegacyMaterial))
			{
				OutError = TEXT("The preserved legacy benchmark has an unexpected type, path, or material graph; it was left unchanged.");
				return false;
			}
			if (LegacyMaterial->GetOutermost()->IsDirty())
			{
				OutError = TEXT("The existing legacy benchmark has unsaved changes; refusing to overwrite the immutable baseline.");
				return false;
			}
			return true;
		}
		if (FPackageName::DoesPackageExist(LegacyMaterialPackagePath))
		{
			OutError = TEXT("The legacy benchmark package exists but its expected asset cannot be loaded; refusing to overwrite it.");
			return false;
		}
		if (!IsLegacyCylinderMaterial(Material))
		{
			OutError = TEXT("The original cylinder material is missing; refusing to create a misleading legacy benchmark.");
			return false;
		}
		UPackage* LegacyPackage = CreatePackage(LegacyMaterialPackagePath);
		LegacyPackage->FullyLoad();
		LegacyMaterial = DuplicateObject<UMaterial>(
			Material, LegacyPackage, TEXT("M_CommanderUnitRing_LegacyBenchmark"));
		if (!IsLegacyCylinderMaterial(LegacyMaterial))
		{
			OutError = TEXT("Copying the original cylinder material did not preserve its benchmark graph.");
			return false;
		}
		LegacyMaterial->SetFlags(RF_Public | RF_Standalone);
		LegacyMaterial->ClearFlags(RF_Transient);
		FAssetRegistryModule::AssetCreated(LegacyMaterial);
		LegacyMaterial->MarkPackageDirty();
		if (!UEditorLoadingAndSavingUtils::SavePackages({LegacyPackage}, false))
		{
			OutError = TEXT("Could not save the legacy benchmark; the active ring material was left unchanged.");
			return false;
		}
		return true;
	}

	template <typename TExpression>
	TExpression* AddExpression(UMaterial* Material, const int32 X, const int32 Y)
	{
		TExpression* Expression = NewObject<TExpression>(Material);
		Expression->Material = Material;
		Expression->MaterialExpressionEditorX = X;
		Expression->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(Expression);
		return Expression;
	}

	void RebuildRingMaterial(UMaterial* Material)
	{
		Material->Modify();
		Material->PreEditChange(nullptr);
		for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
		{
			if (FExpressionInput* Input = Material->GetExpressionInputForProperty(
				static_cast<EMaterialProperty>(PropertyIndex)))
			{
				*Input = FExpressionInput();
			}
		}
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			Material->RemoveExpressionParameter(Expression);
			Expression->MarkAsGarbage();
		}
		for (UMaterialExpressionComment* Comment : Material->GetEditorComments())
		{
			Comment->MarkAsGarbage();
		}
		Material->GetExpressionCollection().Empty();
		Material->BlendMode = BLEND_Translucent;
		Material->MaterialDomain = MD_Surface;
		Material->bUseMaterialAttributes = false;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		Material->bDisableDepthTest = true;
		Material->bUsedWithInstancedStaticMeshes = true;
		Material->DitheredLODTransition = false;

		UMaterialExpressionPerInstanceCustomData3Vector* Color =
			AddExpression<UMaterialExpressionPerInstanceCustomData3Vector>(Material, -650, -160);
		Color->DataIndex = 0;
		Color->ConstDefaultValue = FLinearColor::White;
		UMaterialExpressionPerInstanceCustomData* Alpha =
			AddExpression<UMaterialExpressionPerInstanceCustomData>(Material, -650, 140);
		Alpha->DataIndex = 3;
		Alpha->ConstDefaultValue = 1.0f;
		UMaterialExpressionMultiply* Emissive =
			AddExpression<UMaterialExpressionMultiply>(Material, -400, -160);
		Emissive->A.Expression = Color;
		Emissive->ConstB = EmissiveMultiplier;
		UMaterialExpressionMultiply* Opacity =
			AddExpression<UMaterialExpressionMultiply>(Material, -400, 140);
		Opacity->A.Expression = Alpha;
		Opacity->ConstB = OpacityMultiplier;

		// Non-Nanite ISM custom data is vertex-stage data. Interpolate after the
		// multiply so the pixel stage does neither the old disk mask nor color math.
		UMaterialExpressionVertexInterpolator* ColorInterpolator =
			AddExpression<UMaterialExpressionVertexInterpolator>(Material, -160, -160);
		ColorInterpolator->Input.Expression = Emissive;
		UMaterialExpressionVertexInterpolator* AlphaInterpolator =
			AddExpression<UMaterialExpressionVertexInterpolator>(Material, -160, 140);
		AlphaInterpolator->Input.Expression = Opacity;
		Material->GetEditorOnlyData()->EmissiveColor.Expression = ColorInterpolator;
		Material->GetEditorOnlyData()->EmissiveColor.UseConstant = false;
		Material->GetEditorOnlyData()->Opacity.Expression = AlphaInterpolator;
		Material->GetEditorOnlyData()->Opacity.UseConstant = false;
		Material->PostEditChange();
		Material->MarkPackageDirty();
	}

	FMeshDescription MakeRingMeshDescription()
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		auto Positions = Attributes.GetVertexPositions();
		auto Normals = Attributes.GetVertexInstanceNormals();
		auto Tangents = Attributes.GetVertexInstanceTangents();
		auto BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
		auto Colors = Attributes.GetVertexInstanceColors();
		auto UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		const FPolygonGroupID PolygonGroup = Description.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[PolygonGroup] = MaterialSlotName;

		FVertexID OuterVertices[SegmentCount];
		FVertexID InnerVertices[SegmentCount];
		for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
		{
			const float Angle = 2.0f * UE_PI * static_cast<float>(Segment) / SegmentCount;
			const FVector3f Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			OuterVertices[Segment] = Description.CreateVertex();
			InnerVertices[Segment] = Description.CreateVertex();
			Positions[OuterVertices[Segment]] = Direction * OuterRadius;
			Positions[InnerVertices[Segment]] = Direction * InnerRadius;
		}

		auto AddTriangle = [&](const FVertexID A, const FVertexID B, const FVertexID C)
		{
			const FVertexID Vertices[] = {A, B, C};
			TArray<FVertexInstanceID, TInlineAllocator<3>> Instances;
			for (const FVertexID Vertex : Vertices)
			{
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				Normals[Instance] = FVector3f(0.0f, 0.0f, 1.0f);
				Tangents[Instance] = FVector3f(1.0f, 0.0f, 0.0f);
				BinormalSigns[Instance] = 1.0f;
				Colors[Instance] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
				const FVector3f Position = Positions[Vertex];
				UVs.Set(Instance, 0, FVector2f(0.5f + Position.X * 0.01f, 0.5f + Position.Y * 0.01f));
				Instances.Add(Instance);
			}
			Description.CreateTriangle(PolygonGroup, Instances);
		};
		for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
		{
			const int32 Next = (Segment + 1) % SegmentCount;
			// UE mesh normals use Cross(P2-P0, P1-P0) in its left-handed space.
			// This winding is +Z; no lower cap, side wall, or center fill is emitted.
			AddTriangle(OuterVertices[Segment], InnerVertices[Segment], OuterVertices[Next]);
			AddTriangle(OuterVertices[Next], InnerVertices[Segment], InnerVertices[Next]);
		}
		return Description;
	}

	bool RebuildRingMesh(UStaticMesh* Mesh, UMaterial* Material)
	{
		Mesh->Modify();
		Mesh->SetNumSourceModels(1);
		Mesh->GetStaticMaterials().Reset();
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, MaterialSlotName, MaterialSlotName));
		Mesh->bAllowCPUAccess = false;
		Mesh->bSupportRayTracing = false;
		Mesh->bGenerateMeshDistanceField = false;
		FMeshNaniteSettings NaniteSettings = Mesh->GetNaniteSettings();
		NaniteSettings.bEnabled = false;
		Mesh->SetNaniteSettings(NaniteSettings);
		FMeshBuildSettings& Settings = Mesh->GetSourceModel(0).BuildSettings;
		Settings.bRecomputeNormals = false;
		Settings.bRecomputeTangents = false;
		Settings.bBuildReversedIndexBuffer = false;
		Settings.bGenerateLightmapUVs = false;
		Settings.DistanceFieldResolutionScale = 0.0f;
		FMeshSectionInfo SectionInfo(0);
		SectionInfo.bEnableCollision = false;
		SectionInfo.bCastShadow = false;
		SectionInfo.bVisibleInRayTracing = false;
		SectionInfo.bAffectDistanceFieldLighting = false;
		Mesh->GetSectionInfoMap().Clear();
		Mesh->GetOriginalSectionInfoMap().Clear();
		Mesh->GetSectionInfoMap().Set(0, 0, SectionInfo);
		Mesh->GetOriginalSectionInfoMap().Set(0, 0, SectionInfo);
		Mesh->CreateBodySetup();
		if (UBodySetup* BodySetup = Mesh->GetBodySetup())
		{
			BodySetup->AggGeom.EmptyElements();
			BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			BodySetup->bNeverNeedsCookedCollisionData = true;
			BodySetup->InvalidatePhysicsData();
		}
		Mesh->MarkAsNotHavingNavigationData();

		const FMeshDescription Description = MakeRingMeshDescription();
		UStaticMesh::FBuildMeshDescriptionsParams BuildParameters;
		BuildParameters.bUseHashAsGuid = true;
		BuildParameters.bBuildSimpleCollision = false;
		if (!Mesh->BuildFromMeshDescriptions({&Description}, BuildParameters))
		{
			return false;
		}
		// BuildFromMeshDescriptions reconstructs its section-info map and resets
		// RT/DF flags to defaults. Restore the presentation-only contract before
		// the final build so both serialized settings and render sections agree.
		Mesh->GetSectionInfoMap().Set(0, 0, SectionInfo);
		Mesh->GetOriginalSectionInfoMap().Set(0, 0, SectionInfo);
		Mesh->PostEditChange();
		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
		Mesh->MarkPackageDirty();
		return true;
	}

	bool ValidateMaterial(const UMaterial* Material, FString& OutError)
	{
		if (!Material || Material->BlendMode != BLEND_Translucent || Material->TwoSided
			|| !Material->bDisableDepthTest || !Material->bUsedWithInstancedStaticMeshes
			|| !Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit)
			|| Material->DitheredLODTransition || Material->GetExpressions().Num() != 6)
		{
			OutError = TEXT("Ring material must be one-sided Translucent/Unlit, use ISM data, retain no-depth-test, and have six expressions.");
			return false;
		}
		const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		const auto* ColorInterpolator = Cast<UMaterialExpressionVertexInterpolator>(Data->EmissiveColor.Expression);
		const auto* AlphaInterpolator = Cast<UMaterialExpressionVertexInterpolator>(Data->Opacity.Expression);
		const auto* Emissive = ColorInterpolator
			? Cast<UMaterialExpressionMultiply>(ColorInterpolator->Input.Expression) : nullptr;
		const auto* Opacity = AlphaInterpolator
			? Cast<UMaterialExpressionMultiply>(AlphaInterpolator->Input.Expression) : nullptr;
		const auto* Color = Emissive
			? Cast<UMaterialExpressionPerInstanceCustomData3Vector>(Emissive->A.Expression) : nullptr;
		const auto* Alpha = Opacity
			? Cast<UMaterialExpressionPerInstanceCustomData>(Opacity->A.Expression) : nullptr;
		if (!Color || !Alpha || Color->DataIndex != 0 || Alpha->DataIndex != 3
			|| Emissive->B.IsConnected() || Opacity->B.IsConnected()
			|| !FMath::IsNearlyEqual(Emissive->ConstB, EmissiveMultiplier)
			|| !FMath::IsNearlyEqual(Opacity->ConstB, OpacityMultiplier)
			|| Data->WorldPositionOffset.IsConnected() || Data->OpacityMask.IsConnected())
		{
			OutError = TEXT("Ring graph must interpolate vertex-stage CustomData.RGB*3 and CustomData.A*0.58 without a disk mask or WPO.");
			return false;
		}
		return true;
	}

	bool ValidateGeometry(const UStaticMesh* Mesh, FString& OutError)
	{
		const FMeshDescription* Description = Mesh->GetMeshDescription(0);
		if (!Description || Description->Vertices().Num() != SegmentCount * 2
			|| Description->Triangles().Num() != SegmentCount * 2
			|| Description->PolygonGroups().Num() != 1)
		{
			OutError = TEXT("Ring source must have 128 vertices, 128 triangles, and one polygon group.");
			return false;
		}
		const FStaticMeshConstAttributes Attributes(*Description);
		const auto Positions = Attributes.GetVertexPositions();
		int32 InnerCount = 0;
		int32 OuterCount = 0;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[Vertex];
			const float Radius = Position.Size();
			if (!FMath::IsNearlyZero(Position.Z, 0.001f))
			{
				OutError = TEXT("Ring contains a cap or side wall away from Z=0.");
				return false;
			}
			InnerCount += FMath::IsNearlyEqual(Radius, InnerRadius, 0.001f) ? 1 : 0;
			OuterCount += FMath::IsNearlyEqual(Radius, OuterRadius, 0.001f) ? 1 : 0;
		}
		if (InnerCount != SegmentCount || OuterCount != SegmentCount)
		{
			OutError = TEXT("Ring inner/outer radii must be 34/48 cm, with 64 vertices on each boundary.");
			return false;
		}
		double TriangleArea = 0.0;
		for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> Vertices = Description->GetTriangleVertices(Triangle);
			const FVector3f Normal = FVector3f::CrossProduct(
				Positions[Vertices[2]] - Positions[Vertices[0]],
				Positions[Vertices[1]] - Positions[Vertices[0]]);
			if (Normal.Z <= UE_SMALL_NUMBER)
			{
				OutError = TEXT("Ring contains a degenerate or downward-facing triangle.");
				return false;
			}
			TriangleArea += static_cast<double>(Normal.Z) * 0.5;
		}
		const double ExpectedArea = SegmentCount * 0.5
			* FMath::Sin(2.0 * UE_PI / SegmentCount)
			* (OuterRadius * OuterRadius - InnerRadius * InnerRadius);
		if (!FMath::IsNearlyEqual(TriangleArea, ExpectedArea, 0.02))
		{
			OutError = TEXT("Ring area includes missing, duplicated, or center-filling geometry.");
			return false;
		}
		int32 InnerBoundaryEdges = 0;
		int32 OuterBoundaryEdges = 0;
		for (const FEdgeID Edge : Description->Edges().GetElementIDs())
		{
			const int32 AdjacentTriangles = Description->GetNumEdgeConnectedTriangles(Edge);
			if (AdjacentTriangles == 1)
			{
				const TArrayView<const FVertexID> Vertices = Description->GetEdgeVertices(Edge);
				const float RadiusA = Positions[Vertices[0]].Size();
				const float RadiusB = Positions[Vertices[1]].Size();
				InnerBoundaryEdges += FMath::IsNearlyEqual(RadiusA, InnerRadius, 0.001f)
					&& FMath::IsNearlyEqual(RadiusB, InnerRadius, 0.001f) ? 1 : 0;
				OuterBoundaryEdges += FMath::IsNearlyEqual(RadiusA, OuterRadius, 0.001f)
					&& FMath::IsNearlyEqual(RadiusB, OuterRadius, 0.001f) ? 1 : 0;
			}
			else if (AdjacentTriangles != 2)
			{
				OutError = TEXT("Ring contains non-manifold geometry.");
				return false;
			}
		}
		if (InnerBoundaryEdges != SegmentCount || OuterBoundaryEdges != SegmentCount)
		{
			OutError = TEXT("Ring must retain two open 64-edge boundaries, including the actual center hole.");
			return false;
		}
		return true;
	}

	bool ValidateRingAssetContract(FString& OutError)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshObjectPath);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, MaterialObjectPath);
		if (!Mesh || !ValidateMaterial(Material, OutError))
		{
			if (!Mesh)
			{
				OutError = TEXT("Commander ring mesh is missing; run gs.Commander.BuildUnitRing first.");
			}
			return false;
		}
		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
		const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (Mesh->GetNumSourceModels() != 1 || !RenderData || RenderData->LODResources.Num() != 1
			|| RenderData->LODResources[0].Sections.Num() != 1
			|| RenderData->LODResources[0].GetNumTriangles() != SegmentCount * 2
			|| Mesh->GetStaticMaterials().Num() != 1
			|| Mesh->GetStaticMaterials()[0].MaterialInterface != Material)
		{
			OutError = TEXT("Ring render data must have one LOD, one section, 128 triangles, and its one ring material.");
			return false;
		}
		const FMeshSectionInfo Section = Mesh->GetSectionInfoMap().Get(0, 0);
		const UBodySetup* Body = Mesh->GetBodySetup();
		if (Mesh->GetNaniteSettings().bEnabled || Mesh->bAllowCPUAccess || Mesh->bSupportRayTracing
			|| Mesh->bGenerateMeshDistanceField || Mesh->bHasNavigationData
			|| Section.MaterialIndex != 0 || Section.bEnableCollision || Section.bCastShadow
			|| Section.bVisibleInRayTracing || Section.bAffectDistanceFieldLighting
			|| !Body || !Body->bNeverNeedsCookedCollisionData || Body->AggGeom.GetElementCount() != 0
			|| Body->CollisionTraceFlag != CTF_UseSimpleAsComplex)
		{
			OutError = TEXT("Ring has unnecessary collision, shadow, navigation, Nanite, RT, DF, or CPU-access data enabled.");
			return false;
		}
		if (!IsLegacyCylinderMaterial(LoadObject<UMaterial>(nullptr, LegacyMaterialObjectPath)))
		{
			OutError = TEXT("The preserved legacy benchmark material is missing or invalid.");
			return false;
		}
		return ValidateGeometry(Mesh, OutError);
	}

	void BuildUnitRingNow()
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, MaterialObjectPath);
		if (!Material || !Material->GetPathName().Equals(MaterialObjectPath, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Commander ring build stopped: the active material is missing or redirects outside its designated asset path."));
			return;
		}
		UObject* ExistingMeshAsset = LoadObject<UObject>(nullptr, MeshObjectPath);
		UStaticMesh* Mesh = Cast<UStaticMesh>(ExistingMeshAsset);
		if ((ExistingMeshAsset && (!Mesh || !Mesh->GetPathName().Equals(MeshObjectPath, ESearchCase::IgnoreCase)))
			|| (!ExistingMeshAsset && FPackageName::DoesPackageExist(MeshPackagePath)))
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Commander ring build stopped: the mesh output has a type/path conflict or cannot be loaded; no active asset was modified."));
			return;
		}
		FString Error;
		if (!PreserveLegacyBenchmark(Material, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander ring build stopped before modifying active assets: %s"),
				*Error);
			return;
		}

		if (!Mesh)
		{
			UPackage* Package = CreatePackage(MeshPackagePath);
			Package->FullyLoad();
			Mesh = NewObject<UStaticMesh>(Package, TEXT("SM_CommanderUnitRing"), RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Mesh);
		}
		if (!RebuildRingMesh(Mesh, Material))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander ring mesh build failed; active material was left unchanged and no new mesh was saved."));
			return;
		}
		RebuildRingMaterial(Material);
		if (!ValidateRingAssetContract(Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander ring build/validation failed; new assets were not saved: %s"), *Error);
			return;
		}
		if (!UEditorLoadingAndSavingUtils::SavePackages({Material->GetOutermost(), Mesh->GetOutermost()}, false))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not save every Commander ring package; some may have saved. The legacy benchmark remains available."));
			return;
		}
		UE_LOG(LogGuLiStrike, Display,
			TEXT("Built Commander unit ring: 64 segments, 128 triangles, one +Z surface, radii 34/48 cm; CustomData.RGB*3 and A*0.58. Legacy: %s"),
			LegacyMaterialObjectPath);
	}

	void QueueBuildUnitRing(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty() || !World || !World->IsEditorWorld() || !GEditor || GEditor->PlayWorld)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Usage: gs.Commander.BuildUnitRing, in an editor world with PIE stopped."));
			return;
		}
		if (bBuildQueuedOrRunning)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander ring build is already queued or running."));
			return;
		}
		// Defer beyond UnrealMCPython's GameThread task; mesh/material compilation
		// can pump tasks and must not recursively enter the TaskGraph dispatcher.
		bBuildQueuedOrRunning = true;
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* DeferredWorld = WeakWorld.Get();
				DeferredWorld && DeferredWorld->IsEditorWorld() && GEditor && !GEditor->PlayWorld)
			{
				BuildUnitRingNow();
			}
			else
			{
				UE_LOG(LogGuLiStrike, Error, TEXT("Commander ring build cancelled: editor world changed or PIE started."));
			}
			bBuildQueuedOrRunning = false;
		}));
		UE_LOG(LogGuLiStrike, Display, TEXT("Commander ring build queued for the next editor tick."));
	}

	void ValidateUnitRing(const TArray<FString>& Args, UWorld* World)
	{
		(void)World;
		if (!Args.IsEmpty())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Usage: gs.Commander.ValidateUnitRing"));
			return;
		}
		FString Error;
		if (!ValidateRingAssetContract(Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander unit ring validation failed: %s"), *Error);
			return;
		}
		UE_LOG(LogGuLiStrike, Display, TEXT("Commander unit ring asset contract passed (geometry, material, legacy benchmark)."));
	}

	FAutoConsoleCommandWithWorldAndArgs BuildUnitRingCommand(
		TEXT("gs.Commander.BuildUnitRing"),
		TEXT("Preserves the legacy cylinder shader, then builds and saves the Commander annulus mesh and lightweight ring material."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QueueBuildUnitRing));

	FAutoConsoleCommandWithWorldAndArgs ValidateUnitRingCommand(
		TEXT("gs.Commander.ValidateUnitRing"),
		TEXT("Validates the Commander ring's actual hollow geometry, +Z winding, render data, material wiring, and legacy benchmark."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ValidateUnitRing));

#if WITH_DEV_AUTOMATION_TESTS
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FGuLiCommanderRingAssetContractTest,
		"GuLiStrike.Commander.Presentation.RingAsset.Contract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FGuLiCommanderRingAssetContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FString Error;
		if (!ValidateRingAssetContract(Error))
		{
			AddError(Error);
			return false;
		}
		return true;
	}
#endif
}

#endif // WITH_EDITOR
