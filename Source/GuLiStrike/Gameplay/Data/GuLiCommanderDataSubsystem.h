// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
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

	const FGuLiSoldierDefinition& GetDefaultSoldierDefinition() const
	{
		return DefaultSoldierDefinition;
	}

	/** True only when every cached value, including the model, came from a valid DataTable row. */
	bool IsDefaultSoldierDefinitionFromDataTable() const
	{
		return bDefaultSoldierDefinitionFromDataTable;
	}

private:
	UPROPERTY(Transient)
	FGuLiSoldierDefinition DefaultSoldierDefinition;

	bool bDefaultSoldierDefinitionFromDataTable = false;
};
