// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiArmySkillSubsystem.generated.h"

class AGuLiBattlePlayerState;
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiArmyWeaponLoadoutCommitted, uint32);

/** Per-world read model of committed army capabilities; mutations use the current owner. */
UCLASS()
class GULISTRIKE_API UGuLiArmySkillSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;


	const FGuLiResolvedSkillProfile* FindResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId,
		FName SlotId = FName(TEXT("BasicAttack"))) const;
	const TArray<FGuLiResolvedSkillProfile>& GetResolvedSkills() const;
	TSharedPtr<const TArray<FGuLiResolvedSkillProfile>> GetSharedUnitProfiles(EGuLiTeam Team, uint16 UnitTypeId) const;
	uint32 GetLoadoutRevision() const;
	FGuLiArmyWeaponLoadoutCommitted& OnWeaponLoadoutCommitted() { return WeaponLoadoutCommitted; }
	bool EquipWeapon(const AGuLiBattlePlayerState& Commander, uint16 UnitTypeId, FName SlotId,
		FName SkillId, uint32 ExpectedRevision, FString& OutError);


	FString ExplainResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId = FName(TEXT("BasicAttack"))) const;

	bool IsExecutorRegistered(FName ExecutorId) const { return RegisteredExecutors.Contains(ExecutorId); }
	bool RegisterExecutor(FName ExecutorId);
	bool RegisterEffectHook(FName HookId, FGuLiSkillEffectHook Hook);
	void UnregisterEffectHook(FName HookId);
	void ExecuteEffectHook(FName HookId, const FGuLiSkillEffectContext& Context) const;
private:
    friend class FGuLiHybridArmyBridge;

	void RebuildLookup();

	bool IsCurrentMatchState() const;
	static uint64 ProfileKey(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId);
	UPROPERTY(Transient) TArray<FGuLiResolvedSkillProfile> CommittedProfiles;
	TMap<uint64, TArray<int32>> ProfileLookup;
	TMap<uint32, TSharedPtr<const TArray<FGuLiResolvedSkillProfile>>> SharedUnitProfiles;
	TSet<FName> RegisteredExecutors;
	TMap<FName, FGuLiSkillEffectHook> EffectHooks;
    TMap<EGuLiTeam,uint32> HybridTeamRevisions;
	uint32 CommittedMatchEpoch = 0;
	FGuLiArmyWeaponLoadoutCommitted WeaponLoadoutCommitted;
};
