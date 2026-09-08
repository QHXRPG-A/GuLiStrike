// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "IMeshMergeUtilities.h"
#include "MaterialUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshMerge/MeshMergingSettings.h"
#include "MeshMergeModule.h"
#include "MeshUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "RenderingThread.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshEditorSubsystem.h"
#include "StaticMeshEditorSubsystemHelpers.h"
#include "TimerManager.h"
#include "UObject/Package.h"

namespace GuLiCommanderCrowdAssetCommands
{
	constexpr TCHAR SourceStaticMeshPath[] =
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot.SM_CommanderFourFRobot");
	constexpr TCHAR WM01SourceSkeletalMeshPath[] =
		TEXT("/Game/Assets/WarMachines/CombatAvatarWM-01/WM01_Avatar/SkeletalMeshes/WM01_Avatar.WM01_Avatar");

	struct FCrowdAssetBuildSpec
	{
		const TCHAR* Label;
		const TCHAR* MergeBasePackagePath;
		const TCHAR* MeshPackagePath;
		const TCHAR* MeshObjectPath;
		const TCHAR* MaterialPackagePath;
		const TCHAR* MaterialObjectPath;
		const TCHAR* BaseColorPackagePath;
		const TCHAR* BaseColorObjectPath;
		const TCHAR* NormalPackagePath;
		const TCHAR* NormalObjectPath;
		const TCHAR* OrmPackagePath;
		const TCHAR* OrmObjectPath;
	};

	const FCrowdAssetBuildSpec FourFRobotSpec = {
		TEXT("FourFRobot"),
		TEXT("/Game/Commander/Units/CommanderFourFRobot_Crowd"),
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd"),
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd.SM_CommanderFourFRobot_Crowd"),
		TEXT("/Game/Commander/Units/M_CommanderFourFRobot_Crowd"),
		TEXT("/Game/Commander/Units/M_CommanderFourFRobot_Crowd.M_CommanderFourFRobot_Crowd"),
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_BaseColor"),
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_BaseColor.T_CommanderFourFRobot_Crowd_BaseColor"),
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_Normal"),
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_Normal.T_CommanderFourFRobot_Crowd_Normal"),
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_ORM"),
		TEXT("/Game/Commander/Units/T_CommanderFourFRobot_Crowd_ORM.T_CommanderFourFRobot_Crowd_ORM")};

	const FCrowdAssetBuildSpec WM01Spec = {
		TEXT("WM01"),
		TEXT("/Game/Commander/Units/WM01_Crowd"),
		TEXT("/Game/Commander/Units/SM_WM01_Crowd"),
		TEXT("/Game/Commander/Units/SM_WM01_Crowd.SM_WM01_Crowd"),
		TEXT("/Game/Commander/Units/M_WM01_Crowd"),
		TEXT("/Game/Commander/Units/M_WM01_Crowd.M_WM01_Crowd"),
		TEXT("/Game/Commander/Units/T_WM01_Crowd_BaseColor"),
		TEXT("/Game/Commander/Units/T_WM01_Crowd_BaseColor.T_WM01_Crowd_BaseColor"),
		TEXT("/Game/Commander/Units/T_WM01_Crowd_Normal"),
		TEXT("/Game/Commander/Units/T_WM01_Crowd_Normal.T_WM01_Crowd_Normal"),
		TEXT("/Game/Commander/Units/T_WM01_Crowd_ORM"),
		TEXT("/Game/Commander/Units/T_WM01_Crowd_ORM.T_WM01_Crowd_ORM")};

	constexpr int32 AtlasSize = 2048;
	constexpr int32 LodTriangleBudgets[] = {20000, 6000, 1500};
	constexpr int32 LodTriangleTargets[] = {19000, 5700, 1400};
	constexpr float LodScreenSizes[] = {1.0f, 0.056f, 0.028f};
	constexpr float WM01BattlePoseHipRadians = 0.30f;
	const TCHAR* const WM01BattlePosePositiveHipBones[] = {
		TEXT("Leg_01e"), TEXT("Leg_03e"), TEXT("Leg_05e")};
	const TCHAR* const WM01BattlePoseNegativeHipBones[] = {
		TEXT("Leg_02e"), TEXT("Leg_04e"), TEXT("Leg_06e")};
	bool bCrowdBuildQueuedOrRunning = false;

	struct FBakedTextureSources
	{
		UTexture2D* BaseColor = nullptr;
		UTexture2D* Normal = nullptr;
		UTexture2D* PackedMrs = nullptr;
		UTexture2D* Metallic = nullptr;
		UTexture2D* Roughness = nullptr;
		UTexture2D* AmbientOcclusion = nullptr;
		float MetallicConstant = 0.0f;
		float RoughnessConstant = 0.5f;
		float AmbientOcclusionConstant = 1.0f;
	};

	bool IsOutputAssetPresent(const FCrowdAssetBuildSpec& Spec)
	{
		return LoadObject<UObject>(nullptr, Spec.MeshObjectPath)
			|| LoadObject<UObject>(nullptr, Spec.MaterialObjectPath)
			|| LoadObject<UObject>(nullptr, Spec.BaseColorObjectPath)
			|| LoadObject<UObject>(nullptr, Spec.NormalObjectPath)
			|| LoadObject<UObject>(nullptr, Spec.OrmObjectPath);
	}

	bool ReadTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, FString& OutError)
	{
		if (!Texture)
		{
			OutError = TEXT("Required baked texture is null.");
			return false;
		}

		const int32 SizeX = Texture->Source.GetSizeX();
		const int32 SizeY = Texture->Source.GetSizeY();
		if (SizeX != AtlasSize || SizeY != AtlasSize)
		{
			OutError = FString::Printf(
				TEXT("Baked texture %s is %dx%d; expected %dx%d."),
				*Texture->GetName(),
				SizeX,
				SizeY,
				AtlasSize,
				AtlasSize);
			return false;
		}

		TArray64<uint8> RawData;
		if (!Texture->Source.GetMipData(RawData, 0))
		{
			OutError = FString::Printf(TEXT("Could not read source pixels from %s."), *Texture->GetName());
			return false;
		}

		const int64 PixelCount = static_cast<int64>(SizeX) * SizeY;
		OutPixels.SetNumUninitialized(PixelCount);
		switch (Texture->Source.GetFormat())
		{
		case TSF_BGRA8:
			if (RawData.Num() != PixelCount * sizeof(FColor))
			{
				OutError = FString::Printf(TEXT("Unexpected BGRA8 source size for %s."), *Texture->GetName());
				return false;
			}
			FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), RawData.Num());
			return true;

		case TSF_G8:
			if (RawData.Num() != PixelCount)
			{
				OutError = FString::Printf(TEXT("Unexpected G8 source size for %s."), *Texture->GetName());
				return false;
			}
			for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
			{
				const uint8 Value = RawData[PixelIndex];
				OutPixels[PixelIndex] = FColor(Value, Value, Value, 255);
			}
			return true;

		default:
			OutError = FString::Printf(
				TEXT("Unsupported source format %d for %s."),
				static_cast<int32>(Texture->Source.GetFormat()),
				*Texture->GetName());
			return false;
		}
	}

	void CaptureScalarConstant(
		const UMaterialInstanceConstant* Material,
		const FName ParameterName,
		float& InOutValue)
	{
		if (Material)
		{
			Material->GetScalarParameterValue(ParameterName, InOutValue);
		}
	}

	FBakedTextureSources FindBakedTextureSources(const TArray<UObject*>& GeneratedAssets)
	{
		FBakedTextureSources Result;
		const UMaterialInstanceConstant* FlattenedMaterial = nullptr;

		for (UObject* Asset : GeneratedAssets)
		{
			if (const UMaterialInstanceConstant* Material = Cast<UMaterialInstanceConstant>(Asset))
			{
				FlattenedMaterial = Material;
				continue;
			}

			UTexture2D* Texture = Cast<UTexture2D>(Asset);
			if (!Texture)
			{
				continue;
			}

			const FString Name = Texture->GetName();
			if (Name.Contains(TEXT("BaseColor"), ESearchCase::IgnoreCase)
				|| Name.Contains(TEXT("Diffuse"), ESearchCase::IgnoreCase))
			{
				Result.BaseColor = Texture;
			}
			else if (Name.Contains(TEXT("Normal"), ESearchCase::IgnoreCase))
			{
				Result.Normal = Texture;
			}
			else if (Name.Contains(TEXT("MRS"), ESearchCase::IgnoreCase))
			{
				Result.PackedMrs = Texture;
			}
			else if (Name.Contains(TEXT("AmbientOcclusion"), ESearchCase::IgnoreCase))
			{
				Result.AmbientOcclusion = Texture;
			}
			else if (Name.Contains(TEXT("Metallic"), ESearchCase::IgnoreCase))
			{
				Result.Metallic = Texture;
			}
			else if (Name.Contains(TEXT("Roughness"), ESearchCase::IgnoreCase))
			{
				Result.Roughness = Texture;
			}
		}

		CaptureScalarConstant(FlattenedMaterial, TEXT("MetallicConst"), Result.MetallicConstant);
		CaptureScalarConstant(FlattenedMaterial, TEXT("RoughnessConst"), Result.RoughnessConstant);
		CaptureScalarConstant(
			FlattenedMaterial,
			TEXT("AmbientOcclusionConst"),
			Result.AmbientOcclusionConstant);
		return Result;
	}

	bool BuildFinalTextureSamples(
		const FBakedTextureSources& Sources,
		TArray<FColor>& OutBaseColor,
		TArray<FColor>& OutNormal,
		TArray<FColor>& OutOrm,
		FString& OutError)
	{
		if (!ReadTexturePixels(Sources.BaseColor, OutBaseColor, OutError)
			|| !ReadTexturePixels(Sources.Normal, OutNormal, OutError))
		{
			return false;
		}

		TArray<FColor> PackedMrs;
		TArray<FColor> Metallic;
		TArray<FColor> Roughness;
		TArray<FColor> AmbientOcclusion;
		if (Sources.PackedMrs && !ReadTexturePixels(Sources.PackedMrs, PackedMrs, OutError))
		{
			return false;
		}
		if (!Sources.PackedMrs && Sources.Metallic
			&& !ReadTexturePixels(Sources.Metallic, Metallic, OutError))
		{
			return false;
		}
		if (!Sources.PackedMrs && Sources.Roughness
			&& !ReadTexturePixels(Sources.Roughness, Roughness, OutError))
		{
			return false;
		}
		if (Sources.AmbientOcclusion
			&& !ReadTexturePixels(Sources.AmbientOcclusion, AmbientOcclusion, OutError))
		{
			return false;
		}

		const int32 PixelCount = AtlasSize * AtlasSize;
		OutOrm.SetNumUninitialized(PixelCount);
		const uint8 MetallicConstant = static_cast<uint8>(
			FMath::RoundToInt(FMath::Clamp(Sources.MetallicConstant, 0.0f, 1.0f) * 255.0f));
		const uint8 RoughnessConstant = static_cast<uint8>(
			FMath::RoundToInt(FMath::Clamp(Sources.RoughnessConstant, 0.0f, 1.0f) * 255.0f));
		const uint8 AmbientOcclusionConstant = static_cast<uint8>(
			FMath::RoundToInt(FMath::Clamp(Sources.AmbientOcclusionConstant, 0.0f, 1.0f) * 255.0f));

		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			const uint8 MetallicValue = !PackedMrs.IsEmpty()
				? PackedMrs[PixelIndex].R
				: (!Metallic.IsEmpty() ? Metallic[PixelIndex].R : MetallicConstant);
			const uint8 RoughnessValue = !PackedMrs.IsEmpty()
				? PackedMrs[PixelIndex].G
				: (!Roughness.IsEmpty() ? Roughness[PixelIndex].R : RoughnessConstant);
			const uint8 AmbientOcclusionValue = !AmbientOcclusion.IsEmpty()
				? AmbientOcclusion[PixelIndex].R
				: AmbientOcclusionConstant;
			OutOrm[PixelIndex] = FColor(
				AmbientOcclusionValue,
				RoughnessValue,
				MetallicValue,
				255);
		}
		return true;
	}

	UMaterial* CreateCrowdMaterial(
		UTexture2D* BaseColorTexture,
		UTexture2D* NormalTexture,
		UTexture2D* OrmTexture,
		const FCrowdAssetBuildSpec& Spec)
	{
		UPackage* Package = CreatePackage(Spec.MaterialPackagePath);
		Package->FullyLoad();
		Package->Modify();
		const FString MaterialName = FPackageName::GetLongPackageAssetName(
			Spec.MaterialPackagePath);
		UMaterial* Material = NewObject<UMaterial>(
			Package,
			*MaterialName,
			RF_Public | RF_Standalone);
		Material->BlendMode = BLEND_Opaque;
		Material->TwoSided = false;
		Material->DitheredLODTransition = false;
		Material->bUsedWithInstancedStaticMeshes = true;
		Material->SetShadingModel(MSM_DefaultLit);

		UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
		auto AddTextureSample = [Material](
			UTexture2D* Texture,
			const EMaterialSamplerType SamplerType,
			const int32 EditorY)
		{
			UMaterialExpressionTextureSample* Expression =
				NewObject<UMaterialExpressionTextureSample>(Material);
			Expression->Texture = Texture;
			Expression->SamplerType = SamplerType;
			Expression->MaterialExpressionEditorX = -400;
			Expression->MaterialExpressionEditorY = EditorY;
			Material->GetExpressionCollection().AddExpression(Expression);
			return Expression;
		};

		UMaterialExpressionTextureSample* BaseColorExpression =
			AddTextureSample(BaseColorTexture, SAMPLERTYPE_Color, -180);
		UMaterialExpressionTextureSample* NormalExpression =
			AddTextureSample(NormalTexture, SAMPLERTYPE_Normal, 0);
		UMaterialExpressionTextureSample* OrmExpression =
			AddTextureSample(OrmTexture, SAMPLERTYPE_Masks, 180);

		EditorOnlyData->BaseColor.Expression = BaseColorExpression;
		EditorOnlyData->Normal.Expression = NormalExpression;
		EditorOnlyData->AmbientOcclusion.Expression = OrmExpression;
		EditorOnlyData->AmbientOcclusion.SetMask(1, 1, 0, 0, 0);
		EditorOnlyData->Roughness.Expression = OrmExpression;
		EditorOnlyData->Roughness.SetMask(1, 0, 1, 0, 0);
		EditorOnlyData->Metallic.Expression = OrmExpression;
		EditorOnlyData->Metallic.SetMask(1, 0, 0, 1, 0);

		Material->PostEditChange();
		Material->MarkPackageDirty();
		return Material;
	}

	int32 GetTriangleCount(const UStaticMesh* StaticMesh, const int32 LodIndex)
	{
		const FStaticMeshRenderData* RenderData = StaticMesh ? StaticMesh->GetRenderData() : nullptr;
		return RenderData && RenderData->LODResources.IsValidIndex(LodIndex)
			? RenderData->LODResources[LodIndex].GetNumTriangles()
			: INDEX_NONE;
	}

	bool GenerateContractLods(UStaticMesh* StaticMesh, FString& OutError)
	{
		if (!GEditor || !StaticMesh)
		{
			OutError = TEXT("StaticMesh editor subsystem is unavailable.");
			return false;
		}

		FStaticMeshCompilingManager::Get().FinishCompilation({StaticMesh});
		const int32 SourceTriangleCount = GetTriangleCount(StaticMesh, 0);
		if (SourceTriangleCount <= 0)
		{
			OutError = TEXT("Crowd source mesh has no renderable triangles.");
			return false;
		}

		UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem =
			GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();
		if (!StaticMeshEditorSubsystem)
		{
			OutError = TEXT("Could not acquire UStaticMeshEditorSubsystem.");
			return false;
		}

		float RetryScale = 1.0f;
		for (int32 Attempt = 0; Attempt < 5; ++Attempt)
		{
			FStaticMeshReductionOptions ReductionOptions;
			ReductionOptions.bAutoComputeLODScreenSize = false;
			for (int32 LodIndex = 0; LodIndex < UE_ARRAY_COUNT(LodTriangleTargets); ++LodIndex)
			{
				FStaticMeshReductionSettings& Settings = ReductionOptions.ReductionSettings.AddDefaulted_GetRef();
				Settings.PercentTriangles = FMath::Clamp(
					(static_cast<float>(LodTriangleTargets[LodIndex]) / SourceTriangleCount) * RetryScale,
					0.001f,
					1.0f);
				Settings.ScreenSize = LodScreenSizes[LodIndex];
			}

			if (StaticMeshEditorSubsystem->SetLods(StaticMesh, ReductionOptions) != 3)
			{
				OutError = TEXT("UStaticMeshEditorSubsystem failed to generate exactly three LODs.");
				return false;
			}
			FStaticMeshCompilingManager::Get().FinishCompilation({StaticMesh});

			bool bWithinBudget = true;
			for (int32 LodIndex = 0; LodIndex < UE_ARRAY_COUNT(LodTriangleBudgets); ++LodIndex)
			{
				const int32 TriangleCount = GetTriangleCount(StaticMesh, LodIndex);
				bWithinBudget &= TriangleCount > 0 && TriangleCount <= LodTriangleBudgets[LodIndex];
			}
			if (bWithinBudget)
			{
				return true;
			}
			RetryScale *= 0.75f;
		}

		OutError = FString::Printf(
			TEXT("LOD reducer could not meet triangle budgets: %d/%d/%d."),
			GetTriangleCount(StaticMesh, 0),
			GetTriangleCount(StaticMesh, 1),
			GetTriangleCount(StaticMesh, 2));
		return false;
	}

	void ApplyStaticMeshPerformanceContract(UStaticMesh* StaticMesh, UMaterial* Material)
	{
		StaticMesh->GetStaticMaterials().Reset();
		StaticMesh->GetStaticMaterials().Add(
			FStaticMaterial(Material, TEXT("CommanderCrowd"), TEXT("CommanderCrowd")));
		StaticMesh->bAllowCPUAccess = false;
		StaticMesh->bSupportRayTracing = false;
		StaticMesh->bGenerateMeshDistanceField = false;
		FMeshNaniteSettings NaniteSettings = StaticMesh->GetNaniteSettings();
		NaniteSettings.bEnabled = false;
		StaticMesh->SetNaniteSettings(NaniteSettings);

		for (int32 LodIndex = 0; LodIndex < StaticMesh->GetNumSourceModels(); ++LodIndex)
		{
			FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(LodIndex);
			SourceModel.BuildSettings.DistanceFieldResolutionScale = 0.0f;
			FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(LodIndex, 0);
			SectionInfo.MaterialIndex = 0;
			SectionInfo.bEnableCollision = false;
			// Keep the mesh section shadow-capable. The local Presentation setting owns
			// the runtime default/override; baking this off here would make
			// unit.cast_shadow=true report success without rendering a shadow.
			SectionInfo.bCastShadow = true;
			SectionInfo.bVisibleInRayTracing = false;
			SectionInfo.bAffectDistanceFieldLighting = false;
			StaticMesh->GetSectionInfoMap().Set(LodIndex, 0, SectionInfo);
			StaticMesh->GetOriginalSectionInfoMap().Set(LodIndex, 0, SectionInfo);
		}

		StaticMesh->CreateBodySetup();
		if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
		{
			BodySetup->AggGeom.EmptyElements();
			BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			BodySetup->bNeverNeedsCookedCollisionData = true;
			BodySetup->InvalidatePhysicsData();
		}
		StaticMesh->MarkAsNotHavingNavigationData();

		StaticMesh->PostEditChange();
		FStaticMeshCompilingManager::Get().FinishCompilation({StaticMesh});
		StaticMesh->MarkPackageDirty();
	}

	bool ValidateCrowdAssetContract(
		const FCrowdAssetBuildSpec& Spec,
		FString& OutError)
	{
		UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, Spec.MeshObjectPath);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, Spec.MaterialObjectPath);
		UTexture2D* BaseColor = LoadObject<UTexture2D>(nullptr, Spec.BaseColorObjectPath);
		UTexture2D* Normal = LoadObject<UTexture2D>(nullptr, Spec.NormalObjectPath);
		UTexture2D* Orm = LoadObject<UTexture2D>(nullptr, Spec.OrmObjectPath);
		if (!StaticMesh || !Material || !BaseColor || !Normal || !Orm)
		{
			OutError = FString::Printf(
				TEXT("One or more %s Commander Crowd assets are missing."),
				Spec.Label);
			return false;
		}

		FStaticMeshCompilingManager::Get().FinishCompilation({StaticMesh});
		if (StaticMesh->GetNumSourceModels() != 3 || StaticMesh->GetStaticMaterials().Num() != 1
			|| StaticMesh->GetStaticMaterials()[0].MaterialInterface != Material)
		{
			OutError = TEXT("Crowd mesh must contain exactly three LODs and one material slot.");
			return false;
		}

		const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
		for (int32 LodIndex = 0; LodIndex < UE_ARRAY_COUNT(LodTriangleBudgets); ++LodIndex)
		{
			if (!RenderData || !RenderData->LODResources.IsValidIndex(LodIndex)
				|| RenderData->LODResources[LodIndex].Sections.Num() != 1
				|| GetTriangleCount(StaticMesh, LodIndex) <= 0
				|| GetTriangleCount(StaticMesh, LodIndex) > LodTriangleBudgets[LodIndex])
			{
				OutError = FString::Printf(TEXT("LOD%d violates its one-section triangle contract."), LodIndex);
				return false;
			}
			const FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(LodIndex, 0);
			if (SectionInfo.MaterialIndex != 0 || SectionInfo.bEnableCollision || !SectionInfo.bCastShadow
				|| SectionInfo.bVisibleInRayTracing || SectionInfo.bAffectDistanceFieldLighting
				|| !FMath::IsNearlyZero(
					StaticMesh->GetSourceModel(LodIndex).BuildSettings.DistanceFieldResolutionScale))
			{
				OutError = FString::Printf(TEXT("LOD%d has a forbidden section/build feature enabled."), LodIndex);
				return false;
			}
		}

		if (!FMath::IsNearlyEqual(StaticMesh->GetSourceModel(1).ScreenSize.Default, 0.056f)
			|| !FMath::IsNearlyEqual(StaticMesh->GetSourceModel(2).ScreenSize.Default, 0.028f))
		{
			OutError = TEXT("Crowd mesh LOD ScreenSize values do not match 0.056/0.028.");
			return false;
		}

		if (StaticMesh->GetNaniteSettings().bEnabled || StaticMesh->bSupportRayTracing
			|| StaticMesh->bGenerateMeshDistanceField || StaticMesh->bAllowCPUAccess
			|| StaticMesh->bHasNavigationData)
		{
			OutError = TEXT("Crowd mesh has a forbidden Nanite/RT/DF/CPU-access feature enabled.");
			return false;
		}
		const UBodySetup* BodySetup = StaticMesh->GetBodySetup();
		if (!BodySetup || !BodySetup->bNeverNeedsCookedCollisionData
			|| BodySetup->AggGeom.GetElementCount() != 0
			|| BodySetup->CollisionTraceFlag != CTF_UseSimpleAsComplex)
		{
			OutError = TEXT("Crowd mesh collision data is not fully disabled.");
			return false;
		}

		if (Material->BlendMode != BLEND_Opaque || Material->TwoSided
			|| Material->GetShadingModels() != FMaterialShadingModelField(MSM_DefaultLit)
			|| Material->DitheredLODTransition || !Material->bUsedWithInstancedStaticMeshes
			|| Material->GetEditorOnlyData()->WorldPositionOffset.IsConnected())
		{
			OutError = TEXT("Crowd material violates its lightweight raster contract.");
			return false;
		}

		const UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
		const UMaterialExpressionTextureSample* BaseColorSample =
			Cast<UMaterialExpressionTextureSample>(EditorOnlyData->BaseColor.Expression);
		const UMaterialExpressionTextureSample* NormalSample =
			Cast<UMaterialExpressionTextureSample>(EditorOnlyData->Normal.Expression);
		const UMaterialExpressionTextureSample* OrmSample =
			Cast<UMaterialExpressionTextureSample>(EditorOnlyData->AmbientOcclusion.Expression);
		int32 TextureSampleCount = 0;
		for (const UMaterialExpression* Expression : Material->GetExpressions())
		{
			TextureSampleCount += Cast<UMaterialExpressionTextureSample>(Expression) ? 1 : 0;
		}
		if (TextureSampleCount != 3 || !BaseColorSample || BaseColorSample->Texture != BaseColor
			|| BaseColorSample->SamplerType != SAMPLERTYPE_Color
			|| !NormalSample || NormalSample->Texture != Normal
			|| NormalSample->SamplerType != SAMPLERTYPE_Normal
			|| !OrmSample || OrmSample->Texture != Orm || OrmSample->SamplerType != SAMPLERTYPE_Masks
			|| EditorOnlyData->Roughness.Expression != OrmSample
			|| EditorOnlyData->Metallic.Expression != OrmSample
			|| EditorOnlyData->AmbientOcclusion.MaskR != 1
			|| EditorOnlyData->AmbientOcclusion.MaskG != 0
			|| EditorOnlyData->AmbientOcclusion.MaskB != 0
			|| EditorOnlyData->Roughness.MaskR != 0
			|| EditorOnlyData->Roughness.MaskG != 1
			|| EditorOnlyData->Roughness.MaskB != 0
			|| EditorOnlyData->Metallic.MaskR != 0
			|| EditorOnlyData->Metallic.MaskG != 0
			|| EditorOnlyData->Metallic.MaskB != 1)
		{
			OutError = TEXT("Crowd material graph or ORM channel wiring is invalid.");
			return false;
		}

		if (BaseColor->Source.GetSizeX() != AtlasSize || BaseColor->Source.GetSizeY() != AtlasSize
			|| Normal->Source.GetSizeX() != AtlasSize || Normal->Source.GetSizeY() != AtlasSize
			|| Orm->Source.GetSizeX() != AtlasSize || Orm->Source.GetSizeY() != AtlasSize
			|| !BaseColor->SRGB || Normal->SRGB || Orm->SRGB
			|| Normal->CompressionSettings != TC_Normalmap || Orm->CompressionSettings != TC_Masks)
		{
			OutError = TEXT("Crowd texture resolution, color space, or compression contract failed.");
			return false;
		}
		return true;
	}

	void BuildCrowdFromStaticMeshNow(
		UWorld* World,
		UStaticMesh* SourceStaticMesh,
		const FCrowdAssetBuildSpec& Spec)
	{
		if (!World || !World->IsEditorWorld() || !SourceStaticMesh)
		{
			UE_LOG(
				LogGuLiStrike,
				Error,
				TEXT("%s Crowd build received an invalid editor world or source mesh."),
				Spec.Label);
			return;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("GuLiCommanderCrowdBuildSource");
		SpawnParameters.ObjectFlags = RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* TemporaryActor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (!TemporaryActor)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not spawn temporary Crowd build actor."));
			return;
		}

		UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(
			TemporaryActor,
			TEXT("CrowdSourceMesh"),
			RF_Transient);
		TemporaryActor->SetRootComponent(MeshComponent);
		TemporaryActor->AddInstanceComponent(MeshComponent);
		MeshComponent->SetStaticMesh(SourceStaticMesh);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->RegisterComponentWithWorld(World);
		FlushRenderingCommands();

		FMeshMergingSettings MergeSettings;
		MergeSettings.LODSelectionType = EMeshLODSelectionType::SpecificLOD;
		MergeSettings.SpecificLOD = 0;
		MergeSettings.bMergeMaterials = true;
		MergeSettings.bGenerateLightMapUV = false;
		MergeSettings.bComputedLightMapResolution = false;
		MergeSettings.bPivotPointAtZero = false;
		MergeSettings.bMergePhysicsData = false;
		MergeSettings.bMergeMeshSockets = false;
		MergeSettings.bBakeVertexDataToMesh = false;
		MergeSettings.bUseVertexDataForBakingMaterial = false;
		MergeSettings.bUseTextureBinning = false;
		MergeSettings.bReuseMeshLightmapUVs = false;
		MergeSettings.bMergeEquivalentMaterials = false;
		MergeSettings.bIncludeImposters = false;
		MergeSettings.bSupportRayTracing = false;
		MergeSettings.bAllowDistanceField = false;
		MergeSettings.NaniteSettings.bEnabled = false;
		for (EUVOutput& OutputUv : MergeSettings.OutputUVs)
		{
			OutputUv = EUVOutput::DoNotOutputChannel;
		}
		MergeSettings.OutputUVs[0] = EUVOutput::OutputChannel;

		FMaterialProxySettings& MaterialSettings = MergeSettings.MaterialSettings;
		MaterialSettings.TextureSizingType = TextureSizingType_UseManualOverrideTextureSize;
		MaterialSettings.TextureSize = FIntPoint(AtlasSize, AtlasSize);
		MaterialSettings.DiffuseTextureSize = FIntPoint(AtlasSize, AtlasSize);
		MaterialSettings.NormalTextureSize = FIntPoint(AtlasSize, AtlasSize);
		MaterialSettings.MetallicTextureSize = FIntPoint(AtlasSize, AtlasSize);
		MaterialSettings.RoughnessTextureSize = FIntPoint(AtlasSize, AtlasSize);
		MaterialSettings.AmbientOcclusionTextureSize = FIntPoint(AtlasSize, AtlasSize);
		MaterialSettings.BlendMode = BLEND_Opaque;
		MaterialSettings.bAllowTwoSidedMaterial = false;
		MaterialSettings.bNormalMap = true;
		MaterialSettings.bMetallicMap = true;
		MaterialSettings.bRoughnessMap = true;
		MaterialSettings.bAmbientOcclusionMap = true;
		MaterialSettings.bSpecularMap = false;
		MaterialSettings.bAnisotropyMap = false;
		MaterialSettings.bEmissiveMap = false;
		MaterialSettings.bOpacityMap = false;
		MaterialSettings.bOpacityMaskMap = false;
		MaterialSettings.bTangentMap = false;

		TArray<UPrimitiveComponent*> ComponentsToMerge;
		ComponentsToMerge.Add(MeshComponent);
		TArray<UObject*> GeneratedAssets;
		FVector MergedLocation = FVector::ZeroVector;
		IMeshMergeModule& MeshMergeModule =
			FModuleManager::LoadModuleChecked<IMeshMergeModule>(TEXT("MeshMergeUtilities"));
		MeshMergeModule.GetUtilities().MergeComponentsToStaticMesh(
			ComponentsToMerge,
			World,
			MergeSettings,
			GEngine ? GEngine->DefaultFlattenMaterial : nullptr,
			GetTransientPackage(),
			Spec.MergeBasePackagePath,
			GeneratedAssets,
			MergedLocation,
			1.0f,
			true);
		TemporaryActor->Destroy();

		UStaticMesh* TransientMergedMesh = nullptr;
		for (UObject* Asset : GeneratedAssets)
		{
			if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
			{
				TransientMergedMesh = StaticMesh;
				break;
			}
		}
		if (!TransientMergedMesh)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Mesh merge did not produce a Crowd static mesh."));
			return;
		}

		TArray<FColor> BaseColorSamples;
		TArray<FColor> NormalSamples;
		TArray<FColor> OrmSamples;
		FString Error;
		if (!BuildFinalTextureSamples(
			FindBakedTextureSources(GeneratedAssets),
			BaseColorSamples,
			NormalSamples,
			OrmSamples,
			Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Crowd texture bake failed: %s"), *Error);
			return;
		}

		UPackage* MeshPackage = CreatePackage(Spec.MeshPackagePath);
		MeshPackage->FullyLoad();
		MeshPackage->Modify();
		const FString MeshName = FPackageName::GetLongPackageAssetName(
			Spec.MeshPackagePath);
		UStaticMesh* CrowdMesh = DuplicateObject<UStaticMesh>(
			TransientMergedMesh,
			MeshPackage,
			*MeshName);
		CrowdMesh->SetFlags(RF_Public | RF_Standalone);
		CrowdMesh->ClearFlags(RF_Transient);
		if (!GenerateContractLods(CrowdMesh, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Crowd LOD generation failed: %s"), *Error);
			return;
		}

		UTexture2D* BaseColorTexture = FMaterialUtilities::CreateTexture(
			nullptr,
			Spec.BaseColorPackagePath,
			FIntPoint(AtlasSize, AtlasSize),
			BaseColorSamples,
			TC_Default,
			TEXTUREGROUP_World,
			RF_Public | RF_Standalone,
			true);
		UTexture2D* NormalTexture = FMaterialUtilities::CreateTexture(
			nullptr,
			Spec.NormalPackagePath,
			FIntPoint(AtlasSize, AtlasSize),
			NormalSamples,
			TC_Normalmap,
			TEXTUREGROUP_WorldNormalMap,
			RF_Public | RF_Standalone,
			false);
		UTexture2D* OrmTexture = FMaterialUtilities::CreateTexture(
			nullptr,
			Spec.OrmPackagePath,
			FIntPoint(AtlasSize, AtlasSize),
			OrmSamples,
			TC_Masks,
			TEXTUREGROUP_World,
			RF_Public | RF_Standalone,
			false);
		if (!BaseColorTexture || !NormalTexture || !OrmTexture)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not create final Crowd atlas textures."));
			return;
		}

		UMaterial* CrowdMaterial = CreateCrowdMaterial(
			BaseColorTexture,
			NormalTexture,
			OrmTexture,
			Spec);
		ApplyStaticMeshPerformanceContract(CrowdMesh, CrowdMaterial);

		FAssetRegistryModule::AssetCreated(BaseColorTexture);
		FAssetRegistryModule::AssetCreated(NormalTexture);
		FAssetRegistryModule::AssetCreated(OrmTexture);
		FAssetRegistryModule::AssetCreated(CrowdMaterial);
		FAssetRegistryModule::AssetCreated(CrowdMesh);
		BaseColorTexture->MarkPackageDirty();
		NormalTexture->MarkPackageDirty();
		OrmTexture->MarkPackageDirty();
		CrowdMaterial->MarkPackageDirty();
		CrowdMesh->MarkPackageDirty();
		if (!ValidateCrowdAssetContract(Spec, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Unsaved Crowd asset contract failed: %s"), *Error);
			return;
		}

		TArray<UPackage*> PackagesToSave = {
			BaseColorTexture->GetOutermost(),
			NormalTexture->GetOutermost(),
			OrmTexture->GetOutermost(),
			CrowdMaterial->GetOutermost(),
			CrowdMesh->GetOutermost()};
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Crowd assets were built but one or more packages failed to save."));
			return;
		}

		if (!ValidateCrowdAssetContract(Spec, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Saved Crowd asset contract failed: %s"), *Error);
			return;
		}

		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("Built Commander Crowd asset: %s (tris %d/%d/%d, one material, three 2K textures)."),
			Spec.MeshObjectPath,
			GetTriangleCount(CrowdMesh, 0),
			GetTriangleCount(CrowdMesh, 1),
			GetTriangleCount(CrowdMesh, 2));
	}

	bool CanBuildCrowd(UWorld* World, const FCrowdAssetBuildSpec& Spec)
	{
		if (!World || !World->IsEditorWorld())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander Crowd build requires an editor world."));
			return false;
		}
		if (IsOutputAssetPresent(Spec))
		{
			UE_LOG(
				LogGuLiStrike,
				Error,
				TEXT("%s Commander Crowd output already exists; refusing to overwrite generated assets."),
				Spec.Label);
			return false;
		}
		return true;
	}

	void BuildFourFRobotCrowdNow(UWorld* World)
	{
		if (!CanBuildCrowd(World, FourFRobotSpec))
		{
			return;
		}
		UStaticMesh* SourceStaticMesh = LoadObject<UStaticMesh>(nullptr, SourceStaticMeshPath);
		if (!SourceStaticMesh)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not load source proxy %s."), SourceStaticMeshPath);
			return;
		}
		BuildCrowdFromStaticMeshNow(World, SourceStaticMesh, FourFRobotSpec);
	}

	bool ApplyWM01HipRotations(
		const FReferenceSkeleton& ReferenceSkeleton,
		const TCHAR* const* BoneNames,
		const int32 BoneNameCount,
		const float RotationRadians,
		TArray<FTransform>& InOutLocalTransforms,
		FString& OutError)
	{
		for (int32 BoneNameIndex = 0; BoneNameIndex < BoneNameCount; ++BoneNameIndex)
		{
			const FName BoneName(BoneNames[BoneNameIndex]);
			const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
			if (!InOutLocalTransforms.IsValidIndex(BoneIndex))
			{
				OutError = FString::Printf(
					TEXT("Required WM01 battle-pose hip bone %s is missing."),
					*BoneName.ToString());
				return false;
			}

			FTransform& LocalTransform = InOutLocalTransforms[BoneIndex];
			if (LocalTransform.ContainsNaN())
			{
				OutError = FString::Printf(
					TEXT("Reference transform for WM01 hip bone %s contains non-finite values."),
					*BoneName.ToString());
				return false;
			}

			LocalTransform.NormalizeRotation();
			LocalTransform.ConcatenateRotation(FQuat(FVector::UpVector, RotationRadians));
			LocalTransform.NormalizeRotation();
			if (LocalTransform.ContainsNaN() || !LocalTransform.IsRotationNormalized())
			{
				OutError = FString::Printf(
					TEXT("Computed WM01 battle-pose transform for hip bone %s is invalid."),
					*BoneName.ToString());
				return false;
			}
		}
		return true;
	}

	bool BuildWM01FixedBattlePose(
		const USkeletalMesh* SourceMesh,
		TArray<FTransform>& OutLocalTransforms,
		FString& OutError)
	{
		if (!IsValid(SourceMesh) || SourceMesh->GetLODNum() <= 0)
		{
			OutError = TEXT("WM01 source SkeletalMesh is invalid or has no renderable LOD.");
			return false;
		}

		const FReferenceSkeleton& ReferenceSkeleton = SourceMesh->GetRefSkeleton();
		const TArray<FTransform>& ReferencePose = ReferenceSkeleton.GetRefBonePose();
		if (ReferencePose.IsEmpty() || ReferencePose.Num() != ReferenceSkeleton.GetNum())
		{
			OutError = TEXT("WM01 source SkeletalMesh has an invalid reference pose.");
			return false;
		}

		OutLocalTransforms = ReferencePose;
		if (!ApplyWM01HipRotations(
				ReferenceSkeleton,
				WM01BattlePosePositiveHipBones,
				UE_ARRAY_COUNT(WM01BattlePosePositiveHipBones),
				WM01BattlePoseHipRadians,
				OutLocalTransforms,
				OutError))
		{
			return false;
		}
		return ApplyWM01HipRotations(
			ReferenceSkeleton,
			WM01BattlePoseNegativeHipBones,
			UE_ARRAY_COUNT(WM01BattlePoseNegativeHipBones),
			-WM01BattlePoseHipRadians,
			OutLocalTransforms,
			OutError);
	}

	bool ApplyWM01FixedBattlePose(
		UPoseableMeshComponent* MeshComponent,
		const TArray<FTransform>& LocalTransforms,
		FString& OutError)
	{
		if (!IsValid(MeshComponent)
			|| MeshComponent->BoneSpaceTransforms.Num() != LocalTransforms.Num())
		{
			OutError = TEXT("WM01 PoseableMesh did not initialize the validated reference-pose transforms.");
			return false;
		}

		MeshComponent->BoneSpaceTransforms = LocalTransforms;
		MeshComponent->RefreshBoneTransforms();
		MeshComponent->UpdateComponentToWorld();
		MeshComponent->MarkRenderDynamicDataDirty();
		MeshComponent->MarkRenderStateDirty();
		FlushRenderingCommands();
		return true;
	}

	void BuildWM01CrowdNow(UWorld* World)
	{
		if (!CanBuildCrowd(World, WM01Spec))
		{
			return;
		}
		USkeletalMesh* SourceMesh = LoadObject<USkeletalMesh>(nullptr, WM01SourceSkeletalMeshPath);
		if (!SourceMesh)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not load WM01 source mesh %s."), WM01SourceSkeletalMeshPath);
			return;
		}

		TArray<FTransform> FixedBattlePose;
		FString PoseError;
		if (!BuildWM01FixedBattlePose(SourceMesh, FixedBattlePose, PoseError))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("WM01 Crowd source validation failed: %s"), *PoseError);
			return;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("GuLiWM01CrowdPoseSource");
		SpawnParameters.ObjectFlags = RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* TemporaryActor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (!TemporaryActor)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Could not create the temporary WM01 pose actor."));
			return;
		}

		UPoseableMeshComponent* MeshComponent = NewObject<UPoseableMeshComponent>(
			TemporaryActor,
			TEXT("WM01PoseSourceMesh"),
			RF_Transient);
		TemporaryActor->SetRootComponent(MeshComponent);
		TemporaryActor->AddInstanceComponent(MeshComponent);
		MeshComponent->SetSkinnedAssetAndUpdate(SourceMesh, true);
		MeshComponent->SetVisibility(true);
		MeshComponent->SetHiddenInGame(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->RegisterComponentWithWorld(World);
		if (!ApplyWM01FixedBattlePose(MeshComponent, FixedBattlePose, PoseError))
		{
			TemporaryActor->Destroy();
			UE_LOG(LogGuLiStrike, Error, TEXT("WM01 Crowd pose setup failed: %s"), *PoseError);
			return;
		}

		TArray<UMeshComponent*> MeshComponents;
		MeshComponents.Add(MeshComponent);
		IMeshUtilities& MeshUtilities =
			FModuleManager::LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
		const FString TemporaryPackagePath = FString::Printf(
			TEXT("/Temp/GuLiStrike/SM_WM01_CrowdPose_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		UStaticMesh* PosedStaticMesh = MeshUtilities.ConvertMeshesToStaticMesh(
			MeshComponents,
			MeshComponent->GetComponentTransform(),
			TemporaryPackagePath);
		TemporaryActor->Destroy();
		if (!PosedStaticMesh)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("MeshUtilities failed to freeze the WM01 fixed battle pose."));
			return;
		}
		FStaticMeshCompilingManager::Get().FinishCompilation({PosedStaticMesh});
		UE_LOG(
			LogGuLiStrike,
			Display,
			TEXT("WM01 Crowd pose source: reference pose with Leg_01e/03e/05e Z +%.2f rad and Leg_02e/04e/06e Z -%.2f rad; Mainbody remains neutral."),
			WM01BattlePoseHipRadians,
			WM01BattlePoseHipRadians);
		BuildCrowdFromStaticMeshNow(World, PosedStaticMesh, WM01Spec);
	}

	using FCrowdBuildFunction = void (*)(UWorld*);

	void QueueCrowdBuild(
		const TArray<FString>& Args,
		UWorld* World,
		const TCHAR* Usage,
		FCrowdBuildFunction BuildFunction)
	{
		if (Args.Num() != 0)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Usage: %s"), Usage);
			return;
		}
		if (!World || !World->IsEditorWorld())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander Crowd build requires an editor world."));
			return;
		}
		if (bCrowdBuildQueuedOrRunning)
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander Crowd build is already queued or running."));
			return;
		}
		if (!GEditor)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander Crowd build requires an active editor."));
			return;
		}

		// UnrealMCPython dispatches ExecuteConsoleCommand from a GameThread TaskGraph task.
		// MaterialBaking temporarily pumps the GameThread task queue while waiting for
		// texture streaming, which would trip TaskGraph's recursion guard if the merge
		// ran inline here. The material baker also pumps CoreTicker, so defer through the
		// editor timer manager instead; that callback begins after the console task returns.
		bCrowdBuildQueuedOrRunning = true;
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		GEditor->GetTimerManager()->SetTimerForNextTick(
			FTimerDelegate::CreateLambda(
				[WeakWorld, BuildFunction]()
				{
					if (UWorld* DeferredWorld = WeakWorld.Get();
						DeferredWorld && DeferredWorld->IsEditorWorld())
					{
						BuildFunction(DeferredWorld);
					}
					else
					{
						UE_LOG(
							LogGuLiStrike,
							Error,
							TEXT("Commander Crowd build was cancelled because its editor World expired."));
					}
					bCrowdBuildQueuedOrRunning = false;
				}));
		UE_LOG(LogGuLiStrike, Display, TEXT("Commander Crowd build queued for the next editor tick."));
	}

	void QueueBuildFourFRobotCrowd(const TArray<FString>& Args, UWorld* World)
	{
		QueueCrowdBuild(
			Args,
			World,
			TEXT("gs.Commander.BuildFourFRobotCrowd"),
			&BuildFourFRobotCrowdNow);
	}

	void QueueBuildWM01Crowd(const TArray<FString>& Args, UWorld* World)
	{
		QueueCrowdBuild(
			Args,
			World,
			TEXT("gs.Commander.BuildWM01Crowd"),
			&BuildWM01CrowdNow);
	}

	void ValidateFourFRobotCrowd(const TArray<FString>& Args, UWorld* World)
	{
		(void)World;
		if (Args.Num() != 0)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Usage: gs.Commander.ValidateFourFRobotCrowd"));
			return;
		}

		FString Error;
		if (!ValidateCrowdAssetContract(FourFRobotSpec, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander Crowd validation failed: %s"), *Error);
			return;
		}
		UE_LOG(LogGuLiStrike, Display, TEXT("Commander Crowd asset contract passed."));
	}

	void ValidateWM01Crowd(const TArray<FString>& Args, UWorld* World)
	{
		(void)World;
		if (Args.Num() != 0)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Usage: gs.Commander.ValidateWM01Crowd"));
			return;
		}

		FString Error;
		if (!ValidateCrowdAssetContract(WM01Spec, Error))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("WM01 Commander Crowd validation failed: %s"), *Error);
			return;
		}
		UE_LOG(LogGuLiStrike, Display, TEXT("WM01 Commander Crowd asset contract passed."));
	}

	FAutoConsoleCommandWithWorldAndArgs BuildFourFRobotCrowdCommand(
		TEXT("gs.Commander.BuildFourFRobotCrowd"),
		TEXT("Builds the single-material, three-LOD Commander FourFRobot Crowd asset set."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QueueBuildFourFRobotCrowd));

	FAutoConsoleCommandWithWorldAndArgs ValidateFourFRobotCrowdCommand(
		TEXT("gs.Commander.ValidateFourFRobotCrowd"),
		TEXT("Validates the Commander FourFRobot Crowd mesh, material, textures, and LOD contract."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ValidateFourFRobotCrowd));

	FAutoConsoleCommandWithWorldAndArgs BuildWM01CrowdCommand(
		TEXT("gs.Commander.BuildWM01Crowd"),
		TEXT("Freezes WM01 in a deterministic fixed battle pose and builds its lightweight Crowd asset set."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QueueBuildWM01Crowd));

	FAutoConsoleCommandWithWorldAndArgs ValidateWM01CrowdCommand(
		TEXT("gs.Commander.ValidateWM01Crowd"),
		TEXT("Validates the WM01 Crowd mesh, material, textures, and LOD contract."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ValidateWM01Crowd));
}

#endif // WITH_EDITOR
