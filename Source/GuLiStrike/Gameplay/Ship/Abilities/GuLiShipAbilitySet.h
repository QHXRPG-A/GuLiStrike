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

	/** Stable equipment binding within WingmanTypeId; independent from GAS slot/category. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FName WeaponSlotId;

	/** Stable player-facing weapon identity. Empty derives from AbilityId for legacy assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FName SkillId;

	UPROPERTY(EditAnywhere, Category = "Ship|Abilities|Weapon", meta = (ClampMin = "1"))
	uint32 ProfileRevision = 1u;

	/** Shared active cooldown identity; empty automatic channels remain per-member/per-slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities|Weapon")
	FName CooldownGroupId;

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
	FName GetEffectiveWeaponSlotId() const;
	FName GetEffectiveSkillId() const;
	FName GetEffectiveCooldownGroupId() const;
	uint32 GetDefinitionRevision() const;
	uint64 GetDefinitionChecksum() const;
};

/** Data-driven grant catalog. A loadout selects one formation and up to eight unique weapon bindings. */
UCLASS(BlueprintType, Const)
class GULISTRIKE_API UGuLiShipAbilitySet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Type identity used by roster and every weapon BindingKey; not a Pawn/asset path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Abilities")
	FName WingmanTypeId = TEXT("DefaultWingman");

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

	/** Native v2 catalog: SwarmOrbit is selectable while DoubleRing remains a rollback entry. */
	static UGuLiShipAbilitySet* CreateNativeV2Transient(UObject* Outer);
	static UGuLiShipAbilitySet* CreateNativeV3Transient(UObject* Outer);
};
