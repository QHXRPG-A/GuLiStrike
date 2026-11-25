#pragma once

#include "Commander/UI/GuLiSceneUITypes.h"

/** Per-player hold of world-space anchors. Camera projection and lifetime checks use the current frame. */
class FGuLiHealthBarPoseCache
{
public:
	static constexpr double SampleInterval = 1.0 / 30.0;
	void BeginFrame(double Now);
	bool Resolve(const FGuLiSceneUIHealthBar& Bar, TFunctionRef<bool(FVector&)> Read, FVector& Out,
		uint32 DisplacementRevision = 0);
	void ApplySupplied(TArray<FGuLiSceneUIHealthBar>& Bars);
	void EndFrame();
	void Reset();
	uint64 GetSampleCount() const { return SampleCount; }

private:
	struct FAnchor
	{
		FGuLiHybridObjectKey Target;
		FVector Center = FVector::ZeroVector;
		uint32 DisplacementRevision = 0;
		uint64 SeenFrame = 0;
		bool bValid = false;
	};
	TMap<FGuLiSceneUIElementKey, FAnchor> Anchors;
	double LastFrameTime = -1, NextSample = -1;
	uint64 Frame = 0, SampleCount = 0;
	bool bSampleFrame = false;
};

/** A local view samples world anchors at 30 Hz, publishes combat information at 10 Hz and projects both for the current camera. */
class FGuLiHealthBarLayout
{
public:
	FGuLiHealthBarPoseCache Poses;
	struct FVisible
	{
		int32 Index;
		FBox2D Bounds;
		float Opacity;
	};

	bool Publish(TArray<FGuLiSceneUIHealthBar>& Bars, double Now);
	static FBox2D ScreenBounds(const FGuLiSceneUIHealthBar& Bar, FVector2D Anchor, float DPI, bool bOverview);
	void ProjectVisible(TConstArrayView<FGuLiSceneUIHealthBar> Bars, const FBox2D& Viewport,
		TFunctionRef<bool(int32, FBox2D&, float&)> Project);
	void Reset();
	FString Describe(TConstArrayView<FGuLiSceneUIHealthBar> Bars, bool bIncludeCandidates) const;
	TArray<FVisible> Visible;
	int32 Projected = 0;
	double PublishedAt = -1;
	uint64 Publications = 0;

private:
	struct FInfo
	{
		FGuLiHybridObjectKey Target;
		float Health, Shield;
		bool HasShield;
	};
	double NextPublication = -1;
	TMap<FGuLiSceneUIElementKey, FInfo> Information;
};
