// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Misc/AutomationTest.h"
#include "Commander/UI/GuLiHealthBarLayout.h"
#include "Commander/UI/GuLiGroundSceneUIState.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Components/SceneComponent.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
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
	FGuLiSoldierStateItem State;
	State.SoldierId = FGuLiSoldierId(1);
	State.Health = State.MaxHealth = 100;
	auto Apply = [&] { Source->ApplyAuthoritySnapshot({&State, 1}, 1); };
	auto Event = [&] { return Source->GetActiveVitalChanges().Find(State.SoldierId); };
	Apply();
	TestNull(TEXT("Initial roster creates no bar"), Event());
	State.ActiveOrderId = 3; Apply();
	TestNull(TEXT("Orders create no bar"), Event());

	State.Health = 75; Apply();
	uint64 FirstGeneration = 0;
	if (const auto* Hit = Event())
	{
		FirstGeneration = Hit->Generation;
		const auto Bar = GuLiHealthBars::FromEvent(Source, State.SoldierId.Value, *Hit, World->GetTimeSeconds());
		TestEqual(TEXT("Confirmed damage reaches Slate as a 75% bar"), Bar.Fraction, .75f);
		TestEqual(TEXT("New event is fully visible"), Bar.Opacity, 1.f);
	}
	else AddError(TEXT("Confirmed damage failed to create a bar"));

	World->TimeSeconds += 2.;
	State.Health = 100; Apply();
	if (const auto* Heal = Event())
	{
		TestEqual(TEXT("Healing to full refreshes time"), Heal->LastChange, World->GetTimeSeconds());
		TestEqual(TEXT("Updates within a visible event retain its identity"), Heal->Generation, FirstGeneration);
	}
	else AddError(TEXT("Full healing failed to create a bar"));

	World->TimeSeconds += 4.;
	++State.StateRevision; Apply();
	TestNull(TEXT("Unchanged vitals expire and leave the active directory"), Event());
	State.MaxHealth = 150; Apply();
	if (const auto* Capacity = Event()) TestTrue(TEXT("A new event after expiry has a fresh key"), Capacity->Generation != FirstGeneration);
	else AddError(TEXT("A capacity change failed to create a bar"));
	State.Health = 0; State.LifeState = EGuLiSoldierLifeState::Destroyed; Apply();
	TestNull(TEXT("Death removes a bar immediately"), Event());
	Source->ApplyAuthoritySnapshot({}, 1);
	State.Health = 60; State.LifeState = EGuLiSoldierLifeState::Alive; Apply();
	TestNull(TEXT("A partially depleted replacement starts with a baseline"), Event());
	State.Health = 55; Apply();
	TestNotNull(TEXT("The replacement may generate its own bar"), Event());
	Source->ApplyAuthoritySnapshot({&State, 1}, 2);
	TestNull(TEXT("Match epoch resets the old event"), Event());
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHealthBarVisibilityPolicyTest,
	"GuLiStrike.Commander.UI.HealthBar.VisibilityPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHealthBarVisibilityPolicyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	auto* Unit = World->SpawnActor<AActor>();
	auto* Health = NewObject<UGuLiCombatHealthComponent>(Unit);
	Unit->AddInstanceComponent(Health);
	Health->RegisterComponent();
	Unit->DispatchBeginPlay();
	auto* Feedback = World->GetSubsystem<UGuLiUnitFeedbackSubsystem>();
	if (TestNotNull(TEXT("Rendered world has a feedback subsystem"), Feedback))
	{
		FGuLiTargetHandle Target;
		Target.Kind = EGuLiTargetKind::GroundActor;
		Target.AuthorityId = FGuid::NewGuid(); Target.LocalId = Target.Generation = 1;
		FGuLiCombatHealthState State;
		State.Health = 80; State.MaxHealth = 100; State.Shield = 40; State.MaxShield = 50;
		uint64 Revision = 0;
		auto Apply = [&]()
		{
			State.Revision = ++Revision;
			return Health->ApplyHybridHealth(Target, EGuLiTeam::Red, State, 1, Revision, false);
		};
		TestTrue(TEXT("Initial partially depleted health snapshot applies"), Apply());
		Feedback->FlashActor(Unit);
		TestFalse(TEXT("Initial snapshot and visual-only flash do not create a bar"), Feedback->GetActorHealthBars().Contains(Unit));
		State.Shield = 30;
		TestTrue(TEXT("Shield damage applies"), Apply());
		if (const auto* Bar = Feedback->GetActorHealthBars().Find(Unit))
		{
			TestEqual(TEXT("Shield-only hit leaves hull at 80%"), Bar->HealthFraction, .8f);
			TestEqual(TEXT("Shield-only hit displays remaining shield"), Bar->ShieldFraction, .6f);
			const double HitTime = Bar->LastChange;
			World->TimeSeconds += 1.;
			Apply();
			Feedback->FlashActor(Unit);
			TestEqual(TEXT("New revision and flash cannot refresh an unchanged bar"), Feedback->GetActorHealthBars().FindChecked(Unit).LastChange, HitTime);
			State.Shield = 50; Apply();
			TestEqual(TEXT("Full recharge refreshes time"), Feedback->GetActorHealthBars().FindChecked(Unit).LastChange, World->GetTimeSeconds());
			World->TimeSeconds += 1.;
			State.Health = 100; Apply();
			TestEqual(TEXT("Full healing refreshes time"), Feedback->GetActorHealthBars().FindChecked(Unit).LastChange, World->GetTimeSeconds());
			World->TimeSeconds += 1.;
			State.MaxShield = 60; Apply();
			TestEqual(TEXT("Shield capacity refreshes time"), Feedback->GetActorHealthBars().FindChecked(Unit).LastChange, World->GetTimeSeconds());
			TestTrue(TEXT("Healing window holds then fades"), GetDefault<UGuLiUnitFeedbackSettings>()->HealthBarOpacity(3., 0.) == 1.f && FMath::IsNearlyEqual(GetDefault<UGuLiUnitFeedbackSettings>()->HealthBarOpacity(3.25, 0.), .5f));
			++Target.Generation; Apply();
			TestFalse(TEXT("Replacement life cannot inherit a bar"), Feedback->GetActorHealthBars().Contains(Unit));
			State.Health = 50; Apply();
			TestTrue(TEXT("New life can generate its own event"), Feedback->GetActorHealthBars().Contains(Unit));
			State.bDead = true; State.Health = 0; State.Shield = 0; Apply();
			TestFalse(TEXT("Death removes a bar before the next publication"), Feedback->GetActorHealthBars().Contains(Unit));
		}
		else AddError(TEXT("The confirmed shield change did not reach the bar renderer"));
	}
	Unit->Destroy();
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHealthBarSizingPolicyTest,
	"GuLiStrike.Commander.UI.HealthBar.SizingPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHealthBarSizingPolicyTest::RunTest(const FString& Parameters)
{
	FGuLiSceneUIHealthBar Bar;
	const FVector2D Anchor(550, 400);
	auto Bounds = [&](float DPI, bool Overview) { return FGuLiHealthBarLayout::ScreenBounds(Bar, Anchor, DPI, Overview); };
	TestEqual(TEXT("Ordinary health bar is centered on the projected anchor"), Bounds(1, false).GetCenter(), Anchor);
	TestEqual(TEXT("Health body and border are 86 by 14 pixels"), Bounds(1, false).GetSize(), FVector2D(86, 14));
	Bar.bHasShield = true;
	TestEqual(TEXT("Shield extends the rectangle downward"), Bounds(1, false).GetSize(), FVector2D(86, 18));
	Bar.OverviewIconPixels = 30;
	TestEqual(TEXT("Overview bar follows the icon width"), Bounds(1, true).GetSize(), FVector2D(32, 8));
	TestEqual(TEXT("Overview minimum width and shield height follow DPI"), Bounds(2, true).GetSize(), FVector2D(48, 16));
	TestTrue(TEXT("Overview bar begins below the icon"), Bounds(2, true).Min.Y > Anchor.Y + 15.);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSceneUIPositionLifetimeTest,
    "GuLiStrike.Commander.UI.SceneUI.PositionSamplingLifetimes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSceneUIPositionLifetimeTest::RunTest(const FString&)
{
    auto* Source=NewObject<USceneComponent>();FGuLiSceneUIPositionCache Cache;
    FGuLiSceneUIElementKey Key{Source,7,1};FVector Actual(1),Out;uint32 Reads=0;
    auto Read=[&](FVector& P){++Reads;P=Actual;return true;};
    auto Sample=[&](double T,uint64 Revision,bool Selected)
    {Cache.Begin();const bool Good=Cache.Resolve(Key,Revision,Selected,T,Read,Out);Cache.End();return Good;};
    TestTrue(TEXT("New identity is available immediately"),Sample(0,1,false));
    Actual=FVector(2);Sample(.05,1,false);
    TestEqual(TEXT("Ordinary motion holds within 100ms"),Out,FVector(1));
    Sample(.101,1,false);TestEqual(TEXT("Ordinary position updates after 100ms"),Out,Actual);
    Actual=FVector(3);Sample(.11,2,false);TestEqual(TEXT("Damage/teleport revision bypasses interval"),Out,Actual);
    Actual=FVector(4);Sample(.12,2,true);TestEqual(TEXT("Selection takes current pose"),Out,Actual);
    Actual=FVector(5);Sample(.13,2,true);TestEqual(TEXT("Selected pose advances every frame"),Out,Actual);
    Actual=FVector(6);Sample(.14,2,false);TestEqual(TEXT("Deselection cannot expose the old sampled pose"),Out,Actual);
    ++Key.Generation;Actual=FVector(7);Sample(.15,2,false);
    TestEqual(TEXT("Reused slot has a new lifetime and fresh pose"),Out,Actual);
    TestEqual(TEXT("Old lifetime was removed"),Cache.Samples.Num(),1);
    Cache.Begin();Cache.End();TestEqual(TEXT("Death/removal clears unseen entries"),Cache.Samples.Num(),0);
    TestEqual(TEXT("Only the held ordinary sample avoided a source read"),Reads,7u);
    Sample(.16,2,false);Cache.Reset();TestEqual(TEXT("Source/world rebind clears entries"),Cache.Samples.Num(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHealthBarScreenLayoutTest,
	"GuLiStrike.Commander.UI.HealthBar.AllVisibleAndCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiHealthBarScreenLayoutTest::RunTest(const FString&)
{
	FGuLiHealthBarLayout Layout;
	TArray<FGuLiSceneUIHealthBar> Bars;
	for (int32 I = 0; I < 96; ++I)
	{
		auto& Bar = Bars.AddDefaulted_GetRef();
		Bar.Key = {GetTransientPackage(), uint64(I + 1), 1};
		Bar.Center = FVector(400, 300, 0);
		Bar.Fraction = Bar.ShieldFraction = 1;
		Bar.bHasShield = true;
	}
	const FBox2D Screen(FVector2D::ZeroVector, FVector2D(1920, 1080));
	auto Select = [&](FGuLiHealthBarLayout& View, const FBox2D& Rect)
	{
		View.ProjectVisible(Bars, Rect, [&](int32 Index, FBox2D& Bounds, float& Opacity)
		{
			const auto& B = Bars[Index];
			const FVector2D P(B.Center.X, B.Center.Y);
			Bounds = FBox2D(P, P + FVector2D(86, B.bHasShield ? 18 : 14));
			Opacity = B.Opacity;
			return true;
		});
	};
	auto Has = [&](const FGuLiHealthBarLayout& View, int32 Index)
	{
		return View.Visible.ContainsByPredicate([Index](const auto& Item) { return Item.Index == Index; });
	};
	Layout.Publish(Bars, 0);
	Select(Layout, Screen);
	TestEqual(TEXT("All 96 identical shielded rectangles are visible"), Layout.Visible.Num(), 96);
	TestEqual(TEXT("Each eligible bar was projected once"), Layout.Projected, 96);
	Bars[46].bSelected = true;
	Bars[47].Fraction = .6f;
	Bars[47].ShieldFraction = .4f;
	Layout.Publish(Bars, .05);
	TestTrue(TEXT("Sub-tick health and shield retain published values"),
		Bars[47].Fraction == 1 && Bars[47].ShieldFraction == 1);
	Bars[47].Fraction = .6f; Bars[47].ShieldFraction = .4f;
	Layout.Publish(Bars, .101);
	Select(Layout, Screen);
	TestEqual(TEXT("Selected, damaged and recovering neighbours can share a rectangle"), Layout.Visible.Num(), 96);
	TestTrue(TEXT("Next 100 ms boundary publishes new values"), Bars[47].Fraction == .6f && Bars[47].ShieldFraction == .4f);
	Bars[47].Center.X = -86;
	Layout.Publish(Bars, .14); Select(Layout, Screen);
	TestFalse(TEXT("Entirely off-screen leader consumes no quota"), Has(Layout,47));
	Bars[47].Center.X += 1;
	Layout.Publish(Bars, .15); Select(Layout, Screen);
	TestTrue(TEXT("A one-pixel visible edge participates"), Has(Layout,47));
	FGuLiHealthBarLayout Split;
	Split.Publish(Bars, .15); Select(Split, FBox2D(FVector2D(900,0), FVector2D(1920,540)));
	TestFalse(TEXT("A second local viewport owns a separate layout"), Has(Split,47));
	Bars.SetNum(47); Layout.Publish(Bars,.16); Select(Layout,Screen);
	TestTrue(TEXT("Removed life leaves before the next publication"), Layout.Visible.Num()>0 && !Has(Layout,47));
	Bars[46].Key.Generation++; Layout.Publish(Bars,.17); Select(Layout,Screen);
	TestFalse(TEXT("Recycled key cannot inherit the preceding life"), Has(Layout,46));
	for (int32 Frame=11; Frame<=60; ++Frame) Layout.Publish(Bars, Frame/60.);
	TestEqual(TEXT("One second at 60 FPS spans ten 100 ms intervals"), Layout.Publications, uint64(11));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHealthBarVitalHistoryTest,
	"GuLiStrike.Commander.UI.HealthBar.ShieldChangeHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiHealthBarVitalHistoryTest::RunTest(const FString&)
{
	FGuLiUnitVitalHistory History;
	FGuLiCombatHealthState State;
	State.Health = State.MaxHealth = 100;
	State.Shield = State.MaxShield = 50;
	History.Observe(State, EGuLiTeam::Red, true, 1.);
	TestEqual(TEXT("First contact establishes a baseline"), History.LastChange, -1.);
	State.Shield = 30; History.Observe(State, EGuLiTeam::Red, true, 2.);
	TestEqual(TEXT("Shield-only loss opens the display window"), History.LastChange, 2.);
	History.Observe(State, EGuLiTeam::Red, true, 3.);
	TestEqual(TEXT("Identical values do not extend the window"), History.LastChange, 2.);
	State.Shield = 40; History.Observe(State, EGuLiTeam::Red, true, 4.);
	TestEqual(TEXT("Recovery extends the window"), History.LastChange, 4.);
	History.Observe(State, EGuLiTeam::Blue, true, 5.);
	TestEqual(TEXT("Changing affiliation starts a fresh baseline"), History.LastChange, -1.);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiHealthBarPoseCadenceTest,
	"GuLiStrike.Commander.UI.HealthBar.Pose30HzCameraPerFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiHealthBarPoseCadenceTest::RunTest(const FString&)
{
	FGuLiHealthBarLayout View;
	FGuLiHealthBarPoseCache Supplied;
	TArray<FGuLiSceneUIHealthBar> Bars;
	auto& Bar = Bars.AddDefaulted_GetRef();
	Bar.Key = {GetTransientPackage(), 7, 1};
	const FBox2D Screen(FVector2D::ZeroVector, FVector2D(1920,1080));
	int32 Reads = 0, CameraOnlyFrames = 0;
	double PreviousX = 0;
	bool bCameraMovedOnEveryHold = true, bPosesAgree = true, bNewSamplesCurrent = true;
	auto At = [&](double Now, uint32 Displacement = 0)
	{
		View.Poses.BeginFrame(Now);
		View.Poses.Resolve(Bar, [&](FVector& Center)
		{
			++Reads;
			Center = FVector(200 + 4800 * Now, 0, 0);
			return true;
		}, Bar.Center, Displacement);
		View.Poses.EndFrame();
	};
	for (int32 Frame = 0; Frame <= 120; ++Frame)
	{
		const double Now = Frame / 120.0;
		const int32 OldReads = Reads;
		At(Now);
		auto Raw = Bars;
		Raw[0].Center = FVector(200 + 4800 * Now, 0, 0);
		Supplied.BeginFrame(Now);
		Supplied.ApplySupplied(Raw);
		Supplied.EndFrame();
		bPosesAgree &= Raw[0].Center == Bar.Center;
		if (Reads != OldReads) bNewSamplesCurrent &= Bar.Center.Equals(FVector(200 + 4800 * Now, 0, 0));
		View.Publish(Bars, Now);
		// The moving camera still changes the rectangle between model samples.
		View.ProjectVisible(Bars, Screen, [&](int32, FBox2D& Bounds, float& Opacity)
		{
			const FVector2D P(100 + Bar.Center.X * .05 + Frame, 100);
			Bounds = FBox2D(P, P + FVector2D(86,14)); Opacity = 1;
			return true;
		});
		if (View.Visible.Num() != 1) { AddError(TEXT("Expected one live bar")); return false; }
		const double X = View.Visible[0].Bounds.Min.X;
		if (Reads == OldReads)
		{
			++CameraOnlyFrames;
			bCameraMovedOnEveryHold &= FMath::IsNearlyEqual(X, PreviousX + 1.);
		}
		PreviousX = X;
	}
	TestEqual(TEXT("One initial world anchor plus 30 samples per game second"), Reads, 31);
	TestEqual(TEXT("One initial information publication plus ten updates"), View.Publications, uint64(11));
	TestEqual(TEXT("Ninety intervening frames reuse the model anchor"), CameraOnlyFrames, 90);
	TestTrue(TEXT("Camera motion projects held world anchors each frame"), bCameraMovedOnEveryHold);
	TestTrue(TEXT("All three sources share the same 30 Hz cadence"), bPosesAgree && bNewSamplesCurrent);

	View.Poses.BeginFrame(1.001); View.Poses.EndFrame(); // Removed between poses.
	At(1.005);
	TestEqual(TEXT("Returning to view acquires a fresh pose immediately"), Reads, 32);
	++Bar.Target.LifeGeneration;
	At(1.008);
	TestEqual(TEXT("A replacement life cannot borrow an old position"), Reads, 33);
	At(1.012, 3);
	TestEqual(TEXT("A soldier displacement takes effect before the next sample"), Reads, 34);
	const uint64 BeforeHitch = View.Poses.GetSampleCount();
	At(2.25, 3);
	TestEqual(TEXT("A long frame only consumes the latest sample"), View.Poses.GetSampleCount(), BeforeHitch + 1);
	At(.25, 3); View.Publish(Bars, .25);
	TestEqual(TEXT("A time rewind restarts both independent clocks"), View.Poses.GetSampleCount(), uint64(1));
	TestEqual(TEXT("Rewind publishes fresh information"), View.Publications, uint64(1));
	return true;
}

#endif
