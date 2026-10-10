#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiSceneUISourceRegistry.generated.h"

enum class EGuLiSceneUISourceKind : uint8 { PawnRing, OutpostHalo, Presentation, OutlineActor };
enum class EGuLiSceneUIChange : uint8 { Content, Pose, Membership };

struct FGuLiSceneUISource
{
	TWeakObjectPtr<UObject> Object;
	EGuLiSceneUISourceKind Kind = EGuLiSceneUISourceKind::PawnRing;
	uint64 Generation = 0, Revision = 0;
};

/** World inventory only; selection, view team, materials and projection belong to LocalPlayer. */
UCLASS()
class GULISTRIKE_API UGuLiSceneUISourceRegistry final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void RegisterSource(UObject* Source, EGuLiSceneUISourceKind Kind);
	void UnregisterSource(const UObject* Source);
	void NotifyChanged(UObject* Source, EGuLiSceneUIChange Change = EGuLiSceneUIChange::Content);
	void GetSnapshot(TArray<FGuLiSceneUISource>& Out) const;
	uint64 GetSourceRevision(const UObject* Source) const;
	uint64 GetMembershipRevision() const { return MembershipRevision; }
	uint64 GetContentRevision() const { return ContentRevision; }
	uint64 GetPoseRevision() const { return PoseRevision; }
	uint64 GetDiscoveryIterations() const { return DiscoveryIterations; }
	UFUNCTION(BlueprintPure, Category="Scene UI|Diagnostics") FString GetRegistryStatsJson() const;
	static void Notify(UObject* Source, EGuLiSceneUIChange Change = EGuLiSceneUIChange::Content);
private:
	void ActorSpawned(AActor* Actor);
	void DiscoverActor(AActor* Actor);
	void DiscoverPending();
	UFUNCTION() void ActorEnded(AActor* Actor, EEndPlayReason::Type Reason);
	TArray<FGuLiSceneUISource> Sources;
	TArray<TWeakObjectPtr<AActor>> Pending;
	FDelegateHandle SpawnHandle;
	FTimerHandle PendingTimer;
	uint64 NextGeneration = 1, MembershipRevision = 1, ContentRevision = 1, PoseRevision = 1;
	uint64 DiscoveryIterations = 0;
};
