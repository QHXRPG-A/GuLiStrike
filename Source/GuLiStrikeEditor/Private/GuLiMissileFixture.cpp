// Explicit player-invoked acceptance scene helpers; never run during editor authoring.
#include "GuLiRogueCardQALibrary.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Camera/CameraActor.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FFixture { TArray<FGuLiSoldierId> Units; TWeakObjectPtr<APlayerController> Owner; int32 Count = 1; };
	TMap<TWeakObjectPtr<UWorld>, FFixture> Fixtures;
	bool IsMissileFixtureSession(APlayerController* PC)
	{
		return PC && PC->HasAuthority() && PC->IsLocalController() && PC->GetWorld()->IsPlayInEditor()
			&& PC->GetWorld()->GetMapName().Contains(TEXT("LVL_CommanderMassPrototype"));
	}
	FString MissileFixtureFailure(const FString& Message)
	{
		FString Result; auto W=TJsonWriterFactory<>::Create(&Result);
		W->WriteObjectStart(); W->WriteValue(TEXT("success"),false); W->WriteValue(TEXT("error"),Message); W->WriteObjectEnd(); W->Close(); return Result;
	}
}

FString UGuLiRogueCardQALibrary::BuildMissileFixture(APlayerController* PC,int32 Count,int32 ProjectilesPerSalvo,FVector Center)
{
	const bool bGuidanceCase = (Count==25 && (ProjectilesPerSalvo==6 || ProjectilesPerSalvo==7))
		|| (Count==2 && ProjectilesPerSalvo==60);
	const bool bPerformanceCase = (Count==100 || Count==500)
		&& (ProjectilesPerSalvo==1 || ProjectilesPerSalvo==4 || ProjectilesPerSalvo==8);
	if (!IsMissileFixtureSession(PC) || (!bGuidanceCase && !bPerformanceCase) || Center.ContainsNaN())
		return MissileFixtureFailure(TEXT("Use a local authoritative PIE commander: guidance 25x6/25x7/2x60, performance 100/500 x 1/4/8."));
	for (auto It=Fixtures.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	if (Fixtures.Contains(PC->GetWorld())) return MissileFixtureFailure(TEXT("Restart PIE for a fresh population and acquisition baseline before changing cases."));
	auto* Player=PC->GetPlayerState<AGuLiBattlePlayerState>();
	auto* Army=PC->GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	const auto* Data=PC->GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Definition=Data ? Data->FindSoldierDefinition(2) : nullptr;
	auto* Tasks=PC->GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
	const auto* Profile=Player && Army ? Army->FindResolvedSkill(Player->GetTeam(),2,TEXT("MissileLauncher")) : nullptr;
	if (!Player || !Player->IsCommander() || !Player->IsBattleReady() || !Profile || Profile->ProjectileCount!=1 || !Definition || !Tasks)
		return MissileFixtureFailure(TEXT("Start a fresh ready commander match with base projectile count 1."));
	FGuLiSkillSource Source; Source.SourceInstanceId=FGuid::NewGuid(); Source.DebugLabel=TEXT("WM01MissilePerformanceFixture");
	auto& Unlock=Source.Unlocks.AddDefaulted_GetRef(); Unlock.Target.UnitTypeIds.Add(2); Unlock.Target.SlotId=TEXT("MissileLauncher");
	if (ProjectilesPerSalvo>1)
	{
		auto& Modifier=Source.Modifiers.AddDefaulted_GetRef(); Modifier.Target=Unlock.Target;
		Modifier.Attribute=EGuLiSkillAttribute::ProjectileCount; Modifier.Operation=EGuLiSkillModifierOperation::AddFlat;
		Modifier.IntegerMagnitude=ProjectilesPerSalvo-1;
	}
	FString Error;
	if (!Army->UpsertSource(*Player,Source,Error)) return MissileFixtureFailure(Error);
	Army->CommitPendingChanges();
	// Respect the same resolved avoidance radius as authority spawning. The old
	// 400 cm grid rejected most WM01s after its diameter became 1250 cm.
	const float Spacing=FMath::Max(400.f,Definition->GetMassAvoidanceRadius(150.f)*2.f+20.f);
	// BuildFixture reserves ten spare columns/rows for blocked spawn locations.
	// Center its first requested rows on the review marker instead of shifting
	// the entire larger formation behind the fixed observation cameras.
	const int32 Columns=FMath::CeilToInt(FMath::Sqrt(float(Count)))+10;
	const int32 Rows=FMath::DivideAndRoundUp(Count,Columns);
	const FVector LayoutCenter=bGuidanceCase ? Center : Center+FVector(0,(Columns-Rows)*Spacing*.5f,0);
	const FString Built=BuildFixture(PC,Count,LayoutCenter,Spacing,bGuidanceCase);
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Built),Json) || !Json.IsValid() || !Json->HasField(TEXT("units"))) return MissileFixtureFailure(TEXT("Fixture creation failed."));
	auto& Fixture=Fixtures.Add(PC->GetWorld()); Fixture.Owner=PC; Fixture.Count=ProjectilesPerSalvo;
	for (const auto& Value:Json->GetArrayField(TEXT("units")))
	{
		FGuLiSoldierId Id; Id.Value=static_cast<uint32>(Value->AsObject()->GetNumberField(TEXT("id"))); Fixture.Units.Add(Id);
	}
	// Stop through the real task scheduler so automatic advance cannot carry the
	// prepared population out of the observation area between salvos. Only this
	// fixture's new units are selected; the player's selection is untouched.
	FGuLiCommanderSelectionState Selection; Selection.SelectionRevision=1;
	for (const auto Id:Fixture.Units)
	{
		if (Selection.Cohorts.IsEmpty() || Selection.Cohorts.Last().MemberIds.Num()==int32(GULI_CONTROL_COHORT_TARGET_SIZE))
			Selection.Cohorts.AddDefaulted();
		Selection.Cohorts.Last().MemberIds.Add(Id);
	}
	FGuLiUnitTaskCommand Stop; Stop.CommandId=1; Stop.SelectionRevision=Selection.SelectionRevision; Stop.Disposition=EGuLiTaskDisposition::Stop;
	int32 Stopped=0, Rejected=0;
	Tasks->Submit(*Player,Selection,Stop,Error,Stopped,Rejected);
	for (TActorIterator<ACameraActor> It(PC->GetWorld());It;++It)
		if (It->ActorHasTag(TEXT("WM01MissileFixedCamera"))) { PC->SetViewTarget(*It); break; }
	return FString::Printf(TEXT("{\"success\":%s,\"requested_units\":%d,\"fixture_units\":%d,\"stopped_units\":%d,\"spacing_centimeters\":%.1f,\"projectiles_per_salvo\":%d,\"expected_missiles\":%d,\"other_map_units_unchanged\":true}"),
		Fixture.Units.Num()==Count && Stopped==Count && Rejected==0?TEXT("true"):TEXT("false"),Count,Fixture.Units.Num(),Stopped,Spacing,ProjectilesPerSalvo,Fixture.Units.Num()*ProjectilesPerSalvo);
}

FString UGuLiRogueCardQALibrary::FireMissileFixture(APlayerController* PC)
{
	if (!IsMissileFixtureSession(PC)) return MissileFixtureFailure(TEXT("PIE authority required."));
	const auto* Fixture=Fixtures.Find(PC->GetWorld());
	auto* Player=PC->GetPlayerState<AGuLiBattlePlayerState>();
	const auto* Skills=Player ? Player->FindComponentByClass<UGuLiCommanderSkillComponent>() : nullptr;
	const auto* Catalog=Skills ? Skills->GetSkillCatalog() : nullptr;
	auto* Authority=PC->GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	const auto* Army=PC->GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	const auto* Profile=Player && Army ? Army->FindResolvedSkill(Player->GetTeam(),2,TEXT("MissileLauncher")) : nullptr;
	if (!Fixture || Fixture->Owner.Get()!=PC || !Catalog || !Authority || !Profile) return MissileFixtureFailure(TEXT("Build this commander's fixture first."));
	int32 Success=0;
	const FGuid Request=FGuid::NewGuid();
	for (const auto Id:Fixture->Units)
	{
		FGuLiSoldierCombatDebug State;
		if (!Authority->TryGetSoldierCombatDebug(Id,State)) continue;
		FVector Ground;
		if (!GuLiSkillTargeting::ResolveGround(*PC->GetWorld(),State.Location+FVector(0,-FMath::Min(2000.f,Profile->RangeCentimeters*.5f),0),Ground)) continue;
		FGuLiCommanderSelectionState Selection; Selection.Cohorts.AddDefaulted_GetRef().MemberIds.Add(Id);
		TArray<FGuLiActiveSkillUnitResult> Results;
		Authority->ExecuteSelectedUnitSkills(*Player,Selection,*Catalog,Request,true,Ground,Results);
		for (const auto& Result:Results) if (Result.Code==EGuLiActiveSkillResultCode::Succeeded) ++Success;
	}
	return FString::Printf(TEXT("{\"successful_units\":%d,\"missiles\":%d,\"expected_units\":%d,\"uses_authority_skill_and_cooldown\":true}"),
		Success,Success*Profile->ProjectileCount,Fixture->Units.Num());
}

FString UGuLiRogueCardQALibrary::MissileMetrics(APlayerController* PC)
{
	if (!IsMissileFixtureSession(PC)) return TEXT("{}");
	const auto* Presentation=PC->GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>();
	if (!Presentation) return TEXT("{}"); const auto C=Presentation->GetCounters();
	return FString::Printf(TEXT("{\"all_vfx_components\":%d,\"missile_components\":%d,\"particle_capacity\":%d,\"full_trails\":%d,\"cluster_upload_ms\":%.6f,\"frame\":%s,\"gpu_niagara_and_overdraw_require_profiler\":true}"),
		C.ComponentCount,C.MissileClusterComponents,C.MissileParticleCapacity,C.MissileFullTrails,C.MissileClusterUpdateMilliseconds,*FrameTimings(PC));
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs BuildMissileCase(TEXT("gs.MissileFixture.Build"),TEXT("PIE only: guidance 25 6 / 25 7 / 2 60; performance 100|500 1|4|8. Fresh match required. Guidance acceptance uses selection + Q, not Fixture.Fire."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
		{
			if (!World || Args.Num()!=2) return;
			FVector Center(0,70000,200);
			const bool bGuidanceCase=FCString::Atoi(*Args[0])==25 || FCString::Atoi(*Args[0])==2;
			const FName CenterTag=bGuidanceCase?FName(TEXT("WM01GuidanceFixtureCenter")):FName(TEXT("WM01MissileFixtureCenter"));
			for (TActorIterator<AActor> It(World);It;++It) if (It->ActorHasTag(CenterTag)) { Center=It->GetActorLocation(); break; }
			UE_LOG(LogTemp,Display,TEXT("%s"),*UGuLiRogueCardQALibrary::BuildMissileFixture(World->GetFirstPlayerController(),FCString::Atoi(*Args[0]),FCString::Atoi(*Args[1]),Center));
		}));
	FAutoConsoleCommandWithWorld FireMissileCase(TEXT("gs.MissileFixture.Fire"),TEXT("Fire the prepared real units through authority skill execution; normal cooldown applies."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (World) UE_LOG(LogTemp,Display,TEXT("%s"),*UGuLiRogueCardQALibrary::FireMissileFixture(World->GetFirstPlayerController())); }));
	FAutoConsoleCommandWithWorld MissileCaseMetrics(TEXT("gs.MissileFixture.Metrics"),TEXT("Read cluster counters and frame timings; does not start profiling."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (World) UE_LOG(LogTemp,Display,TEXT("%s"),*UGuLiRogueCardQALibrary::MissileMetrics(World->GetFirstPlayerController())); }));
	FAutoConsoleCommandWithWorldAndArgs MissileCaseCamera(TEXT("gs.MissileFixture.Camera"),TEXT("PIE only: near|overview|lod0|lod1|lod2. Overview remains the fixed performance comparison camera."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
		{
			auto* PC=World ? World->GetFirstPlayerController() : nullptr;
			if (!IsMissileFixtureSession(PC) || Args.Num()!=1) return;
			FName Tag;
			if (Args[0].Equals(TEXT("near"),ESearchCase::IgnoreCase)) Tag=TEXT("WM01MissileNearCamera");
			else if (Args[0].Equals(TEXT("overview"),ESearchCase::IgnoreCase)) Tag=TEXT("WM01MissileFixedCamera");
			else if (Args[0].Equals(TEXT("lod0"),ESearchCase::IgnoreCase)) Tag=TEXT("CommanderLODCamera0");
			else if (Args[0].Equals(TEXT("lod1"),ESearchCase::IgnoreCase)) Tag=TEXT("CommanderLODCamera1");
			else if (Args[0].Equals(TEXT("lod2"),ESearchCase::IgnoreCase)) Tag=TEXT("CommanderLODCamera2");
			else return;
			for (TActorIterator<ACameraActor> It(World);It;++It)
				if (It->ActorHasTag(Tag)) { PC->SetViewTarget(*It); break; }
		}));
}
