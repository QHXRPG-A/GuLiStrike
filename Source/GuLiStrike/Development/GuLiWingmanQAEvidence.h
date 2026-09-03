// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Development/GuLiWingmanAcceptanceCatalog.h"

/** The seven stable log/evidence streams frozen by the Wingman development contract. */
enum class EGuLiWingmanQALogStream : uint8
{
	FlightNav = 0,
	Wingman,
	WingmanAI,
	WingmanRelay,
	BattleCombat,
	WingmanNet,
	WingmanQA
};

/** Immutable identity and output policy for one process participating in one formal run. */
struct GULISTRIKE_API FGuLiWingmanQARunDescriptor
{
	FString CampaignId;
	FString RunId;
	FName SuiteRunRole;
	FString PairId;
	FString EndpointId;
	FString AcceptanceProfile;
	FString Map;
	FString NetMode;
	uint32 Seed = 0u;
	bool bServerEndpoint = false;
	/** Exact number of client endpoint verdicts the server must aggregate; zero permits server-only runs. */
	int32 ExpectedClientEndpoints = 0;

	/** Defaults to <Project>/Saved/WingmanQA when empty. */
	FString OutputRoot;
	/** Required for a formal pass; each process must point at its own trace. */
	FString InsightsTracePath;
};

/** One schema-checked event. Details are string encoded so JSONL and CSV share exact values. */
struct GULISTRIKE_API FGuLiWingmanQAEvent
{
	EGuLiWingmanQALogStream Stream = EGuLiWingmanQALogStream::WingmanQA;
	FName Event;
	FName GateId;
	FName InvariantKey;
	int64 InvariantCount = 0;
	double ServerTimeSeconds = 0.0;
	TMap<FName, FString> Fields;
};

/** Canonical allowlists. Unknown event/field names are rejected before evidence is written. */
class GULISTRIKE_API FGuLiWingmanQASchema
{
public:
	static constexpr int32 Version = 1;

	static const TSet<FName>& GetEventNames();
	static const TSet<FName>& GetFieldNames();
	static bool IsKnownEvent(FName EventName);
	static bool IsKnownField(FName FieldName);
	static const TCHAR* StreamName(EGuLiWingmanQALogStream Stream);
};

/** Process-local counters for invariant violations and forbidden execution-domain activity. */
class GULISTRIKE_API FGuLiWingmanQAInvariantRegistry
{
public:
	static void Reset();
	static bool Add(FName InvariantKey, int64 Amount = 1);
	static TMap<FName, int64> Snapshot();
};

/**
 * Non-Shipping, fail-closed structured evidence writer.
 *
 * The writer owns no gameplay state. Callers report observations from production paths; it validates
 * names, appends JSONL/CSV, tracks required gates/invariants, and writes acceptance/assertion files.
 */
class GULISTRIKE_API FGuLiWingmanQAEvidenceWriter
{
public:
	FGuLiWingmanQAEvidenceWriter();
	~FGuLiWingmanQAEvidenceWriter();

	FGuLiWingmanQAEvidenceWriter(const FGuLiWingmanQAEvidenceWriter&) = delete;
	FGuLiWingmanQAEvidenceWriter& operator=(const FGuLiWingmanQAEvidenceWriter&) = delete;

	bool Start(const FGuLiWingmanQARunDescriptor& Descriptor, FString& OutError);
	bool RecordEvent(const FGuLiWingmanQAEvent& Event, FString& OutError);
	bool RecordGate(FName GateId, bool bPassed, int64 SampleCount, FString& OutError);
	bool RecordInvariant(FName InvariantKey, int64 Count, FString& OutError);

	/** Finalizes this process/run. Any missing artifact, gate, or invariant makes the result fail. */
	bool Stop(bool bRunCompleted, bool bScenarioPassed,
		FGuLiWingmanAcceptanceRunEvidence& OutEvidence, FString& OutError);

	bool IsActive() const;
	FString GetRunRoot() const;
	FString GetInsightsTracePath() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
