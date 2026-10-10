#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"

class UTextureRenderTarget2D;

/** Read-only display snapshots. None of these values is replicated or authoritative. */
struct FGuLiSceneUIRing
{
	FVector Center = FVector::ZeroVector;
	float OuterRadiusCm = 0;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	bool bSelected = false;
	bool operator==(const FGuLiSceneUIRing& R) const { return Center==R.Center && OuterRadiusCm==R.OuterRadiusCm && Team==R.Team && bSelected==R.bSelected; }
};

struct FGuLiSceneUIHealthBar
{
	FVector Center = FVector::ZeroVector;
	float Fraction = 0;
	bool bSelected = false;
	bool operator==(const FGuLiSceneUIHealthBar& R) const { return Center==R.Center && Fraction==R.Fraction && bSelected==R.bSelected; }
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
