#pragma once
#include "CoreMinimal.h"
#include "Components/StateTreeComponentSchema.h"
#include "MassStateTreeSchema.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "GuLiCommanderBehaviorSchema.generated.h"

class UStateTree;
struct FGuLiSoldierDefinition;

/** Values are the established ordered-command wire IDs, not a new network enum. */
UENUM()
enum class EGuLiCommanderBehavior : uint8 { None = 0, Mining = 1, Construction = 2, StrongholdAdvance = 3 };

USTRUCT()
struct FGuLiCommanderBehaviorPolicy
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Commander") EGuLiCommanderBehavior Behavior = EGuLiCommanderBehavior::None;
	UPROPERTY(EditAnywhere, Category="Commander") FString DisplayName;
	UPROPERTY(EditAnywhere, Category="Commander") bool bAutoActivate = true;
	UPROPERTY(EditAnywhere, Category="Commander") EGuLiTaskLifetime Lifetime = EGuLiTaskLifetime::Persistent;
	int32 GetWireId() const { return static_cast<int32>(Behavior); }
};

UCLASS(EditInlineNew, meta=(DisplayName="Commander Actor Behavior"))
class UGuLiCommanderActorStateTreeSchema final : public UStateTreeComponentSchema
{
	GENERATED_BODY()
public:
	UGuLiCommanderActorStateTreeSchema();
	UPROPERTY(EditAnywhere, Category="Commander") FGuLiCommanderBehaviorPolicy Policy;
};

UCLASS(EditInlineNew, meta=(DisplayName="Commander Mass Behavior"))
class UGuLiCommanderMassStateTreeSchema final : public UMassStateTreeSchema
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Commander") FGuLiCommanderBehaviorPolicy Policy;
};

namespace GuLiCommanderBehavior
{
	const FGuLiCommanderBehaviorPolicy* GetPolicy(const UStateTree* Tree);
	const FGuLiCommanderBehaviorPolicy* FindPolicy(const UWorld& World, uint16 UnitTypeId);
	const FGuLiCommanderBehaviorPolicy* FindPolicyByWireId(const UWorld& World, int32 WireId);
	bool ValidateDefinition(const FGuLiSoldierDefinition& Unit, FString& Error);
}
