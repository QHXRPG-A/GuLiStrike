// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationPolicy.h"
#include "GuLiWingmanPresentationActor.generated.h"

class AGuLiWingmanPawn;
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
	TWeakObjectPtr<AGuLiWingmanPawn> PresentedPawn;
	float Opacity = 0.0f;
	uint32 LastRebasedSequence = 0u;
	bool bAlive = true;
	bool bHasPresentedTransform = false;
	bool bInteractable = false;
};

struct FGuLiWingmanPresentationGroupRuntime
{
	FGuLiWingmanGroupHandle Group;
	FGuid LeaseOwnerPlayerGuid;
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	TArray<FGuLiWingmanPresentationTrack> Tracks;
	EGuLiWingmanPresentationRole Role = EGuLiWingmanPresentationRole::Remote;
	uint64 LastBootstrapCutId = 0u;
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> LastSourceSequenceByFlight{};
	TStaticArray<double, GULI_WINGMAN_FLIGHT_COUNT> LastSourceTimeSecondsByFlight{};
	uint32 LastSourceSequence = 0u;
	double LastSourceTimeSeconds = 0.0;
	double ClockServerSeconds = 0.0;
	double ClockLocalReceiptSeconds = 0.0;
	bool bHasClock = false;
	bool bUsingServerTimeline = true;
};

struct GULISTRIKE_API FGuLiWingmanAcceptedTargetPose
{
	FGuLiWingmanHandle Wingman;
	FGuid LeaseOwnerPlayerGuid;
	FTransform Transform = FTransform::Identity;
	double ServerAcceptedTimeSeconds = 0.0;
	uint32 AcceptedSequence = 0u;
};

/**
 * Client-only logical snapshot manager and remote Wingman Actor pool.
 * Owner tracks point at the Pawns simulated by UGuLiWingmanSimulationSubsystem;
 * remote tracks receive at most 175 lightweight interpolation-only Pawns.
 */
UCLASS(Transient, NotPlaceable, Config=Game)
class GULISTRIKE_API AGuLiWingmanPresentationActor final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiWingmanPresentationActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	static AGuLiWingmanPresentationActor* FindOrSpawn(UWorld* World);

	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	bool ApplyBootstrap(const FGuLiWingmanBootstrapBundle& Bundle,
		bool bLocallyOwned, double LocalReceiptTimeSeconds);
	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	bool ApplyOwnerFrame(const FGuLiWingmanCandidateBatch& Candidate,
		double LocalTimeSeconds);
	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	bool ApplyAcceptedSnapshot(const FGuLiWingmanAcceptedBatch& AcceptedBatch,
		double LocalReceiptTimeSeconds);
	bool ObserveServerClock(const FGuLiWingmanGroupHandle& Group,
		double EstimatedServerNowSeconds, double LocalReceiptTimeSeconds);
	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	bool SetGroupRole(const FGuLiWingmanGroupHandle& Group,
		EGuLiWingmanPresentationRole NewRole);
	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	bool SetWingmanAlive(const FGuLiWingmanHandle& Wingman, bool bAlive);
	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	bool RemoveGroup(const FGuLiWingmanGroupHandle& Group);
	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	void ResetAllGroups();

	UFUNCTION(BlueprintPure, Category="Wingman|Presentation")
	bool TryGetPresentedTransform(const FGuLiWingmanHandle& Wingman,
		FTransform& OutTransform) const;
	UFUNCTION(BlueprintPure, Category="Wingman|Presentation")
	bool IsWingmanInteractable(const FGuLiWingmanHandle& Wingman) const;
	UFUNCTION(BlueprintPure, Category="Wingman|Presentation")
	AGuLiWingmanPawn* FindPresentedPawn(const FGuLiWingmanHandle& Wingman) const;
	UFUNCTION(BlueprintPure, Category="Wingman|Presentation")
	int32 GetActiveActorCount() const;
	UFUNCTION(BlueprintPure, Category="Wingman|Presentation")
	int32 GetActiveRemoteActorCount() const;

	bool HasAppliedBootstrap(const FGuLiWingmanGroupHandle& Group, uint64 CutId) const;
	void GetFreshAcceptedTargetPoses(double EstimatedServerNowSeconds,
		double MaximumAcceptedAgeSeconds,
		TArray<FGuLiWingmanAcceptedTargetPose>& OutPoses) const;
	UFUNCTION(BlueprintCallable, Category="Wingman|Presentation")
	void ConfigurePresentationMeshes(UStaticMesh* InOwnerMesh, UStaticMesh* InRemoteMesh);

private:
	bool EnsureClientResources();
	bool BuildRuntimeFromBootstrap(const FGuLiWingmanBootstrapBundle& Bundle,
		EGuLiWingmanPresentationRole PresentationRole,
		double LocalReceiptTimeSeconds,
		FGuLiWingmanPresentationGroupRuntime& OutRuntime) const;
	bool ValidateAcceptedBatch(const FGuLiWingmanPresentationGroupRuntime& Runtime,
		const FGuLiWingmanAcceptedBatch& AcceptedBatch) const;
	bool AppendAcceptedBatch(FGuLiWingmanPresentationGroupRuntime& Runtime,
		const FGuLiWingmanAcceptedBatch& AcceptedBatch) const;
	bool ValidateCandidate(const FGuLiWingmanPresentationGroupRuntime& Runtime,
		const FGuLiWingmanCandidateBatch& Candidate) const;
	void TickGroup(FGuLiWingmanPresentationGroupRuntime& Runtime,
		double LocalNowSeconds);
	void RefreshActorAllocation();
	AGuLiWingmanPawn* AcquireRemotePawn(const FGuLiWingmanHandle& Handle);
	void ReleaseRemotePawn(FGuLiWingmanPresentationTrack& Track);
	FVector GetLocalViewLocation() const;
	FGuLiWingmanPresentationTrack* FindTrack(const FGuLiWingmanHandle& Wingman);
	const FGuLiWingmanPresentationTrack* FindTrack(const FGuLiWingmanHandle& Wingman) const;

	UPROPERTY(VisibleAnywhere, Category="Wingman|Presentation")
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(EditDefaultsOnly, Category="Wingman|Presentation")
	TObjectPtr<UStaticMesh> OwnerMesh;
	UPROPERTY(EditDefaultsOnly, Category="Wingman|Presentation")
	TObjectPtr<UStaticMesh> RemoteMesh;
	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Presentation")
	TSoftObjectPtr<UStaticMesh> DefaultPresentationMesh;
	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Presentation",
		meta=(ClampMin="0.0", ClampMax="0.5"))
	float InterpolationBackTimeSeconds = 0.1f;
	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Presentation", meta=(ClampMin="1"))
	int32 MaximumPresentedGroups = 128;
	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Presentation", meta=(ClampMin="1"))
	int32 MaximumActiveWingmanActors = 200;
	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Presentation", meta=(ClampMin="0"))
	int32 MaximumRemoteWingmanActors = 175;
	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Presentation",
		meta=(ClampMin="0", Units="cm"))
	int32 CullDistanceCentimeters = 300000;

	TMap<FGuLiWingmanGroupHandle, FGuLiWingmanPresentationGroupRuntime> Groups;
	TArray<TWeakObjectPtr<AGuLiWingmanPawn>> RemotePawnPool;
};
