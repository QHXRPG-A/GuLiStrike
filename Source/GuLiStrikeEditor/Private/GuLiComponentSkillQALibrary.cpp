#include "GuLiComponentSkillQALibrary.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Editor.h"
#include "GameFramework/PlayerController.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "EngineUtils.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Battle/Framework/GuLiBattleGameMode.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "GameFramework/GameNetworkManager.h"
#include "Slate/SceneViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "MassEntityHandle.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Gameplay/Resources/GuLiMiningPresentationComponent.h"
#include "Commander/UI/GuLiSceneUISubsystem.h"
#include "Commander/UI/GuLiSceneUIWidget.h"

namespace
{
	FDelegateHandle FlightLoginHandle;
	FDelegateHandle PerformanceInitHandle;
	FDelegateHandle FlightEndHandle;
	int32 FlightLoginCount = 0;
	TArray<EGuLiCommanderRole> FlightOriginalPriority;
	TWeakObjectPtr<AGuLiBattleGameMode> FlightGameMode;

	void ClearFlightLogin()
	{
		if (auto* Mode=FlightGameMode.Get())
			if (auto* Property=FindFProperty<FArrayProperty>(Mode->GetClass(),TEXT("InitialRolePriority")))
				*Property->ContainerPtrToValuePtr<TArray<EGuLiCommanderRole>>(Mode)=FlightOriginalPriority;
		FGameModeEvents::OnGameModePostLoginEvent().Remove(FlightLoginHandle);
		FGameModeEvents::OnGameModeInitializedEvent().Remove(PerformanceInitHandle);
		FEditorDelegates::EndPIE.Remove(FlightEndHandle);
		FlightLoginHandle.Reset(); FlightEndHandle.Reset(); FlightGameMode.Reset(); FlightOriginalPriority.Reset();
	}
}

bool UGuLiComponentSkillQALibrary::StartMixedFlightPIE()
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) return false;
	ClearFlightLogin(); FlightLoginCount=0;
	FlightLoginHandle=FGameModeEvents::OnGameModePostLoginEvent().AddLambda([](AGameModeBase* Base,APlayerController*)
	{
		auto* Mode=Cast<AGuLiBattleGameMode>(Base);
		if (!Mode || !Mode->GetWorld()->IsPlayInEditor()) return;
		auto* Property=FindFProperty<FArrayProperty>(Mode->GetClass(),TEXT("InitialRolePriority"));
		if (!Property) { ClearFlightLogin(); return; }
		auto& Priority=*Property->ContainerPtrToValuePtr<TArray<EGuLiCommanderRole>>(Mode);
		if (!FlightLoginCount) { FlightGameMode=Mode; FlightOriginalPriority=Priority; }
		++FlightLoginCount;
		if (FlightLoginCount==1) Priority={EGuLiCommanderRole::Ground};
		else if (FlightLoginCount==2) Priority={EGuLiCommanderRole::Air};
		else if (FlightLoginCount==3) Priority={EGuLiCommanderRole::Commander};
		else ClearFlightLogin();
	});
	FlightEndHandle=FEditorDelegates::EndPIE.AddLambda([](bool){ ClearFlightLogin(); });
	if (!StartPIE(2,4)) { ClearFlightLogin(); return false; }
	return true;
}


bool UGuLiComponentSkillQALibrary::StartPerformancePIE(bool MixedFlights,int32 Width,int32 Height)
{
 if (!GEditor || GEditor->IsPlaySessionInProgress() || Width<320 || Height<180 || Width>3840 || Height>2160) return false;
 ClearFlightLogin(); FlightLoginCount=0;
 if (MixedFlights)
 {
  PerformanceInitHandle=FGameModeEvents::OnGameModeInitializedEvent().AddLambda([](AGameModeBase* Base)
  {
   auto* Mode=Cast<AGuLiBattleGameMode>(Base);
   if (!Mode || !Mode->GetWorld()->IsPlayInEditor()) return;
   if (auto* P=FindFProperty<FArrayProperty>(Mode->GetClass(),TEXT("InitialRolePriority")))
   {
    auto& Roles=*P->ContainerPtrToValuePtr<TArray<EGuLiCommanderRole>>(Mode);
    FlightGameMode=Mode; FlightOriginalPriority=Roles; Roles={EGuLiCommanderRole::Ground};
   }
  });
  FlightLoginHandle=FGameModeEvents::OnGameModePostLoginEvent().AddLambda([](AGameModeBase* Base,APlayerController*)
  {
   auto* Mode=Cast<AGuLiBattleGameMode>(Base);
   if (!Mode || Mode!=FlightGameMode.Get()) return;
   if (auto* P=FindFProperty<FArrayProperty>(Mode->GetClass(),TEXT("InitialRolePriority")))
    *P->ContainerPtrToValuePtr<TArray<EGuLiCommanderRole>>(Mode)={EGuLiCommanderRole::Air};
   if (++FlightLoginCount>=2) ClearFlightLogin();
  });
  FlightEndHandle=FEditorDelegates::EndPIE.AddLambda([](bool){ClearFlightLogin();});
 }
 static TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
 Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
 Settings->SetPlayNetMode(EPlayNetMode::PIE_Client); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
 Settings->bLaunchSeparateServer=false; Settings->NewWindowWidth=Width; Settings->NewWindowHeight=Height;
 FRequestPlaySessionParams Request; Request.EditorPlaySettings=Settings.Get();
 Request.SessionDestination=EPlaySessionDestinationType::InProcess; Request.WorldType=EPlaySessionWorldType::PlayInEditor;
 Request.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Request); return true;
}
bool UGuLiComponentSkillQALibrary::SetPerformanceViewportSize(APlayerController* Controller,int32 Width,int32 Height)
{
 if (!Controller || !Controller->GetWorld()->IsPlayInEditor() || Width<320 || Height<180) return false;
 auto* Player=Controller->GetLocalPlayer(); auto* View=Player && Player->ViewportClient ? Player->ViewportClient->GetGameViewport():nullptr;
 if (!View) return false; View->SetFixedViewportSize(Width,Height); return View->GetSizeXY()==FIntPoint(Width,Height);
}
bool UGuLiComponentSkillQALibrary::CapturePerformanceViewport(APlayerController* Controller,const FString& Path)
{
 if (!Controller || !Controller->GetWorld()->IsPlayInEditor() || !FPaths::ConvertRelativePathToFull(Path).StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("outputs/performance/")))) return false;
 auto* Player=Controller->GetLocalPlayer(); auto* View=Player && Player->ViewportClient ? Player->ViewportClient->GetGameViewport():nullptr;
 if (!View) return false; TArray<FColor> Pixels; if (!View->ReadPixels(Pixels)) return false;
 const auto Size=View->GetSizeXY(); if (Pixels.Num()!=Size.X*Size.Y) return false;
 TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
 return FFileHelper::SaveArrayToFile(PNG,*Path);
}
FString UGuLiComponentSkillQALibrary::SetPerformanceBandwidth(UObject* WorldContext,int32 BytesPerSecond)
{
 UWorld* World=WorldContext ? WorldContext->GetWorld() : nullptr;
 UNetDriver* Driver=World ? World->GetNetDriver() : nullptr;
 if (!World || !World->IsPlayInEditor() || !Driver || BytesPerSecond<250000 || BytesPerSecond>4000000) return TEXT("{\"success\":false}");
 Driver->MaxClientRate=BytesPerSecond; Driver->MaxInternetClientRate=BytesPerSecond;
 if (Driver->ServerConnection) Driver->ServerConnection->CurrentNetSpeed=BytesPerSecond;
 for (UNetConnection* Connection:Driver->ClientConnections) if (Connection) Connection->CurrentNetSpeed=BytesPerSecond;
 for (TActorIterator<AGameNetworkManager> It(World);It;++It)
 { It->TotalNetBandwidth=BytesPerSecond*2; It->MaxDynamicBandwidth=BytesPerSecond; It->MinDynamicBandwidth=BytesPerSecond; }
 for (auto It=World->GetPlayerControllerIterator();It;++It) if (auto* Controller=It->Get())
  if (Controller->Player) Controller->Player->CurrentNetSpeed=BytesPerSecond;
 TArray<TSharedPtr<FJsonValue>> Connections;
 for (UNetConnection* Connection:Driver->ClientConnections) if (Connection)
 {
  auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("net_speed"),Connection->CurrentNetSpeed);
  Row->SetNumberField(TEXT("queued_bits"),Connection->QueuedBits); Connections.Add(MakeShared<FJsonValueObject>(Row));
 }
 auto Result=MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"),true);
 Result->SetNumberField(TEXT("bytes_per_second"),BytesPerSecond); Result->SetArrayField(TEXT("connections"),Connections);
 FString Json; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json)); return Json;
}

FString UGuLiComponentSkillQALibrary::SceneUIPerformanceProbe(APlayerController* Controller,const FString& Action,FVector Start,FVector End)
{
 if (!Controller || !Controller->IsLocalController() || !Controller->GetWorld()->IsPlayInEditor()) return TEXT("{\"success\":false}");
 auto* Widget=UGuLiSceneUISubsystem::ForController(Controller);
 if (!Widget) return TEXT("{\"success\":false}");
 if (Action==TEXT("remount"))
 { Widget->RemoveFromParent(); Widget=UGuLiSceneUISubsystem::ForController(Controller); }
 if (Action==TEXT("rect") || Action==TEXT("empty"))
 {
  Widget->BeginHUDFrame();
  if (Action==TEXT("rect")) Widget->AddScreenRect(FLinearColor::Green,50,50,120,60);
  Widget->EndHUDFrame();
 }
 FVector2D A,B; const bool Clipped=Widget->ProjectWorldLineToScreen(Start,End,A,B);
 return FString::Printf(TEXT("{\"success\":true,\"in_viewport\":%s,\"line_visible\":%s,\"a\":[%.4f,%.4f],\"b\":[%.4f,%.4f],\"stats\":%s}"),
  Widget->IsInViewport() ? TEXT("true") : TEXT("false"),Clipped ? TEXT("true") : TEXT("false"),A.X,A.Y,B.X,B.Y,*Widget->GetFrameStatsJson());
}

namespace
{
 struct FPerformancePopulation
 {
  TWeakObjectPtr<UWorld> World;
  TWeakObjectPtr<AGuLiBattlePlayerState> Owners[2];
  TArray<FGuLiSoldierId> Ids[2];
  uint32 Command[2] = {};
  int32 Requested = 0;
  int32 RejectedPlans = 0;
  bool bSpawnAttempted = false;
  bool bCombatLayout = false;
 } PerformancePopulation;

 UWorld* PerformanceAuthorityWorld(UObject* Context)
 {
  UWorld* World = Context ? Context->GetWorld() : nullptr;
  return World && World->IsPlayInEditor() && World->GetNetMode()!=NM_Client ? World : nullptr;
 }
 FString PerformanceJson(const TSharedRef<FJsonObject>& Object)
 {
  FString Result; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Result)); return Result;
 }
}

FString UGuLiComponentSkillQALibrary::PerformancePopulationSnapshot(UObject* WorldContext)
{
 const auto Result=MakeShared<FJsonObject>();
 UWorld* World=PerformanceAuthorityWorld(WorldContext);
 auto* Authority=World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
 Result->SetBoolField(TEXT("success"),Authority!=nullptr);
 Result->SetBoolField(TEXT("ready"),Authority && Authority->HasSpawnedAuthorityPopulation());
 if (World) Result->SetNumberField(TEXT("world_seconds"),World->GetTimeSeconds());
 if (Authority)
 {
  const auto Stats=Authority->GetNavigationStats();
  Result->SetNumberField(TEXT("alive"),Stats.Alive);
  Result->SetNumberField(TEXT("moving"),Stats.Active);
  Result->SetNumberField(TEXT("blocked"),Stats.Blocked);
  Result->SetNumberField(TEXT("pending_plans"),Stats.PendingMovePlanningTasks);
  Result->SetNumberField(TEXT("forced_avoidance_solves"),Stats.ForcedPredictiveAvoidanceSolves);
  Result->SetNumberField(TEXT("avoidance_solves"),Stats.PredictiveAvoidanceSolves);
  Result->SetNumberField(TEXT("requested"),PerformancePopulation.World==World ? PerformancePopulation.Requested : 0);
  if (PerformancePopulation.World==World)
  {
   // Read the real native completion acknowledgements; this also releases completed planning jobs.
   for (int32 Team=0;Team<2;++Team)
    if (auto* Owner=PerformancePopulation.Owners[Team].Get())
     for (uint32 Command=1;Command<=PerformancePopulation.Command[Team];++Command)
     {
      FGuLiCommandAck Ack; FGuLiCommanderSelectionState Selection; bool Changed=false;
      if (Authority->PollMovePlanning(*Owner,Command,Ack,Selection,Changed)==EGuLiMovePlanningStatus::Completed
       && Ack.Result!=EGuLiCommandAckResult::Accepted) ++PerformancePopulation.RejectedPlans;
     }
   Result->SetNumberField(TEXT("rejected_completed_plans"),PerformancePopulation.RejectedPlans);
   TArray<TSharedPtr<FJsonValue>> Positions;
   for (int32 Team=0;Team<2;++Team) for (const auto Id:PerformancePopulation.Ids[Team])
   {
    FTransform Pose; if (!Authority->TryGetSoldierTransform(Id,Pose)) continue;
    auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("id"),Id.Value);
    const FVector P=Pose.GetLocation(); Row->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});
    Row->SetNumberField(TEXT("yaw_degrees"),Pose.Rotator().Yaw);
    FMassEntityHandle Entity;
    if (Authority->FindSoldierEntity(Id,Entity)) Row->SetStringField(TEXT("entity_identity"),LexToString(Entity.AsNumber()));
    FGuLiSoldierNavigationDebug Navigation;
    if (Authority->TryGetSoldierNavigationDebug(Id,Navigation))
    {
     Row->SetBoolField(TEXT("moving"),Navigation.bMoving);
     Row->SetNumberField(TEXT("order_id"),Navigation.ActiveOrderId);
     const FVector V=Navigation.Velocity;
     Row->SetArrayField(TEXT("velocity_cm_s"),{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});
    }
    Positions.Add(MakeShared<FJsonValueObject>(Row));
   }
   Result->SetArrayField(TEXT("positions"),Positions);
  }
 }
 return PerformanceJson(Result);
}

bool UGuLiComponentSkillQALibrary::OrderPerformancePopulation(UObject* WorldContext,int32 Direction)
{
 UWorld* World=PerformanceAuthorityWorld(WorldContext);
 auto* Authority=World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
 if (!Authority || PerformancePopulation.World!=World || FMath::Abs(Direction)>2) return false;
 bool Accepted=true;
 for (int32 Team=0;Team<2;++Team)
 {
  auto* Owner=PerformancePopulation.Owners[Team].Get(); if (!Owner) return false;
  const auto& Ids=PerformancePopulation.Ids[Team];
  if (!Direction) { Authority->StopTaskSoldiers(Ids); continue; }
  for (int32 Begin=0;Begin<Ids.Num();Begin+=100)
  {
   FGuLiCommanderSelectionState Selection; Selection.SelectionRevision=1;
   FVector Center=FVector::ZeroVector; const int32 End=FMath::Min(Begin+100,Ids.Num());
   for (int32 Index=Begin;Index<End;++Index)
   {
    if ((Index-Begin)%25==0) Selection.Cohorts.AddDefaulted_GetRef().CohortId=FGuLiControlCohortId(Index/25+1);
    auto& Cohort=Selection.Cohorts.Last(); Cohort.MemberIds.Add(Ids[Index]); ++Cohort.AliveCount;
    FTransform Pose; if (!Authority->TryGetSoldierTransform(Ids[Index],Pose)) return false;
    Center+=Pose.GetLocation();
   }
   FGuLiMoveRequest Request; Request.SelectionRevision=1; Request.ClientCommandId=++PerformancePopulation.Command[Team];
   // The north/south review keeps both compact fixtures inside the authored
   // +/-90,000 cm navigation volume. Existing east/west captures are unchanged.
   const FVector Offset=FMath::Abs(Direction)==2 ? FVector(0,FMath::Sign(Direction)*10000.0,0)
    : FVector(Direction*(PerformancePopulation.bCombatLayout ? 20000.0 : 100000.0),0,0);
   Request.Target=Center/(End-Begin)+Offset;
   FGuLiCommandAck Ack; Accepted &= Authority->BeginMovePlanning(*Owner,Request,Selection,Ack);
  }
 }
 return Accepted;
}

FString UGuLiComponentSkillQALibrary::PreparePerformancePopulation(UObject* WorldContext,int32 Population,bool Moving,FVector CombatCenter)
{
 UWorld* World=PerformanceAuthorityWorld(WorldContext);
 auto* Authority=World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
 // Ground Crowd also observes Mass and environment bodies. This opt-in fixture
 // stays within the current map's existing capacity; it never raises runtime limits.
 if (!Authority || Population<2 || Population>600 || Population%2) return TEXT("{\"success\":false,\"error\":\"invalid_authority_or_count\"}");
 if (!Authority->HasSpawnedAuthorityPopulation()) return PerformancePopulationSnapshot(WorldContext);
 if (PerformancePopulation.World!=World) { PerformancePopulation=FPerformancePopulation(); PerformancePopulation.World=World; }
 if (PerformancePopulation.bSpawnAttempted) return PerformancePopulationSnapshot(WorldContext);
 auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
 auto* NavData=Navigation ? GuLiCommanderNavigationPolicy::ResolveRequiredNavigationData(*Navigation) : nullptr;
 if (!NavData || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World)) return TEXT("{\"success\":true,\"ready\":false}");
 // Validation inputs are process-local. The runner restores this console variable after PIE ends.
 IConsoleManager::Get().FindConsoleVariable(TEXT("guli.stronghold.TeamUnitCap"))->Set(Population/2,ECVF_SetByConsole);
 for (TActorIterator<AActor> It(World);It;++It)
  if (auto* Production=It->FindComponentByClass<UGuLiBuildingProductionComponent>()) Production->SetComponentTickEnabled(false);
 TArray<FGuLiSoldierStateItem> Existing; Authority->BuildSoldierStateSnapshot(Existing);
 for (const auto& State:Existing) Authority->ApplyDamage(State.SoldierId,1.e9f);
 // Keep population stable through paired captures without rounding ordinary
 // damage away (1e9 has a 64-point float ULP and hid hit flashes/health bars).
 auto Tuning=Authority->GetEffectiveRuntimeTuning(); Tuning.bOverrideMaxHealth=true; Tuning.MaxHealth=500000.f;
 Authority->ApplyRuntimeTuning(Tuning);
 PerformancePopulation.bSpawnAttempted=true; PerformancePopulation.Requested=Population;
 PerformancePopulation.bCombatLayout=!CombatCenter.IsNearlyZero();
 const int32 PerTeam=Population/2,Columns=FMath::CeilToInt(FMath::Sqrt(double(PerTeam)));
 for (int32 Team=0;Team<2;++Team)
 {
  const EGuLiTeam TeamId=Team==0 ? EGuLiTeam::Red : EGuLiTeam::Blue;
  auto* Owner=World->SpawnActor<AGuLiBattlePlayerState>(); Owner->SetReplicates(false);
  Owner->SetServerRoleAssignment(TeamId,EGuLiCommanderRole::Commander,Team);
  PerformancePopulation.Owners[Team]=Owner;
  for (int32 Index=0;Index<PerTeam*4 && PerformancePopulation.Ids[Team].Num()<PerTeam;++Index)
  {
   // Current map's authored navigation volume is +/-90,000 cm. Keep the entire
   // fixture and its 100,000 cm eastward destination inside that volume.
   const FVector Desired=PerformancePopulation.bCombatLayout
    ? CombatCenter+FVector((Team==0 ? 1.0 : -1.0)*(1000.0+(Index/Columns)*650.0),(Index%Columns-(Columns-1)*.5)*650.0,0)
    : FVector(-40000.0+(Index%Columns-(Columns-1)*.5)*650.0,
      (Team==0 ? 35000.0 : -35000.0)+(Index/Columns-(Columns-1)*.5)*650.0,0);
   FNavLocation Ground; if (!Navigation->ProjectPointToNavigation(Desired,Ground,FVector(100,100,50000),NavData)) continue;
   FGuLiSoldierId Id; if (Authority->SpawnDebugSoldier(TeamId,1,Ground.Location,Id)) PerformancePopulation.Ids[Team].Add(Id);
  }
 }
 const int32 Actual=PerformancePopulation.Ids[0].Num()+PerformancePopulation.Ids[1].Num();
 if (Actual!=Population) return FString::Printf(TEXT("{\"success\":false,\"error\":\"finite_grid_spawn_incomplete\",\"actual\":%d}"),Actual);
 if (Moving && !OrderPerformancePopulation(WorldContext,1)) return TEXT("{\"success\":false,\"error\":\"native_move_rejected\"}");
 return PerformancePopulationSnapshot(WorldContext);
}

bool UGuLiComponentSkillQALibrary::SetPerformanceMiningVisual(AActor* Owner,bool Active,FVector Target)
{
 if (!Owner || !Owner->GetWorld()->IsPlayInEditor() || Owner->GetNetMode()!=NM_Client || Target.ContainsNaN()) return false;
 auto* Component=Owner->FindComponentByClass<UGuLiMiningPresentationComponent>();
 if (!Component || !Component->IsReady()) return false;
 Component->ApplyMining(Active,Target); return true;
}

static FAutoConsoleCommand StartFlightPIECommand(TEXT("gs.Flights.PIE"),
	TEXT("Explicit editor acceptance: four real Commander/Ground/Air/Commander seats. Requires saved separate PlayerStarts."),
	FConsoleCommandDelegate::CreateLambda([]{ UGuLiComponentSkillQALibrary::StartMixedFlightPIE(); }));

void UGuLiComponentSkillQALibrary::SetGMPanelOpen(APlayerController* Controller, bool bOpen)
{
	auto& BattleController = *CastChecked<AGuLiBattlePlayerController>(Controller);
	if (BattleController.IsGMPanelOpen() != bOpen) BattleController.ToggleGMPanel();
}

void UGuLiComponentSkillQALibrary::FlushPlayerInput(APlayerController* Controller)
{
	Controller->FlushPressedKeys();
}

bool UGuLiComponentSkillQALibrary::StartPIE(int32 Mode, int32 Clients)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress() || Mode < 0 || Mode > 2 || Clients < 1 || Clients > 4) return false;
	static TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
	Settings->SetPlayNetMode(EPlayNetMode(Mode)); Settings->SetPlayNumberOfClients(Clients);
	Settings->SetRunUnderOneProcess(true); Settings->bLaunchSeparateServer = false;
	Settings->NewWindowWidth = 640; Settings->NewWindowHeight = 360;
	FRequestPlaySessionParams Request;
	Request.EditorPlaySettings = Settings.Get(); Request.SessionDestination = EPlaySessionDestinationType::InProcess;
	Request.WorldType = EPlaySessionWorldType::PlayInEditor; Request.bAllowOnlineSubsystem = false;
	GEditor->RequestPlaySession(Request); return true;
}
bool UGuLiComponentSkillQALibrary::SelectRadius(APlayerController* Controller, FVector Center, int32 RequestId, bool bAdd)
{
	if (!Controller || !Controller->IsLocalController() || !Controller->GetWorld()->IsPlayInEditor()) return false;
	auto* Sync = Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>();
	if (!Sync || !Sync->IsSoldierStreamReady()) return false;
	FGuLiSelectionRequest Request;
	Request.Center = Center; Request.RadiusPreset = EGuLiSelectionRadiusPreset::Large;
	Request.ClientRequestId = RequestId; Request.KnownSelectionRevision = Sync->GetSelectionState().SelectionRevision;
	Request.Modifier = bAdd ? EGuLiSelectionModifier::Add : EGuLiSelectionModifier::Replace;
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	Sync->SubmitSelectionRequest(Request); return true;
}
bool UGuLiComponentSkillQALibrary::SubmitSkill(UGuLiCommanderSkillComponent* Component, FGuid RequestId,
	FName GlobalSkillId, int64 SelectionRevision, bool bHasPoint, FVector Point)
{
	if (!Component || !Component->GetWorld()->IsPlayInEditor()) return false;
	auto* PC = Cast<APlayerController>(Component->GetOwner()->GetOwner());
	if (!PC || !PC->IsLocalController()) return false;
	FGuLiActiveSkillRequest Request;
	Request.RequestId = RequestId; Request.MatchEpoch = Component->GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
	Request.GlobalSkillId = GlobalSkillId; Request.SelectionRevision = SelectionRevision;
	Request.bHasGroundPoint = bHasPoint; Request.GroundPoint = Point;
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	Component->ServerRequestSkill(Request); return true;
}
void UGuLiComponentSkillQALibrary::PressQ(UGuLiCommanderSkillComponent* Component, bool bHasPoint, FVector Point)
{
	if (!Component || !Component->GetWorld()->IsPlayInEditor()) return;
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	Component->ActivateSelectedUnits(bHasPoint, Point);
}
FString UGuLiComponentSkillQALibrary::SelectionSnapshot(APlayerController* Controller)
{
	FString Result;
	const auto Writer = TJsonWriterFactory<>::Create(&Result);
	Writer->WriteObjectStart();
	if (const auto* Sync = Controller ? Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>() : nullptr)
	{
		Writer->WriteValue(TEXT("ready"), Sync->IsSoldierStreamReady());
		Writer->WriteValue(TEXT("pending"), Sync->HasUnresolvedSelectionIntent());
		Writer->WriteValue(TEXT("revision"), Sync->GetSelectionState().SelectionRevision);
		Writer->WriteArrayStart(TEXT("members"));
		for (const auto& Cohort : Sync->GetSelectionState().Cohorts) for (auto Id : Cohort.MemberIds) Writer->WriteValue(Id.Value);
		Writer->WriteArrayEnd();
	}
	Writer->WriteObjectEnd(); Writer->Close(); return Result;
}

bool UGuLiComponentSkillQALibrary::CommitShipChoice(UGuLiShipBuildComponent* Component, FName NodeId, FString& Error)
{
	if (!Component || !Component->GetOwner()->HasAuthority() || !Component->GetWorld()->IsPlayInEditor()) return false;
	const auto State = Component->GetBuildState();
	// Python's editor callspace override also intercepts Client RPCs triggered by a server-local commit.
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	const auto Result = Component->CommitConfirmedChoice(FGuid::NewGuid(), NodeId, State.MatchEpoch, State.BuildRevision);
	Error = Result.Reason;
	return Result.bCommitted;
}

FString UGuLiComponentSkillQALibrary::WingmanSnapshot(UObject* WorldContext)
{
	FString Result;
	const auto Writer = TJsonWriterFactory<>::Create(&Result);
	Writer->WriteArrayStart();
	for (TActorIterator<APlayerController> It(WorldContext->GetWorld()); It; ++It)
	{
		const auto* Relay = It->FindComponentByClass<UGuLiWingmanRelayComponent>();
		if (!Relay) continue;
		const auto& State = Relay->GetRelayState();
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("controller"), It->GetName());
		Writer->WriteValue(TEXT("local"), It->IsLocalController());
		Writer->WriteValue(TEXT("lifecycle"), int32(State.Lease.Lifecycle));
		Writer->WriteValue(TEXT("carrier_valid"), State.LatestCarrierSource.IsValid());
		Writer->WriteValue(TEXT("config_usable"), State.AbilityConfig.IsUsableByLeaseOwner());
		Writer->WriteValue(TEXT("bootstrap_valid"), Relay->GetLastClientBootstrap().IsWellFormed());
		Writer->WriteValue(TEXT("accepted_sequence"), State.LastAcceptedCandidateSequence);
		Writer->WriteValue(TEXT("atomic_attempts"), Relay->GetListenSmokeAtomicBuildAttemptCount());
		Writer->WriteValue(TEXT("atomic_accepted"), Relay->GetListenSmokeAtomicAcceptedCount());
		Writer->WriteValue(TEXT("atomic_rejected"), Relay->GetListenSmokeAtomicRejectedCount());
		Writer->WriteValue(TEXT("atomic_reject_reason"), int32(Relay->GetListenSmokeLastAtomicResultRejectReason()));
		if (const auto* Ship = Cast<AGuLiStrikeShip>(It->GetPawn()))
		{
			const auto* Movement = Ship->FindComponentByClass<UGuLiShipMovementComponent>();
			Writer->WriteValue(TEXT("applied_loadout_revision"), Movement->GetAppliedLoadoutRevision());
		}
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd(); Writer->Close(); return Result;
}
