#pragma once

#include "CoreMinimal.h"
#include "Gameplay/GroundMech/GuLiGroundMassCollisionTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiGroundMassContactSubsystem.generated.h"

class AGuLiCommanderPresentationActor;
class AGuLiSoldierStateReplicator;

struct GULISTRIKE_API FGuLiGroundMassContactStats
{
	uint64 SnapshotRefreshes = 0u;
	double LastSnapshotMilliseconds = 0.0;
	double MaximumSnapshotMilliseconds = 0.0;
	uint64 Queries = 0u;
	uint64 RawQueryCandidates = 0u;
	uint64 SideHits = 0u;
	uint64 SupportContacts = 0u;
};

/** World-local, read-only collision cache shared by every player ground mech. */
UCLASS()
class GULISTRIKE_API UGuLiGroundMassContactSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Caller supplies a swept 2D capsule AABB. Output bodies are extrapolated by at most 100 ms. */
	void QueryMassBodies(
		const FBox2D& SweptBounds,
		float MovementSeconds,
		TArray<FGuLiGroundMassBody>& OutBodies);
	bool FindMassBody(FGuLiSoldierId SoldierId, FGuLiGroundMassBody& OutBody);
	void RecordSideHits(int32 Count);
	void RecordSupportContact();
	const FGuLiGroundMassContactStats& GetStats() const { return Stats; }
	int32 GetBodyCount() const { return SpatialIndex.Num(); }

#if WITH_DEV_AUTOMATION_TESTS
	void TestOnly_SetBodies(TConstArrayView<FGuLiGroundMassBody> Bodies);
#endif

private:
	void RefreshSnapshot();
	void BuildClientSnapshot(TArray<FGuLiGroundMassBody>& OutBodies);
	float GetSnapshotAgeSeconds() const;

	FGuLiGroundMassSpatialIndex SpatialIndex;
	TMap<uint32, FVector> PreviousClientLocations;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> PresentationActor;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> StateReplicator;
	double LastSnapshotWorldSeconds = -1.0;
	double LastClientSampleWorldSeconds = -1.0;
	double NextRefreshWorldSeconds = 0.0;
	uint32 ClientMatchEpoch = 0u;
	FGuLiGroundMassContactStats Stats;
};
