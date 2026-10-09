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
};

struct FGuLiSceneUIHealthBar
{
	FVector Center = FVector::ZeroVector;
	float Fraction = 0;
	bool bSelected = false;
};

struct FGuLiSceneUILine
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
};

/** Existing Ship widgets remain their data/render sources, with no scene pass. */
struct FGuLiSceneUIWorldWidget
{
	TWeakObjectPtr<UTextureRenderTarget2D> Texture;
	FTransform Transform;
	FVector2D Size = FVector2D::ZeroVector;
	FVector2D Pivot = FVector2D(.5);
};

namespace GuLiSceneUI
{
	inline constexpr float RingWidthCm = 20.f;
	inline constexpr int32 RingSegments = 64;
}
