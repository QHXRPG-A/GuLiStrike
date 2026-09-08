#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "UObject/CoreNet.h"

#if WITH_EDITOR
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelFunctionLibrary.h"
#include "NiagaraDataChannelHandler.h"
#include "NiagaraDataChannelData.h"
#include "NiagaraDataSet.h"
#include "UObject/UnrealType.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "EngineUtils.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "Serialization/JsonWriter.h"

// Explicit acceptance helpers stay in this authorized test file, never in shipping gameplay.
namespace GuLiCombatEffectsPIEQA
{
	TStrongObjectPtr<ULevelEditorPlaySettings> PlaySettings;
	FDelegateHandle EndHandle;
	void Start(const TArray<FString>& Args)
	{
		if (!GEditor || GEditor->IsPlaySessionInProgress() || Args.Num() != 1
			|| (Args[0] != TEXT("listen") && Args[0] != TEXT("dedicated") && Args[0] != TEXT("dedicated-late")))
		{ UE_LOG(LogTemp, Error, TEXT("Combat effects QA requires stopped PIE: gs.CombatEffects.QA.Start listen|dedicated")); return; }
		PlaySettings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
		PlaySettings->SetFlags(RF_Transient);
		PlaySettings->SetPlayNetMode(Args[0] == TEXT("listen") ? EPlayNetMode::PIE_ListenServer : EPlayNetMode::PIE_Client);
		PlaySettings->SetPlayNumberOfClients(Args[0]==TEXT("dedicated-late") ? 1 : 2); PlaySettings->SetRunUnderOneProcess(true);
		PlaySettings->bLaunchSeparateServer = false;
		PlaySettings->NewWindowWidth = 960; PlaySettings->NewWindowHeight = 540;
		PlaySettings->SetClientWindowSize(FIntPoint(960,540)); PlaySettings->LastSize = FIntPoint(960,540);
		PlaySettings->MultipleInstancePositions.Reset(); PlaySettings->CenterNewWindow = true;
		EndHandle = FEditorDelegates::EndPIE.AddLambda([](bool)
		{ PlaySettings.Reset(); FEditorDelegates::EndPIE.Remove(EndHandle); EndHandle.Reset(); });
		FRequestPlaySessionParams Request;
		Request.EditorPlaySettings = PlaySettings.Get(); Request.SessionDestination = EPlaySessionDestinationType::InProcess;
		Request.WorldType = EPlaySessionWorldType::PlayInEditor; Request.bAllowOnlineSubsystem = false;
		GEditor->RequestPlaySession(Request);
	}
	void Sample()
	{
		FString Json; const auto Writer = TJsonWriterFactory<>::Create(&Json); Writer->WriteArrayStart();
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World(); if (!World || World->WorldType != EWorldType::PIE) continue;
			Writer->WriteObjectStart(); Writer->WriteValue(TEXT("world"),World->GetPathName());
			Writer->WriteValue(TEXT("net_mode"),static_cast<int32>(World->GetNetMode()));
			Writer->WriteValue(TEXT("world_delta_seconds"),World->GetDeltaSeconds());
			int32 RosterCount = 0;
			for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It) RosterCount += It->GetItems().Num();
			Writer->WriteValue(TEXT("roster_count"),RosterCount);
			Writer->WriteArrayStart(TEXT("transport"));
			if (const UNetDriver* Driver=World->GetNetDriver())
			{
				TArray<UNetConnection*> Connections;
				if (Driver->ServerConnection) Connections.Add(Driver->ServerConnection);
				for (UNetConnection* Connection:Driver->ClientConnections) if (Connection) Connections.Add(Connection);
				for (const auto* Connection:Connections)
				{
					Writer->WriteObjectStart(); Writer->WriteValue(TEXT("speed_limit_bytes_s"),Connection->CurrentNetSpeed);
					Writer->WriteValue(TEXT("out_bytes_s"),Connection->OutBytesPerSecond);
					Writer->WriteValue(TEXT("in_bytes_s"),Connection->InBytesPerSecond);
					Writer->WriteValue(TEXT("queued_bits"),Connection->QueuedBits); Writer->WriteObjectEnd();
				}
			}
			Writer->WriteArrayEnd();
			if (World->GetNetMode() != NM_DedicatedServer)
			{
				auto* Channel = LoadObject<UNiagaraDataChannelAsset>(nullptr,TEXT("/Game/GuLiStrike/FX/CommanderWeapons/NDC_CommanderGunfire.NDC_CommanderGunfire"));
				if (auto* Handler = Channel ? UNiagaraDataChannelLibrary::FindDataChannelHandler(World,Channel->Get()) : nullptr)
				{
					FNDCAccessContextInst Access(Channel->Get()->GetAccessContextType());
					if (auto Data = Handler->FindData(Access,ENiagaraResourceAccess::ReadOnly))
					{
						const auto Current = Data->GetCPUData(false), Previous = Data->GetCPUData(true);
						Writer->WriteValue(TEXT("ndc_current"),Current ? Current->GetNumInstances() : 0);
						Writer->WriteValue(TEXT("ndc_previous"),Previous ? Previous->GetNumInstances() : 0);
					}
				}
			}
			Writer->WriteArrayStart(TEXT("niagara"));
			for (TObjectIterator<UNiagaraComponent> It; It; ++It)
			{
				UNiagaraComponent* Component = *It;
				if (!IsValid(Component) || !Component->IsRegistered() || Component->GetWorld() != World || !Component->GetAsset()
					|| !Component->GetAsset()->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/FX/CommanderWeapons/"))) continue;
				Writer->WriteObjectStart(); Writer->WriteValue(TEXT("asset"),Component->GetAsset()->GetPathName());
				Writer->WriteValue(TEXT("active"),Component->IsActive());
				Writer->WriteArrayStart(TEXT("emitters"));
				if (const auto Controller = Component->GetSystemInstanceController(); Controller && Controller->IsValid())
				{
					Controller->WaitForConcurrentTickAndFinalize();
					if (Component->GetAsset()->GetName() == TEXT("NS_CommanderGunfireBatch"))
					{
						for (int32 Index=0; Index<32; ++Index)
						{
							UNiagaraDataInterface* Interface = nullptr; void* Data = nullptr;
							Controller->GetSystemInstance_Unsafe()->GetDataInterfaceInstanceDataInfo(Index,Interface,Data);
							if (!Interface) break;
							if (const auto* ChannelProperty = FindFProperty<FObjectPropertyBase>(Interface->GetClass(),TEXT("Channel")))
							{
								UE_LOG(LogTemp,Display,TEXT("Gunfire DI %s Channel=%s InstanceData=%d"),*Interface->GetPathName(),
									*GetPathNameSafe(ChannelProperty->GetObjectPropertyValue_InContainer(Interface)),Data!=nullptr);
							}
						}
					}
					for (const auto& Emitter : Controller->GetSystemInstance_Unsafe()->GetEmitters())
					{
						Writer->WriteObjectStart(); Writer->WriteValue(TEXT("name"),Emitter->GetEmitterHandle().GetName().ToString());
						Writer->WriteValue(TEXT("particles"),Emitter->GetNumParticles()); Writer->WriteObjectEnd();
					}
				}
				Writer->WriteArrayEnd(); Writer->WriteObjectEnd();
			}
			Writer->WriteArrayEnd(); Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd(); Writer->Close();
		const FString Directory=FPaths::ProjectDir()/TEXT("outputs/commander-combat-effects");
		IFileManager::Get().MakeDirectory(*Directory,true);
		FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("native_runtime_sample.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	FAutoConsoleCommand StartCommand(TEXT("gs.CombatEffects.QA.Start"),TEXT("Transient settings: listen (1+1) or dedicated (server+2). Never saves assets."),FConsoleCommandWithArgsDelegate::CreateStatic(&Start));
	FAutoConsoleCommand SampleCommand(TEXT("gs.CombatEffects.QA.Sample"),TEXT("Read-only role, component and real particle-count sample for all PIE worlds."),FConsoleCommandDelegate::CreateStatic(&Sample));
	FAutoConsoleCommand CaptureCommand(TEXT("gs.CombatEffects.QA.Capture"),TEXT("Capture a specified PIE client viewport when real gunfire particles span two frames: <label> <PIE instance>."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num()!=2 || Args[0].IsEmpty() || !Args[0].GetCharArray().ContainsByPredicate([](TCHAR C){ return FChar::IsAlnum(C); })) return;
		for (TCHAR C:Args[0]) if (!FChar::IsAlnum(C) && C!=TEXT('_') && C!=TEXT('-')) return;
		TWeakObjectPtr<UWorld> Target;
		for (const auto& Context:GEngine->GetWorldContexts())
			if (Context.WorldType==EWorldType::PIE && Context.PIEInstance==FCString::Atoi(*Args[1])) Target=Context.World();
		if (!Target.IsValid() || Target->GetNetMode()==NM_DedicatedServer) return;
		const FString File=FPaths::ProjectDir()/TEXT("outputs/commander-combat-effects")/(Args[0]+TEXT(".png"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Target,File,Elapsed=0.0f,VisibleFrames=0](float Delta) mutable
		{
			Elapsed+=Delta; if (!Target.IsValid() || Elapsed>10) return false;
			bool bParticles=false;
			for (TObjectIterator<UNiagaraComponent> It; It; ++It)
			{
				if (It->GetWorld()!=Target.Get() || !It->GetAsset() || It->GetAsset()->GetName()!=TEXT("NS_CommanderGunfireBatch")) continue;
				if (const auto Controller=It->GetSystemInstanceController(); Controller && Controller->IsValid())
				{
					Controller->WaitForConcurrentTickAndFinalize();
					for (const auto& Emitter:Controller->GetSystemInstance_Unsafe()->GetEmitters()) bParticles|=Emitter->GetNumParticles()>0;
				}
			}
			VisibleFrames=bParticles ? VisibleFrames+1 : 0;
			if (VisibleFrames<2) return true;
			UGameViewportClient* Client=Target->GetGameViewport(); FViewport* Viewport=Client ? Client->Viewport : nullptr;
			TArray<FColor> Pixels;
			if (!Viewport || !GetViewportScreenShot(Viewport,Pixels)) return true;
			TArray64<uint8> PNG; const FIntPoint Size=Viewport->GetSizeXY();
			FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
			FFileHelper::SaveArrayToFile(PNG,*File);
			UE_LOG(LogTemp,Display,TEXT("Combat effects actual PIE viewport captured: %s"),*File);
			return false;
		}));
	}));
	FAutoConsoleCommand LateJoinCommand(TEXT("gs.CombatEffects.QA.LateJoin"),TEXT("Add the second client to the dedicated-late acceptance fixture."),FConsoleCommandDelegate::CreateLambda([]
	{
		if (!GEditor || !GEditor->IsPlaySessionInProgress() || !PlaySettings.IsValid()) return;
		int32 Clients=0; bool bDedicated=false;
		for (const auto& Context:GEngine->GetWorldContexts()) if (UWorld* World=Context.World(); World && World->WorldType==EWorldType::PIE)
		{ Clients+=World->GetNetMode()==NM_Client; bDedicated|=World->GetNetMode()==NM_DedicatedServer; }
		if (bDedicated && Clients==1) GEditor->RequestLateJoin();
	}));
}
#endif

// A deterministic test clock, not a second public gameplay entry point.
struct FGuLiCombatEffectRuntimeTestAccess
{
	static void Field(UGuLiCombatEffectRuntimeSubsystem* Runtime, FGuid Id, float Now)
	{ Runtime->bIndexReady = false; Runtime->StepField(Id, Now); }
	static void Projectile(UGuLiCombatEffectRuntimeSubsystem* Runtime, FGuid Id, float Now, float Delta)
	{ Runtime->StepProjectile(Id, Delta, Now); }
};

namespace GuLiCombatEffectTests
{
	struct FTarget { FGuLiCombatTargetSnapshot Snapshot; int32 Hits = 0; };
	struct FFixture
	{
		UWorld* World = nullptr;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UGuLiCombatEffectRuntimeSubsystem* Runtime = nullptr;
		TArray<TUniquePtr<FTarget>> Targets;
		bool Initialize(FAutomationTestBase& Test)
		{
			if (!GEngine) return false;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("isolated game world"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
			Runtime = World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
			return Test.TestNotNull(TEXT("independent runtime"), Runtime) && Ledger && Ledger->BeginServerEpoch(9);
		}
		~FFixture()
		{
			if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); }
		}
		FTarget& Add(EGuLiTeam Team, FVector Position, bool bMass = false, float Radius = 50)
		{
			auto Target = MakeUnique<FTarget>(); FTarget* Ptr = Target.Get();
			const uint32 Id = Targets.Num() + 1;
			Ptr->Snapshot.Handle = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9, Id);
			if (!bMass)
			{
				Ptr->Snapshot.Handle.Kind = EGuLiTargetKind::Ship;
				Ptr->Snapshot.Handle.AuthorityId = FGuid(0, 0, 0, Id);
				Ptr->Snapshot.Handle.LocalId = 0;
			}
			Ptr->Snapshot.Team = Team; Ptr->Snapshot.Location = Position; Ptr->Snapshot.CollisionRadius = Radius;
			Ptr->Snapshot.Health = 100; Ptr->Snapshot.bAlive = true;
			FGuLiCombatTargetAdapter Adapter; Adapter.LifetimeOwner = World;
			Adapter.ReadSnapshot = [Ptr](FGuLiCombatTargetSnapshot& Out) { Out = Ptr->Snapshot; return true; };
			Adapter.ApplyDamage = [Ptr](const FGuLiDamageRequest& Request, FGuLiDamageCommitResult& Out)
			{
				if (!Ptr->Snapshot.bAlive) return false;
				++Ptr->Hits; Out.AppliedDamage = FMath::Min(Request.Damage, Ptr->Snapshot.Health);
				Ptr->Snapshot.Health -= Out.AppliedDamage; Ptr->Snapshot.bAlive = Ptr->Snapshot.Health > 0;
				Out.RemainingHealth = Ptr->Snapshot.Health; Out.bKilled = !Ptr->Snapshot.bAlive; return true;
			};
			check(Ledger->RegisterTarget(Ptr->Snapshot.Handle, MoveTemp(Adapter)));
			Targets.Add(MoveTemp(Target)); return *Ptr;
		}
		FGuLiCombatEffectContext Context(const FTarget& Source, const FTarget* Target = nullptr)
		{
			FGuLiCombatEffectContext Value; Value.Source = Source.Snapshot.Handle;
			if (Target) Value.Target = Target->Snapshot.Handle;
			Value.Damage = 10; Value.MatchEpoch = 9; Value.SkillId = TEXT("TestSpell");
			return Value;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCombatEffectEpochBootstrapTest, "GuLiStrike.CombatEffects.PublicGameStateInitializesLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCombatEffectEpochBootstrapTest::RunTest(const FString& Parameters)
{
	GuLiCombatEffectTests::FFixture F; if (!F.Initialize(*this)) return false;
	auto* State = F.World->SpawnActor<AGuLiBattleGameState>();
	if (!TestNotNull(TEXT("public Battle GameState"),State)) return false;
	State->InitializeServerMatchState();
	TestTrue(TEXT("a Commander-only match initializes the ledger without another weapon domain"),State->GetMatchEpoch()!=0 && F.Ledger->GetMatchEpoch()==State->GetMatchEpoch());
	const uint32 Epoch=State->GetMatchEpoch(); State->InitializeServerMatchState();
	TestEqual(TEXT("bootstrap is idempotent"),F.Ledger->GetMatchEpoch(),Epoch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCombatEffectMathTest, "GuLiStrike.CombatEffects.MathAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCombatEffectMathTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffects;
	FGuLiCombatEffectState A; A.RandomSeed = 142;
	A.LaunchLocation = FVector(100, 200, 50); A.Location = A.LaunchLocation;
	A.LastTargetLocation = FVector(6000, 0, 50); A.LaunchDirection = FVector::ForwardVector;
	FGuLiCombatEffectState B = A;
	for (int32 Step = 1; Step < 45; ++Step)
	{
		AdvanceProjectile(A, Step / 30.0f, 1 / 30.0f); AdvanceProjectile(B, Step / 30.0f, 1 / 30.0f);
		TestTrue(TEXT("same seed and input reproduce every flight sample"), FVector(A.Location).Equals(B.Location, 0.001));
	}
	B.RandomSeed = 421;
	TestFalse(TEXT("different seeds vary the lift"), LiftPosition(A, 0.25f).Equals(LiftPosition(B, 0.25f)));
	TestTrue(TEXT("lift starts at the authored launch point"), LiftPosition(A, 0).Equals(A.LaunchLocation));
	TestTrue(TEXT("lift raises the missile"), LiftPosition(A, 0.25f).Z > A.LaunchLocation.Z + 500);
	TestEqual(TEXT("instant is not due before activation"), PulsesDue(EGuLiSpellFieldTiming::Instant, 1, 0, 1, .99), 0);
	TestEqual(TEXT("instant remains one pulse"), PulsesDue(EGuLiSpellFieldTiming::Instant, 1, 0, 1, 99), 1);
	TestEqual(TEXT("delayed activates at the exact boundary"), PulsesDue(EGuLiSpellFieldTiming::Delayed, 2, 0, 1, 2), 1);
	TestEqual(TEXT("periodic includes activation"), PulsesDue(EGuLiSpellFieldTiming::Periodic, 2, 2, .5, 2), 1);
	TestEqual(TEXT("periodic excludes a pulse at the exclusive end"), PulsesDue(EGuLiSpellFieldTiming::Periodic, 2, 2, .5, 4), 4);
	FGuLiCombatTargetSnapshot Target; Target.Handle = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9, 2);
	Target.bAlive = true; Target.Location = FVector(850, 0, 0); Target.CollisionRadius = 50;
	TestTrue(TEXT("sphere intersects target collision radius at the boundary"), IntersectsSphere(FVector::ZeroVector, 800, Target));
	Target.Location.X += .01;
	TestFalse(TEXT("just outside is excluded"), IntersectsSphere(FVector::ZeroVector, 800, Target));
	const FGuid Effect(1, 2, 3, 4);
	TestEqual(TEXT("one target and pulse have a stable damage identity"), DamageId(Effect, 0, Target.Handle), DamageId(Effect, 0, Target.Handle));
	TestNotEqual(TEXT("different pulses stack"), DamageId(Effect, 0, Target.Handle), DamageId(Effect, 1, Target.Handle));
	// Instant tracers use two full endpoints, not velocity integration. Check local scale, pivot and yaw.
	const FTransform Pose(FRotator(0, 90, 0), FVector(100, 200, 30), FVector(2));
	TestTrue(TEXT("muzzle transform includes scale and pivot"), Pose.TransformPosition(FVector(50, 10, 20)).Equals(FVector(80, 300, 70), .001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSpellFieldTableConfigTest, "GuLiStrike.CombatEffects.SpellFieldTableConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSpellFieldTableConfigTest::RunTest(const FString& Parameters)
{
	GuLiCombatEffectTests::FFixture F; if (!F.Initialize(*this)) return false;
	const UGuLiCommanderDataSubsystem* Data = F.World->GetSubsystem<UGuLiCommanderDataSubsystem>();
	if (!TestNotNull(TEXT("Commander table cache"), Data)
		|| !TestTrue(TEXT("SpellFields table is valid"), Data->IsSpellFieldCatalogValid())
		|| !TestTrue(TEXT("WeaponMounts table is valid"), Data->IsWeaponMountCatalogValid())) return false;
	const FGuLiSpellFieldConfig* Field = Data->FindSpellFieldConfig(TEXT("WM01_MissileExplosion"));
	if (!TestNotNull(TEXT("WM01 table field"), Field)) return false;
	TestEqual(TEXT("field base damage comes from SpellFields"), Field->Damage, 30.0f);
	TestEqual(TEXT("field radius comes from SpellFields"), Field->Radius, 800.0f);
	TestEqual(TEXT("field timing comes from SpellFields"), Field->Timing, EGuLiSpellFieldTiming::Instant);
	const FGuLiSkillDefinition* Skill = Data->FindSkillDefinition(TEXT("WM01_HomingMissile"));
	TestTrue(TEXT("missile skill links the field row"), Skill && Skill->EffectConfigId == Field->ConfigId);
	const FGuLiUnitSkillConfig* Weapon = Data->GetUnitSkillConfigs().FindByPredicate([](const auto& Config)
	{
		return Config.UnitTypeId == 2 && Config.SlotId == TEXT("MissileLauncher");
	});
	TestTrue(TEXT("field damage is injected into the resolved weapon base without a duplicate authored value"),
		Weapon && Weapon->Damage == Field->Damage);
	const FGuLiWeaponMountConfig* FourFGun = Data->FindWeaponMountConfig(1, TEXT("BasicAttack"));
	TestTrue(TEXT("FourFRobot muzzle is loaded from WeaponMounts"), FourFGun && FourFGun->Muzzles.Num() == 1
		&& FourFGun->Muzzles[0].Equals(FVector(-480, 5, 935), 0.01)
		&& FourFGun->AimOffset.Equals(FVector(0, 0, 650), 0.01));
	const FGuLiWeaponMountConfig* WM01Missiles = Data->FindWeaponMountConfig(2, TEXT("MissileLauncher"));
	TestTrue(TEXT("WM01 twin missile sockets retain table order"), WM01Missiles && WM01Missiles->Muzzles.Num() == 2
		&& WM01Missiles->Muzzles[0].Equals(FVector(-260, 1257, 2440), 0.01)
		&& WM01Missiles->Muzzles[1].Equals(FVector(-260, -326, 2440), 0.01)
		&& WM01Missiles->AimOffset.Equals(FVector(0, 495, 1600), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSpellFieldLedgerTest, "GuLiStrike.CombatEffects.SphereLedgerAndRetainedSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSpellFieldLedgerTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffectTests;
	FFixture F; if (!F.Initialize(*this)) return false;
	auto& Source = F.Add(EGuLiTeam::Red, FVector(-5000, 0, 0));
	auto& Actor = F.Add(EGuLiTeam::Blue, FVector::ZeroVector);
	auto& Mass = F.Add(EGuLiTeam::Blue, FVector(850, 0, 0), true);
	auto& Outside = F.Add(EGuLiTeam::Blue, FVector(850.1, 0, 0), true);
	auto& Friendly = F.Add(EGuLiTeam::Red, FVector::ZeroVector);
	auto* Definition = NewObject<UGuLiSpellFieldDefinition>(F.World);
	const auto Context = F.Context(Source);
	FGuid Id = F.Runtime->CreateSpellField(Definition, Context, FVector::ZeroVector);
	TestTrue(TEXT("field returns a stable handle"), Id.IsValid());
	TestEqual(TEXT("registered Actor damaged"), Actor.Hits, 1);
	TestEqual(TEXT("registered Mass target damaged at sphere boundary"), Mass.Hits, 1);
	TestEqual(TEXT("outside untouched"), Outside.Hits, 0);
	TestEqual(TEXT("friendly untouched"), Friendly.Hits, 0);
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, 0);
	TestEqual(TEXT("same pulse cannot repeat"), Actor.Hits, 1);
	Definition->Timing = EGuLiSpellFieldTiming::Delayed; Definition->Delay = 1;
	Id = F.Runtime->CreateSpellField(Definition, Context, FVector::ZeroVector);
	Definition->Radius = 1; // Changing the asset after creation cannot change the frozen radius.
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, .5f);
	TestEqual(TEXT("delay has no early damage"), Mass.Hits, 1);
	F.Ledger->UnregisterTarget(Source.Snapshot.Handle, F.World); Source.Snapshot.bAlive = false;
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, 1.0f);
	TestEqual(TEXT("a removed caster retains validated attribution"), Actor.Hits, 2);
	TestEqual(TEXT("frozen radius still reaches its original boundary"), Mass.Hits, 2);
	TestTrue(TEXT("cancel removes the visual tail too"), F.Runtime->CancelEffect(Id));
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, 5.0f);
	TestEqual(TEXT("cancel cannot deal further damage"), Mass.Hits, 2);
	TestFalse(TEXT("removed source cannot submit a new spell"), F.Runtime->CreateSpellField(Definition, Context, FVector::ZeroVector).IsValid());
	F.Ledger->BeginServerEpoch(10); F.Runtime->Tick(0);
	TestEqual(TEXT("new epoch clears every remaining field"), F.Runtime->GetActiveEffectCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPeriodicFieldTest, "GuLiStrike.CombatEffects.PeriodicRequeryAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiPeriodicFieldTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffectTests;
	FFixture F; if (!F.Initialize(*this)) return false;
	auto& Source = F.Add(EGuLiTeam::Red, FVector(-5000, 0, 0));
	auto& Target = F.Add(EGuLiTeam::Blue, FVector::ZeroVector, true);
	auto* Definition = NewObject<UGuLiSpellFieldDefinition>(F.World);
	Definition->Timing = EGuLiSpellFieldTiming::Periodic; Definition->Delay = .2f;
	Definition->Duration = 1.1f; Definition->PulseInterval = .5f;
	FGuid Id = F.Runtime->CreateSpellField(Definition, F.Context(Source), FVector::ZeroVector);
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, .2f); TestEqual(TEXT("activation pulse"), Target.Hits, 1);
	Target.Snapshot.Location = FVector(5000, 0, 0);
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, .7f); TestEqual(TEXT("departed target excluded next pulse"), Target.Hits, 1);
	Target.Snapshot.Location = FVector::ZeroVector;
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, 1.2f); TestEqual(TEXT("returning target included next pulse"), Target.Hits, 2);
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, 2); TestEqual(TEXT("tail has no damage"), Target.Hits, 2);
	const FGuid Skipped = F.Runtime->CreateSpellField(Definition, F.Context(Source), FVector::ZeroVector);
	TestTrue(TEXT("independent fields coexist"), F.Runtime->GetActiveEffectCount() == 2);
	FGuLiCombatEffectState ExpiredState; F.Runtime->QueryEffect(Skipped,ExpiredState);
	FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime,Skipped,ExpiredState.EndTime);
	TestEqual(TEXT("a hitch to the exclusive end cannot cash out missed periodic damage"),Target.Hits,2);
	F.Runtime->CancelEffect(Id); FGuLiCombatEffectRuntimeTestAccess::Field(F.Runtime, Id, 3);
	TestEqual(TEXT("cancelled field never resumes"), Target.Hits, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiProjectileLifecycleTest, "GuLiStrike.CombatEffects.ProjectileLossImpactAndExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiProjectileLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffectTests;
	FFixture F; if (!F.Initialize(*this)) return false;
	auto& Source = F.Add(EGuLiTeam::Red, FVector(-5000, 0, 1000));
	auto& Target = F.Add(EGuLiTeam::Blue, FVector(4000, 0, 1000), true, 150);
	auto& Nearby = F.Add(EGuLiTeam::Blue, FVector(4200, 0, 1000));
	auto* Field = NewObject<UGuLiSpellFieldDefinition>(F.World);
	auto* Projectile = NewObject<UGuLiProjectileEffectDefinition>(F.World); Projectile->ImpactField = Field;
	const auto Context = F.Context(Source, &Target);
	const FGuid Id = F.Runtime->LaunchProjectile(Projectile, Context, FTransform(FVector(0, 0, 1000)));
	TestTrue(TEXT("projectile accepts server target context"), Id.IsValid());
	TestEqual(TEXT("launch itself has no direct damage"), Target.Hits, 0);
	F.Ledger->UnregisterTarget(Source.Snapshot.Handle, F.World);
	F.Ledger->UnregisterTarget(Target.Snapshot.Handle, F.World);
	Target.Snapshot.Location = FVector(100000, 0, 1000);
	Field->Radius = 1; // In-flight payload must retain the original explosion radius.
	FGuLiCombatEffectState State;
	for (int32 Step = 1; Step <= 240 && F.Runtime->QueryEffect(Id, State); ++Step)
	{
		FGuLiCombatEffectRuntimeTestAccess::Projectile(F.Runtime, Id, Step / 30.0f, 1 / 30.0f);
		if (F.Runtime->QueryEffect(Id, State)) TestTrue(TEXT("lost target keeps the last valid destination"), FVector(State.LastTargetLocation).Equals(FVector(4000, 0, 1000)));
	}
	TestEqual(TEXT("last-location arrival creates exactly one explosion"), F.Runtime->GetCounters().FieldsCreated, static_cast<int64>(1));
	TestEqual(TEXT("nearby target takes one field hit, no direct hit"), Nearby.Hits, 1);
	// Independent source remains alive for cancellation and timeout checks.
	auto& Source2 = F.Add(EGuLiTeam::Red, FVector(-10000, 0, 1000));
	Field->Radius = 800;
	const auto Context2 = F.Context(Source2, &Nearby);
	FGuid Other = F.Runtime->LaunchProjectile(Projectile, Context2, FTransform(FVector(0, 0, 1000)));
	F.Runtime->CancelEffect(Other);
	Other = F.Runtime->LaunchProjectile(Projectile, Context2, FTransform(FVector(0, 0, 1000)));
	FGuLiCombatEffectRuntimeTestAccess::Projectile(F.Runtime, Other, 9, 1 / 30.0f);
	TestEqual(TEXT("cancel and timeout do not create bonus explosions"), F.Runtime->GetCounters().FieldsCreated, static_cast<int64>(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCombatEffectNetworkOrderTest, "GuLiStrike.CombatEffects.NetworkOrderAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCombatEffectNetworkOrderTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCombatEffectTests;
	// The 500-unit fixture must not resend long asset paths/frozen motion on each
	// terminal event. Exercise the real bit serializer, identities and byte budget.
	FGuLiCombatEffectState Wire; Wire.MatchEpoch=9; Wire.EffectId=FGuid(1,2,3,4); Wire.Sequence=3;
	Wire.Source=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9,17);
	Wire.Target=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9,500,2);
	Wire.Location=FVector(123,456,789); Wire.EndTime=8;
	bool bSerialized=false; FNetBitWriter StateWriter(nullptr,0);
	Wire.NetSerialize(StateWriter,nullptr,bSerialized);
	TestTrue(TEXT("compact create bit stream is valid and under 200 bytes"),bSerialized && StateWriter.GetNumBytes()<200);
	FNetBitReader StateReader(nullptr,StateWriter.GetData(),StateWriter.GetNumBits()); FGuLiCombatEffectState Decoded;
	Decoded.NetSerialize(StateReader,nullptr,bSerialized);
	TestTrue(TEXT("compact targets round trip the complete stable identities"),bSerialized && Decoded.Source==Wire.Source && Decoded.Target==Wire.Target);
	TestTrue(TEXT("quantized create preserves position"),FVector(Decoded.Location).Equals(Wire.Location,1));
	Wire.Phase=EGuLiCombatEffectPhase::Finished; Wire.EndReason=EGuLiCombatEffectEndReason::Impact;
	FNetBitWriter EndWriter(nullptr,0); Wire.NetSerialize(EndWriter,nullptr,bSerialized);
	TestTrue(TEXT("terminal event has no repeated cast payload and is under 28 bytes"),bSerialized && EndWriter.GetNumBytes()<28);
	FNetBitReader EndReader(nullptr,EndWriter.GetData(),EndWriter.GetNumBits()); Decoded.NetSerialize(EndReader,nullptr,bSerialized);
	TestTrue(TEXT("payload-free terminal remains a well-formed removal"),bSerialized && Decoded.IsWellFormed() && Decoded.Phase==EGuLiCombatEffectPhase::Finished);
	FGuLiCombatShotCue WireShot; WireShot.MatchEpoch=9; WireShot.ShotId=FGuid(4,3,2,1);
	WireShot.Source=Wire.Source; WireShot.Target=Wire.Target; WireShot.SlotId=TEXT("BasicAttack"); WireShot.UnitTypeId=2;
	WireShot.Start=FVector(123,456,789); WireShot.End=FVector(12000,500,800);
	FNetBitWriter ShotWriter(nullptr,0); WireShot.NetSerialize(ShotWriter,nullptr,bSerialized);
	TestTrue(TEXT("full-endpoint shot is under 65 bytes"),bSerialized && ShotWriter.GetNumBytes()<65);
	FNetBitReader ShotReader(nullptr,ShotWriter.GetData(),ShotWriter.GetNumBits()); FGuLiCombatShotCue DecodedShot;
	DecodedShot.NetSerialize(ShotReader,nullptr,bSerialized);
	TestTrue(TEXT("shot wire round trip preserves endpoints and mount"),bSerialized && DecodedShot.Source==WireShot.Source
		&& DecodedShot.Target==WireShot.Target && DecodedShot.SlotId==WireShot.SlotId
		&& FVector(DecodedShot.Start).Equals(WireShot.Start,1) && FVector(DecodedShot.End).Equals(WireShot.End,1));
	FFixture F; if (!F.Initialize(*this)) return false;
	auto* Visuals = F.World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>();
	if (!TestNotNull(TEXT("standalone client presentation exists"), Visuals)) return false;
	FGuLiCombatEffectState State; State.EffectId = FGuid(1, 2, 3, 4); State.MatchEpoch = 9; State.Sequence = 1;
	State.Source = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(9, 1); State.EndTime = 8;
	Visuals->ApplyState(State, true); Visuals->ApplyState(State);
	TestEqual(TEXT("duplicate create has one active instance"), Visuals->GetActiveVisualCount(), 1);
	State.Sequence = 5; Visuals->ApplyState(State);
	State.Sequence = 3; Visuals->ApplyState(State);
	TestEqual(TEXT("out of order correction cannot roll state back"), Visuals->GetEffectStates()[0].Sequence, 5u);
	FGuLiCombatEffectCorrection Correction; Correction.MatchEpoch=9; Correction.EffectId=State.EffectId;
	Correction.Sequence=6; Correction.SampleTime=.2f; Correction.Location=FVector(100,200,300);
	Visuals->ApplyCorrection(Correction);
	TestTrue(TEXT("compact correction updates an existing projectile"),FVector(Visuals->GetEffectStates()[0].Location).Equals(Correction.Location));
	Correction.Sequence=4; Correction.Location=FVector::ZeroVector; Visuals->ApplyCorrection(Correction);
	TestEqual(TEXT("stale compact correction is ignored"),Visuals->GetEffectStates()[0].Sequence,6u);
	State.Sequence = 7; State.Phase = EGuLiCombatEffectPhase::Finished; Visuals->ApplyState(State);
	State.Sequence = 8; State.Phase = EGuLiCombatEffectPhase::Active; Visuals->ApplyState(State);
	Correction.Sequence=9; Visuals->ApplyCorrection(Correction);
	TestEqual(TEXT("terminal tombstone prevents resurrection"), Visuals->GetActiveVisualCount(), 0);
	FGuLiCombatShotCue Cue; Cue.MatchEpoch = 9; Cue.ShotId = FGuid(4, 3, 2, 1);
	Visuals->ApplyShots({Cue, Cue}); TestEqual(TEXT("duplicate shot is rejected"), Visuals->GetCounters().DroppedShots, static_cast<int64>(1));
	Visuals->BeginEpoch(10); State.EffectId = FGuid::NewGuid(); Visuals->ApplyState(State);
	TestEqual(TEXT("old epoch cannot repopulate the new match"), Visuals->GetActiveVisualCount(), 0);
	TestEqual(TEXT("epoch leaves no Niagara components"), Visuals->GetCounters().ComponentCount, 0);
	return true;
}
#endif
