#include "GuLiComponentSkillQALibrary.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Network/GuLiHybridActorComponent.h"
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
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "MassEntityHandle.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Gameplay/Resources/GuLiMiningPresentationComponent.h"
#include "Commander/UI/GuLiSceneUISubsystem.h"
#include "Commander/UI/GuLiSceneUIWidget.h"

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

bool UGuLiComponentSkillQALibrary::SetPerformanceMiningVisual(AActor* Owner,bool Active,FVector Target)
{
 if (!Owner || !Owner->GetWorld()->IsPlayInEditor() || Owner->GetNetMode()!=NM_Client || Target.ContainsNaN()) return false;
 auto* Component=Owner->FindComponentByClass<UGuLiMiningPresentationComponent>();
 if (!Component || !Component->IsReady()) return false;
 Component->ApplyMining(Active,Target); return true;
}


void UGuLiComponentSkillQALibrary::SetGMPanelOpen(APlayerController* Controller, bool bOpen)
{
	auto& BattleController = *CastChecked<AGuLiBattlePlayerController>(Controller);
	if (BattleController.IsGMPanelOpen() != bOpen) BattleController.ToggleGMPanel();
}

void UGuLiComponentSkillQALibrary::FlushPlayerInput(APlayerController* Controller)
{
	Controller->FlushPressedKeys();
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
bool UGuLiComponentSkillQALibrary::PrepareSoldierPositions(APlayerController* Controller,
	const TArray<int64>& Ids, const TArray<FVector>& Positions)
{
	UWorld* World=Controller?Controller->GetWorld():nullptr;
	if (!World || !World->IsPlayInEditor() || !Controller->IsLocalController()
		|| !World->GetMapName().Contains(TEXT("LVL_CommanderMassPrototype"))
		|| Ids.IsEmpty() || Ids.Num()>25 || Ids.Num()!=Positions.Num()) return false;
	auto* Authority=World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	const auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>();
	if (!Authority || !Hybrid || !Hybrid->CanPublishFacts()) return false;
	TArray<FGuLiMassExternalUnit> Units;TSet<int64> Seen;
	for (int32 I=0;I<Ids.Num();++I)
	{
		if (Ids[I]<1 || Ids[I]>MAX_uint32 || Seen.Contains(Ids[I]) || Positions[I].ContainsNaN()) return false;
		Seen.Add(Ids[I]);const FGuLiSoldierId Id{uint32(Ids[I])};
		const auto Key=Authority->MakeSoldierTargetHandle(Id);FGuLiHybridObjectKey Object;
		FTransform Previous;
		if (!Hybrid->ResolveTarget(Key,Object) || !Hybrid->HasSimulation(Object)
			|| !Authority->TryGetSoldierTransform(Id,Previous)) return false;
		FVector Surface;if (!Authority->ProjectExternalUnitLocation(Positions[I],Surface)) return false;
		auto& Unit=Units.AddDefaulted_GetRef();Unit.Id=Id;
		Previous.SetLocation(Surface);Unit.Transform=Previous;
	}
	return Authority->ApplyExternalUnitState(Units,FGuid::NewGuid(),false,false,true);
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
    if (UWorld* World = IsValid(WorldContext) ? WorldContext->GetWorld() : nullptr)
    for (TActorIterator<AGuLiWingmanPawn> It(World); It; ++It)
    {
        const auto* Binding = UGuLiHybridActorComponent::Find(*It);
        if (!Binding) continue;
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("object"), Binding->GetObjectKey().StableId.ToString());
        Writer->WriteValue(TEXT("generation"), Binding->GetObjectKey().LifeGeneration);
        Writer->WriteValue(TEXT("local_simulation"), UGuLiHybridActorComponent::HasSimulation(*It));
        Writer->WriteValue(TEXT("alive"), It->GetRuntimeState().Dynamics.bAlive);
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd(); Writer->Close(); return Result;
}
