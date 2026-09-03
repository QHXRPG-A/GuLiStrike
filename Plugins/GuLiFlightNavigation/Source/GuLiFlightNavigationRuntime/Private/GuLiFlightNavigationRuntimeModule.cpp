#include "GuLiFlightNavigationLog.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogGuLiFlightNav);

class FGuLiFlightNavigationRuntimeModule final : public IModuleInterface
{
};

IMPLEMENT_MODULE(FGuLiFlightNavigationRuntimeModule, GuLiFlightNavigationRuntime)
