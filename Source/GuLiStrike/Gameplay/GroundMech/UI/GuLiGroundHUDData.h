#pragma once
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiGroundHUDData.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiGroundMechCharacter;
class UGuLiRogueCardSubsystem;
struct FGuLiGroundPresentationEvent;
enum class EGuLiGroundHUDGroup:uint8 { None=0,Vitals=1,Skills=2,Buffs=4,Rogue=8,Map=16,All=31 };
ENUM_CLASS_FLAGS(EGuLiGroundHUDGroup);
struct FGuLiGroundHUDBuff { int32 Id=0;FText Name;double Remaining=0;bool bTimed=false; };
struct FGuLiGroundHUDCard { FString Id;FText Title,Description;int32 Count=0,IconId=0; };
struct FGuLiGroundHUDMarker
{
    FGuLiHybridObjectKey Key;FVector Location=FVector::ZeroVector;
    EGuLiTeam Team=EGuLiTeam::Unassigned;float Yaw=0;
    FName Definition;FText DisplayName;
    FVector BuildingExtent=FVector::ZeroVector;
};
struct FGuLiGroundHUDSnapshot
{
    FGuLiCombatHealthState Health;
    float Fuel=0,FireRate=0,Damage=0,Speed=0,BlinkRemaining=0,BlinkDuration=0;
    int32 WeaponLevel=0;bool bThrusting=false,bJetReady=false,bBlinkReady=false,bWeaponReady=false,bAimReady=false,bAiming=false;
    int32 SkillIconIds[4]{};
    double BlinkEndsAt=0;uint64 MotionGeneration=0;
    EGuLiTeam Team=EGuLiTeam::Unassigned;
    FVector Position=FVector::ZeroVector;
    float PlayerYaw=0;
    TArray<FGuLiGroundHUDBuff> Buffs;TArray<FGuLiGroundHUDCard> Cards;
    TMap<FGuLiHybridObjectKey,FGuLiGroundHUDMarker> Markers;
    uint64 Revision=0;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiGroundHUDUrgent,EGuLiGroundHUDGroup);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiGroundHUDHit,bool);
/** Fixed-size frame accumulator: slot bits Fire/Aim/Jet/Blink, no animation queue. */
struct FGuLiGroundHUDMotion
{
    uint8 Cast=0,Ended=0,Ready=0,Shots=0;
    float ShotAge=0;
    bool HasEvents() const {return Cast||Ended||Ready||Shots;}
};
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiGroundHUDMotionChanged,const FGuLiGroundHUDMotion&);

/** One read model per local player. No network requests, world scans or asset loads per refresh. */
UCLASS()
class GULISTRIKE_API UGuLiGroundHUDData final:public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    void Bind(AGuLiCommanderPlayerController* InController);
    void Unbind();
    void SetActive(bool Active);
    const FGuLiGroundHUDSnapshot& Read(EGuLiGroundHUDGroup Groups);
    void Invalidate(EGuLiGroundHUDGroup Groups,bool Urgent=false);
    FGuLiGroundHUDUrgent OnUrgent;FGuLiGroundHUDHit OnHit;
    FGuLiGroundHUDMotionChanged OnMotion;
    double GetLogicalSeconds() const;
private:
    void PresentationChanged(const FGuLiGroundPresentationEvent& Event);
    void PrimeMotion();
    void ObjectChanged(const FGuLiHybridObjectKey& Key);
    void ReadObject(const FGuLiHybridObjectKey& Key);
    void DamageFeedback(const FGuLiDamageFeedback& Feedback);
    void EndFrame();
    UFUNCTION() void HealthChanged(float Health,float Maximum);
    UFUNCTION() void BlinkChanged(uint8 Phase,bool Cooling);
    UFUNCTION() void RocketChanged(float Fuel,bool Thrusting);
    UFUNCTION() void BuildChanged(FGuid Player,int64 Revision);
    TWeakObjectPtr<AGuLiCommanderPlayerController> Controller;
    TWeakObjectPtr<AGuLiGroundMechCharacter> Pawn;
    TWeakObjectPtr<UGuLiHybridWorldSubsystem> Hybrid;
    TWeakObjectPtr<UGuLiRogueCardSubsystem> Rogue;
    FGuLiHybridObjectKey PlayerKey;
    FGuid PlayerGuid;uint32 Epoch=0;
    FGuid FeedbackPlayer;uint32 FeedbackEpoch=0;TWeakObjectPtr<UWorld> FeedbackWorld;
    double NextPositionSample=0;
    FDelegateHandle ObjectHandle,DamageHandle,FrameHandle,PresentationHandle;
    EGuLiGroundHUDGroup Dirty=EGuLiGroundHUDGroup::All,Urgent=EGuLiGroundHUDGroup::None;
    FGuLiGroundHUDSnapshot View;
    struct FBuff { TArray<int32> Ids;uint64 Expires=0;bool bTimed=false; };
    TMap<FGuLiHybridObjectKey,FBuff> HostBuffs;
    struct FBuildingMapInfo { FText Name;FVector Extent=FVector::ZeroVector; };
    TMap<FName,FBuildingMapInfo> BuildingMapInfo;
    TSet<FGuLiHybridObjectKey> DeferredObjects;
    TSet<FGuid> HitEvents;TArray<FGuid> HitOrder;int32 HitCursor=0;
    bool bActive=false,bHitPending=false,bKillPending=false,bLastThrust=false,bLastEmpty=false;
    bool bPrimeMotion=true,bJetReadyArmed=false;
    uint64 MotionGeneration=0;
    FGuLiGroundHUDMotion PendingMotion;
};
