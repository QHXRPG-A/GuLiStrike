#include "Gameplay/GroundMech/UI/GuLiGroundHUDWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"

void UGuLiGroundHUDWidget::BuildAmbientLoop(UCanvasPanel* Canvas,int32 Index,FVector2D Position)
{
    auto& Clip=AmbientClips[Index];
    Clip=MakeClip(Index==0?TEXT("AmbientHealth"):TEXT("AmbientMap"),TEXT("MotionAmbientMaterial"));
    auto* CanvasSlot=Canvas->AddChildToCanvas(Clip.Image.Get());CanvasSlot->SetPosition(Position);CanvasSlot->SetSize({80,80});CanvasSlot->SetZOrder(-1);
    if(auto* MID=Clip.Material.Get())
    {
        MID->SetScalarParameterValue(TEXT("Period"),FMath::Max(1.f,Number(TEXT("MotionAmbientSeconds"),4)));
        MID->SetScalarParameterValue(TEXT("SeamSeconds"),FMath::Clamp(Number(TEXT("MotionAmbientSeamSeconds"),.25f),.05f,.5f));
        MID->SetScalarParameterValue(TEXT("Opacity"),FMath::Clamp(Number(TEXT("MotionAmbientOpacity"),.18f),0.f,.25f));
        MID->SetScalarParameterValue(TEXT("PhaseOffset"),Index*1.7f);
    }
}
void UGuLiGroundHUDWidget::UpdateAmbient(bool ForcePause)
{
    const bool Running=!ForcePause && !bRogueHidden && !ModalMode && EntryStarted<0 && !bWasDead && BoundPawn.IsValid() && IsVisible();
    if(Running==bAmbientRunning)return;
    // Slate UI materials use FSlateRHIRenderer's AppDilatedTime, not World time.
    // Match MaterialExpressionTime(ignore_pause=false) across PIE/world pauses.
    const double Now=FApp::GetCurrentTime()-GStartTime;
    if(bAmbientRunning)AmbientPhase=FMath::Fmod(AmbientPhase+FMath::Max(0.,Now-AmbientStarted),FMath::Max(1.f,Number(TEXT("MotionAmbientSeconds"),4)));
    AmbientStarted=Now;bAmbientRunning=Running;++AmbientTransitions;
    for(auto& Clip:AmbientClips)
    {
        if(auto* Image=Clip.Image.Get())Image->SetVisibility(Running?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* MID=Clip.Material.Get())
        {
            MID->SetScalarParameterValue(TEXT("Run"),Running?1:0);
            MID->SetScalarParameterValue(TEXT("StartTime"),Now);
            MID->SetScalarParameterValue(TEXT("PausedPhase"),AmbientPhase);
        }
    }
    // Material Time drives only these two brushes. No tick-time uniform updates,
    // no media decoding, and no parent Slate invalidation for the loop frames.
}
