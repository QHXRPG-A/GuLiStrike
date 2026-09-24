// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "GuLiPlacedBuilding.generated.h"

class UBoxComponent;
class UNavModifierComponent;
class UStaticMeshComponent;

/** Building instance; authored definition, lifecycle and individual functions have separate owners. */
UCLASS(NotBlueprintable)
class GULISTRIKE_API AGuLiPlacedBuilding : public AActor, public IGuLiBuildingOwner
{
	GENERATED_BODY()

public:
	AGuLiPlacedBuilding();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority-only initialization used before FinishSpawning. */
	bool InitializeBuilding(
		EGuLiBuildingType InBuildingType,
		EGuLiTeam InTeam,
		const FGuid& InBuilderPlayerGuid,
		const FGuLiBuildingDefinition& Definition);

	EGuLiBuildingType GetBuildingType() const { return BuildingType; }
	EGuLiTeam GetBuildingTeam() const { return Team; }
	virtual void SetBuildingTeamAuthority(EGuLiTeam NewTeam) override;
	virtual FVector GetBuildingGroundLocation() const override;
	bool InitializeFromDefinition(int32 DefinitionId, EGuLiTeam InTeam, const FGuid& BuilderGuid,
		int32 TerritoryIndex, EGuLiBuildingOrigin Origin, bool bCompleted);
	const FGuid& GetBuilderPlayerGuid() const { return BuilderPlayerGuid; }
	UBoxComponent* GetBuildingCollision() const { return CollisionRoot; }
	UStaticMeshComponent* GetBuildingVisual() const { return VisualMesh; }
	UNavModifierComponent* GetNavigationModifier() const { return NavigationModifier; }

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
	UPROPERTY(ReplicatedUsing=OnRep_BuildingState) int32 DefinitionId = 0;
	void ApplyDefinition(const FGuLiBuildingDefinition& Definition);

	UFUNCTION()
	void OnRep_BuildingState();

	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UBoxComponent> CollisionRoot;

	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UNavModifierComponent> NavigationModifier;

	UPROPERTY(ReplicatedUsing = OnRep_BuildingState, VisibleInstanceOnly, Category = "Building")
	EGuLiBuildingType BuildingType = EGuLiBuildingType::MissileTurret;

	UPROPERTY(ReplicatedUsing = OnRep_BuildingState, VisibleInstanceOnly, Category = "Building")
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(ReplicatedUsing = OnRep_BuildingState, VisibleInstanceOnly, Category = "Building")
	FGuid BuilderPlayerGuid;
};
