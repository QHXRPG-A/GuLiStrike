#pragma once

#include "Commander/UI/GuLiSceneUITypes.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "UI/GuLiSegmentedHealthBar.h"
struct FSceneViewProjectionData;
struct FGuLiCombatTargetSnapshot;
class UGuLiXuntianGunnery;

/** Coordinates are in the caller's local canvas (viewport pixels or pilot design units). */
struct FGuLiHealthBarView
{
	struct FPoint { FVector2D Screen; double W, NdcY; };
	FGuLiHealthBarView(const FSceneViewProjectionData& Projection, FVector2D CanvasSize, const FMatrix* Combined = nullptr);
	bool Project(const FVector& Center, FPoint& Point) const;
	bool Place(const FPoint& Point, const FGuLiHealthBarModel& Model, bool bShield,
		double BadgeTop, FBox2D& Bounds) const;
	FMatrix Matrix;
	FBox2D Viewport;
	FVector2D Half, Middle;
	FVector ClipY, ClipW;
	double FocalX, DesignPixel;
};

struct FGuLiPilotHealthFrame
{
	struct FContact
	{
		int32 Index;
		FVector2D Center, Flag;
		double Radius, Range, DeltaZ, Miss, Depth;
		bool bBadge, bChanged;
		FBox2D Bar{ForceInit};
		GuLiHealthGrid::FBarValue Life, Shield;
	};
	TArray<FContact> Contacts;
	GuLiHealthGrid::FMeshBatch Bars;
};

/** Per-player hold of world-space anchors. Camera projection and lifetime checks use the current frame. */
class FGuLiHealthBarPoseCache
{
public:
	static constexpr double SampleInterval = 1.0 / 30.0;
	void BeginFrame(double Now);
	bool Resolve(FGuLiSceneUIHealthBar& Bar, TFunctionRef<bool(FGuLiHealthBarModel&)> Read,
		uint32 DisplacementRevision = 0);
	void ApplySupplied(TArray<FGuLiSceneUIHealthBar>& Bars);
	void EndFrame();
	void Reset();
	uint64 GetSampleCount() const { return SampleCount; }

private:
	struct FAnchor
	{
		FGuLiHybridObjectKey Target;
		FGuLiHealthBarModel Model;
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
	static FBox2D OverviewBounds(const FGuLiSceneUIHealthBar& Bar, FVector2D Anchor, float DPI);
	bool WorldBounds(const FGuLiHealthBarView& View, const FGuLiSceneUIHealthBar& Bar, FBox2D& Bounds);
	const FGuLiPilotHealthFrame& PilotFrame(const UGuLiXuntianGunnery& Gun, const FGuLiHealthBarView& View,
		const GuLiHealthGrid::FCanvas& Canvas, FVector Hull, uint64 VitalRevision, double Now,
		TFunctionRef<bool(const FGuLiCombatTargetSnapshot&,FGuLiHealthBarModel&)> ReadModel,
		TFunctionRef<const FGuLiSceneUIHealthBar*(const FGuLiCombatTargetSnapshot&)> ReadVitals);
	void ResetPilot();
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
		float Health, MaxHealth, Shield, MaxShield;
		uint64 Seen = 0;
	};
	double NextPublication = -1;
	TMap<FGuLiSceneUIElementKey, FInfo> Information;
	struct FPilotModel
	{
		FGuLiHealthBarModel Body;
		FVector ContactCenter = FVector::ZeroVector;
		uint64 LastRevision = 0;
		bool bDetailed = false;
	};
	TMap<FGuLiTargetHandle, FPilotModel> PilotModels;
	FGuLiPilotHealthFrame Pilot;
	TWeakObjectPtr<const UGuLiXuntianGunnery> PilotOwner;
	FGuLiTargetHandle PilotFocus;
	FVector PilotHull = FVector::ZeroVector;
	FMatrix PilotMatrix = FMatrix::Identity;
	FBox2D PilotViewport{ForceInit};
	FSlateRenderTransform PilotTransform;
	FVector2D PilotCanvasScale = FVector2D::ZeroVector;
	uint64 PilotContactsRevision = MAX_uint64, PilotVitalRevision = MAX_uint64;
	double NextPilotSample = -1;
};
