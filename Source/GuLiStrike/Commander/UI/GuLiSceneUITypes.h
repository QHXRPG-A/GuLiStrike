#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"

class UTextureRenderTarget2D;

/** Local identity includes a source lifetime and a monotonically allocated element lifetime. */
struct FGuLiSceneUIElementKey
{
    TWeakObjectPtr<const UObject> Source;
    uint64 Element=0,Generation=0;
    bool operator==(const FGuLiSceneUIElementKey& R) const {return Source==R.Source && Element==R.Element && Generation==R.Generation;}
    friend uint32 GetTypeHash(const FGuLiSceneUIElementKey& K)
    {return HashCombine(HashCombine(GetTypeHash(K.Source),GetTypeHash(K.Element)),GetTypeHash(K.Generation));}
};

/** Sample world positions only. Camera projection, health, visibility and receipts never use this clock. */
struct FGuLiSceneUIPositionCache
{
    struct FSample {FVector Position=FVector::ZeroVector;double At=-1;uint64 Urgent=0,Seen=0;bool Selected=false,Valid=false;};
    TMap<FGuLiSceneUIElementKey,FSample> Samples;
    uint64 Frame=0,OrdinarySamples=0,ImmediateSamples=0,Hits=0;
    void Begin() {++Frame;}
    void End() {for (auto It=Samples.CreateIterator();It;++It) if (It.Value().Seen!=Frame || !It.Key().Source.IsValid()) It.RemoveCurrent();}
    void Reset() {Samples.Reset();}
    bool Resolve(const FGuLiSceneUIElementKey& Key,uint64 Urgent,bool Selected,double Now,
        TFunctionRef<bool(FVector&)> Read,FVector& Out)
    {
        auto& S=Samples.FindOrAdd(Key);S.Seen=Frame;
        const bool Immediate=Selected || S.Selected!=Selected || S.Urgent!=Urgent || S.At<0;
        if (Immediate || Now-S.At>=.1)
        {
            S.Valid=Read(S.Position);S.At=Now;S.Urgent=Urgent;S.Selected=Selected;
            if (Immediate) ++ImmediateSamples;else ++OrdinarySamples;
        }
        else ++Hits;
        Out=S.Position;return S.Valid;
    }
};

/** Read-only display snapshots. None of these values is replicated or authoritative. */
struct FGuLiSceneUIRing
{
    FGuLiSceneUIElementKey Key;
    uint64 UrgentRevision=0;
	FVector Center = FVector::ZeroVector;
	float OuterRadiusCm = 0;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	bool bSelected = false;
	bool operator==(const FGuLiSceneUIRing& R) const { return Key==R.Key && Center==R.Center && OuterRadiusCm==R.OuterRadiusCm && Team==R.Team && bSelected==R.bSelected; }
};

struct FGuLiSceneUIHealthBar
{
    FGuLiSceneUIElementKey Key;
	FVector Center = FVector::ZeroVector;
	float Fraction = 0;
	bool bSelected = false;
	bool operator==(const FGuLiSceneUIHealthBar& R) const { return Key==R.Key && Center==R.Center && Fraction==R.Fraction && bSelected==R.bSelected; }
};

struct FGuLiSceneUILine
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	bool operator==(const FGuLiSceneUILine& R) const { return Start==R.Start && End==R.End; }
};

/** Existing Ship widgets remain their data/render sources, with no scene pass. */
struct FGuLiSceneUIWorldWidget
{
	TWeakObjectPtr<UTextureRenderTarget2D> Texture;
	FTransform Transform;
	FVector2D Size = FVector2D::ZeroVector;
	FVector2D Pivot = FVector2D(.5);
	bool operator==(const FGuLiSceneUIWorldWidget& R) const { return Texture==R.Texture && Transform.Equals(R.Transform,0) && Size==R.Size && Pivot==R.Pivot; }
};

namespace GuLiSceneUI
{
	inline constexpr float RingWidthCm = 20.f;
	inline constexpr int32 RingSegments = 64;
}
