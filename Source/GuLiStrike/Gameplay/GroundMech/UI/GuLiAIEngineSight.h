#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"

class UGuLiGroundMechWeaponComponent;
class FSlateWindowElementList;
struct FGeometry;

/** Local optical animation. Only the weapon selects targets or supplies lock progress. */
struct FGuLiAIEngineSight
{
    FSlateBrush Atlas;
    void Update(const UGuLiGroundMechWeaponComponent& Weapon, float ViewportScale, double Now, bool PlayEntry);
    void Reset();
    void Shot(double At,float Kick,float Jitter,float Duration);
    void Paint(const FGeometry& Geometry,FSlateWindowElementList& Out,int32 Layer,double Now,float MaxKick) const;
    bool IsVisible() const { return Radius>0; }

private:
    struct FImpulse { double At; float Kick,Jitter,Duration,Side; };
    TArray<FImpulse,TInlineAllocator<8>> Impulses;
    FVector2D Center{}, TargetPoint{};
    FGuLiTargetHandle Target;
    float Radius=0,Progress=0,NextSide=1;
    int32 LastPercent=INDEX_NONE,LastMeters=INDEX_NONE;
    FString PercentLabel,DistanceLabel;
    double Entry=-1,Released=-1,Seated=-1;
};
