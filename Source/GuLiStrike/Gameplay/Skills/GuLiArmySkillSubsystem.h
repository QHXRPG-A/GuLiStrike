// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiArmySkillSubsystem.generated.h"

class AGuLiBattlePlayerState;
class AGuLiArmySkillReplicationActor;

/** Per-world/team source ledger. Survives commander replacement, never a new World. */
UCLASS()
class GULISTRIKE_API UGuLiArmySkillSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	/** Called exactly at the authority simulation step boundary; no-op without pending changes. */
	void CommitPendingChanges();
	const FGuLiResolvedSkillProfile* FindResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId,
		FName SlotId = FName(TEXT("BasicAttack"))) const;
	const TArray<FGuLiResolvedSkillProfile>& GetResolvedSkills() const;
	/** Authority-only details, returned by value so callers cannot mutate the ledger. Includes accepted pending changes. */
	TArray<FGuLiSkillSource> GetSources(EGuLiTeam Team) const;
	bool UpsertSource(const AGuLiBattlePlayerState& Commander, const FGuLiSkillSource& Source, FString& OutError);
	bool RemoveSource(const AGuLiBattlePlayerState& Commander, FGuid SourceInstanceId, FString& OutError);
	bool SetNumericOverride(const AGuLiBattlePlayerState& Commander, const FGuLiSkillNumericOverride& Override, FString& OutError);
	bool ClearNumericOverride(const AGuLiBattlePlayerState& Commander, uint16 UnitTypeId, FName SlotId, FString& OutError);
	bool ClearAll(const AGuLiBattlePlayerState& Commander, FString& OutError);
	bool ExecuteCommand(const AGuLiBattlePlayerState& Commander, const FGuLiArmySkillCommand& Command, FString& OutError);
	FString ExplainResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId = FName(TEXT("BasicAttack"))) const;
	int32 GetLastResolvedKeyCount() const { return LastResolvedKeyCount; }
	/** Client copy only. Server accepts no profile or team values from remote callers. */
	void ReceiveReplicatedProfiles(uint32 MatchEpoch, const TArray<FGuLiResolvedSkillProfile>& Profiles);
	bool IsExecutorRegistered(FName ExecutorId) const { return RegisteredExecutors.Contains(ExecutorId); }
	bool RegisterExecutor(FName ExecutorId);
	bool RegisterEffectHook(FName HookId, FGuLiSkillEffectHook Hook);
	void UnregisterEffectHook(FName HookId);
	void ExecuteEffectHook(FName HookId, const FGuLiSkillEffectContext& Context) const;
private:
	bool ValidateCommander(const AGuLiBattlePlayerState& Commander, FString& OutError) const;
	bool StageTeam(EGuLiTeam Team, const TArray<FGuLiSkillSource>& Sources,
		const TArray<FGuLiSkillNumericOverride>& Overrides, FString& OutError,
		const TArray<FGuLiSkillSlotKey>* AffectedSlots = nullptr);
	void RebuildLookup();
	void SynchronizeMatchEpoch();
	bool IsCurrentMatchSnapshot() const;
	static uint64 ProfileKey(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId);
	UPROPERTY(Transient) TArray<FGuLiResolvedSkillProfile> CommittedProfiles;
	UPROPERTY(Transient) TArray<FGuLiResolvedSkillProfile> PendingProfiles;
	UPROPERTY(Transient) TArray<FGuLiSkillDefinition> Definitions;
	UPROPERTY(Transient) TArray<FGuLiUnitSkillConfig> Configs;
	TMap<EGuLiTeam, TArray<FGuLiSkillSource>> TeamSources;
	TMap<EGuLiTeam, TArray<FGuLiSkillNumericOverride>> TeamOverrides;
	TMap<EGuLiTeam, FString> LastRejectedChanges;
	TMap<uint64, TArray<int32>> ProfileLookup;
	TSet<FName> RegisteredExecutors;
	TMap<FName, FGuLiSkillEffectHook> EffectHooks;
	TWeakObjectPtr<AGuLiArmySkillReplicationActor> ReplicationActor;
	uint32 NextRevision = 1;
	uint32 CachedMatchEpoch = 0;
	uint32 CommittedMatchEpoch = 0;
	uint32 LastPublishedEpoch = 0;
	bool bPendingChanges = false;
	int32 LastResolvedKeyCount = 0;
};
