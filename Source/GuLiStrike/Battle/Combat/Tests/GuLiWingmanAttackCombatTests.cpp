#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Development/GuLiWingmanQAGameMode.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "Containers/Ticker.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "UnrealClient.h"

namespace GuLiWingmanAttackPIE
{
	TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	TArray<TPair<TWeakObjectPtr<APlayerStart>,FTransform>> PriorStarts;
	TWeakObjectPtr<APlayerStart> ExtraStart;
	TSubclassOf<AGameModeBase> PriorMode;
	FDelegateHandle EndHandle;
	FTSTicker::FDelegateHandle TargetHandle;
	FDelegateHandle WorldHandle;
	void Target(const TArray<FString>& Args);
	void Start()
	{
		if (!GEditor || GEditor->IsPlaySessionInProgress()) return;
		UWorld* World=GEditor->GetEditorWorldContext().World(); if(!World) return;
		PriorMode=World->GetWorldSettings()->DefaultGameMode;
		World->GetWorldSettings()->DefaultGameMode=AGuLiWingmanQAGameMode::StaticClass();
		PriorStarts.Reset(); int32 Index=0;
		WorldHandle=FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* W,const UWorld::InitializationValues)
		{
			if(!W || W->WorldType!=EWorldType::PIE) return;
			auto Count=MakeShared<int32>(0);
			W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([Count](AActor* A)
			{
				if(auto* Ship=Cast<AGuLiStrikeShip>(A); Ship && Ship->GetNetMode()==NM_DedicatedServer)
					Ship->SetActorLocationAndRotation(FVector(-90000,((*Count)++%2 ? 50000 : -50000),35000),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
			}));
		});
		for(TActorIterator<APlayerStart> It(World);It;++It)
		{
			PriorStarts.Emplace(*It,It->GetActorTransform());
			It->SetActorLocation(FVector(-50000,Index++*100000-50000,35000));
		}
		ExtraStart=World->SpawnActor<APlayerStart>(FVector(-50000,50000,35000),FRotator::ZeroRotator);
		Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
		Settings->SetPlayNetMode(EPlayNetMode::PIE_Client); Settings->SetPlayNumberOfClients(2);
		Settings->SetRunUnderOneProcess(true); Settings->bLaunchSeparateServer=false;
		Settings->NewWindowWidth=960; Settings->NewWindowHeight=540; Settings->SetClientWindowSize(FIntPoint(960,540));
		Settings->CenterNewWindow=true; Settings->MultipleInstancePositions.Reset();
		EndHandle=FEditorDelegates::EndPIE.AddLambda([](bool)
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TargetHandle); TargetHandle.Reset();
			FWorldDelegates::OnPostWorldInitialization.Remove(WorldHandle); WorldHandle.Reset();
			if(UWorld* Ed=GEditor->GetEditorWorldContext().World()) Ed->GetWorldSettings()->DefaultGameMode=PriorMode;
			for(const auto& P:PriorStarts) if(P.Key.IsValid()) P.Key->SetActorTransform(P.Value);
			if(ExtraStart.IsValid()) ExtraStart->Destroy(); ExtraStart.Reset(); PriorStarts.Reset(); Settings.Reset();
			FEditorDelegates::EndPIE.Remove(EndHandle); EndHandle.Reset();
		});
		FRequestPlaySessionParams Request; Request.EditorPlaySettings=Settings.Get();
		Request.SessionDestination=EPlaySessionDestinationType::InProcess; Request.WorldType=EPlaySessionWorldType::PlayInEditor;
		Request.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Request);
		TargetHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			for(const auto& C:GEngine->GetWorldContexts())
			{
				UWorld* W=C.World(); if(!W || W->WorldType!=EWorldType::PIE || W->GetNetMode()!=NM_DedicatedServer) continue;
				int32 Ships=0;
				for(TActorIterator<AGuLiStrikeShip> It(W);It;++It)
					if(auto* H=It->FindComponentByClass<UGuLiCombatHealthComponent>())
					{ H->InitializeServerHealth(1000000); ++Ships; }
				if(Ships>=2 && W->GetTimeSeconds()>5.0f) { Target({TEXT("ground")}); return false; }
			}
			return true;
		}),0.1f);
	}
	void Target(const TArray<FString>& Args)
	{
		if(Args.Num()!=1 || (Args[0]!=TEXT("ground") && Args[0]!=TEXT("air"))) return;
		for(const auto& C:GEngine->GetWorldContexts())
		{
			UWorld* World=C.World(); if(!World || World->WorldType!=EWorldType::PIE || World->GetNetMode()==NM_Client) continue;
			auto* Ledger=World->GetSubsystem<UGuLiDamageLedgerSubsystem>(); if(!Ledger) continue;
			int32 Index=0;
			for(TActorIterator<AGuLiStrikeShip> It(World);It;++It)
			{
				const auto* Health=It->FindComponentByClass<UGuLiCombatHealthComponent>(); if(!Health) continue;
				FVector Point=It->GetActorLocation()+FVector(90000,0,0);
				const bool Ground=Args[0]==TEXT("ground");
				if(Ground)
				{
					FHitResult Hit;
					if(!World->LineTraceSingleByObjectType(Hit,Point+FVector(0,0,50000),Point-FVector(0,0,500000),FCollisionObjectQueryParams(ECC_WorldStatic))) continue;
					Point=Hit.ImpactPoint+FVector(0,0,250);
				}
				AStaticMeshActor* Actor=World->SpawnActor<AStaticMeshActor>(Point,FRotator::ZeroRotator);
				Actor->SetReplicates(true); Actor->SetReplicateMovement(true);
				Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Actor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
				Actor->SetActorScale3D(FVector(5));
				auto* TargetHealth=NewObject<UGuLiCombatHealthComponent>(Actor); Actor->AddInstanceComponent(TargetHealth); TargetHealth->RegisterComponent();
				FGuLiTargetHandle Handle=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(Ledger->GetMatchEpoch(),700001+Index++);
				if(!Ground) { Handle.Kind=EGuLiTargetKind::Ship; Handle.AuthorityId=FGuid::NewGuid(); }
				TargetHealth->ConfigureServerTarget(Handle,Health->GetCombatTeam()==EGuLiTeam::Red?EGuLiTeam::Blue:EGuLiTeam::Red);
				TargetHealth->InitializeServerHealth(1000000);
				It->SetWingmanAttackTarget(Handle);
			}
		}
	}
	void Sample()
	{
		FString Json; const auto W=TJsonWriterFactory<>::Create(&Json); W->WriteArrayStart();
		for(const auto& C:GEngine->GetWorldContexts())
		{
			UWorld* World=C.World(); if(!World || World->WorldType!=EWorldType::PIE) continue;
			W->WriteObjectStart(); W->WriteValue(TEXT("world"),World->GetPathName()); W->WriteValue(TEXT("net_mode"),int32(World->GetNetMode()));
			W->WriteValue(TEXT("time"),World->GetTimeSeconds());
			if(auto* Effects=World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>())
			{ const auto V=Effects->GetCounters(); W->WriteValue(TEXT("missiles"),V.ProjectilesLaunched); W->WriteValue(TEXT("gun_shots"),V.ShotsPublished); W->WriteValue(TEXT("damage"),V.DamageCommits); }
			W->WriteArrayStart(TEXT("relays"));
			for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
				if(auto* PC=It->Get()) if(auto* Relay=PC->FindComponentByClass<UGuLiWingmanRelayComponent>())
				{
					const auto& S=Relay->GetRelayState(); W->WriteObjectStart(); W->WriteValue(TEXT("owner"),PC->GetName());
					W->WriteValue(TEXT("lifecycle"),int32(S.Lease.Lifecycle)); W->WriteValue(TEXT("config_valid"),S.AbilityConfig.IsUsableByLeaseOwner());
					W->WriteValue(TEXT("accepted"),int64(S.LastAcceptedCandidateSequence)); W->WriteValue(TEXT("target"),S.AttackState.Target.Target.LocalId);
					W->WriteValue(TEXT("target_location"),S.AttackState.Target.Location.ToString());
					W->WriteValue(TEXT("ship_location"),PC->GetPawn()?PC->GetPawn()->GetActorLocation().ToString():FVector::ZeroVector.ToString());
					W->WriteValue(TEXT("normal_reject"),int32(Relay->GetListenSmokeLastNormalResultRejectReason()));
					W->WriteValue(TEXT("atomic_reject"),int32(Relay->GetListenSmokeLastAtomicResultRejectReason()));
					W->WriteValue(TEXT("server_motion_writes"),Relay->GetServerRelay()?int64(Relay->GetServerRelay()->GetServerWingmanMovementWriteCount()):-1);
					const auto& B=Relay->GetLastClientBootstrap();
					W->WriteValue(TEXT("bootstrap_valid"),B.IsWellFormed());
					W->WriteValue(TEXT("attack_hash_valid"),B.AttackStateHash==B.AttackState.ComputeStableHash());
					W->WriteValue(TEXT("ground"),S.AttackState.Target.bGround); W->WriteArrayStart(TEXT("checkpoints"));
					for(const auto& P:S.AttackState.Checkpoints) { W->WriteObjectStart(); W->WriteValue(TEXT("member"),P.Emitter.GetGroupMemberIndex()); W->WriteValue(TEXT("shot"),P.LastShotIndex); W->WriteValue(TEXT("run"),P.RunId); W->WriteObjectEnd(); }
					W->WriteArrayEnd(); W->WriteObjectEnd();
				}
			W->WriteArrayEnd(); W->WriteArrayStart(TEXT("planes"));
			if(auto* Sim=World->GetSubsystem<UGuLiWingmanSimulationSubsystem>())
			{
				TArray<FGuLiWingmanAttackDiagnostic> Entries; Sim->GetAttackDiagnostics(Entries);
				for(const auto& D:Entries)
				{
					W->WriteObjectStart(); W->WriteValue(TEXT("member"),D.Wingman.GetGroupMemberIndex()); W->WriteValue(TEXT("phase"),D.Phase);
					W->WriteValue(TEXT("position"),D.Position.ToString()); W->WriteValue(TEXT("forward"),D.Forward.ToString()); W->WriteValue(TEXT("entry"),D.Entry.ToString());
					W->WriteValue(TEXT("retreat_point"),D.RetreatPoint.ToString()); W->WriteValue(TEXT("turn_control_point"),D.TurnControlPoint.ToString());
					W->WriteValue(TEXT("turn_yaw_degrees"),D.TurnYawDegrees); W->WriteValue(TEXT("turn_pitch_degrees"),D.TurnPitchDegrees);
					W->WriteValue(TEXT("state_entry_serial"),int64(D.StateEntrySerial));
					W->WriteValue(TEXT("desired"),D.PreferredVelocity.ToString()); W->WriteValue(TEXT("guiding"),D.bGuiding); W->WriteValue(TEXT("next_shot"),D.NextShot);
					W->WriteValue(TEXT("cancel_reason"),D.CancelReason);
					W->WriteValue(TEXT("run"),D.RunId); W->WriteValue(TEXT("start"),D.StartTime); W->WriteObjectEnd();
				}
			}
			W->WriteArrayEnd(); W->WriteObjectEnd();
		}
		W->WriteArrayEnd(); W->Close();
		FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectDir()/TEXT("TestResults/WingmanAttack/pie-sample.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	FAutoConsoleCommand StartCmd(TEXT("gs.WingmanAttack.QA.Start"),TEXT("Dedicated+two clients with temporary separated spawn fixtures."),FConsoleCommandDelegate::CreateStatic(&Start));
	FAutoConsoleCommand TargetCmd(TEXT("gs.WingmanAttack.QA.Target"),TEXT("Set a scoped runtime target: ground|air."),FConsoleCommandWithArgsDelegate::CreateStatic(&Target));
	FAutoConsoleCommand SampleCmd(TEXT("gs.WingmanAttack.QA.Sample"),TEXT("Read current attack phases, authority checkpoints and combat counters."),FConsoleCommandDelegate::CreateStatic(&Sample));
	FAutoConsoleCommand ViewCmd(TEXT("gs.WingmanAttack.QA.View"), TEXT("Frame one PIE client: <instance> [oblique|top|side]."), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1 || Args.Num() > 2) return;
		const FString Mode = Args.Num() == 2 ? Args[1] : TEXT("oblique");
		for (const auto& C : GEngine->GetWorldContexts())
		{
			UWorld* W = C.World();
			if (!W || W->WorldType != EWorldType::PIE || W->GetNetMode() != NM_Client || C.PIEInstance != FCString::Atoi(*Args[0])) continue;
			auto* PC = W->GetFirstPlayerController();
			auto* Relay = PC ? PC->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
			if (!Relay || !Relay->GetRelayState().AttackState.Target.IsValid()) continue;
			const auto& Target = Relay->GetRelayState().AttackState.Target;
			const FVector ShipLocation = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : Target.Location;
			const FVector CombatCenter = FMath::Lerp(Target.Location, ShipLocation, 0.45f);
			FVector Look = Target.bGround ? Target.Location + FVector(6000, 0, 7000) : CombatCenter;
			FVector Eye = Target.bGround ? Target.Location + FVector(4000, -33000, 15000) : CombatCenter + FVector(25000, -52000, 26000);
			if (Mode == TEXT("top")) { Eye = CombatCenter + FVector(0,0,52000); Look = CombatCenter; }
			else if (Mode == TEXT("side")) { Eye = CombatCenter + FVector(0,-52000,8000); Look = CombatCenter + FVector(0,0,4000); }
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->SetActorHiddenInGame(true);
				TInlineComponentArray<UWidgetComponent*> Widgets(Pawn);
				for (UWidgetComponent* Widget : Widgets) Widget->SetVisibility(false, true);
			}
			auto* Camera = W->SpawnActor<ACameraActor>(Eye, (Look - Eye).Rotation());
			if (Camera)
			{
				Camera->GetCameraComponent()->SetFieldOfView(75.0f);
				PC->SetViewTarget(Camera);
			}
		}
	}));
	FAutoConsoleCommand CaptureCmd(TEXT("gs.WingmanAttack.QA.Capture"), TEXT("Save the actual PIE client viewport: <label> <instance>."), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() != 2 || Args[0].IsEmpty()) return;
		for (TCHAR Ch : Args[0]) if (!FChar::IsAlnum(Ch) && Ch != TEXT('_') && Ch != TEXT('-')) return;
		for (const auto& C : GEngine->GetWorldContexts())
		{
			UWorld* W = C.World();
			if (!W || W->WorldType != EWorldType::PIE || W->GetNetMode() != NM_Client || C.PIEInstance != FCString::Atoi(*Args[1])) continue;
			auto* Client = W->GetGameViewport(); auto* Viewport = Client ? Client->Viewport : nullptr;
			TArray<FColor> Pixels;
			if (!Viewport || !GetViewportScreenShot(Viewport, Pixels)) continue;
			TArray64<uint8> PNG; const FIntPoint Size = Viewport->GetSizeXY();
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
			FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectDir() / TEXT("TestResults/WingmanAttack") / (Args[0] + TEXT(".png"))));
		}
	}));
}
#endif

struct FGuLiWingmanAttackCombatTestAccess
{
	static void Step(UGuLiCombatEffectRuntimeSubsystem* Runtime, FGuid Id, float Now)
	{ Runtime->StepProjectile(Id, 1.0f/30.0f, Now); }
};
namespace GuLiWingmanAttackCombatTests
{
	struct FTarget { FGuLiCombatTargetSnapshot Snapshot; int32 Hits=0; FGuLiDamageRequest Last; };
	struct FFixture
	{
		UWorld* World=nullptr; UGuLiDamageLedgerSubsystem* Ledger=nullptr; UGuLiCombatEffectRuntimeSubsystem* Runtime=nullptr;
		TArray<TUniquePtr<FTarget>> Targets;
		bool Initialize()
		{
			if (!GEngine) return false;
			World=UWorld::CreateWorld(EWorldType::Game,false); if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Ledger=World->GetSubsystem<UGuLiDamageLedgerSubsystem>(); Runtime=World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
			return Ledger && Runtime && Ledger->BeginServerEpoch(9);
		}
		~FFixture() { if(World) { World->DestroyWorld(false); if(GEngine) GEngine->DestroyWorldContext(World); } }
		FTarget& Add(EGuLiTeam Team, FVector Location)
		{
			auto Value=MakeUnique<FTarget>(); auto* Ptr=Value.Get();
			Ptr->Snapshot.Handle=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9,Targets.Num()+1);
			Ptr->Snapshot.Team=Team; Ptr->Snapshot.Location=Location; Ptr->Snapshot.Health=1000; Ptr->Snapshot.bAlive=true; Ptr->Snapshot.CollisionRadius=50;
			FGuLiCombatTargetAdapter Adapter; Adapter.LifetimeOwner=World;
			Adapter.ReadSnapshot=[Ptr](auto& Out) { Out=Ptr->Snapshot; return true; };
			Adapter.ApplyDamage=[Ptr](const FGuLiDamageRequest& Request,FGuLiDamageCommitResult& Out)
			{
				Ptr->Last=Request; ++Ptr->Hits; Out.AppliedDamage=FMath::Min(Request.Damage,Ptr->Snapshot.Health);
				Ptr->Snapshot.Health-=Out.AppliedDamage; Ptr->Snapshot.bAlive=Ptr->Snapshot.Health>0;
				Out.RemainingHealth=Ptr->Snapshot.Health; Out.bKilled=!Ptr->Snapshot.bAlive; return true;
			};
			check(Ledger->RegisterTarget(Ptr->Snapshot.Handle,MoveTemp(Adapter))); Targets.Add(MoveTemp(Value)); return *Ptr;
		}
		FGuLiCombatAttackRequest Request(FTarget& Source, FTarget& Target)
		{
			FGuLiCombatAttackRequest R; R.Context.Source=Source.Snapshot.Handle; R.Context.Target=Target.Snapshot.Handle;
			R.Context.Emitter.Flight.Group.ShipInstanceId=FGuid(1,2,3,4); R.Context.Emitter.Flight.Group.ShipGeneration=1;
			R.Context.Emitter.Flight.Group.GroupGeneration=1; R.Context.Emitter.Flight.FlightIndex=0;
			R.Context.Emitter.MemberIndex=0; R.Context.Emitter.EntityGeneration=1;
			R.Context.ShotId=FGuid::NewGuid(); R.Context.Damage=37; R.Context.SkillId=TEXT("Wingman.GroundMissile");
			R.TargetLocation=Target.Snapshot.Location; R.SourceTransform=FTransform(FRotator(-45,0,0),FVector(-6000,0,6000));
			R.ExecutorId=TEXT("WingmanGroundMissile"); R.Projectile=NewObject<UGuLiProjectileEffectDefinition>(World);
			auto* Field=NewObject<UGuLiSpellFieldDefinition>(World); R.Projectile->ImpactField=Field;
			R.FrozenField.ConfigId=TEXT("WingmanGroundMissile"); R.FrozenField.Damage=37; R.FrozenField.Radius=800;
			return R;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanFixedGroundMissileTest,"GuLiStrike.Wingman.Attack.FixedGroundAOEAndAttribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanFixedGroundMissileTest::RunTest(const FString&)
{
	using namespace GuLiWingmanAttackCombatTests;
	FFixture F; if (!TestTrue(TEXT("Authority fixture initializes"),F.Initialize())) return false;
	auto& Source=F.Add(EGuLiTeam::Red,FVector(-20000,0,10000));
	auto& Target=F.Add(EGuLiTeam::Blue,FVector::ZeroVector);
	auto& Neighbor=F.Add(EGuLiTeam::Blue,FVector(800,0,0));
	auto& Outside=F.Add(EGuLiTeam::Blue,FVector(851,0,0));
	auto& Friendly=F.Add(EGuLiTeam::Red,FVector::ZeroVector);
	auto Request=F.Request(Source,Target);
	TestTrue(TEXT("Validated point missile launches"),F.Runtime->ExecuteWingmanAttack(Request));
	TestEqual(TEXT("Launch never deals direct damage"),Target.Hits,0);
	TestFalse(TEXT("Same accepted shot cannot launch twice"),F.Runtime->ExecuteWingmanAttack(Request));
	FGuLiCombatEffectState State; F.Runtime->QueryEffect(Request.Context.ShotId,State);
	TestTrue(TEXT("Point mode bypasses upward lift"),State.bFixedPoint && State.Motion.LiftSeconds==0 && State.Velocity.Z<0);
	TestTrue(TEXT("First aim is downward from actual muzzle"),FVector(State.LastTargetLocation).Equals(FVector::ZeroVector));
	Target.Snapshot.Location=FVector(100000,0,0); Target.Snapshot.bAlive=false;
	F.Ledger->UnregisterTarget(Source.Snapshot.Handle,F.World); Source.Snapshot.bAlive=false;
	Request.Projectile->Motion.Speed=1; Request.FrozenField.Radius=1;
	for(int32 I=1;I<=90;++I) FGuLiWingmanAttackCombatTestAccess::Step(F.Runtime,Request.Context.ShotId,I/30.0f);
	TestEqual(TEXT("Moving/dead original target does not redirect the strip"),Target.Hits,0);
	TestEqual(TEXT("Frozen 800cm AOE damages neighbor exactly once"),Neighbor.Hits,1);
	TestEqual(TEXT("Outside and friendly targets remain untouched"),Outside.Hits+Friendly.Hits,0);
	TestEqual(TEXT("Source removal preserves frozen upgraded damage"),Neighbor.Last.Damage,37.0f);
	TestTrue(TEXT("Ledger retains emitting wingman identity"),Neighbor.Last.Emitter==Request.Context.Emitter);
	TestFalse(TEXT("Dead source cannot initiate another attack"),F.Runtime->ExecuteWingmanAttack(Request));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanGunLedgerTest,"GuLiStrike.Wingman.Attack.GunLedgerAndMuzzleCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanGunLedgerTest::RunTest(const FString&)
{
	using namespace GuLiWingmanAttackCombatTests;
	FFixture F; if (!F.Initialize()) return false;
	auto& Source=F.Add(EGuLiTeam::Red,FVector(-10000,0,0)); auto& Target=F.Add(EGuLiTeam::Blue,FVector::ZeroVector);
	auto R=F.Request(Source,Target); R.ExecutorId=TEXT("WingmanMachineGun"); R.MuzzleOffset=FVector(1200,0,0);
	int32 CueCount=0; FGuLiCombatShotCue Cue;
	F.Runtime->OnShots.AddLambda([&](const auto& Cues) { CueCount+=Cues.Num(); if(!Cues.IsEmpty()) Cue=Cues[0]; });
	TestTrue(TEXT("Accepted gun shot commits"),F.Runtime->ExecuteWingmanAttack(R));
	TestFalse(TEXT("Duplicate gun identity cannot recommit"),F.Runtime->ExecuteWingmanAttack(R));
	TestEqual(TEXT("One ledger hit and one cue"),Target.Hits,1); TestEqual(TEXT("One visual emission"),CueCount,1);
	TestTrue(TEXT("Muzzle attaches to wingman rather than ship"),Cue.Source==GuLiCombatTargets::MakeWingmanTargetHandle(R.Context.Emitter));
	TestTrue(TEXT("Muzzle origin respects logical aircraft rotation"),FVector(Cue.Start).Equals(R.SourceTransform.TransformPosition(R.MuzzleOffset),0.01));
	TestTrue(TEXT("Nose gun stops its muzzle effect promptly"),Cue.KeepAliveSeconds<=0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanTargetHysteresisAndGuardTest,
	"GuLiStrike.Wingman.Attack.TargetHysteresisAndGuardRejoin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanTargetHysteresisAndGuardTest::RunTest(const FString&)
{
	FGuLiWingmanTargetingTuning Tuning;
	TestTrue(TEXT("1500m edge is eligible for automatic acquisition"),
		GuLiWingmanTargeting::IsWithinAcquireRange(150000.0, Tuning));
	TestFalse(TEXT("Automatic acquisition stops beyond 1500m"),
		GuLiWingmanTargeting::IsWithinAcquireRange(150001.0, Tuning));
	TestTrue(TEXT("An automatic target is retained through the 1800m edge"),
		GuLiWingmanTargeting::IsWithinReleaseRange(180000.0, Tuning));
	TestFalse(TEXT("An automatic target is released beyond 1800m"),
		GuLiWingmanTargeting::IsWithinReleaseRange(180001.0, Tuning));
	TestTrue(TEXT("A new specified target inside 1800m can immediately override return lock"),
		GuLiWingmanTargeting::IsWithinReleaseRange(170000.0, Tuning));
	TestFalse(TEXT("A specified target request beyond 1800m is rejected"),
		GuLiWingmanTargeting::IsWithinReleaseRange(180001.0, Tuning));

	TArray<FGuLiWingmanGuardPoseObservation> Poses;
	Poses.SetNum(25);
	for (int32 Index = 0; Index < Poses.Num(); ++Index)
	{
		Poses[Index].bAlive = true; Poses[Index].bHasFreshAcceptedPose = true;
		Poses[Index].Position = FVector(Index < 20 ? 59000.0 : 80000.0, 0, 0);
	}
	TestTrue(TEXT("Ceil 80 percent inside 600m and nobody beyond 900m completes rejoin"),
		GuLiWingmanTargeting::IsGuardRejoinComplete(Poses, FVector::ZeroVector, 60000, 90000, 0.8f));
	Poses[19].Position.X = 61000;
	TestFalse(TEXT("Nineteen of twenty-five does not meet the ceil 80 percent threshold"),
		GuLiWingmanTargeting::IsGuardRejoinComplete(Poses, FVector::ZeroVector, 60000, 90000, 0.8f));
	Poses[19].Position.X = 59000; Poses[24].Position.X = 90001;
	TestFalse(TEXT("Any living wingman beyond 900m blocks rejoin"),
		GuLiWingmanTargeting::IsGuardRejoinComplete(Poses, FVector::ZeroVector, 60000, 90000, 0.8f));
	Poses[24].Position.X = 80000; Poses[0].bHasFreshAcceptedPose = false;
	TestFalse(TEXT("A missing or older-than-one-second Accepted Pose blocks rejoin"),
		GuLiWingmanTargeting::IsGuardRejoinComplete(Poses, FVector::ZeroVector, 60000, 90000, 0.8f));
	Poses[0].bAlive = false; Poses[20].Position.X = 59000;
	TestTrue(TEXT("Dead members are removed from the surviving-member threshold"),
		GuLiWingmanTargeting::IsGuardRejoinComplete(Poses, FVector::ZeroVector, 60000, 90000, 0.8f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanServerGunGateTest,
	"GuLiStrike.Wingman.Attack.ServerGunGeometryLosAndCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanServerGunGateTest::RunTest(const FString&)
{
	FGuLiCombatTargetSnapshot Target;
	Target.Handle = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9, 77);
	Target.bAlive = true; Target.Location = FVector(150000,0,0); Target.CollisionRadius = 0;
	TestTrue(TEXT("Live nose-on target at 1500m with LOS and ready cooldown is accepted"),
		GuLiWingmanAttackAuthority::IsGunShotEligible(FVector::ZeroVector, FVector::ForwardVector,
			Target, 150000, 20, true, 10.0, 10.0));
	Target.Location.X = 150001;
	TestFalse(TEXT("Server rejects a target beyond machine-gun range"),
		GuLiWingmanAttackAuthority::IsGunShotEligible(FVector::ZeroVector, FVector::ForwardVector,
			Target, 150000, 20, true, 10.0, 10.0));
	Target.Location = FVector(100000,100000,0);
	TestFalse(TEXT("Server rejects a target outside the 20-degree forward cone"),
		GuLiWingmanAttackAuthority::IsGunShotEligible(FVector::ZeroVector, FVector::ForwardVector,
			Target, 150000, 20, true, 10.0, 10.0));
	Target.Location = FVector(100000,0,0);
	TestFalse(TEXT("Server rejects blocked line of sight"),
		GuLiWingmanAttackAuthority::IsGunShotEligible(FVector::ZeroVector, FVector::ForwardVector,
			Target, 150000, 20, false, 10.0, 10.0));
	TestFalse(TEXT("Server rejects a shot before its 0.5-second cooldown is ready"),
		GuLiWingmanAttackAuthority::IsGunShotEligible(FVector::ZeroVector, FVector::ForwardVector,
			Target, 150000, 20, true, 10.0, 10.5));
	return true;
}
#endif
