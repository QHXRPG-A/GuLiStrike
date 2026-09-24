// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Misc/AutomationTest.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCommanderHealthBarActivityTest,
	"GuLiStrike.Commander.UI.HealthBar.Activity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHealthBarActivityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	auto* Source = World->SpawnActor<AGuLiSoldierStateReplicator>();
	auto* Presentation = World->SpawnActor<AGuLiCommanderPresentationActor>();
	auto* Bars = World->SpawnActor<AGuLiCommanderHealthBarRenderer>();
	Bars->DispatchBeginPlay(); Bars->BindStateReplicator(Source); Bars->BindPresentationActor(Presentation);
	FGuLiSoldierStateItem State; State.SoldierId = FGuLiSoldierId(1); State.UnitTypeId = 1;
	State.Health = State.MaxHealth = 100;
	TArray<FGuLiSoldierStateItem> States = {State}; Source->ApplyAuthoritySnapshot(States, 1);
	TestFalse(TEXT("Unselected idle roster leaves health renderer asleep"), Bars->IsActorTickEnabled());
	TestEqual(TEXT("Idle roster allocates no bar capacity"), Bars->GetAllocatedInstanceCount(), 0);
	FGuLiCommanderSelectionState Selection;
	Selection.Cohorts.AddDefaulted_GetRef().MemberIds.Add(State.SoldierId);
	Bars->HandleSelectionChanged(Selection);
	TestTrue(TEXT("Selection wakes renderer immediately"), Bars->IsActorTickEnabled());
	const int32 Slot = Bars->SoldierInstanceIndices.FindChecked(State.SoldierId);
	World->SendAllEndOfFrameUpdates();
	Bars->WriteInstance(Slot, FTransform(FVector(123,456,789)), 1, 1, 1);
	TestFalse(TEXT("Ordinary bar position/color does not dirty full render state"), Bars->HealthBarInstances->IsRenderStateDirty());
	Bars->HandleSelectionChanged({});
	TestFalse(TEXT("Deselecting final idle bar sleeps immediately"), Bars->IsActorTickEnabled());
	FTransform Hidden; Bars->HealthBarInstances->GetInstanceTransform(Slot, Hidden, true);
	TestTrue(TEXT("Released bar slot is explicitly hidden"), Hidden.GetScale3D().IsNearlyZero());
	auto& Soldier = Presentation->PresentedSoldiers.FindOrAdd(State.SoldierId);
	Soldier.HitFlashStartTime = 0;
	States[0].Health = 50; Source->ApplyAuthoritySnapshot(States, 1);
	Presentation->OnVisualStatesChanged.Broadcast({State.SoldierId});
	TestTrue(TEXT("Hit event wakes unselected soldier immediately"), Bars->IsActorTickEnabled());
	TestEqual(TEXT("Hit bar reuses free slot"), Bars->SoldierInstanceIndices.FindChecked(State.SoldierId), Slot);
	TestEqual(TEXT("Damage caches latest health"), Bars->ActiveSoldierStates.FindChecked(State.SoldierId).Health, 50.0f);
	Soldier.HitFlashStartTime = -1000;
	Bars->MaintainActivity();
	TestFalse(TEXT("Expired hit removes final active bar and sleeps"), Bars->IsActorTickEnabled());
	Bars->HandleSelectionChanged(Selection);
	States[0].LifeState = EGuLiSoldierLifeState::Destroyed; States[0].Health = 0;
	Source->ApplyAuthoritySnapshot(States, 1);
	TestTrue(TEXT("Death immediately releases selected bar"), Bars->SoldierInstanceIndices.IsEmpty());
	TestFalse(TEXT("Dead selection does not keep Tick awake"), Bars->IsActorTickEnabled());
	Source->ApplyAuthoritySnapshot({}, 1);
	States[0] = State; States[0].SoldierId = FGuLiSoldierId(2); Source->ApplyAuthoritySnapshot(States, 1);
	Selection.Cohorts[0].MemberIds[0] = States[0].SoldierId; Bars->HandleSelectionChanged(Selection);
	TestEqual(TEXT("New identity reuses hidden capacity"), Bars->GetAllocatedInstanceCount(), 1);
	TestFalse(TEXT("Old identity cannot inherit reused bar"), Bars->SoldierInstanceIndices.Contains(State.SoldierId));
	Bars->BindStateReplicator(nullptr);
	TestFalse(TEXT("Source loss sleeps renderer"), Bars->IsActorTickEnabled());
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHealthBarVisibilityPolicyTest,
	"GuLiStrike.Commander.UI.HealthBar.VisibilityPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHealthBarVisibilityPolicyTest::RunTest(const FString& Parameters)
{
	using FRenderer = AGuLiCommanderHealthBarRenderer;
	constexpr float MaximumDistance = 30000.0f;

	TestTrue(
		TEXT("A selected full-health living Soldier is visible"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 100u, 100u, true, 1000.0f, MaximumDistance));
	TestTrue(
		TEXT("An unselected damaged living Soldier is visible"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 75u, 100u, false, 1000.0f, MaximumDistance));
	TestFalse(
		TEXT("An unselected full-health Soldier is hidden"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 100u, 100u, false, 1000.0f, MaximumDistance));
	TestFalse(
		TEXT("A destroyed selected Soldier is hidden"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			false, 15u, 100u, true, 1000.0f, MaximumDistance));
	TestTrue(
		TEXT("The maximum-distance boundary is inclusive"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 55u, 100u, false, MaximumDistance, MaximumDistance));
	TestFalse(
		TEXT("A Soldier beyond the maximum distance is hidden"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 55u, 100u, false, MaximumDistance + 1.0f, MaximumDistance));
	TestTrue(TEXT("Fractional damage on a sub-unit maximum still displays a health bar"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(true, 0.25f, 0.5f, false, 1000.0f, MaximumDistance));
	TestFalse(TEXT("Invalid float health cannot produce a visible bar"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(true,
			std::numeric_limits<float>::quiet_NaN(), 300.5f, true, 1000.0f, MaximumDistance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHealthBarSizingPolicyTest,
	"GuLiStrike.Commander.UI.HealthBar.SizingPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHealthBarSizingPolicyTest::RunTest(const FString& Parameters)
{
	using FRenderer = AGuLiCommanderHealthBarRenderer;

	TestTrue(
		TEXT("100/100 produces a full health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(100u, 100u), 1.0f));
	TestTrue(
		TEXT("75/100 produces a 0.75 health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(75u, 100u), 0.75f));
	TestTrue(
		TEXT("55/100 produces a 0.55 health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(55u, 100u), 0.55f));
	TestTrue(
		TEXT("15/100 produces a critical 0.15 health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(15u, 100u), 0.15f));
	TestTrue(
		TEXT("Health is clamped to a 1.0 fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(200u, 100u), 1.0f));
	TestTrue(
		TEXT("A zero maximum is handled safely"),
		FMath::IsNearlyZero(FRenderer::TestOnly_CalculateHealthFraction(0u, 0u)));
	TestEqual(TEXT("Float health above the old byte ceiling displays the correct ratio"),
		FRenderer::TestOnly_CalculateHealthFraction(150.25f, 300.5f), 0.5f);
	TestEqual(TEXT("Maximum health below one is not coerced to one"),
		FRenderer::TestOnly_CalculateHealthFraction(0.25f, 0.5f), 0.5f);
	TestEqual(TEXT("A non-finite maximum cannot contaminate material custom data"),
		FRenderer::TestOnly_CalculateHealthFraction(50.0f, std::numeric_limits<float>::infinity()), 0.0f);

	const FVector2D SizeAtOneKilometer = FRenderer::TestOnly_CalculateWorldSizeCentimeters(
		1000.0f,
		90.0f,
		1920,
		1080);
	TestTrue(TEXT("84px width is converted from horizontal FOV"), FMath::IsNearlyEqual(
		SizeAtOneKilometer.X, 87.5f, 0.01f));
	TestTrue(TEXT("12px height is converted from horizontal FOV"), FMath::IsNearlyEqual(
		SizeAtOneKilometer.Y, 12.5f, 0.01f));
	TestTrue(TEXT("The requested 7:1 pixel aspect is retained"), FMath::IsNearlyEqual(
		SizeAtOneKilometer.X / SizeAtOneKilometer.Y, 7.0f, 0.001f));

	const FVector2D SizeAtTwoKilometers = FRenderer::TestOnly_CalculateWorldSizeCentimeters(
		2000.0f,
		90.0f,
		1920,
		1080);
	TestTrue(TEXT("World size grows linearly with distance"), SizeAtTwoKilometers.Equals(
		SizeAtOneKilometer * 2.0f, 0.01f));
	TestTrue(TEXT("An invalid viewport returns zero size"),
		FRenderer::TestOnly_CalculateWorldSizeCentimeters(
			1000.0f, 90.0f, 0, 1080).IsNearlyZero());

	const FBoxSphereBounds SoldierABounds(
		FVector(0.0f, 0.0f, 50.0f),
		FVector(40.0f, 40.0f, 50.0f),
		80.0f);
	const FBoxSphereBounds WM01Bounds(
		FVector(0.0f, 0.0f, 100.0f),
		FVector(120.0f, 120.0f, 150.0f),
		230.0f);
	TestEqual(
		TEXT("Soldier A health bar uses its own mesh top plus padding"),
		FRenderer::TestOnly_CalculateSoldierHeightOffset(SoldierABounds),
		120.0f);
	TestEqual(
		TEXT("WM01 health bar uses its distinct mesh top plus padding"),
		FRenderer::TestOnly_CalculateSoldierHeightOffset(WM01Bounds),
		270.0f);

	return true;
}

#endif
