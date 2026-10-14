#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Skills/GuLiHybridArmy.h"

bool UGuLiArmySkillSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiArmySkillSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    RegisteredExecutors.Add(TEXT("DirectSingleTarget"));
    RegisteredExecutors.Add(TEXT("GroundMachineGun"));
    RegisteredExecutors.Add(TEXT("LaunchProjectile"));
}


void UGuLiArmySkillSubsystem::Deinitialize()
{
    CommittedProfiles.Reset(); SharedUnitProfiles.Reset(); HybridTeamRevisions.Reset();
    WeaponLoadoutCommitted.Clear(); ProfileLookup.Reset(); EffectHooks.Reset(); RegisteredExecutors.Reset();
    Super::Deinitialize();
}


uint64 UGuLiArmySkillSubsystem::ProfileKey(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId)
{
	return (static_cast<uint64>(static_cast<uint8>(Team)) << 48) | (static_cast<uint64>(UnitTypeId) << 32) | GetTypeHash(SlotId);
}

void UGuLiArmySkillSubsystem::RebuildLookup()
{
	ProfileLookup.Reset();
	SharedUnitProfiles.Reset();
	TMap<uint32, TArray<FGuLiResolvedSkillProfile>> UnitProfiles;
	for (int32 Index = 0; Index < CommittedProfiles.Num(); ++Index)
	{
		const auto& Profile = CommittedProfiles[Index];
		ProfileLookup.FindOrAdd(ProfileKey(Profile.Team, Profile.UnitTypeId, Profile.SlotId)).Add(Index);
		UnitProfiles.FindOrAdd((static_cast<uint32>(Profile.Team) << 16) | Profile.UnitTypeId).Add(Profile);
	}
	for (auto& Pair : UnitProfiles)
	{
		Pair.Value.Sort([](const auto& A, const auto& B) { return A.SlotId.LexicalLess(B.SlotId); });
		SharedUnitProfiles.Add(Pair.Key, MakeShared<const TArray<FGuLiResolvedSkillProfile>>(MoveTemp(Pair.Value)));
	}
}

TSharedPtr<const TArray<FGuLiResolvedSkillProfile>> UGuLiArmySkillSubsystem::GetSharedUnitProfiles(
	const EGuLiTeam Team, const uint16 UnitTypeId) const
{
	if (!IsCurrentMatchState()) return nullptr;
	const auto* Profiles = SharedUnitProfiles.Find((static_cast<uint32>(Team) << 16) | UnitTypeId);
	return Profiles ? *Profiles : nullptr;
}

const FGuLiResolvedSkillProfile* UGuLiArmySkillSubsystem::FindResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId) const
{
	if (!IsCurrentMatchState()) return nullptr;
	if (const auto* Bucket = ProfileLookup.Find(ProfileKey(Team, UnitTypeId, SlotId)))
	{
		for (int32 Index : *Bucket)
		{
			const auto& Profile = CommittedProfiles[Index];
			if (Profile.Team == Team && Profile.UnitTypeId == UnitTypeId && Profile.SlotId == SlotId) return &Profile;
		}
	}
	return nullptr;
}

bool UGuLiArmySkillSubsystem::IsCurrentMatchState() const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	return State && State->GetMatchEpoch() != 0 && CommittedMatchEpoch == State->GetMatchEpoch();
}

const TArray<FGuLiResolvedSkillProfile>& UGuLiArmySkillSubsystem::GetResolvedSkills() const
{
	static const TArray<FGuLiResolvedSkillProfile> Empty;
	return IsCurrentMatchState() ? CommittedProfiles : Empty;
}

bool UGuLiArmySkillSubsystem::EquipWeapon(const AGuLiBattlePlayerState& Commander, const uint16 UnitTypeId,
    const FName SlotId, const FName SkillId, const uint32 ExpectedRevision, FString& OutError)
{
    return GuLiHybridArmy::Equip(Commander,UnitTypeId,SlotId,SkillId,ExpectedRevision,OutError);
}


uint32 UGuLiArmySkillSubsystem::GetLoadoutRevision() const
{
    if (!IsCurrentMatchState()) return 0;
    const auto* PC=GetWorld()->GetFirstPlayerController();
    const auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    return PS?HybridTeamRevisions.FindRef(PS->GetTeam()):0;
}


FString UGuLiArmySkillSubsystem::ExplainResolvedSkill(EGuLiTeam Team, uint16 UnitTypeId, FName SlotId) const
{
    const auto* Profile=FindResolvedSkill(Team,UnitTypeId,SlotId);
    if (!Profile) return TEXT("The current team's committed army capability is not available yet.");
    return FString::Printf(TEXT("Committed army capability: Team=%u Unit=%u Slot=%s Skill=%s Executor=%s Damage=%.6g Rate=%.6g/s Range=%.6gcm Revision=%u"),
        static_cast<uint8>(Team),UnitTypeId,*SlotId.ToString(),*Profile->SkillId.ToString(),*Profile->ExecutorId.ToString(),
        Profile->Damage,Profile->AttackRatePerSecond,Profile->RangeCentimeters,Profile->Revision);
}


bool UGuLiArmySkillSubsystem::RegisterExecutor(FName ExecutorId)
{
	if (ExecutorId.IsNone() || RegisteredExecutors.Contains(ExecutorId)) return false;
	RegisteredExecutors.Add(ExecutorId); return true;
}

bool UGuLiArmySkillSubsystem::RegisterEffectHook(FName HookId, FGuLiSkillEffectHook Hook)
{
	if (HookId.IsNone() || !Hook || EffectHooks.Contains(HookId)) return false;
	EffectHooks.Add(HookId, MoveTemp(Hook)); return true;
}

void UGuLiArmySkillSubsystem::UnregisterEffectHook(FName HookId) { EffectHooks.Remove(HookId); }

void UGuLiArmySkillSubsystem::ExecuteEffectHook(FName HookId, const FGuLiSkillEffectContext& Context) const
{
	if (!GetWorld()) return;
	if (const auto* Hook = EffectHooks.Find(HookId))
	{
		// A hook may unregister itself while executing.
		const FGuLiSkillEffectHook Callback = *Hook;
		Callback(Context);
	}
}
