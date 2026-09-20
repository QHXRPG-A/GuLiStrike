#pragma once
#include "CoreMinimal.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "GuLiSpecialTaskExecutor.generated.h"
struct FGuLiSoldierDefinition;

/** World-owned shared strategy; all per-unit mutable state belongs to FGuLiTaskExecution. */
UCLASS(Abstract, NotBlueprintable)
class GULISTRIKE_API UGuLiSpecialTaskExecutor : public UObject
{
	GENERATED_BODY()
public:
	virtual bool SupportsDefinition(FGameplayTag Tag, const FGuLiSoldierDefinition& Unit) const { return false; }
	virtual bool Validate(UWorld& World, const FGuLiTaskUnitContext& Unit, const FGuLiUnitTaskCommand& Command, FString& Error) const { return false; }
	virtual bool BuildAutomatic(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiUnitTaskCommand& Command) const { return false; }
	virtual EGuLiTaskStatus Start(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task) const { return EGuLiTaskStatus::Failed; }
	virtual EGuLiTaskStatus Poll(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task) const { return EGuLiTaskStatus::Failed; }
	virtual bool Cancel(UWorld& World, const FGuLiTaskUnitContext& Unit, FGuLiTaskExecution& Task) const { return true; }
	virtual bool RepeatsWhenLast() const { return false; }
};

UCLASS()
class GULISTRIKE_API UGuLiMiningSpecialTaskExecutor final : public UGuLiSpecialTaskExecutor
{
	GENERATED_BODY()
public:
	virtual bool SupportsDefinition(FGameplayTag Tag, const FGuLiSoldierDefinition& Unit) const override;
	virtual bool Validate(UWorld&, const FGuLiTaskUnitContext&, const FGuLiUnitTaskCommand&, FString&) const override;
	virtual bool BuildAutomatic(UWorld&, const FGuLiTaskUnitContext&, FGuLiUnitTaskCommand&) const override;
	virtual EGuLiTaskStatus Start(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
	virtual EGuLiTaskStatus Poll(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
	virtual bool Cancel(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
	virtual bool RepeatsWhenLast() const override { return true; }
};

UCLASS()
class GULISTRIKE_API UGuLiConstructionSpecialTaskExecutor final : public UGuLiSpecialTaskExecutor
{
	GENERATED_BODY()
public:
	virtual bool SupportsDefinition(FGameplayTag Tag, const FGuLiSoldierDefinition& Unit) const override;
	virtual bool Validate(UWorld&, const FGuLiTaskUnitContext&, const FGuLiUnitTaskCommand&, FString&) const override;
	virtual bool BuildAutomatic(UWorld&, const FGuLiTaskUnitContext&, FGuLiUnitTaskCommand&) const override;
	virtual EGuLiTaskStatus Start(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
	virtual EGuLiTaskStatus Poll(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
	virtual bool Cancel(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
};

UCLASS()
class GULISTRIKE_API UGuLiStrongholdAdvanceSpecialTaskExecutor final : public UGuLiSpecialTaskExecutor
{
	GENERATED_BODY()
public:
	virtual bool SupportsDefinition(FGameplayTag Tag, const FGuLiSoldierDefinition& Unit) const override;
	virtual bool BuildAutomatic(UWorld&, const FGuLiTaskUnitContext&, FGuLiUnitTaskCommand&) const override;
	virtual EGuLiTaskStatus Start(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
	virtual EGuLiTaskStatus Poll(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
	virtual bool Cancel(UWorld&, const FGuLiTaskUnitContext&, FGuLiTaskExecution&) const override;
};
