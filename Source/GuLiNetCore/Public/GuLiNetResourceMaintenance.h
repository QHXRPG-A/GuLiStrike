#pragma once
#include "GuLiNetGameplayTypes.h"

namespace GuLiNet
{
inline constexpr const char* ResourceMaintenanceCapability="resource_maintenance";
inline constexpr std::uint32_t ResourceMaintenanceVersion=1;
struct ResourceMaintenanceState
{
    Tick Period=1800;
    std::array<Tick,2> Next{1800,1800};
};
GULINETCORE_API bool GetResourceMaintenance(const ObjectRecord&,ResourceMaintenanceState&);
GULINETCORE_API bool ReadResourceMaintenance(Reader&,ResourceMaintenanceState&);
GULINETCORE_API bool SetResourceMaintenance(ObjectRecord&,const ResourceMaintenanceState&);
}
