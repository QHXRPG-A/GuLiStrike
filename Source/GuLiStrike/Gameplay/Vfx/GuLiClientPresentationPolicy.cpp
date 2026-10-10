#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Commander/Presentation/GuLiCommanderLODPolicy.h"

static TAutoConsoleVariable<int32> GOffscreenLifecycle(TEXT("gs.Effects.OffscreenLifecycle"), 1,
 TEXT("Retire registered transient effects after 0.15 seconds fully outside eligible views."));
static TAutoConsoleVariable<int32> GOffscreenUnits(TEXT("gs.Units.Offscreen5Hz"), 1,
 TEXT("Stagger ordinary offscreen unit presentation at 5 Hz; network clocks stay current."));
static TAutoConsoleVariable<int32> GThreeTierEffects(TEXT("gs.Effects.ThreeTierLOD"), 1,
 TEXT("Select authored full/reduced/minimal effect detail using the shared local-view policy."));
static TAutoConsoleVariable<int32> GOffscreenFlights(TEXT("gs.Flights.OffscreenPresentation"),1,
 TEXT("Skip offscreen mesh transforms and flight Niagara updates; prediction and reliable lifecycle remain active."));
bool GuLiClientPresentation::OffscreenLifecycleEnabled() { return GOffscreenLifecycle.GetValueOnGameThread()!=0; }
bool GuLiClientPresentation::OffscreenUnitsEnabled() { return GOffscreenUnits.GetValueOnGameThread()!=0; }
bool GuLiClientPresentation::OffscreenFlightsEnabled() { return GOffscreenFlights.GetValueOnGameThread()!=0; }
bool GuLiClientPresentation::ThreeTierEffectsEnabled() { return GThreeTierEffects.GetValueOnGameThread()!=0; }
void GuLiClientPresentation::ApplyEndpointDetail(UNiagaraComponent* Beam, EGuLiCommanderLODLevel Level)
{
 if (!Beam || !Beam->GetAsset()) return;
 const float Factor=!ThreeTierEffectsEnabled() || Level==EGuLiCommanderLODLevel::Full ? 1.f
  : Level==EGuLiCommanderLODLevel::Reduced ? .5f : 0.f;
 for (const TCHAR* Emitter:{TEXT("Spark"),TEXT("Spark001")})
 {
  const float Rate=UGuLiVfxRegistrySubsystem::NiagaraFloat(Beam->GetAsset(),FName(FString(TEXT("User.GuLiEndpointBaseSpawnRate_"))+Emitter),-1);
  if (Rate>=0)
  {
   Beam->SetVariableFloat(FName(FString(TEXT("User.EndpointSpawnRate_"))+Emitter),Rate*Factor);
   Beam->SetVariableInt(FName(FString(TEXT("User.EndpointSpawnCount_"))+Emitter),FMath::RoundToInt(Rate*Factor));
  }
 }
}
