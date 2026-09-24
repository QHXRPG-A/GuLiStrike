#pragma once
#include "CoreMinimal.h"
#include "GuLiUnitTaskSettings.generated.h"

/** Command policy is independent of the selected behavior tree. Keep the existing config class name. */
UCLASS(Config=Game, DefaultConfig)
class UGuLiUnitTaskSettings final : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="1", ClampMax="32")) int32 MaximumManualTasks = 32;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="0.1")) float AutomaticRetrySeconds = 1;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="0.05")) float DoublePressSeconds = .3f;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="1")) float SameTypeRadiusCentimeters = 50000;
	UPROPERTY(Config, EditAnywhere, meta=(ClampMin="0", Units="cm")) float MoveReuseDistanceCentimeters = 2500;
};
