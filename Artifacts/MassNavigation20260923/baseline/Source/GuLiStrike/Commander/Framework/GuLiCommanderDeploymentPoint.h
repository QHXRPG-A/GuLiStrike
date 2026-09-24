// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GameFramework/Actor.h"
#include "GuLiCommanderDeploymentPoint.generated.h"

/** Map-authored initial Mass deployment. Maps without these points keep the default armies. */
UCLASS(BlueprintType)
class GULISTRIKE_API AGuLiCommanderDeploymentPoint : public AActor
{
	GENERATED_BODY()

public:
	AGuLiCommanderDeploymentPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment")
	EGuLiTeam Team = EGuLiTeam::Red;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment")
	int32 UnitTypeId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment")
	int32 Rows = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment")
	int32 Columns = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment", meta=(Units="cm"))
	float SpacingCentimeters = 480.0f;

	/** Initial server-side fire permission; passive targets still take damage normally. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment")
	bool bAllowAutomaticFire = true;

	FVector GetSlotLocation(int32 SlotIndex) const;
};
