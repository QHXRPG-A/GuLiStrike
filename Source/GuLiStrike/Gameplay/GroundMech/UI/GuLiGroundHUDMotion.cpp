#include "Gameplay/GroundMech/UI/GuLiGroundHUDWidget.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDVisuals.h"
#include "Gameplay/Data/GuLiIconCatalog.h"
#include "Gameplay/Data/Generated/GuLiStrikeGroundHUDTableRows.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/App.h"

namespace
{
float Ease(float T){return 1-FMath::Pow(1-FMath::Clamp(T,0.f,1.f),3.f);}
void Attach(UCanvasPanel* Canvas,UImage* Image,FVector2D P,FVector2D Size)
{auto* Slot=Canvas->AddChildToCanvas(Image);Slot->SetPosition(P);Slot->SetSize(Size);}
}
void UGuLiGroundHUDWidget::LoadMotionStyle()
{
    auto Seconds=[this](const TCHAR* Key,float Default){return FMath::Clamp(Number(Key,Default),.01f,2.f);};
    MotionStyle.Intro=Seconds(TEXT("MotionIntroSeconds"),.45f);
    MotionStyle.Open=Seconds(TEXT("MotionModalOpenSeconds"),.18f);MotionStyle.Close=Seconds(TEXT("MotionModalCloseSeconds"),.12f);
    MotionStyle.Cast=Seconds(TEXT("MotionCastSeconds"),.18f);MotionStyle.Ready=Seconds(TEXT("MotionReadySeconds"),.30f);
    MotionStyle.ReadyFade=Seconds(TEXT("MotionReadyFadeSeconds"),.10f);MotionStyle.Aim=Seconds(TEXT("MotionAimSeconds"),.12f);
    MotionStyle.Slide=FMath::Clamp(Number(TEXT("MotionSlidePixels"),18),0.f,32.f);
    MotionStyle.Shot=Seconds(TEXT("MotionShotSeconds"),.18f);MotionStyle.Kick=FMath::Clamp(Number(TEXT("MotionShotKickPixels"),4),0.f,4.f);
    MotionStyle.MaxKick=FMath::Clamp(Number(TEXT("MotionShotMaxPixels"),12),0.f,12.f);
    MotionStyle.Jitter=FMath::Clamp(Number(TEXT("MotionShotJitterPixels"),1.5f),0.f,1.5f);
    MotionStyle.Hit=Seconds(TEXT("MotionHitSeconds"),.16f);MotionStyle.Kill=Seconds(TEXT("MotionKillSeconds"),.28f);
}
FGuLiGroundHUDClip UGuLiGroundHUDWidget::MakeClip(const TCHAR* Name,const TCHAR* MaterialKey)
{
    FGuLiGroundHUDClip Clip;
    auto* Image=WidgetTree->ConstructWidget<UImage>();Clip.Image=Image;Image->SetVisibility(ESlateVisibility::Collapsed);
    const FString Prefix=FString(TEXT("Motion"))+Name;
    const auto* Row=Style?Style->FindRow<FGuLiStrikeGroundHUDElementsRow>(*(Prefix+TEXT("Atlas")),TEXT("HUD motion"),false):nullptr;
    const auto* MaterialRow=Style?Style->FindRow<FGuLiStrikeGroundHUDElementsRow>(MaterialKey,TEXT("HUD motion"),false):nullptr;
    auto* Texture=Row?Cast<UTexture2D>(Row->Texture.LoadSynchronous()):nullptr;
    auto* Parent=MaterialRow?Cast<UMaterialInterface>(MaterialRow->Texture.LoadSynchronous()):nullptr;
    if(!Texture||!Parent){UE_LOG(LogTemp,Error,TEXT("Ground HUD motion resources missing: %s"),Name);return Clip;}
    auto* MID=UMaterialInstanceDynamic::Create(Parent,this);Clip.Material=MID;Clip.Frames=FMath::Max(1,Row->Sprite);
    Art.AddUnique(Texture);Art.AddUnique(Parent);Art.Add(MID);
    MID->SetTextureParameterValue(TEXT("Atlas"),Texture);
    MID->SetScalarParameterValue(TEXT("LastFrame"),Clip.Frames-1);
    for(const TCHAR* Key:{TEXT("Columns"),TEXT("Rows"),TEXT("PaddingX"),TEXT("PaddingY")})
        MID->SetScalarParameterValue(Key,Number(*(Prefix+Key),0));
    MID->SetVectorParameterValue(TEXT("Tint"),GuLiGroundUI::Light(Color(TEXT("Line"))));
    MID->SetVectorParameterValue(TEXT("Panel"),Color(TEXT("Panel")));
    Image->SetBrushFromMaterial(MID);Image->SetRenderTransformPivot({.5,.5});return Clip;
}
void UGuLiGroundHUDWidget::ShowClip(FGuLiGroundHUDClip& Clip,float Frame,float Opacity)
{
    auto* Image=Clip.Image.Get();auto* MID=Clip.Material.Get();if(!Image||!MID)return;
    const auto Visible=Opacity>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
    if(Image->GetVisibility()!=Visible)Image->SetVisibility(Visible);
    if(Opacity<=0)return;
    Frame=FMath::Clamp(Frame,0.f,float(Clip.Frames-1));Opacity=FMath::Clamp(Opacity,0.f,1.f);
    if(!FMath::IsNearlyEqual(Clip.LastFrame,Frame,.001f)){Clip.LastFrame=Frame;MID->SetScalarParameterValue(TEXT("Frame"),Frame);}
    if(!FMath::IsNearlyEqual(Clip.LastOpacity,Opacity,.001f)){Clip.LastOpacity=Opacity;MID->SetScalarParameterValue(TEXT("Opacity"),Opacity);}
}
void UGuLiGroundHUDWidget::BuildSkillMotion(UCanvasPanel* Canvas,int32 SkillIndex)
{
    if(SkillIndex==3)
    {
        CooldownClip=MakeClip(TEXT("Cooldown"),TEXT("MotionCooldownMaterial"));
        Attach(Canvas,CooldownClip.Image.Get(),{12,0},{117,117});
    }
    CastClips[SkillIndex]=MakeClip(TEXT("Cast"));ReadyClips[SkillIndex]=MakeClip(TEXT("Ready"));
    Attach(Canvas,CastClips[SkillIndex].Image.Get(),{6,-4},{129,121});
    Attach(Canvas,ReadyClips[SkillIndex].Image.Get(),{10,-2},{121,121});
    if(auto* MID=ReadyClips[SkillIndex].Material.Get())MID->SetVectorParameterValue(TEXT("Tint"),GuLiGroundUI::Light(Color(TEXT("Accent"))));
}
void UGuLiGroundHUDWidget::BuildMotionLayout(UCanvasPanel* Design,UWidget* Header)
{
    EntryClip=MakeClip(TEXT("Intro"));ModalClip=MakeClip(TEXT("Intro"));
    for(auto* Image:{EntryClip.Image.Get(),ModalClip.Image.Get()})
    {
        auto* ClipSlot=Design->AddChildToCanvas(Image);ClipSlot->SetAnchors(FAnchors(0,0,1,1));ClipSlot->SetOffsets(FMargin());ClipSlot->SetZOrder(20);
    }
    for(auto* Widget:Battle->GetAllChildren())
    {
        EntryPanels.Add(Widget);const auto A=CastChecked<UCanvasPanelSlot>(Widget->Slot)->GetAnchors().Minimum;
        EntryDirections.Add(A.X==.5?FVector2D(0,1):FVector2D(A.X>.5?1:-1,0));
    }
    EntryPanels.Add(Header);EntryDirections.Add({0,-1});
    Modal->SetRenderTransformPivot({.5,.5});
}
void UGuLiGroundHUDWidget::ClearMotion()
{
    EntryStarted=-1;ShotStarted=-1;ShotKick=0;HitUntil=0;bKill=false;
    ShowClip(EntryClip,0,0);ShowClip(ModalClip,0,0);ShowClip(CooldownClip,0,0);
    bCooling=false;LastCooldownProgress=-1;
    for(int32 I=0;I<4;++I)
    {CastStarted[I]=ReadyStarted[I]=-1;HeldAlpha[I]=0;ShowClip(CastClips[I],0,0);ShowClip(ReadyClips[I],0,0);}
    for(const auto& Weak:EntryPanels)if(auto* W=Weak.Get()){W->SetRenderOpacity(1);W->SetRenderTranslation(FVector2D::ZeroVector);}
    if(Chrome)Chrome->SetRenderOpacity(1);
}
void UGuLiGroundHUDWidget::StartEntry()
{
    if(bRogueHidden||ModalMode||!BoundPawn.IsValid())return;
    EntryStarted=FPlatformTime::Seconds();++EntryPlays;
    for(const auto& W:EntryPanels)if(W.IsValid())W->SetRenderOpacity(0);
    if(Chrome)Chrome->SetRenderOpacity(0);
}
float UGuLiGroundHUDWidget::ShotExpansion(double Now) const
{
    const float T=FMath::Clamp(float((Now-ShotStarted)/MotionStyle.Shot),0.f,1.f);
    return ShotStarted>=0?ShotKick*FMath::Square(1-T):0;
}
void UGuLiGroundHUDWidget::Motion(const FGuLiGroundHUDMotion& E)
{
    if(bRogueHidden||ModalMode||bWasDead)return;
    const double Now=FPlatformTime::Seconds();
    const bool FreshShot=E.Shots&&E.ShotAge<MotionStyle.Shot;
    if(FreshShot)
    {
        ShotKick=FMath::Min(MotionStyle.MaxKick,ShotExpansion(Now)+E.Shots*MotionStyle.Kick);
        ShotStarted=Now-E.ShotAge;ShotPulses+=E.Shots;
    }
    for(int32 I=0;I<4;++I)
    {
        // Aim has a held-state fade. It does not use the cast burst.
        if((E.Cast&(1<<I))&&I!=1&&(I!=0||FreshShot))
        {CastStarted[I]=Now-(I==0?E.ShotAge:0);++CastPlays[I];}
        if(E.Ready&(1<<I)){ReadyStarted[I]=Now;++ReadyPlays[I];}
    }
}
void UGuLiGroundHUDWidget::TickMotion(double Now,float Delta)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundHUD_Motion);
    bool Active=false;
    if(EntryStarted>=0)
    {
        Active=true;const float Age=Now-EntryStarted,T=Age/MotionStyle.Intro;
        for(int32 I=0;I<EntryPanels.Num();++I)if(auto* W=EntryPanels[I].Get())
        {
            const float Delay=FMath::Min(I*.012f,MotionStyle.Intro*.25f);
            const float A=Ease((Age-Delay)/(MotionStyle.Intro-Delay));
            W->SetRenderOpacity(A);W->SetRenderTranslation(EntryDirections[I]*(MotionStyle.Slide*(1-A)));
        }
        Chrome->SetRenderOpacity(Ease(T));
        ShowClip(EntryClip,T*(EntryClip.Frames-1),FMath::Clamp((1-T)*5,0.f,1.f)*.8f);
        if(T>=1){EntryStarted=-1;ShowClip(EntryClip,0,0);}
    }
    if(ModalStarted>=0)
    {
        Active=true;const float T=FMath::Clamp(float((Now-ModalStarted)/(bModalClosing?MotionStyle.Close:MotionStyle.Open)),0.f,1.f);
        ModalAlpha=FMath::Lerp(ModalFrom,bModalClosing?0.f:1.f,Ease(T));
        Modal->SetRenderOpacity(ModalAlpha);Modal->SetRenderScale(FVector2D(.985f+.015f*ModalAlpha));
        Modal->SetRenderTranslation({0,MotionStyle.Slide*(1-ModalAlpha)});
        ShowClip(ModalClip,(ModalClip.Frames-1)*ModalAlpha,(bModalClosing?T:1-T)*.65f);
        if(T>=1){ModalStarted=-1;ShowClip(ModalClip,0,0);if(bModalClosing)FinishCloseModal();}
    }
    if(!ModalMode)
    {
        for(int32 I=0;I<4;++I)
        {
            const float Target=I==1&&bHeldAim?1.f:I==2&&bHeldJet?1.f:0.f;
            if(!FMath::IsNearlyEqual(HeldAlpha[I],Target,.001f))
            {Active=true;HeldAlpha[I]=FMath::FInterpConstantTo(HeldAlpha[I],Target,Delta,1/MotionStyle.Aim);}
            float Opacity=HeldAlpha[I]*.65f,Frame=CastClips[I].Frames-1;
            if(CastStarted[I]>=0)
            {
                const float T=(Now-CastStarted[I])/MotionStyle.Cast;
                if(T<1){Active=true;Frame=T*(CastClips[I].Frames-1);Opacity=FMath::Max(Opacity,FMath::Min(1.f,(1-T)*4));}
                else CastStarted[I]=-1;
            }
            ShowClip(CastClips[I],Frame,Opacity);
            if(ReadyStarted[I]>=0)
            {
                const float Age=Now-ReadyStarted[I];
                const float Alpha=Age<=MotionStyle.Ready?1.f:1-(Age-MotionStyle.Ready)/MotionStyle.ReadyFade;
                ShowClip(ReadyClips[I],Age/MotionStyle.Ready*(ReadyClips[I].Frames-1),Alpha);
                if(Alpha<=0)ReadyStarted[I]=-1;else Active=true;
            }
        }
        ShowClip(CooldownClip,0,bCooling?1.f:0.f);
        if(bCooling&&Data&&CooldownClip.Material.IsValid())
        {
            const float Progress=BlinkDuration>0?1-FMath::Clamp(float(BlinkEndsAt-Data->GetLogicalSeconds())/BlinkDuration,0.f,1.f):1;
            if(!FMath::IsNearlyEqual(Progress,LastCooldownProgress,.0001f))
            {Active=true;LastCooldownProgress=Progress;CooldownClip.Material->SetScalarParameterValue(TEXT("Progress"),Progress);}
        }
    }
    if(Active)++MotionFrames;
}
FString UGuLiGroundHUDWidget::GetMotionDiagnostics() const
{
    auto J=MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("entry_plays"),EntryPlays);J->SetNumberField(TEXT("shots"),ShotPulses);
    J->SetNumberField(TEXT("hit_plays"),HitPlays);J->SetNumberField(TEXT("kill_plays"),KillPlays);
    J->SetNumberField(TEXT("ordinary_refreshes"),RegularRefreshes);J->SetNumberField(TEXT("motion_frames"),MotionFrames);
    J->SetNumberField(TEXT("generation"),ObservedGeneration);J->SetNumberField(TEXT("modal"),ModalMode);
    J->SetBoolField(TEXT("hidden"),bRogueHidden);J->SetBoolField(TEXT("closing"),bModalClosing);J->SetNumberField(TEXT("modal_alpha"),ModalAlpha);
    J->SetNumberField(TEXT("shot_expansion"),ShotExpansion(FPlatformTime::Seconds()));
    J->SetNumberField(TEXT("cooldown_progress"),LastCooldownProgress);J->SetBoolField(TEXT("cooling"),bCooling);
    J->SetBoolField(TEXT("aiming"),bHeldAim);J->SetBoolField(TEXT("thrusting"),bHeldJet);
    J->SetBoolField(TEXT("ambient_running"),bAmbientRunning);J->SetNumberField(TEXT("ambient_transitions"),AmbientTransitions);
    J->SetNumberField(TEXT("ambient_paused_phase"),AmbientPhase);J->SetNumberField(TEXT("ambient_started"),AmbientStarted);
    J->SetStringField(TEXT("ambient_clock"),TEXT("SlateAppTime"));
    J->SetNumberField(TEXT("ambient_sample_phase"),FMath::Fmod(AmbientPhase+(bAmbientRunning?FMath::Max(0.,FApp::GetCurrentTime()-GStartTime-AmbientStarted):0.),FMath::Max(1.f,Number(TEXT("MotionAmbientSeconds"),4))));
    J->SetNumberField(TEXT("loaded_icons"),IconCatalog?IconCatalog->NumLoaded():0);
    TArray<TSharedPtr<FJsonValue>> Skills;
    for(int32 I=0;I<4;++I)
    {
        auto S=MakeShared<FJsonObject>();S->SetNumberField(TEXT("slot"),I);S->SetNumberField(TEXT("casts"),CastPlays[I]);S->SetNumberField(TEXT("ready"),ReadyPlays[I]);
        S->SetBoolField(TEXT("cast_material"),CastClips[I].Material.IsValid());S->SetBoolField(TEXT("ready_material"),ReadyClips[I].Material.IsValid());
        S->SetNumberField(TEXT("held_alpha"),HeldAlpha[I]);Skills.Add(MakeShared<FJsonValueObject>(S));
        S->SetStringField(TEXT("icon"),SkillIcons.IsValidIndex(I)?GetPathNameSafe(SkillIcons[I]->GetBrush().GetResourceObject()):TEXT("None"));
    }
    J->SetArrayField(TEXT("skills"),Skills);
    TArray<TSharedPtr<FJsonValue>> Textures;uint64 Bytes=0;
    for(const auto& O:Art)if(const auto* T=Cast<UTexture2D>(O.Get());T&&T->GetPathName().Contains(TEXT("/UI/Motion/")))
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("asset"),T->GetPathName());
        const uint64 Resident=T->CalcTextureMemorySizeEnum(TMC_ResidentMips);Bytes+=Resident;
        Item->SetNumberField(TEXT("resident_bytes"),Resident);Textures.Add(MakeShared<FJsonValueObject>(Item));
    }
    J->SetArrayField(TEXT("textures"),Textures);J->SetNumberField(TEXT("resident_mib"),double(Bytes)/(1024*1024));
    FString Result;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Result));return Result;
}
