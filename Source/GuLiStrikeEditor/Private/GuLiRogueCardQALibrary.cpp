#include "GuLiRogueCardQALibrary.h"
#include "Gameplay/Cards/GuLiRogueCardPresentation.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Components/BackgroundBlur.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"

namespace
{
	int32 Integer(UObject* Object,FName Name)
	{ const auto* P=Object ? FindFProperty<FIntProperty>(Object->GetClass(),Name) : nullptr;
		return P ? P->GetPropertyValue_InContainer(Object) : INDEX_NONE; }
}

bool UGuLiRogueCardQALibrary::Action(APlayerController* Controller,FName Command,int32 Index)
{
	auto* PC=Controller;
	if (!PC || !PC->IsLocalController() || !PC->GetWorld()->IsPlayInEditor()) return false;
	auto* P=PC->FindComponentByClass<UGuLiRogueCardPresentation>(); if (!P) return false;
	TGuardValue<bool> Guard(GAllowActorScriptExecutionInEditor,false);
	if (Command==TEXT("Open")) { P->Open(); return P->IsOpen(); }
	if (Command==TEXT("F4Down") || Command==TEXT("F4Up"))
	{ PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::F4,
		Command==TEXT("F4Down")?IE_Pressed:IE_Released,FPlatformTime::Cycles64())); return true; }
	if (Command==TEXT("Cancel")) { P->Cancel(); return true; }
	if (Command==TEXT("CleanupTwice")) { P->Close(true); P->Close(true); P->Cleanup(); return true; }
	if (Command==TEXT("Hit") && IsValid(P->Director))
	{
		// Exercise the real Blueprint phase/lock/two-click path. Pointer hit geometry is separately player-reviewed.
		auto* Fn=P->Director->FindFunction(TEXT("ActivateHit"));
		auto* Input=Fn ? FindFProperty<FIntProperty>(Fn,TEXT("HitIndex")) : nullptr;
		if (!Input) return false; FStructOnScope Params(Fn); Input->SetPropertyValue_InContainer(Params.GetStructMemory(),Index);
		P->Director->ProcessEvent(Fn,Params.GetStructMemory()); return true;
	}
	return false;
}

FString UGuLiRogueCardQALibrary::Snapshot(APlayerController* Controller)
{
	FString Result; auto W=TJsonWriterFactory<>::Create(&Result); W->WriteObjectStart();
	if (Controller && Controller->GetWorld()->IsPlayInEditor())
	{
		auto* P=Controller->FindComponentByClass<UGuLiRogueCardPresentation>();
		W->WriteValue(TEXT("controller"),Controller->GetPathName()); W->WriteValue(TEXT("local"),Controller->IsLocalController());
		bool bReady=false;
		if (UFunction* F=Controller->FindFunction(TEXT("CanIssueCommanderOrders"))) Controller->ProcessEvent(F,&bReady);
		W->WriteValue(TEXT("ready"),bReady);
		if (const auto* PS=Controller->GetPlayerState<AGuLiBattlePlayerState>())
		{ W->WriteValue(TEXT("team"),int32(PS->GetTeam())); W->WriteValue(TEXT("commander"),PS->IsCommander()); }
		if (auto* Local=Controller->GetLocalPlayer();Local && Local->ViewportClient)
		{ W->WriteValue(TEXT("viewmode"),Local->ViewportClient->ViewModeIndex);
			W->WriteValue(TEXT("materials"),Local->ViewportClient->EngineShowFlags.Materials);
			W->WriteValue(TEXT("detail_lighting"),Local->ViewportClient->EngineShowFlags.LightingOnlyOverride); }
		int32 F4Debug=0;
		if (Controller->PlayerInput) for (const auto& B:Controller->PlayerInput->DebugExecBindings)
			if (B.Key==EKeys::F4 && B.Command.Contains(TEXT("detaillighting"))) ++F4Debug;
		W->WriteValue(TEXT("f4_debug_count"),F4Debug);
		if (P)
		{
			W->WriteValue(TEXT("open"),P->bOpen); W->WriteValue(TEXT("closing"),P->bClosing);
			W->WriteValue(TEXT("committed"),P->bCommitted); W->WriteValue(TEXT("submitting"),P->bSubmitting);
			W->WriteValue(TEXT("session"),P->SessionId.ToString()); W->WriteValue(TEXT("fade"),P->Fade);
			W->WriteValue(TEXT("phase"),Integer(P->Director,TEXT("Phase")));
			W->WriteValue(TEXT("selected"),Integer(P->Director,TEXT("SelectedIndex")));
			W->WriteValue(TEXT("overlay"),IsValid(P->Overlay) && P->Overlay->IsInViewport());
			W->WriteValue(TEXT("blur"),P->Overlay && P->Overlay->Blur ? P->Overlay->Blur->GetBlurStrength() : 0.f);
			W->WriteValue(TEXT("capture"),IsValid(P->Capture)); W->WriteValue(TEXT("director"),IsValid(P->Director));
			W->WriteValue(TEXT("target"),IsValid(P->Target)); W->WriteValue(TEXT("ticking"),P->IsComponentTickEnabled());
		}
		if (const auto* V=Controller->GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		{ const auto C=V->GetCounters(); W->WriteValue(TEXT("upgrade_active"),C.UpgradeActive);
			W->WriteValue(TEXT("upgrade_visible"),C.UpgradeVisible); W->WriteValue(TEXT("upgrade_components"),C.UpgradeComponents);
			W->WriteValue(TEXT("upgrade_particles"),C.UpgradeVisibleParticles); W->WriteValue(TEXT("upgrade_ms"),C.UpgradeUpdateMilliseconds); }
	}
	W->WriteObjectEnd(); W->Close(); return Result;
}

void UGuLiRogueCardQALibrary::Replay(APlayerController* Controller,FGuid Session,const FString& CardId,bool bReady)
{
	if (!Controller || !Controller->IsLocalController() || !Controller->GetWorld()->IsPlayInEditor()) return;
	TGuardValue<bool> Guard(GAllowActorScriptExecutionInEditor,false);
	if (auto* Channel=Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
	{ if (bReady) Channel->ServerRogueCardPresentationClosed(Session); else Channel->ServerConfirmRogueCard(Session,CardId); }
}

FString UGuLiRogueCardQALibrary::FrameTimings(APlayerController* Controller)
{
	const auto* Local=Controller ? Controller->GetLocalPlayer() : nullptr;
	const auto* Data=Local && Local->ViewportClient ? Local->ViewportClient->GetStatUnitData() : nullptr;
	if (!Data) return TEXT("{}");
	return FString::Printf(TEXT("{\"game_ms\":%.6f,\"render_ms\":%.6f,\"gpu_ms\":%.6f,\"frame_ms\":%.6f,\"unit_stat\":%s}"),
		Data->RawGameThreadTime,Data->RawRenderThreadTime,Data->RawGPUFrameTime[0],Data->RawFrameTime,
		Local->ViewportClient->IsStatEnabled(TEXT("Unit"))?TEXT("true"):TEXT("false"));
}

FString UGuLiRogueCardQALibrary::UpgradeSlots(APlayerController* PC)
{
	FString Result; auto W=TJsonWriterFactory<>::Create(&Result); W->WriteObjectStart();
	if (PC && PC->GetWorld()->IsPlayInEditor())
		if (const auto* V=PC->GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
		{
			W->WriteValue(TEXT("epoch"),V->Epoch); W->WriteArrayStart(TEXT("slots"));
			for (const auto& B:V->UpgradeBlocks) for (int32 I=0;I<B.Slots.Num();++I)
			{
				const auto& S=B.Slots[I]; if (!S.bActive) continue;
				W->WriteObjectStart(); W->WriteValue(TEXT("id"),S.Soldier.Value);
				W->WriteValue(TEXT("team"),int32(S.Team)); W->WriteValue(TEXT("visible"),B.Colors[I].A>0);
				W->WriteValue(TEXT("x"),B.Positions[I].X); W->WriteValue(TEXT("y"),B.Positions[I].Y); W->WriteValue(TEXT("z"),B.Positions[I].Z);
				W->WriteValue(TEXT("motes"),B.Parameters[I].Z); W->WriteValue(TEXT("start"),S.StartTime); W->WriteObjectEnd();
			}
			W->WriteArrayEnd();
		}
	W->WriteObjectEnd(); W->Close(); return Result;
}
