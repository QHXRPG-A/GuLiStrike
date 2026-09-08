// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GuLiWeaponChannelTypes.generated.h"

/** Execution domains share equipment identity without sharing an ASC or simulation. */
UENUM(BlueprintType)
enum class EGuLiWeaponDomain : uint8 { Army, Wingman, ShipMounted };

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWeaponBindingKey
{
	GENERATED_BODY()
	UPROPERTY() uint32 MatchEpoch = 0u;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") EGuLiTeam Team = EGuLiTeam::Unassigned;
	/** Empty for the Team-owned army; required for player-owned air equipment. */
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FGuid OwnerPlayerGuid;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") EGuLiWeaponDomain Domain = EGuLiWeaponDomain::Army;
	/** Army: decimal UnitTypeId; air adapters define their stable type/loadout IDs. */
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FName SubjectId;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FName SlotId;
	bool IsWellFormed() const;
	bool operator==(const FGuLiWeaponBindingKey& Other) const;
	static FGuLiWeaponBindingKey Army(uint32 Epoch, EGuLiTeam InTeam, uint16 UnitTypeId, FName WeaponSlot);
	static FGuLiWeaponBindingKey Wingman(uint32 Epoch, EGuLiTeam InTeam,
		const FGuid& InOwnerPlayerGuid, FName WingmanTypeId, FName WeaponSlot);
};

GULISTRIKE_API uint32 GetTypeHash(const FGuLiWeaponBindingKey& Key);

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWeaponChannelView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FGuLiWeaponBindingKey Binding;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FName SkillId;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") TArray<FName> CompatibleSkillIds;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") bool bUnlocked = false;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") bool bEquipped = false;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") float Damage = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") float AttackRatePerSecond = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") float RangeCentimeters = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") int64 LoadoutRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") int64 ProfileRevision = 0;
};

UENUM(BlueprintType)
enum class EGuLiWeaponChangeStatus : uint8 { Rejected, AwaitingCommit, Committed };

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWeaponChangeResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FGuLiWeaponBindingKey Binding;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FName RequestedSkillId;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") EGuLiWeaponChangeStatus Status = EGuLiWeaponChangeStatus::Rejected;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") int64 LoadoutRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category="Weapon") FString Message;
};

/** Optional authoring override; missing rules preserve existing table defaults. */
USTRUCT()
struct GULISTRIKE_API FGuLiArmyWeaponSlotRule
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Weapon") int32 UnitTypeId = 1;
	UPROPERTY(EditAnywhere, Category="Weapon") FName SlotId = TEXT("BasicAttack");
	UPROPERTY(EditAnywhere, Category="Weapon") bool bInitiallyUnlocked = true;
};
