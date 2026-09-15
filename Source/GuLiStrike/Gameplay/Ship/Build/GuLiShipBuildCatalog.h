#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GuLiShipBuildCatalog.generated.h"

class UGuLiShipCapabilityComponent;
class UGuLiStrikeShipPartComponent;

/** Operators describe rules, never enumerate the project's component catalogue. */
UENUM(BlueprintType)
enum class EGuLiShipRuleOp : uint8
{
	All, Any, AtLeast, ChosenNode, RouteLevel, InstalledCapability
};

/** Flat expression storage. Child indices refer only to this expression's Nodes. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipRuleNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EGuLiShipRuleOp Op = EGuLiShipRuleOp::All;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<int32> Children;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SubjectId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 Required = 1;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipRuleExpression
{
	GENERATED_BODY()
	/** An empty expression with Root=INDEX_NONE is unconditionally true. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiShipRuleNode> Nodes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Root = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipCapabilityDefinition
{
	GENERATED_BODY()
	/** Unique within its group; the same identifier is legal in another group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName CapabilityId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FGameplayTagContainer ProvidedTags;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UGuLiShipCapabilityComponent> ComponentClass;
	/** Type-specific immutable configuration, validated by the component class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UDataAsset> Configuration;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FGuLiShipRuleExpression RuntimeRequirements;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipGroupMount
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SocketName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftClassPtr<UGuLiStrikeShipPartComponent> PartClass;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipPartGroupDefinition
{
	GENERATED_BODY()
	/** Configuration ID changes per tier; GroupId remains stable across upgrades. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ConfigurationId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName GroupId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiShipGroupMount> Mounts;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiShipCapabilityDefinition> Capabilities;
	/** Draft content participates in rule validation, but cannot enter the reward pool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bExecutable = false;
	/** Unresolved draft mount/layout decisions; never interpreted as runtime rules. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText AuthoringNotes;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipUpgradeNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName NodeId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RouteId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 Level = 1;
	/** Explicit predecessor replaced in the current assembly, retained in history. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ReplacesNodeId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName GroupConfigurationId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FGuLiShipRuleExpression AcquisitionRequirements;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipExclusionRule
{
	GENERATED_BODY()
	/** At most one distinct route in this set may be chosen in a match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RuleId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> Routes;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipRunBuildState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int64 MatchEpoch = 0;
	UPROPERTY(BlueprintReadOnly) int64 BuildRevision = 0;
	/** Append-only on authority. Route locks and current tiers are derived from this. */
	UPROPERTY(BlueprintReadOnly) TArray<FName> ChosenNodeIds;
};

struct GULISTRIKE_API FGuLiShipRuleContext
{
	TSet<FName> ChosenNodes;
	TMap<FName, int32> RouteLevels;
	TSet<FName> InstalledCapabilities;
};

struct GULISTRIKE_API FGuLiShipResolvedBuild
{
	TArray<FName> ActiveNodeIds;
	TArray<FName> GroupConfigurationIds;
	FGuLiShipRuleContext Context;
};

/** Validated cache is built once per catalogue revision, not in a component Tick. */
struct GULISTRIKE_API FGuLiShipCompiledBuildRules
{
	TMap<FName, int32> NodeIndices;
	TMap<FName, int32> GroupIndices;
	TMap<FName, TSet<FName>> ConflictsByRoute;
	TMap<FName, TArray<FName>> Dependencies;
	TMap<FName, TArray<FName>> ReverseDependencies;
	TArray<FName> TopologicalOrder;
	TMap<FName, TArray<FName>> RuntimeDependencies;
	TMap<FName, TArray<FName>> RuntimeReverseDependencies;
	TArray<FName> RuntimeTopologicalOrder;
	bool bValid = false;
	FString Error;
};

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiShipBuildCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 Revision = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiShipUpgradeNode> Upgrades;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiShipPartGroupDefinition> Groups;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiShipExclusionRule> Exclusions;

	bool Compile(FGuLiShipCompiledBuildRules& Out) const;
	bool Resolve(const FGuLiShipCompiledBuildRules& Rules, const FGuLiShipRunBuildState& State,
		FGuLiShipResolvedBuild& Out, FString& Error) const;
	bool CanChoose(const FGuLiShipCompiledBuildRules& Rules, const FGuLiShipRunBuildState& State,
		FName NodeId, FString& Error, bool bRequireExecutable = true) const;
	static bool Evaluate(const FGuLiShipRuleExpression& Expression,
		const FGuLiShipRuleContext& Context, FString* Failure = nullptr);
	const FGuLiShipPartGroupDefinition* FindGroup(FName ConfigurationId) const;
	const FGuLiShipUpgradeNode* FindUpgrade(FName NodeId) const;
};
