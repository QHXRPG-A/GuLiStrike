#include "Gameplay/GroundMech/UI/GuLiGroundHUDData.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGroundMechRocketComponent.h"
#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "GuLiNetBuffs.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
bool IsMapKind(EGuLiHybridObjectKind K)
{
    return K==EGuLiHybridObjectKind::Soldier || K==EGuLiHybridObjectKind::GroundPlayer || K==EGuLiHybridObjectKind::Ship
        || K==EGuLiHybridObjectKind::Wingman || K==EGuLiHybridObjectKind::Building || K==EGuLiHybridObjectKind::Territory
        || K==EGuLiHybridObjectKind::Miner || K==EGuLiHybridObjectKind::Engineer;
}
}
void UGuLiGroundHUDData::Deinitialize(){Unbind();OnUrgent.Clear();OnHit.Clear();OnMotion.Clear();Super::Deinitialize();}
void UGuLiGroundHUDData::Unbind()
{
    if(auto* P=Pawn.Get())
    {
        P->OnBlinkStateChanged.RemoveDynamic(this,&ThisClass::BlinkChanged);
        P->OnLocalPresentation.Remove(PresentationHandle);
        P->GetCombatHealthComponent()->OnHealthChanged.RemoveDynamic(this,&ThisClass::HealthChanged);
        P->GetRocketJump()->OnPresentationChanged.RemoveDynamic(this,&ThisClass::RocketChanged);
    }
    if(auto* R=Rogue.Get())R->OnGroundBuildChanged.RemoveDynamic(this,&ThisClass::BuildChanged);
    if(auto* H=Hybrid.Get()){H->OnObjectChanged.Remove(ObjectHandle);H->OnDamageFeedback.Remove(DamageHandle);}
    FCoreDelegates::OnEndFrame.Remove(FrameHandle);
    Controller.Reset();Pawn.Reset();Hybrid.Reset();Rogue.Reset();PlayerKey={};PlayerGuid={};Epoch=0;
    HostBuffs.Reset();BuildingMapInfo.Reset();DeferredObjects.Reset();NextPositionSample=0;View={};
    Dirty=EGuLiGroundHUDGroup::All;Urgent=EGuLiGroundHUDGroup::None;bHitPending=bKillPending=false;
    PendingMotion={};bPrimeMotion=true;bJetReadyArmed=false;++MotionGeneration;View.MotionGeneration=MotionGeneration;
}
void UGuLiGroundHUDData::Bind(AGuLiCommanderPlayerController* PC)
{
    auto* P=PC?Cast<AGuLiGroundMechCharacter>(PC->GetPawn()):nullptr;
    auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* W=PC?PC->GetWorld():nullptr;
    const uint32 NextEpoch=W?W->GetSubsystem<UGuLiDamageLedgerSubsystem>()->GetMatchEpoch():0;
    const auto* Identity=P?UGuLiHybridActorComponent::Find(P):nullptr;
    const auto Key=Identity?Identity->GetObjectKey():FGuLiHybridObjectKey{};
    if(Controller.Get()==PC && Pawn.Get()==P && Epoch==NextEpoch && PlayerKey==Key && PlayerGuid==(PS?PS->GetPlayerGuid():FGuid()))return;
    Unbind();if(!P||!PS||!PC->IsLocalController())return;
    Controller=PC;Pawn=P;PlayerGuid=PS->GetPlayerGuid();PlayerKey=Key;Epoch=NextEpoch;
    if(FeedbackWorld.Get()!=W||FeedbackEpoch!=Epoch||FeedbackPlayer!=PlayerGuid)
    {HitEvents.Reset();HitOrder.Reset();HitCursor=0;FeedbackWorld=W;FeedbackEpoch=Epoch;FeedbackPlayer=PlayerGuid;}
    Hybrid=W->GetSubsystem<UGuLiHybridWorldSubsystem>();Rogue=W->GetSubsystem<UGuLiRogueCardSubsystem>();
    // Copy the already-resolved gameplay bounds once for this local Ground
    // binding. Object events and 5 Hz motion samples never load art/catalogs.
    if(const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog())
        for(const auto& D:Catalog->Definitions)
            BuildingMapInfo.Add(FName(*FString::Printf(TEXT("building/%d"),D.DefinitionId)),{D.DisplayName,D.CollisionExtent});
    P->OnBlinkStateChanged.AddDynamic(this,&ThisClass::BlinkChanged);
    PresentationHandle=P->OnLocalPresentation.AddUObject(this,&ThisClass::PresentationChanged);
    P->GetCombatHealthComponent()->OnHealthChanged.AddDynamic(this,&ThisClass::HealthChanged);
    P->GetRocketJump()->OnPresentationChanged.AddDynamic(this,&ThisClass::RocketChanged);
    if(Rogue.IsValid())Rogue->OnGroundBuildChanged.AddDynamic(this,&ThisClass::BuildChanged);
    if(Hybrid.IsValid())
    {
        ObjectHandle=Hybrid->OnObjectChanged.AddUObject(this,&ThisClass::ObjectChanged);
        DamageHandle=Hybrid->OnDamageFeedback.AddUObject(this,&ThisClass::DamageFeedback);
        TArray<FGuLiHybridObjectState> Objects;Hybrid->GetObjects(Objects);
        for(const auto& O:Objects)if(IsMapKind(O.Key.Kind)||O.Key.Kind==EGuLiHybridObjectKind::Field)ReadObject(O.Key);
    }
    FrameHandle=FCoreDelegates::OnEndFrame.AddUObject(this,&ThisClass::EndFrame);
    Invalidate(EGuLiGroundHUDGroup::All,true);
}
void UGuLiGroundHUDData::SetActive(bool Active)
{
    if(bActive==Active)return;
    bActive=Active;
    PendingMotion={};bPrimeMotion=true;bJetReadyArmed=false;
    if(Active)
    {
        for(const auto& K:DeferredObjects)ReadObject(K);
        DeferredObjects.Reset();Invalidate(EGuLiGroundHUDGroup::All,true);
    }
    else bHitPending=bKillPending=false;
}
void UGuLiGroundHUDData::Invalidate(EGuLiGroundHUDGroup G,bool Now){Dirty|=G;if(Now)Urgent|=G;}
void UGuLiGroundHUDData::HealthChanged(float,float)
{
    if(Pawn.IsValid()&&Pawn->GetCombatHealthComponent()->GetHealthState().bDead)
    {PendingMotion={};bJetReadyArmed=false;}
    Invalidate(EGuLiGroundHUDGroup::Vitals|EGuLiGroundHUDGroup::Skills,true);
}
void UGuLiGroundHUDData::BlinkChanged(uint8,bool){Invalidate(EGuLiGroundHUDGroup::Skills,true);}
void UGuLiGroundHUDData::BuildChanged(FGuid P,int64){if(P==PlayerGuid)Invalidate(EGuLiGroundHUDGroup::Rogue|EGuLiGroundHUDGroup::Skills,true);}
void UGuLiGroundHUDData::RocketChanged(float Fuel,bool Thrust)
{
    const bool Empty=Fuel<=UE_SMALL_NUMBER;
    Invalidate(EGuLiGroundHUDGroup::Skills,Thrust!=bLastThrust||Empty!=bLastEmpty);
    if(bActive&&!bPrimeMotion&&Pawn.IsValid()&&!Pawn->GetCombatHealthComponent()->GetHealthState().bDead)
    {
        if(Thrust&&!bLastThrust)PendingMotion.Cast|=1<<2;
        if(!Thrust&&bLastThrust)PendingMotion.Ended|=1<<2;
        if(Empty&&!bLastEmpty)bJetReadyArmed=true;
        if(bJetReadyArmed&&!Empty&&Pawn.IsValid()&&Pawn->GetRocketJump()->CanActivateRocket())
        {bJetReadyArmed=false;PendingMotion.Ready|=1<<2;Invalidate(EGuLiGroundHUDGroup::Skills,true);}
    }
    bLastThrust=Thrust;bLastEmpty=Empty;
}
void UGuLiGroundHUDData::PrimeMotion()
{
    if(const auto* P=Pawn.Get())
    {bLastThrust=P->GetRocketJump()->IsThrusting();bLastEmpty=P->GetRocketJump()->GetFuelRatio()<=UE_SMALL_NUMBER;}
    bJetReadyArmed=false;bPrimeMotion=false;PendingMotion={};
}
void UGuLiGroundHUDData::PresentationChanged(const FGuLiGroundPresentationEvent& E)
{
    if(E.Action==EGuLiGroundPresentationAction::Reset)
    {
        // Restore can mutate several components in one transaction; establish the
        // new baseline at end of frame, after all fields have been installed.
        ++MotionGeneration;View.MotionGeneration=MotionGeneration;bPrimeMotion=true;
        PendingMotion={};bHitPending=bKillPending=false;Invalidate(EGuLiGroundHUDGroup::All,true);return;
    }
    if(E.Action!=EGuLiGroundPresentationAction::Fire)Invalidate(EGuLiGroundHUDGroup::Skills,true);
    if(!bActive||bPrimeMotion||!Pawn.IsValid()||Pawn->GetCombatHealthComponent()->GetHealthState().bDead)return;
    const uint8 Slot=E.Action==EGuLiGroundPresentationAction::Fire?0:E.Action==EGuLiGroundPresentationAction::Aim?1:3;
    if(E.Action==EGuLiGroundPresentationAction::Fire)
    {
        PendingMotion.ShotAge=PendingMotion.Shots?FMath::Min(PendingMotion.ShotAge,E.Age):E.Age;
        PendingMotion.Shots=FMath::Min<int32>(3,PendingMotion.Shots+1);
    }
    if(E.Phase==EGuLiGroundPresentationPhase::Started)PendingMotion.Cast|=1<<Slot;
    else if(E.Phase==EGuLiGroundPresentationPhase::Ended)PendingMotion.Ended|=1<<Slot;
    else PendingMotion.Ready|=1<<Slot;
}
double UGuLiGroundHUDData::GetLogicalSeconds() const
{return Hybrid.IsValid()?double(Hybrid->GetObjectLogicalTick(PlayerKey))/GuLiNet::TicksPerSecond:0.;}
void UGuLiGroundHUDData::ObjectChanged(const FGuLiHybridObjectKey& K)
{
    if(!bActive){if(IsMapKind(K.Kind)||K.Kind==EGuLiHybridObjectKind::Field)DeferredObjects.Add(K);return;}
    if(K==PlayerKey && Hybrid.IsValid())
    {
        FGuLiHybridObjectState O;
        if(Hybrid->GetObjectView(K,O) && (O.Health.Health!=View.Health.Health||O.Health.MaxHealth!=View.Health.MaxHealth
            ||O.Health.Shield!=View.Health.Shield||O.Health.MaxShield!=View.Health.MaxShield||O.Health.bDead!=View.Health.bDead))
            Invalidate(EGuLiGroundHUDGroup::Vitals|EGuLiGroundHUDGroup::Skills,true);
        if(const auto* P=Pawn.Get();P && (P->GetWeapon()->GetWeaponLevel()!=View.WeaponLevel||P->GetWeapon()->GetFireRate()!=View.FireRate))
            Invalidate(EGuLiGroundHUDGroup::Skills,true);
    }
    if(IsMapKind(K.Kind)||K.Kind==EGuLiHybridObjectKind::Field)ReadObject(K);
}
void UGuLiGroundHUDData::ReadObject(const FGuLiHybridObjectKey& K)
{
    if(!Hybrid.IsValid())return;
    FGuLiHybridObjectState O;const bool Alive=Hybrid->GetObjectView(K,O)&&O.LifeState==1&&!O.Health.bDead&&!O.bPhased&&!O.bDormant;
    if(IsMapKind(K.Kind))
    {
        auto* Old=View.Markers.Find(K);
        if(!Alive){if(View.Markers.Remove(K))Invalidate(EGuLiGroundHUDGroup::Map,true);return;}
        if(!Old || Old->Team!=O.Affiliation || Old->Definition!=O.Definition)
        {
            auto& M=View.Markers.FindOrAdd(K);M.Key=K;M.Team=O.Affiliation;M.Location=O.Location;M.Yaw=O.FacingYawDegrees;M.Definition=O.Definition;
            M.DisplayName=FText::GetEmpty();M.BuildingExtent=FVector::ZeroVector;
            if(const auto* Info=BuildingMapInfo.Find(O.Definition)){M.DisplayName=Info->Name;M.BuildingExtent=Info->Extent;}
            Invalidate(EGuLiGroundHUDGroup::Map,true);
        }
        return;
    }
    FGuLiHybridCapabilityState C;GuLiNet::Buffs::State S;
    const bool OnHost=Alive&&Hybrid->GetObjectCapability(K,TEXT("buff_status"),C)&&C.Version==GuLiNet::Buffs::Version
        &&GuLiNet::Buffs::Decode({C.Data.GetData(),SIZE_T(C.Data.Num())},S)&&GuLiNetBridge::ToUE(S.Host)==PlayerKey&&S.Active;
    if(!OnHost){if(HostBuffs.Remove(K))Invalidate(EGuLiGroundHUDGroup::Buffs,true);return;}
    FBuff Next;Next.Expires=S.Expires;Next.bTimed=S.Expires!=MAX_uint64;for(const auto& Slot:S.Slots)Next.Ids.Add(int32(Slot.Frozen.Id));
    const auto* Old=HostBuffs.Find(K);
    if(!Old||Old->Ids!=Next.Ids||Old->Expires!=Next.Expires){HostBuffs.Add(K,MoveTemp(Next));Invalidate(EGuLiGroundHUDGroup::Buffs,true);}
}
void UGuLiGroundHUDData::DamageFeedback(const FGuLiDamageFeedback& F)
{
    if(F.Epoch!=Epoch||F.Player!=PlayerGuid||!F.Event.IsValid()||HitEvents.Contains(F.Event))return;
    if(HitOrder.Num()>=2048){HitEvents.Remove(HitOrder[HitCursor]);HitOrder[HitCursor]=F.Event;HitCursor=(HitCursor+1)%2048;}else HitOrder.Add(F.Event);
    HitEvents.Add(F.Event);if(!bActive)return;bHitPending=true;bKillPending|=F.bKilled;
}
void UGuLiGroundHUDData::EndFrame()
{
    if(bPrimeMotion)PrimeMotion();
    if(!bActive)return;
    const auto G=Urgent;Urgent=EGuLiGroundHUDGroup::None;
    if(G!=EGuLiGroundHUDGroup::None)OnUrgent.Broadcast(G);
    if(bHitPending){const bool Kill=bKillPending;bHitPending=bKillPending=false;OnHit.Broadcast(Kill);}
    const auto Motion=PendingMotion;PendingMotion={};if(Motion.HasEvents())OnMotion.Broadcast(Motion);
}
const FGuLiGroundHUDSnapshot& UGuLiGroundHUDData::Read(EGuLiGroundHUDGroup Groups)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundHUD_Read);
    auto* P=Pawn.Get();if(!P)return View;
    const auto Requested=Groups&Dirty;Dirty&=~Requested;Urgent&=~Requested;
    if(EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Vitals))View.Health=P->GetCombatHealthComponent()->GetHealthState();
    if(EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Skills))
    {
        const auto* W=P->GetWeapon();const auto* R=P->GetRocketJump();
        View.SkillIconIds[0]=W->GetSkillIconId();View.SkillIconIds[2]=R->GetConfiguration().IconId;
        View.SkillIconIds[3]=P->GetMobilityConfiguration().Blink.IconId;
        View.Fuel=R->GetFuelRatio();View.bThrusting=R->IsThrusting();View.bJetReady=R->CanActivateRocket();
        View.bAiming=P->IsShoulderAimHeld();
        View.bAimReady=!P->GetCombatHealthComponent()->GetHealthState().bDead && P->GetBlinkPhase()==0 && !P->IsCarrierDormant();
        View.FireRate=W->GetFireRate();View.Damage=W->GetShotDamage();View.WeaponLevel=W->GetWeaponLevel();View.bWeaponReady=W->CanUseLocalWeapon()&&W->bWeaponEnabled;
        View.Speed=P->GetCharacterMovement()->GetMaxSpeed()/100.f;View.BlinkRemaining=P->GetBlinkCooldownRemaining();View.BlinkDuration=P->GetBlinkCooldownDuration();View.bBlinkReady=P->CanActivateBlink();
        View.BlinkEndsAt=GetLogicalSeconds()+View.BlinkRemaining;
    }
    if(EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Buffs)&&Hybrid.IsValid())
    {
        View.Buffs.Reset();
        for(const auto& Pair:HostBuffs)for(int32 Id:Pair.Value.Ids)
        {
            const uint64 Now=Hybrid->GetObjectLogicalTick(Pair.Key);if(Pair.Value.bTimed&&Now>=Pair.Value.Expires)continue;
            FGuLiGroundHUDBuff B;B.Id=Id;B.bTimed=Pair.Value.bTimed;
            B.Remaining=B.bTimed?double(Pair.Value.Expires-Now)/GuLiNet::TicksPerSecond:0;
            B.Name=Id==1?GuLiGameText::Get(TEXT("UI.GroundHUD.Burning")):Id==2?GuLiGameText::Get(TEXT("UI.GroundHUD.BurningBurst")):FText::FromString(GuLiGameText::Format(TEXT("UI.GroundHUD.BuffOther"),{FString::FromInt(Id)}));View.Buffs.Add(B);
        }
        View.Buffs.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
    }
    if(EnumHasAnyFlags(Requested,EGuLiGroundHUDGroup::Rogue)&&Rogue.IsValid())
    {
        View.Cards.Reset();TMap<FString,int32> Counts;uint64 Revision=0;
        if(GuLiGroundRogue::ReadBuild(*P->GetWorld(),PlayerGuid,Counts,Revision))for(const auto& Pair:Counts)
        {
            FGuLiGroundHUDCard C;C.Id=Pair.Key;C.Count=Pair.Value;
            if(const auto* Definition=Rogue->FindCard(C.Id))C.IconId=Definition->IconId;
            C.Title=Rogue->GetCardText(C.Id,0,PlayerGuid,false);C.Description=Rogue->GetCardText(C.Id,1,PlayerGuid,false);View.Cards.Add(C);
        }
        View.Cards.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
    }
    if(EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Map)&&Hybrid.IsValid())
    {
        // Topology events must not increase continuous position sampling beyond 5 Hz.
        const double Now=FPlatformTime::Seconds();
        if(Now>=NextPositionSample)
        {
        NextPositionSample=Now+.2;
        // Motion facts do not emit topology events. Sample only the cached membership.
        for(auto& Pair:View.Markers)
        {FGuLiHybridObjectState O;if(Hybrid->GetObjectView(Pair.Key,O)){Pair.Value.Location=O.Location;Pair.Value.Yaw=O.FacingYawDegrees;}}
        View.Position=P->GetActorLocation();View.PlayerYaw=P->GetActorRotation().Yaw;
        }
        if(const auto* I=UGuLiHybridActorComponent::Find(P))View.Team=I->GetState().Affiliation;
    }
    if(Groups!=EGuLiGroundHUDGroup::None)++View.Revision;return View;
}
