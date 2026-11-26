#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GuLiXuntianGunnery.generated.h"

class AGuLiStrikeShip;
class UGuLiStrikeWeaponPart;
class USkeletalMeshComponent;
struct FGuLiDamageFeedback;
class FGuLiHybridStateArchive;
struct FGuLiHybridManifestData;
struct FMinimalViewInfo;

// World-space geometry used by both the live mounts and a read-only survey snapshot.
struct FGuLiXuntianBoreGeometry
{
    FVector MountOrigin, RestHingeOffset, RestOffset, RestForward, FlatForward, Up;
};

struct FGuLiXuntianGroundCoverage
{
    TArray<FGuLiXuntianBoreGeometry,TInlineAllocator<2>> Bores;
    FQuat Hull;
    float MinDepression, MaxDepression;
    double MarginAt(const FVector& WorldPoint) const;
};

struct FGuLiXuntianMount : FGuLiXuntianBoreGeometry
{
    FName Socket;
    TWeakObjectPtr<UGuLiStrikeWeaponPart> Part;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TArray<FTransform> Bores;
    FVector Hinge = FVector::ZeroVector;
    FQuat HingeAxes = FQuat::Identity;
    FTransform RestVisual, Frame;
    bool bHasGeometry=false;
    FQuat Aim = FQuat::Identity;
    FVector Muzzle = FVector::ZeroVector, Direction = FVector::ForwardVector;
    FVector WorldHinge = FVector::ZeroVector;
    double BoreDistance = 0;
    int32 Barrel = 0;
    bool bGround = false, bReachable = false;
};

struct FGuLiXuntianKill
{
    FGuid Event;
    FString Source, Target;
    EGuLiTeam SourceTeam = EGuLiTeam::Unassigned;
    EGuLiTeam TargetTeam = EGuLiTeam::Unassigned;
    double Time = 0;
};

struct FGuLiXuntianCardSlot
{
    FName Card;
    int32 Level = 0;
};

struct FGuLiXuntianBatteryState
{
    struct FBore { FName Socket; FQuat Aim = FQuat::Identity; int32 Barrel = 0; };
    FVector Point = FVector::ZeroVector;
    float MinAngle = 45, MaxAngle = 90;
    bool bGround = false, bHasAim = false;
    double Cooldown = 0;
    int32 NextAir = 0, NextGround = 0;
    TArray<FBore> Bores;
    void SerializeAim(FGuLiHybridStateArchive& Ar);
    void Serialize(FGuLiHybridStateArchive& Ar);
};

/** Combat controls and authoritative mount geometry for the 5002 chassis. */
UCLASS(ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiXuntianGunnery final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiXuntianGunnery();
    UFUNCTION(BlueprintPure, Category="巡天|舰腹射界")
    void GetGroundGunLimits(float& MinDeg, float& MaxDeg) const;
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="巡天|舰腹射界")
    bool SetGroundGunLimits(float MinDeg, float MaxDeg);

    UPROPERTY(EditDefaultsOnly, Category="巡天|舰腹射界", meta=(ClampMin="0", ClampMax="90", Units="deg"))
    float GroundMinDepression = 45;
    UPROPERTY(EditDefaultsOnly, Category="巡天|舰腹射界", meta=(ClampMin="0", ClampMax="90", Units="deg"))
    float GroundMaxDepression = 90;

    static constexpr float Reach = 190000;
    static bool ValidLimits(float MinDeg, float MaxDeg);
    static bool Covers(const FVector& HullLocalDirection, float MinDeg, float MaxDeg);
    static bool BindRangeHull(FGuLiHybridManifestData& Data,FString& Error);
    static void AddManifestRules(FGuLiHybridManifestData& Data);
    void Orbit(FVector2D Delta);
    void Zoom(float Notches);
    void GetCameraView(FMinimalViewInfo& View) const;
    void SetTrigger(bool bDown);
    bool IsTriggerHeld() const { return bTrigger; }
    void ResetControls();
    void UpdateLocalView();
    void Advance(double Delta, bool bAllowFire);
    void ApplyRemoteAim(FVector Point, bool Ground, float MinDeg, float MaxDeg);
    int32 CoverageAt(const FVector& WorldPoint) const;
    FGuLiXuntianGroundCoverage SnapshotGroundCoverage() const;
    bool SurfaceAt(FVector XY, FVector& Point) const;
    void RestartMountCache() { SeenLoadout = MAX_uint32; }
    void RefreshMounts();
    float InstrumentBlend() const;
    const TArray<FGuLiXuntianMount>& GetMounts() const {return Mounts;}
    const TArray<FGuLiCombatTargetSnapshot>& GetContacts() const {return Contacts;}
    const TArray<FGuLiXuntianKill>& GetFeed() const {return Feed;}
    const TArray<FGuLiXuntianCardSlot>& GetCards() const {return Cards;}
    // Full holdings snapshot, ordered by the four design branches. Empty slots have no card.
    UFUNCTION(BlueprintCallable, Category="巡天|HUD")
    bool SetOwnedCardSnapshot(int64 Revision, const TMap<FName,int32>& OwnedLevels);
    FVector AimPoint = FVector::ZeroVector;
    FGuLiTargetHandle Focus;
    bool bGround = false, bHasAim = false, bAimingAtSurface = false;
    static constexpr float InitialViewPitch = 1.3751f;
    float ViewPitch = InitialViewPitch, ViewYaw = 0, HeightAGL = 0;
    FVector GroundHint = FVector::ZeroVector;
    bool bGroundHint = false;
    void ReadState(FGuLiXuntianBatteryState& State, bool bIncludeBores) const;
    void RestoreState(const FGuLiXuntianBatteryState& State, bool bIncludeBores);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    AGuLiStrikeShip* Ship() const;
    void UpdateGun(FGuLiXuntianMount& Gun, double Delta);
    void RefreshContacts();
    void UpdateGroundHint();
    void OnDamage(const FGuLiDamageFeedback& Feedback);
    bool Fire();
    bool bTrigger = false, bViewStarted = false;
    double Boom = 9100, Cooldown = 0, NextContacts = 0, NextHint = 0;
    uint32 SeenLoadout = MAX_uint32;
    int32 NextAir = 0, NextGround = 0;
    int64 CardRevision = -1;
    TArray<FGuLiXuntianMount> Mounts;
    TArray<int32> BellyMounts;
    TArray<FGuLiCombatTargetSnapshot> Contacts;
    TArray<FGuLiXuntianKill> Feed;
    TArray<FGuLiXuntianCardSlot> Cards;
};
