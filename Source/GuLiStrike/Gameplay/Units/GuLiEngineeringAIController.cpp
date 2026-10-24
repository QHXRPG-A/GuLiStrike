#include "Gameplay/Units/GuLiEngineeringAIController.h"
#include "Navigation/PathFollowingComponent.h"

AGuLiEngineeringAIController::AGuLiEngineeringAIController(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    PrimaryActorTick.bCanEverTick=false;
    GetPathFollowingComponent()->PrimaryComponentTick.bCanEverTick=false;
}
