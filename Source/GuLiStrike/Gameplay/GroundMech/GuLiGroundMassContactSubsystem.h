#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiGroundMassContactSubsystem.generated.h"

class AGuLiSoldierStateReplicator;
class UGuLiCommanderNetSyncComponent;
struct FGuLiSoldierRosterDelta;

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

/** Authoritative/decoded data only. Presentation may read this cache, never the reverse. */
UCLASS()
class GULISTRIKE_API UGuLiGroundMassContactSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
  public:
	virtual bool ShouldCreateSubsystem(UObject *Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	FGuLiGroundMassMoveContext CaptureMove(float Duration);
	void QueryMassBodies(const FGuLiGroundMassMoveContext &Move, const FBox2D &Bounds, double StartSeconds,
						 float Duration, TArray<FGuLiGroundMassBody> &Out);
	void QueryMassBodies(const FBox2D &Bounds, float Duration, TArray<FGuLiGroundMassBody> &Out);
	bool FindMassBody(FGuLiSoldierId Id, FGuLiGroundMassBody &Out);
	uint32 GetCurrentEpoch() const { return SourceEpoch; }
	uint32 GetCacheGeneration() const { return CacheGeneration; }
	void MarkLocalContact(const FGuLiGroundMassBody &Body);
	void ApplyContactPresentation(FGuLiSoldierId Id, FTransform &InOutPose);
	void RecordSideHits(int32 Count);
	void RecordSupportContact();
	const FGuLiGroundMassContactStats &GetStats() const { return Stats; }
	int32 GetBodyCount() const { return CurrentSnapshot && CurrentSnapshot->Index ? CurrentSnapshot->Index->Num() : 0; }
#if WITH_DEV_AUTOMATION_TESTS
	void TestOnly_SetBodies(TConstArrayView<FGuLiGroundMassBody> Bodies);
	void TestOnly_SetRoster(AGuLiSoldierStateReplicator *Roster, uint32 Epoch);
	void TestOnly_ReceivePose(const FGuLiSoldierPoseChunk &Chunk) { HandlePoseChunk(Chunk); }
	void TestOnly_RefreshSnapshot() { RefreshSnapshot(); }
	void TestOnly_EndContactFrame()
	{
		for (auto &Pair : VisualContacts)
			Pair.Value.Frame = GFrameCounter - 1;
	}
#endif
  private:
	void BindSources();
	void ResetSamples();
	void RefreshSnapshot();
	void HandlePoseChunk(const FGuLiSoldierPoseChunk &Chunk);
	void HandleRosterDelta(const FGuLiSoldierRosterDelta &Delta);
	bool FillBody(const FGuLiSoldierStateItem &State, FGuLiGroundMassBody &Body) const;
	double EstimateSimulationNow() const;
	struct FSample
	{
		FGuLiGroundMassBody Body;
		uint32 Sequence = 0u;
		bool bTeleport = false;
	};
	struct FVisualContact
	{
		FTransform Pose;
		uint64 Frame = 0;
		double LastTouched = 0.0;
	};
	TMap<uint32, FSample> Samples;
	TMap<uint32, FVisualContact> VisualContacts;
	TSharedPtr<const FGuLiGroundMassSnapshot> CurrentSnapshot;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> StateReplicator;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> NetSync;
	FDelegateHandle PoseHandle, RosterHandle;
	uint32 SourceEpoch = 0u, ConnectionGeneration = 0u, SyncGeneration = 0u, LatestSequence = 0u;
	uint32 CacheGeneration = 1u;
	double LatestSimulationSample = 0.0, ClockSimulationAnchor = 0.0, ClockLocalAnchor = 0.0;
	double NextRefreshWorldSeconds = 0.0;
	bool bSourceReady = false;
	bool bClockReady = false;
#if WITH_DEV_AUTOMATION_TESTS
	bool bTestSource = false;
#endif
	FGuLiGroundMassContactStats Stats;
};
