#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "GuLiEngineeringAIController.generated.h"

/** Actor possession and default navigation filter; Mass owns ground movement and avoidance. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiEngineeringAIController : public AAIController
{
    GENERATED_BODY()
public:
    AGuLiEngineeringAIController(const FObjectInitializer& Initializer = FObjectInitializer::Get());
};
