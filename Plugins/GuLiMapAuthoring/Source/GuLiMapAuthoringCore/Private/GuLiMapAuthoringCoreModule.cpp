#include "Modules/ModuleManager.h"
#include "GuLiMapAuthoring.h"

class FGuLiMapAuthoringCoreModule : public IModuleInterface
{
    void StartupModule() override { GuLiMap::RegisterBuiltInGeometry(); }
    void ShutdownModule() override { GuLiMap::ShutdownRegistries(); }
};
IMPLEMENT_MODULE(FGuLiMapAuthoringCoreModule, GuLiMapAuthoringCore)
