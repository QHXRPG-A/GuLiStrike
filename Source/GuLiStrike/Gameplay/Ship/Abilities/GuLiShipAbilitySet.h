// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityDefinitions.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "Templates/SubclassOf.h"
#include "GuLiShipAbilitySet.generated.h"

class UGuLiShipGameplayAbility;

/** One catalog mapping from stable AbilityId to a GA class and immutable group definition. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiShipAbilityGrant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGameplayTag AbilityId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	EGuLiShipAbilitySlot Slot = EGuLiShipAbilitySlot::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	TSubclassOf<UGuLiShipGameplayAbility> AbilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities", meta = (ClampMin = "1"))
	int32 AbilityLevel = 1;

	/** Empty for persistent abilities; required for input-triggered abilities. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	TObjectPtr<UGuLiWingmanFormationDefinition> FormationDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	TObjectPtr<UGuLiWingmanWeaponDefinition> WeaponDefinition;

	bool IsWellFormed(FString* OutError = nullptr) const;
	uint32 GetDefinitionRevision() const;
	uint64 GetDefinitionChecksum() const;
};

/** Data-driven grant catalog. A PlayerState loadout selects exactly one entry per v1 slot. */
UCLASS(BlueprintType, Const)
class GULISTRIKE_API UGuLiShipAbilitySet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Ship|Abilities", meta = (ClampMin = "1"))
	uint32 Revision = 1u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	TArray<FGuLiShipAbilityGrant> Grants;

	const FGuLiShipAbilityGrant* FindGrant(FGameplayTag AbilityId) const;
	bool IsWellFormed(FString* OutError = nullptr) const;
	bool ResolveLoadout(
		const FGuLiShipAbilityLoadoutState& Loadout,
		TArray<FGuLiShipAbilityGrant>& OutOrderedGrants,
		FString* OutError = nullptr) const;
	uint64 ComputeLoadoutChecksum(const FGuLiShipAbilityLoadoutState& Loadout) const;

	/** Native transient fallback for tests/bootstrap; production may point Ship defaults at an authored asset. */
	static UGuLiShipAbilitySet* CreateNativeV1Transient(UObject* Outer);
};
