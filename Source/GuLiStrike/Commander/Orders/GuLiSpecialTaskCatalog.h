#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "GuLiSpecialTaskCatalog.generated.h"
class UDataTable;

UCLASS(Config=Game, DefaultConfig)
class UGuLiUnitTaskSettings final : public UObject
{
	GENERATED_BODY()
public:
	UGuLiUnitTaskSettings();
	UPROPERTY(Config, EditAnywhere) TSoftObjectPtr<UDataTable> SpecialTaskTable;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="1", ClampMax="32")) int32 MaximumManualTasks = 32;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="0.1")) float AutomaticRetrySeconds = 1;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="0.05")) float DoublePressSeconds = .3f;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="1")) float SameTypeRadiusCentimeters = 50000;
	/** Ground-plane distance within which a replacement keeps the current move. */
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="0", Units="cm")) float MoveReuseDistanceCentimeters = 2500;
};

UCLASS()
class GULISTRIKE_API UGuLiSpecialTaskCatalog final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	bool IsValidCatalog() const { return Error.IsEmpty(); }
	const FString& GetError() const { return Error; }
	const TArray<FGuLiSpecialTaskDefinition>& GetDefinitions() const { return Definitions; }
	const FGuLiSpecialTaskDefinition* Find(int32 Id) const;
	const FGuLiSpecialTaskDefinition* Find(FGameplayTag Tag, uint16 UnitTypeId) const;
	/** Shared loader used by tests and startup; errors leave an empty catalog. */
	bool Load(UDataTable* Table);
private:
	TArray<FGuLiSpecialTaskDefinition> Definitions;
	FString Error;
	UPROPERTY(Transient) TArray<TObjectPtr<UGuLiSpecialTaskExecutor>> Executors;
};
