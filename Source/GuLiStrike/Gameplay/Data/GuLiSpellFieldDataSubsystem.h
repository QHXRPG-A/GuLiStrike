#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "GuLiSpellFieldDataSubsystem.generated.h"

class UDataTable;

UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiSpellFieldDataSettings final : public UObject
{
	GENERATED_BODY()
public:
	UGuLiSpellFieldDataSettings();
	UPROPERTY(Config, EditAnywhere, Category="Spell Fields") TSoftObjectPtr<UDataTable> DataTable;
};

/** Global, read-only field catalog. It has no Commander, Ship or mining ownership. */
UCLASS()
class GULISTRIKE_API UGuLiSpellFieldDataSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	const FGuLiSpellFieldConfig* FindCombatField(FName ConfigId) const;
	const FGuLiTeleportFieldConfig* FindTeleportField(int32 Level) const;
	const TArray<FGuLiSpellFieldConfig>& GetCombatFields() const { return CombatFields; }
	bool IsCatalogValid() const { return CatalogError.IsEmpty(); }
	bool IsTeleportCatalogValid() const { return TeleportFields.Num() == 4; }
	const FString& GetCatalogError() const { return CatalogError; }
	/** Asset validation has no World; it uses the same row resolver as the per-World cache. */
	static bool ResolveAuthoredConfig(FName ConfigId, FGuLiSpellFieldConfig& OutConfig);
	UFUNCTION(BlueprintPure, Category="Spell Fields")
	bool GetCombatField(FName ConfigId, FGuLiSpellFieldConfig& OutConfig) const;
private:
	UPROPERTY(Transient) TArray<FGuLiSpellFieldConfig> CombatFields;
	UPROPERTY(Transient) TArray<FGuLiTeleportFieldConfig> TeleportFields;
	FString CatalogError;
};
