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

	/** Authored observation targets may remain idle while automatic fire is still independently allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment")
	bool bStartIdle = false;

    /** Replace the same number of automatic slots of this team in population manifests. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment|Manifest")
    bool bUseInPopulationManifest = false;
    /** Zero uses the unit definition. Applies only to this authored initial deployment. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment|Manifest", meta=(ClampMin="0"))
    float InitialMaxHealthOverride = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Deployment|Manifest", meta=(ClampMin="0"))
    float InitialShield = 0;

	FVector GetSlotLocation(int32 SlotIndex) const;
};
