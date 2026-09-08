// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshCompiler.h"

namespace GuLiCommanderCrowdAssetTests
{
	constexpr TCHAR MeshPath[] =
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd.SM_CommanderFourFRobot_Crowd");
	constexpr TCHAR MaterialPath[] =
		TEXT("/Game/Commander/Units/M_CommanderFourFRobot_Crowd.M_CommanderFourFRobot_Crowd");
	constexpr TCHAR BaseColorPath[] =
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_BaseColor.T_CommanderFourFRobot_Crowd_BaseColor");
	constexpr TCHAR NormalPath[] =
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_Normal.T_CommanderFourFRobot_Crowd_Normal");
	constexpr TCHAR OrmPath[] =
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_ORM.T_CommanderFourFRobot_Crowd_ORM");

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FGuLiCommanderCrowdMeshContractTest,
		"GuLiStrike.Commander.Presentation.CrowdAsset.MeshContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FGuLiCommanderCrowdMeshContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		if (!TestNotNull(TEXT("Crowd static mesh exists"), Mesh))
		{
			AddError(TEXT("Run gs.Commander.BuildFourFRobotCrowd in an editor world before this asset contract test."));
			return false;
		}

		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
		TestEqual(TEXT("Crowd mesh has three source LODs"), Mesh->GetNumSourceModels(), 3);
		TestEqual(TEXT("Crowd mesh has one material slot"), Mesh->GetStaticMaterials().Num(), 1);
		TestFalse(TEXT("Nanite is disabled"), Mesh->GetNaniteSettings().bEnabled);
		TestFalse(TEXT("Ray tracing support is disabled"), Mesh->bSupportRayTracing);
		TestFalse(TEXT("Distance field generation is disabled"), Mesh->bGenerateMeshDistanceField);
		TestFalse(TEXT("CPU access is disabled"), Mesh->bAllowCPUAccess);
		TestFalse(TEXT("Navigation collision data is disabled"), Mesh->bHasNavigationData);
		TestTrue(
			TEXT("LOD1 ScreenSize is 0.056"),
			FMath::IsNearlyEqual(Mesh->GetSourceModel(1).ScreenSize.Default, 0.056f));
		TestTrue(
			TEXT("LOD2 ScreenSize is 0.028"),
			FMath::IsNearlyEqual(Mesh->GetSourceModel(2).ScreenSize.Default, 0.028f));

		const int32 TriangleBudgets[] = {20000, 6000, 1500};
		const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (!TestNotNull(TEXT("Crowd mesh has render data"), RenderData))
		{
			return false;
		}
		for (int32 LodIndex = 0; LodIndex < UE_ARRAY_COUNT(TriangleBudgets); ++LodIndex)
		{
			TestTrue(
				FString::Printf(TEXT("LOD%d distance-field resolution scale is zero"), LodIndex),
				FMath::IsNearlyZero(
					Mesh->GetSourceModel(LodIndex).BuildSettings.DistanceFieldResolutionScale));
			if (!TestTrue(
				FString::Printf(TEXT("LOD%d render data exists"), LodIndex),
				RenderData->LODResources.IsValidIndex(LodIndex)))
			{
				continue;
			}
			const FStaticMeshLODResources& Lod = RenderData->LODResources[LodIndex];
			TestEqual(
				FString::Printf(TEXT("LOD%d has one section"), LodIndex),
				Lod.Sections.Num(),
				1);
			TestTrue(
				FString::Printf(TEXT("LOD%d has triangles"), LodIndex),
				Lod.GetNumTriangles() > 0);
			TestTrue(
				FString::Printf(TEXT("LOD%d meets triangle budget"), LodIndex),
				static_cast<int32>(Lod.GetNumTriangles()) <= TriangleBudgets[LodIndex]);

			const FMeshSectionInfo Section = Mesh->GetSectionInfoMap().Get(LodIndex, 0);
			TestFalse(FString::Printf(TEXT("LOD%d collision is disabled"), LodIndex), Section.bEnableCollision);
			TestTrue(
				FString::Printf(TEXT("LOD%d remains shadow-capable for the local runtime override"), LodIndex),
				Section.bCastShadow);
			TestFalse(
				FString::Printf(TEXT("LOD%d ray-tracing visibility is disabled"), LodIndex),
				Section.bVisibleInRayTracing);
			TestFalse(
				FString::Printf(TEXT("LOD%d distance-field lighting is disabled"), LodIndex),
				Section.bAffectDistanceFieldLighting);
		}

		const UBodySetup* BodySetup = Mesh->GetBodySetup();
		if (TestNotNull(TEXT("Crowd mesh has an explicit no-collision BodySetup"), BodySetup))
		{
			TestTrue(TEXT("Collision data is never cooked"), BodySetup->bNeverNeedsCookedCollisionData);
			TestEqual(
				TEXT("No simple collision primitives exist"),
				BodySetup->AggGeom.GetElementCount(),
				0);
			TestEqual(
				TEXT("No complex collision is cooked"),
				BodySetup->CollisionTraceFlag,
				CTF_UseSimpleAsComplex);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FGuLiCommanderCrowdMaterialContractTest,
		"GuLiStrike.Commander.Presentation.CrowdAsset.MaterialContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FGuLiCommanderCrowdMaterialContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UMaterial* Material = LoadObject<UMaterial>(nullptr, MaterialPath);
		UTexture2D* BaseColor = LoadObject<UTexture2D>(nullptr, BaseColorPath);
		UTexture2D* Normal = LoadObject<UTexture2D>(nullptr, NormalPath);
		UTexture2D* Orm = LoadObject<UTexture2D>(nullptr, OrmPath);
		if (!TestNotNull(TEXT("Crowd material exists"), Material)
			|| !TestNotNull(TEXT("Crowd BaseColor exists"), BaseColor)
			|| !TestNotNull(TEXT("Crowd Normal exists"), Normal)
			|| !TestNotNull(TEXT("Crowd ORM exists"), Orm))
		{
			AddError(TEXT("Run gs.Commander.BuildFourFRobotCrowd before this asset contract test."));
			return false;
		}

		TestEqual(TEXT("Material is opaque"), Material->BlendMode, BLEND_Opaque);
		TestFalse(TEXT("Material is one-sided"), Material->TwoSided);
		TestFalse(TEXT("Material has no dithered LOD transition"), Material->DitheredLODTransition);
		TestTrue(TEXT("Material is compiled for ISM"), Material->bUsedWithInstancedStaticMeshes);
		TestTrue(
			TEXT("Material is Default Lit"),
			Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit));
		TestFalse(
			TEXT("Material has no WPO"),
			Material->GetEditorOnlyData()->WorldPositionOffset.IsConnected());

		const UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
		const UMaterialExpressionTextureSample* BaseColorSample =
			Cast<UMaterialExpressionTextureSample>(EditorOnlyData->BaseColor.Expression);
		const UMaterialExpressionTextureSample* NormalSample =
			Cast<UMaterialExpressionTextureSample>(EditorOnlyData->Normal.Expression);
		const UMaterialExpressionTextureSample* OrmSample =
			Cast<UMaterialExpressionTextureSample>(EditorOnlyData->AmbientOcclusion.Expression);
		if (TestNotNull(TEXT("BaseColor is driven by a texture sample"), BaseColorSample))
		{
			TestTrue(TEXT("BaseColor sample uses BaseColor atlas"), BaseColorSample->Texture == BaseColor);
			TestEqual(TEXT("BaseColor sample type is Color"), BaseColorSample->SamplerType, SAMPLERTYPE_Color);
		}
		if (TestNotNull(TEXT("Normal is driven by a texture sample"), NormalSample))
		{
			TestTrue(TEXT("Normal sample uses Normal atlas"), NormalSample->Texture == Normal);
			TestEqual(TEXT("Normal sample type is Normal"), NormalSample->SamplerType, SAMPLERTYPE_Normal);
		}
		if (TestNotNull(TEXT("AO is driven by the ORM texture sample"), OrmSample))
		{
			TestTrue(TEXT("ORM sample uses ORM atlas"), OrmSample->Texture == Orm);
			TestEqual(TEXT("ORM sample type is Masks"), OrmSample->SamplerType, SAMPLERTYPE_Masks);
			TestTrue(
				TEXT("Roughness shares the ORM sample"),
				EditorOnlyData->Roughness.Expression == OrmSample);
			TestTrue(
				TEXT("Metallic shares the ORM sample"),
				EditorOnlyData->Metallic.Expression == OrmSample);
			TestTrue(
				TEXT("AO reads ORM.R"),
				EditorOnlyData->AmbientOcclusion.Mask == 1
					&& EditorOnlyData->AmbientOcclusion.MaskR == 1
					&& EditorOnlyData->AmbientOcclusion.MaskG == 0
					&& EditorOnlyData->AmbientOcclusion.MaskB == 0);
			TestTrue(
				TEXT("Roughness reads ORM.G"),
				EditorOnlyData->Roughness.Mask == 1
					&& EditorOnlyData->Roughness.MaskR == 0
					&& EditorOnlyData->Roughness.MaskG == 1
					&& EditorOnlyData->Roughness.MaskB == 0);
			TestTrue(
				TEXT("Metallic reads ORM.B"),
				EditorOnlyData->Metallic.Mask == 1
					&& EditorOnlyData->Metallic.MaskR == 0
					&& EditorOnlyData->Metallic.MaskG == 0
					&& EditorOnlyData->Metallic.MaskB == 1);
		}

		TSet<const UTexture2D*> UsedTextures;
		int32 TextureSampleCount = 0;
		for (const UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (const UMaterialExpressionTextureSample* TextureSample =
				Cast<UMaterialExpressionTextureSample>(Expression))
			{
				++TextureSampleCount;
				if (const UTexture2D* Texture = Cast<UTexture2D>(TextureSample->Texture))
				{
					UsedTextures.Add(Texture);
				}
			}
		}
		TestEqual(TEXT("Material has exactly three texture sample nodes"), TextureSampleCount, 3);
		TestEqual(TEXT("Material uses exactly three textures"), UsedTextures.Num(), 3);
		TestTrue(TEXT("Material uses BaseColor"), UsedTextures.Contains(BaseColor));
		TestTrue(TEXT("Material uses Normal"), UsedTextures.Contains(Normal));
		TestTrue(TEXT("Material uses ORM"), UsedTextures.Contains(Orm));

		auto TestTextureSize = [this](const TCHAR* Label, const UTexture2D* Texture)
		{
			TestEqual(FString::Printf(TEXT("%s width"), Label), Texture->Source.GetSizeX(), 2048);
			TestEqual(FString::Printf(TEXT("%s height"), Label), Texture->Source.GetSizeY(), 2048);
		};
		TestTextureSize(TEXT("BaseColor"), BaseColor);
		TestTextureSize(TEXT("Normal"), Normal);
		TestTextureSize(TEXT("ORM"), Orm);
		TestTrue(TEXT("BaseColor is sRGB"), BaseColor->SRGB);
		TestFalse(TEXT("Normal is linear"), Normal->SRGB);
		TestFalse(TEXT("ORM is linear"), Orm->SRGB);
		TestEqual(TEXT("Normal compression"), Normal->CompressionSettings, TC_Normalmap);
		TestEqual(TEXT("ORM mask compression"), Orm->CompressionSettings, TC_Masks);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FGuLiCommanderWM01CrowdAssetContractTest,
		"GuLiStrike.Commander.Presentation.CrowdAsset.WM01Contract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FGuLiCommanderWM01CrowdAssetContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		constexpr TCHAR WM01MeshPath[] =
			TEXT("/Game/Commander/Units/SM_WM01_Crowd.SM_WM01_Crowd");
		constexpr TCHAR WM01MaterialPath[] =
			TEXT("/Game/Commander/Units/M_WM01_Crowd.M_WM01_Crowd");
		constexpr TCHAR WM01BaseColorPath[] =
			TEXT("/Game/Commander/Units/T_WM01_Crowd_BaseColor.T_WM01_Crowd_BaseColor");
		constexpr TCHAR WM01NormalPath[] =
			TEXT("/Game/Commander/Units/T_WM01_Crowd_Normal.T_WM01_Crowd_Normal");
		constexpr TCHAR WM01OrmPath[] =
			TEXT("/Game/Commander/Units/T_WM01_Crowd_ORM.T_WM01_Crowd_ORM");

		UStaticMesh* WM01Mesh = LoadObject<UStaticMesh>(nullptr, WM01MeshPath);
		UMaterial* WM01Material = LoadObject<UMaterial>(nullptr, WM01MaterialPath);
		UTexture2D* WM01BaseColor = LoadObject<UTexture2D>(nullptr, WM01BaseColorPath);
		UTexture2D* WM01Normal = LoadObject<UTexture2D>(nullptr, WM01NormalPath);
		UTexture2D* WM01Orm = LoadObject<UTexture2D>(nullptr, WM01OrmPath);
		if (!TestNotNull(TEXT("WM01 Crowd mesh exists"), WM01Mesh)
			|| !TestNotNull(TEXT("WM01 Crowd material exists"), WM01Material)
			|| !TestNotNull(TEXT("WM01 Crowd BaseColor exists"), WM01BaseColor)
			|| !TestNotNull(TEXT("WM01 Crowd Normal exists"), WM01Normal)
			|| !TestNotNull(TEXT("WM01 Crowd ORM exists"), WM01Orm))
		{
			AddError(TEXT("Run gs.Commander.BuildWM01Crowd before this focused contract test."));
			return false;
		}

		FStaticMeshCompilingManager::Get().FinishCompilation({WM01Mesh});
		TestEqual(TEXT("WM01 has exactly three source LODs"),
			WM01Mesh->GetNumSourceModels(), 3);
		TestEqual(TEXT("WM01 has one material slot"),
			WM01Mesh->GetStaticMaterials().Num(), 1);
		TestTrue(TEXT("WM01 mesh uses its baked material"),
			WM01Mesh->GetStaticMaterials().Num() == 1
				&& WM01Mesh->GetStaticMaterials()[0].MaterialInterface == WM01Material);
		TestFalse(TEXT("WM01 Nanite is disabled"), WM01Mesh->GetNaniteSettings().bEnabled);
		TestFalse(TEXT("WM01 ray tracing support is disabled"), WM01Mesh->bSupportRayTracing);
		TestFalse(TEXT("WM01 distance fields are disabled"), WM01Mesh->bGenerateMeshDistanceField);
		TestFalse(TEXT("WM01 CPU access is disabled"), WM01Mesh->bAllowCPUAccess);
		TestFalse(TEXT("WM01 navigation data is disabled"), WM01Mesh->bHasNavigationData);

		const int32 TriangleBudgets[] = {20000, 6000, 1500};
		const FStaticMeshRenderData* RenderData = WM01Mesh->GetRenderData();
		if (!TestNotNull(TEXT("WM01 has render data"), RenderData)) return false;
		for (int32 LodIndex = 0; LodIndex < UE_ARRAY_COUNT(TriangleBudgets); ++LodIndex)
		{
			if (!TestTrue(FString::Printf(TEXT("WM01 LOD%d exists"), LodIndex),
				RenderData->LODResources.IsValidIndex(LodIndex)))
			{
				continue;
			}
			const FStaticMeshLODResources& Lod = RenderData->LODResources[LodIndex];
			TestEqual(FString::Printf(TEXT("WM01 LOD%d has one section"), LodIndex),
				Lod.Sections.Num(), 1);
			TestTrue(FString::Printf(TEXT("WM01 LOD%d has triangles"), LodIndex),
				Lod.GetNumTriangles() > 0);
			TestTrue(FString::Printf(TEXT("WM01 LOD%d meets its budget"), LodIndex),
				static_cast<int32>(Lod.GetNumTriangles()) <= TriangleBudgets[LodIndex]);
			const FMeshSectionInfo Section = WM01Mesh->GetSectionInfoMap().Get(LodIndex, 0);
			TestFalse(FString::Printf(TEXT("WM01 LOD%d collision is disabled"), LodIndex),
				Section.bEnableCollision);
			TestFalse(FString::Printf(TEXT("WM01 LOD%d ray tracing is disabled"), LodIndex),
				Section.bVisibleInRayTracing);
			TestTrue(FString::Printf(TEXT("WM01 LOD%d has zero distance-field scale"), LodIndex),
				FMath::IsNearlyZero(
					WM01Mesh->GetSourceModel(LodIndex).BuildSettings.DistanceFieldResolutionScale));
		}
		TestTrue(TEXT("WM01 LOD1 ScreenSize is 0.056"),
			FMath::IsNearlyEqual(WM01Mesh->GetSourceModel(1).ScreenSize.Default, 0.056f));
		TestTrue(TEXT("WM01 LOD2 ScreenSize is 0.028"),
			FMath::IsNearlyEqual(WM01Mesh->GetSourceModel(2).ScreenSize.Default, 0.028f));

		TestEqual(TEXT("WM01 material is opaque"), WM01Material->BlendMode, BLEND_Opaque);
		TestFalse(TEXT("WM01 material is one-sided"), WM01Material->TwoSided);
		TestTrue(TEXT("WM01 material supports ISM"), WM01Material->bUsedWithInstancedStaticMeshes);
		TestFalse(TEXT("WM01 material has no WPO"),
			WM01Material->GetEditorOnlyData()->WorldPositionOffset.IsConnected());
		TestEqual(TEXT("WM01 BaseColor width"), WM01BaseColor->Source.GetSizeX(), int64{2048});
		TestEqual(TEXT("WM01 BaseColor height"), WM01BaseColor->Source.GetSizeY(), int64{2048});
		TestEqual(TEXT("WM01 Normal width"), WM01Normal->Source.GetSizeX(), int64{2048});
		TestEqual(TEXT("WM01 Normal height"), WM01Normal->Source.GetSizeY(), int64{2048});
		TestEqual(TEXT("WM01 ORM width"), WM01Orm->Source.GetSizeX(), int64{2048});
		TestEqual(TEXT("WM01 ORM height"), WM01Orm->Source.GetSizeY(), int64{2048});
		TestTrue(TEXT("WM01 BaseColor is sRGB"), WM01BaseColor->SRGB);
		TestFalse(TEXT("WM01 Normal is linear"), WM01Normal->SRGB);
		TestFalse(TEXT("WM01 ORM is linear"), WM01Orm->SRGB);
		TestEqual(TEXT("WM01 Normal compression"), WM01Normal->CompressionSettings, TC_Normalmap);
		TestEqual(TEXT("WM01 ORM compression"), WM01Orm->CompressionSettings, TC_Masks);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
