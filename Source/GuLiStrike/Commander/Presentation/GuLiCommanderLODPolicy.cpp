#include "Commander/Presentation/GuLiCommanderLODPolicy.h"

EGuLiPresentationTier GuLiPresentationLOD::Resolve(const float Height, const float MeshHeight,
	const float TacticalHeight, const bool bOverview)
{
	if (bOverview) return EGuLiPresentationTier::Overview;
	if (Height >= TacticalHeight) return EGuLiPresentationTier::Tactical;
	if (Height >= MeshHeight && MeshHeight < TacticalHeight) return EGuLiPresentationTier::MeshReduced;
	return EGuLiPresentationTier::Full;
}

EGuLiCommanderLODLevel GuLiPresentationLOD::EffectDetail(const EGuLiPresentationTier Tier)
{
	switch (Tier)
	{
	case EGuLiPresentationTier::Overview: return EGuLiCommanderLODLevel::Minimal;
	case EGuLiPresentationTier::Tactical: return EGuLiCommanderLODLevel::Reduced;
	default: return EGuLiCommanderLODLevel::Full;
	}
}

bool GuLiPresentationLOD::ReduceMesh(const EGuLiPresentationTier Tier)
{
	return Tier != EGuLiPresentationTier::Full;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPresentationTierBoundaryTest,
	"GuLiStrike.Commander.LOD.FourTierBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiPresentationTierBoundaryTest::RunTest(const FString&)
{
	using namespace GuLiPresentationLOD;
	for (const float Height : {35.f, 99.9f})
		TestEqual(TEXT("Full detail below 100 m"), uint8(Resolve(Height,100,300,false)), uint8(0));
	for (const float Height : {100.f, 299.9f})
		TestEqual(TEXT("Mesh cap from 100 m"), uint8(Resolve(Height,100,300,false)), uint8(1));
	for (const float Height : {300.f, 700.f})
		TestEqual(TEXT("Tactical through 700 m"), uint8(Resolve(Height,100,300,false)), uint8(2));
	TestEqual(TEXT("Overview mode overrides height"), uint8(Resolve(700,100,300,true)), uint8(3));
	TestEqual(TEXT("Leaving overview restores height policy"), uint8(Resolve(299.9f,100,300,false)), uint8(1));
	TestEqual(TEXT("Configurable mesh threshold"), uint8(Resolve(100,120,300,false)), uint8(0));
	TestEqual(TEXT("Camera tactical threshold"), uint8(Resolve(300,100,400,false)), uint8(1));
	const EGuLiCommanderLODLevel Expected[] = {EGuLiCommanderLODLevel::Full,EGuLiCommanderLODLevel::Full,
		EGuLiCommanderLODLevel::Reduced,EGuLiCommanderLODLevel::Minimal};
	for (uint8 Index=0; Index<4; ++Index)
	{
		TestEqual(TEXT("Effect resource quality mapping"), uint8(EffectDetail(EGuLiPresentationTier(Index))), uint8(Expected[Index]));
		TestEqual(TEXT("Mesh cap admission"), ReduceMesh(EGuLiPresentationTier(Index)), Index!=0);
	}
	return true;
}
#endif

bool FGuLiCommanderLODSettings::Validate(FString& Error) const
{
	const float Values[] = {NearEnterDistance, NearHoldDistance, NearEnterScreen, NearHoldScreen,
		MidEnterDistance, MidHoldDistance, MidEnterScreen, MidHoldScreen, MinimumResidenceSeconds, MeshReducedStartHeightMeters};
	for (const float Value : Values)
	{
		if (!FMath::IsFinite(Value) || Value < 0)
		{
			Error = TEXT("LOD values must be finite and non-negative.");
			return false;
		}
	}
	if (!(0 < NearEnterDistance && NearEnterDistance < NearHoldDistance
		&& NearHoldDistance < MidEnterDistance && MidEnterDistance < MidHoldDistance
		&& MidHoldDistance <= 10000000.f))
	{
		Error = TEXT("Distances require 0 < near.enter < near.hold < mid.enter < mid.hold <= 10000000 cm.");
		return false;
	}
	if (!(MidHoldScreen < MidEnterScreen && MidEnterScreen < NearHoldScreen
		&& NearHoldScreen < NearEnterScreen && NearEnterScreen <= 1.f))
	{
		Error = TEXT("Screen thresholds require 0 <= mid.hold < mid.enter < near.hold < near.enter <= 1.");
		return false;
	}
	if (MinimumResidenceSeconds > 10.f)
	{
		Error = TEXT("minimum_residence_seconds must be in [0, 10].");
		return false;
	}
	if (MeshReducedStartHeightMeters <= 0 || MeshReducedStartHeightMeters > 100000.f)
	{
		Error = TEXT("mesh_reduced.start_height_m must be in (0, 100000].");
		return false;
	}
	Error.Reset();
	return true;
}

EGuLiCommanderLODLevel FGuLiCommanderLODSettings::Classify(const double Distance, const float Screen,
	const TOptional<EGuLiCommanderLODLevel> CurrentLevel) const
{
	const bool bWasFull = CurrentLevel.IsSet() && CurrentLevel.GetValue() == EGuLiCommanderLODLevel::Full;
	const bool bWasDetailed = CurrentLevel.IsSet() && CurrentLevel.GetValue() != EGuLiCommanderLODLevel::Minimal;
	if (Distance < (bWasFull ? NearHoldDistance : NearEnterDistance)
		&& Screen > (bWasFull ? NearHoldScreen : NearEnterScreen)) return EGuLiCommanderLODLevel::Full;
	if (Distance < (bWasDetailed ? MidHoldDistance : MidEnterDistance)
		&& Screen > (bWasDetailed ? MidHoldScreen : MidEnterScreen)) return EGuLiCommanderLODLevel::Reduced;
	return EGuLiCommanderLODLevel::Minimal;
}
