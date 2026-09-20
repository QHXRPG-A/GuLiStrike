#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponAnimInstance.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"

struct FGuLiMechFireTestAccess
{
	static bool Load(UGuLiGroundMechWeaponComponent* Weapon) { return Weapon->LoadConfiguration(TEXT("1.1")); }
	static void Fire(UGuLiGroundMechWeaponComponent* Weapon)
	{ Weapon->RegisterSource(); Weapon->bHasAim = true; Weapon->AimPoint = FVector(100000,0,10000); Weapon->bFireHeld = true; Weapon->TryFire(); }
	static void RepeatCue(UGuLiGroundMechWeaponComponent* Weapon, FGuid Id, float Time) { Weapon->MulticastShot_Implementation(Id,Time); }
};

namespace GuLiMechFireTests
{
	constexpr const TCHAR* CurvePath = TEXT("/Game/GuLiStrike/GroundMech/Animations/CF_Machinegun_Recoil");
	void FillCurve(UCurveFloat& Curve)
	{
		Curve.FloatCurve.Reset();
		for (const FVector2D Point : { FVector2D(0,0), FVector2D(.025,1), FVector2D(.055,.85), FVector2D(.1,.3), FVector2D(.15,0) })
		{
			const FKeyHandle Handle = Curve.FloatCurve.AddKey(Point.X, Point.Y);
			// Zero-tangent cubic segments: C1 continuous and bounded between adjacent keys.
			Curve.FloatCurve.SetKeyInterpMode(Handle, RCIM_Cubic);
			Curve.FloatCurve.SetKeyTangentMode(Handle, RCTM_User);
			Curve.FloatCurve.GetKey(Handle).ArriveTangent = Curve.FloatCurve.GetKey(Handle).LeaveTangent = 0;
		}
	}
	struct FWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); World->GetSubsystem<UGuLiDamageLedgerSubsystem>()->BeginServerEpoch(900); }
		~FWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	};
	FGuLiTargetHandle Source()
	{
		FGuLiTargetHandle Handle; Handle.Kind=EGuLiTargetKind::GroundActor;
		Handle.AuthorityId=FGuid(1,2,3,4); Handle.Generation=1; Handle.LocalId=1; return Handle;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMechFireCurveTest,"GuLiStrike.GroundMech.Fire.Recoil",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMechFireCurveTest::RunTest(const FString&)
{
	using namespace GuLiMechFireTests;
	UCurveFloat* Curve=LoadObject<UCurveFloat>(nullptr,CurvePath);
	if (!TestNotNull(TEXT("Saved editable recoil curve"),Curve)) return false;
	TestEqual(TEXT("Exact peak"),Curve->GetFloatValue(.025),1.f);
	for (int32 I=0; I<=1500; ++I) TestTrue(TEXT("No curve overshoot"),Curve->GetFloatValue(I*.0001f)>=0 && Curve->GetFloatValue(I*.0001f)<=1);
	auto* Mesh=NewObject<USkeletalMeshComponent>();
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/GuLiStrike/GroundMech/Style_v9/Meshes/SK_Machinegun")));
	auto* Anim=NewObject<UGuLiGroundMechWeaponAnimInstance>(Mesh);
	Anim->Configure(Curve,TEXT("Barrel_big"),152,.15); Anim->TriggerRecoil(); Anim->NativeUpdateAnimation(.025);
	TestTrue(TEXT("Barrel local Z reaches 152 cm"),FMath::IsNearlyEqual(188.101471f+Anim->RecoilOffset,152.f,.001f));
	Anim->NativeUpdateAnimation(.06); const float Before=Anim->RecoilOffset;
	Anim->TriggerRecoil(); Anim->NativeUpdateAnimation(0);
	TestEqual(TEXT("Retrigger starts at current displacement"),Anim->RecoilOffset,Before);
	Anim->NativeUpdateAnimation(.15); TestEqual(TEXT("Stopped weapon returns completely"),Anim->RecoilOffset,0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMechFirePoolTest,"GuLiStrike.GroundMech.Fire.PlayerPoolAndWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMechFirePoolTest::RunTest(const FString&)
{
	using namespace GuLiMechFireTests; FWorld Fixture;
	auto* Ledger=Fixture.World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	auto* Pool=Fixture.World->GetSubsystem<UGuLiProjectilePoolSubsystem>();
	FGuLiCombatSourceAdapter Adapter; Adapter.LifetimeOwner=Fixture.World;
	Adapter.ReadSnapshot=[](FGuLiCombatTargetSnapshot& Out) { Out.Handle=Source(); Out.Team=EGuLiTeam::Red; Out.bAlive=true; return true; };
	TestTrue(TEXT("Register attack-only player"),Ledger->RegisterSource(Source(),MoveTemp(Adapter)));
	FGuLiCombatTargetSnapshot Snapshot;
	TestFalse(TEXT("Player is not a damage target"),Ledger->TryGetTargetSnapshot(Source(),Snapshot));
	int32 EnemyHits=0, FriendlyHits=0; float Damage=0;
	for (int32 I=0; I<2; ++I)
	{
		FGuLiCombatTargetAdapter Target; Target.LifetimeOwner=Fixture.World;
		const auto Handle=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(900,I+1);
		Target.ReadSnapshot=[Handle,I](FGuLiCombatTargetSnapshot& Out)
		{ Out.Handle=Handle; Out.Location=FVector(I==0?100:600,0,1000); Out.CollisionRadius=20; Out.Team=I==0?EGuLiTeam::Red:EGuLiTeam::Blue; Out.bAlive=true; return true; };
		Target.ApplyDamage=[&EnemyHits,&FriendlyHits,&Damage,I](const FGuLiDamageRequest& R,FGuLiDamageCommitResult& Out)
		{ (I==0?FriendlyHits:EnemyHits)++; Damage+=R.Damage; Out.AppliedDamage=R.Damage; Out.RemainingHealth=100-R.Damage; return true; };
		Ledger->RegisterTarget(Handle,MoveTemp(Target));
	}
	FGuLiPooledProjectileLaunch Launch; Launch.Context.MatchEpoch=900; Launch.Context.Source=Source();
	Launch.Context.ShotId=FGuid::NewGuid(); Launch.Context.RootEventId=Launch.Context.ShotId; Launch.Context.Damage=10;
	Launch.Position=FVector(0,0,1000); Launch.Speed=12000; Launch.Lifetime=5; Launch.MaximumDistance=60000; Launch.SweepRadius=15;
	Launch.PlayerBulletSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_Bullet.NS_GroundMech_Bullet")));
	const auto Handle=Pool->Launch(Launch); TestTrue(TEXT("Player launches without soldier or wingman identity"),Handle.IsValid());
	TestFalse(TEXT("Duplicate live ShotId rejected"),Pool->Launch(Launch).IsValid());
	FGuLiCombatEffectState State; Pool->Query(Handle,State);
	bool bOK=false; FNetBitWriter Writer(nullptr,0); State.NetSerialize(Writer,nullptr,bOK); TestTrue(TEXT("Launch serializes"),bOK);
	FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits()); FGuLiCombatEffectState Copy; Copy.NetSerialize(Reader,nullptr,bOK);
	TestTrue(TEXT("GroundActor survives wire encoding"),bOK && Copy.Source==Source());
	TestTrue(TEXT("Visual and ShotId survive wire encoding"),Copy.PlayerBulletSystem==State.PlayerBulletSystem && Copy.EffectId==State.EffectId);
	Launch.Context.Damage=20; // Already-flying data is frozen.
	Pool->Step(1.f/30); Pool->Query(Handle,State); TestTrue(TEXT("First 30 Hz sweep moved 400 cm"),FMath::IsNearlyEqual(State.Location.X,400.,.01));
	Ledger->UnregisterSource(Source(),Fixture.World); // Retained provenance survives unpossession/destruction.
	Pool->Step(2.f/30); Pool->Step(.2);
	TestEqual(TEXT("Friendly skipped"),FriendlyHits,0); TestEqual(TEXT("Only first enemy hit"),EnemyHits,1);
	TestEqual(TEXT("In-flight damage remains 10"),Damage,10.f); TestEqual(TEXT("One ledger commit"),Ledger->GetCommitCount(),uint64(1));
	TestEqual(TEXT("Hit round recycled"),Pool->GetActiveCount(),0);
	FGuLiCombatSourceAdapter NextSource; NextSource.LifetimeOwner=Fixture.World;
	NextSource.ReadSnapshot=[](FGuLiCombatTargetSnapshot& Out) { Out.Handle=Source(); Out.Team=EGuLiTeam::Red; Out.bAlive=true; return true; };
	Ledger->RegisterSource(Source(),MoveTemp(NextSource));
	Launch.Context.ShotId=FGuid::NewGuid(); Launch.Context.RootEventId=Launch.Context.ShotId;
	Launch.ServerTime=.2f; Launch.Direction=FVector::RightVector;
	const auto Expiring=Pool->Launch(Launch); Pool->Step(5.21f);
	TestFalse(TEXT("Five-second flight expires"),Pool->Query(Expiring,State));
	Launch.Context.ShotId=FGuid::NewGuid(); Launch.ServerTime=5.3f;
	const auto OldRound=Pool->Launch(Launch); Ledger->BeginServerEpoch(901); Pool->BeginEpoch(901);
	TestFalse(TEXT("Round change invalidates old pooled handle"),Pool->Query(OldRound,State));
	TestEqual(TEXT("Round change empties player rounds"),Pool->GetActiveCount(),0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMechFireCadenceTest,"GuLiStrike.GroundMech.Fire.UpgradesCadenceAndInterruptions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMechFireCadenceTest::RunTest(const FString&)
{
	using namespace GuLiMechFireTests; FWorld Fixture;
	auto* PawnClass=LoadClass<AGuLiGroundMechCharacter>(nullptr,TEXT("/Game/GuLiStrike/GroundMech/Review/BP_GroundMech_FireReview.BP_GroundMech_FireReview_C"));
	if (!TestNotNull(TEXT("Playable candidate class"),PawnClass)) return false;
	auto* Pawn=Fixture.World->SpawnActor<AGuLiGroundMechCharacter>(PawnClass);
	auto* PC=Fixture.World->SpawnActor<AGuLiCommanderPlayerController>();
	PC->PlayerState=Fixture.World->SpawnActor<AGuLiBattlePlayerState>();
	auto* PS=CastChecked<AGuLiBattlePlayerState>(PC->PlayerState);
	PS->SetServerRoleAssignment(EGuLiTeam::Red,EGuLiCommanderRole::Ground,0); PS->SetServerBattleReady(true);
	PC->SetPawn(Pawn); Pawn->PossessedBy(PC);
	Pawn->GetMachinegun()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/GuLiStrike/GroundMech/Style_v9/Meshes/SK_Machinegun")));
	auto* Weapon=Pawn->GetWeapon(); Weapon->bWeaponEnabled=true;
	Weapon->UpgradeTable=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Upgrades"));
	Weapon->SkillTable=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills"));
	if (!TestTrue(TEXT("Imported configuration loads"),FGuLiMechFireTestAccess::Load(Weapon))) return false;
	for (int32 Level=1; Level<=3; ++Level)
	{
		Fixture.World->TimeSeconds=Level*10.;
		TestTrue(TEXT("Server applies string upgrade ID"),Weapon->ApplyUpgradeById(FString::Printf(TEXT("1.%d"),Level)));
		TestEqual(TEXT("Rate comes from table"),Weapon->GetFireRate(),float(Level*2)); TestEqual(TEXT("Damage comes from table"),Weapon->GetShotDamage(),float(5+Level*5));
		const int32 Before=Weapon->ShotsFired;
		for (int32 Frame=0; Frame<60; ++Frame) { Fixture.World->TimeSeconds=Level*10.+Frame/60.; FGuLiMechFireTestAccess::Fire(Weapon); }
		TestEqual(TEXT("Actual accepted launches in one second"),Weapon->ShotsFired-Before,Level*2);
	}
	TestFalse(TEXT("Invalid ID refused"),Weapon->ApplyUpgradeById(TEXT("1.9"))); TestEqual(TEXT("Invalid ID preserves old row"),Weapon->CurrentUpgradeId,FString(TEXT("1.3")));
	TestTrue(TEXT("Upgrade rescales remaining phase"),FMath::IsNearlyEqual(GuLiMechFire::RescaleCooldown(.25,.5,2,4),.375));
	TestTrue(TEXT("Overdue cooldown cannot create catch-up shots"),FMath::IsNearlyEqual(GuLiMechFire::RescaleCooldown(2.,.5,2,6),2.));
	const FGuid Cue=FGuid::NewGuid(); const int32 CueBefore=Weapon->CosmeticShots;
	FGuLiMechFireTestAccess::RepeatCue(Weapon,Cue,Fixture.World->GetTimeSeconds());
	FGuLiMechFireTestAccess::RepeatCue(Weapon,Cue,Fixture.World->GetTimeSeconds());
	TestEqual(TEXT("Repeated ShotId produces one cosmetic cue"),Weapon->CosmeticShots,CueBefore+1);
	const int32 Before=Weapon->ShotsFired; Fixture.World->TimeSeconds=40;
	PC->SetIgnoreMoveInput(true); FGuLiMechFireTestAccess::Fire(Weapon); TestEqual(TEXT("Input lock prevents fire"),Weapon->ShotsFired,Before); PC->ResetIgnoreMoveInput();
	const FGuid Token=FGuid::NewGuid(); auto* Control=Pawn->FindComponentByClass<UGuLiExternalUnitControlComponent>();
	Control->ApplyServerState(Token,false,true,Pawn->GetActorTransform(),false); FGuLiMechFireTestAccess::Fire(Weapon);
	TestEqual(TEXT("External action lock prevents fire"),Weapon->ShotsFired,Before);
	Control->ApplyServerState(Token,false,false,Pawn->GetActorTransform(),false);
	Weapon->bFireHeld=true; PC->FlushPressedKeys(); TestFalse(TEXT("Focus flush releases fire"),Weapon->bFireHeld);
	Weapon->bFireHeld=true; Pawn->UnPossessed(); TestFalse(TEXT("Unpossession releases fire"),Weapon->bFireHeld);
	FGuLiMechFireTestAccess::Fire(Weapon); TestEqual(TEXT("Unpossessed pawn cannot fire"),Weapon->ShotsFired,Before);
	return true;
}

#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Serialization/JsonWriter.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "UnrealClient.h"
#include "Editor.h"

// Scoped authoring/evidence commands for the explicitly authorized fire acceptance fixture.
namespace GuLiMechFireQA
{
	FAutoConsoleCommand ControlCommand(TEXT("gs.MechFire.QA.Control"),TEXT("Actual PIE RPC callspace: player-guid held|release|flush|unpossess|repossess."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num()!=2 || !GEngine) return;
		FGuid Guid; if (!FGuid::Parse(Args[0],Guid)) return;
		static TMap<FGuid,TWeakObjectPtr<AGuLiGroundMechCharacter>> Parked;
		// Python deliberately forces nested RPCs local. These tests must use real network callspace.
		TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,false);
		for (const auto& Context:GEngine->GetWorldContexts())
		{
			UWorld* World=Context.World(); if (!World || World->WorldType!=EWorldType::PIE) continue;
			for (TActorIterator<AGuLiCommanderPlayerController> It(World); It; ++It)
			{
				const auto* PS=It->GetPlayerState<AGuLiBattlePlayerState>(); if (!PS || PS->GetPlayerGuid()!=Guid) continue;
				if (Args[1]==TEXT("repossess") && It->HasAuthority())
				{ if (auto* Saved=Parked.Find(Guid); Saved && Saved->IsValid()) It->Possess(Saved->Get()); Parked.Remove(Guid); continue; }
				auto* Pawn=Cast<AGuLiGroundMechCharacter>(It->GetPawn()); if (!Pawn) continue;
				if (Args[1]==TEXT("unpossess") && It->HasAuthority()) { Parked.Add(Guid,Pawn); It->UnPossess(); }
				else if (It->IsLocalController())
				{
					if (Args[1]==TEXT("held")) Pawn->GetWeapon()->SetFireHeld(true);
					else if (Args[1]==TEXT("release")) Pawn->GetWeapon()->SetFireHeld(false);
					else if (Args[1]==TEXT("flush")) It->FlushPressedKeys();
				}
			}
		}
	}));
	UNiagaraSystem* Candidate(const FString& Name)
	{
		if (Name==TEXT("bullet")) return LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_Bullet"));
		if (Name==TEXT("muzzle")) return LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_Muzzle"));
		return nullptr;
	}
	FAutoConsoleCommand InputCommand(TEXT("gs.MechFire.QA.Input"),TEXT("Candidate-only override: bullet|muzzle emitter module input float|vec2|vec3|int value. Use ~ for spaces in input names."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num()!=6) return;
		auto* System=Candidate(Args[0]); if (!System) return;
		const FString InputName=Args[3].Replace(TEXT("~"),TEXT(" "));
		for (auto& Handle:System->GetEmitterHandles()) if (Handle.GetName()==FName(Args[1]))
		{
			auto* Data=Handle.GetInstance().GetEmitterData(); auto* Source=Cast<UNiagaraScriptSource>(Data->GraphSource); if (!Source || !Source->NodeGraph) return;
			TArray<UNiagaraNodeFunctionCall*> Nodes; Source->NodeGraph->GetNodesOfClass(Nodes);
			for (auto* Node:Nodes) if (Node->GetFunctionName()==Args[2])
			{
				FNiagaraTypeDefinition Type=FNiagaraTypeDefinition::GetFloatDef();
				if (Args[4]==TEXT("vec2")) Type=FNiagaraTypeDefinition::GetVec2Def();
				else if (Args[4]==TEXT("vec3")) Type=FNiagaraTypeDefinition::GetVec3Def();
				else if (Args[4]==TEXT("int")) Type=FNiagaraTypeDefinition::GetIntDef();
				else if (Args[4]!=TEXT("float")) return;
				System->Modify(); Source->NodeGraph->Modify(); Node->Modify();
				auto& Pin=FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Node,FNiagaraParameterHandle(*(Args[2]+TEXT(".")+InputName)),Type,FGuid(),FGuid());
				Pin.BreakAllPinLinks();
				CastChecked<UEdGraphSchema_Niagara>(Source->NodeGraph->GetSchema())->TrySetDefaultValue(Pin,Args[5],true);
				const FNiagaraVariable RI(Type,FName(*(TEXT("Constants.")+Args[1]+TEXT(".")+Args[2]+TEXT(".")+InputName)));
				for (UNiagaraScript* Script:{Data->EmitterSpawnScriptProps.Script,Data->EmitterUpdateScriptProps.Script,Data->SpawnScriptProps.Script,Data->UpdateScriptProps.Script}) if (Script) Script->RapidIterationParameters.RemoveParameter(RI);
				Node->MarkNodeRequiresSynchronization(TEXT("GroundMech one-shot authoring"),true);
				System->RequestCompile(false); System->WaitForCompilationComplete(); System->MarkPackageDirty(); return;
			}
		}
	}));
	FAutoConsoleCommand GraphCommand(TEXT("gs.MechFire.QA.NiagaraReadback"),TEXT("Read candidate graph inputs including static enum choices and links."),FConsoleCommandDelegate::CreateLambda([]
	{
		FString Json; const auto Writer=TJsonWriterFactory<>::Create(&Json); Writer->WriteArrayStart();
		for (const FString Kind:{TEXT("bullet"),TEXT("muzzle")}) if (auto* System=Candidate(Kind))
		for (auto& Handle:System->GetEmitterHandles())
		{
			auto* Data=Handle.GetInstance().GetEmitterData(); auto* Source=Cast<UNiagaraScriptSource>(Data->GraphSource); if (!Source || !Source->NodeGraph) continue;
			for (const auto& Node:Source->NodeGraph->Nodes)
			{
				const auto* Call=Cast<UNiagaraNodeFunctionCall>(Node); if (!Call && Node->GetClass()->GetFName()!=TEXT("NiagaraNodeParameterMapSet")) continue;
				Writer->WriteObjectStart(); Writer->WriteValue(TEXT("system"),Kind); Writer->WriteValue(TEXT("emitter"),Handle.GetName().ToString());
				Writer->WriteValue(TEXT("node"),Call?Call->GetFunctionName():Node->GetName()); Writer->WriteArrayStart(TEXT("inputs"));
				for (auto* Pin:Node->Pins) if (Pin->Direction==EGPD_Input)
				{
					Writer->WriteObjectStart(); Writer->WriteValue(TEXT("name"),Pin->PinName.ToString()); Writer->WriteValue(TEXT("default"),Pin->DefaultValue);
					Writer->WriteArrayStart(TEXT("links")); for (auto* Link:Pin->LinkedTo) Writer->WriteValue(Link->GetOwningNode()->GetName()+TEXT(".")+Link->PinName.ToString()); Writer->WriteArrayEnd();
					if (auto* Enum=Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get())) { Writer->WriteArrayStart(TEXT("enum")); for (int32 I=0; I<Enum->NumEnums(); ++I) Writer->WriteValue(FString::Printf(TEXT("%lld=%s"),Enum->GetValueByIndex(I),*Enum->GetDisplayNameTextByIndex(I).ToString())); Writer->WriteArrayEnd(); }
					Writer->WriteObjectEnd();
				}
				Writer->WriteArrayEnd(); Writer->WriteObjectEnd();
			}
		}
		Writer->WriteArrayEnd(); Writer->Close(); FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectDir()/TEXT("TestResults/GroundMech/Fire/niagara-graph.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}));
	FAutoConsoleCommand CurveCommand(TEXT("gs.MechFire.QA.AuthorCurve"),TEXT("Author the one project-owned recoil curve."),FConsoleCommandDelegate::CreateLambda([]
	{
		using namespace GuLiMechFireTests;
		UPackage* Package=CreatePackage(CurvePath); Package->FullyLoad();
		auto* Curve=FindObject<UCurveFloat>(Package,TEXT("CF_Machinegun_Recoil"));
		if (!Curve) { Curve=NewObject<UCurveFloat>(Package,TEXT("CF_Machinegun_Recoil"),RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Curve); }
		Curve->Modify(); FillCurve(*Curve); Curve->MarkPackageDirty();
		FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
		UPackage::SavePackage(Package,Curve,*(FPaths::ProjectContentDir()/TEXT("GuLiStrike/GroundMech/Animations/CF_Machinegun_Recoil.uasset")),Args);
	}));
	FAutoConsoleCommand EmitterCommand(TEXT("gs.MechFire.QA.AuthorEmitterSpace"),TEXT("Set bounds/local space on project-owned candidate systems: bullet|muzzle."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num()!=1 || (Args[0]!=TEXT("bullet") && Args[0]!=TEXT("muzzle"))) return;
		const bool bBullet=Args[0]==TEXT("bullet");
		auto* System=LoadObject<UNiagaraSystem>(nullptr,bBullet?TEXT("/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_Bullet"):TEXT("/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_Muzzle"));
		if (!System) return; System->Modify();
		for (auto& Handle:System->GetEmitterHandles()) if (auto* Data=Handle.GetInstance().GetEmitterData())
		{ Data->bLocalSpace=true; Data->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed; Data->FixedBounds=FBox(FVector(-800),FVector(800)); }
		System->RequestCompile(false); System->MarkPackageDirty();
	}));
	FAutoConsoleCommand SampleCommand(TEXT("gs.MechFire.QA.Sample"),TEXT("Read actual PIE source, pose, particle count and pooled state. Optional safe screenshot label."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString Directory=FPaths::ProjectDir()/TEXT("TestResults/GroundMech/Fire"); IFileManager::Get().MakeDirectory(*Directory,true);
		FString Json; const auto Writer=TJsonWriterFactory<>::Create(&Json); Writer->WriteArrayStart();
		for (const auto& Context:GEngine->GetWorldContexts())
		{
			UWorld* World=Context.World(); if (!World || World->WorldType!=EWorldType::PIE) continue;
			Writer->WriteObjectStart(); Writer->WriteValue(TEXT("world"),World->GetPathName()); Writer->WriteValue(TEXT("net_mode"),int32(World->GetNetMode()));
			Writer->WriteValue(TEXT("time"),World->GetTimeSeconds());
			if (auto* Ledger=World->GetSubsystem<UGuLiDamageLedgerSubsystem>()) Writer->WriteValue(TEXT("commits"),int64(Ledger->GetCommitCount()));
			if (auto* Pool=World->GetSubsystem<UGuLiProjectilePoolSubsystem>())
			{ const auto Stats=Pool->GetStats(); Writer->WriteValue(TEXT("launched"),Stats.Launched); Writer->WriteValue(TEXT("hits"),Stats.Hits); Writer->WriteValue(TEXT("blocked"),Stats.Blocked); Writer->WriteValue(TEXT("active_rounds"),Stats.Active); }
			Writer->WriteArrayStart(TEXT("players"));
			for (TActorIterator<AGuLiGroundMechCharacter> It(World); It; ++It)
			{
				const auto* W=It->GetWeapon(); auto* Mesh=It->GetMachinegun(); Writer->WriteObjectStart();
				Writer->WriteValue(TEXT("name"),It->GetName()); Writer->WriteValue(TEXT("shots"),W->ShotsFired); Writer->WriteValue(TEXT("cosmetic_shots"),W->CosmeticShots);
				Writer->WriteValue(TEXT("ready"),W->IsWeaponReady()); Writer->WriteValue(TEXT("upgrade"),W->CurrentUpgradeId);
				const int32 Bone=Mesh->GetBoneIndex(TEXT("Barrel_big")); const int32 Parent=Mesh->GetBoneIndex(Mesh->GetParentBone(TEXT("Barrel_big")));
				if (Bone!=INDEX_NONE && Parent!=INDEX_NONE) Writer->WriteValue(TEXT("barrel_local_z"),Mesh->GetBoneTransform(Bone).GetRelativeTransform(Mesh->GetBoneTransform(Parent)).GetTranslation().Z);
				Writer->WriteObjectEnd();
			}
			Writer->WriteArrayEnd(); Writer->WriteArrayStart(TEXT("effects"));
			for (TObjectIterator<UNiagaraComponent> It; It; ++It)
			{
				if (!IsValid(*It) || It->GetWorld()!=World || !It->GetAsset() || !It->GetAsset()->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/FX/GroundMech/"))) continue;
				Writer->WriteObjectStart(); Writer->WriteValue(TEXT("asset"),It->GetAsset()->GetName()); Writer->WriteValue(TEXT("active"),It->IsActive()); int32 Count=0;
				if (const auto Controller=It->GetSystemInstanceController(); Controller && Controller->IsValid())
				{ Controller->WaitForConcurrentTickAndFinalize(); for (const auto& Emitter:Controller->GetSystemInstance_Unsafe()->GetEmitters()) Count+=Emitter->GetNumParticles(); }
				Writer->WriteValue(TEXT("particles"),Count);
				if (It->GetAsset()->GetName()==TEXT("NS_GroundMech_Muzzle") && It->GetAttachParent())
				{
					const FVector Axis=It->GetAttachParent()->GetSocketTransform(It->GetAttachSocketName()).TransformVector(FVector::ForwardVector).GetSafeNormal();
					Writer->WriteValue(TEXT("muzzle_alignment"),FVector::DotProduct(Axis,It->GetForwardVector()));
				}
				Writer->WriteObjectEnd();
			}
			Writer->WriteArrayEnd(); Writer->WriteObjectEnd();
			if (Args.Num()==1 && World->GetNetMode()!=NM_DedicatedServer && !Args[0].IsEmpty()
				&& !Args[0].GetCharArray().ContainsByPredicate([](TCHAR C){ return C!=0 && !FChar::IsAlnum(C) && C!=TEXT('_') && C!=TEXT('-'); }))
			{
				if (auto* View=World->GetGameViewport(); View && View->Viewport)
				{ TArray<FColor> Pixels; if (GetViewportScreenShot(View->Viewport,Pixels)) { TArray64<uint8> PNG; auto Size=View->Viewport->GetSizeXY(); FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG); FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s-%d.png"),*Args[0],Context.PIEInstance))); } }
			}
		}
		Writer->WriteArrayEnd(); Writer->Close(); FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("native-sample.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}));
}
#endif
#endif
