// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Aiming/GuLiShipReticleTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipWorldHUDReticleTagsAndConfigTest,
	"GuLiStrike.Ship.WorldHUD.ReticleTagsAndConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipWorldHUDReticleTagsAndConfigTest::RunTest(const FString& Parameters)
{
	FGameplayTagContainer Tags;
	bool bConflict = true;
	TestEqual(TEXT("An untagged ability leaves the reticle hidden"),
		GuLiShipReticle::ResolveMode(Tags, &bConflict), EGuLiShipReticleMode::None);
	TestFalse(TEXT("No reticle tags are not a conflict"), bConflict);

	Tags.AddTag(TAG_GuLi_ShipAbility_Reticle_Omni);
	TestEqual(TEXT("The Omni AssetTag selects free aiming"),
		GuLiShipReticle::ResolveMode(Tags, &bConflict), EGuLiShipReticleMode::Omni);
	TestFalse(TEXT("One reticle tag is valid"), bConflict);

	Tags.Reset();
	Tags.AddTag(TAG_GuLi_ShipAbility_Reticle_Bounded);
	TestEqual(TEXT("The Bounded AssetTag selects bounded aiming"),
		GuLiShipReticle::ResolveMode(Tags, &bConflict), EGuLiShipReticleMode::Bounded);
	TestFalse(TEXT("One bounded tag is valid"), bConflict);

	Tags.AddTag(TAG_GuLi_ShipAbility_Reticle_Omni);
	TestEqual(TEXT("Conflicting tags fail closed"),
		GuLiShipReticle::ResolveMode(Tags, &bConflict), EGuLiShipReticleMode::None);
	TestTrue(TEXT("The two native mode tags are mutually exclusive"), bConflict);

	FGuLiShipReticleConfig Config;
	TestTrue(TEXT("The default 0.40 x 0.30 centered rectangle is valid"), Config.IsValid());
	const FBox2D DefaultBounds = Config.GetNormalizedBounds();
	TestTrue(TEXT("The default rectangle minimum is exact"),
		DefaultBounds.Min.Equals(FVector2D(0.3, 0.35), UE_SMALL_NUMBER));
	TestTrue(TEXT("The default rectangle maximum is exact"),
		DefaultBounds.Max.Equals(FVector2D(0.7, 0.65), UE_SMALL_NUMBER));

	Config.BoundsSizeNormalized.X = 0.0;
	TestFalse(TEXT("A zero-width bounded region is rejected"), Config.IsValid());
	Config = FGuLiShipReticleConfig();
	Config.BoundsCenterNormalized = FVector2D(0.95, 0.5);
	TestFalse(TEXT("A region extending beyond the viewport is rejected"), Config.IsValid());
	Config = FGuLiShipReticleConfig();
	Config.BoundsCenterNormalized.X = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Non-finite configuration is rejected"), Config.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipWorldHUDReticleOwnershipTest,
	"GuLiStrike.Ship.WorldHUD.LatestAbilityOwnershipAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipWorldHUDReticleOwnershipTest::RunTest(const FString& Parameters)
{
	FGuLiShipReticleActivationStack Stack;
	FGuLiShipReticleConfig DefaultConfig;
	UObject* FirstAbility = NewObject<UGuLiShipAbilitySet>(GetTransientPackage());
	UObject* SecondAbility = NewObject<UGuLiShipAbilitySet>(GetTransientPackage());

	TestNull(TEXT("The reticle has no default owner and is hidden"), Stack.GetActive());
	TestFalse(TEXT("A None claim cannot activate"),
		Stack.Activate(FirstAbility, EGuLiShipReticleMode::None, DefaultConfig));
	TestTrue(TEXT("The first tagged ability may claim Omni mode"),
		Stack.Activate(FirstAbility, EGuLiShipReticleMode::Omni, DefaultConfig));
	TestEqual(TEXT("The first claim owns Omni mode"),
		Stack.GetActive()->Mode, EGuLiShipReticleMode::Omni);

	FGuLiShipReticleConfig BoundedConfig;
	BoundedConfig.BoundsCenterNormalized = FVector2D(0.55, 0.45);
	BoundedConfig.BoundsSizeNormalized = FVector2D(0.30, 0.20);
	TestTrue(TEXT("A later bounded ability may claim the reticle"),
		Stack.Activate(SecondAbility, EGuLiShipReticleMode::Bounded, BoundedConfig));
	TestEqual(TEXT("The most recently activated valid ability wins"),
		Stack.GetActive()->Owner.Get(), SecondAbility);
	TestEqual(TEXT("The most recent mode is bounded"),
		Stack.GetActive()->Mode, EGuLiShipReticleMode::Bounded);

	TestTrue(TEXT("Ending or cancelling the current owner removes its claim"), Stack.End(SecondAbility));
	TestEqual(TEXT("Ending the latest claim falls back to the prior active ability"),
		Stack.GetActive()->Owner.Get(), FirstAbility);
	TestEqual(TEXT("Fallback restores the prior mode"),
		Stack.GetActive()->Mode, EGuLiShipReticleMode::Omni);
	TestTrue(TEXT("Ending the final claim performs cleanup"), Stack.End(FirstAbility));
	TestNull(TEXT("No surviving tagged ability leaves the reticle hidden"), Stack.GetActive());

	BoundedConfig.BoundsSizeNormalized = FVector2D::ZeroVector;
	TestFalse(TEXT("An invalid bounded claim never enters the ownership stack"),
		Stack.Activate(FirstAbility, EGuLiShipReticleMode::Bounded, BoundedConfig));
	Stack.Reset();
	TestEqual(TEXT("Reset is idempotent"), Stack.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipWorldHUDReticleClampAndProjectionTest,
	"GuLiStrike.Ship.WorldHUD.ReticleClampAndConstantPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipWorldHUDReticleClampAndProjectionTest::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(1920.0, 1080.0);
	const FGuLiShipReticleConfig Config;
	const FBox2D OmniBounds = GuLiShipReticle::BuildUsablePixelBounds(
		EGuLiShipReticleMode::Omni, Config, Viewport, 72.0f);
	TestTrue(TEXT("Omni aiming reserves the safe margin and reticle half-size"),
		OmniBounds.Min.Equals(FVector2D(72.0, 72.0))
		&& OmniBounds.Max.Equals(FVector2D(1848.0, 1008.0)));
	TestTrue(TEXT("Omni movement clamps to its screen-safe region"),
		GuLiShipReticle::ClampPixelPosition(FVector2D(-20.0, 2000.0), OmniBounds)
			.Equals(FVector2D(72.0, 1008.0)));

	const FBox2D BoundedBounds = GuLiShipReticle::BuildUsablePixelBounds(
		EGuLiShipReticleMode::Bounded, Config, Viewport, 60.0f);
	TestTrue(TEXT("Bounded movement stays inside the authored frame with inner padding"),
		BoundedBounds.Min.Equals(FVector2D(636.0, 438.0), 0.01)
		&& BoundedBounds.Max.Equals(FVector2D(1284.0, 642.0), 0.01));
	TestTrue(TEXT("Bounded movement clamps both axes"),
		GuLiShipReticle::ClampPixelPosition(FVector2D(1600.0, 100.0), BoundedBounds)
			.Equals(FVector2D(1284.0, 438.0), 0.01));

	FGuLiShipReticleConfig TinyConfig;
	TinyConfig.BoundsSizeNormalized = FVector2D(0.02, 0.02);
	const FBox2D CollapsedBounds = GuLiShipReticle::BuildUsablePixelBounds(
		EGuLiShipReticleMode::Bounded, TinyConfig, Viewport, 80.0f);
	TestTrue(TEXT("An undersized usable area collapses safely to its center"),
		CollapsedBounds.Min.Equals(CollapsedBounds.Max));

	const float NearScale = GuLiShipReticle::CalculateCentimetersPerPixel(
		1000.0f, 90.0f, 1920, 1080);
	const float FarScale = GuLiShipReticle::CalculateCentimetersPerPixel(
		4000.0f, 90.0f, 1920, 1080);
	TestTrue(TEXT("World scale grows linearly with camera-plane distance"),
		FMath::IsNearlyEqual(FarScale, NearScale * 4.0f, 0.001f));
	TestTrue(TEXT("A 96-pixel node reconstructs to 96 pixels near the camera"),
		FMath::IsNearlyEqual((NearScale * 96.0f) / NearScale, 96.0f));
	TestTrue(TEXT("A 96-pixel node reconstructs to 96 pixels far from the camera"),
		FMath::IsNearlyEqual((FarScale * 96.0f) / FarScale, 96.0f));
	TestTrue(TEXT("Resolution changes retain the requested logical pixel size"),
		FMath::IsNearlyEqual(
			GuLiShipReticle::CalculateCentimetersPerPixel(1000.0f, 90.0f, 1280, 720) * 420.0f
				/ GuLiShipReticle::CalculateCentimetersPerPixel(1000.0f, 90.0f, 1280, 720),
			420.0f));
	TestEqual(TEXT("Invalid projection inputs fail closed"),
		GuLiShipReticle::CalculateCentimetersPerPixel(0.0f, 90.0f, 1920, 1080), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipWorldHUDCameraAlignmentTest,
	"GuLiStrike.Ship.WorldHUD.CameraAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipWorldHUDCameraAlignmentTest::RunTest(const FString& Parameters)
{
	const FQuat BankedShipRotation = FRotator(18.0, 37.0, 42.0).Quaternion();
	const FVector ShipForward = BankedShipRotation.GetForwardVector();
	const FVector ShipUp = BankedShipRotation.GetUpVector();
	const FRotator CameraRotation = GuLiShipReticle::CalculateAlignedCameraRotation(ShipForward, ShipUp);
	TestTrue(TEXT("Aim entry aligns camera forward with Ship forward"),
		FVector::DotProduct(CameraRotation.Vector(), ShipForward) > 0.9999f);
	TestTrue(TEXT("Aim alignment removes visual bank roll"),
		FMath::IsNearlyZero(FRotator::NormalizeAxis(CameraRotation.Roll), 0.01f));

	const FRotator OrbitCameraRotation(67.05, -7.35, 13.0);
	const FQuat OrbitCameraOrientation = OrbitCameraRotation.Quaternion();
	const FQuat WidgetOrientation =
		GuLiShipReticle::CalculateScreenFacingWidgetRotation(OrbitCameraRotation).Quaternion();
	TestTrue(TEXT("World HUD faces the camera across pitch and roll"),
		FVector::DotProduct(
			WidgetOrientation.GetForwardVector(),
			-OrbitCameraOrientation.GetForwardVector()) > 0.9999f);
	TestTrue(TEXT("World HUD keeps its top parallel to screen up"),
		FVector::DotProduct(
			WidgetOrientation.GetUpVector(),
			OrbitCameraOrientation.GetUpVector()) > 0.9999f);
	TestTrue(TEXT("World HUD text direction stays parallel to screen left"),
		FVector::DotProduct(
			WidgetOrientation.GetRightVector(),
			-OrbitCameraOrientation.GetRightVector()) > 0.9999f);
	TestEqual(TEXT("A missing Ship direction fails closed"),
		GuLiShipReticle::CalculateAlignedCameraRotation(FVector::ZeroVector, FVector::UpVector),
		FRotator::ZeroRotator);
	return true;
}

#endif
