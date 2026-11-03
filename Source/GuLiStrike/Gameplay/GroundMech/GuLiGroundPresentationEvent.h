#pragma once
#include "CoreMinimal.h"

/** Local presentation facts only. Never serialized or used to activate gameplay. */
enum class EGuLiGroundPresentationAction:uint8 { Fire, Aim, Blink, Reset };
enum class EGuLiGroundPresentationPhase:uint8 { Started, Ended, Ready };
struct FGuLiGroundPresentationEvent
{
    EGuLiGroundPresentationAction Action;
    EGuLiGroundPresentationPhase Phase=EGuLiGroundPresentationPhase::Started;
    float Age=0;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiGroundPresentationChanged,const FGuLiGroundPresentationEvent&);
