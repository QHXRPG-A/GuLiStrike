// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GuLiCommanderSoldierDefinition.generated.h"

class UStaticMesh;
class UStateTree;
class AActor;

/**
 * Validated runtime values for one Commander Soldier type.
 *
 * This is the common boundary consumed by authority simulation, presentation and
 * runtime tuning. Values copied here have already passed DataTable validation.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiSoldierDefinition
{
	GENERATED_BODY()

	/** Stable Soldiers table Id; zero is reserved for invalid definitions. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Soldier")
	uint16 UnitTypeId = 1u;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Units")
	FText DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier", meta = (Units = "cm/s"))
	float MovementSpeedCmPerSecond = 720.0f;

	/** Model-width override; zero keeps the existing Mass default. No extra clearance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier", meta = (Units = "cm"))
	float MassAvoidanceRadiusCm = 0.0f;

	float GetMassAvoidanceRadius(const float DefaultRadius) const
	{
		return MassAvoidanceRadiusCm > 0.0f ? MassAvoidanceRadiusCm : DefaultRadius;
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	TObjectPtr<UStaticMesh> Model = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	float Defense = 0.0f;

	/** Empty for Mass models; otherwise the table selects the controllable Actor implementation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Units")
	TSubclassOf<AActor> ActorClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Units")
	TSubclassOf<AActor> PresentationClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Units")
	float PresentationScale = 0.2f;

	/** Required behavior asset selected by Soldiers.StateTreeAsset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Units")
	TObjectPtr<UStateTree> StateTreeAsset = nullptr;

	/** Source mesh/socket coordinates stay authored; these are final gameplay-centimeter queries. */
	FTransform MakeModelTransform(const FTransform& LogicalPose) const;
	FBox GetModelBoundsCentimeters() const;
	FVector ResolveModelOffsetCentimeters(const FVector& AuthoredOffset) const;

	bool UsesMass() const { return !ActorClass; }
};
