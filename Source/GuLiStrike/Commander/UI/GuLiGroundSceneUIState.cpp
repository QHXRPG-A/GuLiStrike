#include "Commander/UI/GuLiGroundSceneUIState.h"
#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/UI/GuLiGroundOutlinePool.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool GuLiGroundSceneUI::IsView(const APlayerController* PC)
{
    const auto* Player=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    return PC && PC->IsLocalController() && Player && Player->GetActiveControlRole()==EGuLiCommanderRole::Ground
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
        && S.Health.IsWellFormed()&&S.Health.Health>0;
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
    USceneCaptureComponent2D& Capture,FGuLiWorldHealthBars& Models,TArray<FGuLiSceneUIHealthBar>& Bars)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundSceneUI_Update);
    Bars.Reset();ActiveEvents=VisibleBars=FarEvents=0;
    if(!Hybrid.IsValid()||!Controller.IsValid()||!OutlinePool)return;
    const double Now=GetWorld()->GetTimeSeconds();
    const FVector Origin=GuLiHealthBars::ViewOrigin(*Controller.Get());
    const auto* OwnerBinding=UGuLiHybridActorComponent::Find(Controller->GetViewTarget());
    const auto* Timing=GetDefault<UGuLiUnitFeedbackSettings>();
    FConvexVolume Frustum;GetViewFrustumBounds(Frustum,Projection.ComputeViewProjectionMatrix(),true);
    OutlinePool->Begin(Capture);
    const auto* Roster = Presentation ? Presentation->GetSoldierRoster() : nullptr;
    for(auto& Pair:Entries)
    {
        const auto& Key=Pair.Key;auto& E=Pair.Value;
        if(OwnerBinding && Key==OwnerBinding->GetObjectKey())continue;
        const bool Enemy=GuLiLocalTeamColors::IsEnemy(E.Vitals.Team,ViewTeam);
        const float Event=Timing->HealthBarOpacity(Now,E.Vitals.LastChange);
        const float Damage=GuLiHealthBars::DamageOpacity(Now,E.Vitals.LastDamage);
        const float BarOpacity=GuLiHealthBars::Visibility(Origin,E.Location,Damage);
        if(!Enemy && BarOpacity<=0)continue;
        if(Event>0)++ActiveEvents;
        UInstancedStaticMeshComponent* Instances=nullptr;int32 InstanceIndex=INDEX_NONE;
        AActor* Actor=nullptr;FBox Bounds(ForceInit);
        double ModelWidth = 0;
        if(Key.Kind==EGuLiHybridObjectKind::Soldier)
        {
            if(!Presentation||!Presentation->GetSceneUIInstance(FGuLiSoldierId(E.Ordinal),Instances,InstanceIndex,Bounds))continue;
        }
        else
        {
            Actor=Hybrid->FindBoundActor(Key);
            float Size=0;
            if(!Actor||Actor->IsHidden()||!UGuLiUnitFeedbackSubsystem::GetActorVisualBounds(Actor,Bounds,Size,BarOpacity>0 ? &ModelWidth : nullptr))continue;
        }
        if(!Bounds.IsValid)continue;
        const double Distance=FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(Projection.ViewOrigin));
        if(Event>0&&Distance>=GetDefault<UGuLiGroundSceneUISettings>()->FadeEndCm)++FarEvents;
        if(!Frustum.IntersectBox(Bounds.GetCenter(),Bounds.GetExtent()))continue;
        const float Outline=Enemy ? FMath::Max(DistanceOpacity(Distance),Event) : 0.f;
        if(Outline>0)
        {
            if(Instances)OutlinePool->SubmitInstance(Key,*Instances,InstanceIndex,Outline);
            else OutlinePool->SubmitActor(Key,*Actor,Outline);
        }
        if(BarOpacity>0)
        {
            uint32 Displacement = 0;
            if (Instances)
            {
                const auto* State = Roster ? Roster->FindSoldierState(FGuLiSoldierId(E.Ordinal)) : nullptr;
                if (!State) continue;
                const FBox& Local = Models.SoldierBounds(*Presentation,State->UnitTypeId);
                if (!Local.IsValid) continue;
                ModelWidth = FMath::Max(Local.GetSize().X,Local.GetSize().Y);
                Displacement = State->DisplacementFrameFloor;
            }
            auto& Bar=Bars.AddDefaulted_GetRef();Bar.Key={this,E.Element,Key.LifeGeneration};Bar.Target=Key;
            Bar.Set(Bounds, ModelWidth);
            Bar.DisplacementRevision = Displacement;
            const auto& Health=E.Vitals.Health;
            GuLiHealthBars::SetVitals(Bar,Health.Health,Health.MaxHealth,Health.Shield,Health.MaxShield);
            Bar.Opacity=BarOpacity; Bar.bRecentDamage=Damage>0; ++VisibleBars;
        }
    }
    OutlinePool->End();
}
FString UGuLiGroundSceneUIState::GetStatsJson() const
{
    return FString::Printf(TEXT("{\"tracked\":%d,\"notifications\":%llu,\"changes\":%llu,\"active_events\":%d,\"far_events\":%d,\"bars\":%d,\"unit_rings\":0,\"pool\":%s}"),
        Entries.Num(),Notifications,Changes,ActiveEvents,FarEvents,VisibleBars,OutlinePool?*OutlinePool->GetStatsJson():TEXT("{}"));
}
