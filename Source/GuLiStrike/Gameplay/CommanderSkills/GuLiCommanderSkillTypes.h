#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "GuLiCommanderSkillTypes.generated.h"

class AGuLiBattlePlayerState;
class UGuLiCommanderSkillExecutor;
class UGuLiProjectileEffectDefinition;
class UGuLiGroundWarningStyle;

UENUM(BlueprintType)
enum class EGuLiActiveSkillScope : uint8 { Unit, Global };
UENUM(BlueprintType)
enum class EGuLiActiveSkillTargetMode : uint8 { Self, GroundPoint, TwoPoint };
UENUM(BlueprintType)
enum class EGuLiActiveSkillCommand : uint8 { Activate, Continue, Cancel };
UENUM(BlueprintType)
enum class EGuLiActiveSkillResultCode : uint8
{
	Succeeded, NoSkill, Ineligible, Cooldown, OutOfRange, InvalidGround, InvalidRequest, StaleSelection, ExecutionFailed
};

USTRUCT(BlueprintType)
struct FGuLiActiveSkillRuntime
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName SkillId;
	/** Authority clock; soldiers use the existing fixed-step simulation clock. */
	UPROPERTY(BlueprintReadOnly) double ReadyAt = 0;
	UPROPERTY(BlueprintReadOnly) int32 Level = 1;
	UPROPERTY(BlueprintReadOnly) FGuid ActiveCastId;
	/** Advances only after successful unit casts, for deterministic muzzle alternation. */
	UPROPERTY() uint32 SuccessfulCasts = 0;
};

USTRUCT(BlueprintType)
struct FGuLiUnitSkillRuntimeView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuLiSoldierId SoldierId;
	UPROPERTY(BlueprintReadOnly) FGuLiActiveSkillRuntime Runtime;
};

USTRUCT(BlueprintType)
struct FGuLiActiveSkillDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SkillId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EGuLiActiveSkillScope Scope = EGuLiActiveSkillScope::Unit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EGuLiActiveSkillTargetMode TargetMode = EGuLiActiveSkillTargetMode::Self;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) float CooldownSeconds = 0;
	/** Zero means unlimited only for global tactics; point-targeted unit skills require positive range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) float RangeCentimeters = 0;
	/** Optional authority-resolved weapon range. Empty retains the fixed range above. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RangeSourceSlot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.001")) float RangeMultiplier = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 MaximumLevel = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UGuLiCommanderSkillExecutor> ExecutorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UDataAsset> Configuration;
};

/** Server-resolved caster and configuration. These values never come from an activation RPC. */
USTRUCT(BlueprintType)
struct FGuLiActiveSkillExecutionContext
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) TObjectPtr<AGuLiBattlePlayerState> Commander;
	UPROPERTY(BlueprintReadOnly) FGuLiSoldierId SoldierId;
	UPROPERTY(BlueprintReadOnly) FGuLiTargetHandle Source;
	UPROPERTY(BlueprintReadOnly) FName SkillId;
	UPROPERTY(BlueprintReadOnly) FTransform SourceTransform;
	UPROPERTY(BlueprintReadOnly) FVector GroundPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY(BlueprintReadOnly) FGuid CastId;
	UPROPERTY(BlueprintReadOnly) int32 Level = 1;
	UPROPERTY(BlueprintReadOnly) int32 UnitTypeId = 0;
	UPROPERTY() uint32 ShotOrdinal = 0;
	UPROPERTY(BlueprintReadOnly) EGuLiActiveSkillScope Scope = EGuLiActiveSkillScope::Unit;
	UPROPERTY(BlueprintReadOnly) EGuLiActiveSkillCommand Command = EGuLiActiveSkillCommand::Activate;
};

USTRUCT(BlueprintType)
struct FGuLiActiveSkillExecutionResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bSucceeded = false;
	UPROPERTY(BlueprintReadOnly) FGuid CastId;
	UPROPERTY(BlueprintReadOnly) FGuid EffectId;
	UPROPERTY(BlueprintReadOnly) FString Error;
};

USTRUCT(BlueprintType)
struct FGuLiActiveSkillUnitResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuLiSoldierId SoldierId;
	UPROPERTY(BlueprintReadOnly) FName SkillId;
	UPROPERTY(BlueprintReadOnly) EGuLiActiveSkillResultCode Code = EGuLiActiveSkillResultCode::NoSkill;
	UPROPERTY(BlueprintReadOnly) double ReadyAtServerSeconds = 0;
	UPROPERTY(BlueprintReadOnly) FGuLiActiveSkillExecutionResult Execution;
};

USTRUCT()
struct FGuLiActiveSkillRequest
{
	GENERATED_BODY()
	UPROPERTY() FGuid RequestId;
	UPROPERTY() uint32 MatchEpoch = 0;
	UPROPERTY() uint32 SelectionRevision = 0;
	/** Empty selects each confirmed soldier's own UnitType skill. */
	UPROPERTY() FName GlobalSkillId;
	UPROPERTY() EGuLiActiveSkillCommand Command = EGuLiActiveSkillCommand::Activate;
	UPROPERTY() FGuid CastId;
	UPROPERTY() bool bHasGroundPoint = false;
	UPROPERTY() FVector_NetQuantize GroundPoint = FVector::ZeroVector;
	bool HasSameContent(const FGuLiActiveSkillRequest& Other) const;
};

USTRUCT(BlueprintType)
struct FGuLiActiveSkillReply
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY() uint32 MatchEpoch = 0;
	UPROPERTY(BlueprintReadOnly) EGuLiActiveSkillResultCode Code = EGuLiActiveSkillResultCode::InvalidRequest;
	UPROPERTY(BlueprintReadOnly) FString Error;
	UPROPERTY(BlueprintReadOnly) TArray<FGuLiActiveSkillUnitResult> Units;
	UPROPERTY(BlueprintReadOnly) FGuLiActiveSkillExecutionResult Global;
};

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiCommanderSkillCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiActiveSkillDefinition> Skills;
	/** A map makes the one-active-skill-per-unit contract structural. Empty means no skill. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<int32, FName> UnitSkills;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> GlobalSkills;
	bool Validate(FString& Error) const;
	const FGuLiActiveSkillDefinition* FindSkill(FName Id) const;
	const FGuLiActiveSkillDefinition* FindUnitSkill(uint16 UnitTypeId) const;
};

UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiCommanderSkillSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Commander Skills") TSoftObjectPtr<UGuLiCommanderSkillCatalog> Catalog;
};

/** Stateless executor; runtime and cooldowns belong to the caster record. */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiCommanderSkillExecutor : public UObject
{
	GENERATED_BODY()
public:
	virtual bool ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const;
	virtual FGuLiActiveSkillExecutionResult Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const;
};

