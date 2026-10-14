#pragma once
#include "CoreMinimal.h"
class UNiagaraComponent;
class UWorld;
enum class EGuLiCommanderLODLevel : uint8;

namespace GuLiClientPresentation
{
 GULISTRIKE_API bool CanPresent(const UWorld* World);
 GULISTRIKE_API bool OffscreenLifecycleEnabled();
 GULISTRIKE_API bool OffscreenUnitsEnabled();
 GULISTRIKE_API bool OffscreenFlightsEnabled();
 GULISTRIKE_API bool ThreeTierEffectsEnabled();
 GULISTRIKE_API void ApplyEndpointDetail(UNiagaraComponent* Beam, EGuLiCommanderLODLevel Level);
 constexpr double OffscreenGraceSeconds = .15;
 constexpr double OffscreenUnitInterval = .2;
}
