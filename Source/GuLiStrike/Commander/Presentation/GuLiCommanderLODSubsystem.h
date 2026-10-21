#pragma once

#include "CoreMinimal.h"
#include "Commander/Presentation/GuLiCommanderLODPolicy.h"
#include "ConvexVolume.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "GuLiCommanderLODSubsystem.generated.h"

class UMeshComponent;
class UMaterialInterface;

/** Client-World policy shared by commander consumers. No actors, effects or gameplay are owned here. */
UCLASS()
class GULISTRIKE_API UGuLiCommanderLODSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Mesh resources remain unchanged. Registrations are scoped to this presentation World. */
	void RegisterMesh(UMeshComponent* Component);
	void UnregisterMesh(UMeshComponent* Component);
	void RegisterActorMeshes(AActor* Actor);
	void UnregisterActorMeshes(AActor* Actor);
	static void NotifyActorChanged(AActor* Actor);
	static void NotifyMeshChanged(UMeshComponent* Component);
	static void ForgetMesh(UMeshComponent* Component);
	static void ForgetActor(AActor* Actor);
	/** Copies inherit the source appearance before transient LOD outline suppression. */
	static UMaterialInterface* GetMaterialForPresentationCopy(const UMeshComponent& Component, int32 Index);
	UFUNCTION(BlueprintPure, Category="Commander|LOD") FString GetMeshLODStatsJson() const;
	/** Shared meshes use the highest precision local view; only runtime LOD0 retains model outlines. */
	bool ShouldShowModelOutlines() const { return !bMeshPolicyEnabled || AppliedMeshTier == EGuLiPresentationTier::Full; }

	FGuLiCommanderLODDecision Evaluate(const FGuLiCommanderLODQuery& Query);
	FGuLiCommanderLODDecision EvaluateWorldEffectBounds(const FGuLiCommanderLODQuery& Query, double NonCommanderMaximumDistance);
	/** World-effect admission: commander LOD0/1/2 allow, LOD3 suppresses. Other views use the
	 * caller's distance limit (<= 0 means unlimited). Any eligible local view wins;
	 * registered visual components remain hidden per overview view by its own mask. */
	bool ShouldRenderWorldEffect(const FVector& Location, double NonCommanderMaximumDistance);
	bool ShouldRenderWorldEffectBounds(const FBox& Bounds, double NonCommanderMaximumDistance);
	/** Continuous FX opacity: commander near/tactical views retain full visibility;
	 * overview contributes none. Other local views fade over the supplied cm range.
	 * The most visible local view wins; overview retains its per-view component mask. */
	float GetContinuousEffectDistanceFade(const FVector& Location, double NonCommanderFadeStart, double NonCommanderFadeEnd);
	const FGuLiCommanderLODSettings& GetSettings() const { return EffectiveSettings; }
	TArray<FGuLiCommanderLODSettingView> ListSettings(const FString& Prefix = FString()) const;
	bool SetSetting(const FString& Key, const FString& Value, FString& Error);
	bool ResetSetting(const FString& KeyOrAll, FString& Error);

	/** One complete snapshot per consumer, per frame; clear when that consumer resets or retires. */
	void ReportConsumer(FName Consumer, const FGuLiCommanderLODConsumerStats& Stats);
	void ClearConsumer(FName Consumer);
	const TMap<FName, FGuLiCommanderLODConsumerStats>& GetConsumerStats() const { return ConsumerStats; }
	uint64 GetViewFrame() const { return ViewFrame; }
	int32 GetLocalViewCount() const { return Views.Num(); }
	int32 GetQueryCount() const { return QueryCount; }
	double GetViewUpdateMilliseconds() const { return ViewUpdateMilliseconds; }

private:
	friend class FGuLiCommanderMeshLODLifecycleTest;
	struct FLocalView
	{
		FVector Position = FVector::ZeroVector;
		FConvexVolume Frustum;
		float ProjectionScale = 1;
		TOptional<EGuLiCommanderLODLevel> CameraLevel;
		TOptional<EGuLiPresentationTier> PresentationTier;
	};
	void EnsureViews();
	struct FOutlineSlot
	{
		TStrongObjectPtr<UMaterialInterface> Override;
		TStrongObjectPtr<UMaterialInterface> Resolved;
	};
	struct FMeshEntry
	{
		TWeakObjectPtr<UMeshComponent> Component;
		TWeakObjectPtr<UObject> Resource;
		int32 OriginalMinLOD = 0, OriginalForcedLOD = 0, ResourceMinLOD = 0, LODCount = 0;
		bool bOriginalOverride = false, bNanite = false, bApplied = false;
		TMap<int32, FOutlineSlot> OutlineOverrides;
		int32 OriginalOverrideCount = 0;
	};
	void ApplyMeshEntry(FMeshEntry& Entry, bool bReduce);
	void RestoreMeshEntry(FMeshEntry& Entry);
	void ApplyModelOutline(FMeshEntry& Entry, bool bHide);
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NoModelOutlineMaterial;
	TMap<TWeakObjectPtr<UMeshComponent>, FMeshEntry> MeshEntries;
	TArray<TWeakObjectPtr<AActor>> PendingMeshActors;
	EGuLiPresentationTier RequestedMeshTier = EGuLiPresentationTier::Full;
	EGuLiPresentationTier AppliedMeshTier = EGuLiPresentationTier::Full;
	double LastMeshTierChangeSeconds = -1;
	uint64 MeshMembershipChanges = 0, MeshStateChanges = 0;
	bool bMeshPolicyInitialized = false, bMeshPolicyEnabled = true;
	FGuLiCommanderLODSettings BaselineSettings;
	FGuLiCommanderLODSettings EffectiveSettings;
	TSet<FString> ConfigKeys;
	TSet<FString> OverrideKeys;
	TArray<FLocalView> Views;
	TMap<FName, FGuLiCommanderLODConsumerStats> ConsumerStats;
	uint64 ViewFrame = MAX_uint64;
	int32 QueryCount = 0;
	double ViewUpdateMilliseconds = 0;
};
