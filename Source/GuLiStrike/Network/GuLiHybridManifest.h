#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiHybridWorldSubsystem.h"

struct GULISTRIKE_API FGuLiHybridStartupEntry
{
    FGuLiHybridObjectKey Key;
    EGuLiTeam Team = EGuLiTeam::Unassigned;
    FString Definition;
    uint32 Ordinal = 0;
    float FacingYawDegrees = 0;
    FVector Location = FVector::ZeroVector;
    float Health = 0, Shield = 0;
    double EstimatedCost = 1, ShieldRadius = 0;
    bool bSupplied = false;
    bool bCompleted = true;
};
enum class EGuLiHybridRuleKind : uint8 { Damage, Credit, Reserve, Commit, Refund, Spawn, Complete, Destroy, Relation, Extract, Deliver, Process, ClaimMiningPosition, ReleaseMiningPosition, ClaimMiningNode, Upgrade=16, Capability };
enum class EGuLiHybridRelationKind : uint8 { None, Supply, Phase, Parent };
struct GULISTRIKE_API FGuLiHybridRuleEntry
{
    FString Id, SourceDefinition;
    EGuLiHybridRuleKind Kind = EGuLiHybridRuleKind::Damage;
    float Damage = 0;
    int32 Blue = 0, Red = 0;
    uint64 CooldownTicks = 0, EffectTicks = 600;
    uint32 MaximumTargets = 1;
    FString TargetDefinition;
    EGuLiHybridObjectKind SpawnKind=EGuLiHybridObjectKind::None;
    float SpawnHealth=0, SpawnShield=0;
    double SpawnCost=1, SpawnShieldRadius=0;
    uint64 MinimumAgeTicks=0;
    uint32 MaximumLiveChildren=0;
    uint32 MaximumFactionObjects=0;
    uint8 SpawnControllerSeat=3;
    bool bSpawnCompleted=true;
    EGuLiHybridRelationKind Relation=EGuLiHybridRelationKind::None;
    bool bRelationValue=false;
    FString UpgradeCatalog;
    FString RequiredCapabilityCatalog,RequiredCapabilityGroup,RequiredCapability;
    FString Capability,CapabilityOperation;
    uint32 CapabilityVersion=0;
};
struct GULISTRIKE_API FGuLiHybridManifestData
{
    FString MapPackage;
    TArray<FGuLiHybridStartupEntry> Objects;
    TArray<FGuLiHybridRuleEntry> Rules;
    TMap<FString, double> Numbers;
    TMap<FString, FString> Strings;
    int32 RedTeamBlue = 0, RedTeamRed = 0, BlueTeamBlue = 0, BlueTeamRed = 0;
};

/** Runtime owns the protocol conversion; the editor never depends on GuLiNetCore. */
class GULISTRIKE_API FGuLiHybridManifest
{
public:
    static bool Write(const FGuLiHybridManifestData& Data, const FString& File, FString& Error);
    static bool Read(const FString& File, FGuLiHybridManifestData& Data, FString& Error);
    static bool ValidateFile(const FString& File, FString& ConfigHash, FString& MapHash, FString& MapPackage, FString& Error);
    static FString MakeDamageRuleId(const FString& Definition, const FName Skill, const FName Slot);
    static FGuLiHybridObjectKey MakeStableKey(const FString& MapPackage, EGuLiHybridObjectKind Kind, const FString& AuthoredId);
};
