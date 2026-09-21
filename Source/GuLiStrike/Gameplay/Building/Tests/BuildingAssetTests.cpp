// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "StaticMeshCompiler.h"

namespace GuLiBuildingAssetTests
{
	const TCHAR* MissilePath = TEXT("/Game/Assets/Props/Buildings/Missile_Turret/missile_turret/StaticMeshes/missile_turret.missile_turret");
	const TCHAR* MissileRoot = TEXT("/Game/Assets/Props/Buildings/Missile_Turret/");
	const TCHAR* SentryPath = TEXT("/Game/Assets/Props/Buildings/Stylized_Turrets_Tower_Defense/Stylized_Turrets_A_a.Stylized_Turrets_A_a");
	const TCHAR* SentryMaterialPath = TEXT("/Game/Assets/Props/Buildings/Stylized_Turrets_Tower_Defense/Stylized_Turrets_A_mat.Stylized_Turrets_A_mat");
	const TCHAR* SentryTexturePath = TEXT("/Game/Assets/Props/Buildings/Stylized_Turrets_Tower_Defense/Stylized_Turrets_A.Stylized_Turrets_A");
	const TCHAR* OutpostPath = TEXT("/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder.SM_OutpostPlaceholder");

	bool TestVectorNear(
		FAutomationTestBase& Test,
		const TCHAR* Description,
		const FVector& Actual,
		const FVector& Expected,
		const double Tolerance = 1.0)
	{
		return Test.TestTrue(
			FString::Printf(TEXT("%s: actual=%s expected=%s"), Description, *Actual.ToString(), *Expected.ToString()),
			Actual.Equals(Expected, Tolerance));
	}

	void TestAllLodBuildScales(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		const UStaticMesh& Mesh,
		const FVector& ExpectedScale)
	{
		Test.TestTrue(FString::Printf(TEXT("%s has at least one source LOD"), Label), Mesh.GetNumSourceModels() > 0);
		for (int32 LODIndex = 0; LODIndex < Mesh.GetNumSourceModels(); ++LODIndex)
		{
			TestVectorNear(
				Test,
				*FString::Printf(TEXT("%s Source LOD %d has the baked BuildScale"), Label, LODIndex),
				Mesh.GetSourceModel(LODIndex).BuildSettings.BuildScale3D,
				ExpectedScale,
				0.001);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBuildingAssetTests,
	"GuLiStrike.Building.BuildingAssetTests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingAssetTests::RunTest(const FString& Parameters)
{
	using namespace GuLiBuildingAssetTests;

	UGuLiBuildingCatalog* Catalog = UGuLiBuildingCatalog::LoadDefaultCatalog();
	UStaticMesh* Missile = LoadObject<UStaticMesh>(nullptr, MissilePath);
	UStaticMesh* Sentry = LoadObject<UStaticMesh>(nullptr, SentryPath);
	UStaticMesh* Outpost = LoadObject<UStaticMesh>(nullptr, OutpostPath);
	UMaterialInstanceConstant* SentryMaterial = LoadObject<UMaterialInstanceConstant>(nullptr, SentryMaterialPath);
	UTexture* SentryTexture = LoadObject<UTexture>(nullptr, SentryTexturePath);
	UMaterialInterface* PreviewMaterial = GuLiVfx::Load<UMaterialInterface>(nullptr, GuLiVfxIds::BuildingPlacement);

	if (!TestNotNull(TEXT("Building catalog loads"), Catalog)
		|| !TestNotNull(TEXT("Missile turret target mesh loads"), Missile)
		|| !TestNotNull(TEXT("Sentry turret target mesh loads"), Sentry)
		|| !TestNotNull(TEXT("Outpost target mesh loads"), Outpost)
		|| !TestNotNull(TEXT("Sentry copied material instance loads"), SentryMaterial)
		|| !TestNotNull(TEXT("Sentry copied texture loads"), SentryTexture)
		|| !TestNotNull(TEXT("Shared preview material loads"), PreviewMaterial))
	{
		return false;
	}

	TArray<UStaticMesh*> Meshes { Missile, Sentry, Outpost };
	FStaticMeshCompilingManager::Get().FinishCompilation(Meshes);

	TestTrue(TEXT("The hard-reference catalog is usable"), Catalog->IsUsable());
	TestEqual(TEXT("The current catalog contains seven definitions"), Catalog->Definitions.Num(), 7);
	TestEqual(TEXT("The catalog uses the registered preview material"), Catalog->PreviewVfxId, GuLiVfxIds::BuildingPlacement);

	struct FExpectedDefinition
	{
		EGuLiBuildingType Type;
		UStaticMesh* Mesh;
	};
	const FExpectedDefinition ExpectedDefinitions[] = {
		{EGuLiBuildingType::MissileTurret, Missile},
		{EGuLiBuildingType::SentryTurret, Sentry},
		{EGuLiBuildingType::Outpost, Outpost},
	};
	for (const FExpectedDefinition& Expected : ExpectedDefinitions)
	{
		const FGuLiBuildingDefinition* Definition = Catalog->FindDefinition(Expected.Type);
		if (!TestNotNull(TEXT("Every building type has one catalog definition"), Definition))
		{
			continue;
		}
		TestTrue(TEXT("The definition hard-references its exact target mesh"), Definition->Mesh == Expected.Mesh);
		TestFalse(TEXT("The definition has a visible display name"), Definition->DisplayName.IsEmpty());
		TestVectorNear(
			*this,
			TEXT("Catalog collision matches final baked mesh bounds"),
			Definition->CollisionExtent,
			Expected.Mesh->GetBounds().BoxExtent * Definition->MeshScale,
			1.0);
	}

	TestAllLodBuildScales(*this, TEXT("Missile turret"), *Missile, FVector(12.0f));
	TestAllLodBuildScales(*this, TEXT("Sentry turret"), *Sentry, FVector(12.0f));
	TestAllLodBuildScales(*this, TEXT("Outpost source is not rebaked by scale020"), *Outpost, FVector(1.0f));
	TestVectorNear(*this, TEXT("Missile turret final dimensions"),
		Missile->GetBounds().BoxExtent * 2.0, FVector(2519.46, 2728.75, 3520.77), 12.0);
	TestVectorNear(*this, TEXT("Sentry turret final dimensions"),
		Sentry->GetBounds().BoxExtent * 2.0, FVector(1935.79, 3866.78, 1675.19), 12.0);
	TestVectorNear(*this, TEXT("Outpost final dimensions"),
		Outpost->GetBounds().BoxExtent * 2.0, FVector(7103.086, 6619.805, 30000.0), 1.0);

	TestTrue(TEXT("Missile turret has Nanite enabled"), Missile->GetNaniteSettings().bEnabled);
	TestFalse(TEXT("Sentry turret keeps Nanite disabled"), Sentry->GetNaniteSettings().bEnabled);
	TestEqual(TEXT("Missile turret retains all 32 material slots"), Missile->GetStaticMaterials().Num(), 32);
	for (int32 SlotIndex = 0; SlotIndex < Missile->GetStaticMaterials().Num(); ++SlotIndex)
	{
		const UMaterialInterface* Material = Missile->GetMaterial(SlotIndex);
		TestNotNull(FString::Printf(TEXT("Missile material slot %d is valid"), SlotIndex), Material);
		if (Material)
		{
			TestTrue(
				FString::Printf(TEXT("Missile material slot %d uses the duplicated target tree"), SlotIndex),
				Material->GetPathName().StartsWith(MissileRoot));
		}
	}

	TestTrue(TEXT("Sentry mesh slot zero uses the repaired copied material"), Sentry->GetMaterial(0) == SentryMaterial);
	UTexture* EffectiveDiffuse = nullptr;
	TestTrue(TEXT("Sentry DiffuseColorMap resolves"),
		SentryMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColorMap")), EffectiveDiffuse));
	TestTrue(TEXT("Sentry DiffuseColorMap explicitly uses the copied texture"), EffectiveDiffuse == SentryTexture);

	TestNotNull(TEXT("Outpost has its gray placeholder material"), Outpost->GetMaterial(0));
	TestTrue(TEXT("Outpost material lives in the managed gameplay directory"),
		Outpost->GetMaterial(0)
		&& Outpost->GetMaterial(0)->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Materials/")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
