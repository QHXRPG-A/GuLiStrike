// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiCommanderDataSubsystem.generated.h"

/**
 * Per-game-world cache for Commander static data.
 *
 * A server world and each client world resolve their own baseline exactly once during
 * subsystem initialization. Consumers only read the validated cached definition.
 */
UCLASS()
class GULISTRIKE_API UGuLiCommanderDataSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	const TArray<FGuLiSoldierDefinition>& GetSoldierDefinitions() const;
	const FGuLiSoldierDefinition* FindSoldierDefinition(uint16 UnitTypeId) const;
	const TArray<FGuLiSkillDefinition>& GetSkillDefinitions() const { return SkillDefinitions; }
	const TArray<FGuLiUnitSkillConfig>& GetUnitSkillConfigs() const { return UnitSkillConfigs; }
	const FGuLiSkillDefinition* FindSkillDefinition(FName SkillId) const;
	const FGuLiTeleportFieldConfig* FindTeleportFieldConfig(int32 Level) const;
	bool IsTeleportCatalogValid() const;
	const FGuLiSpellFieldConfig* FindSpellFieldConfig(FName ConfigId) const;
	const TArray<FGuLiSpellFieldConfig>& GetSpellFieldConfigs() const;
	const FGuLiWeaponMountConfig* FindWeaponMountConfig(uint16 UnitTypeId, FName SlotId) const;
	const FVector* FindAimOffset(uint16 UnitTypeId) const;
	const TArray<FGuLiWeaponMountConfig>& GetWeaponMountConfigs() const { return WeaponMountConfigs; }
	bool IsSkillCatalogValid() const { return SkillCatalogError.IsEmpty(); }
	const FString& GetSkillCatalogError() const { return SkillCatalogError; }
	bool IsSpellFieldCatalogValid() const;
	const FString& GetSpellFieldCatalogError() const;
	bool IsWeaponMountCatalogValid() const { return WeaponMountCatalogError.IsEmpty(); }
	const FString& GetWeaponMountCatalogError() const { return WeaponMountCatalogError; }

	const FGuLiSoldierDefinition& GetDefaultSoldierDefinition() const;

	/** True only when every cached value, including the model, came from a valid DataTable row. */
	bool IsDefaultSoldierDefinitionFromDataTable() const;

private:
	UPROPERTY(Transient) TObjectPtr<class UGuLiUnitDataSubsystem> Units;
	UPROPERTY(Transient) TObjectPtr<class UGuLiSpellFieldDataSubsystem> Fields;

	void LoadSkillCatalog(const class UGuLiCommanderDataSettings* Settings);
	void LoadWeaponMountCatalog(const class UGuLiCommanderDataSettings* Settings);

	UPROPERTY(Transient) TArray<FGuLiSkillDefinition> SkillDefinitions;
	UPROPERTY(Transient) TArray<FGuLiUnitSkillConfig> UnitSkillConfigs;

	UPROPERTY(Transient) TArray<FGuLiWeaponMountConfig> WeaponMountConfigs;
	FString SkillCatalogError;

	FString WeaponMountCatalogError;

};
