#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "GuLiUnitDataSubsystem.generated.h"

class UDataTable;

/** Soldiers is the shared catalog for every Commander-controllable unit. */
UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiUnitDataSettings final : public UObject
{
	GENERATED_BODY()
public:
	UGuLiUnitDataSettings();
	UPROPERTY(Config, EditAnywhere, Category="Units") TSoftObjectPtr<UDataTable> SoldierDataTable;
	UPROPERTY(Config, EditAnywhere, Category="Units") FName DefaultSoldierRowName = TEXT("DefaultSoldier");
};

UCLASS()
class GULISTRIKE_API UGuLiUnitDataSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	const FGuLiSoldierDefinition* FindDefinition(uint16 UnitTypeId) const;
	const TArray<FGuLiSoldierDefinition>& GetDefinitions() const { return Definitions; }
	const TArray<FGuLiSoldierDefinition>& GetMassDefinitions() const { return MassDefinitions; }
	const FGuLiSoldierDefinition& GetDefaultDefinition() const { return DefaultDefinition; }
	bool IsDefaultFromTable() const { return bDefaultFromTable; }
	bool IsCatalogValid() const { return CatalogError.IsEmpty(); }
	const FString& GetCatalogError() const { return CatalogError; }
	UFUNCTION(BlueprintPure, Category="Units")
	bool GetUnitDefinition(int32 UnitTypeId, FGuLiSoldierDefinition& OutDefinition) const;
private:
	UPROPERTY(Transient) TArray<FGuLiSoldierDefinition> Definitions;
	UPROPERTY(Transient) TArray<FGuLiSoldierDefinition> MassDefinitions;
	UPROPERTY(Transient) FGuLiSoldierDefinition DefaultDefinition;
	bool bDefaultFromTable = false;
	FString CatalogError;
};
