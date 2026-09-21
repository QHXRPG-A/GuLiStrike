#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Network/GuLiBattleTypes.h"
#include "Commander/Network/GuLiCommanderPoseCodec.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020ModelContract,
	"GuLiStrike.Scale020.ModelTransformAndMounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiScale020ModelContract::RunTest(const FString& Parameters)
{
	FGuLiSoldierDefinition Definition;
	Definition.Model = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Reference mesh"), Definition.Model.Get())) return false;
	const FTransform Logical(FRotator(0, 37, 0), FVector(125000, -240000, 12650));
	const FTransform Model = Definition.MakeModelTransform(Logical);
	TestTrue(TEXT("World anchor never moves"), Model.GetLocation().Equals(Logical.GetLocation()));
	TestTrue(TEXT("Resolved effective scale is 0.2"), Model.GetScale3D().Equals(FVector(.2), 1.e-6));
	TestTrue(TEXT("Repeated initialization assigns, never compounds"),
		Definition.MakeModelTransform(Model).Equals(Model));
	const FBox SourceBounds = Definition.Model->GetBoundingBox();
	TestTrue(TEXT("Effective bounds are twenty percent of unmodified source"),
		Definition.GetModelBoundsCentimeters().GetSize().Equals(SourceBounds.GetSize() * .2, 1.e-4));
	const FVector LocalMuzzle(-260,1257,2440);
	TestTrue(TEXT("Source-local muzzle resolves once to actual centimeters"),
		Logical.TransformPosition(Definition.ResolveModelOffsetCentimeters(LocalMuzzle))
		.Equals(Model.TransformPosition(LocalMuzzle),1.e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020EffectContract,
	"GuLiStrike.Scale020.EffectRadiusAndTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiScale020EffectContract::RunTest(const FString& Parameters)
{
	const auto* Field = GetDefault<UGuLiSpellFieldDefinition>();
	const auto* Warning = GetDefault<UGuLiGroundWarningStyle>();
	const auto* Projectile = GetDefault<UGuLiProjectileEffectDefinition>();
	TestEqual(TEXT("Gameplay radius is 1.6 m"), Field->Radius,160.0f);
	TestEqual(TEXT("Art reference is deliberately NOT migrated"),Field->VisualReferenceRadius,800.0f);
	TestEqual(TEXT("Visual ratio does not cancel the migration"),Field->Radius/Field->VisualReferenceRadius,.2f);
	TestEqual(TEXT("World warning depth"),Warning->ProjectionDepth,80.0f);
	TestEqual(TEXT("Warning cycle is still 0.8 s"),Warning->WavePeriod,.8f);
	TestEqual(TEXT("Missile speed is final cm/s"),Projectile->Motion.Speed,1200.0f);
	TestEqual(TEXT("Lift duration unchanged"),Projectile->Motion.LiftSeconds,.25f);
	TestTrue(TEXT("Missile base scale comes from the VFX registry"), GuLiVfx::Scale(nullptr, GuLiVfxIds::MissileFlight).Equals(FVector(.4), 1.e-6));
	TestEqual(TEXT("Resource layout migration is versioned"),GULI_RESOURCE_LAYOUT_VERSION,2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020NetworkContract,
	"GuLiStrike.Scale020.NetworkResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiScale020NetworkContract::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Clients predating VfxId must not share this format"), GULI_BATTLE_PROTOCOL_VERSION >= 18u);
	TestEqual(TEXT("XY quantum"),GULI_POSE_XY_STEP_CENTIMETERS,20.0);
	TestEqual(TEXT("Z quantum"),GULI_POSE_Z_STEP_CENTIMETERS,2.0);
	TestEqual(TEXT("Velocity quantum"),GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND,20.0);
	TestEqual(TEXT("Simulation/capture stays at 10 Hz"),GULI_POSE_CAPTURE_RATE_HZ,uint32(10));
	return true;
}
#endif
