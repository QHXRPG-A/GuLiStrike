// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "GameFramework/Actor.h"
#include "MassArchetypeTypes.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationPolicy.h"
#include "GuLiWingmanPresentationActor.generated.h"

class UInstancedStaticMeshComponent;
class UMassEntitySubsystem;
class USceneComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EGuLiWingmanPresentationRole : uint8
{
	Owner = 0,
	Remote
};

struct FGuLiWingmanPresentationTrack
{
	FGuLiWingmanHandle Handle;
	TArray<FGuLiWingmanPresentationPose> Samples;
	FTransform PresentedTransform = FTransform::Identity;
	FTransform PendingRemoteMirrorTransform = FTransform::Identity;
	FMassEntityHandle RemoteMirrorEntity;
	float Opacity = 0.0f;
	bool bAlive = true;
	bool bHasPresentedTransform = false;
	bool bInteractable = false;
	bool bRemoteMirrorUpdatePending = false;
	bool bPendingRemoteMirrorInteractable = false;
};

struct FGuLiWingmanPresentationGroupRuntime
{
	FGuLiWingmanGroupHandle Group;
	FGuid LeaseOwnerPlayerGuid;
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	TArray<FGuLiWingmanPresentationTrack> Tracks;
	EGuLiWingmanPresentationRole Role = EGuLiWingmanPresentationRole::Remote;
	uint64 LastBootstrapCutId = 0u;
	uint32 LastSourceSequence = 0u;
	double LastSourceTimeSeconds = 0.0;
	double ClockServerSeconds = 0.0;
	double ClockLocalReceiptSeconds = 0.0;
	int32 InstanceBaseIndex = INDEX_NONE;
	bool bHasClock = false;
	bool bUsingServerTimeline = true;
};

/** Exact last server-Accepted pose for client-side target selection; never extrapolated. */
struct GULISTRIKE_API FGuLiWingmanAcceptedTargetPose
{
	FGuLiWingmanHandle Wingman;
	FGuid LeaseOwnerPlayerGuid;
	FTransform Transform = FTransform::Identity;
	double ServerAcceptedTimeSeconds = 0.0;
	uint32 AcceptedSequence = 0u;
};

struct FGuLiWingmanInstancePool
{
	TArray<int32> FreeBlockBaseIndices;
	TArray<FTransform> CachedTransforms;
	TArray<float> CachedOpacities;
};

/**
 * Client-only Wingman renderer. Owner poses and accepted remote poses use
 * separate ISM pools; only remote poses create presentation-only Mass mirrors.
 * No value in this actor is replicated or accepted back by authority.
 */
UCLASS(Transient, NotPlaceable, Config = Game)
class GULISTRIKE_API AGuLiWingmanPresentationActor final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiWingmanPresentationActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Finds the per-World actor or creates it locally. Returns null on Dedicated Server. */
	static AGuLiWingmanPresentationActor* FindOrSpawn(UWorld* World);

	/**
	 * Six-scope bootstrap is the only group creation gate. Validation and local
	 * staging complete before the prior generation is replaced.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	bool ApplyBootstrap(
		const FGuLiWingmanBootstrapBundle& Bundle,
		bool bLocallyOwned,
		double LocalReceiptTimeSeconds);

	/** Local Lease Owner frame; it never enters the remote accepted timeline. */
	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	bool ApplyOwnerFrame(
		const FGuLiWingmanCandidateBatch& Candidate,
		double LocalTimeSeconds);

	/** Narrow Relay OnRep entry point for one accepted snapshot. */
	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	bool ApplyAcceptedSnapshot(
		const FGuLiWingmanAcceptedBatch& AcceptedBatch,
		double LocalReceiptTimeSeconds);

	/** Optional clock observation from the connection clock-sync path. */
	bool ObserveServerClock(
		const FGuLiWingmanGroupHandle& Group,
		double EstimatedServerNowSeconds,
		double LocalReceiptTimeSeconds);

	/** Role changes clear the old source timeline; the next frame must rebase it. */
	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	bool SetGroupRole(
		const FGuLiWingmanGroupHandle& Group,
		EGuLiWingmanPresentationRole NewRole);

	/** Reliable roster/death OnRep hook. */
	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	bool SetWingmanAlive(const FGuLiWingmanHandle& Wingman, bool bAlive);

	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	bool RemoveGroup(const FGuLiWingmanGroupHandle& Group);

	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	void ResetAllGroups();

	/** Read-only client query for HUD/picking; false while fading or hidden. */
	UFUNCTION(BlueprintPure, Category = "Wingman|Presentation")
	bool TryGetPresentedTransform(
		const FGuLiWingmanHandle& Wingman,
		FTransform& OutTransform) const;

	UFUNCTION(BlueprintPure, Category = "Wingman|Presentation")
	bool IsWingmanInteractable(const FGuLiWingmanHandle& Wingman) const;

	/**
	 * Enumerates fresh exact Accepted samples for remote groups. It deliberately
	 * ignores PresentedTransform, interpolation and extrapolation.
	 */
	void GetFreshAcceptedTargetPoses(
		double EstimatedServerNowSeconds,
		double MaximumAcceptedAgeSeconds,
		TArray<FGuLiWingmanAcceptedTargetPose>& OutPoses) const;

	/** Runtime override; otherwise the client loads DefaultPresentationMesh lazily. */
	UFUNCTION(BlueprintCallable, Category = "Wingman|Presentation")
	void ConfigurePresentationMeshes(UStaticMesh* InOwnerMesh, UStaticMesh* InRemoteMesh);

	UFUNCTION(BlueprintPure, Category = "Wingman|Presentation")
	UInstancedStaticMeshComponent* GetOwnerInstances() const { return OwnerInstances; }

	UFUNCTION(BlueprintPure, Category = "Wingman|Presentation")
	UInstancedStaticMeshComponent* GetRemoteInstances() const { return RemoteInstances; }

private:
	bool EnsureClientResources();
	void ConfigureInstanceComponent(UInstancedStaticMeshComponent& Component, UStaticMesh* Mesh) const;
	bool EnsureRemoteMassArchetype();

	bool BuildRuntimeFromBootstrap(
		const FGuLiWingmanBootstrapBundle& Bundle,
		EGuLiWingmanPresentationRole PresentationRole,
		double LocalReceiptTimeSeconds,
		FGuLiWingmanPresentationGroupRuntime& OutRuntime) const;
	bool ValidateAcceptedBatch(
		const FGuLiWingmanPresentationGroupRuntime& Runtime,
		const FGuLiWingmanAcceptedBatch& AcceptedBatch) const;
	bool AppendAcceptedBatch(
		FGuLiWingmanPresentationGroupRuntime& Runtime,
		const FGuLiWingmanAcceptedBatch& AcceptedBatch) const;
	bool ValidateCandidate(
		const FGuLiWingmanPresentationGroupRuntime& Runtime,
		const FGuLiWingmanCandidateBatch& Candidate) const;

	int32 AllocateInstanceBlock(EGuLiWingmanPresentationRole PresentationRole);
	void ReleaseInstanceBlock(EGuLiWingmanPresentationRole PresentationRole, int32 BaseIndex);
	FGuLiWingmanInstancePool& GetInstancePool(EGuLiWingmanPresentationRole PresentationRole);
	const FGuLiWingmanInstancePool& GetInstancePool(EGuLiWingmanPresentationRole PresentationRole) const;
	UInstancedStaticMeshComponent* GetInstanceComponent(EGuLiWingmanPresentationRole PresentationRole) const;
	bool UpdateInstance(
		EGuLiWingmanPresentationRole PresentationRole,
		int32 InstanceIndex,
		const FTransform& Transform,
		float Opacity);

	void TickGroup(FGuLiWingmanPresentationGroupRuntime& Runtime, double LocalNowSeconds);
	void DestroyRemoteMirrors(FGuLiWingmanPresentationGroupRuntime& Runtime);
	void UpdateRemoteMirror(
		FGuLiWingmanPresentationTrack& Track,
		const FTransform& Transform,
		bool bInteractable);
	void FlushRemoteMirrorUpdate(FGuLiWingmanPresentationTrack& Track);
	FGuLiWingmanPresentationTrack* FindTrack(const FGuLiWingmanHandle& Wingman);
	const FGuLiWingmanPresentationTrack* FindTrack(const FGuLiWingmanHandle& Wingman) const;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Presentation")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Created dynamically only in non-Dedicated game Worlds. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Wingman|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> OwnerInstances;

	/** Created dynamically only in non-Dedicated game Worlds. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Wingman|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> RemoteInstances;

	UPROPERTY(EditDefaultsOnly, Category = "Wingman|Presentation")
	TObjectPtr<UStaticMesh> OwnerMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Wingman|Presentation")
	TObjectPtr<UStaticMesh> RemoteMesh;

	/** Client-only soft reference. Dedicated Server never resolves this asset. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Wingman|Presentation")
	TSoftObjectPtr<UStaticMesh> DefaultPresentationMesh;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Wingman|Presentation", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float InterpolationBackTimeSeconds = 0.1f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Wingman|Presentation", meta = (ClampMin = "1"))
	int32 MaximumPresentedGroups = 128;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Wingman|Presentation", meta = (ClampMin = "0", Units = "cm"))
	int32 CullDistanceCentimeters = 300000;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> MassEntitySubsystem;

	FMassArchetypeHandle RemoteMassArchetype;
	TMap<FGuLiWingmanGroupHandle, FGuLiWingmanPresentationGroupRuntime> Groups;
	FGuLiWingmanInstancePool OwnerPool;
	FGuLiWingmanInstancePool RemotePool;
	bool bOwnerRenderStateDirty = false;
	bool bRemoteRenderStateDirty = false;
};
