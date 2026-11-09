#include "Commander/UI/GuLiGroundSceneUIState.h"
#include "Commander/UI/GuLiGroundOutlinePool.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool GuLiGroundSceneUI::IsView(const APlayerController* PC)
{
    const auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    return PC && PC->IsLocalController() && Player && Player->GetBattleRole()==EGuLiCommanderRole::Ground
        && Cast<AGuLiGroundMechCharacter>(PC->GetViewTarget());
}
bool GuLiGroundSceneUI::IsDisplayKind(EGuLiHybridObjectKind Kind)
{
    switch(Kind)
    {
    case EGuLiHybridObjectKind::Soldier:case EGuLiHybridObjectKind::Wingman:case EGuLiHybridObjectKind::Ship:
    case EGuLiHybridObjectKind::GroundPlayer:case EGuLiHybridObjectKind::Engineer:case EGuLiHybridObjectKind::Miner:
    case EGuLiHybridObjectKind::Building:case EGuLiHybridObjectKind::Territory:return true;
    default:return false;
    }
}
float GuLiGroundSceneUI::DistanceOpacity(double Distance,float Start,float End)
{
    if(!FMath::IsFinite(Distance)||!FMath::IsFinite(Start)||!FMath::IsFinite(End)||End<=Start)return 0;
    return float(FMath::Clamp((End-FMath::Max(0.,Distance))/double(End-Start),0.,1.));
}
float GuLiGroundSceneUI::EventOpacity(double Now,double LastChange,float Hold,float Fade)
{
    if(LastChange<0||Now<LastChange||!FMath::IsFinite(Now)||!FMath::IsFinite(LastChange))return 0;
    return float(FMath::Clamp(1.-FMath::Max(0.,Now-LastChange-FMath::Max(0.f,Hold))/FMath::Max(.0001f,Fade),0.,1.));
}
bool GuLiGroundSceneUI::FVitalHistory::Observe(const FGuLiCombatHealthState& Next,EGuLiTeam NextTeam,bool Eligible,double Now)
{
    const bool Changed=Eligible && bInitialized && Team==NextTeam
        && (Health.Health!=Next.Health||Health.MaxHealth!=Next.MaxHealth||Health.Shield!=Next.Shield||Health.MaxShield!=Next.MaxShield);
    if(!Eligible||Team!=NextTeam)LastChange=-1;
    Health=Next;Team=NextTeam;bInitialized=Eligible;
    if(Changed)LastChange=Now;
    return Changed;
}
UWorld* UGuLiGroundSceneUIState::GetWorld() const {return Controller.IsValid()?Controller->GetWorld():nullptr;}
float UGuLiGroundSceneUIState::DistanceOpacity(double Distance)
{
    const auto* S=GetDefault<UGuLiGroundSceneUISettings>();
    return GuLiGroundSceneUI::DistanceOpacity(Distance,S->FadeStartCm,S->FadeEndCm);
}
void UGuLiGroundSceneUIState::Bind(APlayerController* PC)
{
    auto* World=PC?PC->GetWorld():nullptr;
    auto* Next=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    const auto Team=GuLiLocalTeamColors::GetViewTeam(PC);
    if(Controller==PC&&Hybrid==Next&&ViewTeam==Team)return;
    Unbind();Controller=PC;Hybrid=Next;ViewTeam=Team;
    if(!Next)return;
    ObjectHandle=Next->OnObjectChanged.AddUObject(this,&ThisClass::ObjectChanged);
    OutlinePool=NewObject<UGuLiGroundOutlinePool>(this);
    OutlinePool->Initialize(PC);
    Seed();
}
void UGuLiGroundSceneUIState::Unbind()
{
    if(Hybrid.IsValid())Hybrid->OnObjectChanged.Remove(ObjectHandle);
    ObjectHandle.Reset();Entries.Reset();LatestLives.Reset();
    if(OutlinePool)OutlinePool->Shutdown();OutlinePool=nullptr;
    Hybrid.Reset();Controller.Reset();ViewTeam=EGuLiTeam::Unassigned;
    ActiveEvents=VisibleBars=FarEvents=0;
}
void UGuLiGroundSceneUIState::Seed()
{
    if(!Hybrid.IsValid())return;
    // Exactly once per local-view binding, never in the frame update.
    TArray<FGuLiHybridObjectState> Objects;Hybrid->GetObjects(Objects);
    for(const auto& Object:Objects)if(GuLiGroundSceneUI::IsDisplayKind(Object.Key.Kind))Read(Object,true);
}
void UGuLiGroundSceneUIState::ObjectChanged(const FGuLiHybridObjectKey& Key)
{
    if(!Hybrid.IsValid()||!GuLiGroundSceneUI::IsDisplayKind(Key.Kind))return;
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundSceneUI_VitalChange);
    ++Notifications;FGuLiHybridObjectState State;
    if(Hybrid->GetObjectView(Key,State))Read(State,false);
    else
    {
        Entries.Remove(Key);auto Identity=Key;Identity.LifeGeneration=1;
        if(const auto* Latest=LatestLives.Find(Identity);Latest&&*Latest==Key)LatestLives.Remove(Identity);
    }
}
void UGuLiGroundSceneUIState::Read(const FGuLiHybridObjectState& S,bool SeedOnly)
{
    if(!S.Key.IsValid())return;
    auto Identity=S.Key;Identity.LifeGeneration=1;
    if(const auto* Previous=LatestLives.Find(Identity))
    {
        if(Previous->LifeGeneration>S.Key.LifeGeneration)return;
        if(*Previous!=S.Key)Entries.Remove(*Previous);
    }
    LatestLives.Add(Identity,S.Key);
    const bool Eligible=S.LifeState==1&&!S.bPhased&&!S.bDormant&&!S.Health.bDead
        && S.Health.IsWellFormed()&&S.Health.Health>0&&GuLiLocalTeamColors::IsEnemy(S.Affiliation,ViewTeam);
    auto* Existing=Entries.Find(S.Key);
    if(!Eligible)
    {
        Entries.Remove(S.Key);
        if(const auto* Latest=LatestLives.Find(Identity);Latest&&*Latest==S.Key)LatestLives.Remove(Identity);
        return;
    }
    auto& E=Existing?*Existing:Entries.Add(S.Key);
    if(!E.Element)E.Element=NextElement++;
    E.Ordinal=S.Ordinal;E.Location=S.Location;
    if(SeedOnly)E.Vitals.bInitialized=false;
    if(E.Vitals.Observe(S.Health,S.Affiliation,true,GetWorld()->GetTimeSeconds()))++Changes;
}
void UGuLiGroundSceneUIState::Update(const FSceneViewProjectionData& Projection,AGuLiCommanderPresentationActor* Presentation,
    USceneCaptureComponent2D& Capture,TArray<FGuLiSceneUIHealthBar>& Bars)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundSceneUI_Update);
    Bars.Reset();ActiveEvents=VisibleBars=FarEvents=0;
    if(!Hybrid.IsValid()||!Controller.IsValid()||!OutlinePool)return;
    const double Now=GetWorld()->GetTimeSeconds();
    const auto* Timing=GetDefault<UGuLiUnitFeedbackSettings>();
    FConvexVolume Frustum;GetViewFrustumBounds(Frustum,Projection.ComputeViewProjectionMatrix(),true);
    OutlinePool->Begin(Capture);
    for(auto& Pair:Entries)
    {
        const auto& Key=Pair.Key;auto& E=Pair.Value;
        const float Event=GuLiGroundSceneUI::EventOpacity(Now,E.Vitals.LastChange,Timing->HealthBarHoldSeconds,Timing->HealthBarFadeSeconds);
        if(Event>0)++ActiveEvents;
        UInstancedStaticMeshComponent* Instances=nullptr;int32 InstanceIndex=INDEX_NONE;
        AActor* Actor=nullptr;FBox Bounds(ForceInit);
        if(Key.Kind==EGuLiHybridObjectKind::Soldier)
        {
            if(!Presentation||!Presentation->GetSceneUIInstance(FGuLiSoldierId(E.Ordinal),Instances,InstanceIndex,Bounds))continue;
        }
        else
        {
            Actor=Hybrid->FindBoundActor(Key);
            float Size=0;
            if(!Actor||Actor->IsHidden()||!UGuLiUnitFeedbackSubsystem::GetActorVisualBounds(Actor,Bounds,Size))continue;
        }
        if(!Bounds.IsValid)continue;
        const double Distance=FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(Projection.ViewOrigin));
        if(Event>0&&Distance>=GetDefault<UGuLiGroundSceneUISettings>()->FadeEndCm)++FarEvents;
        if(!Frustum.IntersectBox(Bounds.GetCenter(),Bounds.GetExtent()))continue;
        const float Outline=FMath::Max(DistanceOpacity(Distance),Event);
        if(Outline>0)
        {
            if(Instances)OutlinePool->SubmitInstance(Key,*Instances,InstanceIndex,Outline);
            else OutlinePool->SubmitActor(Key,*Actor,Outline);
        }
        if(Event>0)
        {
            auto& Bar=Bars.AddDefaulted_GetRef();Bar.Key={this,E.Element,Key.LifeGeneration};Bar.Target=Key;Bar.Team=E.Vitals.Team;
            Bar.Center=Bounds.GetCenter();Bar.Center.Z=Bounds.Max.Z+4;
            Bar.Fraction=FMath::Clamp(E.Vitals.Health.Health/E.Vitals.Health.MaxHealth,0.f,1.f);
            Bar.bHasShield=E.Vitals.Health.MaxShield>0;
            Bar.ShieldFraction=Bar.bHasShield?FMath::Clamp(E.Vitals.Health.Shield/E.Vitals.Health.MaxShield,0.f,1.f):0;
            Bar.Opacity=Event;Bar.bEventOverride=true;++VisibleBars;
        }
    }
    OutlinePool->End();
}
FString UGuLiGroundSceneUIState::GetStatsJson() const
{
    return FString::Printf(TEXT("{\"tracked\":%d,\"notifications\":%llu,\"changes\":%llu,\"active_events\":%d,\"far_events\":%d,\"bars\":%d,\"unit_rings\":0,\"pool\":%s}"),
        Entries.Num(),Notifications,Changes,ActiveEvents,FarEvents,VisibleBars,OutlinePool?*OutlinePool->GetStatsJson():TEXT("{}"));
}
