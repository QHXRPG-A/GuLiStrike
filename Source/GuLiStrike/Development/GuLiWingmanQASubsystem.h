// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "CoreMinimal.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiWingmanQASubsystem.generated.h"

/**
 * Non-Shipping runtime observer and command endpoint for Wingman QA.
 * It never spawns units, submits Relay movement, or writes transforms. The formal Main role may
 * submit one server-only production-ledger damage transaction to prove death and replenishment.
 */
UCLASS()
class GULISTRIKE_API UGuLiWingmanQASubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	bool StartSession(const FGuLiWingmanQARunDescriptor& Descriptor,
		double InDurationSeconds, FString& OutError);
	bool StopSession(bool bScenarioPassed, FGuLiWingmanAcceptanceRunEvidence& OutEvidence,
		FString& OutError);
	bool IsSessionActive() const { return EvidenceWriter.IsActive(); }

	void LogStats() const;
	void LogUnit(const FString& WingmanId) const;
	void LogGroup(const FString& SimulationGroupId) const;
	void LogNavigation(const FString& FlightId) const;
	void LogRelayStats() const;
	void LogLeaseWatchdog() const;

	static bool SetDebugDrawMode(int32 Mode);
	static int32 GetDebugDrawMode();

private:
	void TryStartFromCommandLine();
	void EmitSample(bool bFinalSample);
	void AdvanceMainCombatFixture(bool bCoreReady, TArray<FGuLiWingmanQAEvent>& OutEvents);
	void AdvanceS8Fixture(bool bCoreReady, TArray<FGuLiWingmanQAEvent>& OutEvents);
	void DrawDiagnostics() const;

	FGuLiWingmanQAEvidenceWriter EvidenceWriter;
	double StartedWallSeconds = 0.0;
	double NextSampleWallSeconds = 0.0;
	double DurationSeconds = 0.0;
	uint32 SampleIndex = 0u;
	uint32 PreviousMinimumAcceptedFrame = 0u;
	uint32 CommanderFixedStepAtStart = 0u;
	uint64 CommanderDroppedStepAtStart = 0u;
	uint64 NetworkMetricSampleCount = 0u;
	uint64 NetworkIncomingBytesPerSecondSum = 0u;
	uint64 NetworkOutgoingBytesPerSecondSum = 0u;
	uint64 InitialProcessPhysicalBytes = 0u;
	FGuLiWingmanHandle CombatFixtureWingman;
	FGuLiTargetHandle CombatFixtureSource;
	FGuLiTargetHandle CombatFixtureTarget;
	FGuid CombatFixtureDamageEventId;
	FGuid CombatFixtureShotId;
	FGuid CombatFixtureDeathEventId;
	FGuid CombatFixtureRewardEventId;
	double CombatFixtureDeathServerSeconds = 0.0;
	double CombatFixtureReplenishDueServerSeconds = 0.0;
	float CombatFixtureHealthBefore = 0.0f;
	float CombatFixtureHealthAfter = 0.0f;
	uint64 CombatFixtureCommitOrdinal = 0u;
	uint64 CombatFixtureReplenishScheduleId = 0u;
	int32 ExpectedClientEndpoints = 0;
	FName ActiveRole;
	TSet<FName> ObservedRoleProbeGates;
	FString RoleProbeError;
	FGuLiWingmanHandle S8FixtureWingman;
	FGuLiTargetHandle S8FixtureSource;
	FGuLiTargetHandle S8FixtureTarget;
	FGuid S8NonLethalDamageEventId;
	FGuid S8LethalDamageEventId;
	FGuLiWingmanHandle S8ObserverWoundedWingman;
	FGuLiWingmanGroupHandle S8TransferGroup;
	FGuid S8TransferCandidateGuid;
	uint32 S8TransferOfferRevision = 0u;
	uint64 S8ObserverFirstCutId = 0u;
	uint64 S8TransferPreviewCutId = 0u;
	double S8ObserverFirstBootstrapElapsedSeconds = -1.0;
	double S8ObserverDetectedServerSeconds = 0.0;
	float S8FixtureRemainingHealth = 0.0f;
	bool bS8FixtureAttempted = false;
	bool bS8NonLethalCommitted = false;
	bool bS8NonLethalPublicCutReady = false;
	bool bS8ObserverDetectedInWindow = false;
	bool bS8LethalCommitted = false;
	bool bS8LethalPublicCutReady = false;
	bool bS8TransferOfferStarted = false;
	bool bS8TransferOfferDelivered = false;
	bool bS8TransferRecoveredActive = false;
	bool bS8ObserverInitialCutAllFull = false;
	bool bS8ObserverInitialCutHadWoundedAlive = false;
	bool bS8ObserverAtomicBootstrapObserved = false;
	bool bS8ObserverNoAuthorityObserved = false;
	bool bS8ObserverDeathCutObserved = false;
	bool bCombatFixtureAttempted = false;
	bool bObservedCombatLedgerSingleCommit = false;
	bool bObservedDeathRosterCommit = false;
	bool bObservedReplenishmentSchedule = false;
	bool bObservedReplenishAfter15Seconds = false;
	bool bRoleProbeAttempted = false;
	bool bCommandLineStartPending = false;
	bool bAutoExit = false;
	bool bOwnsTrace = false;
	bool bFinalized = false;
};
