#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
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
	const FGuLiWingmanAttackTarget* FindRepresentativeTarget(
		const FGuLiWingmanAttackAuthorityState& State)
	{
		if (State.Target.IsValid() && State.Target.bSpecified)
		{
			return &State.Target;
		}
		return State.AutomaticTargets.IsEmpty() ? nullptr : &State.AutomaticTargets[0].Target;
	}
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
				if(Ships>=2 && W->GetTimeSeconds()>5.0f) { Target({TEXT("air")}); return false; }
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
			W->WriteArrayStart(TEXT("connections"));
			if (const UNetDriver* Driver = World->GetNetDriver())
			{
				TArray<UNetConnection*> Connections;
				if (Driver->ServerConnection) Connections.Add(Driver->ServerConnection);
				for (UNetConnection* Connection : Driver->ClientConnections) Connections.Add(Connection);
				for (const UNetConnection* Connection : Connections)
				{
					W->WriteObjectStart();
					W->WriteValue(TEXT("name"), Connection->GetName());
					W->WriteValue(TEXT("budget_bytes_s"), Connection->CurrentNetSpeed);
					W->WriteValue(TEXT("in_bytes_s"), Connection->InBytesPerSecond);
					W->WriteValue(TEXT("out_bytes_s"), Connection->OutBytesPerSecond);
					W->WriteValue(TEXT("in_total_bytes"), Connection->InTotalBytes);
					W->WriteValue(TEXT("out_total_bytes"), Connection->OutTotalBytes);
					W->WriteValue(TEXT("queued_bits"), Connection->QueuedBits);
					W->WriteValue(TEXT("in_lost"), Connection->InTotalPacketsLost);
					W->WriteValue(TEXT("out_lost"), Connection->OutTotalPacketsLost);
					W->WriteObjectEnd();
				}
			}
			W->WriteArrayEnd();
			if(auto* Effects=World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>())
			{
				const auto V=Effects->GetCounters(); W->WriteValue(TEXT("missiles"),V.ProjectilesLaunched);
				W->WriteValue(TEXT("gun_bursts"),V.GunBurstsStarted);
				W->WriteValue(TEXT("logical_gun_shots"),V.LogicalGunShots);
				W->WriteValue(TEXT("network_shot_cues"),V.ShotsPublished);
				W->WriteValue(TEXT("damage"),V.DamageCommits);
			}
			if(auto* Presentation=World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
			{
				const auto V=Presentation->GetCounters();
				W->WriteValue(TEXT("received_effect_states"),V.ReceivedStates);
				W->WriteValue(TEXT("received_network_shots"),V.ReceivedShots);
				W->WriteValue(TEXT("synthesized_gun_shots"),V.SynthesizedGunShots);
			}
			W->WriteArrayStart(TEXT("relays"));
			for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
				if(auto* PC=It->Get()) if(auto* Relay=PC->FindComponentByClass<UGuLiWingmanRelayComponent>())
				{
					const auto& S=Relay->GetRelayState(); W->WriteObjectStart(); W->WriteValue(TEXT("owner"),PC->GetName());
					W->WriteValue(TEXT("lifecycle"),int32(S.Lease.Lifecycle)); W->WriteValue(TEXT("config_valid"),S.AbilityConfig.IsUsableByLeaseOwner());
					const FGuLiWingmanAttackTarget* RepresentativeTarget = FindRepresentativeTarget(S.AttackState);
					W->WriteValue(TEXT("accepted"),int64(S.LastAcceptedCandidateSequence));
					W->WriteValue(TEXT("automatic_target_count"), S.AttackState.AutomaticTargets.Num());
					W->WriteValue(TEXT("target"), RepresentativeTarget ? RepresentativeTarget->Target.LocalId : 0u);
					W->WriteValue(TEXT("target_location"), RepresentativeTarget ? RepresentativeTarget->Location.ToString() : FVector::ZeroVector.ToString());
					W->WriteValue(TEXT("ship_location"),PC->GetPawn()?PC->GetPawn()->GetActorLocation().ToString():FVector::ZeroVector.ToString());
					W->WriteValue(TEXT("normal_reject"),int32(Relay->GetListenSmokeLastNormalResultRejectReason()));
					W->WriteValue(TEXT("atomic_reject"),int32(Relay->GetListenSmokeLastAtomicResultRejectReason()));
					W->WriteValue(TEXT("server_motion_writes"),Relay->GetServerRelay()?int64(Relay->GetServerRelay()->GetServerWingmanMovementWriteCount()):-1);
					W->WriteArrayStart(TEXT("accepted_flights"));
					if (const auto* Server = Relay->GetServerRelay())
					{
						const auto& History = Server->GetAcceptedHistory();
						for (uint8 Flight = 0; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
						{
							const int32 Last = History.FindLastByPredicate([Flight](const auto& B) { return B.FlightIndex == Flight; });
							if (Last == INDEX_NONE) continue;
							const auto& Batch = History[Last];
							W->WriteObjectStart(); W->WriteValue(TEXT("flight"), Flight);
							W->WriteValue(TEXT("frame"), int64(Batch.FrameSequence));
							W->WriteValue(TEXT("tick"), int64(Batch.StateRef.ClientSimTick));
							W->WriteValue(TEXT("receipt_time"), Batch.ServerAcceptedTimeSeconds);
							W->WriteObjectEnd();
						}
					}
					W->WriteArrayEnd();
					const auto& B=Relay->GetLastClientBootstrap();
					W->WriteValue(TEXT("bootstrap_valid"),B.IsWellFormed());
					W->WriteValue(TEXT("attack_hash_valid"),B.AttackStateHash==B.AttackState.ComputeStableHash());
					W->WriteValue(TEXT("ground"), RepresentativeTarget && RepresentativeTarget->bGround);
					W->WriteArrayStart(TEXT("automatic_targets"));
					for (const FGuLiWingmanAutoTargetAssignment& Assignment : S.AttackState.AutomaticTargets)
					{
						W->WriteObjectStart();
						W->WriteValue(TEXT("member"), Assignment.Emitter.GetGroupMemberIndex());
						W->WriteValue(TEXT("target"), Assignment.Target.Target.LocalId);
						W->WriteValue(TEXT("revision"), int64(Assignment.Target.Revision));
						W->WriteValue(TEXT("location"), Assignment.Target.Location.ToString());
						W->WriteObjectEnd();
					}
					W->WriteArrayEnd();
					W->WriteArrayStart(TEXT("checkpoints"));
					for(const auto& P:S.AttackState.Checkpoints) { W->WriteObjectStart(); W->WriteValue(TEXT("member"),P.Emitter.GetGroupMemberIndex()); W->WriteValue(TEXT("slot"),P.SlotId.ToString()); W->WriteValue(TEXT("shot"),P.LastShotIndex); W->WriteValue(TEXT("run"),P.RunId); W->WriteObjectEnd(); }
					W->WriteArrayEnd(); W->WriteObjectEnd();
				}
			W->WriteArrayEnd(); W->WriteArrayStart(TEXT("planes"));
			if(auto* Sim=World->GetSubsystem<UGuLiWingmanSimulationSubsystem>())
			{
				TArray<FGuLiWingmanAttackDiagnostic> Entries; Sim->GetAttackDiagnostics(Entries);
				for(const auto& D:Entries)
				{
					W->WriteObjectStart(); W->WriteValue(TEXT("member"),D.Wingman.GetGroupMemberIndex()); W->WriteValue(TEXT("phase"),D.Phase);
					W->WriteValue(TEXT("flight_mode"), D.FlightMode);
					W->WriteValue(TEXT("target"), D.Target.Target.LocalId);
					W->WriteValue(TEXT("target_location"), D.Target.Location.ToString());
					W->WriteValue(TEXT("target_ground"), D.Target.bGround);
					W->WriteValue(TEXT("slot"), D.SlotId.ToString());
					W->WriteValue(TEXT("position"),D.Position.ToString()); W->WriteValue(TEXT("forward"),D.Forward.ToString()); W->WriteValue(TEXT("entry"),D.Entry.ToString());
					W->WriteValue(TEXT("target_distance"),D.TargetDistance);
					W->WriteValue(TEXT("carrier_distance"),D.CarrierDistance);
					W->WriteValue(TEXT("desired"),D.PreferredVelocity.ToString()); W->WriteValue(TEXT("guiding"),D.bGuiding); W->WriteValue(TEXT("next_shot"),D.NextShot);
					W->WriteValue(TEXT("cancel_reason"),D.CancelReason);
					W->WriteValue(TEXT("ground_path_failure_mask"), D.GroundPathFailureMask);
					W->WriteValue(TEXT("ground_navigation_failure_mask"), D.GroundNavigationFailureMask);
					W->WriteValue(TEXT("run"),D.RunId);
					W->WriteValue(TEXT("completed_ground_runs"), int64(D.CompletedGroundRuns));
					W->WriteValue(TEXT("start"),D.StartTime);
					W->WriteValue(TEXT("phase_start"),D.PhaseStartTime);
					W->WriteValue(TEXT("blocked_seconds"),D.ConsecutiveBlockedSeconds);
					W->WriteValue(TEXT("controlled_recovery"),D.bControlledRecovery);
					W->WriteObjectEnd();
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
			if (!Relay) continue;
			const FGuLiWingmanAttackTarget* ResolvedTarget =
				FindRepresentativeTarget(Relay->GetRelayState().AttackState);
			if (!ResolvedTarget) continue;
			const FGuLiWingmanAttackTarget& Target = *ResolvedTarget;
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
	static void StepGun(UGuLiCombatEffectRuntimeSubsystem* Runtime, FGuid Id, float Now)
	{
		Runtime->StepSustainedHitscan(Id, Now);
		Runtime->GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>()->Step(Now);
	}
	static void StepCooldowns(UGuLiCombatEffectRuntimeSubsystem* Runtime, float Now)
	{ Runtime->StepWingmanBurstCooldowns(Now); }
	static void DrainGunProjectiles(UWorld* World, float BurstEnd, float Lifetime)
	{
		auto* Pool = World->GetSubsystem<UGuLiProjectilePoolSubsystem>();
		for (float Now = BurstEnd + 1.0f / 30.0f; Now <= BurstEnd + Lifetime + 1.0f / 30.0f; Now += 1.0f / 30.0f)
			Pool->Step(Now);
	}
	static bool HasCooldown(UGuLiCombatEffectRuntimeSubsystem* Runtime,
		const FGuLiWingmanHandle& Emitter, FName Slot)
	{
		const auto* BySlot = Runtime->WingmanBurstCooldowns.Find(Emitter);
		return BySlot && BySlot->Contains(Slot);
	}
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
		FTarget& AddHandle(FGuLiTargetHandle Handle, EGuLiTeam Team, FVector Location, float Health = 1000.0f)
		{
			auto Value=MakeUnique<FTarget>(); auto* Ptr=Value.Get();
			Ptr->Snapshot.Handle=Handle;
			Ptr->Snapshot.Team=Team; Ptr->Snapshot.Location=Location; Ptr->Snapshot.Health=Health; Ptr->Snapshot.bAlive=true; Ptr->Snapshot.CollisionRadius=50;
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
		FTarget& Add(EGuLiTeam Team, FVector Location, float Health = 1000.0f)
		{
			return AddHandle(GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9,Targets.Num()+1), Team, Location, Health);
		}
		FTarget& AddWingman(const FGuLiWingmanHandle& Emitter, EGuLiTeam Team, FVector Location, float Health = 1000.0f)
		{
			return AddHandle(GuLiCombatTargets::MakeWingmanTargetHandle(Emitter), Team, Location, Health);
		}
		FTarget& AddShip(EGuLiTeam Team, FVector Location, float Health = 1000.0f)
		{
			FGuLiTargetHandle Handle; Handle.Kind=EGuLiTargetKind::Ship;
			Handle.AuthorityId=FGuid(9,Targets.Num()+1,17,23); Handle.Generation=1;
			return AddHandle(Handle, Team, Location, Health);
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
			R.FrozenField.ConfigId=TEXT("WingmanGroundMissile"); R.FrozenField.Damage=37; R.FrozenField.Radius=4000;
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
	auto& Neighbor=F.Add(EGuLiTeam::Blue,FVector(4000,0,0));
	auto& Outside=F.Add(EGuLiTeam::Blue,FVector(4051,0,0));
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
	TestEqual(TEXT("Frozen 4000cm AOE damages neighbor exactly once"),Neighbor.Hits,1);
	TestEqual(TEXT("Outside and friendly targets remain untouched"),Outside.Hits+Friendly.Hits,0);
	TestEqual(TEXT("Source removal preserves frozen upgraded damage"),Neighbor.Last.Damage,37.0f);
	TestTrue(TEXT("Ledger retains emitting wingman identity"),Neighbor.Last.Emitter==Request.Context.Emitter);
	TestFalse(TEXT("Dead source cannot initiate another attack"),F.Runtime->ExecuteWingmanAttack(Request));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanGunLedgerTest,"GuLiStrike.Wingman.Attack.SustainedGunLedgerAndState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanGunLedgerTest::RunTest(const FString&)
{
	using namespace GuLiWingmanAttackCombatTests;
	FFixture F; if (!F.Initialize()) return false;
	auto& Carrier=F.AddShip(EGuLiTeam::Red,FVector::ZeroVector,10000);
	auto& Target=F.AddShip(EGuLiTeam::Blue,FVector(110000,0,0),10000);
	auto R=F.Request(Carrier,Target); R.ExecutorId=TEXT("WingmanMachineGun");
	R.Context.SkillId=TEXT("Wingman.MachineGun"); R.Context.Damage=10;
	R.Context.WeaponBinding.SlotId=TEXT("AirWeapon"); R.MuzzleOffset=FVector(1200,0,0);
	R.SourceTransform=FTransform(FRotator::ZeroRotator,FVector(100000,0,0));
	auto& Emitter=F.AddWingman(R.Context.Emitter,EGuLiTeam::Red,R.SourceTransform.GetLocation(),1000);
	int32 StateBoundaries=0; int32 NetworkShotCues=0; FGuLiCombatEffectState Started;
	F.Runtime->OnState.AddLambda([&](const FGuLiCombatEffectState& State, bool bReliable)
	{
		if (bReliable && State.Kind==EGuLiCombatEffectKind::SustainedHitscan)
		{
			++StateBoundaries; if (State.Phase!=EGuLiCombatEffectPhase::Finished) Started=State;
		}
	});
	F.Runtime->OnShots.AddLambda([&](const auto& Cues) { NetworkShotCues+=Cues.Num(); });
	const FGuid Burst=F.Runtime->StartWingmanGunBurst(R,0.2f,5.0f,5000.0f,52000.0f,3.0f);
	TestTrue(TEXT("One accepted record starts one sustained burst"),Burst.IsValid());
	TestFalse(TEXT("Duplicate record cannot start another burst"),
		F.Runtime->StartWingmanGunBurst(R,0.2f,5.0f,5000.0f,52000.0f,3.0f).IsValid());
	TestTrue(TEXT("Reliable start state identifies wingman, target, muzzle and five-hertz cadence"),
		Started.IsWellFormed() && Started.Source==GuLiCombatTargets::MakeWingmanTargetHandle(R.Context.Emitter)
		&& Started.Target==R.Context.Target && Started.SlotId==TEXT("AirWeapon")
		&& Started.MuzzleOffset.Equals(R.MuzzleOffset) && FMath::IsNearlyEqual(Started.FireRateHz,5.0f));
	for(int32 Step=1;Step<=150;++Step)
		FGuLiWingmanAttackCombatTestAccess::StepGun(F.Runtime,Burst,Step/30.0f);
	FGuLiWingmanAttackCombatTestAccess::DrainGunProjectiles(F.World, 5.0f, R.Motion.MaximumLifetime);
	const auto FiveHzCounters=F.Runtime->GetCounters();
	TestEqual(TEXT("Five-hertz full burst produces exactly 25 logical shots"),FiveHzCounters.LogicalGunShots,25ll);
	TestEqual(TEXT("Five-hertz full burst deals exactly 250 damage"),Target.Snapshot.Health,9750.0f);
	TestEqual(TEXT("Burst publishes only reliable start and end boundaries"),StateBoundaries,2);
	TestEqual(TEXT("Burst publishes no per-shot network cues"),NetworkShotCues,0);
	TestTrue(TEXT("Return cooldown exists after the burst"),
		FGuLiWingmanAttackCombatTestAccess::HasCooldown(F.Runtime,R.Context.Emitter,TEXT("AirWeapon")));
	FGuLiWingmanAttackCombatTestAccess::StepCooldowns(F.Runtime,10.0f);
	TestTrue(TEXT("Cooldown does not count while the wingman remains outside 520m"),
		FGuLiWingmanAttackCombatTestAccess::HasCooldown(F.Runtime,R.Context.Emitter,TEXT("AirWeapon")));
	Emitter.Snapshot.Location=FVector(52001,0,0);
	FGuLiWingmanAttackCombatTestAccess::StepCooldowns(F.Runtime,20.0f);
	TestTrue(TEXT("Crossing only near the orbit boundary still does not start cooldown"),
		FGuLiWingmanAttackCombatTestAccess::HasCooldown(F.Runtime,R.Context.Emitter,TEXT("AirWeapon")));
	Emitter.Snapshot.Location=FVector(52000,0,0);
	FGuLiWingmanAttackCombatTestAccess::StepCooldowns(F.Runtime,30.0f);
	FGuLiWingmanAttackCombatTestAccess::StepCooldowns(F.Runtime,32.999f);
	TestTrue(TEXT("Cooldown remains active before three complete in-orbit seconds"),
		FGuLiWingmanAttackCombatTestAccess::HasCooldown(F.Runtime,R.Context.Emitter,TEXT("AirWeapon")));
	FGuLiWingmanAttackCombatTestAccess::StepCooldowns(F.Runtime,33.0f);
	TestFalse(TEXT("Cooldown completes after three in-orbit seconds"),
		FGuLiWingmanAttackCombatTestAccess::HasCooldown(F.Runtime,R.Context.Emitter,TEXT("AirWeapon")));

	FFixture F30; if (!F30.Initialize()) return false;
	auto& Carrier30=F30.AddShip(EGuLiTeam::Red,FVector::ZeroVector,10000);
	auto& Target30=F30.AddShip(EGuLiTeam::Blue,FVector(110000,0,0),10000);
	auto R30=F30.Request(Carrier30,Target30); R30.ExecutorId=TEXT("WingmanMachineGun");
	R30.Context.SkillId=TEXT("Wingman.MachineGun"); R30.Context.Damage=10;
	R30.Context.WeaponBinding.SlotId=TEXT("AirWeapon"); R30.SourceTransform=FTransform(FVector(100000,0,0));
	F30.AddWingman(R30.Context.Emitter,EGuLiTeam::Red,R30.SourceTransform.GetLocation(),1000);
	TestFalse(TEXT("A rate above 30Hz is rejected instead of silently corrected"),
		F30.Runtime->StartWingmanGunBurst(R30,0.03f,5.0f,5000.0f,52000.0f,3.0f).IsValid());
	const FGuid Burst30=F30.Runtime->StartWingmanGunBurst(R30,GuLiWingmanAttack::MinimumAirShotIntervalSeconds,
		5.0f,5000.0f,52000.0f,3.0f);
	for(int32 Step=1;Step<=150;++Step)
		FGuLiWingmanAttackCombatTestAccess::StepGun(F30.Runtime,Burst30,Step/30.0f);
	// The last rounds remain in flight after the firing segment ends.
	FGuLiWingmanAttackCombatTestAccess::DrainGunProjectiles(F30.World, 5.0f, R30.Motion.MaximumLifetime);
	TestEqual(TEXT("Thirty-hertz full burst produces exactly 150 logical shots"),
		F30.Runtime->GetCounters().LogicalGunShots,150ll);
	TestEqual(TEXT("Thirty-hertz full burst deals exactly 1500 damage"),Target30.Snapshot.Health,8500.0f);

	auto& LostTarget=F30.AddShip(EGuLiTeam::Blue,FVector(210000,0,0),1000);
	auto Lost=F30.Request(Carrier30,LostTarget); Lost.ExecutorId=TEXT("WingmanMachineGun");
	Lost.Context.SkillId=TEXT("Wingman.MachineGun"); Lost.Context.Damage=10;
	Lost.Context.WeaponBinding.SlotId=TEXT("AirWeapon"); Lost.Context.Emitter.MemberIndex=1;
	Lost.SourceTransform=FTransform(FVector(200000,0,0));
	F30.AddWingman(Lost.Context.Emitter,EGuLiTeam::Red,Lost.SourceTransform.GetLocation(),1000);
	const FGuid LostBurst=F30.Runtime->StartWingmanGunBurst(Lost,0.2f,5.0f,5000.0f,52000.0f,3.0f);
	F30.Ledger->UnregisterTarget(LostTarget.Snapshot.Handle,F30.World);
	FGuLiWingmanAttackCombatTestAccess::StepGun(F30.Runtime,LostBurst,0.1f);
	FGuLiCombatEffectState Query;
	TestFalse(TEXT("A missing target stops its active burst"),F30.Runtime->QueryEffect(LostBurst,Query));

	auto& DeadTarget=F30.AddShip(EGuLiTeam::Blue,FVector(310000,0,0),1000);
	auto Dead=F30.Request(Carrier30,DeadTarget); Dead.ExecutorId=TEXT("WingmanMachineGun");
	Dead.Context.SkillId=TEXT("Wingman.MachineGun"); Dead.Context.Damage=10;
	Dead.Context.WeaponBinding.SlotId=TEXT("AirWeapon"); Dead.Context.Emitter.MemberIndex=2;
	Dead.SourceTransform=FTransform(FVector(300000,0,0));
	F30.AddWingman(Dead.Context.Emitter,EGuLiTeam::Red,Dead.SourceTransform.GetLocation(),1000);
	const FGuid DeadBurst=F30.Runtime->StartWingmanGunBurst(Dead,0.2f,5.0f,5000.0f,52000.0f,3.0f);
	DeadTarget.Snapshot.bAlive=false;
	FGuLiWingmanAttackCombatTestAccess::StepGun(F30.Runtime,DeadBurst,0.1f);
	TestFalse(TEXT("A dead target stops its active burst"),F30.Runtime->QueryEffect(DeadBurst,Query));

	auto& SelfTarget=F30.AddShip(EGuLiTeam::Blue,FVector(410000,0,0),1000);
	auto Self=F30.Request(Carrier30,SelfTarget); Self.ExecutorId=TEXT("WingmanMachineGun");
	Self.Context.SkillId=TEXT("Wingman.MachineGun"); Self.Context.Damage=10;
	Self.Context.WeaponBinding.SlotId=TEXT("AirWeapon"); Self.Context.Emitter.MemberIndex=3;
	Self.SourceTransform=FTransform(FVector(400000,0,0));
	auto& SelfEmitter=F30.AddWingman(Self.Context.Emitter,EGuLiTeam::Red,Self.SourceTransform.GetLocation(),1000);
	const FGuid SelfBurst=F30.Runtime->StartWingmanGunBurst(Self,0.2f,5.0f,5000.0f,52000.0f,3.0f);
	SelfEmitter.Snapshot.bAlive=false;
	FGuLiWingmanAttackCombatTestAccess::StepGun(F30.Runtime,SelfBurst,0.1f);
	TestFalse(TEXT("A dead wingman immediately clears its active burst"),F30.Runtime->QueryEffect(SelfBurst,Query));

	auto& CloseTarget=F30.AddShip(EGuLiTeam::Blue,FVector(510000,0,0),1000);
	auto Close=F30.Request(Carrier30,CloseTarget); Close.ExecutorId=TEXT("WingmanMachineGun");
	Close.Context.SkillId=TEXT("Wingman.MachineGun"); Close.Context.Damage=10;
	Close.Context.WeaponBinding.SlotId=TEXT("AirWeapon"); Close.Context.Emitter.MemberIndex=4;
	Close.SourceTransform=FTransform(FVector(500000,0,0));
	auto& CloseEmitter=F30.AddWingman(Close.Context.Emitter,EGuLiTeam::Red,Close.SourceTransform.GetLocation(),1000);
	const FGuid CloseBurst=F30.Runtime->StartWingmanGunBurst(Close,0.2f,5.0f,5000.0f,52000.0f,3.0f);
	CloseEmitter.Snapshot.Location=FVector(505001,0,0);
	FGuLiWingmanAttackCombatTestAccess::StepGun(F30.Runtime,CloseBurst,0.1f);
	TestFalse(TEXT("Strictly below 50m stops the active burst"),F30.Runtime->QueryEffect(CloseBurst,Query));
	F30.Runtime->CancelWingmanGunBurst(Close.Context.Emitter,true);
	CloseEmitter.Snapshot.Location=FVector(500000,0,0); Close.Context.ShotId=FGuid::NewGuid();
	const FGuid SwitchedBurst=F30.Runtime->StartWingmanGunBurst(Close,0.2f,5.0f,5000.0f,52000.0f,3.0f);
	TestEqual(TEXT("Target switching cancels the old segment immediately"),
		F30.Runtime->CancelWingmanGunBurst(Close.Context.Emitter),1);
	TestFalse(TEXT("The switched-away segment is no longer active"),F30.Runtime->QueryEffect(SwitchedBurst,Query));
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
	"GuLiStrike.Wingman.Attack.ServerAirBurstStartBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanServerGunGateTest::RunTest(const FString&)
{
	FGuLiCombatTargetSnapshot Target;
	Target.Handle.Kind=EGuLiTargetKind::Ship; Target.Handle.AuthorityId=FGuid(1,7,7,7);
	Target.Handle.Generation=1; Target.bAlive=true; Target.Location=FVector(10000,0,0);
	TestTrue(TEXT("Exactly 100m starts an air burst"),
		GuLiWingmanAttackAuthority::IsAirBurstStartEligible(FVector::ZeroVector,Target,10000));
	Target.Location=FVector(9999,0,0);
	TestFalse(TEXT("Below 100m must separate before starting"),
		GuLiWingmanAttackAuthority::IsAirBurstStartEligible(FVector::ZeroVector,Target,10000));
	Target.Location=FVector(-200000,0,0);
	TestTrue(TEXT("A rear target beyond the old range still starts; facing, range, LOS, Recover and avoidance are not inputs"),
		GuLiWingmanAttackAuthority::IsAirBurstStartEligible(FVector::ZeroVector,Target,10000));
	Target.bAlive=false;
	TestFalse(TEXT("A dead target cannot start a burst"),
		GuLiWingmanAttackAuthority::IsAirBurstStartEligible(FVector::ZeroVector,Target,10000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiReusableTeamOutlineTest,
	"GuLiStrike.Wingman.Attack.ReusableTeamOutline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiReusableTeamOutlineTest::RunTest(const FString&)
{
	GuLiWingmanAttackCombatTests::FFixture F;
	if (!F.Initialize()) return false;
	AActor* Unit = F.World->SpawnActor<AActor>();
	auto* StaticMesh = NewObject<UStaticMeshComponent>(Unit);
	auto* SkeletalMesh = NewObject<USkeletalMeshComponent>(Unit);
	auto* Outline = NewObject<UGuLiTeamOutlineComponent>(Unit);
	Unit->AddInstanceComponent(StaticMesh);
	Unit->AddInstanceComponent(SkeletalMesh);
	Unit->AddInstanceComponent(Outline);
	for (EGuLiTeam Team : {EGuLiTeam::Red, EGuLiTeam::Blue})
	{
		Outline->SetOutlineTeam(Team);
		for (UPrimitiveComponent* Mesh : {static_cast<UPrimitiveComponent*>(StaticMesh),
			static_cast<UPrimitiveComponent*>(SkeletalMesh)})
		{
			TestTrue(TEXT("Both static and skeletal unit meshes write the team mask"), Mesh->bRenderCustomDepth);
			TestEqual(TEXT("Stencil is the unit team, not a hard-coded enemy flag"),
				Mesh->CustomDepthStencilValue, static_cast<int32>(Team));
		}
	}
	auto* NewPart = NewObject<UStaticMeshComponent>(Unit);
	Unit->AddInstanceComponent(NewPart);
	Outline->RefreshMeshes();
	TestTrue(TEXT("A newly installed unit part joins the same outline mask"), NewPart->bRenderCustomDepth);
	TestEqual(TEXT("The new part inherits its unit's current team"),
		NewPart->CustomDepthStencilValue, static_cast<int32>(EGuLiTeam::Blue));
	Outline->SetOutlineTeam(EGuLiTeam::Unassigned);
	TestFalse(TEXT("Pool reset clears the static mask"), StaticMesh->bRenderCustomDepth);
	TestFalse(TEXT("Pool reset clears the skeletal mask"), SkeletalMesh->bRenderCustomDepth);
	TestFalse(TEXT("Pool reset clears later-added parts"), NewPart->bRenderCustomDepth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiRemoteWingmanModelCorrectionTest,
	"GuLiStrike.Wingman.Attack.RemoteModelCorrectionIsVisualOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiRemoteWingmanModelCorrectionTest::RunTest(const FString&)
{
	GuLiWingmanAttackCombatTests::FFixture F;
	if (!F.Initialize()) return false;
	FGuLiWingmanHandle Handle;
	Handle.Flight.Group.ShipInstanceId = FGuid(1, 2, 3, 4);
	Handle.Flight.Group.ShipGeneration = 1;
	Handle.Flight.Group.GroupGeneration = 1;
	Handle.Flight.FlightIndex = 0;
	Handle.MemberIndex = 0;
	Handle.EntityGeneration = 1;
	auto* Pawn = F.World->SpawnActor<AGuLiWingmanPawn>();
	TestTrue(TEXT("Remote visual fixture initializes"), Pawn->InitializeRemotePresentation(Handle, nullptr));
	const FTransform First(FVector(1000, 0, 0));
	const FTransform Next(FVector(3000, 0, 0));
	Pawn->ApplyRemotePresentation(First, 1, true, false);
	TestTrue(TEXT("First pose establishes the model at its actual spawn position"),
		Pawn->GetPresentationTransform().Equals(First));
	Pawn->ApplyRemotePresentation(Next, 1, true, false);
	TestTrue(TEXT("The accepted root immediately contains the new pose"), Pawn->GetActorTransform().Equals(Next));
	TestFalse(TEXT("The rendered model does not snap to a corrected endpoint"),
		Pawn->GetPresentationTransform().Equals(Next));
	Pawn->ApplyRemotePresentation(Next, 1, true, true);
	TestTrue(TEXT("An explicit gameplay displacement establishes a new visual origin"),
		Pawn->GetPresentationTransform().Equals(Next));
	Pawn->ResetForPool();
	++Handle.EntityGeneration;
	Pawn->InitializeRemotePresentation(Handle, nullptr);
	Pawn->ApplyRemotePresentation(First, 1, true, false);
	TestTrue(TEXT("A new pooled identity does not blend from the old unit"),
		Pawn->GetPresentationTransform().Equals(First));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAllDeadRosterCutTest,
	"GuLiStrike.Wingman.Attack.AllDeadRosterCutIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiWingmanAllDeadRosterCutTest::RunTest(const FString&)
{
	GuLiWingmanAttackCombatTests::FFixture F;
	if (!F.Initialize()) return false;
	AActor* Ship = F.World->SpawnActor<AActor>();
	auto* ASC = NewObject<UGuLiShipHangarCapabilityComponent>(Ship);
	Ship->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitializeShipActorInfo(Ship);
			ASC->SetCapabilityEnabled(true);
	FGuLiShipAbilityProjectionContext Projection;
	Projection.ShipInstanceId = FGuid(10, 20, 30, 40);
	Projection.MatchEpoch = 9;
	Projection.Team = EGuLiTeam::Red;
	Projection.OwnerPlayerGuid = FGuid(1, 2, 3, 4);
	Projection.WingmanTypeId = GuLiGetDefaultWingmanTypeId();
	Projection.ShipGeneration = 1;
	Projection.GroupGeneration = 1;
	Projection.FormationCommandRevision = 1;
	Projection.EffectiveClientSimTick = 1;
	TestTrue(TEXT("Native projection initializes"), ASC->SetProjectionContext(Projection));
	FString Error;
	TestTrue(TEXT("Native V3 abilities initialize"), ASC->ServerApplyAbilitySet(
		UGuLiShipAbilitySet::CreateNativeV3Transient(ASC), FGuLiShipAbilityLoadoutState::MakeNativeV3(), Error));
	FGuLiGroupAbilityConfigSnapshot Config;
	TestTrue(TEXT("Native config projects"), ASC->BuildGroupAbilityConfigSnapshot(Config));
	FGuLiWingmanGroupHandle Group;
	Group.ShipInstanceId = Projection.ShipInstanceId;
	Group.ShipGeneration = 1;
	Group.GroupGeneration = 1;
	FGuLiWingmanRelayServer Relay;
	TestTrue(TEXT("Authority roster initializes"), Relay.InitializeGroup(9, Group,
		Projection.OwnerPlayerGuid, FGuid(5, 6, 7, 8), Config, 0));
	TestTrue(TEXT("Use the live strict connection contract"),
		Relay.ConfigureStrictFlightContract(1, FGuLiWingmanRelayValidationRevisions{}, 0));
	FGuLiWingmanBootstrapBundle Cut;
	if (!TestTrue(TEXT("Initial cut builds"), Relay.BuildBootstrap(Cut))) return false;

	// A complete, authoritative death roster uses the same reliable cut format.
	Cut.bRequiresAtomicCandidateBatch = false;
	Cut.bActiveRosterRefresh = true;
	Cut.RequiredFlightMask = 0;
	for (FGuLiWingmanRosterEntry& Entry : Cut.Roster)
	{
		Entry.bDead = true;
		Cut.Dead.Add(Entry.Wingman);
	}
	for (FGuLiWingmanHealthEntry& Health : Cut.Health) Health.CurrentHealthPermille = 0;
	Cut.RequiredMemberMaskHash = GuLiWingmanRelayHash::RequiredMemberMasks(Cut.Roster);
	for (FGuLiWingmanBootstrapScopeState& Scope : Cut.Commit.Scopes)
	{
		if (Scope.Scope == EGuLiWingmanBootstrapScope::Roster)
			Scope.Hash = GuLiWingmanRelayHash::Roster(Cut.Roster);
		else if (Scope.Scope == EGuLiWingmanBootstrapScope::Health)
			Scope.Hash = GuLiWingmanRelayHash::Health(Cut.Health);
		else if (Scope.Scope == EGuLiWingmanBootstrapScope::Dead)
			Scope.Hash = GuLiWingmanRelayHash::Dead(Cut.Dead);
	}
	TestTrue(TEXT("All 25 dead members can be published without waiting for a replacement"), Cut.IsWellFormed());
	Cut.bActiveRosterRefresh = false;
	TestFalse(TEXT("An initial/atomic bootstrap still needs its live Flight contract"), Cut.IsWellFormed());
	return true;
}
#endif
