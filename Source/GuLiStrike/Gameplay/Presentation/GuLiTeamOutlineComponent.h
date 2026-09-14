// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Components/ActorComponent.h"
#include "GuLiTeamOutlineComponent.generated.h"

class UGuLiCombatHealthComponent;

/** Reusable cosmetic team mask. Each player's camera selects the enemy stencil. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiTeamOutlineComponent : public UActorComponent
{

	GENERATED_BODY()
public:
	UGuLiTeamOutlineComponent();
	UFUNCTION(BlueprintCallable, Category="Presentation|Outline")
	void SetOutlineTeam(EGuLiTeam NewTeam);
	/** Call after adding or replacing a unit's visual mesh components. */
	UFUNCTION(BlueprintCallable, Category="Presentation|Outline")
	void RefreshMeshes();
	UFUNCTION(BlueprintPure, Category="Presentation|Outline")
	EGuLiTeam GetOutlineTeam() const { return OutlineTeam; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(EditAnywhere, Category="Presentation|Outline")
	EGuLiTeam OutlineTeam = EGuLiTeam::Unassigned;
	UPROPERTY(Transient)
	TObjectPtr<UGuLiCombatHealthComponent> HealthSource;
};
