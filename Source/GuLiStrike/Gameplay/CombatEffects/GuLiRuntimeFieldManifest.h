#pragma once
#include "Network/GuLiHybridManifest.h"

namespace GuLiRuntimeFields
{
    /** Derive the admission, settlement and retirement rules for this exact damage source. */
    GULISTRIKE_API bool AddRules(FGuLiHybridManifestData& Data,const FGuLiHybridRuleEntry& Damage,FString& Error);
}
