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
    struct FMarker
    {
        FVector2D TargetPoint{};
        float Progress=0, NextSide=1;
        int32 LastPercent=INDEX_NONE, LastMeters=INDEX_NONE;
        FString PercentLabel,DistanceLabel;
        double Released=-1,Seated=-1;
        TArray<FImpulse,TInlineAllocator<8>> Impulses;
    };
    TMap<FGuLiTargetHandle,FMarker> Markers;
    FVector2D Center{};
    float Radius=0,DesignRadius=0,HexRadius=0;
    double Entry=-1;
};
