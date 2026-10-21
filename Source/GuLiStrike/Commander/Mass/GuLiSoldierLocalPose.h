#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Presentation/GuLiMechanicalAnimation.h"
#include "Gameplay/Presentation/GuLiVATAnimation.h"

/** Read-only local authority pose. Never serialized, predicted or written back by presentation. */
struct FGuLiSoldierLocalPose
{
	FTransform Transform = FTransform::Identity;
	FVector Velocity = FVector::ZeroVector;
	FGuLiMechanicalAnimationState Mechanical;
	FGuLiVATPlayback VAT;
	double ServerTimeSeconds = 0.0;
	double AnimationAheadSeconds = 0.0;
	uint32 ActiveOrderId = 0;
};
